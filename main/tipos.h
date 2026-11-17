#pragma once
#include <stdbool.h>

typedef enum {
    EST_REPOSO,
    EST_LLENADO,
    EST_PRECALENTAMIENTO,
    EST_DUCHA,
    EST_FILTRADO,
    EST_ERROR
} estado_t;

typedef enum {
    ERR_NINGUNO,
    ERR_TIMEOUT_LLENADO,
    ERR_TIMEOUT_PRECALENT,
    ERR_NIVEL_BAJO,
    ERR_SOBRETEMP,
    ERR_SENSOR_TEMP
} codigo_error_t;

typedef struct {
    float temp_c;
    bool  temp_valida;
    int   turbidez_raw;
    float turbidez_pct;
    bool  nivel_ok;
} lecturas_t;

typedef enum {
    CMD_INICIAR,
    CMD_DETENER,
    CMD_SET_TEMP,
    CMD_RESET,
    CMD_SET_CADENCIA,
    CMD_OTA
} cmd_tipo_t;

typedef struct {
    cmd_tipo_t tipo;
    float valor;
} comando_t;

typedef bool (*cb_comando_t)(const comando_t *cmd);

typedef enum {
    AVISO_NINGUNO,
    AVISO_AGUA_LISTA,
    AVISO_FILTRO_SATURADO,
    AVISO_ERROR
} aviso_t;
