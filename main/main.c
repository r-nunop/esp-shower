#include "nvs_flash.h"
#include "esp_err.h"
#include "actuadores.h"
#include "sensor_temperatura.h"
#include "sensor_turbidez.h"
#include "sensor_nivel.h"
#include "pantalla.h"
#include "avisos.h"
#include "maquina_estados.h"
#include "thingsboard.h"
#include "wifi_manager.h"

static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void app_main(void)
{
    actuadores_init();
    nvs_init();

    sensor_temp_init();
    sensor_turbidez_init();
    sensor_nivel_init();
    pantalla_init();
    avisos_init();

    tb_init(fsm_enviar_comando);
    fsm_init();
    wifi_manager_init(tb_iniciar);
}
