use std::collections::BTreeMap;

use pyon_core::identity::Identity;

pub const VERSION: u8 = 1;

pub const TYPE_HANDSHAKE: &str = "handshake";
pub const TYPE_HANDSHAKE_ACK: &str = "handshake_ack";
pub const TYPE_PING: &str = "ping";
pub const TYPE_PONG: &str = "pong";
pub const TYPE_POST_ANNOUNCE: &str = "post_announce";
pub const TYPE_POST_REQUEST: &str = "post_request";
pub const TYPE_POST_DATA: &str = "post_data";
pub const TYPE_CHAT_MESSAGE: &str = "chat_message";
pub const TYPE_CHAT_JOIN: &str = "chat_join";
pub const TYPE_CHAT_LEAVE: &str = "chat_leave";
pub const TYPE_CHAT_DM: &str = "chat_dm";
pub const TYPE_BAN_BROADCAST: &str = "ban_broadcast";
pub const TYPE_PEER_LIST: &str = "peer_list";

pub const VALID_TYPES: &[&str] = &[
    TYPE_HANDSHAKE, TYPE_HANDSHAKE_ACK, TYPE_PING, TYPE_PONG,
    TYPE_POST_ANNOUNCE, TYPE_POST_REQUEST, TYPE_POST_DATA,
    TYPE_CHAT_MESSAGE, TYPE_CHAT_JOIN, TYPE_CHAT_LEAVE, TYPE_CHAT_DM,
    TYPE_BAN_BROADCAST, TYPE_PEER_LIST,
];

pub type Payload = BTreeMap<String, String>;

pub type Frame = (u8, String, Payload);

pub fn encode(frame: &Frame) -> Vec<u8> {
    let raw = rmp_serde::to_vec(frame).unwrap();
    let len = (raw.len() as u32).to_be_bytes();
    [&len[..], &raw[..]].concat()
}

pub fn parse(data: &[u8]) -> Result<Frame, String> {
    let (version, type_, payload): (u8, String, Payload) =
        rmp_serde::from_slice(data).map_err(|e| format!("msgpack inválido: {}", e))?;
    if version != VERSION {
        return Err(format!("versão incompatível: {}", version));
    }
    if !VALID_TYPES.contains(&type_.as_str()) {
        return Err(format!("tipo inválido: {}", type_));
    }
    Ok((version, type_, payload))
}

pub fn build_signed(type_: &str, mut payload: Payload, identity: &Identity) -> Vec<u8> {
    payload.insert("pubkey".into(), identity.pubkey_hex.clone());
    let canonical = canonical_bytes(type_, &payload);
    let sig = identity.sign(&canonical);
    payload.insert("sig".into(), sig);
    let raw = rmp_serde::to_vec(&(VERSION, type_, &payload)).unwrap();
    let len = (raw.len() as u32).to_be_bytes();
    [&len[..], &raw[..]].concat()
}

pub fn build_unsigned(type_: &str, payload: Payload) -> Vec<u8> {
    let raw = rmp_serde::to_vec(&(VERSION, type_, &payload)).unwrap();
    let len = (raw.len() as u32).to_be_bytes();
    [&len[..], &raw[..]].concat()
}

pub fn canonical_bytes(type_: &str, payload: &Payload) -> Vec<u8> {
    let parts: Vec<String> = payload
        .iter()
        .filter(|(k, _)| *k != "sig")
        .map(|(k, v)| format!("{}={}", k, v))
        .collect();
    format!("{}|{}|{}", VERSION, type_, parts.join("|")).into_bytes()
}

pub fn verify(frame: &Frame) -> Result<(), String> {
    let pubkey_hex = frame.2.get("pubkey").ok_or_else(|| "sem pubkey".to_string())?;
    let sig_hex = frame.2.get("sig").ok_or_else(|| "sem sig".to_string())?;
    let payload_without_sig: Payload = frame.2.iter()
        .filter(|(k, _)| *k != "sig")
        .map(|(k, v)| (k.clone(), v.clone()))
        .collect();
    let canonical = canonical_bytes(&frame.1, &payload_without_sig);
    match Identity::verify(pubkey_hex, sig_hex, &canonical) {
        Ok(true) => Ok(()),
        Ok(false) => Err(format!("assinatura inválida de {}", &pubkey_hex[..pubkey_hex.len().min(12)])),
        Err(e) => Err(format!("erro de verificação: {}", e)),
    }
}
