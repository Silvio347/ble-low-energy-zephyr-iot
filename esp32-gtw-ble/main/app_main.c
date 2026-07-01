#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "ble_scanner.h"
#include "config_portal.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mqtt_client_app.h"
#include "nvs_flash.h"
#include "packet_loss.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define APP_CONFIG_RESET_GPIO GPIO_NUM_0
#define SAMPLE_QUEUE_DEPTH 16
#define MQTT_PUBLISH_MIN_INTERVAL_US 1000000

typedef struct {
    sensor_payload_t payload;
    int8_t rssi;
} gateway_sample_t;

static const char *TAG = "app_main";

static QueueHandle_t s_sample_queue;
static app_config_t s_config;
static packet_loss_tracker_t s_packet_loss;

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
}

static void maybe_reset_config_from_button(void)
{
    gpio_reset_pin(APP_CONFIG_RESET_GPIO);
    gpio_set_direction(APP_CONFIG_RESET_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(APP_CONFIG_RESET_GPIO, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(50));

    if (gpio_get_level(APP_CONFIG_RESET_GPIO) == 0) {
        ESP_LOGW(TAG, "Reset GPIO is held low, erasing saved configuration");
        ESP_ERROR_CHECK(app_config_erase());
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

static void start_configuration_mode(void)
{
    ESP_LOGW(TAG, "Starting configuration mode");
    ESP_ERROR_CHECK(wifi_manager_start_config_ap());
    ESP_ERROR_CHECK(config_portal_start(&s_config));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void ble_sample_cb(const sensor_payload_t *payload, int8_t rssi, void *ctx)
{
    (void)ctx;

    gateway_sample_t sample = {
        .payload = *payload,
        .rssi = rssi,
    };

    if (xQueueSend(s_sample_queue, &sample, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Sample queue is full, dropping BLE sample");
    }
}

static void publisher_task(void *arg)
{
    (void)arg;

    gateway_sample_t sample;
    while (true) {
        if (xQueueReceive(s_sample_queue, &sample, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        static int64_t last_publish_us = -MQTT_PUBLISH_MIN_INTERVAL_US;
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_publish_us < MQTT_PUBLISH_MIN_INTERVAL_US) {
            continue;
        }
        last_publish_us = now_us;

        packet_loss_result_t packet_loss = packet_loss_update(&s_packet_loss, sample.payload.counter);
        esp_err_t err = mqtt_client_app_publish_sensor(&s_config, &sample.payload, sample.rssi, &packet_loss);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Failed to publish sensor payload: %s", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
    init_nvs();
    ESP_ERROR_CHECK(app_config_init());
    (void)app_config_load(&s_config);
    maybe_reset_config_from_button();

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t event_loop_err = esp_event_loop_create_default();
    if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(event_loop_err);
    }
    ESP_ERROR_CHECK(wifi_manager_init());

    if (!app_config_is_valid(&s_config)) {
        ESP_LOGW(TAG, "Configuration is missing or invalid");
        start_configuration_mode();
    }

    ESP_ERROR_CHECK(wifi_manager_start_sta(&s_config));
    if (!wifi_manager_wait_connected(30000)) {
        ESP_LOGW(TAG, "Wi-Fi connection timed out, opening configuration portal");
        start_configuration_mode();
    }

    ESP_ERROR_CHECK(config_portal_start(&s_config));
    ESP_ERROR_CHECK(mqtt_client_app_start(&s_config));

    s_sample_queue = xQueueCreate(SAMPLE_QUEUE_DEPTH, sizeof(gateway_sample_t));
    if (s_sample_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create sample queue");
        abort();
    }

    packet_loss_init(&s_packet_loss);
    xTaskCreate(publisher_task, "publisher_task", 6144, NULL, 5, NULL);

    ble_scanner_config_t scanner_config = {
        .company_id = s_config.ble_company_id,
        .target_device_id = s_config.ble_target_device_id,
        .sample_cb = ble_sample_cb,
        .sample_ctx = NULL,
    };
    ESP_ERROR_CHECK(ble_scanner_start(&scanner_config));

    ESP_LOGI(TAG, "Gateway is running");
}
