#include "sensor_nivel.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Antirrebote: el estado solo cambia si la lectura nueva se mantiene
// NIVEL_ANTIRREBOTE_MS. Pensado para que el oleaje no dispare un error.
// Solo debe llamarse desde una tarea (la FSM): no es reentrante.
static bool estable = false;
static bool candidato = false;
static int64_t t_candidato_us = 0;

static bool leer_crudo(void)
{
    return gpio_get_level(PIN_NIVEL) == NIVEL_VALOR_CON_AGUA;
}

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

    estable = leer_crudo();
    candidato = estable;
    t_candidato_us = esp_timer_get_time();
}

bool sensor_nivel_ok(void)
{
    bool crudo = leer_crudo();
    int64_t ahora = esp_timer_get_time();

    if (crudo == estable) {
        candidato = estable;
    } else if (crudo != candidato) {
        candidato = crudo;
        t_candidato_us = ahora;
    } else if ((ahora - t_candidato_us) >= (int64_t)NIVEL_ANTIRREBOTE_MS * 1000) {
        estable = crudo;
    }
    return estable;
}
