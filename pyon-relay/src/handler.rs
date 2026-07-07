use std::sync::Arc;

use pyon_core::identity::Identity;
use serde_json::Value;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::{mpsc, Mutex};

use crate::protocol;
use crate::server::RelayServer;

pub async fn handle_connection(
    server: Arc<Mutex<RelayServer>>,
    stream: TcpStream,
    peer_addr: String,
) {
    stream.set_nodelay(true).ok();
    let peer = if peer_addr.is_empty() { "?" } else { &peer_addr };

    let (mut reader, mut writer) = stream.into_split();
    let (tx, mut rx) = mpsc::unbounded_channel::<Vec<u8>>();

    let write_task = tokio::spawn(async move {
        while let Some(data) = rx.recv().await {
            if writer.write_all(&data).await.is_err() {
                break;
            }
        }
    });

    let frame = match read_frame(&mut reader).await {
        Ok(Some(f)) => f,
        Ok(None) => {
            tracing::warn!("handshake: conexão de {} fechou sem enviar nada", peer);
            return;
        }
        Err(e) => {
            tracing::warn!("handshake: erro de leitura de {}: {}", peer, e);
            return;
        }
    };

    if frame.type_ != protocol::TYPE_HANDSHAKE {
        tracing::warn!("handshake: tipo inesperado '{}' de {}", frame.type_, peer);
        let mut p = protocol::Payload::new();
        p.insert("ok".into(), "false".into());
        p.insert("reason".into(), "esperava handshake".into());
        let raw = protocol::build_signed(protocol::TYPE_HANDSHAKE_ACK, p, &server.lock().await.identity);
        let _ = tx.send(raw);
        return;
    }

    let pubkey = frame.payload.get("pubkey").cloned().unwrap_or_default();
    let channel = frame
        .payload
        .get("channel")
        .cloned()
        .unwrap_or_else(|| "geral".into());
    let name = frame
        .payload
        .get("name")
        .cloned()
        .unwrap_or_else(|| format!("anon:{}", &pubkey[..pubkey.len().min(8)]));

    tracing::info!(
        "handshake: {} ({}…) → #{} de {}",
        name,
        &pubkey[..pubkey.len().min(12)],
        channel,
        peer
    );

    if let Err(e) = protocol::verify(&frame) {
        tracing::warn!(
            "handshake: assinatura inválida de {} ({}…): {}",
            name,
            &pubkey[..pubkey.len().min(12)],
            e
        );
        let mut p = protocol::Payload::new();
        p.insert("ok".into(), "false".into());
        p.insert("reason".into(), e.into());
        let raw = protocol::build_signed(protocol::TYPE_HANDSHAKE_ACK, p, &server.lock().await.identity);
        let _ = tx.send(raw);
        return;
    }

    {
        let srv = server.lock().await;
        if srv.is_banned(&pubkey) {
            tracing::warn!(
                "handshake: acesso negado — {} ({}…) está banide",
                name,
                &pubkey[..pubkey.len().min(12)]
            );
            let mut p = protocol::Payload::new();
            p.insert("ok".into(), "false".into());
            p.insert("reason".into(), "*zap!!* banide (\u{2D8}\u{2D8})".into());
            let raw = protocol::build_signed(protocol::TYPE_HANDSHAKE_ACK, p, &srv.identity);
            let _ = tx.send(raw);
            return;
        }
    }

    {
        let srv = server.lock().await;
        let mut ack = protocol::Payload::new();
        ack.insert("ok".into(), "true".into());
        ack.insert("channel".into(), channel.clone());
        ack.insert(
            "motd".into(),
            format!("*kyaa~!* bem-vinde, {}! (\u{25D5}\u{203F}\u{25D5}\u{2727})", name),
        );
        let raw = protocol::build_signed(protocol::TYPE_HANDSHAKE_ACK, ack, &srv.identity);
        let _ = tx.send(raw);

        srv.announce_board_to(&tx, &channel);
        srv.replay_chat_to(&tx, &channel, &pubkey);
    }

    {
        let mut srv = server.lock().await;
        srv.register_client(pubkey.clone(), name.clone(), channel.clone(), tx.clone());
        let online = srv.online_in_channel(&channel);
        tracing::info!(
            "[#{}] {} conectou ({} online no canal)",
            channel,
            name,
            online
        );
        srv.broadcast_sys(
            &channel,
            &format!("*pyon!* {} entrou! (\u{FF89}\u{25D5}\u{203F}\u{25D5})\u{FF89}*:\u{30FB}\u{FF9F}\u{2727}", name),
            Some(&pubkey),
        );
    }

    loop {
        match read_frame(&mut reader).await {
            Ok(Some(frame)) => {
                process_frame(&server, &frame, &pubkey, &name, &channel, &tx).await;
            }
            Ok(None) => break,
            Err(e) => {
                tracing::warn!("[#{}] {} desconectou com erro: {}", channel, name, e);
                break;
            }
        }
    }

    {
        let mut srv = server.lock().await;
        srv.unregister_client(&pubkey);
        let remaining = srv.online_in_channel(&channel);
        tracing::info!(
            "[#{}] {} saiu ({} online no canal)",
            channel,
            name,
            remaining
        );
        srv.broadcast_sys(
            &channel,
            &format!("*plop...* {} saiu. (\u{1F464}\u{2022}\u{301}\u{30C1}\u{2022}\u{30CD}\u{1F464})", name),
            Some(&pubkey),
        );
    }

    drop(tx);
    write_task.await.ok();
}

