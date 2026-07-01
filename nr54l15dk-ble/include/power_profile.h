#pragma once

#include <stdbool.h>
#include <stdint.h>

enum power_profile_id {
	POWER_PROFILE_DEMO,
	POWER_PROFILE_BALANCED,
	POWER_PROFILE_ECO,
};

struct power_profile {
	enum power_profile_id id;
	const char *name;
	uint32_t send_interval_ms;
	uint32_t adv_window_ms;
	uint32_t dht11_warmup_ms;
	bool verbose_logs;
	bool power_cycle_sensor;
	bool low_power_mode;
};

const struct power_profile *power_profile_get(void);
