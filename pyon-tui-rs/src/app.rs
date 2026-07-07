use std::sync::atomic::Ordering;
use std::sync::{Arc, Mutex};
use std::collections::VecDeque;

use crate::relay::RelayEvent;
use crate::ui;
use crate::protocol;

pub enum Screen {
    Splash,
    Home,
    Board,
    Thread,
    Chat,
}

pub struct ComposeState {
    pub subject: String,
    pub body: String,
    pub cursor_subject: bool,
}

#[derive(Clone)]
pub struct PostDisplay {
    pub id: u64,
    #[allow(unused)]
    pub board: String,
    pub author_name: String,
    #[allow(unused)]
    pub author_pubkey: String,
    pub subject: String,
    pub body: String,
    pub reply_to: Option<u64>,
    pub created_at: String,
    #[allow(unused)]
    pub raw_created: chrono::DateTime<chrono::Utc>,
}

pub struct ChatEntry {
    pub is_system: bool,
    pub is_dm: bool,
    pub nick: String,
    pub body: String,
    pub timestamp: String,
}

pub struct UserEntry {
    pub pubkey: String,
    pub name: String,
    pub has_unread: bool,
}

pub struct DmEntry {
    pub pubkey: String,
    pub name: String,
    #[allow(unused)]
    pub last_message: String,
    pub has_unread: bool,
}

#[derive(Clone)]
pub struct TreeNode {
    pub post: PostDisplay,
    pub depth: usize,
    pub has_children: bool,
}

pub struct App {
    pub identity: pyon_core::identity::Identity,
    pub display_name: String,

    pub screen: Screen,
    pub should_quit: bool,

    pub host: String,
    pub port: u16,
    pub channel: String,
    pub offline: bool,
    pub relay_connected: bool,
    pub relay: Option<crate::relay::RelayClient>,
    pub relay_events: Arc<Mutex<VecDeque<RelayEvent>>>,
    pub relay_outgoing: Arc<Mutex<VecDeque<Vec<u8>>>>,

    pub splash_dots: usize,

    pub home_filter: String,
    pub home_search_open: bool,
    pub home_selected: usize,
    pub boards: Vec<pyon_core::board::BoardMeta>,

    pub current_board: Option<String>,
    pub board_search: String,
    pub board_search_open: bool,
    pub board_selected: usize,
    pub board_posts: Vec<PostDisplay>,
    pub board_compose: Option<ComposeState>,

    pub thread_post_id: Option<u64>,
    pub thread_posts: Vec<PostDisplay>,
    pub thread_tree: Vec<TreeNode>,
    pub thread_selected: usize,
    pub thread_reply: Option<ComposeState>,

    pub chat_messages: Vec<ChatEntry>,
    pub chat_users: Vec<UserEntry>,
    pub chat_dms: Vec<DmEntry>,
    pub chat_input: String,
    pub chat_input_cursor: usize,
    pub chat_selected_user: Option<usize>,
    pub chat_focus_sidebar: bool,
    pub chat_scroll: usize,
    pub chat_dm_target: Option<String>,
    pub chat_sidebar_scroll: usize,
}

impl App {
    pub fn new(
        identity: pyon_core::identity::Identity,
        host: String,
        port: u16,
        channel: String,
        cli_name: Option<String>,
    ) -> Self {
        let display_name = cli_name
            .or_else(|| identity.display_name.clone())
            .unwrap_or_else(|| format!("anon:{}", &identity.pubkey_hex[..8]));

        let boards = pyon_core::board::all_boards();
        let events = Arc::new(Mutex::new(VecDeque::new()));
        let outgoing = Arc::new(Mutex::new(VecDeque::new()));

        let relay = if host.is_empty() {
            None
        } else {
            let rc = crate::relay::RelayClient::connect(
                &host, port, &channel, &display_name,
                identity.clone(),
                events.clone(), outgoing.clone(),
            );
            Some(rc)
        };

        Self {
            identity,
            display_name,
            screen: Screen::Splash,
            should_quit: false,
            host, port, channel,
            offline: false,
            relay_connected: false,
            relay,
            relay_events: events,
            relay_outgoing: outgoing,
            splash_dots: 0,
            home_filter: String::new(),
            home_search_open: false,
            home_selected: 0,
            boards,
            current_board: None,
            board_search: String::new(),
            board_search_open: false,
            board_selected: 0,
            board_posts: Vec::new(),
            board_compose: None,
            thread_post_id: None,
            thread_posts: Vec::new(),
            thread_tree: Vec::new(),
            thread_selected: 0,
            thread_reply: None,
            chat_messages: Vec::new(),
            chat_users: Vec::new(),
            chat_dms: Vec::new(),
            chat_input: String::new(),
            chat_input_cursor: 0,
            chat_selected_user: None,
            chat_focus_sidebar: false,
            chat_scroll: 0,
            chat_dm_target: None,
            chat_sidebar_scroll: 0,
        }
    }

    pub fn relay_tick(&mut self) {
        let events: Vec<RelayEvent> = self.relay_events.lock().unwrap().drain(..).collect();
        for event in events {
            self.handle_relay_event(event);
        }
        if let Some(ref relay) = self.relay {
            self.relay_connected = relay.connected.load(Ordering::SeqCst);
        }
    }

