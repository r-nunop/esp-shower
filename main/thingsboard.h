#pragma once
#include "tipos.h"

void tb_init(cb_comando_t on_comando);
void tb_iniciar(void);
bool tb_conectado(void);
void tb_publicar_atributos(const char *json);
void tb_publicar(const lecturas_t *l, estado_t e, codigo_error_t err, float t_obj, bool filtro_saturado);
