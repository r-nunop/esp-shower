#include "mcp23017.h"
#include "bus_i2c.h"
#include "esp_log.h"

static const char *TAG = "MCP23017";

// Banco 0: el registro del puerto B va justo después del de A
#define REG_IODIRA   0x00
#define REG_OLATA    0x14
#define TIMEOUT_MS   50

static i2c_master_dev_handle_t dev = NULL;
static bool fallo = true;

// Si el MCP no responde no sabemos cómo están los relés, así que se corta la extensión
static void cortar_extension(void)
{
    bus_i2c_cortar_extension();
    fallo = true;
    ESP_LOGE(TAG, "Fallo en el MCP23017: extensión sin alimentación");
}

static esp_err_t escribir_reg(uint8_t reg, uint8_t valor)
{
    if (fallo) return ESP_ERR_INVALID_STATE;

    uint8_t buf[2] = { reg, valor };
    // Un reintento por si ha sido ruido puntual en el bus
    esp_err_t ret = i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS);
    if (ret != ESP_OK) ret = i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS);
    if (ret != ESP_OK) cortar_extension();
    return ret;
}

esp_err_t mcp23017_init(void)
{
    if (dev != NULL) return fallo ? ESP_ERR_INVALID_STATE : ESP_OK;

    i2c_master_bus_handle_t bus = bus_i2c_handle();
    if (bus == NULL) return ESP_ERR_INVALID_STATE;

    // La placa viene en 0x27, pero en algunos lotes está en 0x20
    const uint8_t direcciones[] = { 0x27, 0x20 };
    uint8_t dir = 0;
    for (int i = 0; i < 2; i++) {
        if (i2c_master_probe(bus, direcciones[i], TIMEOUT_MS) == ESP_OK) {
            dir = direcciones[i];
            break;
        }
    }
    if (dir == 0) {
        ESP_LOGE(TAG, "No encontrado en 0x27 ni en 0x20");
        cortar_extension();
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Encontrado en 0x%02x", dir);

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dir,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (ret != ESP_OK) {
        dev = NULL;
        cortar_extension();
        return ret;
    }
    fallo = false;
    return ESP_OK;
}

// salidas: bit a 1 = pin de salida. El resto se queda como entrada.
esp_err_t mcp23017_configurar(mcp_puerto_t puerto, uint8_t olat, uint8_t salidas)
{
    // Primero el valor y luego la dirección, para que al pasar a salida ya esté apagado
    esp_err_t ret = escribir_reg(REG_OLATA + puerto, olat);
    if (ret == ESP_OK) ret = escribir_reg(REG_IODIRA + puerto, (uint8_t)~salidas);
    return ret;
}

esp_err_t mcp23017_escribir(mcp_puerto_t puerto, uint8_t olat)
{
    return escribir_reg(REG_OLATA + puerto, olat);
}

bool mcp23017_ok(void)
{
    return !fallo;
}
