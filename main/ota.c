#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "maquina_estados.h"
#include "thingsboard.h"

static const char *TAG = "OTA";
static volatile bool en_curso = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

#define PLAZO_VALIDAR_MS  (5 * 60 * 1000)
#define TZ_MADRID         "CET-1CEST,M3.5.0,M10.5.0/3"

static bool leer_version(const char *v, int n[3])
{
    char resto;
    return sscanf(v, "%d.%d.%d%c", &n[0], &n[1], &n[2], &resto) == 3;
}

static bool es_mas_nueva(const char *nueva, const char *actual)
{
    int a[3], b[3];
    if (!leer_version(nueva, a) || !leer_version(actual, b)) {
        ESP_LOGW(TAG, "Version con formato no valido: %s / %s", nueva, actual);
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

static bool ya_fallo(const char *version)
{
    const esp_partition_t *p = esp_ota_get_last_invalid_partition();
    esp_app_desc_t desc;
    return p != NULL && esp_ota_get_partition_description(p, &desc) == ESP_OK &&
           strcmp(desc.version, version) == 0;
}

static void enviar_estado(const char *estado, const char *error)
{
    char json[128];
    if (error) snprintf(json, sizeof(json), "{\"fw_state\":\"%s\",\"fw_error\":\"%s\"}", estado, error);
    else snprintf(json, sizeof(json), "{\"fw_state\":\"%s\"}", estado);
    tb_publicar_atributos(json);
}

static void guardar_instalada(const char *version)
{
    nvs_handle_t h;
    if (nvs_open("ota", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "instalada", version);
    nvs_commit(h);
    nvs_close(h);
}

static bool leer_instalada(char *version, size_t len)
{
    nvs_handle_t h;
    if (nvs_open("ota", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_str(h, "instalada", version, &len) == ESP_OK;
    nvs_close(h);
    return ok;
}

static void borrar_instalada(void)
{
    nvs_handle_t h;
    if (nvs_open("ota", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, "instalada");
    nvs_commit(h);
    nvs_close(h);
}

static void ota_task(void *arg)
{
    if (strlen(CONFIG_OTA_URL) == 0) {
        ESP_LOGW(TAG, "CONFIG_OTA_URL vacia: OTA desactivada");
        goto fin;
    }

    esp_http_client_config_t http_cfg = {
        .url = CONFIG_OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size_tx = 2048,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    esp_https_ota_handle_t handle = NULL;

    if (esp_https_ota_begin(&ota_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar la OTA");
        enviar_estado("FAILED", "No se pudo conectar con la URL del firmware");
        goto fin;
    }

    esp_app_desc_t nueva;
    const esp_app_desc_t *actual = esp_app_get_description();
    if (esp_https_ota_get_img_desc(handle, &nueva) != ESP_OK) {
        esp_https_ota_abort(handle);
        enviar_estado("FAILED", "No se pudo leer la cabecera del firmware");
        goto fin;
    }

    if (!es_mas_nueva(nueva.version, actual->version) || ya_fallo(nueva.version)) {
        ESP_LOGI(TAG, "No se instala %s (actual %s)", nueva.version, actual->version);
        esp_https_ota_abort(handle);
        goto fin;
    }

    enviar_estado("DOWNLOADING", NULL);
    esp_err_t r;
    while ((r = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        // Cede la CPU para no dejar sin tiempo a la tarea idle (watchdog)
        // ni a las demás tareas.
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (r != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        enviar_estado("FAILED", "Descarga incompleta");
        goto fin;
    }
    enviar_estado("DOWNLOADED", NULL);
    if (esp_https_ota_finish(handle) != ESP_OK) {
        enviar_estado("FAILED", "Imagen no valida");
        goto fin;
    }
    enviar_estado("VERIFIED", NULL);

    while (fsm_estado() != EST_REPOSO && fsm_estado() != EST_ERROR) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Version %s instalada, reiniciando", nueva.version);
    guardar_instalada(nueva.version);
    enviar_estado("UPDATING", NULL);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

fin:
    en_curso = false;
    vTaskDelete(NULL);
}

bool ota_en_curso(void)
{
    return en_curso;
}

bool ota_lanzar(void)
{
    if (fsm_estado() != EST_REPOSO) return false;

    portENTER_CRITICAL(&mux);
    bool libre = !en_curso;
    if (libre) en_curso = true;
    portEXIT_CRITICAL(&mux);
    if (!libre) return false;

    // Prioridad 3: menor que la FSM (5) para que la descarga nunca la retrase.
    if (xTaskCreate(ota_task, "ota_task", 8192, NULL, 3, NULL) != pdPASS) {
        en_curso = false;
        return false;
    }
    return true;
}

static int64_t ahora_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool wifi_con_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    return netif != NULL && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

static bool hora_local(struct tm *local)
{
    time_t ahora = time(NULL);
    localtime_r(&ahora, local);
    return local->tm_year + 1900 >= 2025;
}

static void planificador_task(void *arg)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_ota_img_states_t st;
    bool por_validar = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
                       st == ESP_OTA_IMG_PENDING_VERIFY;
    char instalada[32];
    bool avisar = leer_instalada(instalada, sizeof(instalada));
    int64_t wifi_desde = -1;
    int64_t sin_wifi_desde = -1;
    bool comprobada_al_arrancar = false;
    int ultimo_dia = -1;

    for (;;) {
        int64_t t = ahora_ms();
        bool hay_wifi = wifi_con_ip();
        if (hay_wifi) {
            sin_wifi_desde = -1;
            if (wifi_desde < 0) wifi_desde = t;
        } else {
            wifi_desde = -1;
            if (sin_wifi_desde < 0) sin_wifi_desde = t;
        }

        if (por_validar) {
            bool validar = tb_conectado() || (!hay_wifi && t - sin_wifi_desde >= PLAZO_VALIDAR_MS);
            bool volver = hay_wifi && t - wifi_desde >= PLAZO_VALIDAR_MS && !tb_conectado();
            if (validar) {
                ESP_LOGI(TAG, "Version %s validada", app->version);
                esp_ota_mark_app_valid_cancel_rollback();
                por_validar = false;
            } else if (volver && (fsm_estado() == EST_REPOSO || fsm_estado() == EST_ERROR)) {
                ESP_LOGE(TAG, "Sin ThingsBoard tras 5 min con Wi-Fi: vuelta atras");
                esp_ota_mark_app_invalid_rollback_and_reboot();
                por_validar = false;
            }
        }

        if (avisar && !por_validar && tb_conectado()) {
            if (strcmp(instalada, app->version) == 0) {
                enviar_estado("UPDATED", NULL);
            } else {
                char error[80];
                snprintf(error, sizeof(error), "Vuelta atras: la version %s no arranco bien", instalada);
                enviar_estado("FAILED", error);
            }
            borrar_instalada();
            avisar = false;
        }

        bool se_puede = tb_conectado() && fsm_estado() == EST_REPOSO;
        if (!comprobada_al_arrancar && se_puede && ota_lanzar()) {
            ESP_LOGI(TAG, "Comprobacion al arrancar");
            comprobada_al_arrancar = true;
        }

        struct tm local;
        if (hora_local(&local) && local.tm_hour == CONFIG_OTA_HORA_DIARIA && local.tm_yday != ultimo_dia &&
            se_puede && ota_lanzar()) {
            ESP_LOGI(TAG, "Comprobacion diaria");
            ultimo_dia = local.tm_yday;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void ota_init(void)
{
    setenv("TZ", TZ_MADRID, 1);
    tzset();
    if (xTaskCreate(planificador_task, "ota_planif", 4096, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear ota_planif");
    }
}
