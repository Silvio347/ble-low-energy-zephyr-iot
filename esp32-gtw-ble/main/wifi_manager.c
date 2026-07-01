#include "wifi_manager.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_manager";
static const int WIFI_CONNECTED_BIT = BIT0;

static EventGroupHandle_t s_wifi_event_group;
static bool s_initialized;
static bool s_sta_enabled;

static void stop_wifi_if_running(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_ERROR_CHECK(err);
    }
}

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_sta_enabled) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected, IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Configuration and OTA portal: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, NULL));

    s_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_start_sta(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_sta_enabled = false;
    stop_wifi_if_running();
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, config->wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, config->wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode =
        config->wifi_password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    s_sta_enabled = true;
    ESP_LOGI(TAG, "Starting Wi-Fi station for SSID '%s'", config->wifi_ssid);
    return esp_wifi_start();
}

esp_err_t wifi_manager_start_config_ap(void)
{
    s_sta_enabled = false;
    stop_wifi_if_running();

    wifi_config_t ap_config = {
        .ap =
            {
                .ssid = "ESP32-Gateway-Setup",
                .ssid_len = 19,
                .channel = 1,
                .password = "",
                .max_connection = 4,
                .authmode = WIFI_AUTH_OPEN,
            },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "Starting configuration AP: %s", ap_config.ap.ssid);
    return esp_wifi_start();
}

bool wifi_manager_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
