#include "bus_i2c.h"
#include "config.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "I2C";

static i2c_master_bus_handle_t bus = NULL;

esp_err_t bus_i2c_init(void)
{
    if (bus != NULL) return ESP_OK;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_EXT_ALIM,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_EXT_ALIM, 1);
    // La placa de STEMinds necesita este tiempo para arrancar
    vTaskDelay(pdMS_TO_TICKS(100));

    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&cfg, &bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear el bus (%s)", esp_err_to_name(ret));
        bus = NULL;
        bus_i2c_cortar_extension();
    }
    return ret;
}

i2c_master_bus_handle_t bus_i2c_handle(void)
{
    return bus;
}

// Sin alimentación los relés de la extensión se sueltan, así que es el apagado seguro
void bus_i2c_cortar_extension(void)
{
    gpio_set_level(PIN_EXT_ALIM, 0);
}
