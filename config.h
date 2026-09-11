/**
 * config.h – Compile-time defaults for the oil tank ultrasonic sensor
 *
 * These values apply on first boot / as fallback until overridden and saved
 * via the serial configuration menu (persisted in ESP32 NVS).
 *
 * Do not commit real passwords to a public repository – prefer serial setup.
 */

#ifndef CONFIG_H
#define CONFIG_H

// -------------------- Hardware --------------------
#define SENSOR_RX_PIN     4       // GPIO <- sensor TX
#define SENSOR_TX_PIN     3       // GPIO -> sensor RX
#define SENSOR_BAUD       9600
#define SERIAL_BAUD       115200
#define SENSOR_UART_NUM   1       // HardwareSerial instance

// -------------------- Feature switches --------------------
#define CFG_WIFI_ENABLED  true
#define CFG_MQTT_ENABLED  true

// -------------------- WiFi --------------------
#define CFG_WIFI_SSID     ""
#define CFG_WIFI_PASSWORD ""

// -------------------- MQTT --------------------
#define CFG_MQTT_HOST      ""
#define CFG_MQTT_PORT      1883
#define CFG_MQTT_USER      ""
#define CFG_MQTT_PASSWORD  ""
#define CFG_MQTT_TOPIC     "oiltank/distance"
#define CFG_MQTT_CLIENT_ID "oiltank-esp"

// -------------------- Tank (optional) --------------------
// *_SET = false → parameter unset (no fill level / liters)
// *_SET = true  → use the matching value as default
#define CFG_DIST_EMPTY_SET  false
#define CFG_DIST_EMPTY_CM   100.0f   // sensor -> bottom (empty)

#define CFG_DIST_FULL_SET   false
#define CFG_DIST_FULL_CM    20.0f    // sensor -> oil surface (full)

#define CFG_TANK_LITERS_SET false
#define CFG_TANK_LITERS     1000.0f  // volume at 100 %

// -------------------- Operation --------------------
#define CFG_INTERVAL_SEC       30       // measure / publish interval (seconds)
#define CFG_WIFI_TIMEOUT_MS    15000
#define CFG_NET_RETRIES        3        // WiFi / MQTT connect attempts
#define CFG_NET_RETRY_DELAY_MS 1000     // pause between retries
#define CFG_SLEEP_SLICE_MS     1000     // light-sleep slice (timer + UART/serial poll)

// Serial line ending for menu / console output:
//   0 = auto (CRLF until first Enter, then learn CR / LF / CRLF)
//   1 = LF   (0x0A)
//   2 = CR   (0x0D)
//   3 = CRLF (0x0D 0x0A)
#define CFG_SERIAL_EOL      0

#endif  // CONFIG_H
