#include "avisos.h"
#include "config.h"
#include "driver/gpio.h"

void avisos_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_LED, 0);
}

void avisos_lanzar(aviso_t a)
{
}

void avisos_update(void)
{
}
