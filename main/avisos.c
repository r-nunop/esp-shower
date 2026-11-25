#include "avisos.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdbool.h>

static aviso_tipo_t aviso_actual = AVISO_NINGUNO;
static int pulsos_restantes = 0;
static int64_t t_ultimo_us = 0;
static int64_t duracion_pulso_us = 0;
static bool salida_activa = false;

static void set_salidas(bool on)
{
    gpio_set_level(PIN_LED, on ? 1 : 0);
    gpio_set_level(PIN_BUZZER, on ? 1 : 0);
}

void avisos_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LED) | (1ULL << PIN_BUZZER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    set_salidas(false);
}

void avisos_lanzar(aviso_tipo_t tipo)
{
    aviso_actual = tipo;
    t_ultimo_us = esp_timer_get_time();

    switch (tipo) {
    case AVISO_AGUA_LISTA:
        pulsos_restantes = 2;
        duracion_pulso_us = 200000;
        break;
    case AVISO_FILTRO_SATURADO:
        pulsos_restantes = 3;
        duracion_pulso_us = 400000;
        break;
    case AVISO_ERROR:
        pulsos_restantes = 5;
        duracion_pulso_us = 150000;
        break;
    default:
        pulsos_restantes = 0;
        salida_activa = false;
        set_salidas(false);
        return;
    }

    salida_activa = true;
    set_salidas(true);
}

void avisos_update(void)
{
    if (pulsos_restantes <= 0) {
        return;
    }

    int64_t ahora = esp_timer_get_time();
    if ((ahora - t_ultimo_us) < duracion_pulso_us) {
        return;
    }

    t_ultimo_us = ahora;

    if (salida_activa) {
        salida_activa = false;
        set_salidas(false);
        pulsos_restantes--;
        if (pulsos_restantes == 0) {
            aviso_actual = AVISO_NINGUNO;
        }
    } else {
        salida_activa = true;
        set_salidas(true);
    }
}