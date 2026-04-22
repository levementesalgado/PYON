#include "relay.h"
#include "ui.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

/* ── constantes ──────────────────────────────────────────────── */
#define MAX_MSGS     512
#define MAX_MSG_LEN  400
#define MAX_USERS     64
#define SIDEBAR_W     20   /* sidebar ESQUERDA */
#define INPUT_H        3
#define MAX_DM_CONVS  16
#define DM_HIST       128

/* ── tipos ───────────────────────────────────────────────────── */
typedef enum { MSG_CHAT, MSG_SYS, MSG_DM } MsgType;

typedef struct {
    MsgType type;
    char    ts[8];
    char    nick[32];
    char    pubkey[128];   /* pubkey do remetente (para DMs) */
    char    body[MAX_MSG_LEN];
    int     is_mine;       /* mensagem própria */
} Msg;

typedef struct {
    char pubkey[128];
    char name[32];
    int  unread;
} User;

/* conversa DM: histórico local por pubkey */
typedef struct {
    char pubkey[128];
    char name[32];
    Msg  hist[DM_HIST];
    int  count;
    int  unread;
} DMConv;

/* ── estado global da sessão ─────────────────────────────────── */
static Msg     msgs[MAX_MSGS];
static int     msg_count   = 0;
static User    users[MAX_USERS];
static int     user_count  = 0;
static int     scroll_off  = 0;
static DMConv  dms[MAX_DM_CONVS];
static int     dm_count    = 0;

/* estado do painel */
static int     view_mode   = 0;   /* 0=canal, 1=DM */
static int     dm_sel      = -1;  /* índice em dms[] da conversa aberta */
static int     user_sel    = 0;   /* cursor na lista de usuários */
static int     sidebar_focus = 0; /* 0=msgs, 1=sidebar */

/* pipe para relay_client.rb */
static FILE   *relay_in    = NULL;
static FILE   *relay_out   = NULL;
static int     pipe_in_fd  = -1;
static pid_t   relay_pid   = -1;

/* ── variáveis globais de sessão — declaradas cedo pois poll_relay e spawn_relay as usam ── */
static char g_my_name[64]   = {0};
static char g_my_pubkey[128]= {0};
static char g_channel[32]   = "geral";
static int  g_connected     = 0;

/* ── helpers de timestamp ─────────────────────────────────────── */
static void ts_now(char *buf, size_t n) {
    time_t t = time(NULL);
    strftime(buf, n, "%H:%M", localtime(&t));
}

/* ── push de mensagens ───────────────────────────────────────── */
static void push_msg(MsgType type, const char *nick, const char *pk,
                     const char *body, int is_mine) {
    if (msg_count >= MAX_MSGS) {
        memmove(&msgs[0], &msgs[1], sizeof(Msg)*(MAX_MSGS-1));
        msg_count = MAX_MSGS-1;
    }
    Msg *m = &msgs[msg_count++];
    m->type    = type;
    m->is_mine = is_mine;
    ts_now(m->ts, sizeof(m->ts));
    snprintf(m->nick,   sizeof(m->nick),   "%s", nick ? nick : "");
    snprintf(m->pubkey, sizeof(m->pubkey), "%s", pk   ? pk   : "");
    snprintf(m->body,   sizeof(m->body),   "%s", body);
    scroll_off = 0;
}
static void push_sys(const char *body) { push_msg(MSG_SYS,"✦sys✦","",body,0); }

/* ── gestão de usuários ──────────────────────────────────────── */
static void add_user(const char *name, const char *pk) {
    for (int i = 0; i < user_count; i++)
        if (!strcmp(users[i].pubkey, pk)) return;
    if (user_count >= MAX_USERS) return;
    snprintf(users[user_count].name,   32,  "%s", name);
    snprintf(users[user_count].pubkey, 128, "%s", pk);
    users[user_count].unread = 0;
    user_count++;
}
static void drop_user(const char *pk) {
    for (int i = 0; i < user_count; i++) {
        if (!strcmp(users[i].pubkey, pk)) {
            memmove(&users[i], &users[i+1], sizeof(User)*(user_count-i-1));
            user_count--; return;
        }
    }
}

