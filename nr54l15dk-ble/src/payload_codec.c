#include "payload_codec.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

BUILD_ASSERT(sizeof(sensor_payload_t) == 14, "sensor_payload_t must be 14 bytes");

uint8_t payload_codec_checksum(const sensor_payload_t *payload)
{
	const uint8_t *bytes = (const uint8_t *)payload;
	uint8_t sum = 0U;

	for (size_t i = 0; i < offsetof(sensor_payload_t, checksum); i++) {
		sum += bytes[i];
	}

	return sum;
}

bool payload_codec_is_valid(const sensor_payload_t *payload)
{
	if (payload == NULL) {
		return false;
	}

	return payload->checksum == payload_codec_checksum(payload);
}

void payload_codec_build(sensor_payload_t *payload,
			 const struct payload_measurement *measurement,
			 uint16_t battery_mv,
			 uint32_t counter,
			 uint8_t flags)
{
	memset(payload, 0, sizeof(*payload));

	payload->device_id = sys_cpu_to_le16(CONFIG_LOWB_DEVICE_ID);

	if (measurement != NULL) {
		payload->temperature_x10 = sys_cpu_to_le16((uint16_t)measurement->temperature_x10);
		payload->humidity_x10 = sys_cpu_to_le16(measurement->humidity_x10);
	}

	payload->battery_mv = sys_cpu_to_le16(battery_mv);
	payload->counter = sys_cpu_to_le32(counter);
	payload->flags = flags;
	payload->checksum = payload_codec_checksum(payload);
}

const uint8_t *payload_codec_bytes(const sensor_payload_t *payload)
{
	return (const uint8_t *)payload;
}

size_t payload_codec_size(void)
{
	return sizeof(sensor_payload_t);
}
