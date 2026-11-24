#pragma once
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// Eduponics Mini
#define PIN_RELE_BOMBA        GPIO_NUM_23
#define BOMBA_NIVEL_ON        1
#define PIN_NIVEL             GPIO_NUM_21
#define PIN_LED               GPIO_NUM_14

// Placa de extensión. IO12 arranca en bajo, así la placa está apagada durante el reset
#define PIN_EXT_ALIM          GPIO_NUM_12
#define PIN_I2C_SDA           GPIO_NUM_32
#define PIN_I2C_SCL           GPIO_NUM_33
#define MCP_RELE_VALVULA      0     // relé 1 (GPA0)
#define MCP_RELE_CALENTADOR   1     // relé 2 (GPA1), activa el IN del módulo de 30 A
// Sin comprobar: probarlo con la placa antes de conectar el calentador
#define RELE_EXT_NIVEL_ON     1

#define PIN_ONEWIRE           GPIO_NUM_4
#define CANAL_TURBIDEZ        ADC_CHANNEL_6
#define NIVEL_VALOR_CON_AGUA  1
// Para que el oleaje del agua no dispare un error de nivel
#define NIVEL_ANTIRREBOTE_MS  500

#define TEMP_OBJETIVO_DEFECTO 38.0f
#define TEMP_MIN_C            20.0f
#define TEMP_MAX_C            45.0f
#define TEMP_SEGURIDAD_C      55.0f
#define HISTERESIS_C          1.0f

// Hay que calibrarlo con el sensor real: con agua limpia da más tensión
#define TURBIDEZ_RAW_LIMPIA   3200
#define TURBIDEZ_RAW_TURBIA   2000
#define TURBIDEZ_MUESTRAS     16
#define UMBRAL_TURBIDEZ_PCT   60.0f

#define PERIODO_FSM_MS        100
#define CADENCIA_DEFECTO_MS   1000
#define CADENCIA_MAX_S        3600.0f
#define TIMEOUT_LLENADO_MS    (120 * 1000)
#define TIMEOUT_PRECALENT_MS  (600 * 1000)
#define DUCHA_MAX_MS          (600 * 1000)
#define FILTRADO_MS           (30 * 1000)

#define RPC_INICIAR           "iniciar"
#define RPC_DETENER           "detener"
#define RPC_SET_TEMP          "setTemperatura"
#define RPC_RESET             "reset"
#define RPC_SET_CADENCIA      "setCadencia"
#define RPC_OTA               "startOTA"

#define AP_SSID               "ESP32_SBC26M14"
#define AP_CANAL              6
#define WIFI_MAX_REINTENTOS   10