/* ── gestão de DMs ───────────────────────────────────────────── */
static DMConv *find_or_create_dm(const char *pk, const char *name) {
    for (int i = 0; i < dm_count; i++)
        if (!strcmp(dms[i].pubkey, pk)) return &dms[i];
    if (dm_count >= MAX_DM_CONVS) return NULL;
    DMConv *d = &dms[dm_count++];
    memset(d, 0, sizeof(*d));
    snprintf(d->pubkey, 128, "%s", pk);
    snprintf(d->name,    32, "%s", name[0] ? name : pk[0] ? pk : "?");
    return d;
}

static void push_dm(const char *pk, const char *name,
                    const char *body, int is_mine) {
    DMConv *d = find_or_create_dm(pk, name);
    if (!d) return;
    if (d->count >= DM_HIST) {
        memmove(&d->hist[0], &d->hist[1], sizeof(Msg)*(DM_HIST-1));
        d->count = DM_HIST-1;
    }
    Msg *m = &d->hist[d->count++];
    m->type    = MSG_DM;
    m->is_mine = is_mine;
    ts_now(m->ts, sizeof(m->ts));
    snprintf(m->nick,   sizeof(m->nick),   "%s", is_mine ? "você" : name);
    snprintf(m->pubkey, sizeof(m->pubkey), "%s", pk);
    snprintf(m->body,   sizeof(m->body),   "%s", body);
    if (!is_mine && (dm_sel < 0 || strcmp(dms[dm_sel].pubkey, pk))) {
        d->unread++;
        /* marca usuário com unread */
        for (int i = 0; i < user_count; i++)
            if (!strcmp(users[i].pubkey, pk)) { users[i].unread++; break; }
    }
}

/* ── JSON mínimo ─────────────────────────────────────────────── */
static int jstr(const char *json, const char *key, char *out, size_t n) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p == '"') {
        p++; size_t i = 0;
        while (*p && *p != '"' && i < n-1) out[i++] = *p++;
        out[i] = 0; return 1;
    }
    size_t i = 0;
    while (*p && *p != ',' && *p != '}' && i < n-1) out[i++] = *p++;
    out[i] = 0; return 1;
}
static int jbool(const char *json, const char *key) {
    char b[8] = {0}; jstr(json,key,b,sizeof(b));
    return !strcmp(b,"true");
}

/* ── poll do pipe relay_client.rb ────────────────────────────── */
/* ── fila de novos posts (relay → board_view) ────────────────── */

static RelayNewPost post_queue[RELAY_POST_QUEUE_MAX];
static int post_queue_head = 0;
static int post_queue_tail = 0;

void relay_post_enqueue(const char *board, int post_id) {
    int next = (post_queue_tail + 1) % RELAY_POST_QUEUE_MAX;
    if (next == post_queue_head) return; /* fila cheia — descarta */
    snprintf(post_queue[post_queue_tail].board,
             sizeof(post_queue[0].board), "%s", board);
    post_queue[post_queue_tail].post_id = post_id;
    post_queue_tail = next;
}

int relay_post_dequeue(RelayNewPost *out) {
    if (post_queue_head == post_queue_tail) return 0;
    *out = post_queue[post_queue_head];
    post_queue_head = (post_queue_head + 1) % RELAY_POST_QUEUE_MAX;
    return 1;
}

