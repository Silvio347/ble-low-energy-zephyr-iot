# LowBLE: nRF54L15 BLE Sensor + ESP32 BLE MQTT Gateway

LowBLE is a two-firmware IoT prototype composed of:

1. **Nordic nRF54L15 DK sensor node**  
   A Zephyr/NCS firmware that reads a GPIO-powered DHT11 temperature/humidity sensor and broadcasts compact telemetry through legacy BLE advertising manufacturer data.

2. **ESP32 BLE MQTT gateway**  
   An ESP-IDF firmware that scans the BLE advertisements from the Nordic node, validates the payload, estimates packet loss, and publishes JSON telemetry to an MQTT broker such as HiveMQ Cloud.

The data path is:

```text
DHT11 -> nRF54L15 DK -> BLE manufacturer data -> ESP32 gateway -> MQTT/HiveMQ
```

---

## Repository Structure

This repository contains both projects:

```text
.
|-- nrf54l15_sensor/
|   |-- CMakeLists.txt
|   |-- Kconfig
|   |-- prj.conf
|   |-- boards/
|   |   `-- nrf54l15dk_nrf54l15_cpuapp.overlay
|   |-- include/
|   |   |-- battery_sim.h
|   |   |-- ble_adv.h
|   |   |-- dht11.h
|   |   |-- payload_codec.h
|   |   `-- power_profile.h
|   `-- src/
|       |-- battery_sim.c
|       |-- ble_adv.c
|       |-- dht11.c
|       |-- main.c
|       |-- payload_codec.c
|       `-- power_profile.c
|
`-- esp32_gateway/
    |-- CMakeLists.txt
    |-- partitions.csv
    |-- config/
    |   |-- default_config.example.h
    |   `-- default_config.h        # local/private, ignored by git
    `-- main/
        `-- ...
```

> Adjust the folder names above if your local repository uses different names.

---

## Part 1: nRF54L15 Zephyr BLE Sensor

### Overview

The nRF54L15 firmware runs on the **Nordic nRF54L15 DK application core**. It reads a **DHT11** sensor, packs temperature, humidity, battery simulation, status flags, and a packet counter into a compact binary payload, then transmits it using **legacy BLE advertising manufacturer data**.

There is no GATT, pairing, bonding, or BLE Long Range support in this version.

### Hardware

- Board: Nordic nRF54L15 DK, application core.
- Sensor: DHT11.
- DHT11 VCC: GPIO configured in devicetree as `power-gpios`.
- DHT11 DATA: GPIO configured in devicetree as `data-gpios`.
- DHT11 GND: board GND.
- DHT11 supply: use VDDIO/3.3 V. Do not use 5 V.
- Pull-up: if the DHT11 module does not include a DATA pull-up, add a 4.7 kΩ or 10 kΩ pull-up to VDDIO.

Initial overlay pins:

```text
DHT11_POWER_GPIO: gpio1 10
DHT11_DATA_GPIO:  gpio1 11, with GPIO_PULL_UP
```

Confirm these pins against the connector used on the nRF54L15 DK before wiring the hardware.

### Devicetree Overlay Example

```dts
/ {
    dht11_sensor: dht11-sensor {
        compatible = "lowble,dht11-gpio";
        power-gpios = <&gpio1 10 GPIO_ACTIVE_HIGH>;
        data-gpios = <&gpio1 11 (GPIO_PULL_UP | GPIO_ACTIVE_HIGH)>;
    };
};
```

If using the custom compatible string above, keep the binding file in the project:

```text
dts/bindings/sensor/lowble,dht11-gpio.yaml
```

Example binding:

```yaml
description: GPIO based DHT11 sensor

compatible: "lowble,dht11-gpio"

properties:
  power-gpios:
    type: phandle-array
    required: true

  data-gpios:
    type: phandle-array
    required: true
```

### BLE Payload

The application payload is 14 bytes and is placed after the 2-byte Bluetooth company ID inside the BLE manufacturer data field.

```c
typedef struct __attribute__((packed)) {
    uint16_t device_id;
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint16_t battery_mv;
    uint32_t counter;
    uint8_t flags;
    uint8_t checksum;
} sensor_payload_t;
```

Multi-byte fields are transmitted in little-endian format.

Checksum:

```c
checksum = sum(payload_bytes_without_checksum) & 0xff;
```

