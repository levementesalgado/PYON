#pragma once

#define NUM_BOARDS 31

typedef enum {
    CAT_PADRAO = 0,
    CAT_ANIME,
    CAT_UNI,
    CAT_CULTURA,
    CAT_DIRETO
} BoardCat;

typedef struct {
    int      num;
    char     slug[16];
    char     title[40];
    char     desc[64];
    char     tags[3][24];  /* até 3 tags por board */
    BoardCat cat;
} Board;

extern const Board BOARDS[NUM_BOARDS];
const char *cat_label(BoardCat c);
