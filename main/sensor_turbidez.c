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
    if (adc1 == NULL) return false;

    // Media de varias muestras para filtrar el ruido del ADC.
    int suma = 0;
    for (int i = 0; i < TURBIDEZ_MUESTRAS; i++) {
        int v;
        if (adc_oneshot_read(adc1, CANAL_TURBIDEZ, &v) != ESP_OK) return false;
        suma += v;
    }
    int media = suma / TURBIDEZ_MUESTRAS;

    // El sensor baja de tensión cuando el agua se ensucia: se invierte la escala
    // para que 0 % sea agua limpia y 100 % agua muy turbia.
    float p = (float)(TURBIDEZ_RAW_LIMPIA - media) * 100.0f /
              (float)(TURBIDEZ_RAW_LIMPIA - TURBIDEZ_RAW_TURBIA);
    if (p < 0.0f) p = 0.0f;
    if (p > 100.0f) p = 100.0f;

    *raw = media;
    *pct = p;
    return true;
}
