use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use crate::store::Store;

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct BanRecord {
    pub _id: String,           // pubkey banida (também é o ID do registro)
    pub pubkey: String,
    pub reason: Option<String>,
    pub banned_at: DateTime<Utc>,
    pub banned_by: String,     // pubkey do admin
}

pub struct BanList<'a> {
    store: &'a Store,
}

impl<'a> BanList<'a> {
    pub fn new(store: &'a Store) -> Self { Self { store } }

    pub fn ban(&self, pubkey: &str, reason: Option<String>, admin_pubkey: &str) -> std::io::Result<()> {
        let record = BanRecord {
            _id: pubkey.to_string(),
            pubkey: pubkey.to_string(),
            reason,
            banned_at: Utc::now(),
            banned_by: admin_pubkey.to_string(),
        };
        // upsert: remove anterior se existir
        let _ = self.store.delete("bans", pubkey);
        self.store.insert("bans", &record)
    }

    pub fn unban(&self, pubkey: &str) -> std::io::Result<bool> {
        Ok(self.store.delete("bans", pubkey)? > 0)
    }

    pub fn is_banned(&self, pubkey: &str) -> bool {
        let all: Vec<BanRecord> = self.store.all("bans").unwrap_or_default();
        all.iter().any(|b| b.pubkey == pubkey)
    }

    pub fn list(&self) -> Vec<BanRecord> {
        self.store.all("bans").unwrap_or_default()
    }
}
