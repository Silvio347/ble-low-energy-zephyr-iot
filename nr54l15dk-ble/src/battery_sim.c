#include "battery_sim.h"

#include <zephyr/kernel.h>

static uint16_t battery_mv;
static uint32_t packet_count;

void battery_sim_init(void)
{
	battery_mv = CONFIG_LOWB_BATTERY_INITIAL_MV;
	packet_count = 0;
}

uint16_t battery_sim_next_mv(void)
{
	packet_count++;

	if ((packet_count % CONFIG_LOWB_BATTERY_DROP_EVERY_N_PACKETS) == 0U &&
	    battery_mv > CONFIG_LOWB_BATTERY_MIN_MV) {
		battery_mv--;
	}

	return battery_mv;
}
