#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#define FLAG_SENSOR_OK        BIT(0)
#define FLAG_BATTERY_SIM      BIT(1)
#define FLAG_LOW_POWER_MODE   BIT(2)
#define FLAG_DHT_ERROR        BIT(3)

typedef struct __attribute__((packed)) {
	uint16_t device_id;
	int16_t temperature_x10;
	uint16_t humidity_x10;
	uint16_t battery_mv;
	uint32_t counter;
	uint8_t flags;
	uint8_t checksum;
} sensor_payload_t;

struct payload_measurement {
	int16_t temperature_x10;
	uint16_t humidity_x10;
};

void payload_codec_build(sensor_payload_t *payload,
			 const struct payload_measurement *measurement,
			 uint16_t battery_mv,
			 uint32_t counter,
			 uint8_t flags);

uint8_t payload_codec_checksum(const sensor_payload_t *payload);
bool payload_codec_is_valid(const sensor_payload_t *payload);
const uint8_t *payload_codec_bytes(const sensor_payload_t *payload);
size_t payload_codec_size(void);
