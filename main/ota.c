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

static void ota_task(void *arg)
{
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

    esp_app_desc_t nueva, actual;
    if (esp_https_ota_get_img_desc(handle, &nueva) != ESP_OK ||
        esp_ota_get_partition_description(esp_ota_get_running_partition(), &actual) != ESP_OK) {
        esp_https_ota_abort(handle);
        goto fin;
    }

    if (strcmp(nueva.version, actual.version) == 0) {
        ESP_LOGI(TAG, "Ya se ejecuta la version %s", actual.version);
        esp_https_ota_abort(handle);
        goto fin;
    }

    esp_err_t r;
    while ((r = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    }

    if (r == ESP_OK && esp_https_ota_is_complete_data_received(handle)) {
        if (esp_https_ota_finish(handle) == ESP_OK && fsm_estado() == EST_REPOSO) {
            esp_restart();
        }
    } else {
        esp_https_ota_abort(handle);
    }

fin:
    en_curso = false;
    vTaskDelete(NULL);
}

bool ota_lanzar(void)
{
    if (en_curso) return false;
    if (fsm_estado() != EST_REPOSO) return false;
    en_curso = true;
    if (xTaskCreate(ota_task, "ota_task", 8192, NULL, 5, NULL) != pdPASS) {
        en_curso = false;
        return false;
    }
    return true;
}
