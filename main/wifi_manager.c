#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_spiffs.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager.h"
#include "config.h"

static const char *TAG = "WIFI";
static void (*cb_got_ip)(void) = NULL;
static bool ip_notificada = false;

static void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error al montar SPIFFS (%s)", esp_err_to_name(ret));
    }
}

static bool read_wifi_credentials(char *ssid, char *password)
{
    FILE *f = fopen("/spiffs/wifi_config.txt", "r");
    if (f == NULL) return false;
    if (fgets(ssid, 32, f) == NULL) { fclose(f); return false; }
    ssid[strcspn(ssid, "\r\n")] = 0;
    if (fgets(password, 64, f) == NULL) { fclose(f); return false; }
    password[strcspn(password, "\r\n")] = 0;
    fclose(f);
    return true;
}

static void save_wifi_credentials(const char *ssid, const char *password)
{
    FILE *f = fopen("/spiffs/wifi_config.txt", "w");
    if (f == NULL) return;
    fprintf(f, "%s\n%s\n", ssid, password);
    fclose(f);
}

static esp_err_t index_get_handler(httpd_req_t *req)
{
    const char *html = "<!DOCTYPE html>"
        "<html><body style='font-family: Arial; padding: 20px;'>"
        "<h2>Configurar WiFi S-PockShower</h2>"
        "<form action=\"/guardar\" method=\"POST\">"
        "SSID:<br><input type=\"text\" name=\"ssid\" required><br><br>"
        "Password:<br><input type=\"password\" name=\"pwd\" required><br><br>"
        "<input type=\"submit\" value=\"Guardar y Conectar\">"
        "</form></body></html>";
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t guardar_post_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char nuevo_ssid[32] = {0};
    char nueva_pwd[64] = {0};

    if (httpd_query_key_value(buf, "ssid", nuevo_ssid, sizeof(nuevo_ssid)) == ESP_OK &&
        httpd_query_key_value(buf, "pwd", nueva_pwd, sizeof(nueva_pwd)) == ESP_OK) {
        save_wifi_credentials(nuevo_ssid, nueva_pwd);
        const char *resp = "<h1>Credenciales guardadas!</h1><p>Reiniciando...</p>";
        httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        httpd_resp_send_500(req);
    }
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_get = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_post = { .uri = "/guardar", .method = HTTP_POST, .handler = guardar_post_handler, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri_post);
        return server;
    }
    return NULL;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        if (!ip_notificada) {
            ip_notificada = true;
            esp_sntp_config_t config_sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&config_sntp);
            if (cb_got_ip) cb_got_ip();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Desconectado. Reintentando...");
        esp_wifi_connect();
    }
}

void wifi_manager_init(void (*on_got_ip)(void))
{
    cb_got_ip = on_got_ip;
    init_spiffs();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    char ssid[32] = {0};
    char password[64] = {0};

    if (read_wifi_credentials(ssid, password)) {
        ESP_LOGI(TAG, "Modo STA hacia: %s", ssid);
        esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        esp_wifi_connect();
    } else {
        ESP_LOGW(TAG, "Sin credenciales. Modo AP");
        esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        strcpy((char *)wifi_config.ap.ssid, AP_SSID);
        wifi_config.ap.ssid_len = 0;
        wifi_config.ap.channel = AP_CANAL;
        wifi_config.ap.password[0] = '\0';
        wifi_config.ap.max_connection = 4;
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        start_webserver();
    }
}
