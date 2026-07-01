#include "ble_adv.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ble_adv, CONFIG_LOG_DEFAULT_LEVEL);

#define MFG_DATA_SIZE (sizeof(uint16_t) + sizeof(sensor_payload_t))

static uint8_t mfg_data[MFG_DATA_SIZE];
static bool advertising;

int ble_adv_init(void)
{
	int err = bt_enable(NULL);

	if (err) {
		LOG_ERR("Bluetooth init failed: %d", err);
		return err;
	}

	LOG_INF("Bluetooth initialized");
	return 0;
}

int ble_adv_start(const sensor_payload_t *payload)
{
	struct bt_data ad[] = {
		BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
	};
	int err;

	if (payload == NULL) {
		return -EINVAL;
	}

	if (advertising) {
		err = ble_adv_stop();
		if (err) {
			return err;
		}
	}

	sys_put_le16(CONFIG_LOWB_COMPANY_ID, mfg_data);
	memcpy(&mfg_data[sizeof(uint16_t)], payload_codec_bytes(payload), payload_codec_size());

	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("BLE advertising start failed: %d", err);
		return err;
	}

	advertising = true;
	return 0;
}

int ble_adv_stop(void)
{
	int err;

	if (!advertising) {
		return 0;
	}

	err = bt_le_adv_stop();
	if (err) {
		LOG_ERR("BLE advertising stop failed: %d", err);
		return err;
	}

	advertising = false;
	return 0;
}
