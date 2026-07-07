use ratatui::layout::{Constraint, Direction, Layout};
use ratatui::style::Modifier;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, ListItem, List};
use ratatui::Frame;

use crate::app::{App, ComposeState, TreeNode, Screen};
use crate::ui::*;

pub fn render_thread(f: &mut Frame, app: &mut App) {
    let area = f.area();
    let constraints = if app.thread_reply.is_some() {
        vec![Constraint::Length(1), Constraint::Min(0)]
    } else {
        vec![Constraint::Length(1), Constraint::Min(0), Constraint::Length(1)]
    };

    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints(constraints)
        .split(area);

    let post_id = app.thread_post_id.unwrap_or(0);
    render_topbar(f, chunks[0], &format!(" /{} | #{} ", app.current_board.as_deref().unwrap_or("?"), post_id), app.relay_connected, &app.display_name);

    // ── Tree display ──────────────────────────────────────────────
    let tree = build_tree_display(app);
    let mut items: Vec<ListItem> = Vec::new();

    for node in &tree {
        let indent = "  ".repeat(node.depth);
        let is_root = node.depth == 0;
        let prefix = if is_root {
            "◆"
        } else if node.has_children {
            "├"
        } else {
            "└"
        };

        let subj = if node.post.subject.is_empty() {
            "(sem assunto)".to_string()
        } else {
            truncate(&node.post.subject, 30)
        };

        items.push(ListItem::new(Line::from(vec![
            Span::styled(format!("{}{} ", indent, prefix), if is_root { C_BOARD_BOLD } else { C_LINE }),
            Span::styled(format!("#{} ", node.post.id), C_POST_COUNT),
            Span::styled(subj, if is_root { C_NORMAL.add_modifier(Modifier::BOLD) } else { C_NORMAL }),
            Span::raw(" "),
            Span::styled(format!("— {}", truncate(&node.post.author_name, 16)), C_NICK),
            Span::raw(" "),
            Span::styled(node.post.created_at.clone(), C_DIM),
        ])));

        // Full body for root post, preview for replies
        if is_root {
            for line in node.post.body.lines() {
                items.push(ListItem::new(Line::from(Span::styled(
                    format!("   {}", line),
                    C_NORMAL,
                ))));
            }
        } else {
            if let Some(first) = node.post.body.lines().next() {
                if !first.is_empty() {
                    items.push(ListItem::new(Line::from(Span::styled(
                        format!("{}  {}", "  ".repeat(node.depth + 1), truncate(first, 50)),
                        C_DIM,
                    ))));
                }
            }
        }
    }

    let block = Block::default()
        .title(format!(" {} posts ", tree.len()))
        .borders(ratatui::widgets::Borders::NONE);
    let list = List::new(items).block(block).highlight_style(C_SELECTED);
    f.render_widget(list, chunks[1]);

    if app.thread_reply.is_none() {
        render_help_bar(f, chunks[2], &[
            ("r", "responder"), ("PageUp/Down", "rolar"), ("ESC", "voltar"),
        ]);
    }

    // Reply overlay
    if let Some(ref compose) = app.thread_reply {
        render_reply_overlay(f, area, compose);
    }
}

fn build_tree_display(app: &App) -> Vec<TreeNode> {
    if !app.thread_tree.is_empty() {
        return app.thread_tree.clone();
    }
    app.thread_posts
        .iter()
        .map(|p| TreeNode {
            post: p.clone(),
            depth: if p.reply_to.is_some() { 1 } else { 0 },
            has_children: app.thread_posts.iter().any(|c| c.reply_to == Some(p.id)),
        })
        .collect()
}

fn render_reply_overlay(f: &mut Frame, area: ratatui::layout::Rect, compose: &ComposeState) {
    let inner = render_centered_block(f, area, 60, 40, " Responder ", C_NICK);

    let body_style = C_INPUT;
    let body_text = if compose.body.is_empty() {
        vec![Line::from(Span::styled(" (digite sua resposta)", C_DIM))]
    } else {
        compose.body.lines().map(|l| Line::from(Span::styled(l.to_string(), body_style))).collect()
    };
    f.render_widget(
        ratatui::widgets::Paragraph::new(body_text).block(
            Block::default().borders(Borders::ALL).title(" resposta "),
        ),
        inner,
    );

    let cursor_x = inner.x + 1 + (compose.body.len() as u16 % inner.width.saturating_sub(2));
    let cursor_y = inner.y + 1 + (compose.body.len() as u16 / inner.width.saturating_sub(2));
    f.set_cursor_position((cursor_x, cursor_y));

    // Help
    let help_area = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Min(0), Constraint::Length(1)])
        .split(inner)[1];
    render_help_bar(f, help_area, &[
        ("Enter", "responder"), ("ESC", "cancelar"),
    ]);
}

pub fn handle_key(app: &mut App, key: crossterm::event::KeyCode) {
    if let Some(ref mut compose) = app.thread_reply {
        match key {
            crossterm::event::KeyCode::Esc => {
                app.thread_reply = None;
            }
            crossterm::event::KeyCode::Enter => {
                if !compose.body.is_empty() {
                    let body = compose.body.clone();
                    let board = app.current_board.as_deref().unwrap_or("sr").to_string();
                    let reply_to = app.thread_post_id;
                    let id = std::time::SystemTime::now()
                        .duration_since(std::time::UNIX_EPOCH)
                        .unwrap()
                        .as_millis() as u64;
                    let subject = String::new();
                    app.relay_send_post(&board, id, &body, &subject, reply_to.unwrap_or(0));

                    let now = chrono::Utc::now();
                    app.thread_posts.push(crate::app::PostDisplay {
                        id,
                        board: board.clone(),
                        author_name: app.display_name.clone(),
                        author_pubkey: app.identity.pubkey_hex.clone(),
                        subject: String::new(),
                        body: body.clone(),
                        reply_to,
                        created_at: now.format("%Y-%m-%d %H:%M").to_string(),
                        raw_created: now,
                    });
                    app.thread_tree = crate::app::build_thread_tree(&app.thread_posts);
                    app.thread_reply = None;
                }
            }
            crossterm::event::KeyCode::Backspace => {
                compose.body.pop();
            }
            crossterm::event::KeyCode::Char(c) => {
                compose.body.push(c);
            }
            _ => {}
        }
        return;
    }

    match key {
        crossterm::event::KeyCode::Char('r') => {
            app.thread_reply = Some(ComposeState {
                subject: String::new(),
                body: String::new(),
                cursor_subject: false,
            });
        }
        crossterm::event::KeyCode::Esc => {
            app.screen = Screen::Board;
        }
        _ => {}
    }
}
