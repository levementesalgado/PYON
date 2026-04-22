#pragma once
#include <signal.h>

/* ── fila de novos posts (relay → board_view) ────────────────── */
#define RELAY_POST_QUEUE_MAX 32

typedef struct {
    char board[16];
    int  post_id;
} RelayNewPost;

void relay_post_enqueue(const char *board, int post_id);
int  relay_post_dequeue(RelayNewPost *out);

/* ── API de conexão persistente ──────────────────────────────────
 * Uso:
 *   relay_connect(...)   → chamado UMA VEZ no main(), antes das telas
 *   relay_tick()         → chamado em CADA loop de QUALQUER tela
 *   relay_disconnect()   → chamado no exit do main()
 *   relay_connected()    → 1 se processo filho está vivo
 * ─────────────────────────────────────────────────────────────── */

/* Inicia o processo relay_client.rb em background.
 * host/port = NULL/0 → modo offline (sem processo filho).        */
void relay_connect(const char *my_name, const char *my_pubkey,
                   const char *host, int port);

/* Poll não-bloqueante: drena o pipe e atualiza estado interno.
 * Deve ser chamado pelo loop de cada tela (home, board, relay).  */
void relay_tick(void);

/* Encerra o processo filho limpa. Chamar antes de ui_teardown(). */
void relay_disconnect(void);

/* Retorna 1 se o processo filho está rodando.                    */
int relay_connected(void);

/* ── tela de chat (abre sobre a conexão já ativa) ───────────── */
void relay_run(const char *my_name, const char *my_pubkey,
               const char *channel, const char *host, int port);
