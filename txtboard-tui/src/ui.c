#include "ui.h"

void ui_init(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak(); noecho();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    if (!has_colors()) { endwin(); fprintf(stderr,"sem cores\n"); exit(1); }
    start_color();
    use_default_colors();

    init_pair(C_NORMAL,  COLOR_WHITE,   -1);
    init_pair(C_TITLE,   COLOR_CYAN,    -1);
    init_pair(C_BOARD,   COLOR_MAGENTA, -1);
    init_pair(C_SEL,     COLOR_BLACK,   COLOR_MAGENTA);
    init_pair(C_SYS,     COLOR_BLUE,    -1);
    init_pair(C_NICK,    COLOR_GREEN,   -1);
    init_pair(C_DIM,     COLOR_BLACK,   -1);
    init_pair(C_WARN,    COLOR_YELLOW,  -1);
    init_pair(C_INPUT,   COLOR_WHITE,   COLOR_BLUE);
    init_pair(C_HEADER,  COLOR_BLACK,   COLOR_CYAN);
    init_pair(C_CULT,    COLOR_CYAN,    -1);
    init_pair(C_UNI,     COLOR_GREEN,   -1);
    init_pair(C_DIRETO,  COLOR_WHITE,   -1);
    init_pair(C_DM,      COLOR_YELLOW,  -1);
}

void ui_teardown(void) { endwin(); }

void ui_topbar(int cols, const char *left, const char *right) {
    attron(COLOR_PAIR(C_HEADER)|A_BOLD);
    mvhline(0, 0, ' ', cols);
    if (left)  mvprintw(0, 2, "%s", left);
    if (right) mvprintw(0, cols-(int)strlen(right)-2, "%s", right);
    attroff(COLOR_PAIR(C_HEADER)|A_BOLD);
}

void ui_footbar(int row, int cols, const char *text) {
    attron(COLOR_PAIR(C_SYS)|A_BOLD);
    mvhline(row, 0, ' ', cols);
    if (text) mvprintw(row, 2, "%s", text);
    attroff(COLOR_PAIR(C_SYS)|A_BOLD);
}

void ui_box(WINDOW *w, const char *title) {
    box(w, 0, 0);
    if (title && title[0]) {
        int c   = getmaxx(w);
        int tl  = (int)strlen(title);
        int tx  = (c - tl - 4) / 2;
        if (tx < 1) tx = 1;
        wattron(w, COLOR_PAIR(C_TITLE)|A_BOLD);
        mvwprintw(w, 0, tx, "[ %s ]", title);
        wattroff(w, COLOR_PAIR(C_TITLE)|A_BOLD);
    }
}

void ui_sep(int row, int col, int width, const char *label, int attr) {
    int llen = label ? (int)strlen(label)+2 : 0;
    int left = (width - llen) / 2;
    int right = width - llen - left;
    attron(COLOR_PAIR(C_DIM));
    for (int i=0;i<left;i++)  mvaddch(row, col+i, ACS_HLINE);
    attroff(COLOR_PAIR(C_DIM));
    if (label && label[0]) {
        attron(attr|A_BOLD);
        mvprintw(row, col+left, " %s", label);
        attroff(attr|A_BOLD);
    }
    attron(COLOR_PAIR(C_DIM));
    for (int i=0;i<right;i++) mvaddch(row, col+left+llen+i, ACS_HLINE);
    attroff(COLOR_PAIR(C_DIM));
}

int ui_key(void) {
    int ch = getch();
    if (ch == 27) {
        nodelay(stdscr, TRUE);
        int c2 = getch(); nodelay(stdscr, FALSE);
        if (c2 == ERR) return 27;
        if (c2 == '[') {
            int c3 = getch();
            switch(c3) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case '5': getch(); return KEY_PPAGE;
                case '6': getch(); return KEY_NPAGE;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
        return 27;
    }
    return ch;
}

int ui_key_timeout(int tenths) {
    halfdelay(tenths);
    int ch = getch();
    nocbreak(); cbreak(); keypad(stdscr, TRUE);
    if (ch == ERR) return ERR;
    /* expand escapes */
    if (ch == 27) {
        nodelay(stdscr, TRUE);
        int c2 = getch(); nodelay(stdscr, FALSE);
        if (c2 == ERR) return 27;
        if (c2 == '[') {
            int c3 = getch();
            switch(c3) {
                case 'A': return KEY_UP;
                case 'B': return KEY_DOWN;
                case 'C': return KEY_RIGHT;
                case 'D': return KEY_LEFT;
                case '5': getch(); return KEY_PPAGE;
                case '6': getch(); return KEY_NPAGE;
            }
        }
        return 27;
    }
    return ch;
}

int ui_input(char *buf, int bufsz, int *cur, int ch) {
    int len = (int)strlen(buf);
    switch (ch) {
        case 27:               return -1;
        case '\n': case KEY_ENTER: return 1;
        case KEY_LEFT:  if(*cur>0) (*cur)--; break;
        case KEY_RIGHT: if(*cur<len) (*cur)++; break;
        case KEY_HOME:  *cur=0; break;
        case KEY_END:   *cur=len; break;
        case KEY_BACKSPACE: case 127:
            if (*cur>0) {
                memmove(buf+*cur-1, buf+*cur, len-*cur+1);
                (*cur)--;
            } break;
        default:
            if (ch>=32 && ch<127 && len<bufsz-1) {
                memmove(buf+*cur+1, buf+*cur, len-*cur+1);
                buf[(*cur)++] = (char)ch;
            } break;
    }
    return 0;
}

int ui_modal(const char *prompt, char *buf, int bufsz) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int pw  = (int)strlen(prompt);
    int fy  = rows/2;
    int fx  = 3;
    int fw  = cols - 6;

    attron(COLOR_PAIR(C_INPUT)|A_BOLD);
    for (int r=fy-1;r<=fy+1;r++) mvhline(r, fx-1, ' ', fw+2);
    mvprintw(fy, fx, "%s", prompt);
    attroff(COLOR_PAIR(C_INPUT)|A_BOLD);

    curs_set(1);
    int cur = (int)strlen(buf);
    int ret = 0;
    while (1) {
        int avail = fw - pw - 1;
        attron(COLOR_PAIR(C_NORMAL));
        mvprintw(fy, fx+pw+1, "%-*.*s", avail, avail, buf);
        if (cur < avail) {
            attron(A_REVERSE);
            mvaddch(fy, fx+pw+1+cur, buf[cur] ? (unsigned char)buf[cur] : ' ');
            attroff(A_REVERSE);
        }
        attroff(COLOR_PAIR(C_NORMAL));
        refresh();
        int ch = ui_key();
        int r  = ui_input(buf, bufsz, &cur, ch);
        if (r ==  1) { ret=1; break; }
        if (r == -1) { ret=0; break; }
    }
    curs_set(0);
    return ret;
}
