use ratatui::layout::{Alignment, Constraint, Direction, Layout, Rect};
use ratatui::style::Modifier;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph, ListItem, List};
use ratatui::Frame;

use crate::app::{App, Screen};
use crate::ui::*;

pub fn render_splash(f: &mut Frame, app: &App) {
    let area = f.area();
    let total_height = 7u16;
    let top_margin = area.height.saturating_sub(total_height) / 2;

    let vert = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(top_margin),
            Constraint::Length(2),
            Constraint::Length(1),
            Constraint::Length(1),
            Constraint::Length(1),
            Constraint::Length(1),
            Constraint::Min(0),
        ])
        .split(area);

    let logo = Span::styled("✦  P Y O N  ✦", C_BOARD_BOLD);
    let sub = Span::styled("Peer Yet Onnected Network  v0.2-alpha", C_DIM);
    f.render_widget(
        Paragraph::new(Line::from(logo)).alignment(Alignment::Center),
        vert[1],
    );
    f.render_widget(
        Paragraph::new(Line::from(sub)).alignment(Alignment::Center),
        vert[2],
    );

    let ac = format!("acesso: {}", app.identity.access_code);
    f.render_widget(
        Paragraph::new(Line::from(Span::styled(ac, C_ACCENT))).alignment(Alignment::Center),
        vert[3],
    );

    if app.relay_connected {
        f.render_widget(
            Paragraph::new(Line::from(Span::styled("★ relay conectado", C_NICK.add_modifier(Modifier::BOLD))))
                .alignment(Alignment::Center),
            vert[4],
        );
        f.render_widget(
            Paragraph::new(Line::from(Span::styled("pressione qualquer tecla para continuar", C_DIM)))
                .alignment(Alignment::Center),
            vert[5],
        );
    } else if !app.host.is_empty() {
        let connecting = format!("conectando a {}:{}{}", app.host, app.port, ".".repeat(app.splash_dots));
        f.render_widget(
            Paragraph::new(Line::from(Span::styled(connecting, C_WARN.add_modifier(Modifier::BOLD))))
                .alignment(Alignment::Center),
            vert[4],
        );
        f.render_widget(
            Paragraph::new(Line::from(Span::styled("[ESC] entrar offline", C_DIM)))
                .alignment(Alignment::Center),
            vert[5],
        );
    } else {
        f.render_widget(
            Paragraph::new(Line::from(Span::styled("modo offline", C_DIM.add_modifier(Modifier::BOLD))))
                .alignment(Alignment::Center),
            vert[4],
        );
        f.render_widget(
            Paragraph::new(Line::from(Span::styled("pressione qualquer tecla para continuar", C_DIM)))
                .alignment(Alignment::Center),
            vert[5],
        );
    }
}

