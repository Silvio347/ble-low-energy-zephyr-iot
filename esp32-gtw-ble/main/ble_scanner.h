#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "payload_parser.h"

typedef void (*ble_scanner_sample_cb_t)(const sensor_payload_t *payload, int8_t rssi, void *ctx);

typedef struct {
    uint16_t company_id;
    uint16_t target_device_id;
    ble_scanner_sample_cb_t sample_cb;
    void *sample_ctx;
} ble_scanner_config_t;

esp_err_t ble_scanner_start(const ble_scanner_config_t *config);
