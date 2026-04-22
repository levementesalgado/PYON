#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* parser JSON mínimo — extrai "key":"value" */
static int jstr(const char *json, const char *key, char *out, size_t outlen) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < outlen - 1) out[i++] = *p++;
    out[i] = 0;
    return 1;
}

static int jint(const char *json, const char *key, int fallback) {
    char buf[16] = {0};
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(json, needle);
    if (!p) return fallback;
    p += strlen(needle);
    while (*p == ' ') p++;
    size_t i = 0;
    while (*p >= '0' && *p <= '9' && i < sizeof(buf)-1) buf[i++] = *p++;
    return i > 0 ? atoi(buf) : fallback;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

void config_load(Config *cfg) {
    snprintf(cfg->relay_host, sizeof(cfg->relay_host), "%s", DEFAULT_RELAY_HOST);
    cfg->relay_port = DEFAULT_RELAY_PORT;
    snprintf(cfg->my_name,    sizeof(cfg->my_name),    "anon");
    cfg->my_pubkey[0]   = 0;
    cfg->access_code[0] = 0;

    const char *home = getenv("HOME");
    if (!home) return;

    /* lê identity.json gerado pelo core Rust */
    char id_path[256];
    snprintf(id_path, sizeof(id_path), "%s%s", home, IDENTITY_PATH_SUFFIX);
    char *id_json = read_file(id_path);
    if (id_json) {
        char disp[64] = {0};
        if (jstr(id_json, "display_name", disp, sizeof(disp)) && disp[0])
            snprintf(cfg->my_name, sizeof(cfg->my_name), "%s", disp);
        jstr(id_json, "pubkey_hex",   cfg->my_pubkey,   sizeof(cfg->my_pubkey));
        jstr(id_json, "access_code",  cfg->access_code, sizeof(cfg->access_code));
        /* fallback: primeiros 8 chars do pubkey como nome */
        if (!disp[0] && cfg->my_pubkey[0]) {
            snprintf(cfg->my_name, sizeof(cfg->my_name),
                     "anon:%.8s", cfg->my_pubkey);
        }
        free(id_json);
    }

    /* lê config.json (opcional) */
    char cfg_path[256];
    snprintf(cfg_path, sizeof(cfg_path), "%s%s", home, CONFIG_PATH_SUFFIX);
    char *cfg_json = read_file(cfg_path);
    if (cfg_json) {
        char host[128] = {0};
        if (jstr(cfg_json, "relay_host", host, sizeof(host)) && host[0])
            snprintf(cfg->relay_host, sizeof(cfg->relay_host), "%s", host);
        int port = jint(cfg_json, "relay_port", 0);
        if (port > 0) cfg->relay_port = port;
        free(cfg_json);
    }
}

void config_save_name(const char *name) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[256];
    snprintf(path, sizeof(path), "%s%s", home, IDENTITY_PATH_SUFFIX);

    char *json = read_file(path);
    if (!json) return;

    /* substitui ou adiciona display_name via txtboard-core set-name */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "txtboard-core set-name '%s' 2>/dev/null", name);
    system(cmd);
    free(json);
}
