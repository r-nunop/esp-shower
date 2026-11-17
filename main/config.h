#pragma once
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#define PIN_RELE_BOMBA        GPIO_NUM_26
#define PIN_RELE_VALVULA      GPIO_NUM_27
// Antes GPIO14: ese pin emite una señal PWM durante el arranque y podría
// activar el calentador. GPIO17 no tiene función especial al arrancar.
// Provisional: contrastar con el esquema técnico.
#define PIN_RELE_CALENTADOR   GPIO_NUM_17
#define PIN_RELE_LIBRE        GPIO_NUM_25
#define RELE_NIVEL_ACTIVO     0

#define PIN_ONEWIRE           GPIO_NUM_4
#define CANAL_TURBIDEZ        ADC_CHANNEL_6
#define PIN_NIVEL             GPIO_NUM_35
#define NIVEL_VALOR_CON_AGUA  1
#define NIVEL_ANTIRREBOTE_MS  500      // el nivel debe mantenerse estable este tiempo

#define PIN_I2C_SDA           GPIO_NUM_21
#define PIN_I2C_SCL           GPIO_NUM_22
#define PIN_BUZZER            GPIO_NUM_13
#define PIN_LED               GPIO_NUM_33

#define TEMP_OBJETIVO_DEFECTO 38.0f
#define TEMP_MIN_C            20.0f
#define TEMP_MAX_C            45.0f
#define TEMP_SEGURIDAD_C      55.0f
#define HISTERESIS_C          1.0f

// Turbidez en %: 0 = agua limpia, 100 = muy turbia.
// El SEN0189 da MÁS tensión con agua limpia y MENOS con agua turbia.
// CALIBRAR: medir el valor crudo con agua limpia y con agua muy turbia.
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
#define WIFI_MAX_REINTENTOS   10       // intentos fallidos antes de abrir el portal AP
