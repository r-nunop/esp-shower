#pragma once
#include "esp_err.h"

esp_err_t ads1115_init(void);
esp_err_t ads1115_leer_v(int canal, float *voltios);
