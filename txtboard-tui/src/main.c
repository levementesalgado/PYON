#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include "ui.h"
#include "home.h"
#include "relay.h"
#include "board_view.h"
#include "boards.h"
#include "config.h"

static Config cfg;

static void on_resize(int sig) {
    (void)sig;
    endwin(); refresh(); clear();
}

int main(void) {
    signal(SIGWINCH, on_resize);
    signal(SIGPIPE,  SIG_IGN);

    config_load(&cfg);
    ui_init();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);

    /* ── conexão persistente: inicia ANTES de qualquer tela ── */
    relay_connect(cfg.my_name, cfg.my_pubkey,
                  cfg.relay_host[0] ? cfg.relay_host : NULL,
                  cfg.relay_port);

    /* splash */
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    clear();
    attron(COLOR_PAIR(C_BOARD)|A_BOLD);
    mvprintw(rows/2-2,(cols-24)/2,"  *  PYON v0.2-alpha  *  ");
    attroff(COLOR_PAIR(C_BOARD)|A_BOLD);
    attron(COLOR_PAIR(C_TITLE)|A_BOLD);
    mvprintw(rows/2,(cols-36)/2,"Peer Yet Onnected Network  (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧");
    attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
    attron(COLOR_PAIR(C_DIM)|A_BOLD);
    if (cfg.access_code[0])
        mvprintw(rows/2+2,(cols-(int)strlen(cfg.access_code)-10)/2,
                 "acesso: %s", cfg.access_code);
    else
        mvprintw(rows/2+2,(cols-32)/2,
                 "rode 'pyon-core' para criar identidade");
    /* mostra status da conexão no splash */
    if (relay_connected())
        mvprintw(rows/2+4,(cols-24)/2,"  relay: conectado ✓  ");
    else if (cfg.relay_host[0])
        mvprintw(rows/2+4,(cols-30)/2,"  relay: conectando…  ");
    attroff(COLOR_PAIR(C_DIM)|A_BOLD);
    refresh();
    napms(700);

    while(1) {
        /* poll em cada iteração do loop principal — mantém pipe drenado */
        relay_tick();

        int sel = home_run(&cfg);
        if (sel == -1) break;
        if (sel == -2) {
            /* tecla 'r' na home → abre tela de chat */
            relay_run(cfg.my_name, cfg.my_pubkey,
                      "geral", cfg.relay_host, cfg.relay_port);
            continue;
        }
        if (sel >= 0 && sel < NUM_BOARDS)
            board_view_run(&BOARDS[sel], &cfg);
    }

    relay_disconnect();
    ui_teardown();
    return 0;
}
