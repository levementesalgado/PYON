use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};

/// Mensagem de relay chat. Guardada apenas no nó do autor.
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct ChatMessage {
    pub _id: String,             // uuid v4
    pub from_pubkey: String,
    pub from_name: Option<String>,
    pub to_pubkey: Option<String>, // None = broadcast no canal
    pub channel: Option<String>,   // None = DM direto
    pub body: String,
    pub sig: String,
    pub created_at: DateTime<Utc>,
    pub edited_at: Option<DateTime<Utc>>,
}

impl ChatMessage {
    /// Conteúdo assinável: determinístico, sem campos mutáveis.
    pub fn signable_body(&self) -> Vec<u8> {
        format!("{}|{}|{}", self._id, self.from_pubkey, self.body).into_bytes()
    }

    /// Texto exibido no relay: "nome_ou_shortid: mensagem"
    pub fn display_sender(&self) -> String {
        self.from_name
            .clone()
            .unwrap_or_else(|| format!("anon:{}", &self.from_pubkey[..8]))
    }
}

/// Onomatopeias de sistema para eventos do relay.
pub fn sys_join(name: &str) -> String {
    let sounds = ["*nyaa~*", "*fwoosh!*", "*pyon!*", "*ding~*", "*suuu~*"];
    let i = name.len() % sounds.len();
    format!("{} {} entrou no canal! (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧", sounds[i], name)
}

pub fn sys_leave(name: &str) -> String {
    format!("*plop...* {} saiu. (｡•́︿•̀｡)", name)
}

pub fn sys_welcome(name: &str) -> String {
    format!("*kiiin~!* bem-vinde, {}! (◕‿◕✿)", name)
}

pub fn sys_ban(name: &str) -> String {
    format!("*zap!!* {} foi removide do servidor. ( ˘︹˘ )", name)
}

pub fn sys_new_dm(name: &str) -> String {
    format!("*piiing!* nova mensagem de {}! ♡", name)
}
