use std::collections::VecDeque;
use std::io::{Read, Write};
use std::net::{TcpStream, Shutdown};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use chrono::{DateTime, Utc};
use pyon_core::identity::Identity;

use crate::protocol;

/// Events produced by the relay background thread.
#[derive(Debug, Clone)]
pub enum RelayEvent {
    Connected,
    Disconnected,
    Message {
        nick: String,
        body: String,
        is_dm: bool,
    },
    SystemMessage(String),
    UserJoin {
        pubkey: String,
        name: String,
    },
    UserLeave {
        pubkey: String,
    },
    NewPost {
        board: String,
        id: u64,
        author_name: String,
        author_pubkey: String,
        subject: Option<String>,
        body: String,
        reply_to: Option<u64>,
        created_at: DateTime<Utc>,
    },
}

pub struct RelayClient {
    pub running: Arc<AtomicBool>,
    pub connected: Arc<AtomicBool>,
    _handle: Option<thread::JoinHandle<()>>,
}

impl RelayClient {
    pub fn connect(
        host: &str,
        port: u16,
        channel: &str,
        display_name: &str,
        identity: Identity,
        events: Arc<Mutex<VecDeque<RelayEvent>>>,
        outgoing: Arc<Mutex<VecDeque<Vec<u8>>>>,
    ) -> Self {
        let running = Arc::new(AtomicBool::new(true));
        let connected = Arc::new(AtomicBool::new(false));

        let host = host.to_string();
        let channel = channel.to_string();
        let display_name = display_name.to_string();
        let running_clone = running.clone();
        let connected_clone = connected.clone();

        let handle = thread::spawn(move || {
            let addr = format!("{}:{}", host, port);
            loop {
                if !running_clone.load(Ordering::SeqCst) {
                    return;
                }

                match TcpStream::connect_timeout(&addr.parse().unwrap_or_else(|_| {
                    ([127, 0, 0, 1], port).into()
                }), Duration::from_secs(5)) {
                    Ok(mut stream) => {
                        // Send signed handshake
                        let mut handshake = protocol::Payload::new();
                        handshake.insert("channel".into(), channel.clone());
                        handshake.insert("name".into(), display_name.clone());
                        let handshake_data = protocol::build_signed(protocol::TYPE_HANDSHAKE, handshake, &identity);
                        if stream.write_all(&handshake_data).is_err() {
                            thread::sleep(Duration::from_secs(1));
                            continue;
                        }

                        // Wait for handshake_ack
                        let mut ack_buf = [0u8; 4];
                        if stream.read_exact(&mut ack_buf).is_err() {
                            thread::sleep(Duration::from_secs(1));
                            continue;
                        }
                        let ack_len = u32::from_be_bytes(ack_buf) as usize;
                        if ack_len > 4 * 1024 * 1024 {
                            continue;
                        }
                        let mut ack_data = vec![0u8; ack_len];
                        if stream.read_exact(&mut ack_data).is_err() {
                            thread::sleep(Duration::from_secs(1));
                            continue;
                        }
                        if let Ok(ack_frame) = protocol::parse(&ack_data) {
                            if ack_frame.1 == protocol::TYPE_HANDSHAKE_ACK {
                                let ok = ack_frame.2.get("ok").map(|s| s == "true").unwrap_or(false);
                                if ok {
                                    connected_clone.store(true, Ordering::SeqCst);
                                    {
                                        let mut ev = events.lock().unwrap();
                                        ev.push_back(RelayEvent::Connected);
                                        let motd = ack_frame.2.get("motd").cloned().unwrap_or_default();
                                        if !motd.is_empty() {
                                            ev.push_back(RelayEvent::SystemMessage(motd));
                                        }
                                    }
                                } else {
                                    let reason = ack_frame.2.get("reason").cloned().unwrap_or("desconhecido".into());
                                    let mut ev = events.lock().unwrap();
                                    ev.push_back(RelayEvent::SystemMessage(format!("*pip!* relay recusou: {}", reason)));
                                    thread::sleep(Duration::from_secs(3));
                                    continue;
                                }
                            }
                        }

                        let relay_pubkey = identity.pubkey_hex.clone();

                        // Read loop
                        let mut buf = Vec::new();
                        let mut read_buf = [0u8; 8192];
                        loop {
                            // Check outgoing queue
                            {
                                let mut q = outgoing.lock().unwrap();
                                while let Some(data) = q.pop_front() {
                                    if stream.write_all(&data).is_err() {
                                        break;
                                    }
                                }
                            }

                            // Read with timeout
                            stream.set_read_timeout(Some(Duration::from_millis(100))).ok();
                            match stream.read(&mut read_buf) {
                                Ok(0) => break,
                                Ok(n) => {
                                    buf.extend_from_slice(&read_buf[..n]);
                                    loop {
                                        if buf.len() < 4 {
                                            break;
                                        }
                                        let frame_len = u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]) as usize;
                                        if buf.len() < 4 + frame_len {
                                            break;
                                        }
                                        let frame_data = buf[4..4 + frame_len].to_vec();
                                        buf.drain(..4 + frame_len);
                                        if let Ok(frame) = protocol::parse(&frame_data) {
                                            Self::handle_frame(&frame, &relay_pubkey, &events);
                                        }
                                    }
                                }
                                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock
                                    || e.kind() == std::io::ErrorKind::TimedOut => {}
                                Err(_) => break,
                            }

                            if !running_clone.load(Ordering::SeqCst) {
                                break;
                            }
                        }

                        connected_clone.store(false, Ordering::SeqCst);
                        {
                            let mut ev = events.lock().unwrap();
                            ev.push_back(RelayEvent::Disconnected);
                        }
                        let _ = stream.shutdown(Shutdown::Both);
                    }
                    Err(_) => {
                        thread::sleep(Duration::from_secs(2));
                    }
                }

                if !running_clone.load(Ordering::SeqCst) {
                    return;
                }
                thread::sleep(Duration::from_secs(1));
            }
        });

        Self {
            running,
            connected,
            _handle: Some(handle),
        }
    }

    fn handle_frame(frame: &protocol::Frame, relay_pubkey: &str, events: &Arc<Mutex<VecDeque<RelayEvent>>>) {
        let (_, type_, payload) = frame;
        match type_.as_str() {
            protocol::TYPE_CHAT_MESSAGE => {
                let from_pubkey = payload.get("from_pubkey").cloned().unwrap_or_default();
                let from_name = payload.get("from_name").cloned().unwrap_or_else(|| {
                    format!("anon:{}", &from_pubkey[..from_pubkey.len().min(8)])
                });
                let body = payload.get("body").cloned().unwrap_or_default();

                // System messages from the relay itself
                if from_pubkey == *relay_pubkey && from_name.contains("sistema") {
                    let mut ev = events.lock().unwrap();
                    ev.push_back(RelayEvent::SystemMessage(body));
                } else {
                    let mut ev = events.lock().unwrap();
                    ev.push_back(RelayEvent::Message {
                        nick: from_name,
                        body,
                        is_dm: false,
                    });
                }
            }
            protocol::TYPE_CHAT_DM => {
                let from_name = payload.get("from_name").cloned().unwrap_or("anon".into());
                let body = payload.get("body").cloned().unwrap_or_default();
                let mut ev = events.lock().unwrap();
                ev.push_back(RelayEvent::Message {
                    nick: from_name.clone(),
                    body,
                    is_dm: true,
                });
                ev.push_back(RelayEvent::SystemMessage(format!(
                    "*piiing!* nova mensagem de {}! ♡", from_name
                )));
            }
            protocol::TYPE_CHAT_JOIN => {
                let pubkey = payload.get("pubkey").cloned().unwrap_or_default();
                let name = payload.get("name").cloned().unwrap_or_else(|| {
                    format!("anon:{}", &pubkey[..pubkey.len().min(8)])
                });
                let mut ev = events.lock().unwrap();
                ev.push_back(RelayEvent::UserJoin { pubkey, name: name.clone() });
                let sounds = ["*nyaa~*", "*fwoosh!*", "*pyon!*", "*ding~*", "*suuu~*"];
                let i = name.len() % sounds.len();
                ev.push_back(RelayEvent::SystemMessage(format!(
                    "{} {} entrou no canal! (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧",
                    sounds[i], name
                )));
            }
            protocol::TYPE_CHAT_LEAVE => {
                let pubkey = payload.get("pubkey").cloned().unwrap_or_default();
                let name = payload.get("name").cloned().unwrap_or_else(|| {
                    format!("anon:{}", &pubkey[..pubkey.len().min(8)])
                });
                let mut ev = events.lock().unwrap();
                ev.push_back(RelayEvent::UserLeave { pubkey });
                ev.push_back(RelayEvent::SystemMessage(format!(
                    "*plop...* {} saiu. (｡•́︿•̀｡)", name
                )));
            }
            protocol::TYPE_POST_ANNOUNCE => {
                tracing::debug!("post_announce: {:?}", payload);
            }
            _ => {
                tracing::debug!("unhandled frame type: {}", type_);
            }
        }
    }
}
