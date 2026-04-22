#pragma once
#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>

/* ── pares de cor ─────────────────────────────────────────────── */
#define C_NORMAL   1   /* branco / texto padrão */
#define C_TITLE    2   /* ciano — títulos */
#define C_BOARD    3   /* magenta — slugs de board / anime */
#define C_SEL      4   /* preto sobre magenta — seleção */
#define C_SYS      5   /* azul — mensagens de sistema */
#define C_NICK     6   /* verde — nomes de usuário */
#define C_DIM      7   /* cinza — timestamps, bordas */
#define C_WARN     8   /* amarelo — avisos, padrão, DM */
#define C_INPUT    9   /* branco sobre azul — barra de input */
#define C_HEADER  10   /* preto sobre ciano — topbar */
#define C_CULT    11   /* ciano — cultura */
#define C_UNI     12   /* verde — universitário */
#define C_DIRETO  13   /* branco — direto */
#define C_DM      14   /* amarelo — mensagens diretas */

/* ── primitivas de UI ─────────────────────────────────────────── */
void ui_init(void);
void ui_teardown(void);

/* topbar preta/ciano */
void ui_topbar(int cols, const char *left, const char *right);

/* rodapé de status */
void ui_footbar(int row, int cols, const char *text);

/* box com título centralizado */
void ui_box(WINDOW *w, const char *title);

/* separador ── label ── */
void ui_sep(int row, int col, int width, const char *label, int attr);

/* lê tecla com fallback de escape sequences (Slackware/xterm) */
int ui_key(void);

/* lê tecla com timeout em décimos de segundo; ERR = timeout */
int ui_key_timeout(int tenths);

/* input inline: retorna 1=enter, -1=esc, 0=continua */
int ui_input(char *buf, int bufsz, int *cur, int ch);

/* modal de uma linha: retorna 1=ok 0=cancelado */
int ui_modal(const char *prompt, char *buf, int bufsz);
