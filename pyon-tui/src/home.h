#pragma once
#include "boards.h"
#include "config.h"

/* Retorna índice em BOARDS[] da board escolhida,
   -1 = sair, -2 = relay global */
int home_run(Config *cfg);
