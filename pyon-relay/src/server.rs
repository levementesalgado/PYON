use std::collections::HashMap;

use pyon_core::identity::Identity;
use pyon_core::store::Store;
use serde_json::Value;
use tokio::sync::mpsc;

use crate::protocol;

pub struct ClientInfo {
    pub name: String,
    pub channel: String,
    pub tx: mpsc::UnboundedSender<Vec<u8>>,
}

pub struct RelayServer {
    pub clients: HashMap<String, ClientInfo>,
    pub store: Store,
    pub identity: Identity,
}

impl RelayServer {
    pub fn new(store: Store, identity: Identity) -> Self {
        Self {
            clients: HashMap::new(),
            store,
            identity,
        }
    }

    pub fn is_banned(&self, pubkey: &str) -> bool {
        let all: Vec<Value> = self.store.all("bans").unwrap_or_default();
        all.iter().any(|v| v.get("pubkey").and_then(|v| v.as_str()) == Some(pubkey))
    }

    pub fn post_exists(&self, ndjson_id: &str) -> bool {
        self.store.exists("posts", ndjson_id)
    }

    pub fn load_post(&self, ndjson_id: &str) -> Option<Value> {
        let all: Vec<Value> = self.store.all("posts").ok()?;
        all.into_iter().find(|v| v.get("_id").and_then(|v| v.as_str()) == Some(ndjson_id))
    }

    pub fn save_post(&self, record: &Value) {
        if let Some(id) = record.get("_id").and_then(|v| v.as_str()) {
            if !self.post_exists(id) {
                if let Err(e) = self.store.insert("posts", record) {
                    tracing::warn!("falha ao salvar post {}: {}", id, e);
                } else {
                    tracing::info!("post salvo: {} (autor: {})", id,
                        record.get("author_name").and_then(|v| v.as_str()).unwrap_or("?"));
                }
            }
        }
    }

    pub fn next_post_id(&self, board: &str) -> u64 {
        self.store.next_post_id(board)
    }

    pub fn post_payload(post: &Value) -> protocol::Payload {
        let mut p = protocol::Payload::new();
        p.insert("board".into(), post.get("board").and_then(|v| v.as_str()).unwrap_or("").into());
        p.insert("post_id".into(), post.get("id").and_then(|v| v.as_u64()).map(|v| v.to_string()).unwrap_or_default());
        p.insert("body".into(), post.get("body").and_then(|v| v.as_str()).unwrap_or("").into());
        p.insert("subject".into(), post.get("subject").and_then(|v| v.as_str()).unwrap_or("").into());
        p.insert("reply_to".into(), post.get("reply_to").and_then(|v| v.as_u64()).map(|v| v.to_string()).unwrap_or_default());
        p.insert("images".into(), post.get("images").map(|v| v.to_string()).unwrap_or_else(|| "[]".into()));
        p.insert("author".into(), post.get("author_pubkey").and_then(|v| v.as_str()).unwrap_or("").into());
        p.insert("author_name".into(), post.get("author_name").and_then(|v| v.as_str()).unwrap_or("").into());
        p.insert("created_at".into(), post.get("created_at").and_then(|v| v.as_str()).unwrap_or("").into());
        p.insert("orig_sig".into(), post.get("sig").and_then(|v| v.as_str()).unwrap_or("").into());
        p
    }

    pub fn register_client(
        &mut self,
        pubkey: String,
        name: String,
        channel: String,
        tx: mpsc::UnboundedSender<Vec<u8>>,
    ) {
        self.clients.insert(
            pubkey,
            ClientInfo { name, channel, tx },
        );
    }

    pub fn unregister_client(&mut self, pubkey: &str) {
        self.clients.remove(pubkey);
    }

    pub fn online_in_channel(&self, channel: &str) -> usize {
        self.clients
            .values()
            .filter(|c| c.channel == channel)
            .count()
    }

    pub fn broadcast(&self, channel: &str, type_: &str, payload: protocol::Payload, except: Option<&str>) {
        let frame = protocol::build_signed(type_, payload, &self.identity);
        for (pk, client) in &self.clients {
            if client.channel == channel && Some(pk.as_str()) != except {
                let _ = client.tx.send(frame.clone());
            }
        }
    }

    pub fn broadcast_sys(&self, channel: &str, text: &str, except: Option<&str>) {
        let mut p = protocol::Payload::new();
        p.insert("from_pubkey".into(), self.identity.pubkey_hex.clone());
        p.insert("from_name".into(), "\u{2726}sistema\u{2726}".into());
        p.insert("body".into(), text.into());
        p.insert("ts".into(), chrono::Utc::now().timestamp().to_string());
        self.broadcast(channel, protocol::TYPE_CHAT_MESSAGE, p, except);
    }