/* ── poll do pipe relay_client.rb ────────────────────────────── */
static void poll_relay(void) {
    if (pipe_in_fd < 0) return;
    char line[1024];
    while (1) {
        fd_set fds; FD_ZERO(&fds); FD_SET(pipe_in_fd, &fds);
        struct timeval tv = {0,0};
        if (select(pipe_in_fd+1,&fds,NULL,NULL,&tv) <= 0) break;
        if (!fgets(line, sizeof(line), relay_in)) break;
        line[strcspn(line,"\n")] = 0;
        if (!line[0]) continue;

        char type[32]={0}, from[64]={0}, pk[128]={0}, body[MAX_MSG_LEN]={0};
        jstr(line,"type",type,sizeof(type));
        jstr(line,"from",from,sizeof(from));
        jstr(line,"pubkey",pk,sizeof(pk));
        jstr(line,"body",body,sizeof(body));

        if (!strcmp(type,"msg")) {
            int is_sys = jbool(line,"sys");
            int is_dm  = jbool(line,"dm");
            if (is_sys) push_sys(body);
            else if (is_dm) { push_dm(pk,from,body,0); }
            else {
                /* ignora mensagens do próprio usuário — já foram adicionadas
                   localmente no momento do envio com is_mine=1 */
                if (g_my_pubkey[0] && pk[0] && !strcmp(pk, g_my_pubkey)) continue;
                push_msg(MSG_CHAT,from,pk,body,0);
            }
        } else if (!strcmp(type,"join")) {
            add_user(from,pk);
            char s[128]; snprintf(s,sizeof(s),"*pyon!* %s entrou (ﾉ◕ヮ◕)ﾉ",from);
            push_sys(s);
        } else if (!strcmp(type,"leave")) {
            char s[128]; snprintf(s,sizeof(s),"*plop...* %s saiu (｡•́︿•̀｡)",from);
            push_sys(s); drop_user(pk);
        } else if (!strcmp(type,"error")) {
            char s[256];
            /* mensagem mais simpática quando o servidor não está rodando */
            if (strstr(body,"Connection refused") || strstr(body,"connect") ||
                strstr(body,"recusado") || strstr(body,"refused")) {
                snprintf(s,sizeof(s),
                    "*pip...* nenhum servidor pyon encontrado. "
                    "Rode pyon-srv primeiro, bb. (｡•́︿•̀｡)");
            } else {
                snprintf(s,sizeof(s),"*pip!* erro: %s",body);
            }
            push_sys(s);
        } else if (!strcmp(type,"post_new")) {
            /* novo post chegou da rede — enfileira para board_view recarregar */
            char board[16]={0}, pid_s[16]={0};
            jstr(line,"board",board,sizeof(board));
            jstr(line,"post_id",pid_s,sizeof(pid_s));
            if (board[0]) {
                relay_post_enqueue(board, atoi(pid_s));
                /* notifica no chat também */
                char s[64];
                snprintf(s,sizeof(s),"*ding~!* novo post em /%s/%s (◕‿◕✿)",
                         board, pid_s);
                push_sys(s);
            }
        }
    }
}

/* ── envia para o relay_client.rb ────────────────────────────── */
static void send_relay(const char *json) {
    if (!relay_out) return;
    fprintf(relay_out,"%s\n",json); fflush(relay_out);
}

/* ── spawn do relay_client.rb ────────────────────────────────── */
static int spawn_relay(const char *host, int port, const char *channel) {
    int to_rb[2], from_rb[2];
    if (pipe(to_rb)<0 || pipe(from_rb)<0) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        dup2(to_rb[0],  STDIN_FILENO);
        dup2(from_rb[1],STDOUT_FILENO);
        close(to_rb[0]); close(to_rb[1]);
        close(from_rb[0]); close(from_rb[1]);
        char ps[16]; snprintf(ps,sizeof(ps),"%d",port);
        /* g_my_name é o nome escolhido pelo utilizador — passado como 4º arg */
        const char *name = g_my_name[0] ? g_my_name : "anon";
        char cmd[1280];
        snprintf(cmd, sizeof(cmd),
            "RC=\"\"; "
            "for p in"
            " /usr/local/share/txtboard/lib/txtboard/relay_client.rb"
            " /usr/local/share/txtboard/relay_client.rb"
            " \"$HOME/.local/share/txtboard/relay_client.rb\"; do"
            "  [ -f \"$p\" ] && RC=\"$p\" && break; "
            "done; "
            "[ -z \"$RC\" ] && exit 1; "
            "torsocks ruby \"$RC\" '%s' '%s' '%s' '%s' 2>/dev/null"
            " || ruby \"$RC\" '%s' '%s' '%s' '%s'",
            host, ps, channel, name,
            host, ps, channel, name);
        execl("/bin/sh","sh","-c",cmd,NULL);
        _exit(1);
    }
    close(to_rb[0]); close(from_rb[1]);
    relay_out  = fdopen(to_rb[1],  "w");
    relay_in   = fdopen(from_rb[0],"r");
    pipe_in_fd = from_rb[0];
    fcntl(pipe_in_fd, F_SETFL, O_NONBLOCK);
    relay_pid = pid;
    return 0;
}

static void kill_relay(void) {
    if (relay_out) { send_relay("{\"type\":\"quit\"}"); fclose(relay_out); relay_out=NULL; }
    if (relay_in)  { fclose(relay_in);  relay_in=NULL;  }
    pipe_in_fd = -1;
    if (relay_pid > 0) { kill(relay_pid,SIGTERM); waitpid(relay_pid,NULL,WNOHANG); relay_pid=-1; }
}

/* ── API pública de conexão persistente ─────────────────────── */

