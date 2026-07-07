use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::Modifier;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph, ListItem, List};
use ratatui::Frame;

use crate::app::{App, ComposeState, PostDisplay, Screen};
use crate::ui::*;

pub fn render_board(f: &mut Frame, app: &mut App) {
    let area = f.area();
    let constraints = if app.board_compose.is_some() {
        vec![Constraint::Length(1), Constraint::Min(0)]
    } else if app.board_search_open {
        vec![Constraint::Length(3), Constraint::Min(0), Constraint::Length(1)]
    } else {
        vec![Constraint::Length(1), Constraint::Min(0), Constraint::Length(1)]
    };

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints(constraints)
        .split(area);

    let board_name = app.current_board.as_deref().unwrap_or("?");

    if app.board_search_open {
        let search_area = chunks[0];
        let block = Block::default().borders(Borders::ALL).title(" Buscar ");
        f.render_widget(block, search_area);
        let inner = Rect {
            x: search_area.x + 1,
            y: search_area.y + 1,
            width: search_area.width.saturating_sub(2),
            height: search_area.height.saturating_sub(2),
        };
        f.render_widget(Paragraph::new(app.board_search.as_str()).style(C_INPUT), inner);
        f.set_cursor_position((inner.x + app.board_search.len() as u16, inner.y));
    } else {
        render_topbar(f, chunks[0], &format!(" /{}/ ", board_name), app.relay_connected, &app.display_name);
    }

    // Post list with body preview
    let filter = app.board_search.to_lowercase();
    let filtered: Vec<&PostDisplay> = app.board_posts
        .iter()
        .filter(|p| {
            filter.is_empty()
                || p.subject.to_lowercase().contains(&filter)
                || p.body.to_lowercase().contains(&filter)
                || p.author_name.to_lowercase().contains(&filter)
        })
        .collect();

    let mut items: Vec<ListItem> = Vec::new();
    for (i, post) in filtered.iter().enumerate() {
        let is_selected = i == app.board_selected;
        let style = if is_selected { C_SELECTED } else { C_NORMAL };

        let reply_count = app.board_posts.iter().filter(|p| p.reply_to == Some(post.id)).count();
        let badge = if reply_count > 0 {
            format!("[{}R]", reply_count)
        } else {
            String::new()
        };

        let subj = truncate(&post.subject, 40);
        let author = truncate(&post.author_name, 20);
        let line = Line::from(vec![
            Span::styled(if is_selected { " ▶" } else { "  " }, C_LINE),
            Span::styled(format!(" #{} ", post.id), C_POST_COUNT),
            Span::styled(subj, style.add_modifier(Modifier::BOLD)),
            Span::raw(" "),
            Span::styled(format!("— {} ", author), C_DIM),
            Span::styled(post.created_at.clone(), C_DIM),
            if !badge.is_empty() {
                Span::styled(format!(" {}", badge), C_REPLY_BADGE)
            } else {
                Span::raw("")
            },
        ]);
        items.push(ListItem::new(line));

        // Body preview line (first line of body)
        if let Some(first_line) = post.body.lines().next() {
            if !first_line.is_empty() {
                let preview = truncate(first_line, 50);
                items.push(ListItem::new(Line::from(Span::styled(
                    format!("     {}", preview),
                    if is_selected { C_SELECTED } else { C_DIM },
                ))));
            }
        }
    }

    let title = format!(" {} posts ", filtered.len());
    let block = Block::default().title(title).borders(Borders::NONE);
    let list = List::new(items).block(block).highlight_style(C_SELECTED);
    f.render_widget(list, chunks[1]);

    if app.board_compose.is_none() && !app.board_search_open {
        render_help_bar(f, chunks[2], &[
            ("↑↓/jk", "navegar"), ("g/G", "topo/fim"), ("c", "compor"),
            ("/", "buscar"), ("Enter", "thread"), ("ESC", "voltar"),
        ]);
    }

    // Compose overlay
    if let Some(ref compose) = app.board_compose {
        render_compose_overlay(f, area, compose);
    }
}

fn render_compose_overlay(f: &mut Frame, area: Rect, compose: &ComposeState) {
    let inner = render_centered_block(f, area, 60, 50, " Compor post ", C_BOARD);

    let vert = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Length(3), Constraint::Min(0)])
        .split(inner);

    // Subject line
    let subject_style = if compose.cursor_subject { C_INPUT } else { C_DIM };
    f.render_widget(
        Paragraph::new(Line::from(vec![
            Span::styled(" assunto ", C_DIM),
            Span::styled(compose.subject.as_str(), subject_style),
        ])),
        vert[0],
    );
    if compose.cursor_subject {
        f.set_cursor_position((
            vert[0].x + 9 + compose.subject.len() as u16,
            vert[0].y,
        ));
    }

    // Body area
    let body_style = if !compose.cursor_subject { C_INPUT } else { C_DIM };
    let body_text = if compose.body.is_empty() {
        vec![Line::from(Span::styled(" (digite seu post aqui)", C_DIM))]
    } else {
        compose.body.lines().map(|l| Line::from(Span::styled(l.to_string(), body_style))).collect()
    };
    f.render_widget(
        ratatui::widgets::Paragraph::new(body_text).block(
            Block::default().borders(Borders::ALL).title(" corpo "),
        ),
        vert[1],
    );
    if !compose.cursor_subject {
        let cursor_x = vert[1].x + 1 + (compose.body.len() as u16 % vert[1].width.saturating_sub(2));
        let cursor_y = vert[1].y + 1 + (compose.body.len() as u16 / vert[1].width.saturating_sub(2));
        f.set_cursor_position((cursor_x, cursor_y));
    }

    // Help
    let help_area = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Min(0), Constraint::Length(1)])
        .split(inner)[1];
    render_help_bar(f, help_area, &[
        ("Tab", "alternar"), ("Enter", "publicar"), ("ESC", "cancelar"),
    ]);
}

