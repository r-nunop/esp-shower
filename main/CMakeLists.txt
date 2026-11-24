idf_component_register(
    SRCS
        "main.c"
        "wifi_manager.c"
        "thingsboard.c"
        "ota.c"
        "sensor_temperatura.c"
        "sensor_turbidez.c"
        "sensor_nivel.c"
        "actuadores.c"
        "bus_i2c.c"
        "mcp23017.c"
        "ads1115.c"
        "pantalla.c"
        "avisos.c"
        "seguridad.c"
        "maquina_estados.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash esp_wifi esp_netif esp_event spiffs esp_http_server
        esp_http_client esp_https_ota app_update esp-tls mqtt json
        esp_adc driver esp_timer
)