    /// Send a signed chat message to the relay via the outgoing queue.
    pub fn relay_send_chat(&self, body: &str) {
        let mut p = protocol::Payload::new();
        p.insert("body".into(), body.into());
        let frame = protocol::build_signed(protocol::TYPE_CHAT_MESSAGE, p, &self.identity);
        let mut q = self.relay_outgoing.lock().unwrap();
        q.push_back(frame);
    }

    /// Send a signed DM to a specific pubkey.
    pub fn relay_send_dm(&self, to_pubkey: &str, body: &str) {
        let mut p = protocol::Payload::new();
        p.insert("to_pubkey".into(), to_pubkey.into());
        p.insert("body".into(), body.into());
        let frame = protocol::build_signed(protocol::TYPE_CHAT_DM, p, &self.identity);
        let mut q = self.relay_outgoing.lock().unwrap();
        q.push_back(frame);
    }

    /// Send a signed post_data frame (new post) and persist locally.
    pub fn relay_send_post(&self, board: &str, id: u64, body: &str, subject: &str, reply_to: u64) {
        let canonical = pyon_core::board::Post::canonical(board, id, body, subject);
        let orig_sig = self.identity.sign(&canonical);

        // Persist locally
        append_post_to_store(board, id, body, subject, reply_to, &orig_sig);

        let mut p = protocol::Payload::new();
        p.insert("board".into(), board.into());
        p.insert("post_id".into(), id.to_string());
        p.insert("body".into(), body.into());
        p.insert("subject".into(), subject.into());
        p.insert("reply_to".into(), reply_to.to_string());
        p.insert("images".into(), "[]".into());
        p.insert("author".into(), self.identity.pubkey_hex.clone());
        p.insert("author_name".into(), self.display_name.clone());
        p.insert("created_at".into(), chrono::Utc::now().to_rfc3339());
        p.insert("orig_sig".into(), orig_sig);
        let frame = protocol::build_signed(protocol::TYPE_POST_DATA, p, &self.identity);
        let mut q = self.relay_outgoing.lock().unwrap();
        q.push_back(frame);
    }

    fn handle_relay_event(&mut self, event: RelayEvent) {
        match event {
            RelayEvent::Connected => {
                self.relay_connected = true;
            }
            RelayEvent::Disconnected => {
                self.relay_connected = false;
            }
            RelayEvent::Message { nick, body, is_dm } => {
                let now = chrono::Utc::now();
                let ts = now.format("%H:%M").to_string();
                self.chat_messages.push(ChatEntry {
                    is_system: false, is_dm, nick, body,
                    timestamp: ts,
                });
                self.chat_scroll = self.chat_messages.len().saturating_sub(1);
            }
            RelayEvent::SystemMessage(body) => {
                let now = chrono::Utc::now();
                let ts = now.format("%H:%M").to_string();
                self.chat_messages.push(ChatEntry {
                    is_system: true, is_dm: false,
                    nick: String::new(), body,
                    timestamp: ts,
                });
                self.chat_scroll = self.chat_messages.len().saturating_sub(1);
            }
            RelayEvent::UserJoin { pubkey, name } => {
                if !self.chat_users.iter().any(|u| u.pubkey == pubkey) {
                    self.chat_users.push(UserEntry { pubkey, name, has_unread: false });
                }
            }
            RelayEvent::UserLeave { pubkey } => {
                self.chat_users.retain(|u| u.pubkey != pubkey);
            }
            RelayEvent::NewPost { board, id, author_name, author_pubkey, subject, body, reply_to, created_at } => {
                let raw_created = created_at;
                let ts = raw_created.format("%Y-%m-%d %H:%M").to_string();
                let post = PostDisplay {
                    id, board: board.clone(), author_name, author_pubkey,
                    subject: subject.unwrap_or_default(), body, reply_to,
                    created_at: ts, raw_created,
                };
                if self.current_board.as_deref() == Some(&board) {
                    self.board_posts.push(post.clone());
                }
                self.thread_posts.push(post);
            }
        }
    }

    pub fn render(&mut self, f: &mut ratatui::Frame) {
        match self.screen {
            Screen::Splash => ui::home::render_splash(f, self),
            Screen::Home => ui::home::render_home(f, self),
            Screen::Board => ui::board::render_board(f, self),
            Screen::Thread => ui::thread::render_thread(f, self),
            Screen::Chat => ui::chat::render_chat(f, self),
        }
    }
}