Flags:

```c
#define FLAG_SENSOR_OK        BIT(0)
#define FLAG_BATTERY_SIM      BIT(1)
#define FLAG_LOW_POWER_MODE   BIT(2)
#define FLAG_DHT_ERROR        BIT(3)
```

The default `CONFIG_LOWB_COMPANY_ID` is `0xffff` and is intended only for prototyping. Replace it with an assigned Bluetooth SIG Company Identifier before any real product use.

### Operating Profiles

The operating profile is selected through Kconfig:

| Profile | Interval | Advertising Window | DHT11 Power |
|---|---:|---:|---|
| `CONFIG_LOWB_PROFILE_DEMO=y` | 2 s | 1200 ms | Kept powered |
| `CONFIG_LOWB_PROFILE_BALANCED=y` | 10 s | 800 ms | Power-cycled per sample |
| `CONFIG_LOWB_PROFILE_ECO=y` | 60 s | 350 ms | Power-cycled per sample |

The default in `prj.conf` is eco mode.

To switch profiles, edit `prj.conf` or use a configuration overlay containing exactly one `CONFIG_LOWB_PROFILE_*` symbol.

### Build and Flash

With Zephyr/NCS configured:

```sh
west build -b nrf54l15dk/nrf54l15/cpuapp .
west flash
```

If your environment still uses the older board name, try:

```sh
west build -b nrf54l15dk_nrf54l15_cpuapp .
```

### Operation Flow

1. `main.c` initializes the DHT11 module, simulated battery, Bluetooth stack, and the first cycle through `k_work_delayable`.
2. On each cycle, the firmware powers the DHT11 if the active profile requires power cycling.
3. It waits for sensor stabilization.
4. It reads the 40-bit DHT11 frame through GPIO.
5. It validates the DHT11 checksum.
6. It powers the DHT11 off when the profile uses sensor power cycling.
7. It builds `sensor_payload_t`.
8. It calculates the application checksum.
9. It starts legacy non-connectable BLE advertising.
10. It schedules the next cycle using the selected profile interval.
11. At the end of the advertising window, it stops advertising and increments the packet counter.

---

## Part 2: ESP32 BLE MQTT Gateway

### Overview

The ESP32 firmware scans BLE legacy advertisements from the Nordic node. It filters manufacturer data by Bluetooth company ID and device ID, validates the 14-byte application payload checksum, estimates packet loss from the payload counter, and publishes JSON telemetry to MQTT.

### Requirements

- ESP-IDF with ESP32 support.
- ESP32 module with enough flash for OTA.
- The included partition table targets 8 MB flash.
- HiveMQ broker or HiveMQ Cloud cluster.

### Build

```sh
idf.py set-target esp32
idf.py build
```

Flash and monitor:

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

Adjust the serial port for your host.

### Private Default Configuration

The gateway can load local defaults from:

```text
config/default_config.h
```

when no configuration has been saved to NVS. This file is intentionally ignored by git.

Create it from the committed example:

```sh
cp config/default_config.example.h config/default_config.h
```

Then edit `config/default_config.h` with private Wi-Fi, MQTT, and BLE defaults.

The project still compiles if this private file does not exist. In that case, blank fallback defaults force the device into configuration AP mode.

Example MQTT defaults:

```c
#define DEFAULT_MQTT_HOST "03e613ef35[...].s1.eu.hivemq.cloud"
#define DEFAULT_MQTT_PORT 8883
#define DEFAULT_MQTT_USERNAME "hivemq-user"
#define DEFAULT_MQTT_PASSWORD "hivemq-password"
#define DEFAULT_MQTT_TLS true
#define DEFAULT_MQTT_CLIENT_ID "esp32-ble-gateway"
#define DEFAULT_MQTT_BASE_TOPIC "lowble"

#define DEFAULT_BLE_COMPANY_ID 0xFFFF
#define DEFAULT_BLE_TARGET_DEVICE_ID 1
```

Do not commit real credentials.

### Configuration AP

If saved configuration is missing or invalid, or if station mode cannot connect within 30 seconds, the device starts an open AP:

```text
SSID: ESP32-Gateway-Setup
URL:  http://192.168.4.1/
```

The portal can edit:

- Wi-Fi SSID and password.
- MQTT host, port, username, password, TLS setting, client ID, and base topic.
- BLE company ID and target device ID.

