#pragma once
#include <stdbool.h>

bool ota_lanzar(void);
void ota_asignar(const char *titulo, const char *version, const char *url);
bool ota_en_curso(void);
void ota_init(void);
