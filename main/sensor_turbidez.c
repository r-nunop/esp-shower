#include "sensor_turbidez.h"
#include "config.h"
#include "bus_i2c.h"
#include "mcp23017.h"
#include "ads1115.h"

// Cada entrada analógica tiene su MOSFET en el puerto B: AIN0 -> GPB0, AIN1 -> GPB1...
#define MOSFETS          0x0F
#define BIT_MOSFET       (1 << TURBIDEZ_CANAL_ADS)
#define OLAT_B_APAGADO   (MOSFET_NIVEL_ON ? 0x00 : MOSFETS)

static bool listo = false;
static bool encendido = false;

void sensor_turbidez_init(void)
{
    if (bus_i2c_init() != ESP_OK || mcp23017_init() != ESP_OK) return;
    // Los 4 MOSFET como salidas apagadas, para que no se queden flotando
    if (mcp23017_configurar(MCP_PUERTO_B, OLAT_B_APAGADO, MOSFETS) != ESP_OK) return;
    listo = (ads1115_init() == ESP_OK);
}

void sensor_turbidez_encender(bool on)
{
    if (on == encendido) return;

    bool nivel = on ? MOSFET_NIVEL_ON : !MOSFET_NIVEL_ON;
    uint8_t olat = nivel ? (OLAT_B_APAGADO | BIT_MOSFET) : (OLAT_B_APAGADO & ~BIT_MOSFET);
    if (mcp23017_escribir(MCP_PUERTO_B, olat) == ESP_OK) encendido = on;
}

bool sensor_turbidez_leer(float *voltios, float *pct)
{
    if (!listo || !encendido) return false;

    // Media de varias muestras para filtrar el ruido.
    float suma = 0.0f;
    for (int i = 0; i < TURBIDEZ_MUESTRAS; i++) {
        float v;
        if (ads1115_leer_v(TURBIDEZ_CANAL_ADS, &v) != ESP_OK) return false;
        suma += v;
    }
    float v = suma / TURBIDEZ_MUESTRAS * TURBIDEZ_DIVISOR;

    // El sensor baja de tensión cuando el agua se ensucia: se invierte la escala
    // para que 0 % sea agua limpia y 100 % agua muy turbia.
    float p = (TURBIDEZ_V_LIMPIA - v) * 100.0f / (TURBIDEZ_V_LIMPIA - TURBIDEZ_V_TURBIA);
    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;

    *voltios = v;
    *pct = p;
    return true;
}
