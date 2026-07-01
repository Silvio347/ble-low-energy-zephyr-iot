#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_CONFIG_WIFI_SSID_MAX_LEN 32
#define APP_CONFIG_WIFI_PASSWORD_MAX_LEN 64
#define APP_CONFIG_MQTT_HOST_MAX_LEN 127
#define APP_CONFIG_MQTT_USERNAME_MAX_LEN 63
#define APP_CONFIG_MQTT_PASSWORD_MAX_LEN 127
#define APP_CONFIG_MQTT_CLIENT_ID_MAX_LEN 63
#define APP_CONFIG_MQTT_BASE_TOPIC_MAX_LEN 95

typedef struct {
    char wifi_ssid[APP_CONFIG_WIFI_SSID_MAX_LEN + 1];
    char wifi_password[APP_CONFIG_WIFI_PASSWORD_MAX_LEN + 1];
    char mqtt_host[APP_CONFIG_MQTT_HOST_MAX_LEN + 1];
    uint16_t mqtt_port;
    char mqtt_username[APP_CONFIG_MQTT_USERNAME_MAX_LEN + 1];
    char mqtt_password[APP_CONFIG_MQTT_PASSWORD_MAX_LEN + 1];
    bool mqtt_tls;
    char mqtt_client_id[APP_CONFIG_MQTT_CLIENT_ID_MAX_LEN + 1];
    char mqtt_base_topic[APP_CONFIG_MQTT_BASE_TOPIC_MAX_LEN + 1];
    uint16_t ble_company_id;
    uint16_t ble_target_device_id;
} app_config_t;

esp_err_t app_config_init(void);
void app_config_load_defaults(app_config_t *config);
bool app_config_load(app_config_t *config);
esp_err_t app_config_save(const app_config_t *config);
esp_err_t app_config_erase(void);
bool app_config_is_valid(const app_config_t *config);
