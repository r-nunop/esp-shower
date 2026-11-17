#include "sensor_turbidez.h"
#include "config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static adc_oneshot_unit_handle_t adc1 = NULL;

void sensor_turbidez_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc1));
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1, CANAL_TURBIDEZ, &chan_cfg));
}

bool sensor_turbidez_leer(int *raw, float *pct)
{
    int v;
    if (adc_oneshot_read(adc1, CANAL_TURBIDEZ, &v) != ESP_OK) return false;
    *raw = v;
    *pct = (v * 100.0f) / 4095.0f;
    return true;
}
