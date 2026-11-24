#pragma once
#include <stdbool.h>

void actuadores_init(void);
void actuadores_bomba(bool on);
void actuadores_valvula_ducha(bool ducha);
void actuadores_calentador(bool on);
void actuadores_todo_off(void);
bool actuadores_ok(void);
