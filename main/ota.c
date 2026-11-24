#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "maquina_estados.h"

static const char *TAG = "OTA";
static volatile bool en_curso = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Formato X.Y.Z. Cualquier otra cosa (por ejemplo "1.0.0-dirty") no se acepta
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

// Si esta versión ya volvió atrás una vez, no se reinstala: si no, entraría en bucle
static bool ya_fallo(const char *version)
{
    const esp_partition_t *p = esp_ota_get_last_invalid_partition();
    esp_app_desc_t desc;
    return p != NULL && esp_ota_get_partition_description(p, &desc) == ESP_OK &&
           strcmp(desc.version, version) == 0;
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
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };
    esp_https_ota_handle_t handle = NULL;

    if (esp_https_ota_begin(&ota_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar la OTA");
        goto fin;
    }

    esp_app_desc_t nueva;
    const esp_app_desc_t *actual = esp_app_get_description();
    if (esp_https_ota_get_img_desc(handle, &nueva) != ESP_OK) {
        esp_https_ota_abort(handle);
        goto fin;
    }

    if (!es_mas_nueva(nueva.version, actual->version) || ya_fallo(nueva.version)) {
        ESP_LOGI(TAG, "No se instala %s (actual %s)", nueva.version, actual->version);
        esp_https_ota_abort(handle);
        goto fin;
    }

    esp_err_t r;
    while ((r = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        // Cede la CPU para no dejar sin tiempo a la tarea idle (watchdog)
        // ni a las demás tareas.
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (r != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        esp_https_ota_abort(handle);
        goto fin;
    }
    if (esp_https_ota_finish(handle) != ESP_OK) goto fin;

    // en_curso sigue a true hasta el reinicio, así no puede empezar una ducha
    while (fsm_estado() != EST_REPOSO && fsm_estado() != EST_ERROR) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Version %s instalada, reiniciando", nueva.version);
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

    // Pueden pedirla dos tareas a la vez: comprobar y marcar de una vez
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
