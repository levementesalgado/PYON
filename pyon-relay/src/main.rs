use std::path::PathBuf;
use std::sync::Arc;

use clap::Parser;
use pyon_core::identity::Identity;
use pyon_core::store::Store;
use tokio::net::TcpListener;
use tokio::sync::Mutex;

mod handler;
mod protocol;
mod server;

use server::RelayServer;

#[derive(Parser)]
#[command(name = "pyon-relay")]
struct Args {
    #[arg(long, default_value = "7667")]
    relay_port: u16,

    #[arg(long, default_value = "127.0.0.1")]
    host: String,

    #[arg(long)]
    debug: bool,
}

fn data_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".into());
    PathBuf::from(home).join(".pyon")
}

fn load_identity() -> Identity {
    let path = data_dir().join("identity.json");
    if path.exists() {
        let raw = std::fs::read_to_string(&path).expect("falha ao ler identity.json");
        serde_json::from_str(&raw).expect("identity.json corrompido")
    } else {
        eprintln!(
            "identidade não encontrada em {} — rode pyon-core primeiro",
            path.display()
        );
        std::process::exit(1);
    }
}

#[tokio::main]
async fn main() {
    let args = Args::parse();

    if args.debug {
        tracing_subscriber::fmt()
            .with_env_filter("debug")
            .init();
    } else {
        tracing_subscriber::fmt()
            .with_env_filter("info")
            .init();
    }

    let identity = load_identity();
    let db_dir = data_dir().join("db");
    std::fs::create_dir_all(&db_dir).expect("falha ao criar db dir");
    let store = Store::open(&db_dir).expect("falha ao abrir store");

    let server = Arc::new(Mutex::new(RelayServer::new(store, identity.clone())));

    tracing::info!(
        "nó: {}… | acesso: {}",
        &identity.pubkey_hex[..16],
        identity.access_code
    );
    tracing::info!("relay TCP em {}:{}", args.host, args.relay_port);
    tracing::info!("db: {}", db_dir.display());
    tracing::info!("(rode sob torsocks para expor como .onion)");

    let addr = format!("{}:{}", args.host, args.relay_port);
    let listener = TcpListener::bind(&addr)
        .await
        .expect("falha ao bindar TCP");

    tracing::info!("(＾▽＾) aguardando conexões kawaii…");

    loop {
        match listener.accept().await {
            Ok((stream, addr)) => {
                let peer = addr.to_string();
                let srv = Arc::clone(&server);
                tracing::info!("TCP: nova conexão de {} (ﾉ◕ヮ◕)ﾉ", peer);
                tokio::spawn(async move {
                    handler::handle_connection(srv, stream, peer).await;
                });
            }
            Err(e) => {
                tracing::error!("accept: {}", e);
            }
        }
    }
}
