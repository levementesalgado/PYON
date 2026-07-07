use ratatui::layout::{Constraint, Direction, Layout, Rect};
use ratatui::style::Modifier;
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders, Paragraph, ListItem, List, Wrap};
use ratatui::Frame;

use crate::app::{App, ChatEntry, Screen};
use crate::ui::*;

pub fn render_chat(f: &mut Frame, app: &mut App) {
    let area = f.area();

    let main = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Length(1), Constraint::Min(0), Constraint::Length(3)])
        .split(area);

    let middle = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Length(26), Constraint::Min(0)])
        .split(main[1]);

    let title = if let Some(ref target) = app.chat_dm_target {
        format!(" DM:{} ", target)
    } else {
        format!(" #{} ", app.channel)
    };
    render_topbar(f, main[0], &title, app.relay_connected, &app.display_name);

    render_sidebar(f, middle[0], app);
    render_messages(f, middle[1], app);
    render_input(f, main[2], app);
}

fn render_sidebar(f: &mut Frame, area: Rect, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([Constraint::Length(1), Constraint::Min(0), Constraint::Length(1), Constraint::Min(0)])
        .split(area);

    let channel_name = format!(" #{} ", app.channel);
    let channel_style = if app.chat_focus_sidebar { C_SELECTED } else { C_HEADER };
    f.render_widget(
        Paragraph::new(Line::from(Span::styled(channel_name, channel_style.add_modifier(Modifier::BOLD)))),
        chunks[0],
    );

    // Users
    let mut user_items: Vec<ListItem> = Vec::new();
    let online = app.chat_users.len();
    user_items.push(ListItem::new(Line::from(Span::styled(
        format!(" online: {}", online), C_DIM,
    ))));

    for (i, user) in app.chat_users.iter().enumerate() {
        let is_selected = app.chat_focus_sidebar
            && app.chat_dm_target.is_none()
            && app.chat_selected_user == Some(i);
        let style = if is_selected { C_SELECTED } else if user.has_unread { C_NICK.add_modifier(Modifier::BOLD) } else { C_NICK };
        let indicator = if user.has_unread { " ●" } else { "  " };
        user_items.push(ListItem::new(Line::from(Span::styled(
            format!("{}{}", indicator, truncate(&user.name, 20)),
            style,
        ))));
    }
    let users_list = List::new(user_items).block(Block::default().style(C_DIM));
    f.render_widget(users_list, chunks[1]);

    // DMs
    let dm_header = if app.chat_focus_sidebar && app.chat_dm_target.is_some() {
        C_SELECTED
    } else {
        C_DIM
    };
    f.render_widget(
        Paragraph::new(Line::from(Span::styled(" DM ", dm_header.add_modifier(Modifier::BOLD)))),
        chunks[2],
    );

    let mut dm_items: Vec<ListItem> = Vec::new();
    for dm in &app.chat_dms {
        let is_selected = app.chat_focus_sidebar
            && app.chat_dm_target.as_deref() == Some(&dm.name);
        let style = if is_selected {
            C_SELECTED
        } else if dm.has_unread {
            C_DM.add_modifier(Modifier::BOLD)
        } else {
            C_DM
        };
        let indicator = if dm.has_unread { " ●" } else { "  " };
        dm_items.push(ListItem::new(Line::from(Span::styled(
            format!("{}{}", indicator, truncate(&dm.name, 20)),
            style,
        ))));
    }
    if dm_items.is_empty() {
        dm_items.push(ListItem::new(Line::from(Span::styled("  (vazio)", C_DIM))));
    }
    let dms_list = List::new(dm_items);
    f.render_widget(dms_list, chunks[3]);
}

