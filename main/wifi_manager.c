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

#define SSID_MAX  32    // longitud máxima de un SSID
#define PWD_MAX   63    // longitud máxima de una contraseña WPA2

static const char *TAG = "WIFI";
static void (*cb_got_ip)(void) = NULL;
static bool ip_notificada = false;
static bool conectada_alguna_vez = false;
static bool ap_activo = false;
static int reintentos = 0;

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

// Los buffers deben tener sitio para el texto, el salto de línea y el '\0'
// (SSID_MAX + 2 y PWD_MAX + 2); si no, un SSID de 32 caracteres se leería mal.
static bool read_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pwd_len)
{
    FILE *f = fopen("/spiffs/wifi_config.txt", "r");
    if (f == NULL) return false;
    if (fgets(ssid, (int)ssid_len, f) == NULL) { fclose(f); return false; }
    ssid[strcspn(ssid, "\r\n")] = 0;
    if (fgets(password, (int)pwd_len, f) == NULL) { fclose(f); return false; }
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

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// El navegador envía el formulario codificado ("+" = espacio, "%40" = "@", ...).
// httpd_query_key_value NO lo decodifica, así que hay que hacerlo a mano.
static void url_decode(char *dst, size_t dst_len, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_len; i++) {
        if (src[i] == '+') {
            dst[j++] = ' ';
        } else if (src[i] == '%' && hex_val(src[i + 1]) >= 0 && hex_val(src[i + 2]) >= 0) {
            dst[j++] = (char)(hex_val(src[i + 1]) * 16 + hex_val(src[i + 2]));
            i += 2;
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
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
    char buf[384];
    if (req->content_len == 0 || req->content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Datos no validos");
        return ESP_FAIL;
    }

    int recibido = 0;
    while (recibido < (int)req->content_len) {
        int r = httpd_req_recv(req, buf + recibido, (size_t)req->content_len - (size_t)recibido);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        recibido += r;
    }
    buf[recibido] = '\0';

    // Cada carácter puede ocupar hasta 3 bytes codificado ("%XX").
    char ssid_raw[3 * SSID_MAX + 1] = {0};
    char pwd_raw[3 * PWD_MAX + 1] = {0};
    char nuevo_ssid[SSID_MAX + 1];
    char nueva_pwd[PWD_MAX + 1];

    if (httpd_query_key_value(buf, "ssid", ssid_raw, sizeof(ssid_raw)) == ESP_OK &&
        httpd_query_key_value(buf, "pwd", pwd_raw, sizeof(pwd_raw)) == ESP_OK) {
        url_decode(nuevo_ssid, sizeof(nuevo_ssid), ssid_raw);
        url_decode(nueva_pwd, sizeof(nueva_pwd), pwd_raw);
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

// Debe llamarse con el modo Wi-Fi ya en AP o APSTA.
static void configurar_ap(void)
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    strcpy((char *)wifi_config.ap.ssid, AP_SSID);
    wifi_config.ap.ssid_len = 0;
    wifi_config.ap.channel = AP_CANAL;
    wifi_config.ap.password[0] = '\0';
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
}

// Se abre en su propia tarea porque el manejador de eventos tiene poca pila.
static void portal_task(void *arg)
{
    ESP_LOGW(TAG, "Sin conexion tras %d intentos. Abro el portal AP (%s)", WIFI_MAX_REINTENTOS, AP_SSID);
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    configurar_ap();
    start_webserver();
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP asignada: " IPSTR, IP2STR(&event->ip_info.ip));
        reintentos = 0;
        conectada_alguna_vez = true;
        if (!ip_notificada) {
            ip_notificada = true;
            esp_sntp_config_t config_sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            esp_netif_sntp_init(&config_sntp);
            if (cb_got_ip) cb_got_ip();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        reintentos++;
        ESP_LOGW(TAG, "Desconectado (intento %d). Reintentando...", reintentos);
        // Si nunca ha llegado a conectar (contraseña o red incorrectas), se abre
        // el portal para poder cambiar las credenciales sin reflashear.
        if (!conectada_alguna_vez && reintentos >= WIFI_MAX_REINTENTOS && !ap_activo) {
            ap_activo = true;
            xTaskCreate(portal_task, "portal_ap", 4096, NULL, 4, NULL);
        }
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

    char ssid[SSID_MAX + 2] = {0};
    char password[PWD_MAX + 2] = {0};

    if (read_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "Modo STA hacia: %s", ssid);
        esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t wifi_config = {0};
        size_t n_ssid = strnlen(ssid, sizeof(wifi_config.sta.ssid));
        size_t n_pwd = strnlen(password, sizeof(wifi_config.sta.password));
        memcpy(wifi_config.sta.ssid, ssid, n_ssid);
        memcpy(wifi_config.sta.password, password, n_pwd);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        esp_wifi_connect();
    } else {
        ESP_LOGW(TAG, "Sin credenciales. Modo AP");
        ap_activo = true;
        esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        configurar_ap();
        ESP_ERROR_CHECK(esp_wifi_start());
        start_webserver();
    }
}
