#pragma once

#include <stdint.h>

struct dht11_sample {
	int16_t temperature_x10;
	uint16_t humidity_x10;
};

int dht11_init(void);
int dht11_power_on(void);
int dht11_power_off(void);
int dht11_read(struct dht11_sample *sample);