fn render_messages(f: &mut Frame, area: Rect, app: &App) {
    let mut lines: Vec<Line> = Vec::new();

    for msg in &app.chat_messages {
        if msg.is_system {
            let text = format!(" ── {} ──", msg.body);
            lines.push(Line::from(Span::styled(text, C_SYSTEM)));
        } else if msg.is_dm {
            let nick = Span::styled(msg.nick.clone(), C_DM.add_modifier(Modifier::BOLD));
            lines.push(Line::from(vec![
                Span::styled("DM ", C_DM),
                nick,
                Span::raw(": "),
                Span::styled(msg.body.clone(), C_NORMAL),
                Span::styled(format!(" [{}]", msg.timestamp), C_DIM),
            ]));
        } else {
            lines.push(Line::from(vec![
                Span::styled(format!("{} ", msg.nick), C_NICK.add_modifier(Modifier::BOLD)),
                Span::styled(msg.body.clone(), C_NORMAL),
                Span::styled(format!(" [{}]", msg.timestamp), C_DIM),
            ]));
        }
    }

    if lines.is_empty() {
        lines.push(Line::from(Span::styled(" ♡ Nenhuma mensagem ainda.", C_DIM)));
    }

    let scroll = app.chat_scroll;
    let visible_lines: Vec<Line> = if lines.len() > area.height as usize {
        let start = lines.len().saturating_sub(area.height as usize).min(scroll);
        lines[start..].to_vec()
    } else {
        lines
    };

    let para = Paragraph::new(visible_lines).wrap(Wrap { trim: false })
        .block(Block::default().borders(Borders::NONE));
    f.render_widget(para, area);
}

fn render_input(f: &mut Frame, area: Rect, app: &App) {
    let ctx = if let Some(ref target) = app.chat_dm_target {
        format!(" DM:{} ", target)
    } else {
        format!(" #{} ", app.channel)
    };
    let input_style = if !app.chat_focus_sidebar { C_INPUT } else { C_DIM };

    let input_line = Line::from(vec![
        Span::styled(ctx.as_str(), C_BOARD_BOLD),
        Span::styled(app.chat_input.as_str(), input_style),
    ]);

    let block = Block::default()
        .borders(Borders::ALL)
        .style(C_INPUT);
    f.render_widget(block, area);

    let inner = Layout::default().margin(1).split(area)[0];
    f.render_widget(Paragraph::new(input_line), inner);

    if !app.chat_focus_sidebar {
        let cursor_x = inner.x + ctx.len() as u16 + app.chat_input.len() as u16;
        f.set_cursor_position((cursor_x.min(area.right().saturating_sub(2)), inner.y));
    }
}

pub fn handle_key(app: &mut App, key: crossterm::event::KeyCode) {
    match key {
        crossterm::event::KeyCode::Tab => {
            app.chat_focus_sidebar = !app.chat_focus_sidebar;
        }
        crossterm::event::KeyCode::Esc => {
            if app.chat_dm_target.is_some() {
                app.chat_dm_target = None;
                app.chat_selected_user = None;
            } else {
                app.screen = Screen::Home;
            }
        }
        _ => {
            if app.chat_focus_sidebar {
                handle_sidebar_key(app, key);
            } else {
                handle_input_key(app, key);
            }
        }
    }
}

fn handle_sidebar_key(app: &mut App, key: crossterm::event::KeyCode) {
    match key {
        crossterm::event::KeyCode::Up | crossterm::event::KeyCode::Char('k') => {
            let total = app.chat_users.len() + app.chat_dms.len();
            if total == 0 { return; }
            let current = app.chat_selected_user.unwrap_or(0);
            if current > 0 { app.chat_selected_user = Some(current - 1); }
        }
        crossterm::event::KeyCode::Down | crossterm::event::KeyCode::Char('j') => {
            let total = app.chat_users.len() + app.chat_dms.len();
            if total == 0 { return; }
            let current = app.chat_selected_user.unwrap_or(0);
            if current + 1 < total { app.chat_selected_user = Some(current + 1); }
        }
        crossterm::event::KeyCode::Enter => {
            if let Some(idx) = app.chat_selected_user {
                if idx < app.chat_users.len() {
                    let user = &app.chat_users[idx];
                    app.chat_dm_target = Some(user.name.clone());
                    app.chat_focus_sidebar = false;
                    if !app.chat_dms.iter().any(|d| d.pubkey == user.pubkey) {
                        app.chat_dms.push(crate::app::DmEntry {
                            pubkey: user.pubkey.clone(),
                            name: user.name.clone(),
                            last_message: String::new(),
                            has_unread: false,
                        });
                    }
                } else {
                    let dm_idx = idx - app.chat_users.len();
                    if dm_idx < app.chat_dms.len() {
                        let name = app.chat_dms[dm_idx].name.clone();
                        app.chat_dm_target = Some(name);
                        app.chat_focus_sidebar = false;
                    }
                }
            }
        }
        _ => {}
    }
}

