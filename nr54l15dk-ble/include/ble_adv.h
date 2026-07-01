#pragma once

#include "payload_codec.h"

int ble_adv_init(void);
int ble_adv_start(const sensor_payload_t *payload);
int ble_adv_stop(void);
