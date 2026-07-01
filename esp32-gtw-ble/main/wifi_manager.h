#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "esp_err.h"

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start_sta(const app_config_t *config);
esp_err_t wifi_manager_start_config_ap(void);
bool wifi_manager_wait_connected(uint32_t timeout_ms);
