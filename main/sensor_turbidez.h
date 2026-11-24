#pragma once
#include <stdbool.h>

void sensor_turbidez_init(void);
void sensor_turbidez_encender(bool on);
bool sensor_turbidez_leer(float *voltios, float *pct);
