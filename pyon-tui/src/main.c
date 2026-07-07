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

/* ── tela de splash/conexão ──────────────────────────────────────
 * Fica aqui até conectar (ou usuário pressionar qualquer tecla para
 * entrar em modo offline).
 * Retorna 1 = conectado, 0 = offline (usuário forçou) */
static int splash_screen(void) {
    int dots = 0;
    time_t last_dot = 0;

    while (1) {
        relay_tick();

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        erase();

        /* logo */
        attron(COLOR_PAIR(C_BOARD)|A_BOLD);
        const char *logo = "✦  P Y O N  ✦";
        mvprintw(rows/2-4, (cols-(int)strlen(logo))/2, "%s", logo);
        attroff(COLOR_PAIR(C_BOARD)|A_BOLD);

        attron(COLOR_PAIR(C_DIM));
        const char *sub = "Peer Yet Onnected Network  v0.2-alpha";
        mvprintw(rows/2-2, (cols-(int)strlen(sub))/2, "%s", sub);
        attroff(COLOR_PAIR(C_DIM));

        /* acesso */
        if (cfg.access_code[0]) {
            attron(COLOR_PAIR(C_TITLE)|A_BOLD);
            char ac[64]; snprintf(ac, sizeof(ac), "acesso: %s", cfg.access_code);
            mvprintw(rows/2, (cols-(int)strlen(ac))/2, "%s", ac);
            attroff(COLOR_PAIR(C_TITLE)|A_BOLD);
        }

        /* status de conexão */
        if (relay_connected()) {
            attron(COLOR_PAIR(C_NICK)|A_BOLD);
            const char *ok_msg = "relay conectado ✓  (◕‿◕✿)";
            mvprintw(rows/2+2, (cols-(int)strlen(ok_msg))/2, "%s", ok_msg);
            attroff(COLOR_PAIR(C_NICK)|A_BOLD);

            attron(COLOR_PAIR(C_DIM));
            const char *hint = "pressione qualquer tecla para continuar";
            mvprintw(rows/2+4, (cols-(int)strlen(hint))/2, "%s", hint);
            attroff(COLOR_PAIR(C_DIM));
        } else if (cfg.relay_host[0]) {
            /* animação de dots */
            time_t now = time(NULL);
            if (now != last_dot) { dots = (dots+1)%4; last_dot = now; }
            char connecting[64];
            snprintf(connecting, sizeof(connecting),
                "conectando a %s:%d%.*s",
                cfg.relay_host, cfg.relay_port, dots, "...");
            attron(COLOR_PAIR(C_WARN)|A_BOLD);
            mvprintw(rows/2+2, (cols-(int)strlen(connecting))/2, "%s", connecting);
            attroff(COLOR_PAIR(C_WARN)|A_BOLD);

            attron(COLOR_PAIR(C_DIM));
            const char *skip = "[ESC] entrar offline";
            mvprintw(rows/2+4, (cols-(int)strlen(skip))/2, "%s", skip);
            attroff(COLOR_PAIR(C_DIM));
        } else {
            attron(COLOR_PAIR(C_DIM)|A_BOLD);
            const char *off = "modo offline  (sem relay configurado)";
            mvprintw(rows/2+2, (cols-(int)strlen(off))/2, "%s", off);
            attroff(COLOR_PAIR(C_DIM)|A_BOLD);

            attron(COLOR_PAIR(C_DIM));
            const char *hint = "pressione qualquer tecla para continuar";
            mvprintw(rows/2+4, (cols-(int)strlen(hint))/2, "%s", hint);
            attroff(COLOR_PAIR(C_DIM));
        }

        wnoutrefresh(stdscr);
        doupdate();

        /* lê tecla com timeout curto para animar os dots */
        halfdelay(4);
        int ch = getch();
        nocbreak(); cbreak(); keypad(stdscr, TRUE);

        if (relay_connected()) {
            if (ch != ERR) return 1;  /* qualquer tecla após conectar → avança */
        } else if (!cfg.relay_host[0]) {
            if (ch != ERR) return 0;  /* sem relay configurado → offline direto */
        } else {
            if (ch == 27) return 0;   /* ESC → força offline */
            /* continua aguardando conexão */
        }
    }
}

int main(void) {
    signal(SIGWINCH, on_resize);
    signal(SIGPIPE,  SIG_IGN);

    config_load(&cfg);
    ui_init();
    keypad(stdscr, TRUE);
    meta(stdscr, TRUE);

    /* inicia conexão persistente */
    relay_connect(cfg.my_name, cfg.my_pubkey,
                  cfg.relay_host[0] ? cfg.relay_host : NULL,
                  cfg.relay_port);

    /* tela de conexão — fica aqui até conectar ou ESC */
    splash_screen();

    /* loop principal */
    while (1) {
        relay_tick();

        int sel = home_run(&cfg);
        if (sel == -1) break;
        if (sel == -2) {
            relay_run(cfg.my_name, cfg.my_pubkey,
                      "geral", cfg.relay_host, cfg.relay_port);
            /* força redraw completo ao voltar */
            clear(); refresh();
            continue;
        }
        if (sel >= 0 && sel < NUM_BOARDS) {
            board_view_run(&BOARDS[sel], &cfg);
            /* força redraw completo ao voltar */
            clear(); refresh();
        }
    }

    relay_disconnect();
    ui_teardown();
    return 0;
}
