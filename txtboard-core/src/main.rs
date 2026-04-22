use std::fs;
use std::path::PathBuf;
use txtboard_core::identity::Identity;
use txtboard_core::store::Store;
use txtboard_core::board::Post;

fn data_dir() -> PathBuf {
    PathBuf::from(std::env::var("HOME").unwrap_or_else(|_| ".".into())).join(".txtboard")
}

fn main() {
    let dir     = data_dir();
    let id_path = dir.join("identity.json");
    let db_dir  = dir.join("db");

    /* garante que ~/.txtboard/ e ~/.txtboard/db/ existem sempre */
    fs::create_dir_all(&db_dir).expect("falha ao criar ~/.txtboard/db/");
    fs::create_dir_all(dir.join("images")).ok();

    let identity = if id_path.exists() {
        let raw = fs::read_to_string(&id_path).expect("falha ao ler identidade");
        serde_json::from_str::<Identity>(&raw).expect("identity.json corrompido")
    } else {
        let id = Identity::generate();
        fs::write(&id_path, serde_json::to_string_pretty(&id).unwrap())
            .expect("falha ao salvar identidade");
        eprintln!("\n  ✦ pyon v0.2 — primeira sessão ✦\n");
        eprintln!("  código de acesso (guarde!):\n  {}\n", id.access_code);
        id
    };

    let store = Store::open(&db_dir).expect("falha ao abrir store");
    let args: Vec<String> = std::env::args().skip(1).collect();

    match args.first().map(|s| s.as_str()).unwrap_or("") {
        "" => {
            println!("ok | nó: {} | acesso: {}",
                &identity.pubkey_hex[..12], identity.access_code);
        }

        "post" => {
            let mut board = String::new();
            let mut body  = String::new();
            let mut subj  = String::new();
            let mut reply = 0u64;
            let mut i = 1usize;
            while i < args.len() {
                match args[i].as_str() {
                    "--board"   => { board = args.get(i+1).cloned().unwrap_or_default(); i+=2; }
                    "--body"    => { body  = args.get(i+1).cloned().unwrap_or_default(); i+=2; }
                    "--subject" => { subj  = args.get(i+1).cloned().unwrap_or_default(); i+=2; }
                    "--reply"   => { reply = args.get(i+1)
                        .and_then(|s| s.parse().ok()).unwrap_or(0); i+=2; }
                    _           => { i+=1; }
                }
            }
            if board.is_empty() || body.is_empty() {
                eprintln!("uso: pyon post --board BOARD --body BODY [--subject S]");
                std::process::exit(1);
            }
            let post_id  = store.next_post_id(&board);
            let canonical = Post::canonical(&board, post_id, &body, &subj);
            let sig       = identity.sign(&canonical);
            let post = Post {
                _id:           format!("{}:{}", board, post_id),
                board:         board.clone(),
                id:            post_id,
                author_pubkey: identity.pubkey_hex.clone(),
                author_name:   identity.display_name.clone(),
                subject:       if subj.is_empty() { None } else { Some(subj) },
                body,
                images:        vec![],
                reply_to:      if reply == 0 { None } else { Some(reply) },
                sig,
                created_at:    chrono::Utc::now(),
                edited_at:     None,
            };
            store.insert("posts", &post).expect("falha ao salvar post");
            println!("ok:{}:{}", board, post_id);
        }

        "set-name" => {
            let name = args.get(1).cloned().unwrap_or_default();
            if name.is_empty() {
                eprintln!("uso: pyon set-name <nome>"); std::process::exit(1);
            }
            let raw = fs::read_to_string(&id_path).expect("sem identity.json");
            let mut val: serde_json::Value = serde_json::from_str(&raw).unwrap();
            val["display_name"] = serde_json::Value::String(name.clone());
            fs::write(&id_path, serde_json::to_string_pretty(&val).unwrap())
                .expect("falha ao salvar");
            println!("nome: {}", name);
        }

        "ban" => {
            let pubkey = args.get(1).cloned().unwrap_or_default();
            if pubkey.is_empty() {
                eprintln!("uso: pyon ban <pubkey> [motivo]"); std::process::exit(1);
            }
            let reason = args.get(2).cloned();
            let rec = serde_json::json!({
                "_id":       &pubkey,
                "pubkey":    &pubkey,
                "reason":    reason,
                "banned_at": chrono::Utc::now().to_rfc3339(),
                "banned_by": &identity.pubkey_hex,
            });
            store.upsert("bans", &pubkey, &rec).expect("falha");
            println!("banido: {}", &pubkey[..pubkey.len().min(16)]);
        }

        "unban" => {
            let pubkey = args.get(1).cloned().unwrap_or_default();
            let n = store.delete("bans", &pubkey).unwrap_or(0);
            println!("{}", if n > 0 { "removido" } else { "não encontrado" });
        }

        "bans" => {
            let bans: Vec<serde_json::Value> = store.all("bans").unwrap_or_default();
            if bans.is_empty() { println!("nenhum ban."); return; }
            for b in &bans {
                println!("  {}  {}",
                    b["pubkey"].as_str().unwrap_or("?"),
                    b["reason"].as_str().unwrap_or("-"));
            }
        }

        cmd => {
            eprintln!("pyon: comando desconhecido '{}'", cmd);
            eprintln!("comandos: post  set-name  ban  unban  bans");
            std::process::exit(1);
        }
    }
}
