#include "actuadores.h"
#include "config.h"
#include "bus_i2c.h"
#include "mcp23017.h"
#include "driver/gpio.h"

// Los 4 relés de la extensión (GPA0-GPA3) como salidas, también los que no se usan
#define MASCARA_RELES   0x0F
#define OLAT_APAGADO    (RELE_EXT_NIVEL_ON ? 0x00 : MASCARA_RELES)

static uint8_t olat = OLAT_APAGADO;

static void rele_ext(int n, bool on)
{
    bool nivel = on ? RELE_EXT_NIVEL_ON : !RELE_EXT_NIVEL_ON;
    uint8_t nuevo = nivel ? (olat | (1 << n)) : (olat & ~(1 << n));
    if (nuevo == olat) return;

    if (mcp23017_escribir(MCP_PUERTO_A, nuevo) == ESP_OK) olat = nuevo;
}

void actuadores_init(void)
{
    gpio_set_level(PIN_RELE_BOMBA, !BOMBA_NIVEL_ON);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_RELE_BOMBA,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_RELE_BOMBA, !BOMBA_NIVEL_ON);

    if (bus_i2c_init() == ESP_OK && mcp23017_init() == ESP_OK) {
        mcp23017_configurar(MCP_PUERTO_A, OLAT_APAGADO, MASCARA_RELES);
    }
}

bool actuadores_ok(void)
{
    return mcp23017_ok();
}

void actuadores_bomba(bool on)
{
    gpio_set_level(PIN_RELE_BOMBA, on ? BOMBA_NIVEL_ON : !BOMBA_NIVEL_ON);
}

void actuadores_valvula_ducha(bool ducha)  { rele_ext(MCP_RELE_VALVULA, ducha); }
void actuadores_calentador(bool on)        { rele_ext(MCP_RELE_CALENTADOR, on); }

void actuadores_todo_off(void)
{
    actuadores_calentador(false);
    actuadores_bomba(false);
    actuadores_valvula_ducha(false);
}