void relay_connect(const char *my_name, const char *my_pubkey,
                   const char *host, int port) {
    if (my_name)   snprintf(g_my_name,   sizeof(g_my_name),   "%s", my_name);
    if (my_pubkey) snprintf(g_my_pubkey, sizeof(g_my_pubkey), "%s", my_pubkey);

    /* já conectado — não reconecta */
    if (relay_pid > 0) return;

    /* inicializa estado de mensagens */
    msg_count = 0; user_count = 0;
    dm_count  = 0; dm_sel     = -1;
    add_user(my_name ? my_name : "eu",
             my_pubkey ? my_pubkey : "local");

    if (!host || port <= 0) {
        g_connected = 0;
        push_sys("*nyaa~!* sem servidor configurado — modo offline. (◕‿◕✿)");
        return;
    }

    if (spawn_relay(host, port, g_channel) == 0) {
        g_connected = 1;
        char s[128];
        snprintf(s, sizeof(s), "*kyaa~!* conectando a %s:%d…", host, port);
        push_sys(s);
    } else {
        g_connected = 0;
        push_sys("*pip...* falha ao iniciar relay_client.rb (｡•́︿•̀｡)");
    }
}

void relay_tick(void) {
    /* drena o pipe — seguro chamar de qualquer tela */
    poll_relay();

    /* verifica se o processo filho ainda está vivo */
    if (relay_pid > 0) {
        int status;
        pid_t r = waitpid(relay_pid, &status, WNOHANG);
        if (r == relay_pid) {
            /* filho morreu */
            relay_pid  = -1;
            relay_in   = NULL;
            relay_out  = NULL;
            pipe_in_fd = -1;
            g_connected = 0;
            push_sys("*pip...* conexão com o servidor perdida (｡•́︿•̀｡)");
        }
    }
}

void relay_disconnect(void) {
    g_connected = 0;
    kill_relay();
}

int relay_connected(void) {
    return g_connected && relay_pid > 0;
}



/* Sidebar ESQUERDA — canal no topo, usuários embaixo.
 * Layout (mockup da Haruna):
 *  ## /geral          ← canal ativo
 *  ──────────────
 *  online (3)
 *  + misakichan       ← usuários, DMs com ! de unread
 *    yumeiri
 *  ──────────────
 *  DMs
 *  ! misakichan (1)
 */
