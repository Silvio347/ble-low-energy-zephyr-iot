# Agent Notes

## Scope

This repository contains ESP-IDF firmware for an ESP32 BLE-to-MQTT gateway. It complements a Nordic nRF54L15 Zephyr BLE sensor project but does not modify that Nordic firmware.

## Coding Conventions

- Use ESP-IDF APIs only. Do not add Arduino framework code.
- Keep all source, comments, logs, web UI labels, and documentation in English.
- Use `#pragma once` in headers.
- Keep modules focused:
  - `app_config`: default configuration and NVS persistence
  - `wifi_manager`: station and configuration AP startup
  - `config_portal`: HTTP UI, configuration save/reset, OTA upload route
  - `ota_update`: ESP-IDF OTA write and boot partition handling
  - `ble_scanner`: NimBLE passive scanning and manufacturer-data filtering
  - `payload_parser`: Nordic binary payload parsing and checksum validation
  - `packet_loss`: counter-based loss estimation
  - `mqtt_client_app`: MQTT connection and JSON publishing

## Build And Validation

Use:

```sh
idf.py set-target esp32
idf.py build
```

Flash and monitor with:

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

Run `idf.py fullclean` only when a clean rebuild is needed.

## Security Rules

- Do not commit `config/default_config.h`.
- Do not hardcode real Wi-Fi or MQTT credentials in committed source files.
- Keep placeholders in `config/default_config.example.h`.
- Use NVS for runtime configuration saved through the AP portal.

## OTA Notes

The included partition table assumes a 4 MB ESP32 flash layout with no `factory` partition. It contains `otadata`, `ota_0` at `0x20000`, and `ota_1`; each OTA app slot is `1920K`. Verify that the built application binary fits inside one OTA slot before flashing or uploading OTA images.
