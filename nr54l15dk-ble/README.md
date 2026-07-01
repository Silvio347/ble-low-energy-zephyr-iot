# LowBLE nRF54L15 Zephyr BLE Sensor

Zephyr firmware for a low-power BLE sensor node running on a Nordic nRF54L15 DK. Version 1 reads a GPIO-powered DHT11 sensor, packs temperature and humidity into a compact binary payload, and transmits it through legacy BLE advertising manufacturer data.

## Hardware

- Board: Nordic nRF54L15 DK, application core.
- Sensor: DHT11.
- DHT11 VCC: GPIO configured in devicetree as `power-gpios`.
- DHT11 DATA: GPIO configured in devicetree as `data-gpios`.
- DHT11 GND: board GND.
- DHT11 supply: use VDDIO/3.3 V. Do not use 5 V.
- Pull-up: if the DHT11 module does not include a DATA pull-up, add a 4.7k or 10k pull-up to VDDIO.

The initial overlay uses:

- `DHT11_POWER_GPIO`: `gpio1 10`
- `DHT11_DATA_GPIO`: `gpio1 11`, with `GPIO_PULL_UP`

Confirm these pins against the connector you are using on the nRF54L15 DK before wiring the hardware.

## Project Layout

```text
.
|-- CMakeLists.txt
|-- Kconfig
|-- prj.conf
|-- boards/
|   `-- nrf54l15dk_nrf54l15_cpuapp.overlay
|-- include/
|   |-- battery_sim.h
|   |-- ble_adv.h
|   |-- dht11.h
|   |-- payload_codec.h
|   `-- power_profile.h
`-- src/
    |-- battery_sim.c
    |-- ble_adv.c
    |-- dht11.c
    |-- main.c
    |-- payload_codec.c
    `-- power_profile.c
```

## BLE Payload

The application payload is 14 bytes and is placed after the 2-byte company ID inside the BLE manufacturer data field.

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

Multi-byte fields are transmitted in little-endian format. The checksum is the sum of all previous payload bytes, truncated to 8 bits:

```c
checksum = sum(payload_bytes_without_checksum) & 0xff;
```

Flags:

- `FLAG_SENSOR_OK`: valid DHT11 reading.
- `FLAG_BATTERY_SIM`: simulated battery value.
- `FLAG_LOW_POWER_MODE`: eco profile is active.
- `FLAG_DHT_ERROR`: DHT11 timeout or checksum failure.

The default `CONFIG_LOWB_COMPANY_ID` is `0xffff` and is only meant for prototyping. Replace it with an assigned Bluetooth SIG Company Identifier before any real product use.

## Profiles

The operating profile is selected through Kconfig:

- `CONFIG_LOWB_PROFILE_DEMO=y`: transmit every 2 s, 1200 ms BLE advertising window, DHT11 kept powered.
- `CONFIG_LOWB_PROFILE_BALANCED=y`: transmit every 10 s, 800 ms BLE advertising window, DHT11 power-cycled per sample.
- `CONFIG_LOWB_PROFILE_ECO=y`: transmit every 60 s, 350 ms BLE advertising window, DHT11 power-cycled per sample.

The default in `prj.conf` is eco mode.

## Build

With Zephyr/NCS configured:

```sh
west build -b nrf54l15dk/nrf54l15/cpuapp .
west flash.
```

If your environment still uses the older board name, try:

```sh
west build -b nrf54l15dk_nrf54l15_cpuapp .
```

To switch profiles, edit `prj.conf` or use a configuration overlay containing exactly one `CONFIG_LOWB_PROFILE_*` symbol.

## Operation Flow

1. `main.c` initializes the DHT11 module, simulated battery, Bluetooth stack, and the first cycle through `k_work_delayable`.
2. On each cycle, the firmware powers the DHT11 if the active profile requires power cycling.
3. It waits for sensor stabilization, reads the 40-bit DHT11 frame through GPIO, and validates the DHT11 checksum.
4. It powers the DHT11 off when the profile uses sensor power cycling.
5. It builds `sensor_payload_t`, calculates the application checksum, and starts legacy non-connectable BLE advertising.
6. It schedules the next cycle using the selected profile interval.
7. At the end of the advertising window, it stops advertising and increments the packet counter.

There is no GATT, pairing, bonding, or BLE Long Range support in this V1.
