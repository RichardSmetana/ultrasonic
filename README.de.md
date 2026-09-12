# UltraOilPing – ESP32 ultrasonic oil tank level monitor with MQTT

<p align="center">
  <img src="assets/ultraoilping-logo.png" alt="UltraOilPing Logo" width="320">
</p>

[English](README.md)

ESP32-Firmware für einen Ultraschallsensor ([JSN-SR04T-V3.3](https://esphome.io/components/sensor/jsn_sr04t/) in Mode 2 / M2), die den Abstand zum Ölspiegel misst und optional per MQTT sendet. Zwischen den Zyklen geht das Gerät in den Light Sleep und schaltet die Funkmodule ab.

## Hardware

> **Warnung — Netzspannung**  
> Am WX-DC12003-Netzteil liegt auf der Primärseite **gefährliche Netzspannung** an. Pads mit der Aufschrift **DC310V** können bei Betrieb an 230 V AC **mehr als 300 V DC** führen. Nach dem Abschalten kann in den Kondensatoren noch Hochspannung gespeichert sein. Nur qualifiziert arbeiten; vor Berühren freischalten und entladen. Diese Projektdokumentation dient technischen / Bildungszwecken.

### Mikrocontroller

- **[ESP32-C3 SuperMini](https://randomnerdtutorials.com/getting-started-esp32-c3-super-mini/)** (in diesem Projekt verwendet)
  - Kompaktes ESP32-C3-Board mit USB-C
  - Pinout / Specs: [Mischianti](https://mischianti.org/esp32-c3-super-mini-high-resolution-pinout-datasheet-and-specs/), [Last Minute Engineers](https://lastminuteengineers.com/esp32-c3-super-mini-pinout-reference/)
  - **Hinweis zur Reichweite:** Die integrierte PCB-Antenne ist schwach; eine übliche Modifikation (Leiterbahn trennen und externe Antenne anschließen) kann die Wi‑Fi-Reichweite deutlich erhöhen — siehe [ESP32-C3 SuperMini Antenna Modification (Peter Neufeld)](https://peterneufeld.wordpress.com/2025/03/04/esp32-c3-supermini-antenna-modification/)

<p align="center">
  <img src="assets/esp32-c3-supermini-top.jpeg" alt="ESP32-C3 SuperMini Oberseite" width="320">
  &nbsp;
  <img src="assets/esp32-c3-supermini-pinout.jpeg" alt="ESP32-C3 SuperMini Pin-Beschriftung" width="320">
</p>

### Ultraschallsensor

- **[JSN-SR04T-V3.3](https://esphome.io/components/sensor/jsn_sr04t/)** wasserdichtes Ultraschallmodul (UART)
  - Datenblatt (EN): [JSN-SR04T-V3.3 (übersetztes PDF)](https://make.net.za/wp-content/datasheets/JSN-SR04T-V3.3%20Datasheet%20-%20Translated.pdf)
  - **Hier verwendete Mode-Einstellung:** Lötbrücke **M2** auf der Modul-Platine (Mode 2 / ca. 120 kΩ) → UART-gesteuerte Ausgabe; Trigger mit `0x55` bei 9600 Baud
  - Mode-Übersicht: [Sigmanortec – JSN-SR04T v3.3](https://sigmanortec.ro/en/ultrasonic-sensor-module-jsn-sr04t-v33-waterproof-3-5v)

<p align="center">
  <img src="assets/jsn-sr04t-v3.3-top-m2.jpeg" alt="JSN-SR04T-V3.3 Oberseite mit M2-Lötbrücke" width="320">
  &nbsp;
  <img src="assets/jsn-sr04t-v3.3-bottom-m2.jpeg" alt="JSN-SR04T-V3.3 Unterseite mit M2-Lötbrücke" width="320">
</p>

### Netzteil — WX-DC12003 AC/DC-Modul

Kompaktes **isoliertes** AC/DC-Schaltnetzteil zur Versorgung aus dem Stromnetz.

- Weiter Eingangsbereich: ca. **50–277 V AC** oder **70–390 V DC**
- Häufig in mehreren Ausgangsvarianten erhältlich (dieser Aufbau nutzt ein Modul der Klasse **~12 V**, z. B. WX-DC12003)
- Anschluss (Unterseite): **L / N** = AC-Eingang; **VCC (+) / GND (−)** = DC-Ausgang
- Typische Referenzinfos: Datenblätter, Reverse-Engineering-Schaltpläne, Bauteil-/PCB-Identifikation, Pinout, Fotos und Messungen

<p align="center">
  <img src="assets/psu-wx-dc12003-top.jpeg" alt="WX-DC12003 Netzteil Oberseite" width="280">
  &nbsp;
  <img src="assets/psu-wx-dc12003-bottom.jpeg" alt="WX-DC12003 Netzteil Unterseite" width="280">
</p>

> Auf der Primärseite dieses Moduls liegt **gefährliche Netzspannung** an.  
> Die Pads **DC310V** können bei 230 V AC **mehr als 300 V DC** führen. Nach dem Abschalten kann in den Kondensatoren noch Hochspannung gespeichert sein.

### Aufbauhinweise

- **Isolierung:** Alle Platinen (ESP32-C3 SuperMini, JSN-SR04T-V3.3 und WX-DC12003) sind auf der **Unterseite** mit **beidseitig klebendem Schaumstoffband** isoliert, damit Lötstellen und Pads nicht gegen die Montagefläche oder untereinander kurzschließen.
- Primärseite (Netz) räumlich von der Niedervoltseite (ESP32 / Sensor) getrennt halten.
- Netzteil im Betrieb nicht berühren; nach dem Abschalten Kondensatoren als geladen behandeln, bis sicher entladen.

### Verdrahtung (Defaults in `config.h`)

- GPIO 4 ← Sensor TX *(siehe `SENSOR_RX_PIN` / `SENSOR_TX_PIN` in `config.h`)*
- GPIO 3 → Sensor RX
- GPIO 8 → Onboard-LED (active low) — Publish-Feedback
- GPIO 9 → BOOT-Taster (active low) — Wake + Messen/Publish

### Prototypen

**v1 – Breadboard / lose Verdrahtung**

<p align="center">
  <img src="assets/prototype-breadboard-v1.png" alt="UltraOilPing Breadboard-Prototyp v1" width="640">
</p>

ESP32-C3 SuperMini verdrahtet mit JSN-SR04T-V3.3 (M2 gelötet) und wasserdichtem Ultraschallwandler.

**v2 – Lochraster-Aufbau** (ESP32-C3 SuperMini mit Antennen-Draht-Mod, JSN-SR04T-V3.3 mit M2, Netzteil WX-DC12003; Platinenunterseiten mit beidseitig klebendem Schaumstoffband isoliert)

<p align="center">
  <img src="assets/prototype-perfboard-top.jpeg" alt="UltraOilPing Lochraster-Prototyp Oberseite" width="480">
  &nbsp;
  <img src="assets/prototype-perfboard-bottom.jpeg" alt="UltraOilPing Lochraster-Prototyp Unterseite" width="480">
</p>

## Funktionen

- Periodische Distanzmessung in **Millimetern**
- Optional Füllstand (%) und Liter, wenn Distanz leer/voll (mm) und Volumen gesetzt sind
- Optionale **Maximaldistanz** (mm): größere Werte = Fehler (kein Publish, LED 5×)
- MQTT: nur die Distanz als Ganzzahl in mm (z. B. `1234`)
- **Pro Messung:** WiFi + MQTT verbinden → senden → trennen / Funk aus
- WiFi und MQTT jeweils mit **3 Retries**
- LED (GPIO 8): **1× blinken** = Publish OK, **5× blinken** = Publish-/Sensor-/Max-Distanz-Fehler
- BOOT-Taster (GPIO 9): aus dem Sleep wecken und Mess- + Publish-Zyklus ausführen
- **Light Sleep** zwischen den Zyklen (Wake per Timer, Serial oder Taster); ungenutzte Radios aus
- Serielles Konfigurationsmenü (beliebiges Zeichen im Serial Monitor)
- Zeilenende CR / LF / CRLF wird von Enter gelernt und im NVS gespeichert
- Persistenz im ESP32-NVS
- Compile-Zeit-Defaults in `config.h`

## Voraussetzungen

- Arduino IDE oder PlatformIO
- Board: **ESP32-C3 SuperMini** (Arduino-Board: „ESP32C3 Dev Module“, **USB CDC On Boot** aktivieren)
- Bibliothek: [PubSubClient](https://github.com/knolleary/pubsubclient) (Nick O’Leary)

## Schnellstart

1. Repository klonen
2. `config.h` anpassen (SSID, Broker, Pins, WiFi/MQTT ein/aus, …)
3. Sketch `ultrasonic.ino` flashen
4. Serial Monitor öffnen (115200 Baud)
5. Beliebiges Zeichen tippen → Menü; mit `x` speichern

Ohne Einträge in `config.h` kann alles auch nur über das serielle Menü eingerichtet werden.

## Betriebszyklus

1. Distanz messen (und Füllstand/Liter ausgeben, falls kalibriert)
2. Wenn WiFi und MQTT aktiv: verbinden (je bis 3 Versuche) → publish → Netzwerk abbauen
3. Light Sleep bis zum Messintervall **oder** bis Serieneingabe / BOOT-Taster
4. Serial-Wake öffnet das Konfigurationsmenü; Taster-Wake startet einen Mess- + Publish-Zyklus; nach dem Menü (und nach Reset) hält ein **Idle-Timeout** das Gerät wach, bevor wieder Stromsparmodus folgt

Light Sleep statt Deep Sleep, damit Wake per Timer **und** Serial möglich ist (UART-Wakeup + kurze Sleep-Slices für USB-CDC Serial Monitor).

## Konfiguration

### `config.h` (Defaults beim Compile)

| Makro | Bedeutung |
|-------|-----------|
| `CFG_WIFI_ENABLED` / `CFG_MQTT_ENABLED` | WiFi bzw. MQTT standardmäßig an/aus |
| `CFG_WIFI_SSID` / `CFG_WIFI_PASSWORD` | WLAN-Zugangsdaten |
| `CFG_MQTT_*` | Broker, Port, User, Passwort, Topic, Client-ID |
| `CFG_DIST_EMPTY_SET` / `CFG_DIST_FULL_SET` | Kalibrierung aktivieren (mm) |
| `CFG_DIST_MAX_SET` / `CFG_DIST_MAX_MM` | Maximaldistanz (mm); darüber = Fehler |
| `CFG_TANK_LITERS_SET` | Tankvolumen aktivieren |
| `CFG_INTERVAL_SEC` | Mess-/Sleep-Intervall |
| `CFG_IDLE_TIMEOUT_SEC` | Wachzeit nach Reset / Serial vor Stromsparmodus |
| `CFG_NET_RETRIES` | Verbindungsversuche WiFi/MQTT (Standard 3) |
| `CFG_NET_RETRY_DELAY_MS` | Pause zwischen Retries |
| `CFG_SLEEP_SLICE_MS` | Light-Sleep-Slice (Serial-Poll / UART-Wake) |
| `CFG_SERIAL_EOL` | Konsolen-Zeilenende: 0=auto, 1=LF, 2=CR, 3=CRLF |

**Hinweis:** Für ein öffentliches Repo keine echten Passwörter in `config.h` committen – lieber das serielle Menü nutzen.

### Serielles Menü

| Taste | Einstellung |
|-------|-------------|
| `0` / `3` | WiFi / MQTT aktiv (0/1) |
| `1`–`2` | WiFi SSID / Passwort |
| `4`–`9` | MQTT-Parameter |
| `a`–`c` | Distanz leer/voll (mm), Tankvolumen (`-` löscht) |
| `g` | Distanz max (mm; darüber = Fehler, `-` löscht) |
| `d` | Messintervall |
| `f` | Idle-Timeout vor Stromsparmodus (Sekunden) |
| `e` | Serielles Zeilenende (0=auto, 1=LF, 2=CR, 3=CRLF) |
| `s` | Status / Konfiguration |
| `t` | Testmessung |
| `u` | EOL-Konsistenz-Self-Test |
| `w` | WiFi + MQTT testen (3× Retry, danach Funk aus) |
| `x` / `q` | Speichern & Exit / Abbruch |

Enter wird als CR (`0x0D`), LF (`0x0A`) oder CRLF akzeptiert. Der erkannte Stil gilt für die Menüausgabe und kann gespeichert werden.

## MQTT

- Topic: laut Konfiguration (Default `oiltank/distance`)
- Payload: Distanz in **mm** als Ganzzahl, z. B. `1234`
- Retain: aktiv
- Verbindung nur für jedes Publish, danach wieder getrennt
- Wenn Distanz &gt; konfiguriertem Maximum: Fehler (kein MQTT-Publish, LED blinkt 5×)

## Lizenz

Dieses Projekt steht unter der [GNU General Public License v3.0](LICENSE).

### Logo

Das UltraOilPing-Logo (`assets/ultraoilping-logo.png`) steht unter der [GNU General Public License v3.0](LICENSE).

Es wurde mit ChatGPT (OpenAI) generiert.
