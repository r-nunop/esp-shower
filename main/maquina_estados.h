#pragma once
#include "tipos.h"

void     fsm_init(void);
bool     fsm_enviar_comando(const comando_t *c);
estado_t fsm_estado(void);
