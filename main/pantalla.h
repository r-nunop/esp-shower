#pragma once
#include "tipos.h"

void pantalla_init(void);
void pantalla_actualizar(estado_t e, const lecturas_t *l, float t_obj, codigo_error_t err);
