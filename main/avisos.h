#pragma once

typedef enum {
    AVISO_NINGUNO = 0,
    AVISO_AGUA_LISTA,
    AVISO_FILTRO_SATURADO,
    AVISO_ERROR
} aviso_tipo_t;

void avisos_init(void);
void avisos_lanzar(aviso_tipo_t tipo);
void avisos_update(void);