# UltraOilPing – ESP32 ultrasonic oil tank level monitor with MQTT

<p align="center">
  <img src="assets/logo.png" alt="UltraOilPing logo" width="320">
</p>

[Deutsch](README.de.md)

ESP32 firmware for an ultrasonic sensor ([JSN-SR04T-V3.3](https://esphome.io/components/sensor/jsn_sr04t/) in Mode 2 / M2) that measures the distance to the oil surface and optionally publishes it via MQTT. Between cycles the device enters light sleep and powers the radios down.

## Hardware

### Microcontroller

- **[ESP32-C3 SuperMini](https://randomnerdtutorials.com/getting-started-esp32-c3-super-mini/)** (used in this project)
  - Compact ESP32-C3 board with USB-C
  - Pinout / specs: [Mischianti](https://mischianti.org/esp32-c3-super-mini-high-resolution-pinout-datasheet-and-specs/), [Last Minute Engineers](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/)

### Ultrasonic sensor

- **[JSN-SR04T-V3.3](https://esphome.io/components/sensor/jsn_sr04t/)** waterproof ultrasonic module (UART)
  - Datasheet (EN): [JSN-SR04T-V3.3 (translated PDF)](https://make.net.za/wp-content/datasheets/JSN-SR04T-V3.3%20Datasheet%20-%20Translated.pdf)
  - **Mode setup used here:** solder bridge **M2** on the module PCB (Mode 2 / ~120 kΩ) → UART-controlled output; trigger with `0x55` at 9600 baud
  - Mode overview: [Sigmanortec – JSN-SR04T v3.3](https://sigmanortec.ro/en/ultrasonic-sensor-module-jsn-sr04t-v33-waterproof-3-5v)

### Wiring (defaults in `config.h`)

- GPIO 4 ← sensor TX
- GPIO 3 → sensor RX

### First prototype

<p align="center">
  <img src="assets/prototype.png" alt="UltraOilPing first prototype" width="640">
</p>

ESP32-C3 SuperMini connected to a JSN-SR04T-V3.3 driver board (M2 soldered) and waterproof ultrasonic transducer (proof-of-concept wiring).

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
- Board: **ESP32-C3 SuperMini** (Arduino board: “ESP32C3 Dev Module”, enable **USB CDC On Boot**)
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