Saving configuration stores it in NVS and reboots into normal gateway mode.

To erase saved configuration, either use the portal reset button or hold GPIO0 low during boot.

### OTA Updates

The configuration portal includes a firmware upload field.

Upload the application binary generated by ESP-IDF, usually:

```text
build/esp32_ble_mqtt_gateway.bin
```

The upload endpoint streams the binary through ESP-IDF OTA APIs, validates the image, sets the next boot partition, and reboots on success.

The included `partitions.csv` provides:

```text
factory
ota_0
ota_1
```

If the ESP32 board has 4 MB flash, reduce the app partition sizes and verify that the built binary still fits.

---

## Shared BLE Payload Contract

The nRF54L15 sensor and the ESP32 gateway must use the same payload contract.

BLE manufacturer data format:

```text
[company_id: 2 bytes little-endian][sensor_payload_t: 14 bytes]
```

Payload:

```c
typedef struct __attribute__((packed)) {
    uint16_t device_id;
    int16_t temperature_x10;
    uint16_t humidity_x10;
    uint16_t battery_mv;
    uint32_t counter;
    uint8_t flags;
    uint8_t checksum;
} sensor_payload_t;
```

Checksum:

```c
checksum = sum(payload_bytes_without_checksum) & 0xff;
```

The ESP32 gateway validates this checksum before publishing telemetry.

---

## MQTT Output

The gateway publishes telemetry to:

```text
{base_topic}/{device_id}/telemetry
```

With the default base topic, device `1` publishes to:

```text
lowble/1/telemetry
```

Example JSON:

```json
{
  "device_id": 1,
  "temperature_c": 24.7,
  "humidity_percent": 53.2,
  "battery_mv": 3010,
  "counter": 42,
  "flags": 1,
  "sensor_ok": true,
  "dht_error": false,
  "low_power_mode": false,
  "battery_simulated": false,
  "rssi": -63,
  "packet_loss_total": 0,
  "packet_loss_delta": 0,
  "checksum_valid": true,
  "timestamp_ms": 123456
}
```

HiveMQ Cloud normally uses TLS on port `8883` and requires username/password authentication. TLS mode uses the ESP-IDF certificate bundle.

---

## End-to-End Test Checklist

### nRF54L15 Sensor

- Build succeeds with `west build`.
- Flash succeeds with `west flash`.
- DHT11 is powered from VDDIO/3.3 V.
- DATA has pull-up to VDDIO.
- BLE advertising is active.
- Company ID matches gateway configuration.
- Device ID matches gateway target device ID.

### ESP32 Gateway

- Build succeeds with `idf.py build`.
- Wi-Fi configuration is saved or default config is present.
- MQTT host, credentials, port, TLS mode, client ID, and base topic are valid.
- Gateway is scanning BLE advertisements.
- Gateway receives manufacturer data from the Nordic node.
- Gateway publishes to `{base_topic}/{device_id}/telemetry`.

---

## Troubleshooting

### Gateway always starts configuration portal

Check that Wi-Fi SSID, MQTT host, port, client ID, and base topic are set.

### MQTT does not connect

Verify:

- HiveMQ host.
- Username.
- Password.
- TLS mode.
- Port. HiveMQ Cloud typically uses port `8883` for MQTT over TLS.

### No BLE samples are published

Verify:

- Nordic node is running.
- Nordic node uses legacy advertising manufacturer data.
- Company ID byte order matches the gateway filter.
- Target device ID matches the Nordic payload.
- Gateway scan is active.

### `checksum_valid` is false

Compare the Nordic payload bytes with the checksum algorithm documented above.

### OTA fails to start

Verify that the custom partition table is active and contains OTA partitions.

### ESP32 image does not fit

Increase flash size or adjust `partitions.csv`.

### nRF54L15 DHT11 does not read correctly

Check:

- DHT11 VCC is 3.3 V, not 5 V.
- DATA pull-up exists.
- `power-gpios` and `data-gpios` match the real wiring.
- The devicetree binding exists if using `compatible = "lowble,dht11-gpio"`.

---

## Notes

- This project uses BLE advertising as a compact telemetry transport.
- The gateway does not pair with the sensor; it only scans advertisements.
- MQTT credentials should stay outside version control.
- `0xffff` is a prototype company ID and should not be used in production.
