#include "power_profile.h"

#include <zephyr/sys/util_macro.h>

static const struct power_profile demo_profile = {
	.id = POWER_PROFILE_DEMO,
	.name = "demo",
	.send_interval_ms = 2000,
	.adv_window_ms = 1200,
	.dht11_warmup_ms = 1000,
	.verbose_logs = true,
	.power_cycle_sensor = false,
	.low_power_mode = false,
};

static const struct power_profile balanced_profile = {
	.id = POWER_PROFILE_BALANCED,
	.name = "balanced",
	.send_interval_ms = 10000,
	.adv_window_ms = 800,
	.dht11_warmup_ms = 1000,
	.verbose_logs = true,
	.power_cycle_sensor = true,
	.low_power_mode = false,
};

static const struct power_profile eco_profile = {
	.id = POWER_PROFILE_ECO,
	.name = "eco",
	.send_interval_ms = 60000,
	.adv_window_ms = 350,
	.dht11_warmup_ms = 1000,
	.verbose_logs = false,
	.power_cycle_sensor = true,
	.low_power_mode = true,
};

const struct power_profile *power_profile_get(void)
{
	if (IS_ENABLED(CONFIG_LOWB_PROFILE_DEMO)) {
		return &demo_profile;
	}

	if (IS_ENABLED(CONFIG_LOWB_PROFILE_BALANCED)) {
		return &balanced_profile;
	}

	return &eco_profile;
}
