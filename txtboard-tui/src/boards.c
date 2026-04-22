#include "boards.h"

const Board BOARDS[NUM_BOARDS] = {
    /* num  slug      title                    desc                              tags                              cat */
    {  1,  "sr",    "sala da república",      "board padrão · sem tema",         {"geral","textão","livre"},       CAT_PADRAO  },
    {  2,  "meta",  "sobre o txtboard",       "regras · sugestões · anúncios",   {"regras","meta","anúncios"},     CAT_PADRAO  },
    {  3,  "a",     "anime",                  "anime geral",                     {"anime","série","review"},       CAT_ANIME   },
    {  4,  "m",     "mangá",                  "mangá · light novel",             {"mangá","ln","scanlation"},      CAT_ANIME   },
    {  5,  "fan",   "fanart · doujinshi",     "fanart · doujinshi · pixiv",      {"fanart","pixiv","doujin"},      CAT_ANIME   },
    {  6,  "vn",    "visual novels",          "vns · eroge · kinetic novels",    {"vn","eroge","kinetic"},         CAT_ANIME   },
    {  7,  "fig",   "figuras",                "figures · nendos · merch",        {"figures","nendos","merch"},     CAT_ANIME   },
    {  8,  "ost",   "trilhas · j-music",      "ost · jpop · jrock · vocaloid",   {"ost","jpop","vocaloid"},        CAT_ANIME   },
    {  9,  "rep",   "república",              "moradia · repúblicas · kitnet",   {"moradia","kitnet","rep"},       CAT_UNI     },
    { 10,  "aula",  "estudos",                "matérias · provas · resumos",     {"estudos","provas","resumos"},   CAT_UNI     },
    { 11,  "ic",    "inic. científica",       "pesquisa · bolsas · lattes",      {"ic","pesquisa","bolsas"},       CAT_UNI     },
    { 12,  "rango", "comida",                 "bandejão · receitas · delivery",  {"comida","bandejão","receita"},  CAT_UNI     },
    { 13,  "estag", "estágio",                "estágio · emprego · carreira",    {"estágio","emprego","cv"},       CAT_UNI     },
    { 14,  "café",  "desabafo",               "textão · vent · comfy acadêmico", {"desabafo","vent","comfy"},      CAT_UNI     },
    { 15,  "hist",  "história",               "história geral e comparada",      {"história","política","wars"},   CAT_CULTURA },
    { 16,  "arte",  "artes visuais",          "pintura · escultura · ilustração",{"arte","pintura","ilustração"},  CAT_CULTURA },
    { 17,  "lit",   "literatura",             "livros · poesia · contos",        {"livros","poesia","contos"},     CAT_CULTURA },
    { 18,  "fil",   "filosofia",              "filosofia · ética · epistemologia",{"filosofia","ética","lógica"},  CAT_CULTURA },
    { 19,  "music", "música",                 "música geral · teoria · bandas",  {"música","teoria","bandas"},     CAT_CULTURA },
    { 20,  "cine",  "cinema · séries",        "filmes · séries · análises",      {"cinema","série","análise"},     CAT_CULTURA },
    { 21,  "arq",   "arquitetura",            "arquitetura · design · urbanism", {"arquitetura","design","urb"},   CAT_CULTURA },
    { 22,  "foto",  "fotografia",             "foto · analógica · digital",      {"foto","analógica","digital"},   CAT_CULTURA },
    { 23,  "tech",  "tecnologia",             "código · hardware · software",    {"código","hardware","linux"},    CAT_DIRETO  },
    { 24,  "sci",   "ciência",                "ciências exatas e naturais",      {"ciência","exatas","naturais"},  CAT_DIRETO  },
    { 25,  "jp",    "japão",                  "cultura jp · idioma · viagem",    {"japão","idioma","viagem"},      CAT_DIRETO  },
    { 26,  "br",    "brasil",                 "cotidiano · política · cultura",  {"brasil","política","cotidiano"},CAT_DIRETO  },
    { 27,  "jogo",  "jogos",                  "games · tabletop · rpg",          {"games","tabletop","rpg"},       CAT_DIRETO  },
    { 28,  "livr",  "livros · quadrinhos",    "leitura · hq · graphic novel",    {"livros","hq","quadrinhos"},     CAT_DIRETO  },
    { 29,  "id",    "identidade",             "identidade · flags · comunidade", {"identidade","lgbtq+","comunidade"},CAT_DIRETO},
    { 30,  "diy",   "faça você mesmo",        "craft · eletrônica · marcenaria", {"craft","eletrônica","diy"},     CAT_DIRETO  },
    { 31,  "comfy", "comfy",                  "slice of life · relaxar · vibe",  {"comfy","vibe","relaxar"},       CAT_DIRETO  },
};

const char *cat_label(BoardCat c) {
    switch (c) {
        case CAT_PADRAO:  return "padrão";
        case CAT_ANIME:   return "anime";
        case CAT_UNI:     return "uni";
        case CAT_CULTURA: return "cultura";
        case CAT_DIRETO:  return "direto";
        default:          return "?";
    }
}