pub fn load_board_posts(board: &str) -> Vec<PostDisplay> {
    let home = dirs::home_dir().unwrap_or_default();
    let posts_path = home.join(".pyon").join("db").join("posts.ndjson");

    let mut posts = Vec::new();
    if posts_path.exists() {
        if let Ok(data) = std::fs::read_to_string(&posts_path) {
            for line in data.lines() {
                if line.is_empty() {
                    continue;
                }
                if let Ok(raw) = serde_json::from_str::<serde_json::Value>(line) {
                    let b = raw.get("board").and_then(|v| v.as_str()).unwrap_or("");
                    if b != board {
                        continue;
                    }
                    let id = raw.get("id").and_then(|v| v.as_u64()).unwrap_or(0);
                    let author_name = raw
                        .get("author_name")
                        .and_then(|v| v.as_str())
                        .unwrap_or("anon")
                        .to_string();
                    let author_pubkey = raw
                        .get("author_pubkey")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string();
                    let subject = raw
                        .get("subject")
                        .and_then(|v| v.as_str())
                        .unwrap_or("")
                        .to_string();
                    let body = raw.get("body").and_then(|v| v.as_str()).unwrap_or("").to_string();
                    let reply_to = raw.get("reply_to").and_then(|v| v.as_u64());
                    let raw_created: chrono::DateTime<chrono::Utc> = raw
                        .get("created_at")
                        .and_then(|v| v.as_str())
                        .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
                        .map(|dt| dt.with_timezone(&chrono::Utc))
                        .unwrap_or_else(chrono::Utc::now);
                    let created_at = raw_created.format("%Y-%m-%d %H:%M").to_string();
                    posts.push(PostDisplay {
                        id,
                        board: b.to_string(),
                        author_name,
                        author_pubkey,
                        subject,
                        body,
                        reply_to,
                        created_at,
                        raw_created,
                    });
                }
            }
        }
    }

    if posts.is_empty() {
        posts = demo_posts(board);
    }

    posts.sort_by(|a, b| b.id.cmp(&a.id));
    posts
}

pub fn demo_posts(board: &str) -> Vec<PostDisplay> {
    vec![
        PostDisplay {
            id: 1,
            board: board.to_string(),
            author_name: "pyon-chan".into(),
            author_pubkey: "aaaa".into(),
            subject: format!("Bem-vinde ao /{}/!", board),
            body: "Este é um post de demonstração. Conecte-se a um relay para ver posts reais! (◕‿◕✿)".into(),
            reply_to: None,
            created_at: chrono::Utc::now().format("%Y-%m-%d %H:%M").to_string(),
            raw_created: chrono::Utc::now(),
        },
        PostDisplay {
            id: 3,
            board: board.to_string(),
            author_name: "neko-chan".into(),
            author_pubkey: "bbbb".into(),
            subject: "Teste!".into(),
            body: "pyon pyon~! 🐱".into(),
            reply_to: None,
            created_at: (chrono::Utc::now() - chrono::Duration::hours(2))
                .format("%Y-%m-%d %H:%M")
                .to_string(),
            raw_created: chrono::Utc::now() - chrono::Duration::hours(2),
        },
        PostDisplay {
            id: 2,
            board: board.to_string(),
            author_name: "sama".into(),
            author_pubkey: "cccc".into(),
            subject: "Alguém aí?".into(),
            body: "o/".into(),
            reply_to: None,
            created_at: (chrono::Utc::now() - chrono::Duration::hours(5))
                .format("%Y-%m-%d %H:%M")
                .to_string(),
            raw_created: chrono::Utc::now() - chrono::Duration::hours(5),
        },
    ]
}

pub fn append_post_to_store(board: &str, id: u64, body: &str, subject: &str, reply_to: u64, sig: &str) {
    let home = dirs::home_dir().unwrap_or_default();
    let db_dir = home.join(".pyon").join("db");
    let _ = std::fs::create_dir_all(&db_dir);
    let posts_path = db_dir.join("posts.ndjson");
    let post = serde_json::json!({
        "board": board,
        "id": id,
        "author_pubkey": "",
        "author_name": "",
        "subject": subject,
        "body": body,
        "reply_to": if reply_to > 0 { serde_json::Value::Number(reply_to.into()) } else { serde_json::Value::Null },
        "sig": sig,
        "created_at": chrono::Utc::now().to_rfc3339(),
    });
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&posts_path) {
        use std::io::Write;
        let _ = writeln!(f, "{}", post);
    }
}

pub fn build_thread_tree(posts: &[PostDisplay]) -> Vec<TreeNode> {
    use std::collections::HashMap;

    let post_map: HashMap<u64, &PostDisplay> = posts.iter().map(|p| (p.id, p)).collect();
    let mut children: HashMap<Option<u64>, Vec<u64>> = HashMap::new();
    for post in posts {
        children.entry(post.reply_to).or_default().push(post.id);
    }
    for ids in children.values_mut() {
        ids.sort();
    }

    fn build(
        parent_id: Option<u64>,
        depth: usize,
        children: &HashMap<Option<u64>, Vec<u64>>,
        post_map: &HashMap<u64, &PostDisplay>,
        result: &mut Vec<TreeNode>,
    ) {
        if let Some(ids) = children.get(&parent_id) {
            for &id in ids {
                if let Some(post) = post_map.get(&id) {
                    let has_more = children.contains_key(&Some(id));
                    result.push(TreeNode {
                        post: (*post).clone(),
                        depth,
                        has_children: has_more,
                    });
                    build(Some(id), depth + 1, children, post_map, result);
                }
            }
        }
    }

    let mut result = Vec::new();
    build(None, 0, &children, &post_map, &mut result);
    result
}