    pub fn deliver_dm(&self, from_pubkey: &str, from_name: &str, to_pubkey: &str, body: &str) {
        if body.is_empty() || to_pubkey.is_empty() {
            return;
        }
        let src = self.clients.get(from_pubkey);
        let target = self.clients.get(to_pubkey);
        match target {
            Some(t) => {
                let mut dm = protocol::Payload::new();
                dm.insert("from_pubkey".into(), from_pubkey.into());
                dm.insert("from_name".into(), from_name.into());
                dm.insert("body".into(), body.into());
                dm.insert("ts".into(), chrono::Utc::now().timestamp().to_string());
                let frame = protocol::build_signed(protocol::TYPE_CHAT_DM, dm, &self.identity);
                let _ = t.tx.send(frame);

                if let Some(s) = src {
                    let mut sys = protocol::Payload::new();
                    sys.insert("from_pubkey".into(), self.identity.pubkey_hex.clone());
                    sys.insert("from_name".into(), "\u{2726}sistema\u{2726}".into());
                    sys.insert("body".into(), format!("*piiing!* DM para {}! \u{2661}", t.name));
                    sys.insert("ts".into(), chrono::Utc::now().timestamp().to_string());
                    let sys_frame = protocol::build_signed(protocol::TYPE_CHAT_MESSAGE, sys, &self.identity);
                    let _ = s.tx.send(sys_frame);
                }
                tracing::info!("DM entregue: {} \u{2192} {} ({} bytes)", from_name, t.name, body.len());
            }
            None => {
                if let Some(s) = src {
                    let mut sys = protocol::Payload::new();
                    sys.insert("from_pubkey".into(), self.identity.pubkey_hex.clone());
                    sys.insert("from_name".into(), "\u{2726}sistema\u{2726}".into());
                    sys.insert("body".into(), format!("*pip...* {} offline. (\u{1F424}\u{2022}\u{301}\u{30C1}\u{2022}\u{30CD}\u{1F424})",
                        &to_pubkey[..to_pubkey.len().min(12)]));
                    sys.insert("ts".into(), chrono::Utc::now().timestamp().to_string());
                    let sys_frame = protocol::build_signed(protocol::TYPE_CHAT_MESSAGE, sys, &self.identity);
                    let _ = s.tx.send(sys_frame);
                }
                tracing::warn!("DM: destino {} offline", &to_pubkey[..to_pubkey.len().min(12)]);
            }
        }
    }

    pub fn save_chat(&self, record: &Value) {
        let id = record.get("_id").and_then(|v| v.as_str()).unwrap_or("?");
        if let Err(e) = self.store.insert("chat", record) {
            tracing::warn!("falha ao salvar chat {}: {}", id, e);
        }
    }

    pub fn replay_chat_to(&self, io_tx: &mpsc::UnboundedSender<Vec<u8>>, channel: &str, my_pubkey: &str) {
        let all: Vec<Value> = match self.store.all("chat") {
            Ok(v) => v,
            Err(_) => return,
        };
        let board = channel.strip_prefix('#').unwrap_or(channel);
        let mut msgs: Vec<&Value> = all
            .iter()
            .filter(|v| {
                let t = v.get("type").and_then(|v| v.as_str()).unwrap_or("");
                match t {
                    "chat_message" => v.get("channel").and_then(|v| v.as_str()) == Some(channel),
                    "chat_dm" => v.get("to_pubkey").and_then(|v| v.as_str()) == Some(my_pubkey),
                    _ => false,
                }
            })
            .collect();
        msgs.sort_by(|a, b| {
            let ta = a.get("ts").and_then(|v| v.as_str()).unwrap_or("0");
            let tb = b.get("ts").and_then(|v| v.as_str()).unwrap_or("0");
            ta.cmp(tb)
        });
        let recent = if msgs.len() > 100 { &msgs[msgs.len() - 100..] } else { &msgs[..] };
        for msg in recent {
            let t = msg.get("type").and_then(|v| v.as_str()).unwrap_or("");
            let from_name = msg.get("from_name").and_then(|v| v.as_str()).unwrap_or("");
            let body = msg.get("body").and_then(|v| v.as_str()).unwrap_or("");
            let ts = msg.get("ts").and_then(|v| v.as_str()).unwrap_or("0");
            let from_pubkey = msg.get("from_pubkey").and_then(|v| v.as_str()).unwrap_or("");
            let mut p = protocol::Payload::new();
            p.insert("from_pubkey".into(), from_pubkey.into());
            p.insert("from_name".into(), from_name.into());
            p.insert("body".into(), body.into());
            p.insert("ts".into(), ts.into());
            let frame = protocol::build_signed(t, p, &self.identity);
            let _ = io_tx.send(frame);
        }
        if !recent.is_empty() {
            tracing::info!("sync: reenviadas {} mensagens do chat #{}", recent.len(), board);
        }
    }

    pub fn announce_board_to(&self, io_tx: &mpsc::UnboundedSender<Vec<u8>>, channel: &str) {
        let board = channel.strip_prefix('#').unwrap_or(channel);
        let all: Vec<Value> = match self.store.all("posts") {
            Ok(v) => v,
            Err(_) => return,
        };
        let prefix = format!("\"board\":\"{}\"", board);
        let mut count = 0;
        for post in &all {
            let json_str = serde_json::to_string(post).unwrap_or_default();
            if !json_str.contains(&prefix) {
                continue;
            }
            let id = post.get("id").and_then(|v| v.as_u64()).unwrap_or(0);
            let ts = post.get("created_at").and_then(|v| v.as_str()).unwrap_or("");
            let mut p = protocol::Payload::new();
            p.insert("board".into(), board.into());
            p.insert("post_id".into(), id.to_string());
            p.insert("ts".into(), ts.into());
            let ann = protocol::build_signed(protocol::TYPE_POST_ANNOUNCE, p, &self.identity);
            let _ = io_tx.send(ann);
            count += 1;
        }
        tracing::info!(
            "sync: anunciados {} posts da board /{}/ ao novo peer",
            count,
            board
        );
    }
}
