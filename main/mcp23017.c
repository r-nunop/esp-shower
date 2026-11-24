#include "mcp23017.h"
#include "bus_i2c.h"
#include "esp_log.h"

static const char *TAG = "MCP23017";

#define REG_IODIRA   0x00
#define REG_OLATA    0x14
#define TIMEOUT_MS   50

static i2c_master_dev_handle_t dev = NULL;

static esp_err_t escribir_reg(uint8_t reg, uint8_t valor)
{
    uint8_t buf[2] = { reg, valor };
    return i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS);
}

// salidas_a: bit a 1 = pin de salida. El resto se queda como entrada.
esp_err_t mcp23017_init(uint8_t olat_a, uint8_t salidas_a)
{
    if (dev != NULL) return ESP_OK;

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
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "Encontrado en 0x%02x", dir);

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dir,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (ret != ESP_OK) return ret;

    // Primero el valor y luego la dirección, para que al pasar a salida ya esté apagado
    ret = escribir_reg(REG_OLATA, olat_a);
    if (ret == ESP_OK) ret = escribir_reg(REG_IODIRA, (uint8_t)~salidas_a);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(dev);
        dev = NULL;
    }
    return ret;
}

esp_err_t mcp23017_escribir_a(uint8_t olat_a)
{
    if (dev == NULL) return ESP_ERR_INVALID_STATE;
    return escribir_reg(REG_OLATA, olat_a);
}
