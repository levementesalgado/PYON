#pragma once
#include "config.h"

/* Dados do perfil — persiste em ~/.pyon/profile.json */
typedef struct {
    char  name[64];
    char  bio[2500];
    char  fortune[256];   /* sorte do dia — frase curta */
    long  fortune_day;    /* dia do epoch em que a frase foi definida */
    int   rating_sum;     /* soma das avaliações recebidas */
    int   rating_count;   /* número de avaliações */
    char  pubkey[128];    /* chave pública do dono */
} Profile;

/* Carrega perfil próprio de ~/.pyon/profile.json.
 * Se não existe, inicializa com cfg->my_name. */
void profile_load(Profile *p, const Config *cfg);

/* Salva perfil em ~/.pyon/profile.json */
void profile_save(const Profile *p);

/* Abre a página de perfil próprio (edição).
 * Retorna quando o usuário sair com ESC/q. */
void profile_edit(Config *cfg);

/* Exibe o perfil de outro usuário (somente leitura + avaliação).
 * pubkey = chave do dono. Carrega de ~/.pyon/profiles/<pubkey>.json */
void profile_view(const char *pubkey, const char *name, Config *cfg);
