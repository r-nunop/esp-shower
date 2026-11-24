#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "thingsboard.h"
#include "config.h"

#define PREFIJO_RPC      "v1/devices/me/rpc/request/"
#define PREFIJO_RPC_LEN  (sizeof(PREFIJO_RPC) - 1)

static const char *TAG = "TB";
static esp_mqtt_client_handle_t cliente = NULL;
static cb_comando_t cb_comando = NULL;
static volatile bool conectado = false;

static void responder(const char *req_id, const char *json)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "v1/devices/me/rpc/response/%s", req_id);
    esp_mqtt_client_publish(cliente, topic, json, 0, 1, 0);
}

static void procesar_rpc(const char *req_id, const char *data, int data_len)
{
    cJSON *json = cJSON_ParseWithLength(data, data_len);
    if (json == NULL) {
        responder(req_id, "{\"status\":\"json_invalido\"}");
        return;
    }

    cJSON *method = cJSON_GetObjectItem(json, "method");
    cJSON *params = cJSON_GetObjectItem(json, "params");
    comando_t cmd = {0};
    bool valido = false;
    bool necesita_num = false;

    if (cJSON_IsString(method) && method->valuestring) {
        const char *m = method->valuestring;
        if (strcmp(m, RPC_INICIAR) == 0)             { cmd.tipo = CMD_INICIAR;      valido = true; }
        else if (strcmp(m, RPC_DETENER) == 0)        { cmd.tipo = CMD_DETENER;      valido = true; }
        else if (strcmp(m, RPC_RESET) == 0)          { cmd.tipo = CMD_RESET;        valido = true; }
        else if (strcmp(m, RPC_OTA) == 0)            { cmd.tipo = CMD_OTA;          valido = true; }
        else if (strcmp(m, RPC_SET_TEMP) == 0)       { cmd.tipo = CMD_SET_TEMP;     valido = true; necesita_num = true; }
        else if (strcmp(m, RPC_SET_CADENCIA) == 0)   { cmd.tipo = CMD_SET_CADENCIA; valido = true; necesita_num = true; }
    }

    if (valido && necesita_num) {
        if (cJSON_IsNumber(params)) cmd.valor = (float)params->valuedouble;
        else valido = false;
    }

    if (!valido) {
        responder(req_id, "{\"status\":\"comando_invalido\"}");
    } else if (cb_comando && cb_comando(&cmd)) {
        responder(req_id, "{\"status\":\"ok\"}");
    } else {
        responder(req_id, "{\"status\":\"rechazado\"}");
    }
    cJSON_Delete(json);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado");
        conectado = true;
        esp_mqtt_client_subscribe(event->client, PREFIJO_RPC "+", 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        conectado = false;
        break;
    case MQTT_EVENT_DATA:
        if (event->topic != NULL &&
            event->topic_len > (int)PREFIJO_RPC_LEN &&
            strncmp(event->topic, PREFIJO_RPC, PREFIJO_RPC_LEN) == 0) {
            char req_id[20];
            snprintf(req_id, sizeof(req_id), "%.*s", (int)(event->topic_len - PREFIJO_RPC_LEN), event->topic + PREFIJO_RPC_LEN);
            procesar_rpc(req_id, event->data, event->data_len);
        }
        break;
    default:
        break;
    }
}

void tb_init(cb_comando_t on_comando)
{
    cb_comando = on_comando;
}

void tb_iniciar(void)
{
    if (cliente != NULL) return;
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_TB_URI,
        .credentials.username = CONFIG_TB_TOKEN,
    };
    cliente = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(cliente, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(cliente);
}

bool tb_conectado(void)
{
    return conectado;
}

void tb_publicar(const lecturas_t *l, estado_t e, codigo_error_t err, float t_obj, bool filtro_saturado)
{
    if (cliente == NULL || !conectado) return;

    cJSON *root = cJSON_CreateObject();
    if (l->temp_valida) cJSON_AddNumberToObject(root, "temp", l->temp_c);
    if (l->turbidez_valida) {
        cJSON_AddNumberToObject(root, "turbidez", l->turbidez_pct);
        cJSON_AddNumberToObject(root, "turbidez_v", l->turbidez_v);
    }
    cJSON_AddBoolToObject(root, "nivel_ok", l->nivel_ok);
    cJSON_AddNumberToObject(root, "estado", e);
    cJSON_AddNumberToObject(root, "error", err);
    cJSON_AddNumberToObject(root, "temp_objetivo", t_obj);
    cJSON_AddBoolToObject(root, "filtro_saturado", filtro_saturado);

    char *payload = cJSON_PrintUnformatted(root);
    if (payload) {
        esp_mqtt_client_publish(cliente, "v1/devices/me/telemetry", payload, 0, 1, 0);
        free(payload);
    }
    cJSON_Delete(root);
}
