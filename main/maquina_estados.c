#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
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
static TaskHandle_t tarea_telemetria = NULL;
static volatile estado_t estado = EST_REPOSO;
static codigo_error_t error_actual = ERR_NINGUNO;
static int64_t t_entrada_ms = 0;
static float t_obj = TEMP_OBJETIVO_DEFECTO;
static volatile int cadencia_ms = CADENCIA_DEFECTO_MS;
static bool calentador_on = false;
static bool filtro_saturado = false;
// Última medida de turbidez: solo se mide al final de FILTRADO y se publica hasta la siguiente
static float turbidez_v = 0.0f;
static float turbidez_pct = 0.0f;
static bool turbidez_valida = false;

// Copia de los últimos datos para que la tarea de telemetría los publique
// sin que la FSM tenga que tocar la red (la red puede bloquearse).
typedef struct {
    lecturas_t     l;
    estado_t       estado;
    codigo_error_t error;
    float          t_obj;
    bool           filtro_saturado;
} instantanea_t;

static portMUX_TYPE mux_inst = portMUX_INITIALIZER_UNLOCKED;
static instantanea_t inst;
static bool inst_valida = false;

static void avisar_telemetria(void)
{
    if (tarea_telemetria != NULL) xTaskNotifyGive(tarea_telemetria);
}

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
    l.turbidez_v = turbidez_v;
    l.turbidez_pct = turbidez_pct;
    l.turbidez_valida = turbidez_valida;
    l.nivel_ok = sensor_nivel_ok();
    return l;
}

static void procesar_comando(const comando_t *c, const lecturas_t *l)
{
    switch (c->tipo) {
    case CMD_INICIAR:
        // No se empieza una ducha mientras se descarga una actualización.
        if (estado == EST_REPOSO && !ota_en_curso())
            ir_a(l->nivel_ok ? EST_PRECALENTAMIENTO : EST_LLENADO, ERR_NINGUNO);
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
        if (c->valor >= 1.0f && c->valor <= CADENCIA_MAX_S) {
            cadencia_ms = (int)(c->valor * 1000.0f);
            avisar_telemetria();
        }
        break;
    case CMD_OTA:
        ota_lanzar();
        break;
    }
}

// histeresis = false: calienta mientras T < T_obj (se usa al precalentar, porque
// la salida del estado es T >= T_obj y con histéresis el agua podría quedarse
// entre T_obj-1 y T_obj sin calentar nunca).
// histeresis = true: enciende a T_obj-1 y apaga a T_obj (se usa en la ducha).
static void control_calentador(const lecturas_t *l, bool histeresis)
{
    if (!l->temp_valida || !l->nivel_ok) {
        calentador_on = false;
        return;
    }
    if (!histeresis) {
        calentador_on = l->temp_c < t_obj;
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
        control_calentador(l, false);
        if (l->temp_valida && l->temp_c >= t_obj) {
            avisos_lanzar(AVISO_AGUA_LISTA);
            ir_a(EST_DUCHA, ERR_NINGUNO);
        } else if (en_estado_ms() > TIMEOUT_PRECALENT_MS) {
            ir_a(EST_ERROR, ERR_TIMEOUT_PRECALENT);
        }
        break;

    case EST_DUCHA:
        control_calentador(l, true);
        if (!l->nivel_ok || en_estado_ms() > DUCHA_MAX_MS) ir_a(EST_FILTRADO, ERR_NINGUNO);
        break;

    case EST_FILTRADO:
        if (en_estado_ms() > FILTRADO_MS) {
            if (sensor_turbidez_leer(&turbidez_v, &turbidez_pct)) {
                turbidez_valida = true;
                filtro_saturado = turbidez_pct > UMBRAL_TURBIDEZ_PCT;
                if (filtro_saturado) avisos_lanzar(AVISO_FILTRO_SATURADO);
            } else {
                ESP_LOGW(TAG, "Turbidez no disponible: se mantiene el ultimo veredicto");
            }
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

static void guardar_instantanea(const lecturas_t *l)
{
    portENTER_CRITICAL(&mux_inst);
    inst.l = *l;
    inst.estado = estado;
    inst.error = error_actual;
    inst.t_obj = t_obj;
    inst.filtro_saturado = filtro_saturado;
    inst_valida = true;
    portEXIT_CRITICAL(&mux_inst);
}

static void fsm_task(void *arg)
{
    // Si esta tarea se cuelga, el watchdog reinicia el chip y los relés
    // vuelven a OFF (ver CONFIG_ESP_TASK_WDT_PANIC en sdkconfig.defaults).
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_task_wdt_add(NULL));

    for (;;) {
        TickType_t inicio = xTaskGetTickCount();
        estado_t anterior = estado;
        lecturas_t l = leer_sensores();

        comando_t c;
        while (xQueueReceive(cola_cmd, &c, 0) == pdTRUE) procesar_comando(&c, &l);

        codigo_error_t e = seguridad_evaluar(&l, estado);
        if (e != ERR_NINGUNO && estado != EST_ERROR) ir_a(EST_ERROR, e);

        ejecutar_estado(&l);
        aplicar_actuadores();
        sensor_turbidez_encender(estado == EST_FILTRADO);
        if (!actuadores_ok() && estado != EST_ERROR) ir_a(EST_ERROR, ERR_ACTUADORES);

        pantalla_actualizar(estado, &l, t_obj, error_actual);
        avisos_update();

        guardar_instantanea(&l);
        if (estado != anterior) avisar_telemetria();
        esp_task_wdt_reset();

        int periodo_ms = estado == EST_REPOSO ? PERIODO_REPOSO_MS : PERIODO_FSM_MS;
        TickType_t pasado = xTaskGetTickCount() - inicio;
        TickType_t periodo = pdMS_TO_TICKS(periodo_ms);
        if (pasado < periodo) xQueuePeek(cola_cmd, &c, periodo - pasado);
    }
}

static void telemetria_task(void *arg)
{
    for (;;) {
        instantanea_t s;
        bool valida;
        portENTER_CRITICAL(&mux_inst);
        s = inst;
        valida = inst_valida;
        portEXIT_CRITICAL(&mux_inst);

        bool enviada = valida && tb_conectado();
        if (enviada) tb_publicar(&s.l, s.estado, s.error, s.t_obj, s.filtro_saturado);

        bool lenta = s.estado == EST_REPOSO || s.estado == EST_ERROR;
        int espera_ms = !enviada ? 1000 : (lenta ? CADENCIA_REPOSO_MS : cadencia_ms);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(espera_ms));
    }
}

void fsm_init(void)
{
    cola_cmd = xQueueCreate(8, sizeof(comando_t));
    t_entrada_ms = ahora_ms();
    if (xTaskCreate(fsm_task, "fsm_task", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear fsm_task");
    }
    if (xTaskCreate(telemetria_task, "telemetria", 4096, NULL, 3, &tarea_telemetria) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear telemetria_task");
    }
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
