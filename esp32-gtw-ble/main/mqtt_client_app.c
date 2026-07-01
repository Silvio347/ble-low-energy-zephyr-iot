#include "mqtt_client_app.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt_client_app";

static esp_mqtt_client_handle_t s_client;
static volatile bool s_connected;
static char s_uri[192];

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

esp_err_t mqtt_client_app_start(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(s_uri, sizeof(s_uri), "%s://%s:%u", config->mqtt_tls ? "mqtts" : "mqtt",
             config->mqtt_host, config->mqtt_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker =
            {
                .address.uri = s_uri,
                .verification.crt_bundle_attach = config->mqtt_tls ? esp_crt_bundle_attach : NULL,
            },
        .credentials =
            {
                .client_id = config->mqtt_client_id,
                .username = config->mqtt_username[0] != '\0' ? config->mqtt_username : NULL,
                .authentication.password =
                    config->mqtt_password[0] != '\0' ? config->mqtt_password : NULL,
            },
        .session =
            {
                .keepalive = 60,
            },
        .network =
            {
                .reconnect_timeout_ms = 5000,
            },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Connecting to MQTT broker %s", s_uri);
    return esp_mqtt_client_start(s_client);
}

bool mqtt_client_app_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_client_app_publish_sensor(const app_config_t *config,
                                         const sensor_payload_t *payload,
                                         int8_t rssi,
                                         const packet_loss_result_t *packet_loss)
{
    if (s_client == NULL || config == NULL || payload == NULL || packet_loss == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /*
     * Mantém tudo que veio do BLE.
     * Depois sobrescreve apenas os campos que você quer simular.
     */
    sensor_payload_t out_payload = *payload;

    static uint32_t sim_counter = 0;

    int temp_variation = (int)(sim_counter % 7) - 3;
    int hum_variation  = (int)((sim_counter * 3) % 11) - 5;

    /*
     * Simula apenas temperatura e umidade.
     * Mantém do BLE:
     * - device_id
     * - battery_mv
     * - counter
     * - checksum_valid
     * - checksum
     * - RSSI
     * - packet loss
     */
    out_payload.temperature_x10 = 195 + temp_variation;
    out_payload.humidity_x10    = 500 + hum_variation;

    /*
     * Marca que a leitura está OK e que parte do dado é simulada.
     * Mantém os outros flags que já vieram do BLE.
     */
    out_payload.flags |= FLAG_SENSOR_OK;
    out_payload.flags |= FLAG_BATTERY_SIM;
    out_payload.flags &= ~FLAG_DHT_ERROR;

    sim_counter++;

    cJSON_AddNumberToObject(root, "device_id", out_payload.device_id);
    cJSON_AddNumberToObject(root, "temperature_c", ((double)out_payload.temperature_x10) / 10.0);
    cJSON_AddNumberToObject(root, "humidity_percent", ((double)out_payload.humidity_x10) / 10.0);
    cJSON_AddNumberToObject(root, "battery_mv", out_payload.battery_mv);
    cJSON_AddNumberToObject(root, "counter", out_payload.counter);
    cJSON_AddNumberToObject(root, "flags", out_payload.flags);

    cJSON_AddBoolToObject(root, "sensor_ok", 
                          (out_payload.flags & FLAG_SENSOR_OK) != 0);

    cJSON_AddBoolToObject(root, "dht_error", 
                          (out_payload.flags & FLAG_DHT_ERROR) != 0);

    cJSON_AddBoolToObject(root, "low_power_mode", 
                          (out_payload.flags & FLAG_LOW_POWER_MODE) != 0);

    cJSON_AddBoolToObject(root, "battery_simulated", 
                          (out_payload.flags & FLAG_BATTERY_SIM) != 0);

    cJSON_AddNumberToObject(root, "rssi", rssi);
    cJSON_AddNumberToObject(root, "packet_loss_total", (double)packet_loss->total);
    cJSON_AddNumberToObject(root, "packet_loss_delta", packet_loss->delta);
    cJSON_AddBoolToObject(root, "checksum_valid", out_payload.checksum_valid);
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)(esp_timer_get_time() / 1000));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char topic[160];
    snprintf(topic, sizeof(topic), "%s/%u/telemetry",
             config->mqtt_base_topic,
             out_payload.device_id);

    int msg_id = esp_mqtt_client_publish(s_client, topic, json, 0, 1, 0);

    cJSON_free(json);

    if (msg_id < 0) {
        return ESP_FAIL;
    }

    return ESP_OK;
}