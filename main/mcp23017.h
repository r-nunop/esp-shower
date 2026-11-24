#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    MCP_PUERTO_A = 0,
    MCP_PUERTO_B = 1,
} mcp_puerto_t;

esp_err_t mcp23017_init(void);
esp_err_t mcp23017_configurar(mcp_puerto_t puerto, uint8_t olat, uint8_t salidas);
esp_err_t mcp23017_escribir(mcp_puerto_t puerto, uint8_t olat);
bool mcp23017_ok(void);
