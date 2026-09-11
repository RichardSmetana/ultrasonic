# UltraOilPing – ESP32 ultrasonic oil tank level monitor with MQTT

<p align="center">
  <img src="assets/logo.png" alt="UltraOilPing logo" width="320">
</p>

[Deutsch](README.de.md)

ESP32 firmware for an ultrasonic sensor (JSN-SR04T mode 3) that measures the distance to the oil surface and optionally publishes it via MQTT. Between cycles the device enters light sleep and powers the radios down.

## Hardware

- ESP32 (tested with ESP32-C3)
- JSN-SR04T-V3.3 in mode 3 (120 kΩ), UART 9600 baud
- Default pins (change in `config.h`):
  - GPIO 4 ← sensor TX
  - GPIO 3 → sensor RX

### First prototype

<p align="center">
  <img src="assets/prototype.png" alt="UltraOilPing first prototype" width="640">
</p>

ESP32-C3 connected to a JSN-SR04T driver board and waterproof ultrasonic transducer (proof-of-concept wiring).

## Features

- Periodic distance measurement
- Optional fill level (%) and liters when empty/full distance (and tank volume) are set
- MQTT: distance only, as a number (e.g. `45.2`)
- **Per measurement:** WiFi + MQTT connect → publish → disconnect / power off radios
- WiFi and MQTT connect with **3 retries** each
- **Light sleep** between cycles (wake on timer or serial); unused radios off
- Serial configuration menu (any key in the Serial Monitor)
- Serial line ending CR / LF / CRLF learned from Enter and stored in NVS
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

## Operation cycle

1. Measure distance (and print level/liters if calibrated)
2. If WiFi and MQTT are enabled: connect (up to 3 attempts each) → publish → tear down network
3. Enter light sleep until the measure interval elapses **or** serial input arrives
4. Serial wake opens the configuration menu

Light sleep is used instead of deep sleep so the device can wake from the timer **and** from serial (UART wakeup + short sleep slices for USB-CDC Serial Monitor).

## Configuration

### `config.h` (compile-time defaults)

| Macro | Meaning |
|-------|---------|
| `CFG_WIFI_ENABLED` / `CFG_MQTT_ENABLED` | WiFi / MQTT on by default |
| `CFG_WIFI_SSID` / `CFG_WIFI_PASSWORD` | WLAN credentials |
| `CFG_MQTT_*` | Broker, port, user, password, topic, client ID |
| `CFG_DIST_EMPTY_SET` / `CFG_DIST_FULL_SET` | Enable empty/full calibration |
| `CFG_TANK_LITERS_SET` | Enable tank volume |
| `CFG_INTERVAL_SEC` | Measure / sleep interval |
| `CFG_NET_RETRIES` | WiFi / MQTT connect attempts (default 3) |
| `CFG_NET_RETRY_DELAY_MS` | Delay between retries |
| `CFG_SLEEP_SLICE_MS` | Light-sleep slice length (serial poll / UART wake) |
| `CFG_SERIAL_EOL` | Console line ending: 0=auto, 1=LF, 2=CR, 3=CRLF |

**Note:** Do not commit real passwords to a public repo — use the serial menu instead.

### Serial menu

| Key | Setting |
|-----|---------|
| `0` / `3` | WiFi / MQTT enabled (0/1) |
| `1`–`2` | WiFi SSID / password |
| `4`–`9` | MQTT parameters |
| `a`–`c` | Distance empty/full, tank volume (`-` clears) |
| `d` | Measure interval |
| `e` | Serial line ending (0=auto, 1=LF, 2=CR, 3=CRLF) |
| `s` | Status / configuration |
| `t` | Test measurement |
| `u` | EOL consistency self-test |
| `w` | Test WiFi + MQTT (retry 3×, then power off) |
| `x` / `q` | Save & exit / quit without saving |

Enter is accepted as CR (`0x0D`), LF (`0x0A`), or CRLF. The detected style is used for menu output and can be saved.

## MQTT

- Topic: as configured (default `oiltank/distance`)
- Payload: distance in cm, one decimal place, e.g. `123.4`
- Retain: enabled
- Connection is established only for each publish, then closed

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

### Logo

The UltraOilPing logo (`assets/logo.png`) is licensed under the [GNU General Public License v3.0](LICENSE).

It was generated with ChatGPT (OpenAI).
