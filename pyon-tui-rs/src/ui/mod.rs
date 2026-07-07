pub mod home;
pub mod board;
pub mod thread;
pub mod chat;

use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span};
use ratatui::widgets::{Block, Borders};
use ratatui::layout::{Alignment, Constraint, Direction, Layout, Rect};
use ratatui::Frame;

/// ── Color palette ──────────────────────────────────────────────
pub const C_NORMAL: Style = Style::new().fg(Color::White);
pub const C_TITLE: Style = Style::new().fg(Color::Cyan).add_modifier(Modifier::BOLD);
pub const C_BOARD: Style = Style::new().fg(Color::Magenta);
pub const C_BOARD_BOLD: Style = Style::new().fg(Color::Magenta).add_modifier(Modifier::BOLD);
pub const C_SELECTED: Style = Style::new().fg(Color::Black).bg(Color::Magenta);
pub const C_SYSTEM: Style = Style::new().fg(Color::Blue);
pub const C_NICK: Style = Style::new().fg(Color::Green);
pub const C_DIM: Style = Style::new().fg(Color::DarkGray);
pub const C_WARN: Style = Style::new().fg(Color::Yellow);
pub const C_INPUT: Style = Style::new().fg(Color::White).bg(Color::Blue);
pub const C_HEADER: Style = Style::new().fg(Color::Black).bg(Color::Cyan);
pub const C_DM: Style = Style::new().fg(Color::Yellow);
pub const C_ACCENT: Style = Style::new().fg(Color::LightCyan);
pub const C_POST_COUNT: Style = Style::new().fg(Color::LightBlue);
pub const C_REPLY_BADGE: Style = Style::new().fg(Color::LightYellow).add_modifier(Modifier::BOLD);
pub const C_LINE: Style = Style::new().fg(Color::DarkGray);

/// Truncate a string to max_len chars, appending "…" if truncated.
pub fn truncate(s: &str, max_len: usize) -> String {
    if s.len() > max_len {
        format!("{}…", &s[..max_len.saturating_sub(1)])
    } else {
        s.to_string()
    }
}

/// Render a centered block overlay (e.g. for compose/search).
pub fn render_centered_block(
    f: &mut Frame, area: Rect, w_pct: u16, h_pct: u16, title: &str, style: Style,
) -> Rect {
    let vert = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Percentage((100 - h_pct) / 2),
            Constraint::Percentage(h_pct),
            Constraint::Percentage((100 - h_pct) / 2),
        ])
        .split(area)[1];
    let horiz = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([
            Constraint::Percentage((100 - w_pct) / 2),
            Constraint::Percentage(w_pct),
            Constraint::Percentage((100 - w_pct) / 2),
        ])
        .split(vert)[1];
    let block = Block::default()
        .borders(Borders::ALL)
        .title(format!(" {} ", title))
        .style(style);
    f.render_widget(ratatui::widgets::Clear, horiz);
    f.render_widget(block, horiz);
    let inner = Layout::default().margin(1).split(horiz)[0];
    inner
}

/// Render a topbar with connection status and navigation context.
pub fn render_topbar(f: &mut Frame, area: Rect, title: &str, connected: bool, name: &str) {
    let status = if connected { " ● " } else { " ○ " };
    let status_color = if connected { Color::Green } else { Color::DarkGray };
    let text = Line::from(vec![
        Span::styled(format!(" {} ", title), C_HEADER),
        Span::raw(" "),
        Span::styled(name, C_DIM),
        Span::raw(" "),
        Span::styled(status, Style::new().fg(status_color).add_modifier(Modifier::BOLD)),
    ]);
    let block = Block::new().style(C_HEADER);
    f.render_widget(block, area);
    f.render_widget(ratatui::widgets::Paragraph::new(text).alignment(Alignment::Left), area);
}

/// Render a simple help/status bar at the bottom of the screen.
pub fn render_help_bar(f: &mut Frame, area: Rect, items: &[(&str, &str)]) {
    let parts: Vec<Span> = items
        .iter()
        .flat_map(|(key, desc)| {
            vec![
                Span::styled(format!(" {} ", key), C_DIM),
                Span::styled(*desc, C_ACCENT),
                Span::styled(" │", C_LINE),
            ]
        })
        .collect();
    let bar = Line::from(parts);
    f.render_widget(
        ratatui::widgets::Paragraph::new(bar).alignment(Alignment::Left),
        area,
    );
}
