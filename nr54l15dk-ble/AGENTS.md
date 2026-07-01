# AGENTS.md

## Project

This repository contains Zephyr firmware for a Nordic nRF54L15 DK BLE sensor node. Keep the implementation focused on the Nordic/Zephyr side unless the user explicitly asks for the ESP32-CAM gateway.

## Build Target

Primary target:

```sh
west build -b nrf54l15dk/nrf54l15/cpuapp .
```

Fallback target for older environments:

```sh
west build -b nrf54l15dk_nrf54l15_cpuapp .
```

Use `west flash` only when the user asks to flash hardware.

## Coding Rules

- Keep code modular: DHT11, payload encoding, BLE advertising, power profiles, battery simulation, and application flow should stay in separate modules.
- Do not hardcode DHT11 GPIO pins in C files. Use the board devicetree overlay.
- Preserve the 14-byte `sensor_payload_t` binary layout unless the user explicitly changes the protocol.
- Multi-byte payload fields must remain little-endian.
- BLE V1 is legacy non-connectable advertising with manufacturer data only. Do not add GATT, pairing, bonding, or BLE Long Range unless requested.
- Eco mode should avoid permanent busy loops and should not keep advertising active between send windows.
- Prefer Zephyr APIs and patterns over bare-metal-style code.

## Validation

After firmware changes, run:

```sh
git diff --check
```

When Zephyr/NCS is available, also run the primary `west build` command. If `west` or the SDK is unavailable, state that clearly in the final response.

## Hardware Notes

- Use VDDIO/3.3 V for the DHT11. Do not use 5 V.
- If the DHT11 board lacks a DATA pull-up, use 4.7k or 10k to VDDIO.
- Confirm the overlay pins against the actual nRF54L15 DK connector before wiring.
