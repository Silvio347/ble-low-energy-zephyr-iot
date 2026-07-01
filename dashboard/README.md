# LowBLE MQTT Dashboard

Responsive local dashboard for monitoring MQTT messages from the BLE gateway.

## Run

```bash
python3 server.py
```

Open:

```text
http://127.0.0.1:8080
```

The server subscribes to `lowble` and `lowble/#` on HiveMQ Cloud using TLS on port `8883`. The interface receives events through SSE, so the browser does not need to communicate with MQTT directly.

## Configuration

Local credentials are stored in `config.local.json`, which is included in `.gitignore`. To change them without editing the file:

```bash
MQTT_HOST=host MQTT_USERNAME=user MQTT_PASSWORD=pass python3 server.py
```

Main fields expected in the payload:

```json
{
  "device_id": 347,
  "temperature_c": 0,
  "humidity_percent": 0,
  "battery_mv": 2986,
  "counter": 224,
  "flags": 10,
  "sensor_ok": false,
  "dht_error": true,
  "low_power_mode": false,
  "battery_simulated": true,
  "rssi": -36,
  "packet_loss_total": 3,
  "packet_loss_delta": 0,
  "checksum_valid": true,
  "timestamp_ms": 146282
}
```