static void draw_sidebar(WINDOW *sw, int rows,
                         const char *channel) {
    werase(sw);

    /* borda direita */
    wattron(sw, COLOR_PAIR(C_DIM));
    for (int r = 0; r < rows; r++) mvwaddch(sw, r, SIDEBAR_W-1, ACS_VLINE);
    wattroff(sw, COLOR_PAIR(C_DIM));

    /* header do painel */
    wattron(sw, COLOR_PAIR(C_HEADER)|A_BOLD);
    mvwprintw(sw, 0, 1, "%-*.*s", SIDEBAR_W-2, SIDEBAR_W-2, " pyon");
    wattroff(sw, COLOR_PAIR(C_HEADER)|A_BOLD);

    /* canal ativo */
    int y = 1;
    wattron(sw, COLOR_PAIR(C_SEL)|A_BOLD);
    mvwprintw(sw, y++, 1, "##");
    wattroff(sw, COLOR_PAIR(C_SEL)|A_BOLD);
    wattron(sw, COLOR_PAIR(C_TITLE)|A_BOLD);
    mvwprintw(sw, y-1, 4, "%-*.*s", SIDEBAR_W-6, SIDEBAR_W-6,
              channel ? channel : "geral");
    wattroff(sw, COLOR_PAIR(C_TITLE)|A_BOLD);

    /* separador */
    wattron(sw, COLOR_PAIR(C_DIM));
    mvwhline(sw, y++, 1, ACS_HLINE, SIDEBAR_W-2);
    wattroff(sw, COLOR_PAIR(C_DIM));

    /* usuários online */
    wattron(sw, COLOR_PAIR(C_DIM)|A_BOLD);
    mvwprintw(sw, y++, 1, " online (%d)", user_count);
    wattroff(sw, COLOR_PAIR(C_DIM)|A_BOLD);

    for (int i = 0; i < user_count && y < rows-4; i++) {
        int is_sel = (sidebar_focus && view_mode==0 && i == user_sel);
        if (is_sel)            wattron(sw, COLOR_PAIR(C_SEL)|A_BOLD);
        else if (users[i].unread) wattron(sw, COLOR_PAIR(C_WARN)|A_BOLD);
        else                   wattron(sw, COLOR_PAIR(C_NICK));
        mvwprintw(sw, y++, 1, "%s%-*.*s",
            users[i].unread ? "!" : "+",
            SIDEBAR_W-3, SIDEBAR_W-3, users[i].name);
        if (is_sel)            wattroff(sw, COLOR_PAIR(C_SEL)|A_BOLD);
        else if (users[i].unread) wattroff(sw, COLOR_PAIR(C_WARN)|A_BOLD);
        else                   wattroff(sw, COLOR_PAIR(C_NICK));
    }

    /* separador + DMs */
    if (dm_count > 0 && y < rows-2) {
        wattron(sw, COLOR_PAIR(C_DIM));
        mvwhline(sw, y++, 1, ACS_HLINE, SIDEBAR_W-2);
        wattroff(sw, COLOR_PAIR(C_DIM));
        wattron(sw, COLOR_PAIR(C_DIM)|A_BOLD);
        mvwprintw(sw, y++, 1, " DMs");
        wattroff(sw, COLOR_PAIR(C_DIM)|A_BOLD);
        for (int i = 0; i < dm_count && y < rows-1; i++) {
            int is_sel = (sidebar_focus && view_mode==1 && i == dm_sel);
            if (is_sel)          wattron(sw, COLOR_PAIR(C_SEL)|A_BOLD);
            else if (dms[i].unread) wattron(sw, COLOR_PAIR(C_WARN)|A_BOLD);
            else                 wattron(sw, COLOR_PAIR(C_DIM));
            mvwprintw(sw, y++, 1, "%s%-*.*s",
                dms[i].unread ? "!" : " ",
                SIDEBAR_W-3, SIDEBAR_W-3, dms[i].name);
            if (is_sel)          wattroff(sw, COLOR_PAIR(C_SEL)|A_BOLD);
            else                 attrset(A_NORMAL);
        }
    }

    /* hint na base */
    if (y < rows-1) {
        wattron(sw, COLOR_PAIR(C_DIM));
        mvwhline(sw, rows-2, 1, ACS_HLINE, SIDEBAR_W-2);
        mvwprintw(sw, rows-1, 1, "/dm nick");
        wattroff(sw, COLOR_PAIR(C_DIM));
    }

    wnoutrefresh(sw);
}

static void draw_msgs_area(WINDOW *mw, int mrows, int mcols,
                           Msg *buf, int count) {
    werase(mw);
    int visible = mrows - 2;
    int start   = count - visible - scroll_off;
    if (start < 0) start = 0;
    int row = 1;

    for (int i = start; i < count && row < mrows-1; i++) {
        Msg *m = &buf[i];
        if (m->type == MSG_SYS) {
            /* mensagem de sistema centralizada, estilo mockup */
            wattron(mw, COLOR_PAIR(C_DIM)|A_BOLD);
            mvwprintw(mw, row, 1, "── • ");
            wattroff(mw, COLOR_PAIR(C_DIM)|A_BOLD);
            wattron(mw, COLOR_PAIR(C_SYS)|A_BOLD);
            printw("%.*s", mcols-12, m->body);
            wattroff(mw, COLOR_PAIR(C_SYS)|A_BOLD);
            wattron(mw, COLOR_PAIR(C_DIM)|A_BOLD);
            printw(" •──");
            wattroff(mw, COLOR_PAIR(C_DIM)|A_BOLD);
        } else {
            /* ts */
            wattron(mw, COLOR_PAIR(C_DIM));
            mvwprintw(mw, row, 1, "%s", m->ts);
            wattroff(mw, COLOR_PAIR(C_DIM));

            /* nick — cor por tipo */
            int attr = m->is_mine    ? (COLOR_PAIR(C_WARN)|A_BOLD)
                     : m->type==MSG_DM ? (COLOR_PAIR(C_DM)|A_BOLD)
                     : (COLOR_PAIR(C_NICK)|A_BOLD);
            wattron(mw, attr);
            mvwprintw(mw, row, 7, "%-12.12s", m->nick);
            wattroff(mw, attr);

            /* corpo — >>quote em cor diferente, wrap */
            int bx = 20, bw = mcols - bx - 1;
            int len = (int)strlen(m->body), off = 0, first = 1;
            while (off < len && row < mrows-1) {
                /* detecta >>quote no início do segmento de linha */
                int is_q = first && off < len-1
                           && m->body[off]=='>' && m->body[off+1]=='>';
                if (is_q) wattron(mw, COLOR_PAIR(C_SYS)|A_BOLD);
                else      wattron(mw, COLOR_PAIR(C_NORMAL));
                mvwprintw(mw, row, first ? bx : bx+2, "%.*s", bw-(first?0:2), m->body+off);
                if (is_q) wattroff(mw, COLOR_PAIR(C_SYS)|A_BOLD);
                else      wattroff(mw, COLOR_PAIR(C_NORMAL));
                off += bw-(first?0:2); first = 0;
                if (off < len) row++;
            }
        }
        row++;
    }

    if (scroll_off > 0) {
        wattron(mw, COLOR_PAIR(C_WARN)|A_BOLD);
        mvwprintw(mw, 1, mcols-12, "^ +%d", scroll_off);
        wattroff(mw, COLOR_PAIR(C_WARN)|A_BOLD);
    }
    box(mw, 0, 0);
    wnoutrefresh(mw);
}

