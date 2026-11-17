#include "actuadores.h"
#include "config.h"
#include "driver/gpio.h"

#define NIVEL_ON   (RELE_NIVEL_ACTIVO)
#define NIVEL_OFF  (!RELE_NIVEL_ACTIVO)

static void rele(gpio_num_t pin, bool on)
{
    gpio_set_level(pin, on ? NIVEL_ON : NIVEL_OFF);
}

void actuadores_init(void)
{
    const gpio_num_t pines[] = { PIN_RELE_BOMBA, PIN_RELE_VALVULA, PIN_RELE_CALENTADOR, PIN_RELE_LIBRE };
    for (int i = 0; i < 4; i++) {
        gpio_set_level(pines[i], NIVEL_OFF);
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pines[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(pines[i], NIVEL_OFF);
    }
}

void actuadores_bomba(bool on)             { rele(PIN_RELE_BOMBA, on); }
void actuadores_valvula_ducha(bool ducha)  { rele(PIN_RELE_VALVULA, ducha); }
void actuadores_calentador(bool on)        { rele(PIN_RELE_CALENTADOR, on); }

void actuadores_todo_off(void)
{
    actuadores_calentador(false);
    actuadores_bomba(false);
    actuadores_valvula_ducha(false);
}
