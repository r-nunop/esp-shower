#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t mcp23017_init(uint8_t olat_a, uint8_t salidas_a);
esp_err_t mcp23017_escribir_a(uint8_t olat_a);
