#include "ota_update.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "ota_update";

esp_err_t ota_update_begin(ota_update_context_t *ctx)
{
    if (ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->partition = esp_ota_get_next_update_partition(NULL);
    if (ctx->partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition is available");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Writing OTA image to partition %s", ctx->partition->label);
    esp_err_t err = esp_ota_begin(ctx->partition, OTA_WITH_SEQUENTIAL_WRITES, &ctx->handle);
    if (err == ESP_OK) {
        ctx->active = true;
    }
    return err;
}

esp_err_t ota_update_write(ota_update_context_t *ctx, const void *data, size_t len)
{
    if (ctx == NULL || !ctx->active || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_ota_write(ctx->handle, data, len);
    if (err == ESP_OK) {
        ctx->bytes_written += len;
    }
    return err;
}

esp_err_t ota_update_finish(ota_update_context_t *ctx)
{
    if (ctx == NULL || !ctx->active) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_ota_end(ctx->handle);
    ctx->active = false;
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "OTA image received, bytes=%u", (unsigned)ctx->bytes_written);
    return esp_ota_set_boot_partition(ctx->partition);
}

void ota_update_abort(ota_update_context_t *ctx)
{
    if (ctx != NULL && ctx->active) {
        esp_ota_abort(ctx->handle);
        ctx->active = false;
    }
}
