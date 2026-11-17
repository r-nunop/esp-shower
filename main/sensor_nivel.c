#include "sensor_nivel.h"
#include "config.h"
#include "driver/gpio.h"

void sensor_nivel_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_NIVEL,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
}

bool sensor_nivel_ok(void)
{
    return gpio_get_level(PIN_NIVEL) == NIVEL_VALOR_CON_AGUA;
}
