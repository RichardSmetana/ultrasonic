# Öltank-Pegel (JSN-SR04T + MQTT)

[English](README.md)

ESP32-Firmware für einen Ultraschallsensor (JSN-SR04T Mode 3), die den Abstand zum Ölspiegel misst und optional per MQTT sendet.

## Hardware

- ESP32 (getestet mit ESP32-C3)
- JSN-SR04T-V3.3 im Mode 3 (120 kΩ), UART 9600 Baud
- Standard-Pins (änderbar in `config.h`):
  - GPIO 4 ← Sensor TX
  - GPIO 3 → Sensor RX

## Funktionen

- Periodische Distanzmessung
- Optional Füllstand (%) und Liter, wenn Distanz leer/voll (und Volumen) gesetzt sind
- MQTT: nur die Distanz als Zahl (z. B. `45.2`)
- Serielles Konfigurationsmenü (beliebiges Zeichen im Serial Monitor)
- Persistenz im ESP32-NVS
- Compile-Zeit-Defaults in `config.h`

## Voraussetzungen

- Arduino IDE oder PlatformIO
- Board: ESP32
- Bibliothek: [PubSubClient](https://github.com/knolleary/pubsubclient) (Nick O’Leary)

## Schnellstart

1. Repository klonen
2. `config.h` anpassen (SSID, Broker, Pins, WiFi/MQTT ein/aus, …)
3. Sketch `ultrasonic.ino` flashen
4. Serial Monitor öffnen (115200 Baud)
5. Beliebiges Zeichen tippen → Menü; mit `x` speichern

Ohne Einträge in `config.h` kann alles auch nur über das serielle Menü eingerichtet werden.

## Konfiguration

### `config.h` (Defaults beim Compile)

| Makro | Bedeutung |
|-------|-----------|
| `CFG_WIFI_ENABLED` / `CFG_MQTT_ENABLED` | WiFi bzw. MQTT standardmäßig an/aus |
| `CFG_WIFI_SSID` / `CFG_WIFI_PASSWORD` | WLAN-Zugangsdaten |
| `CFG_MQTT_*` | Broker, Port, User, Passwort, Topic, Client-ID |
| `CFG_DIST_EMPTY_SET` / `CFG_DIST_FULL_SET` | Kalibrierung aktivieren |
| `CFG_TANK_LITERS_SET` | Tankvolumen aktivieren |
| `CFG_INTERVAL_SEC` | Messintervall |

**Hinweis:** Für ein öffentliches Repo keine echten Passwörter in `config.h` committen – lieber das serielle Menü nutzen.

### Serielles Menü

| Taste | Einstellung |
|-------|-------------|
| `0` / `3` | WiFi / MQTT aktiv (0/1) |
| `1`–`2` | WiFi SSID / Passwort |
| `4`–`9` | MQTT-Parameter |
| `a`–`c` | Distanz leer/voll, Tankvolumen (`-` löscht) |
| `d` | Messintervall |
| `s` / `t` / `w` | Status / Test / neu verbinden |
| `x` / `q` | Speichern & Exit / Abbruch |

## MQTT

- Topic: laut Konfiguration (Default `oiltank/distance`)
- Payload: Distanz in cm, eine Dezimalstelle, z. B. `123.4`
- Retain: aktiv

## Lizenz

[GNU General Public License v3.0](LICENSE)
