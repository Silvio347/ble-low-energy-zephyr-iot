#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "battery_sim.h"
#include "ble_adv.h"
#include "dht11.h"
#include "payload_codec.h"
#include "power_profile.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static const struct power_profile *profile;
static struct k_work_delayable cycle_work;
static struct k_work_delayable adv_stop_work;
static uint32_t packet_counter;

static void schedule_next_cycle(void)
{
	k_work_schedule(&cycle_work, K_MSEC(profile->send_interval_ms));
}

static void adv_stop_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int err = ble_adv_stop();

	if (err == 0) {
		packet_counter++;
	}
}

static int read_sensor_for_profile(struct dht11_sample *sample)
{
	int err;

	if (profile->power_cycle_sensor) {
		err = dht11_power_on();
		if (err) {
			return err;
		}

		k_msleep(profile->dht11_warmup_ms);
	}

	err = dht11_read(sample);

	if (profile->power_cycle_sensor) {
		int power_err = dht11_power_off();

		if (power_err && err == 0) {
			err = power_err;
		}
	}

	return err;
}

static void cycle_handler(struct k_work *work)
{
	struct dht11_sample dht_sample;
	struct payload_measurement measurement = { 0 };
	sensor_payload_t payload;
	uint8_t flags = FLAG_BATTERY_SIM;
	uint16_t battery_mv;
	int err;

	ARG_UNUSED(work);

	err = read_sensor_for_profile(&dht_sample);
	if (err == 0) {
		flags |= FLAG_SENSOR_OK;
		measurement.temperature_x10 = dht_sample.temperature_x10;
		measurement.humidity_x10 = dht_sample.humidity_x10;
	} else {
		flags |= FLAG_DHT_ERROR;
		LOG_WRN("DHT11 read failed: %d", err);
	}

	if (profile->low_power_mode) {
		flags |= FLAG_LOW_POWER_MODE;
	}

	battery_mv = battery_sim_next_mv();

	payload_codec_build(&payload, &measurement, battery_mv, packet_counter, flags);

	if (profile->verbose_logs) {
		LOG_INF("payload counter=%u temp_x10=%d hum_x10=%u batt=%u flags=0x%02x checksum=0x%02x",
			packet_counter,
			measurement.temperature_x10,
			measurement.humidity_x10,
			battery_mv,
			flags,
			payload.checksum);
	}

	err = ble_adv_start(&payload);
	if (err) {
		LOG_ERR("Could not start advertising: %d", err);
		schedule_next_cycle();
		return;
	}

	k_work_schedule(&adv_stop_work, K_MSEC(profile->adv_window_ms));
	schedule_next_cycle();
}

int main(void)
{
	int err;

	profile = power_profile_get();

	LOG_INF("LowBLE nRF54L15 sensor starting, profile=%s", profile->name);

	err = dht11_init();
	if (err) {
		LOG_ERR("DHT11 init failed: %d", err);
		return err;
	}

	if (!profile->power_cycle_sensor) {
		err = dht11_power_on();
		if (err) {
			LOG_ERR("DHT11 power on failed: %d", err);
			return err;
		}

		k_msleep(profile->dht11_warmup_ms);
	}

	battery_sim_init();

	err = ble_adv_init();
	if (err) {
		return err;
	}

	k_work_init_delayable(&cycle_work, cycle_handler);
	k_work_init_delayable(&adv_stop_work, adv_stop_handler);
	k_work_schedule(&cycle_work, K_NO_WAIT);

	while (true) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
