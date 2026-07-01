#include "app_config.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#if __has_include("default_config.h")
#include "default_config.h"
#else
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""
#define DEFAULT_MQTT_HOST ""
#define DEFAULT_MQTT_PORT 0
#define DEFAULT_MQTT_USERNAME ""
#define DEFAULT_MQTT_PASSWORD ""
#define DEFAULT_MQTT_TLS true
#define DEFAULT_MQTT_CLIENT_ID "esp32-ble-gateway"
#define DEFAULT_MQTT_BASE_TOPIC "lowble"
#define DEFAULT_BLE_COMPANY_ID 0
#define DEFAULT_BLE_TARGET_DEVICE_ID 347
#endif

static const char *TAG = "app_config";
static const char *NVS_NAMESPACE = "gateway_cfg";

static void copy_string(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_len, "%s", src);
}

esp_err_t app_config_init(void)
{
    return ESP_OK;
}

void app_config_load_defaults(app_config_t *config)
{
    memset(config, 0, sizeof(*config));
    copy_string(config->wifi_ssid, sizeof(config->wifi_ssid), DEFAULT_WIFI_SSID);
    copy_string(config->wifi_password, sizeof(config->wifi_password), DEFAULT_WIFI_PASSWORD);
    copy_string(config->mqtt_host, sizeof(config->mqtt_host), DEFAULT_MQTT_HOST);
    config->mqtt_port = DEFAULT_MQTT_PORT;
    copy_string(config->mqtt_username, sizeof(config->mqtt_username), DEFAULT_MQTT_USERNAME);
    copy_string(config->mqtt_password, sizeof(config->mqtt_password), DEFAULT_MQTT_PASSWORD);
    config->mqtt_tls = DEFAULT_MQTT_TLS;
    copy_string(config->mqtt_client_id, sizeof(config->mqtt_client_id), DEFAULT_MQTT_CLIENT_ID);
    copy_string(config->mqtt_base_topic, sizeof(config->mqtt_base_topic), DEFAULT_MQTT_BASE_TOPIC);
    config->ble_company_id = DEFAULT_BLE_COMPANY_ID;
    config->ble_target_device_id = DEFAULT_BLE_TARGET_DEVICE_ID;
}

static esp_err_t get_str(nvs_handle_t nvs, const char *key, char *value, size_t value_len)
{
    size_t required = value_len;
    esp_err_t err = nvs_get_str(nvs, key, value, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value[0] = '\0';
    }
    return err;
}

bool app_config_load(app_config_t *config)
{
    app_config_load_defaults(config);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved configuration found, using defaults");
        return false;
    }

    uint8_t configured = 0;
    err = nvs_get_u8(nvs, "configured", &configured);
    if (err != ESP_OK || configured != 1) {
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved configuration marker is missing, using defaults");
        return false;
    }

    (void)get_str(nvs, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    (void)get_str(nvs, "wifi_pass", config->wifi_password, sizeof(config->wifi_password));
    (void)get_str(nvs, "mqtt_host", config->mqtt_host, sizeof(config->mqtt_host));
    (void)nvs_get_u16(nvs, "mqtt_port", &config->mqtt_port);
    (void)get_str(nvs, "mqtt_user", config->mqtt_username, sizeof(config->mqtt_username));
    (void)get_str(nvs, "mqtt_pass", config->mqtt_password, sizeof(config->mqtt_password));
    uint8_t mqtt_tls = config->mqtt_tls ? 1 : 0;
    (void)nvs_get_u8(nvs, "mqtt_tls", &mqtt_tls);
    config->mqtt_tls = mqtt_tls != 0;
    (void)get_str(nvs, "mqtt_id", config->mqtt_client_id, sizeof(config->mqtt_client_id));
    (void)get_str(nvs, "mqtt_base", config->mqtt_base_topic, sizeof(config->mqtt_base_topic));
    (void)nvs_get_u16(nvs, "company_id", &config->ble_company_id);
    (void)nvs_get_u16(nvs, "device_id", &config->ble_target_device_id);

    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded saved configuration from NVS");
    return true;
}

esp_err_t app_config_save(const app_config_t *config)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "wifi_ssid", config->wifi_ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_pass", config->wifi_password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_host", config->mqtt_host);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs, "mqtt_port", config->mqtt_port);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_user", config->mqtt_username);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_pass", config->mqtt_password);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "mqtt_tls", config->mqtt_tls ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_id", config->mqtt_client_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "mqtt_base", config->mqtt_base_topic);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs, "company_id", config->ble_company_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(nvs, "device_id", config->ble_target_device_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(nvs, "configured", 1);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);
    return err;
}

esp_err_t app_config_erase(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

bool app_config_is_valid(const app_config_t *config)
{
    if (config->wifi_ssid[0] == '\0') {
        return false;
    }
    if (config->mqtt_host[0] == '\0') {
        return false;
    }
    if (config->mqtt_port == 0) {
        return false;
    }
    if (config->mqtt_client_id[0] == '\0') {
        return false;
    }
    if (config->mqtt_base_topic[0] == '\0') {
        return false;
    }
    return true;
}
