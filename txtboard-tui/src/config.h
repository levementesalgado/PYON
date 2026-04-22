#pragma once

#define DEFAULT_RELAY_HOST    "127.0.0.1"
#define DEFAULT_RELAY_PORT     7667
#define CONFIG_PATH_SUFFIX     "/.txtboard/config.json"
#define IDENTITY_PATH_SUFFIX   "/.txtboard/identity.json"

typedef struct {
    char relay_host[128];
    int  relay_port;
    char my_name[64];
    char my_pubkey[128];
    char access_code[32];
} Config;

void config_load(Config *cfg);
void config_save_name(const char *name);  /* salva display_name no identity.json */
