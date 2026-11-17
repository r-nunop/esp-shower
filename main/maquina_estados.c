#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "maquina_estados.h"
#include "config.h"
#include "sensor_temperatura.h"
#include "sensor_turbidez.h"
#include "sensor_nivel.h"
#include "actuadores.h"
#include "pantalla.h"
#include "avisos.h"
#include "seguridad.h"
#include "thingsboard.h"
#include "ota.h"

static const char *TAG = "FSM";

static QueueHandle_t cola_cmd = NULL;
static volatile estado_t estado = EST_REPOSO;
static codigo_error_t error_actual = ERR_NINGUNO;
static int64_t t_entrada_ms = 0;
static int64_t t_ultima_telemetria_ms = 0;
static float t_obj = TEMP_OBJETIVO_DEFECTO;
static int cadencia_ms = CADENCIA_DEFECTO_MS;
static bool calentador_on = false;
static bool filtro_saturado = false;

static int64_t ahora_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int64_t en_estado_ms(void)
{
    return ahora_ms() - t_entrada_ms;
}

static void ir_a(estado_t nuevo, codigo_error_t err)
{
    ESP_LOGI(TAG, "%d -> %d (err %d)", estado, nuevo, err);
    estado = nuevo;
    error_actual = err;
    t_entrada_ms = ahora_ms();
    calentador_on = false;
    if (nuevo == EST_ERROR) avisos_lanzar(AVISO_ERROR);
}

static lecturas_t leer_sensores(void)
{
    lecturas_t l = {0};
    l.temp_valida = sensor_temp_get(&l.temp_c);
    sensor_turbidez_leer(&l.turbidez_raw, &l.turbidez_pct);
    l.nivel_ok = sensor_nivel_ok();
    return l;
}

static void procesar_comando(const comando_t *c, const lecturas_t *l)
{
    switch (c->tipo) {
    case CMD_INICIAR:
        if (estado == EST_REPOSO) ir_a(l->nivel_ok ? EST_PRECALENTAMIENTO : EST_LLENADO, ERR_NINGUNO);
        break;
    case CMD_DETENER:
        if (estado == EST_DUCHA) ir_a(EST_FILTRADO, ERR_NINGUNO);
        else if (estado == EST_LLENADO || estado == EST_PRECALENTAMIENTO) ir_a(EST_REPOSO, ERR_NINGUNO);
        break;
    case CMD_SET_TEMP:
        if (c->valor >= TEMP_MIN_C && c->valor <= TEMP_MAX_C) t_obj = c->valor;
        break;
    case CMD_RESET:
        if (estado == EST_ERROR) ir_a(EST_REPOSO, ERR_NINGUNO);
        break;
    case CMD_SET_CADENCIA:
        if (c->valor >= 1.0f) cadencia_ms = (int)(c->valor * 1000.0f);
        break;
    case CMD_OTA:
        ota_lanzar();
        break;
    }
}

static void control_calentador(const lecturas_t *l)
{
    if (!l->temp_valida || !l->nivel_ok) {
        calentador_on = false;
        return;
    }
    if (l->temp_c <= t_obj - HISTERESIS_C) calentador_on = true;
    else if (l->temp_c >= t_obj) calentador_on = false;
}

static void ejecutar_estado(const lecturas_t *l)
{
    switch (estado) {
    case EST_REPOSO:
        break;

    case EST_LLENADO:
        if (l->nivel_ok) ir_a(EST_PRECALENTAMIENTO, ERR_NINGUNO);
        else if (en_estado_ms() > TIMEOUT_LLENADO_MS) ir_a(EST_ERROR, ERR_TIMEOUT_LLENADO);
        break;

    case EST_PRECALENTAMIENTO:
        control_calentador(l);
        if (l->temp_valida && l->temp_c >= t_obj) {
            avisos_lanzar(AVISO_AGUA_LISTA);
            ir_a(EST_DUCHA, ERR_NINGUNO);
        } else if (en_estado_ms() > TIMEOUT_PRECALENT_MS) {
            ir_a(EST_ERROR, ERR_TIMEOUT_PRECALENT);
        }
        break;

    case EST_DUCHA:
        control_calentador(l);
        if (!l->nivel_ok || en_estado_ms() > DUCHA_MAX_MS) ir_a(EST_FILTRADO, ERR_NINGUNO);
        break;

    case EST_FILTRADO:
        if (en_estado_ms() > FILTRADO_MS) {
            filtro_saturado = l->turbidez_pct > UMBRAL_TURBIDEZ_PCT;
            if (filtro_saturado) avisos_lanzar(AVISO_FILTRO_SATURADO);
            ir_a(EST_REPOSO, ERR_NINGUNO);
        }
        break;

    case EST_ERROR:
        break;
    }
}

static void aplicar_actuadores(void)
{
    switch (estado) {
    case EST_LLENADO:
        actuadores_bomba(true);
        actuadores_valvula_ducha(false);
        actuadores_calentador(false);
        break;
    case EST_PRECALENTAMIENTO:
        actuadores_bomba(true);
        actuadores_valvula_ducha(false);
        actuadores_calentador(calentador_on);
        break;
    case EST_DUCHA:
        actuadores_bomba(true);
        actuadores_valvula_ducha(true);
        actuadores_calentador(calentador_on);
        break;
    default:
        actuadores_todo_off();
        break;
    }
}

static void fsm_task(void *arg)
{
    TickType_t t = xTaskGetTickCount();
    for (;;) {
        lecturas_t l = leer_sensores();

        comando_t c;
        while (xQueueReceive(cola_cmd, &c, 0) == pdTRUE) procesar_comando(&c, &l);

        codigo_error_t e = seguridad_evaluar(&l, estado);
        if (e != ERR_NINGUNO && estado != EST_ERROR) ir_a(EST_ERROR, e);

        ejecutar_estado(&l);
        aplicar_actuadores();

        pantalla_actualizar(estado, &l, t_obj, error_actual);
        avisos_update();

        if (ahora_ms() - t_ultima_telemetria_ms >= cadencia_ms) {
            t_ultima_telemetria_ms = ahora_ms();
            tb_publicar(&l, estado, error_actual, t_obj, filtro_saturado);
        }

        vTaskDelayUntil(&t, pdMS_TO_TICKS(PERIODO_FSM_MS));
    }
}

void fsm_init(void)
{
    cola_cmd = xQueueCreate(8, sizeof(comando_t));
    t_entrada_ms = ahora_ms();
    xTaskCreate(fsm_task, "fsm_task", 4096, NULL, 5, NULL);
}

bool fsm_enviar_comando(const comando_t *c)
{
    if (cola_cmd == NULL) return false;
    return xQueueSend(cola_cmd, c, 0) == pdTRUE;
}

estado_t fsm_estado(void)
{
    return estado;
}