fn handle_input_key(app: &mut App, key: crossterm::event::KeyCode) {
    match key {
        crossterm::event::KeyCode::Enter => {
            let input = app.chat_input.trim().to_string();
            if input.is_empty() { return; }

            if input.starts_with("/dm ") {
                let target = input[4..].trim().to_string();
                if !target.is_empty() {
                    if let Some(user) = app.chat_users.iter().find(|u| u.name == target) {
                        app.chat_dm_target = Some(user.name.clone());
                        if !app.chat_dms.iter().any(|d| d.pubkey == user.pubkey) {
                            app.chat_dms.push(crate::app::DmEntry {
                                pubkey: user.pubkey.clone(),
                                name: user.name.clone(),
                                last_message: String::new(),
                                has_unread: false,
                            });
                        }
                    }
                }
            } else if let Some(ref target) = app.chat_dm_target.clone() {
                let to_pubkey = app.chat_users.iter()
                    .find(|u| u.name == *target)
                    .map(|u| u.pubkey.clone())
                    .unwrap_or_default();
                if !to_pubkey.is_empty() {
                    app.relay_send_dm(&to_pubkey, &input);
                }
                let now = chrono::Utc::now();
                app.chat_messages.push(ChatEntry {
                    is_system: false, is_dm: true,
                    nick: format!("→ {}", target),
                    body: input,
                    timestamp: now.format("%H:%M").to_string(),
                });
            } else {
                app.relay_send_chat(&input);
                let now = chrono::Utc::now();
                app.chat_messages.push(ChatEntry {
                    is_system: false, is_dm: false,
                    nick: app.display_name.clone(),
                    body: input,
                    timestamp: now.format("%H:%M").to_string(),
                });
            }

            app.chat_input.clear();
            app.chat_input_cursor = 0;
            app.chat_scroll = app.chat_messages.len().saturating_sub(1);
        }
        crossterm::event::KeyCode::Backspace => {
            if app.chat_input_cursor > 0 {
                app.chat_input.remove(app.chat_input_cursor - 1);
                app.chat_input_cursor -= 1;
            }
        }
        crossterm::event::KeyCode::Delete => {
            if app.chat_input_cursor < app.chat_input.len() {
                app.chat_input.remove(app.chat_input_cursor);
            }
        }
        crossterm::event::KeyCode::Left => {
            if app.chat_input_cursor > 0 { app.chat_input_cursor -= 1; }
        }
        crossterm::event::KeyCode::Right => {
            if app.chat_input_cursor < app.chat_input.len() { app.chat_input_cursor += 1; }
        }
        crossterm::event::KeyCode::Home => { app.chat_input_cursor = 0; }
        crossterm::event::KeyCode::End => { app.chat_input_cursor = app.chat_input.len(); }
        crossterm::event::KeyCode::PageUp => {
            app.chat_scroll = (app.chat_scroll + 20).min(app.chat_messages.len());
        }
        crossterm::event::KeyCode::PageDown => {
            app.chat_scroll = app.chat_scroll.saturating_sub(20);
        }
        crossterm::event::KeyCode::Char(c) => {
            app.chat_input.insert(app.chat_input_cursor, c);
            app.chat_input_cursor += 1;
        }
        _ => {}
    }
}
