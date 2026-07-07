mod app;
mod protocol;
mod relay;
mod ui;

use std::time::{Duration, Instant};

use clap::Parser;
use crossterm::event::{self, DisableMouseCapture, EnableMouseCapture, Event, KeyCode, KeyEventKind};
use crossterm::execute;
use crossterm::terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen};
use ratatui::backend::CrosstermBackend;
use ratatui::Terminal;
use tracing_subscriber::EnvFilter;

use crate::app::{App, Screen};

#[derive(Parser, Debug)]
#[command(name = "pyon-tui", version, about = "PYON TUI client")]
struct Cli {
    #[arg(long, default_value = "127.0.0.1")]
    host: String,
    #[arg(long, default_value_t = 7667)]
    port: u16,
    #[arg(long, default_value = "geral")]
    channel: String,
    #[arg(long)]
    name: Option<String>,
}

fn load_identity() -> anyhow::Result<pyon_core::identity::Identity> {
    let path = dirs::home_dir()
        .unwrap_or_default()
        .join(".pyon")
        .join("identity.json");
    if path.exists() {
        let data = std::fs::read_to_string(&path)?;
        let id: pyon_core::identity::Identity = serde_json::from_str(&data)?;
        Ok(id)
    } else {
        let id = pyon_core::identity::Identity::generate();
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let data = serde_json::to_string_pretty(&id)?;
        std::fs::write(&path, &data)?;
        Ok(id)
    }
}

fn init_terminal() -> std::io::Result<Terminal<CrosstermBackend<std::io::Stdout>>> {
    enable_raw_mode()?;
    let mut stdout = std::io::stdout();
    execute!(stdout, EnterAlternateScreen, EnableMouseCapture)?;
    let backend = CrosstermBackend::new(stdout);
    Terminal::new(backend)
}

fn restore_terminal() -> std::io::Result<()> {
    disable_raw_mode()?;
    execute!(std::io::stdout(), LeaveAlternateScreen, DisableMouseCapture)?;
    Ok(())
}

fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .with_target(false)
        .init();

    let cli = Cli::parse();
    let identity = load_identity()?;
    let terminal = init_terminal()?;

    let mut app = App::new(identity, cli.host, cli.port, cli.channel, cli.name);

    let res = run(&mut app, terminal);

    restore_terminal().map_err(|e| anyhow::anyhow!("{}", e))?;
    if let Err(ref e) = res {
        eprintln!("Error: {e}");
    }
    res.map_err(|e| anyhow::anyhow!("{}", e))
}

fn run(app: &mut App, mut terminal: Terminal<ratatui::backend::CrosstermBackend<std::io::Stdout>>) -> std::io::Result<()> {
    let mut last_dot = Instant::now();
    let tick_rate = Duration::from_millis(50);

    loop {
        terminal.draw(|f| app.render(f))?;

        if event::poll(tick_rate)? {
            if let Event::Key(key) = event::read()? {
                if key.kind == KeyEventKind::Press {
                    let is_splash = matches!(app.screen, Screen::Splash);
                    if is_splash {
                        handle_splash_key(app, key.code);
                    } else {
                        match app.screen {
                            Screen::Home => { let q = ui::home::handle_key(app, key.code); if q { return Ok(()); } }
                            Screen::Board => ui::board::handle_key(app, key.code),
                            Screen::Thread => ui::thread::handle_key(app, key.code),
                            Screen::Chat => ui::chat::handle_key(app, key.code),
                            _ => {}
                        }
                    }
                }
            }
        }

        app.relay_tick();

        if matches!(app.screen, Screen::Splash) && last_dot.elapsed() >= Duration::from_secs(1) {
            app.splash_dots = (app.splash_dots + 1) % 4;
            last_dot = Instant::now();
        }

        if app.should_quit {
            return Ok(());
        }
    }
}

fn handle_splash_key(app: &mut App, key: KeyCode) {
    match key {
        KeyCode::Esc => {
            app.offline = true;
            app.relay_connected = false;
            if let Some(ref relay) = app.relay {
                relay.running.store(false, std::sync::atomic::Ordering::SeqCst);
            }
            app.screen = Screen::Home;
        }
        _ => {
            if app.relay_connected || app.offline {
                app.screen = Screen::Home;
            }
            if !app.relay_connected && !app.offline {
                app.offline = true;
                app.screen = Screen::Home;
            }
        }
    }
}
