#include "payload_parser.h"

#include <string.h>

#include "esp_bit_defs.h"

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint8_t payload_parser_checksum(const uint8_t *data, size_t len_without_checksum)
{
    uint8_t checksum = 0;
    for (size_t i = 0; i < len_without_checksum; ++i) {
        checksum = (uint8_t)(checksum + data[i]);
    }
    return checksum;
}

bool payload_parser_parse(const uint8_t *data, size_t len, sensor_payload_t *payload)
{
    if (data == NULL || payload == NULL || len != SENSOR_PAYLOAD_SIZE) {
        return false;
    }

    memset(payload, 0, sizeof(*payload));
    payload->device_id = read_le16(&data[0]);
    payload->temperature_x10 = (int16_t)read_le16(&data[2]);
    payload->humidity_x10 = read_le16(&data[4]);
    payload->battery_mv = read_le16(&data[6]);
    payload->counter = read_le32(&data[8]);
    payload->flags = data[12];
    payload->checksum = data[13];
    payload->checksum_valid = payload_parser_checksum(data, SENSOR_PAYLOAD_SIZE - 1) == payload->checksum;

    return true;
}
