#pragma once

#include <stdbool.h>

/*
 * Copy this file to config/default_config.h and replace the placeholders with
 * local private defaults. The private file is ignored by git.
 */

#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASSWORD ""

#define DEFAULT_MQTT_HOST "00000000000000000000000000000000.s1.eu.hivemq.cloud"
#define DEFAULT_MQTT_PORT 8883
#define DEFAULT_MQTT_USERNAME "hivemq-user"
#define DEFAULT_MQTT_PASSWORD "hivemq-password"
#define DEFAULT_MQTT_TLS true
#define DEFAULT_MQTT_CLIENT_ID "esp32-ble-gateway"
#define DEFAULT_MQTT_BASE_TOPIC "lowble"

#define DEFAULT_BLE_COMPANY_ID 0xFFFF
#define DEFAULT_BLE_TARGET_DEVICE_ID 347
