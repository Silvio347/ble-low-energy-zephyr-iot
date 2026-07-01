#include "config_portal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_portal_pages.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_update.h"

static const char *TAG = "config_portal";

static app_config_t s_initial_config;
static httpd_handle_t s_server;

static char hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void url_decode(char *value)
{
    char *src = value;
    char *dst = value;

    while (*src != '\0') {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            *dst++ = (char)((hex_digit(src[1]) << 4) | hex_digit(src[2]));
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void html_escape(const char *src, char *dst, size_t dst_len)
{
    size_t pos = 0;
    for (size_t i = 0; src != NULL && src[i] != '\0' && pos + 1 < dst_len; ++i) {
        const char *replacement = NULL;
        switch (src[i]) {
        case '&':
            replacement = "&amp;";
            break;
        case '<':
            replacement = "&lt;";
            break;
        case '>':
            replacement = "&gt;";
            break;
        case '"':
            replacement = "&quot;";
            break;
        default:
            break;
        }

        if (replacement != NULL) {
            size_t len = strlen(replacement);
            if (pos + len >= dst_len) {
                break;
            }
            memcpy(&dst[pos], replacement, len);
            pos += len;
        } else {
            dst[pos++] = src[i];
        }
    }
    dst[pos] = '\0';
}

static void copy_form_value(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }

    size_t len = strnlen(src, dst_len - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static bool get_param(const char *body, const char *key, char *value, size_t value_len)
{
    size_t key_len = strlen(key);
    const char *cursor = body;

    while (cursor != NULL && *cursor != '\0') {
        const char *pair_end = strchr(cursor, '&');
        size_t pair_len = pair_end == NULL ? strlen(cursor) : (size_t)(pair_end - cursor);
        if (pair_len > key_len && strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=') {
            size_t raw_len = pair_len - key_len - 1;
            if (raw_len >= value_len) {
                raw_len = value_len - 1;
            }
            memcpy(value, cursor + key_len + 1, raw_len);
            value[raw_len] = '\0';
            url_decode(value);
            return true;
        }
        cursor = pair_end == NULL ? NULL : pair_end + 1;
    }

    if (value_len > 0) {
        value[0] = '\0';
    }
    return false;
}

static uint16_t parse_u16(const char *value, uint16_t fallback)
{
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(value, &end, 0);
    if (end == value || parsed > UINT16_MAX) {
        return fallback;
    }
    return (uint16_t)parsed;
}

static esp_err_t send_text(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    char wifi_ssid[96];
    char mqtt_host[192];
    char mqtt_user[96];
    char mqtt_client_id[96];
    char mqtt_base[128];
    html_escape(s_initial_config.wifi_ssid, wifi_ssid, sizeof(wifi_ssid));
    html_escape(s_initial_config.mqtt_host, mqtt_host, sizeof(mqtt_host));
    html_escape(s_initial_config.mqtt_username, mqtt_user, sizeof(mqtt_user));
    html_escape(s_initial_config.mqtt_client_id, mqtt_client_id, sizeof(mqtt_client_id));
    html_escape(s_initial_config.mqtt_base_topic, mqtt_base, sizeof(mqtt_base));

    char *page = malloc(16384);
    if (page == NULL) {
        return ESP_ERR_NO_MEM;
    }

    snprintf(page, 16384,
             "%s"
             "<header class=\"top\"><div class=\"brand\"><div class=\"mark\">GW</div><div>"
             "<h1>ESP32 BLE MQTT Gateway</h1>"
             "<p class=\"sub\">Configuration portal for Wi-Fi, HiveMQ, BLE filters, and OTA updates.</p>"
             "</div></div><div class=\"status\"><span class=\"pill\">Config Portal</span>"
             "<span class=\"pill\">OTA Ready</span><span class=\"pill\">Reboots after save</span></div></header>"
             "<div class=\"grid\"><form method=\"post\" action=\"/save\">"
             "<section class=\"panel\"><div class=\"panel-head\"><div><h2>Wi-Fi</h2>"
             "<p class=\"hint\">Station credentials used during normal gateway operation.</p></div></div>"
             "<div class=\"panel-body\"><div class=\"field\"><label>SSID</label>"
             "<input name=\"wifi_ssid\" maxlength=\"32\" value=\"%s\" autocomplete=\"off\" required></div>"
             "<div class=\"field\"><label>Password</label>"
             "<input name=\"wifi_password\" maxlength=\"64\" type=\"password\" value=\"\" placeholder=\"Leave blank to keep current password\">"
             "<p class=\"help\">Open networks are allowed when this field is empty.</p></div></div></section>"
             "<section class=\"panel\"><div class=\"panel-head\"><div><h2>MQTT</h2>"
             "<p class=\"hint\">HiveMQ broker settings and telemetry topic base.</p></div></div>"
             "<div class=\"panel-body\"><div class=\"field\"><label>Host</label>"
             "<input name=\"mqtt_host\" maxlength=\"127\" value=\"%s\" autocomplete=\"off\" required></div>"
             "<div class=\"row\"><div class=\"field\"><label>Port</label>"
             "<input name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"%u\" required></div>"
             "<div class=\"field\"><label>TLS</label><div class=\"switch\"><span>Use secure MQTT</span>"
             "<input name=\"mqtt_tls\" type=\"checkbox\" value=\"1\" %s></div></div></div>"
             "<div class=\"row\"><div class=\"field\"><label>Username</label>"
             "<input name=\"mqtt_username\" maxlength=\"63\" value=\"%s\" autocomplete=\"off\"></div>"
             "<div class=\"field\"><label>Password</label>"
             "<input name=\"mqtt_password\" maxlength=\"127\" type=\"password\" value=\"\" placeholder=\"Leave blank to keep current password\"></div></div>"
             "<div class=\"row\"><div class=\"field\"><label>Client ID</label>"
             "<input name=\"mqtt_client_id\" maxlength=\"63\" value=\"%s\" autocomplete=\"off\" required></div>"
             "<div class=\"field\"><label>Base Topic</label>"
             "<input name=\"mqtt_base_topic\" maxlength=\"95\" value=\"%s\" autocomplete=\"off\" required>"
             "<p class=\"help\">Publishes as base_topic/device_id/telemetry.</p></div></div></div></section>"
             "<section class=\"panel\"><div class=\"panel-head\"><div><h2>BLE Filter</h2>"
             "<p class=\"hint\">Only matching Nordic manufacturer payloads are published.</p></div></div>"
             "<div class=\"panel-body\"><div class=\"row\"><div class=\"field\"><label>Company ID</label>"
             "<input name=\"ble_company_id\" value=\"0x%04x\" autocomplete=\"off\" required>"
             "<p class=\"help\">Accepts decimal or hexadecimal values.</p></div>"
             "<div class=\"field\"><label>Target Device ID</label>"
             "<input name=\"ble_target_device_id\" value=\"%u\" autocomplete=\"off\" required></div></div>"
             "<div class=\"actions\"><button type=\"submit\">Save and Reboot</button></div></div></section></form>"
             "<aside><section class=\"panel\"><div class=\"panel-head\"><div><h2>Firmware Update</h2>"
             "<p class=\"hint\">Upload an ESP-IDF application binary for OTA.</p></div></div>"
             "<div class=\"panel-body\"><div class=\"file\"><input id=\"fw\" type=\"file\" accept=\".bin\"></div>"
             "<div class=\"actions\"><button id=\"ota\" class=\"secondary\" type=\"button\">Upload OTA Image</button></div>"
             "<div id=\"status\" class=\"statusbox\">Waiting for firmware image.</div></div></section>"
             "<section class=\"panel danger-zone\"><div class=\"panel-head\"><div><h2>Reset</h2>"
             "<p class=\"hint\">Erase saved NVS configuration and restart the setup flow.</p></div></div>"
             "<div class=\"panel-body\"><form method=\"post\" action=\"/reset\">"
             "<button class=\"danger\" type=\"submit\">Reset Saved Configuration</button>"
             "<p class=\"help\">This does not erase the firmware image.</p></form></div></section>"
             "</aside></div>%s",
             CONFIG_PORTAL_PAGE_HEAD, wifi_ssid, mqtt_host, s_initial_config.mqtt_port,
             s_initial_config.mqtt_tls ? "checked" : "", mqtt_user, mqtt_client_id, mqtt_base,
             s_initial_config.ble_company_id, s_initial_config.ble_target_device_id,
             CONFIG_PORTAL_PAGE_TAIL);

    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_sendstr(req, page);
    free(page);
    return err;
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 2048) {
        return send_text(req, "Invalid configuration body size.");
    }

    char *body = calloc(1, req->content_len + 1);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int total_received = 0;
    while (total_received < req->content_len) {
        int received = httpd_req_recv(req, body + total_received, req->content_len - total_received);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            free(body);
            return send_text(req, "Failed to read configuration body.");
        }
        total_received += received;
    }
    body[total_received] = '\0';

    app_config_t config = s_initial_config;
    char value[160];
    get_param(body, "wifi_ssid", config.wifi_ssid, sizeof(config.wifi_ssid));
    if (get_param(body, "wifi_password", value, sizeof(value)) && value[0] != '\0') {
        copy_form_value(config.wifi_password, sizeof(config.wifi_password), value);
    }
    get_param(body, "mqtt_host", config.mqtt_host, sizeof(config.mqtt_host));
    get_param(body, "mqtt_port", value, sizeof(value));
    config.mqtt_port = parse_u16(value, 0);
    get_param(body, "mqtt_username", config.mqtt_username, sizeof(config.mqtt_username));
    if (get_param(body, "mqtt_password", value, sizeof(value)) && value[0] != '\0') {
        copy_form_value(config.mqtt_password, sizeof(config.mqtt_password), value);
    }
    config.mqtt_tls = get_param(body, "mqtt_tls", value, sizeof(value));
    get_param(body, "mqtt_client_id", config.mqtt_client_id, sizeof(config.mqtt_client_id));
    get_param(body, "mqtt_base_topic", config.mqtt_base_topic, sizeof(config.mqtt_base_topic));
    get_param(body, "ble_company_id", value, sizeof(value));
    config.ble_company_id = parse_u16(value, 0);
    get_param(body, "ble_target_device_id", value, sizeof(value));
    config.ble_target_device_id = parse_u16(value, 0);
    free(body);

    if (!app_config_is_valid(&config)) {
        return send_text(req, "Configuration is incomplete. Check Wi-Fi, MQTT host, port, client ID, and base topic.");
    }

    esp_err_t err = app_config_save(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save configuration: %s", esp_err_to_name(err));
        return send_text(req, "Failed to save configuration.");
    }

    httpd_resp_sendstr(req, "Configuration saved. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t reset_post_handler(httpd_req_t *req)
{
    esp_err_t err = app_config_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase configuration: %s", esp_err_to_name(err));
        return send_text(req, "Failed to erase configuration.");
    }
    httpd_resp_sendstr(req, "Configuration erased. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        return send_text(req, "OTA upload is empty.");
    }

    ota_update_context_t ota;
    esp_err_t err = ota_update_begin(&ota);
    if (err != ESP_OK) {
        return send_text(req, "OTA could not start. Check partition table.");
    }

    char buffer[2048];
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = remaining > (int)sizeof(buffer) ? (int)sizeof(buffer) : remaining;
        int received = httpd_req_recv(req, buffer, to_read);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            ota_update_abort(&ota);
            return send_text(req, "OTA upload failed while reading the request.");
        }

        err = ota_update_write(&ota, buffer, received);
        if (err != ESP_OK) {
            ota_update_abort(&ota);
            return send_text(req, "OTA write failed.");
        }
        remaining -= received;
    }

    err = ota_update_finish(&ota);
    if (err != ESP_OK) {
        return send_text(req, "OTA image validation failed.");
    }

    httpd_resp_sendstr(req, "OTA update applied. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t config_portal_start(const app_config_t *initial_config)
{
    if (initial_config != NULL) {
        memcpy(&s_initial_config, initial_config, sizeof(s_initial_config));
    } else {
        app_config_load_defaults(&s_initial_config);
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        return err;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post_handler};
    httpd_uri_t reset = {.uri = "/reset", .method = HTTP_POST, .handler = reset_post_handler};
    httpd_uri_t ota = {.uri = "/ota", .method = HTTP_POST, .handler = ota_post_handler};

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &save));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &reset));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &ota));

    ESP_LOGI(TAG, "Configuration portal is running");
    return ESP_OK;
}
