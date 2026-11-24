#include "energia.h"
#include "esp_pm.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ENERGIA";

#ifdef CONFIG_SHOWER_LIGHT_SLEEP
#define LIGHT_SLEEP  true
#else
#define LIGHT_SLEEP  false
#endif

void energia_modo_reposo(bool reposo)
{
#ifdef CONFIG_PM_ENABLE
    static int actual = -1;
    if (actual == (int)reposo) return;
    actual = reposo;

    esp_pm_config_t cfg = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = reposo && LIGHT_SLEEP,
    };
    esp_err_t ret = esp_pm_configure(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo configurar la gestion de energia (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Light sleep %s", cfg.light_sleep_enable ? "activado" : "desactivado");
    }
#endif
}
