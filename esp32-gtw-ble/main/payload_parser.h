#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_bit_defs.h"

#define SENSOR_PAYLOAD_SIZE 14

#define FLAG_SENSOR_OK BIT(0)
#define FLAG_BATTERY_SIM BIT(1)
#define FLAG_LOW_POWER_MODE BIT(2)
#define FLAG_DHT_ERROR BIT(3)

typedef struct {
    uint16_t device_id;
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint16_t battery_mv;
    uint32_t counter;
    uint8_t flags;
    uint8_t checksum;
    bool checksum_valid;
} sensor_payload_t;

bool payload_parser_parse(const uint8_t *data, size_t len, sensor_payload_t *payload);
uint8_t payload_parser_checksum(const uint8_t *data, size_t len_without_checksum);
