#pragma once

#include "app_config.h"
#include "esp_err.h"
#include "packet_loss.h"
#include "payload_parser.h"

esp_err_t mqtt_client_app_start(const app_config_t *config);
bool mqtt_client_app_is_connected(void);
esp_err_t mqtt_client_app_publish_sensor(const app_config_t *config,
                                         const sensor_payload_t *payload,
                                         int8_t rssi,
                                         const packet_loss_result_t *packet_loss);
