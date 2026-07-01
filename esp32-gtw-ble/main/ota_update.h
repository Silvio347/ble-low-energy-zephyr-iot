#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_ota_ops.h"

typedef struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    size_t bytes_written;
    bool active;
} ota_update_context_t;

esp_err_t ota_update_begin(ota_update_context_t *ctx);
esp_err_t ota_update_write(ota_update_context_t *ctx, const void *data, size_t len);
esp_err_t ota_update_finish(ota_update_context_t *ctx);
void ota_update_abort(ota_update_context_t *ctx);
