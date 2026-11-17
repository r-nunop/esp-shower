#pragma once
#include <stdbool.h>

void sensor_turbidez_init(void);
bool sensor_turbidez_leer(int *raw, float *pct);
