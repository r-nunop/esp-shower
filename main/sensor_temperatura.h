#pragma once
#include <stdbool.h>

void sensor_temp_init(void);
bool sensor_temp_get(float *temp_c);