pub fn handle_key(app: &mut App, key: crossterm::event::KeyCode) {
    if let Some(ref mut compose) = app.board_compose {
        match key {
            crossterm::event::KeyCode::Esc => {
                app.board_compose = None;
            }
            crossterm::event::KeyCode::Tab => {
                compose.cursor_subject = !compose.cursor_subject;
            }
            crossterm::event::KeyCode::Backspace => {
                if compose.cursor_subject {
                    compose.subject.pop();
                } else {
                    compose.body.pop();
                }
            }
            crossterm::event::KeyCode::Enter => {
                if !compose.cursor_subject && !compose.body.is_empty() {
                    let subject = compose.subject.clone();
                    let body = compose.body.clone();
                    submit_post(app, subject, body, None);
                    app.board_compose = None;
                }
            }
            crossterm::event::KeyCode::Char(c) => {
                if compose.cursor_subject {
                    compose.subject.push(c);
                } else {
                    compose.body.push(c);
                }
            }
            _ => {}
        }
        return;
    }

    if app.board_search_open {
        match key {
            crossterm::event::KeyCode::Esc => {
                app.board_search_open = false;
                app.board_search.clear();
            }
            crossterm::event::KeyCode::Enter => {
                app.board_search_open = false;
            }
            crossterm::event::KeyCode::Backspace => {
                app.board_search.pop();
            }
            crossterm::event::KeyCode::Char(c) => {
                app.board_search.push(c);
            }
            _ => {}
        }
        return;
    }

    use crossterm::event::KeyCode;
    match key {
        KeyCode::Up | KeyCode::Char('k') => {
            if app.board_selected > 0 {
                app.board_selected -= 1;
            }
        }
        KeyCode::Down | KeyCode::Char('j') => {
            let filtered = filtered_count(app);
            if app.board_selected + 1 < filtered {
                app.board_selected += 1;
            }
        }
        KeyCode::PageUp => {
            app.board_selected = app.board_selected.saturating_sub(10);
        }
        KeyCode::PageDown => {
            let max = filtered_count(app).saturating_sub(1);
            app.board_selected = (app.board_selected + 10).min(max);
        }
        KeyCode::Home | KeyCode::Char('g') => {
            app.board_selected = 0;
        }
        KeyCode::End | KeyCode::Char('G') => {
            app.board_selected = filtered_count(app).saturating_sub(1);
        }
        KeyCode::Char('/') => {
            app.board_search_open = true;
            app.board_search.clear();
        }
        KeyCode::Char('c') => {
            app.board_compose = Some(ComposeState {
                subject: String::new(),
                body: String::new(),
                cursor_subject: true,
            });
        }
        KeyCode::Enter => {
            let filter = app.board_search.to_lowercase();
            let filtered: Vec<usize> = app.board_posts
                .iter()
                .enumerate()
                .filter(|(_, p)| {
                    filter.is_empty()
                        || p.subject.to_lowercase().contains(&filter)
                        || p.body.to_lowercase().contains(&filter)
                        || p.author_name.to_lowercase().contains(&filter)
                })
                .map(|(i, _)| i)
                .collect();
            if let Some(&idx) = filtered.get(app.board_selected) {
                if let Some(post) = app.board_posts.get(idx) {
                    app.thread_post_id = Some(post.id);
                    app.thread_posts = app.board_posts.clone();
                    app.thread_tree = crate::app::build_thread_tree(&app.board_posts);
                    app.thread_selected = 0;
                    app.thread_reply = None;
                    app.screen = Screen::Thread;
                }
            }
        }
        KeyCode::Esc => {
            app.screen = Screen::Home;
        }
        _ => {}
    }
}

fn filtered_count(app: &App) -> usize {
    let filter = app.board_search.to_lowercase();
    app.board_posts
        .iter()
        .filter(|p| {
            filter.is_empty()
                || p.subject.to_lowercase().contains(&filter)
                || p.body.to_lowercase().contains(&filter)
                || p.author_name.to_lowercase().contains(&filter)
        })
        .count()
}

fn submit_post(app: &mut App, subject: String, body: String, reply_to: Option<u64>) {
    let board = app.current_board.as_deref().unwrap_or("sr").to_string();
    let id = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as u64;
    app.relay_send_post(&board, id, &body, &subject, reply_to.unwrap_or(0));
    let now = chrono::Utc::now();
    app.board_posts.push(crate::app::PostDisplay {
        id,
        board: board.clone(),
        author_name: app.display_name.clone(),
        author_pubkey: app.identity.pubkey_hex.clone(),
        subject,
        body,
        reply_to,
        created_at: now.format("%Y-%m-%d %H:%M").to_string(),
        raw_created: now,
    });
}