pub fn render_home(f: &mut Frame, app: &mut App) {
    let area = f.area();
    let constraints = if app.home_search_open {
        vec![Constraint::Length(3), Constraint::Min(0), Constraint::Length(1)]
    } else {
        vec![Constraint::Length(1), Constraint::Min(0), Constraint::Length(1)]
    };

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints(constraints)
        .split(area);

    // Render topbar or search overlay
    if app.home_search_open {
        let search_area = chunks[0];
        let block = Block::default().borders(Borders::ALL).title(" Buscar ");
        f.render_widget(block, search_area);
        let inner = Rect {
            x: search_area.x + 1,
            y: search_area.y + 1,
            width: search_area.width.saturating_sub(2),
            height: search_area.height.saturating_sub(2),
        };
        f.render_widget(Paragraph::new(app.home_filter.as_str()).style(C_INPUT), inner);
        f.set_cursor_position((inner.x + app.home_filter.len() as u16, inner.y));
    } else {
        render_topbar(f, chunks[0], " PYON ", app.relay_connected, &app.display_name);
    }

    // Build board list grouped by category
    let filter = app.home_filter.to_lowercase();
    use pyon_core::board::BoardCategory;
    let categories = [
        (BoardCategory::Padrao, "★ padrão", C_ACCENT),
        (BoardCategory::Anime, "✦ anime", C_BOARD),
        (BoardCategory::Universitario, "✧ universitário", C_TITLE),
        (BoardCategory::Cultura, "◈ cultura", C_NICK),
        (BoardCategory::Direto, "◆ direto", C_WARN),
    ];

    let mut items: Vec<ListItem> = Vec::new();
    for (cat, cat_label, cat_style) in &categories {
        let cat_boards: Vec<(usize, &pyon_core::board::BoardMeta)> = app
            .boards
            .iter()
            .enumerate()
            .filter(|(_, b)| b.category == *cat)
            .filter(|(_, b)| {
                filter.is_empty()
                    || b.slug.contains(&filter)
                    || b.title.to_lowercase().contains(&filter)
                    || b.description.to_lowercase().contains(&filter)
            })
            .collect();

        if cat_boards.is_empty() {
            continue;
        }

        // Category header
        items.push(ListItem::new(Line::from(vec![
            Span::raw("  "),
            Span::styled(format!("── {} ──", cat_label), cat_style.add_modifier(Modifier::BOLD)),
        ])));

        for (i, board) in &cat_boards {
            let is_selected = app.home_selected == *i;
            let style = if is_selected { C_SELECTED } else { C_NORMAL };
            let bullet = if is_selected { " ▶" } else { "  " };
            let line = format!(
                "{bullet} /{board_slug}/ {title}",
                bullet=bullet,
                board_slug=board.slug,
                title=board.title,
            );
            items.push(ListItem::new(Line::from(Span::styled(line, style))));

            if !is_selected {
                let desc = truncate(&board.description, area.width.saturating_sub(8) as usize);
                items.push(ListItem::new(Line::from(Span::styled(
                    format!("       {}", desc),
                    C_DIM,
                ))));
            }
        }
    }

    let board_list = List::new(items).highlight_style(C_SELECTED);
    f.render_widget(board_list, chunks[1]);

    if app.home_search_open {
        render_help_bar(f, chunks[2], &[("ESC", "cancelar")]);
    } else {
        render_help_bar(f, chunks[2], &[
            ("↑↓/jk", "navegar"), ("g/G", "topo/fim"), ("/", "buscar"),
            ("r", "chat"), ("q", "sair"),
        ]);
    }
}

pub fn handle_key(app: &mut App, key: crossterm::event::KeyCode) -> bool {
    if app.home_search_open {
        match key {
            crossterm::event::KeyCode::Esc => {
                app.home_search_open = false;
                app.home_filter.clear();
            }
            crossterm::event::KeyCode::Enter => {
                app.home_search_open = false;
            }
            crossterm::event::KeyCode::Backspace => {
                app.home_filter.pop();
            }
            crossterm::event::KeyCode::Char(c) => {
                app.home_filter.push(c);
            }
            _ => {}
        }
        return false;
    }

    use crossterm::event::KeyCode;
    match key {
        KeyCode::Char('/') => {
            app.home_search_open = true;
        }
        KeyCode::Up | KeyCode::Char('k') => {
            if app.home_selected > 0 {
                app.home_selected -= 1;
            }
        }
        KeyCode::Down | KeyCode::Char('j') => {
            if app.home_selected + 1 < app.boards.len() {
                app.home_selected += 1;
            }
        }
        KeyCode::PageUp => {
            app.home_selected = app.home_selected.saturating_sub(10);
        }
        KeyCode::PageDown => {
            app.home_selected = (app.home_selected + 10).min(app.boards.len() - 1);
        }
        KeyCode::Home | KeyCode::Char('g') => {
            app.home_selected = 0;
        }
        KeyCode::End | KeyCode::Char('G') => {
            app.home_selected = app.boards.len() - 1;
        }
        KeyCode::Enter => {
            if app.home_selected < app.boards.len() {
                let board = &app.boards[app.home_selected];
                app.current_board = Some(board.slug.clone());
                app.board_posts = crate::app::load_board_posts(&board.slug);
                app.board_selected = 0;
                app.screen = Screen::Board;
            }
        }
        KeyCode::Char('r') => {
            app.chat_messages.clear();
            app.chat_users.clear();
            app.chat_input.clear();
            app.chat_focus_sidebar = false;
            app.chat_scroll = 0;
            app.chat_sidebar_scroll = 0;
            app.chat_dm_target = None;
            app.chat_selected_user = None;
            app.screen = Screen::Chat;
        }
        KeyCode::Char('q') => {
            return true;
        }
        KeyCode::Char(c) if c.is_ascii_digit() => {
            let n = (c as u8 - b'0') as usize;
            if n > 0 && n <= app.boards.len() {
                app.home_selected = n - 1;
            }
        }
        _ => {}
    }
    false
}
