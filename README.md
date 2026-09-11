# Oil tank level (JSN-SR04T + MQTT)

[Deutsch](README.de.md)

ESP32 firmware for an ultrasonic sensor (JSN-SR04T mode 3) that measures the distance to the oil surface and optionally publishes it via MQTT.

## Hardware

- ESP32 (tested with ESP32-C3)
- JSN-SR04T-V3.3 in mode 3 (120 kΩ), UART 9600 baud
- Default pins (change in `config.h`):
  - GPIO 4 ← sensor TX
  - GPIO 3 → sensor RX

## Features

- Periodic distance measurement
- Optional fill level (%) and liters when empty/full distance (and tank volume) are set
- MQTT: distance only, as a number (e.g. `45.2`)
- Serial configuration menu (any character in the Serial Monitor)
- Persistence in ESP32 NVS
- Compile-time defaults in `config.h`

## Requirements

- Arduino IDE or PlatformIO
- Board: ESP32
- Library: [PubSubClient](https://github.com/knolleary/pubsubclient) (Nick O’Leary)

## Quick start

1. Clone the repository
2. Edit `config.h` (SSID, broker, pins, WiFi/MQTT on/off, …)
3. Flash `ultrasonic.ino`
4. Open the Serial Monitor (115200 baud)
5. Press any key → menu; save with `x`

Everything can also be configured only via the serial menu (no secrets required in `config.h`).

## Configuration

### `config.h` (compile-time defaults)

| Macro | Meaning |
|-------|---------|
| `CFG_WIFI_ENABLED` / `CFG_MQTT_ENABLED` | WiFi / MQTT on by default |
| `CFG_WIFI_SSID` / `CFG_WIFI_PASSWORD` | WLAN credentials |
| `CFG_MQTT_*` | Broker, port, user, password, topic, client ID |
| `CFG_DIST_EMPTY_SET` / `CFG_DIST_FULL_SET` | Enable empty/full calibration |
| `CFG_TANK_LITERS_SET` | Enable tank volume |
| `CFG_INTERVAL_SEC` | Measure interval |

**Note:** Do not commit real passwords to a public repo — use the serial menu instead.

### Serial menu

| Key | Setting |
|-----|---------|
| `0` / `3` | WiFi / MQTT enabled (0/1) |
| `1`–`2` | WiFi SSID / password |
| `4`–`9` | MQTT parameters |
| `a`–`c` | Distance empty/full, tank volume (`-` clears) |
| `d` | Measure interval |
| `s` / `t` / `w` | Status / test / reconnect |
| `x` / `q` | Save & exit / quit without saving |

## MQTT

- Topic: as configured (default `oiltank/distance`)
- Payload: distance in cm, one decimal place, e.g. `123.4`
- Retain: enabled

## License

[GNU General Public License v3.0](LICENSE)
