use ed25519_dalek::{SigningKey, VerifyingKey, Signer, Verifier, Signature};
use rand::rngs::OsRng;
use serde::{Deserialize, Serialize};

/// Identidade do nó. Gerada uma vez, salva em disco.
#[derive(Serialize, Deserialize, Clone)]
pub struct Identity {
    /// Chave privada (32 bytes, hex) — NUNCA compartilhada
    pub secret_hex: String,
    /// Chave pública (32 bytes, hex) — ID público do nó
    pub pubkey_hex: String,
    /// Código de acesso legível: prefixo "tb-" + primeiros 24 chars do pubkey
    pub access_code: String,
    /// Nome de exibição escolhido pelo usuário (opcional)
    pub display_name: Option<String>,
}

impl Identity {
    /// Gera nova identidade aleatória.
    pub fn generate() -> Self {
        let secret = SigningKey::generate(&mut OsRng);
        let pubkey: VerifyingKey = (&secret).into();

        let secret_hex = hex::encode(secret.to_bytes());
        let pubkey_hex = hex::encode(pubkey.to_bytes());
        let access_code = format!("tb-{}", &pubkey_hex[..24]);

        Self { secret_hex, pubkey_hex, access_code, display_name: None }
    }

    /// Reconstrói a identidade a partir do secret_hex (login por código de acesso).
    pub fn from_secret(secret_hex: &str) -> anyhow::Result<Self> {
        let bytes = hex::decode(secret_hex)?;
        let arr: [u8; 32] = bytes.try_into().map_err(|_| anyhow::anyhow!("chave inválida"))?;
        let secret = SigningKey::from_bytes(&arr);
        let pubkey: VerifyingKey = (&secret).into();
        let pubkey_hex = hex::encode(pubkey.to_bytes());
        let access_code = format!("tb-{}", &pubkey_hex[..24]);
        Ok(Self { secret_hex: secret_hex.to_string(), pubkey_hex, access_code, display_name: None })
    }

    fn signing_key(&self) -> SigningKey {
        let bytes = hex::decode(&self.secret_hex).unwrap();
        let arr: [u8; 32] = bytes.try_into().unwrap();
        SigningKey::from_bytes(&arr)
    }

    /// Assina uma mensagem arbitrária.
    pub fn sign(&self, msg: &[u8]) -> String {
        let sig: Signature = self.signing_key().sign(msg);
        hex::encode(sig.to_bytes())
    }

    /// Verifica assinatura de outro nó (dado pubkey_hex + sig_hex + msg).
    pub fn verify(pubkey_hex: &str, sig_hex: &str, msg: &[u8]) -> anyhow::Result<bool> {
        let pk_bytes = hex::decode(pubkey_hex)?;
        let pk_arr: [u8; 32] = pk_bytes.try_into().map_err(|_| anyhow::anyhow!("pubkey inválido"))?;
        let vk = VerifyingKey::from_bytes(&pk_arr)?;

        let sig_bytes = hex::decode(sig_hex)?;
        let sig_arr: [u8; 64] = sig_bytes.try_into().map_err(|_| anyhow::anyhow!("assinatura inválida"))?;
        let sig = Signature::from_bytes(&sig_arr);

        Ok(vk.verify(msg, &sig).is_ok())
    }

    /// Hash curto para exibição no relay quando sem nome: primeiros 8 chars do pubkey.
    pub fn short_id(&self) -> String {
        self.pubkey_hex[..8].to_string()
    }
}

/// Fingerprint humano — usado como identificador visual no relay.
pub fn display_name_or_short(id: &Identity) -> String {
    id.display_name.clone().unwrap_or_else(|| format!("anon:{}", id.short_id()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_sign_verify() {
        let id = Identity::generate();
        let msg = b"ola mundo txtboard";
        let sig = id.sign(msg);
        assert!(Identity::verify(&id.pubkey_hex, &sig, msg).unwrap());
    }

    #[test]
    fn restore_from_secret() {
        let id = Identity::generate();
        let id2 = Identity::from_secret(&id.secret_hex).unwrap();
        assert_eq!(id.pubkey_hex, id2.pubkey_hex);
        assert_eq!(id.access_code, id2.access_code);
    }
}