static void draw_input(WINDOW *iw, int icols, const char *buf,
                       int cur, const char *ctx) {
    werase(iw);
    box(iw, 0, 0);
    wattron(iw, COLOR_PAIR(C_INPUT)|A_BOLD);
    mvwprintw(iw, 0, 2, "[ %s ]", ctx);
    wattroff(iw, COLOR_PAIR(C_INPUT)|A_BOLD);
    wattron(iw, COLOR_PAIR(C_NORMAL));
    mvwprintw(iw, 1, 1, "> %.*s", icols-4, buf);
    /* cursor */
    int cx = 3+cur;
    if (cx < icols-1) {
        wattron(iw, A_REVERSE);
        mvwaddch(iw, 1, cx, buf[cur] ? (unsigned char)buf[cur] : ' ');
        wattroff(iw, A_REVERSE);
    }
    wattroff(iw, COLOR_PAIR(C_NORMAL));
    wnoutrefresh(iw);
}

/* ── tela de chat (usa conexão já estabelecida) ──────────────── */
void relay_run(const char *my_name, const char *my_pubkey,
               const char *channel, const char *host, int port) {
    /* Se ainda não há conexão (ex: chamada via tecla 'r' na home),
     * tenta conectar agora. Se já conectado, apenas abre a tela. */
    if (!relay_connected() && host && port > 0) {
        relay_connect(my_name, my_pubkey, host, port);
    }

    /* se offline e sem demo ainda, popula com dados demo */
    if (!relay_connected() && msg_count == 0) {
        if (user_count == 0)
            add_user(my_name ? my_name : "eu",
                     my_pubkey ? my_pubkey : "local");
        push_sys("*nyaa~!* modo offline — sem servidor. (◕‿◕✿)");
        add_user("misakichan","pk_misaki");
        add_user("yumeiri","pk_yumeiri");
        push_msg(MSG_CHAT,"misakichan","pk_misaki","oi oi~~ (◕‿◕✿)",0);
        push_msg(MSG_CHAT,"yumeiri","pk_yumeiri","salve! (˶ᵔ ᵕ ᵔ˶)",0);
        push_dm("pk_misaki","misakichan","ei, me manda aquele artbook?",0);
    }

    scroll_off=0; view_mode=0; dm_sel=-1; user_sel=0; sidebar_focus=0;


    char input[MAX_MSG_LEN] = {0};
    int  input_cur = 0;

    while (1) {
        poll_relay();

        int rows, cols;
        getmaxyx(stdscr, rows, cols);

        /* sidebar ESQUERDA, mensagens à direita dela */
        int msg_x    = SIDEBAR_W;
        int msg_cols = cols - SIDEBAR_W;
        int msg_rows = rows - INPUT_H - 1; /* -1 para topbar */
        int msg_y    = 1;
        int input_y  = rows - INPUT_H;

        WINDOW *side_w = newwin(rows-1,   SIDEBAR_W, 1,     0);
        WINDOW *msg_w  = newwin(msg_rows, msg_cols,  msg_y, msg_x);
        WINDOW *inp_w  = newwin(INPUT_H,  msg_cols,  input_y, msg_x);

        /* topbar: canal ou DM ativo */
        char bar_l[64];
        if (view_mode == 1 && dm_sel >= 0)
            snprintf(bar_l,sizeof(bar_l),"DM ▸ %s",dms[dm_sel].name);
        else
            snprintf(bar_l,sizeof(bar_l),"#%s",channel?channel:"relay");
        ui_topbar(cols, bar_l,
            "[Tab] sidebar  [/dm nick] DM  [PgUp/Dn] scroll  [ESC] sair");

        /* área de mensagens */
        if (view_mode == 1 && dm_sel >= 0) {
            draw_msgs_area(msg_w, msg_rows, msg_cols,
                           dms[dm_sel].hist, dms[dm_sel].count);
            dms[dm_sel].unread = 0;
            for (int i=0;i<user_count;i++)
                if (!strcmp(users[i].pubkey,dms[dm_sel].pubkey))
                    { users[i].unread=0; break; }
        } else {
            draw_msgs_area(msg_w, msg_rows, msg_cols, msgs, msg_count);
        }

        /* contexto do input */
        char ctx[48];
        if (view_mode==1 && dm_sel>=0)
            snprintf(ctx,sizeof(ctx),"DM ▸ %s",dms[dm_sel].name);
        else
            snprintf(ctx,sizeof(ctx),"#%s",channel?channel:"relay");

        draw_sidebar(side_w, rows-1, channel);
        draw_input(inp_w, msg_cols, input, input_cur, ctx);
        doupdate();

        halfdelay(2);
        int ch = getch();
        nocbreak(); cbreak(); keypad(stdscr,TRUE);

        if (ch == ERR) { delwin(side_w);delwin(msg_w);delwin(inp_w); continue; }
        if (ch == 27)  { delwin(side_w);delwin(msg_w);delwin(inp_w); break; }

        /* scroll */
        if (ch == KEY_PPAGE) {
            int max_msgs = (view_mode==1&&dm_sel>=0) ? dms[dm_sel].count : msg_count;
            scroll_off += msg_rows-4;
            if (scroll_off > max_msgs) scroll_off = max_msgs;
        } else if (ch == KEY_NPAGE) {
            scroll_off -= msg_rows-4;
            if (scroll_off < 0) scroll_off = 0;

        /* Tab alterna foco sidebar */
        } else if (ch == '\t') {
            sidebar_focus = !sidebar_focus;

        /* navegação no sidebar quando em foco */
        } else if (sidebar_focus && (ch==KEY_UP||ch=='k')) {
            if (view_mode==0) { if (user_sel>0) user_sel--; }
            else              { if (dm_sel>0) { dm_sel--; scroll_off=0; } }

        } else if (sidebar_focus && (ch==KEY_DOWN||ch=='j')) {
            if (view_mode==0) { if (user_sel<user_count-1) user_sel++; }
            else              { if (dm_sel<dm_count-1) { dm_sel++; scroll_off=0; } }

        /* Enter no sidebar: abre DM com usuário selecionado */
        } else if (sidebar_focus && (ch=='\n'||ch==KEY_ENTER)) {
            if (view_mode==0 && user_sel < user_count) {
                User *u = &users[user_sel];
                if (strcmp(u->pubkey, my_pubkey?my_pubkey:"local")) {
                    DMConv *d = find_or_create_dm(u->pubkey, u->name);
                    if (d) {
                        dm_sel    = (int)(d - dms);
                        view_mode = 1; scroll_off = 0;
                        char s[128];
                        snprintf(s,sizeof(s),"conversa com %s aberta ♡",u->name);
                        push_dm(u->pubkey,u->name,s,0);
                    }
                }
                sidebar_focus = 0;
            }

        /* ESC no sidebar fecha DM e volta ao canal */
        } else if (sidebar_focus && ch==27) {
            view_mode = 0; dm_sel = -1; scroll_off = 0; sidebar_focus = 0;

        /* input de texto */
        } else if (!sidebar_focus) {
            if (ch == KEY_LEFT) {
                if (input_cur > 0) input_cur--;
            } else if (ch == KEY_RIGHT) {
                if (input[input_cur]) input_cur++;
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (input_cur > 0) {
                    int l = (int)strlen(input);
                    memmove(input+input_cur-1, input+input_cur, l-input_cur+1);
                    input_cur--;
                }
            } else if (ch == '\n' || ch == KEY_ENTER) {
                if (input[0]) {
                    /* ── comando /dm nick [msg] ── */
                    if (input[0]=='/' && (input[1]=='d'||input[1]=='D') &&
                        (input[2]=='m'||input[2]=='M') && input[3]==' ') {
                        char target_name[32]={0};
                        const char *rest = input+4;
                        /* extrai nick até espaço */
                        int ni=0;
                        while (*rest && *rest!=' ' && ni<31) target_name[ni++]=*rest++;
                        target_name[ni]=0;
                        /* busca usuário pelo nome */
                        User *u = NULL;
                        for (int i=0;i<user_count;i++)
                            if (!strcasecmp(users[i].name, target_name))
                                { u=&users[i]; break; }
                        if (u && strcmp(u->pubkey, my_pubkey?my_pubkey:"local")) {
                            DMConv *d = find_or_create_dm(u->pubkey, u->name);
                            if (d) {
                                dm_sel = (int)(d - dms);
                                view_mode = 1; scroll_off = 0;
                                /* se tem mensagem junto, envia direto */
                                if (*rest==' ' && *(rest+1)) {
                                    rest++;
                                    push_dm(u->pubkey, u->name, rest, 1);
                                    if (relay_connected()) {
                                        char json[MAX_MSG_LEN+128];
                                        char safe[MAX_MSG_LEN*2]; int si=0;
                                        for(int xi=0;rest[xi]&&si<(int)sizeof(safe)-2;xi++){
                                            if(rest[xi]=='"') safe[si++]='\\';
                                            safe[si++]=rest[xi];
                                        } safe[si]=0;
                                        snprintf(json,sizeof(json),
                                            "{\"type\":\"dm\",\"to\":\"%s\",\"body\":\"%s\"}",
                                            u->pubkey, safe);
                                        send_relay(json);
                                    }
                                } else {
                                    char s[64];
                                    snprintf(s,sizeof(s),"conversa com %s aberta ♡",u->name);
                                    push_dm(u->pubkey,u->name,s,0);
                                }
                            }
                        } else {
                            char s[64];
                            snprintf(s,sizeof(s),"*pip!* usuário '%s' não encontrado",target_name);
                            push_sys(s);
                        }

                    /* ── /canal ou /sair ── */
                    } else if (input[0]=='/') {
                        if (!strcmp(input,"/sair") || !strcmp(input,"/quit")) {
                            delwin(side_w);delwin(msg_w);delwin(inp_w);
                            break;
                        } else if (!strcmp(input,"/canal") || !strcmp(input,"/ch")) {
                            view_mode = 0; dm_sel = -1; scroll_off = 0;
                        } else {
                            char s[64]; snprintf(s,sizeof(s),"*pip!* comando desconhecido: %s",input);
                            push_sys(s);
                        }

                    /* ── mensagem normal ── */
                    } else if (view_mode==1 && dm_sel>=0) {
                        push_dm(dms[dm_sel].pubkey, dms[dm_sel].name, input, 1);
                        if (relay_connected()) {
                            char json[MAX_MSG_LEN+128];
                            char safe[MAX_MSG_LEN*2]; int si=0;
                            for(int xi=0;input[xi]&&si<(int)sizeof(safe)-2;xi++){
                                if(input[xi]=='"') safe[si++]='\\';
                                safe[si++]=input[xi];
                            } safe[si]=0;
                            snprintf(json,sizeof(json),
                                "{\"type\":\"dm\",\"to\":\"%s\",\"body\":\"%s\"}",
                                dms[dm_sel].pubkey, safe);
                            send_relay(json);
                        }
                    } else {
                        push_msg(MSG_CHAT, my_name, my_pubkey?my_pubkey:"", input, 1);
                        if (relay_connected()) {
                            char json[MAX_MSG_LEN+64];
                            char safe[MAX_MSG_LEN*2]; int si=0;
                            for(int xi=0;input[xi]&&si<(int)sizeof(safe)-2;xi++){
                                if(input[xi]=='"') safe[si++]='\\';
                                safe[si++]=input[xi];
                            } safe[si]=0;
                            snprintf(json,sizeof(json),
                                "{\"type\":\"send\",\"body\":\"%s\"}", safe);
                            send_relay(json);
                        }
                    }
                    memset(input,0,sizeof(input)); input_cur=0;
                }
            } else if (ch >= 32 && ch < 127) {
                int l = (int)strlen(input);
                if (l < MAX_MSG_LEN-1) {
                    memmove(input+input_cur+1,input+input_cur,l-input_cur+1);
                    input[input_cur++] = (char)ch;
                }
            }
        }

        delwin(side_w); delwin(msg_w); delwin(inp_w);
    }
    /* NÃO desconecta aqui — a conexão é persistente durante toda a sessão */
}
