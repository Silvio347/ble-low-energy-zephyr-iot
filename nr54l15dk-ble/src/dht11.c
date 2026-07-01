#include "dht11.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(dht11, CONFIG_LOG_DEFAULT_LEVEL);

#define DHT11_NODE DT_NODELABEL(dht11_sensor)

BUILD_ASSERT(DT_NODE_EXISTS(DHT11_NODE), "Missing dht11_sensor devicetree node");

static const struct gpio_dt_spec power_gpio =
	GPIO_DT_SPEC_GET(DHT11_NODE, power_gpios);
static const struct gpio_dt_spec data_gpio =
	GPIO_DT_SPEC_GET(DHT11_NODE, data_gpios);

static int wait_for_level(int expected_level, uint32_t timeout_us)
{
	uint32_t start_cycles = k_cycle_get_32();
	uint32_t timeout_cycles = k_us_to_cyc_ceil32(timeout_us);

	while ((uint32_t)(k_cycle_get_32() - start_cycles) <= timeout_cycles) {
		int value = gpio_pin_get_dt(&data_gpio);

		if (value < 0) {
			return value;
		}

		if (value == expected_level) {
			return 0;
		}

		k_busy_wait(2);
	}

	return -ETIMEDOUT;
}

static int read_bit(uint8_t *bit)
{
	int err;

	err = wait_for_level(1, 80);
	if (err) {
		return err;
	}

	k_busy_wait(40);

	err = gpio_pin_get_dt(&data_gpio);
	if (err < 0) {
		return err;
	}

	*bit = err ? 1U : 0U;

	err = wait_for_level(0, 90);
	if (err) {
		return err;
	}

	return 0;
}

int dht11_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&power_gpio)) {
		LOG_ERR("DHT11 power GPIO controller is not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&data_gpio)) {
		LOG_ERR("DHT11 data GPIO controller is not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&power_gpio, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}

	err = gpio_pin_configure_dt(&data_gpio, GPIO_INPUT);
	if (err) {
		return err;
	}

	return 0;
}

int dht11_power_on(void)
{
	return gpio_pin_set_dt(&power_gpio, 1);
}

int dht11_power_off(void)
{
	return gpio_pin_set_dt(&power_gpio, 0);
}

int dht11_read(struct dht11_sample *sample)
{
	uint8_t raw[5] = { 0 };
	int err;

	if (sample == NULL) {
		return -EINVAL;
	}

	err = gpio_pin_configure_dt(&data_gpio, GPIO_OUTPUT_ACTIVE);
	if (err) {
		return err;
	}

	gpio_pin_set_dt(&data_gpio, 0);
	k_msleep(20);
	gpio_pin_set_dt(&data_gpio, 1);
	k_busy_wait(30);

	err = gpio_pin_configure_dt(&data_gpio, GPIO_INPUT);
	if (err) {
		return err;
	}

	err = wait_for_level(0, 100);
	if (err) {
		return err;
	}

	err = wait_for_level(1, 100);
	if (err) {
		return err;
	}

	err = wait_for_level(0, 100);
	if (err) {
		return err;
	}

	for (size_t byte = 0; byte < ARRAY_SIZE(raw); byte++) {
		for (size_t bit_index = 0; bit_index < 8; bit_index++) {
			uint8_t bit;

			err = read_bit(&bit);
			if (err) {
				return err;
			}

			raw[byte] = (raw[byte] << 1) | bit;
		}
	}

	if (((raw[0] + raw[1] + raw[2] + raw[3]) & 0xff) != raw[4]) {
		return -EBADMSG;
	}

	sample->humidity_x10 = (uint16_t)raw[0] * 10U + raw[1];
	sample->temperature_x10 = (int16_t)raw[2] * 10 + raw[3];

	return 0;
}
