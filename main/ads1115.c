#include "ads1115.h"
#include "bus_i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static const char *TAG = "ADS1115";

#define DIRECCION       0x48
#define REG_CONVERSION  0x00
#define REG_CONFIG      0x01
#define TIMEOUT_MS      50
#define ESPERA_MAX_US   20000

// Campos del registro de configuración, según el datasheet del ADS1115
#define CFG_INICIO      0x8000                    // OS = 1
#define CFG_CANAL(c)    (0x4000 | ((c) << 12))    // MUX = 1xx: AINc respecto a GND
#define CFG_PGA_4V      0x0200                    // PGA = 001: ±4,096 V
#define CFG_SINGLE      0x0100                    // MODE = 1
#define CFG_250SPS      0x00A0                    // DR = 101
#define CFG_SIN_COMP    0x0003                    // COMP_QUE = 11
#define FONDO_ESCALA_V  4.096f

static i2c_master_dev_handle_t dev = NULL;

static esp_err_t leer_reg(uint8_t reg, uint16_t *valor)
{
    uint8_t buf[2];
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, buf, 2, TIMEOUT_MS);
    if (ret == ESP_OK) *valor = ((uint16_t)buf[0] << 8) | buf[1];
    return ret;
}

esp_err_t ads1115_init(void)
{
    if (dev != NULL) return ESP_OK;

    i2c_master_bus_handle_t bus = bus_i2c_handle();
    if (bus == NULL) return ESP_ERR_INVALID_STATE;

    if (i2c_master_probe(bus, DIRECCION, TIMEOUT_MS) != ESP_OK) {
        ESP_LOGE(TAG, "No encontrado en 0x%02x", DIRECCION);
        return ESP_ERR_NOT_FOUND;
    }

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DIRECCION,
        .scl_speed_hz = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &cfg, &dev);
    if (ret != ESP_OK) dev = NULL;
    return ret;
}

esp_err_t ads1115_leer_v(int canal, float *voltios)
{
    if (dev == NULL) return ESP_ERR_INVALID_STATE;
    if (canal < 0 || canal > 3) return ESP_ERR_INVALID_ARG;

    uint16_t config = CFG_INICIO | CFG_CANAL(canal) | CFG_PGA_4V | CFG_SINGLE | CFG_250SPS | CFG_SIN_COMP;
    uint8_t buf[3] = { REG_CONFIG, config >> 8, config & 0xFF };
    esp_err_t ret = i2c_master_transmit(dev, buf, sizeof(buf), TIMEOUT_MS);
    if (ret != ESP_OK) return ret;

    // El bit OS vuelve a 1 al acabar la conversión. Con tiempo máximo para no colgar la FSM
    int64_t inicio = esp_timer_get_time();
    uint16_t estado = 0;
    do {
        esp_rom_delay_us(500);
        ret = leer_reg(REG_CONFIG, &estado);
        if (ret != ESP_OK) return ret;
        if (esp_timer_get_time() - inicio > ESPERA_MAX_US) return ESP_ERR_TIMEOUT;
    } while (!(estado & CFG_INICIO));

    uint16_t crudo;
    ret = leer_reg(REG_CONVERSION, &crudo);
    if (ret != ESP_OK) return ret;

    *voltios = (int16_t)crudo * FONDO_ESCALA_V / 32768.0f;
    return ESP_OK;
}
