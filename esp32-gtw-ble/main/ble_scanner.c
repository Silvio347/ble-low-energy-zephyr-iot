#include "ble_scanner.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "ble_scanner";

static ble_scanner_config_t s_config;
static uint8_t s_own_addr_type;

static void start_scan_with_callback(void);

static uint16_t read_company_id(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void handle_discovery(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0 || fields.mfg_data == NULL || fields.mfg_data_len < 2 + SENSOR_PAYLOAD_SIZE) {
        return;
    }

    uint16_t company_id = read_company_id(fields.mfg_data);
    if (company_id != s_config.company_id) {
        return;
    }

    sensor_payload_t payload;
    if (!payload_parser_parse(fields.mfg_data + 2, SENSOR_PAYLOAD_SIZE, &payload)) {
        return;
    }

    if (payload.device_id != s_config.target_device_id) {
        return;
    }

    if (s_config.sample_cb != NULL) {
        s_config.sample_cb(&payload, disc->rssi, s_config.sample_ctx);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_discovery(&event->disc);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGW(TAG, "BLE scan completed unexpectedly, restarting");
        start_scan_with_callback();
        return 0;
    default:
        return 0;
    }
}

static void start_scan_with_callback(void)
{
    struct ble_gap_disc_params params = {
        .itvl = 0x60,
        .window = 0x30,
        .filter_policy = 0,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 0,
    };

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start BLE scan: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE scanning started for company_id=0x%04x device_id=%u",
                 s_config.company_id, s_config.target_device_id);
    }
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer BLE own address type: rc=%d", rc);
        return;
    }

    start_scan_with_callback();
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_scanner_start(const ble_scanner_config_t *config)
{
    if (config == NULL || config->sample_cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&s_config, config, sizeof(s_config));

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