async fn process_frame(
    server: &Arc<Mutex<RelayServer>>,
    frame: &protocol::Frame,
    from_pubkey: &str,
    from_name: &str,
    channel: &str,
    tx: &mpsc::UnboundedSender<Vec<u8>>,
) {
    if frame.payload.contains_key("pubkey") {
        if let Err(e) = protocol::verify(frame) {
            tracing::warn!(
                "sig inválida de {} ({}…): {}",
                from_name,
                &from_pubkey[..from_pubkey.len().min(12)],
                e
            );
            return;
        }
    }

    {
        let srv = server.lock().await;
        if srv.is_banned(from_pubkey) {
            return;
        }
    }

    tracing::debug!("frame: {} de {} em #{}", frame.type_, from_name, channel);

    match frame.type_.as_str() {
        protocol::TYPE_PING => {
            let srv = server.lock().await;
            let mut p = protocol::Payload::new();
            p.insert("ts".into(), chrono::Utc::now().timestamp().to_string());
            let raw = protocol::build_signed(protocol::TYPE_PONG, p, &srv.identity);
            let _ = tx.send(raw);
        }

        protocol::TYPE_CHAT_MESSAGE => {
            let body = frame
                .payload
                .get("body")
                .cloned()
                .unwrap_or_default()
                .trim()
                .to_string();
            if body.is_empty() {
                tracing::debug!("chat_message vazia de {} — ignorada", from_name);
                return;
            }
            if body.len() > 2048 {
                tracing::warn!(
                    "chat_message de {} muito grande ({} bytes) — ignorada",
                    from_name,
                    body.len()
                );
                return;
            }

            let ts = chrono::Utc::now().timestamp().to_string();
            let srv = server.lock().await;
            let online = srv.online_in_channel(channel);
            let body_preview = if body.len() > 80 {
                format!("{}…", &body[..80])
            } else {
                body.clone()
            };
            tracing::info!(
                "[#{}] {}: {} (broadcast para {} peers)",
                channel,
                from_name,
                body_preview,
                online.saturating_sub(1)
            );

            // Persist
            let record = serde_json::json!({
                "_id": format!("{}:{}:chat", ts, from_pubkey),
                "type": "chat_message",
                "channel": channel,
                "from_pubkey": from_pubkey,
                "from_name": from_name,
                "body": body.clone(),
                "ts": ts,
            });
            srv.save_chat(&record);

            let mut p = protocol::Payload::new();
            p.insert("from_pubkey".into(), from_pubkey.into());
            p.insert("from_name".into(), from_name.into());
            p.insert("body".into(), body);
            p.insert("ts".into(), ts);
            drop(srv);
            let srv = server.lock().await;
            srv.broadcast(channel, protocol::TYPE_CHAT_MESSAGE, p, Some(from_pubkey));
        }

        protocol::TYPE_CHAT_DM => {
            let to = frame
                .payload
                .get("to_pubkey")
                .cloned()
                .unwrap_or_default();
            let body = frame
                .payload
                .get("body")
                .cloned()
                .unwrap_or_default()
                .trim()
                .to_string();
            tracing::info!(
                "DM: {} → {}… ({} bytes)",
                from_name,
                &to[..to.len().min(12)],
                body.len()
            );

            // Persist
            let ts = chrono::Utc::now().timestamp().to_string();
            let record = serde_json::json!({
                "_id": format!("{}:{}:dm:{}", ts, from_pubkey, to),
                "type": "chat_dm",
                "from_pubkey": from_pubkey,
                "from_name": from_name,
                "to_pubkey": to.clone(),
                "body": body.clone(),
                "ts": ts,
            });
            let srv = server.lock().await;
            srv.save_chat(&record);
            srv.deliver_dm(from_pubkey, from_name, &to, &body);
        }

        protocol::TYPE_POST_ANNOUNCE => {
            let board_val = frame.payload.get("board").cloned().unwrap_or_default();
            let post_id = frame.payload.get("post_id").cloned().unwrap_or_default();
            let ndjson_id = format!("{}:{}", board_val, post_id);

            let srv = server.lock().await;
            if srv.post_exists(&ndjson_id) {
                tracing::debug!("sync: {} já existe — ignorando anúncio", ndjson_id);
            } else {
                tracing::info!(
                    "sync: {} anuncia {} — solicitando…",
                    from_name,
                    ndjson_id
                );
                let mut req = protocol::Payload::new();
                req.insert("board".into(), board_val);
                req.insert("post_id".into(), post_id);
                let raw = protocol::build_signed(protocol::TYPE_POST_REQUEST, req, &srv.identity);
                let _ = tx.send(raw);
            }
        }

        protocol::TYPE_POST_REQUEST => {
            let board_val = frame.payload.get("board").cloned().unwrap_or_default();
            let post_id = frame.payload.get("post_id").cloned().unwrap_or_default();
            let ndjson_id = format!("{}:{}", board_val, post_id);

            let srv = server.lock().await;
            if let Some(post) = srv.load_post(&ndjson_id) {
                tracing::info!("sync: enviando {} para {}", ndjson_id, from_name);
                let payload = RelayServer::post_payload(&post);
                let raw = protocol::build_signed(protocol::TYPE_POST_DATA, payload, &srv.identity);
                let _ = tx.send(raw);
            } else {
                tracing::warn!(
                    "sync: {} pediu {} — não encontrado",
                    from_name,
                    ndjson_id
                );
            }
        }

        protocol::TYPE_POST_DATA => {
            let board_val = frame.payload.get("board").cloned().unwrap_or_default();
            let post_id = frame.payload.get("post_id").cloned().unwrap_or_default();
            let ndjson_id = format!("{}:{}", board_val, post_id);

            if server.lock().await.post_exists(&ndjson_id) {
                return;
            }

            let author_key = frame.payload.get("author").cloned().unwrap_or_default();
            let orig_sig = frame.payload.get("orig_sig").cloned().unwrap_or_default();
            let body = frame.payload.get("body").cloned().unwrap_or_default();
            let subject = frame.payload.get("subject").cloned().unwrap_or_default();

            let canonical = format!("{}|{}|{}|{}", board_val, post_id, body, subject);
            let valid = Identity::verify(&author_key, &orig_sig, canonical.as_bytes()).unwrap_or(false);
            if !valid {
                tracing::warn!(
                    "sync: assinatura inválida do autor em {} (autor: {}…) — descartado",
                    ndjson_id,
                    &author_key[..author_key.len().min(12)]
                );
                return;
            }

            let reply_to: u64 = frame
                .payload
                .get("reply_to")
                .and_then(|s| s.parse().ok())
                .unwrap_or(0);
            let images: Vec<Value> = frame
                .payload
                .get("images")
                .and_then(|s| serde_json::from_str(s).ok())
                .unwrap_or_default();
            let author_name = frame.payload.get("author_name").cloned().unwrap_or_default();
            let created_at = frame.payload.get("created_at").cloned().unwrap_or_default();

            let record = serde_json::json!({
                "_id": ndjson_id,
                "board": board_val,
                "id": post_id.parse::<u64>().unwrap_or(0),
                "author_pubkey": author_key,
                "author_name": author_name,
                "subject": subject,
                "body": body,
                "reply_to": reply_to,
                "images": images,
                "sig": orig_sig,
                "created_at": created_at,
                "received_at": chrono::Utc::now().to_rfc3339(),
                "received_from": from_pubkey,
            });

            {
                let srv = server.lock().await;
                srv.save_post(&record);
            }

            let srv = server.lock().await;
            let mut ann = protocol::Payload::new();
            ann.insert("board".into(), board_val.clone());
            ann.insert("post_id".into(), post_id.clone());
            ann.insert("ts".into(), chrono::Utc::now().timestamp().to_string());
            let announce_raw =
                protocol::build_signed(protocol::TYPE_POST_ANNOUNCE, ann, &srv.identity);

            let targets: Vec<String> = srv
                .clients
                .iter()
                .filter(|(pk, c)| *pk != from_pubkey && c.channel == channel)
                .map(|(pk, _)| pk.clone())
                .collect();

            let count = targets.len();
            tracing::info!(
                "sync: re-anunciando {} para {} peers no canal #{}",
                &format!("{}:{}", board_val, post_id),
                count,
                channel
            );
            for pk in &targets {
                if let Some(client) = srv.clients.get(pk) {
                    let _ = client.tx.send(announce_raw.clone());
                }
            }

            srv.broadcast_sys(
                channel,
                &format!(
                    "*ding~!* novo post em /{}/{} (\u{25D5}\u{203F}\u{25D5}\u{2727})",
                    board_val, post_id
                ),
                Some(from_pubkey),
            );
        }

        _ => {
            tracing::warn!(
                "frame desconhecido: '{}' de {} — ignorado",
                frame.type_,
                from_name
            );
        }
    }
}

async fn read_frame(
    reader: &mut (impl AsyncReadExt + Unpin),
) -> Result<Option<protocol::Frame>, String> {
    let mut len_buf = [0u8; 4];
    match reader.read_exact(&mut len_buf).await {
        Ok(_) => {}
        Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => return Ok(None),
        Err(e) => return Err(format!("erro de leitura: {}", e)),
    }
    let len = u32::from_be_bytes(len_buf) as usize;
    if len > 4 * 1024 * 1024 {
        return Err(format!("frame absurdo: {} bytes", len));
    }
    let mut buf = vec![0u8; len];
    reader.read_exact(&mut buf).await.map_err(|e| format!("erro de leitura: {}", e))?;
    protocol::parse(&buf).map(Some)
}
