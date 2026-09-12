#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include <stdarg.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include "config.h"

HardwareSerial SensorSerial(SENSOR_UART_NUM);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

// Runtime config (defaults from config.h, overlay from NVS)
struct Config {
  bool wifiEnabled;
  bool mqttEnabled;
  char wifiSsid[33];
  char wifiPass[65];
  char mqttHost[64];
  uint16_t mqttPort;
  char mqttUser[33];
  char mqttPass[65];
  char mqttTopic[64];
  char mqttClientId[33];
  float distEmptyMm;
  float distFullMm;
  float distMaxMm;    // error if measured distance > this (unset = no limit)
  float tankLiters;
  uint16_t intervalSec;
  uint16_t idleTimeoutSec;  // grace before power-save after reset / serial
  uint8_t serialEol;  // SerialEol value, persisted
} cfg;

bool wifiOk = false;
bool mqttOk = false;
bool pendingIdleGrace = true;  // true after reset; also after serial menu

// Line ending for console output (CR / LF / CRLF); synced with cfg.serialEol
enum SerialEol : uint8_t { EOL_UNKNOWN = 0, EOL_LF = 1, EOL_CR = 2, EOL_CRLF = 3 };
SerialEol serialEol = EOL_UNKNOWN;

enum class WakeSource : uint8_t { Timer = 0, SerialIn = 1, Button = 2 };
enum class IdleEvent : uint8_t { Done = 0, SerialIn = 1, Button = 2 };

// -------------------- LED / BOOT button --------------------
void ledSet(bool on) {
#if CFG_LED_ACTIVE_LOW
  digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

void ledBlink(uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    ledSet(true);
    delay(CFG_LED_BLINK_MS);
    ledSet(false);
    if (i + 1 < times) {
      delay(CFG_LED_BLINK_MS);
    }
  }
}

bool bootButtonPressed() {
  return digitalRead(BOOT_BTN_PIN) == LOW;
}

void waitBootButtonRelease() {
  uint32_t start = millis();
  while (bootButtonPressed() && (millis() - start) < 3000UL) {
    delay(10);
  }
  delay(40);
}

void setupButtonAndLed() {
  pinMode(LED_PIN, OUTPUT);
  ledSet(false);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
}

void enableButtonSleepWakeup() {
  gpio_wakeup_enable((gpio_num_t)BOOT_BTN_PIN, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
}

// -------------------- Serial helpers --------------------
void flushSerialInput() {
  while (Serial.available()) {
    Serial.read();
  }
}

const char *serialEolName(SerialEol mode) {
  switch (mode) {
    case EOL_LF: return "LF (0x0A)";
    case EOL_CR: return "CR (0x0D)";
    case EOL_CRLF: return "CRLF (0x0D 0x0A)";
    case EOL_UNKNOWN:
    default: return "auto (learn from Enter)";
  }
}

// Single source of truth for EOL bytes (also used by self-test)
size_t serialEolBytes(SerialEol mode, uint8_t *out, size_t maxOut) {
  if (maxOut == 0 || out == nullptr) {
    return 0;
  }
  switch (mode) {
    case EOL_CR:
      out[0] = 0x0D;
      return 1;
    case EOL_LF:
      out[0] = 0x0A;
      return 1;
    case EOL_CRLF:
    case EOL_UNKNOWN:
    default:
      if (maxOut < 2) {
        out[0] = 0x0D;
        return 1;
      }
      out[0] = 0x0D;
      out[1] = 0x0A;
      return 2;
  }
}

void applySerialEol(SerialEol mode) {
  serialEol = mode;
  cfg.serialEol = (uint8_t)mode;
}

void serialWriteEol() {
  uint8_t bytes[2];
  size_t n = serialEolBytes(serialEol, bytes, sizeof(bytes));
  for (size_t i = 0; i < n; i++) {
    Serial.write(bytes[i]);
  }
}

void serialPrintln() {
  serialWriteEol();
}

void serialPrintln(const char *msg) {
  Serial.print(msg);
  serialWriteEol();
}

void serialPrintln(const String &msg) {
  Serial.print(msg);
  serialWriteEol();
}

void serialPrintln(const IPAddress &ip) {
  Serial.print(ip);
  serialWriteEol();
}

// Like printf, but every '\n' in the result uses the configured CR/LF style
void serialPrintf(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  for (const char *p = buf; *p; ++p) {
    if (*p == '\n') {
      serialWriteEol();
    } else if (*p == '\r') {
      // Ignore literal CR in format strings; EOL helper owns line breaks
      continue;
    } else {
      Serial.write((uint8_t)*p);
    }
  }
}

bool selfTestSerialEol() {
  uint8_t buf[4];
  size_t n;

  n = serialEolBytes(EOL_LF, buf, sizeof(buf));
  if (n != 1 || buf[0] != 0x0A) {
    return false;
  }

  n = serialEolBytes(EOL_CR, buf, sizeof(buf));
  if (n != 1 || buf[0] != 0x0D) {
    return false;
  }

  n = serialEolBytes(EOL_CRLF, buf, sizeof(buf));
  if (n != 2 || buf[0] != 0x0D || buf[1] != 0x0A) {
    return false;
  }

  n = serialEolBytes(EOL_UNKNOWN, buf, sizeof(buf));
  if (n != 2 || buf[0] != 0x0D || buf[1] != 0x0A) {
    return false;
  }

  // Active mode must match cfg
  if ((uint8_t)serialEol != cfg.serialEol) {
    return false;
  }

  return true;
}

// Remember and echo the EOL sequence that was actually received
void absorbAndEchoEol(char first) {
  delay(5);  // allow second byte of CRLF/LFCR to arrive
  char second = 0;
  if (Serial.available()) {
    char next = (char)Serial.peek();
    if ((first == '\r' && next == '\n') || (first == '\n' && next == '\r')) {
      second = (char)Serial.read();
    }
  }

  SerialEol mode;
  if ((first == '\r' && second == '\n') || (first == '\n' && second == '\r')) {
    mode = EOL_CRLF;
  } else if (first == '\r') {
    mode = EOL_CR;
  } else {
    mode = EOL_LF;
  }

  applySerialEol(mode);
  serialWriteEol();
}

// If the wake byte is CR/LF, learn EOL before flushing the rest
bool consumeSerialWake() {
  if (!Serial.available()) {
    return false;
  }
  char c = (char)Serial.read();
  if (c == '\r' || c == '\n') {
    absorbAndEchoEol(c);
  }
  flushSerialInput();
  return true;
}

String readLine(const char *prompt) {
  Serial.print(prompt);
  flushSerialInput();
  String line;
  while (true) {
    while (Serial.available()) {
      char c = (char)Serial.read();

      // End of line: CR (0x0D) and/or LF (0x0A)
      if (c == '\r' || c == '\n') {
        absorbAndEchoEol(c);
        return line;
      }

      if (c == 0x08 || c == 0x7F) {
        if (line.length() > 0) {
          line.remove(line.length() - 1);
          Serial.print("\b \b");
        }
        continue;
      }
      if (c >= 32 && c < 127) {
        line += c;
        Serial.print(c);
      }
    }
    delay(5);
  }
}

int readInt(const char *prompt, int current) {
  String hint = String(prompt) + " [" + String(current) + "]: ";
  String line = readLine(hint.c_str());
  if (line.length() == 0) {
    return current;
  }
  return line.toInt();
}

bool readBool01(const char *prompt, bool current) {
  String hint = String(prompt) + " [" + String(current ? 1 : 0) + "] (0/1): ";
  String line = readLine(hint.c_str());
  if (line.length() == 0) {
    return current;
  }
  return line.toInt() != 0;
}

bool readOptionalFloat(const char *prompt, float &value) {
  String hint = String(prompt) + " [";
  if (isnan(value)) {
    hint += "unset";
  } else {
    hint += String(value, 1);
  }
  hint += "]: ";
  String line = readLine(hint.c_str());
  if (line.length() == 0) {
    return false;
  }
  if (line == "-") {
    value = NAN;
    return true;
  }
  value = line.toFloat();
  return true;
}

void readString(const char *prompt, char *dest, size_t destSize, const char *current) {
  String hint = String(prompt) + " [";
  if (strlen(current) == 0) {
    hint += "empty";
  } else {
    hint += current;
  }
  hint += "]: ";
  String line = readLine(hint.c_str());
  if (line.length() == 0) {
    return;
  }
  if (line == "-") {
    dest[0] = '\0';
    return;
  }
  strncpy(dest, line.c_str(), destSize - 1);
  dest[destSize - 1] = '\0';
}

static void copyStr(char *dest, size_t destSize, const char *src) {
  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}

// -------------------- Persistence --------------------
void applyCompileDefaults() {
  cfg.wifiEnabled = CFG_WIFI_ENABLED;
  cfg.mqttEnabled = CFG_MQTT_ENABLED;
  copyStr(cfg.wifiSsid, sizeof(cfg.wifiSsid), CFG_WIFI_SSID);
  copyStr(cfg.wifiPass, sizeof(cfg.wifiPass), CFG_WIFI_PASSWORD);
  copyStr(cfg.mqttHost, sizeof(cfg.mqttHost), CFG_MQTT_HOST);
  cfg.mqttPort = CFG_MQTT_PORT;
  copyStr(cfg.mqttUser, sizeof(cfg.mqttUser), CFG_MQTT_USER);
  copyStr(cfg.mqttPass, sizeof(cfg.mqttPass), CFG_MQTT_PASSWORD);
  copyStr(cfg.mqttTopic, sizeof(cfg.mqttTopic), CFG_MQTT_TOPIC);
  copyStr(cfg.mqttClientId, sizeof(cfg.mqttClientId), CFG_MQTT_CLIENT_ID);
  cfg.distEmptyMm = CFG_DIST_EMPTY_SET ? CFG_DIST_EMPTY_MM : NAN;
  cfg.distFullMm = CFG_DIST_FULL_SET ? CFG_DIST_FULL_MM : NAN;
  cfg.distMaxMm = CFG_DIST_MAX_SET ? CFG_DIST_MAX_MM : NAN;
  cfg.tankLiters = CFG_TANK_LITERS_SET ? CFG_TANK_LITERS : NAN;
  cfg.intervalSec = CFG_INTERVAL_SEC;
  cfg.idleTimeoutSec = CFG_IDLE_TIMEOUT_SEC;
  cfg.serialEol = (uint8_t)CFG_SERIAL_EOL;
  if (cfg.serialEol > (uint8_t)EOL_CRLF) {
    cfg.serialEol = (uint8_t)EOL_UNKNOWN;
  }
  applySerialEol((SerialEol)cfg.serialEol);
}

void loadConfig() {
  applyCompileDefaults();

  prefs.begin("oeltank", true);
  if (prefs.isKey("wifiEn")) {
    cfg.wifiEnabled = prefs.getBool("wifiEn", cfg.wifiEnabled);
  }
  if (prefs.isKey("mqttEn")) {
    cfg.mqttEnabled = prefs.getBool("mqttEn", cfg.mqttEnabled);
  }
  prefs.getString("wifiSsid", cfg.wifiSsid, sizeof(cfg.wifiSsid));
  prefs.getString("wifiPass", cfg.wifiPass, sizeof(cfg.wifiPass));
  prefs.getString("mqttHost", cfg.mqttHost, sizeof(cfg.mqttHost));
  cfg.mqttPort = prefs.getUShort("mqttPort", cfg.mqttPort);
  prefs.getString("mqttUser", cfg.mqttUser, sizeof(cfg.mqttUser));
  prefs.getString("mqttPass", cfg.mqttPass, sizeof(cfg.mqttPass));
  prefs.getString("mqttTopic", cfg.mqttTopic, sizeof(cfg.mqttTopic));
  prefs.getString("mqttClient", cfg.mqttClientId, sizeof(cfg.mqttClientId));
  if (prefs.isKey("distEmptyMm")) {
    cfg.distEmptyMm = prefs.getFloat("distEmptyMm", NAN);
  } else if (prefs.isKey("distEmpty")) {
    // migrate legacy cm → mm
    cfg.distEmptyMm = prefs.getFloat("distEmpty", NAN) * 10.0f;
  }
  if (prefs.isKey("distFullMm")) {
    cfg.distFullMm = prefs.getFloat("distFullMm", NAN);
  } else if (prefs.isKey("distFull")) {
    cfg.distFullMm = prefs.getFloat("distFull", NAN) * 10.0f;
  }
  if (prefs.isKey("distMaxMm")) {
    cfg.distMaxMm = prefs.getFloat("distMaxMm", NAN);
  }
  if (prefs.isKey("tankLiters")) {
    cfg.tankLiters = prefs.getFloat("tankLiters", NAN);
  }
  if (prefs.isKey("clrEmpty") && prefs.getBool("clrEmpty", false)) {
    cfg.distEmptyMm = NAN;
  }
  if (prefs.isKey("clrFull") && prefs.getBool("clrFull", false)) {
    cfg.distFullMm = NAN;
  }
  if (prefs.isKey("clrMax") && prefs.getBool("clrMax", false)) {
    cfg.distMaxMm = NAN;
  }
  if (prefs.isKey("clrLiters") && prefs.getBool("clrLiters", false)) {
    cfg.tankLiters = NAN;
  }
  cfg.intervalSec = prefs.getUShort("interval", cfg.intervalSec);
  cfg.idleTimeoutSec = prefs.getUShort("idleTo", cfg.idleTimeoutSec);
  if (prefs.isKey("serialEol")) {
    uint8_t eol = prefs.getUChar("serialEol", cfg.serialEol);
    if (eol <= (uint8_t)EOL_CRLF) {
      cfg.serialEol = eol;
    }
  }
  prefs.end();

  applySerialEol((SerialEol)cfg.serialEol);
}

void saveConfig() {
  prefs.begin("oeltank", false);
  prefs.putBool("wifiEn", cfg.wifiEnabled);
  prefs.putBool("mqttEn", cfg.mqttEnabled);
  prefs.putString("wifiSsid", cfg.wifiSsid);
  prefs.putString("wifiPass", cfg.wifiPass);
  prefs.putString("mqttHost", cfg.mqttHost);
  prefs.putUShort("mqttPort", cfg.mqttPort);
  prefs.putString("mqttUser", cfg.mqttUser);
  prefs.putString("mqttPass", cfg.mqttPass);
  prefs.putString("mqttTopic", cfg.mqttTopic);
  prefs.putString("mqttClient", cfg.mqttClientId);

  if (isnan(cfg.distEmptyMm)) {
    prefs.remove("distEmptyMm");
    prefs.remove("distEmpty");
    prefs.putBool("clrEmpty", true);
  } else {
    prefs.putFloat("distEmptyMm", cfg.distEmptyMm);
    prefs.remove("distEmpty");
    prefs.putBool("clrEmpty", false);
  }
  if (isnan(cfg.distFullMm)) {
    prefs.remove("distFullMm");
    prefs.remove("distFull");
    prefs.putBool("clrFull", true);
  } else {
    prefs.putFloat("distFullMm", cfg.distFullMm);
    prefs.remove("distFull");
    prefs.putBool("clrFull", false);
  }
  if (isnan(cfg.distMaxMm)) {
    prefs.remove("distMaxMm");
    prefs.putBool("clrMax", true);
  } else {
    prefs.putFloat("distMaxMm", cfg.distMaxMm);
    prefs.putBool("clrMax", false);
  }
  if (isnan(cfg.tankLiters)) {
    prefs.remove("tankLiters");
    prefs.putBool("clrLiters", true);
  } else {
    prefs.putFloat("tankLiters", cfg.tankLiters);
    prefs.putBool("clrLiters", false);
  }

  prefs.putUShort("interval", cfg.intervalSec);
  prefs.putUShort("idleTo", cfg.idleTimeoutSec);
  cfg.serialEol = (uint8_t)serialEol;
  prefs.putUChar("serialEol", cfg.serialEol);
  prefs.end();
  serialPrintln("Configuration saved.");
}

void printOptionalMm(const char *label, float value) {
  Serial.print(label);
  if (isnan(value)) {
    serialPrintln("(unset)");
  } else {
    serialPrintf("%.0f mm\n", value);
  }
}

void printOptionalLiters(const char *label, float value) {
  Serial.print(label);
  if (isnan(value)) {
    serialPrintln("(unset)");
  } else {
    serialPrintf("%.0f liters\n", value);
  }
}

void printConfig() {
  serialPrintln();
  serialPrintln("---------- Current configuration ----------");
  serialPrintf("  WiFi enabled:   %s\n", cfg.wifiEnabled ? "yes" : "no");
  serialPrintf("  MQTT enabled:   %s\n", cfg.mqttEnabled ? "yes" : "no");
  serialPrintf("  WiFi SSID:      %s\n", cfg.wifiSsid[0] ? cfg.wifiSsid : "(unset)");
  serialPrintf("  WiFi password:  %s\n", cfg.wifiPass[0] ? "********" : "(unset)");
  serialPrintf("  MQTT broker:    %s\n", cfg.mqttHost[0] ? cfg.mqttHost : "(unset)");
  serialPrintf("  MQTT port:      %u\n", cfg.mqttPort);
  serialPrintf("  MQTT user:      %s\n", cfg.mqttUser[0] ? cfg.mqttUser : "(empty)");
  serialPrintf("  MQTT password:  %s\n", cfg.mqttPass[0] ? "********" : "(empty)");
  serialPrintf("  MQTT topic:     %s\n", cfg.mqttTopic);
  serialPrintf("  MQTT client ID: %s\n", cfg.mqttClientId);
  printOptionalMm("  Dist empty:     ", cfg.distEmptyMm);
  printOptionalMm("  Dist full:      ", cfg.distFullMm);
  printOptionalMm("  Dist max:       ", cfg.distMaxMm);
  printOptionalLiters("  Tank volume:    ", cfg.tankLiters);
  serialPrintf("  Interval:       %u s\n", cfg.intervalSec);
  serialPrintf("  Idle timeout:   %u s (before power-save)\n", cfg.idleTimeoutSec);
  serialPrintf("  Serial EOL:     %s\n", serialEolName(serialEol));
  serialPrintln("-------------------------------------------");
}

// -------------------- WiFi / MQTT / power --------------------
void tearDownNetwork() {
  if (mqtt.connected()) {
    mqtt.disconnect();
  }
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
  }
  esp_wifi_stop();
  wifiOk = false;
  mqttOk = false;
}

void powerDownUnused() {
  tearDownNetwork();
#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  btStop();
#endif
}

bool connectWifiOnce(uint32_t timeoutMs = CFG_WIFI_TIMEOUT_MS) {
  if (!cfg.wifiEnabled) {
    serialPrintln("WiFi: disabled.");
    wifiOk = false;
    return false;
  }
  if (cfg.wifiSsid[0] == '\0') {
    serialPrintln("WiFi: no SSID configured.");
    wifiOk = false;
    return false;
  }

  serialPrintf("WiFi: connecting to '%s' ...\n", cfg.wifiSsid);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  serialPrintln();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi: connected, IP ");
    serialPrintln(WiFi.localIP());
    wifiOk = true;
    return true;
  }

  serialPrintln("WiFi: connection failed.");
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  wifiOk = false;
  return false;
}

bool connectWifiWithRetry() {
  for (int attempt = 1; attempt <= CFG_NET_RETRIES; attempt++) {
    serialPrintf("WiFi attempt %d/%d\n", attempt, CFG_NET_RETRIES);
    if (connectWifiOnce()) {
      return true;
    }
    if (attempt < CFG_NET_RETRIES) {
      delay(CFG_NET_RETRY_DELAY_MS);
    }
  }
  serialPrintln("WiFi: all retries failed.");
  return false;
}

bool connectMqttOnce() {
  if (!cfg.mqttEnabled) {
    serialPrintln("MQTT: disabled.");
    mqttOk = false;
    return false;
  }
  if (!wifiOk || cfg.mqttHost[0] == '\0') {
    if (cfg.mqttEnabled && cfg.wifiEnabled) {
      serialPrintln("MQTT: broker or WiFi missing.");
    }
    mqttOk = false;
    return false;
  }

  mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
  mqtt.setSocketTimeout(5);
  serialPrintf("MQTT: connecting to %s:%u ...\n", cfg.mqttHost, cfg.mqttPort);

  bool ok;
  if (cfg.mqttUser[0] != '\0') {
    ok = mqtt.connect(cfg.mqttClientId, cfg.mqttUser, cfg.mqttPass);
  } else {
    ok = mqtt.connect(cfg.mqttClientId);
  }

  if (ok) {
    serialPrintln("MQTT: connected.");
    mqttOk = true;
  } else {
    serialPrintf("MQTT: failed (state=%d).\n", mqtt.state());
    mqttOk = false;
  }
  return ok;
}

bool connectMqttWithRetry() {
  for (int attempt = 1; attempt <= CFG_NET_RETRIES; attempt++) {
    serialPrintf("MQTT attempt %d/%d\n", attempt, CFG_NET_RETRIES);
    if (connectMqttOnce()) {
      return true;
    }
    if (attempt < CFG_NET_RETRIES) {
      delay(CFG_NET_RETRY_DELAY_MS);
    }
  }
  serialPrintln("MQTT: all retries failed.");
  return false;
}

bool publishDistance(uint16_t distanceMm);

// Bring network up only for a publish, then always tear down
bool publishWithNetwork(uint16_t distanceMm) {
  if (!cfg.wifiEnabled || !cfg.mqttEnabled) {
    serialPrintln("Publish skipped (WiFi or MQTT disabled).");
    return false;
  }

  bool ok = false;
  if (connectWifiWithRetry() && connectMqttWithRetry()) {
    ok = publishDistance(distanceMm);
    delay(50);
    mqtt.loop();
  }
  tearDownNetwork();

  if (ok) {
    serialPrintln("Publish OK.");
    ledBlink(1);
  } else {
    serialPrintln("Publish failed.");
    ledBlink(5);
  }
  return ok;
}

void testNetworkReconnect() {
  serialPrintln("Network test (connect with retries, then disconnect)...");
  if (connectWifiWithRetry() && cfg.mqttEnabled) {
    connectMqttWithRetry();
  }
  tearDownNetwork();
  serialPrintln("Network test done; radios off.");
}

// Light sleep: wake on timer, UART/serial, or BOOT button.
WakeSource sleepUntilWake(uint32_t seconds) {
  powerDownUnused();
  Serial.flush();

  serialPrintf("Sleeping %u s (wake: timer / serial / button)...\n", seconds);
  Serial.flush();

  const uint32_t sliceMs =
      CFG_SLEEP_SLICE_MS < 100 ? 100 : (uint32_t)CFG_SLEEP_SLICE_MS;
  uint32_t remainingMs = seconds * 1000UL;

  while (remainingMs > 0) {
    if (Serial.available()) {
      return WakeSource::SerialIn;
    }
    if (bootButtonPressed()) {
      return WakeSource::Button;
    }

    uint32_t thisSlice = remainingMs < sliceMs ? remainingMs : sliceMs;
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup((uint64_t)thisSlice * 1000ULL);

    uart_set_wakeup_threshold(UART_NUM_0, 3);
    esp_err_t uartWake = esp_sleep_enable_uart_wakeup(UART_NUM_0);
    (void)uartWake;

    enableButtonSleepWakeup();

    esp_light_sleep_start();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_GPIO || bootButtonPressed()) {
      return WakeSource::Button;
    }
    if (cause == ESP_SLEEP_WAKEUP_UART || Serial.available()) {
      return WakeSource::SerialIn;
    }

    if (remainingMs > thisSlice) {
      remainingMs -= thisSlice;
    } else {
      remainingMs = 0;
    }
  }

  return WakeSource::Timer;
}

void runMenu();  // defined below (serial configuration menu)
void runMeasureCycle();

// Stay awake for `seconds` before power-save; serial -> menu, button -> measure.
IdleEvent waitIdleBeforePowerSave(uint16_t seconds) {
  if (seconds == 0) {
    if (Serial.available()) {
      return IdleEvent::SerialIn;
    }
    if (bootButtonPressed()) {
      return IdleEvent::Button;
    }
    return IdleEvent::Done;
  }

  serialPrintf("Idle %u s before power-save (key=menu, button=measure)...\n", seconds);
  Serial.flush();

  uint32_t end = millis() + (uint32_t)seconds * 1000UL;
  while ((int32_t)(millis() - end) < 0) {
    if (Serial.available()) {
      return IdleEvent::SerialIn;
    }
    if (bootButtonPressed()) {
      return IdleEvent::Button;
    }
    delay(50);
  }
  return IdleEvent::Done;
}

// After reset or serial menu: grace period.
// Returns true if a button-triggered measure already ran (skip next scheduled measure).
bool runIdleGraceBeforePowerSave() {
  while (true) {
    IdleEvent ev = waitIdleBeforePowerSave(cfg.idleTimeoutSec);
    if (ev == IdleEvent::Done) {
      return false;
    }
    if (ev == IdleEvent::Button) {
      serialPrintln("Button: measure + publish cycle.");
      waitBootButtonRelease();
      runMeasureCycle();
      return true;
    }
    consumeSerialWake();
    runMenu();
    powerDownUnused();
  }
}

void runMenuSession() {
  consumeSerialWake();
  runMenu();
  powerDownUnused();
  // If button was pressed during post-menu idle, measure already ran.
  (void)runIdleGraceBeforePowerSave();
}

// -------------------- Sensor / level --------------------
bool readDistanceMm(uint16_t &distanceMm) {
  while (SensorSerial.available()) {
    SensorSerial.read();
  }

  SensorSerial.write(0x55);

  unsigned long startTime = millis();
  while (SensorSerial.available() < 4 && (millis() - startTime < 100)) {
    delay(1);
  }

  if (SensorSerial.available() < 4) {
    return false;
  }

  uint8_t buf[4];
  SensorSerial.readBytes(buf, 4);

  if (buf[0] != 0xFF) {
    return false;
  }

  uint8_t checksum = (buf[0] + buf[1] + buf[2]) & 0xFF;
  if (checksum != buf[3]) {
    return false;
  }

  distanceMm = ((uint16_t)buf[1] << 8) | buf[2];
  return true;
}

bool hasLevelCalibration() {
  return !isnan(cfg.distEmptyMm) && !isnan(cfg.distFullMm);
}

bool distanceExceedsMax(uint16_t distanceMm) {
  return !isnan(cfg.distMaxMm) && (float)distanceMm > cfg.distMaxMm;
}

bool distanceToLevelPct(uint16_t distanceMm, float &pct) {
  if (!hasLevelCalibration()) {
    return false;
  }
  float span = cfg.distEmptyMm - cfg.distFullMm;
  if (fabsf(span) < 1.0f) {
    return false;
  }
  pct = (cfg.distEmptyMm - (float)distanceMm) / span * 100.0f;
  if (pct < 0.0f) {
    pct = 0.0f;
  }
  if (pct > 100.0f) {
    pct = 100.0f;
  }
  return true;
}

void printMeasurement(uint16_t distanceMm) {
  float pct;
  if (distanceToLevelPct(distanceMm, pct)) {
    if (!isnan(cfg.tankLiters)) {
      float liters = pct / 100.0f * cfg.tankLiters;
      serialPrintf("Distance: %u mm | Level: %.1f %% | %.1f liters\n",
                   distanceMm, pct, liters);
    } else {
      serialPrintf("Distance: %u mm | Level: %.1f %%\n", distanceMm, pct);
    }
  } else {
    serialPrintf("Distance: %u mm\n", distanceMm);
  }
}

bool publishDistance(uint16_t distanceMm) {
  if (!cfg.mqttEnabled || !mqtt.connected()) {
    return false;
  }

  char payload[24];
  snprintf(payload, sizeof(payload), "%u", distanceMm);

  bool ok = mqtt.publish(cfg.mqttTopic, payload, true);
  serialPrintf("MQTT -> %s : %s %s\n", cfg.mqttTopic, payload, ok ? "OK" : "FAIL");
  return ok;
}

void runMeasureCycle() {
  uint16_t distanceMm = 0;
  if (!readDistanceMm(distanceMm)) {
    serialPrintln("Sensor: measurement failed / timeout.");
    ledBlink(5);
    powerDownUnused();
    return;
  }

  if (distanceExceedsMax(distanceMm)) {
    serialPrintf("ERROR: distance %u mm exceeds max %.0f mm.\n",
                 distanceMm, cfg.distMaxMm);
    ledBlink(5);
    powerDownUnused();
    return;
  }

  printMeasurement(distanceMm);
  if (cfg.wifiEnabled && cfg.mqttEnabled) {
    publishWithNetwork(distanceMm);
  }
  powerDownUnused();
}

// -------------------- Menu --------------------
void showMenuHelp() {
  serialPrintln();
  serialPrintln("=========== Oil tank configuration ===========");
  serialPrintln("  0  WiFi enabled (0/1)");
  serialPrintln("  1  WiFi SSID");
  serialPrintln("  2  WiFi password");
  serialPrintln("  3  MQTT enabled (0/1)");
  serialPrintln("  4  MQTT broker");
  serialPrintln("  5  MQTT port");
  serialPrintln("  6  MQTT user");
  serialPrintln("  7  MQTT password");
  serialPrintln("  8  MQTT topic");
  serialPrintln("  9  MQTT client ID");
  serialPrintln("  a  Distance empty (mm, optional)");
  serialPrintln("  b  Distance full (mm, optional)");
  serialPrintln("  c  Tank volume (liters, optional)");
  serialPrintln("  g  Distance max (mm, optional; over = error)");
  serialPrintln("  d  Measure interval (s)");
  serialPrintln("  f  Idle timeout before power-save (s)");
  serialPrintln("  e  Serial line ending (0=auto 1=LF 2=CR 3=CRLF)");
  serialPrintln("  s  Show status / configuration");
  serialPrintln("  t  Test measurement");
  serialPrintln("  u  EOL consistency self-test");
  serialPrintln("  w  Test WiFi + MQTT (retry 3x, then power off)");
  serialPrintln("  x  Save and leave menu");
  serialPrintln("  q  Quit without saving");
  serialPrintln("==============================================");
  serialPrintln("Empty input = keep value, '-' = clear field");
}

void runMenu() {
  Config backup = cfg;
  showMenuHelp();

  while (true) {
    String choice = readLine("Menu> ");
    if (choice.length() == 0) {
      continue;
    }
    char c = choice.charAt(0);

    switch (c) {
      case '0':
        cfg.wifiEnabled = readBool01("WiFi enabled", cfg.wifiEnabled);
        break;
      case '1':
        readString("WiFi SSID", cfg.wifiSsid, sizeof(cfg.wifiSsid), cfg.wifiSsid);
        break;
      case '2':
        readString("WiFi password", cfg.wifiPass, sizeof(cfg.wifiPass), cfg.wifiPass);
        break;
      case '3':
        cfg.mqttEnabled = readBool01("MQTT enabled", cfg.mqttEnabled);
        break;
      case '4':
        readString("MQTT broker", cfg.mqttHost, sizeof(cfg.mqttHost), cfg.mqttHost);
        break;
      case '5':
        cfg.mqttPort = (uint16_t)readInt("MQTT port", cfg.mqttPort);
        break;
      case '6':
        readString("MQTT user", cfg.mqttUser, sizeof(cfg.mqttUser), cfg.mqttUser);
        break;
      case '7':
        readString("MQTT password", cfg.mqttPass, sizeof(cfg.mqttPass), cfg.mqttPass);
        break;
      case '8':
        readString("MQTT topic", cfg.mqttTopic, sizeof(cfg.mqttTopic), cfg.mqttTopic);
        break;
      case '9':
        readString("MQTT client ID", cfg.mqttClientId, sizeof(cfg.mqttClientId), cfg.mqttClientId);
        break;
      case 'a':
      case 'A':
        readOptionalFloat("Distance empty (mm)", cfg.distEmptyMm);
        break;
      case 'b':
      case 'B':
        readOptionalFloat("Distance full (mm)", cfg.distFullMm);
        break;
      case 'c':
      case 'C':
        readOptionalFloat("Tank volume (liters)", cfg.tankLiters);
        break;
      case 'g':
      case 'G':
        readOptionalFloat("Distance max (mm)", cfg.distMaxMm);
        break;
      case 'd':
      case 'D': {
        int v = readInt("Measure interval (s)", cfg.intervalSec);
        if (v < 1) {
          v = 1;
        }
        cfg.intervalSec = (uint16_t)v;
        break;
      }
      case 'f':
      case 'F': {
        int v = readInt("Idle timeout before power-save (s)", cfg.idleTimeoutSec);
        if (v < 0) {
          v = 0;
        }
        cfg.idleTimeoutSec = (uint16_t)v;
        break;
      }
      case 'e':
      case 'E': {
        serialPrintf("Current serial EOL: %s\n", serialEolName(serialEol));
        serialPrintln("  0 = auto (learn from Enter)");
        serialPrintln("  1 = LF   (0x0A)");
        serialPrintln("  2 = CR   (0x0D)");
        serialPrintln("  3 = CRLF (0x0D 0x0A)");
        int v = readInt("Serial line ending", (int)serialEol);
        if (v >= 0 && v <= 3) {
          applySerialEol((SerialEol)v);
          serialPrintf("Serial EOL set to %s\n", serialEolName(serialEol));
        } else {
          serialPrintln("Invalid value (use 0..3).");
        }
        break;
      }
      case 's':
      case 'S':
        printConfig();
        serialPrintf("  WiFi status:    %s\n",
                     !cfg.wifiEnabled ? "disabled"
                                      : (WiFi.status() == WL_CONNECTED ? "connected" : "disconnected"));
        serialPrintf("  MQTT status:    %s\n",
                     !cfg.mqttEnabled ? "disabled"
                                      : (mqtt.connected() ? "connected" : "disconnected"));
        break;
      case 't':
      case 'T': {
        uint16_t d;
        if (!readDistanceMm(d)) {
          serialPrintln("Measurement failed.");
          ledBlink(5);
        } else if (distanceExceedsMax(d)) {
          serialPrintf("ERROR: distance %u mm exceeds max %.0f mm.\n", d, cfg.distMaxMm);
          ledBlink(5);
        } else {
          printMeasurement(d);
        }
        break;
      }
      case 'u':
      case 'U':
        if (selfTestSerialEol()) {
          serialPrintln("EOL self-test OK (LF / CR / CRLF mappings + cfg sync).");
          serialPrintf("Active output EOL: %s\n", serialEolName(serialEol));
          serialPrintln("Sample line 1");
          serialPrintln("Sample line 2");
        } else {
          serialPrintln("EOL self-test FAIL.");
        }
        break;
      case 'w':
      case 'W':
        testNetworkReconnect();
        break;
      case 'x':
      case 'X':
        saveConfig();
        powerDownUnused();
        serialPrintln("Menu closed. Measurement continues.");
        flushSerialInput();
        return;
      case 'q':
      case 'Q':
        cfg = backup;
        applySerialEol((SerialEol)cfg.serialEol);
        powerDownUnused();
        serialPrintln("Changes discarded. Menu closed.");
        flushSerialInput();
        return;
      case 'h':
      case 'H':
      case '?':
        showMenuHelp();
        break;
      default:
        serialPrintln("Unknown choice. Type ? for help.");
        break;
    }
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  setupButtonAndLed();
  SensorSerial.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  loadConfig();
  powerDownUnused();
  pendingIdleGrace = true;

  serialPrintln();
  serialPrintln("=========================================");
  serialPrintln("  UltraOilPing – ESP32 ultrasonic oil tank");
  serialPrintln("  level monitor with MQTT");
  serialPrintln("  Any key -> configuration menu");
  serialPrintln("  BOOT button -> measure + publish");
  serialPrintln("  Sleep: timer / serial / button wake");
  serialPrintln("=========================================");
  printConfig();
  if (!selfTestSerialEol()) {
    serialPrintln("WARNING: EOL self-test failed at boot.");
  }
  ledBlink(1);  // boot indicator
}

void loop() {
  bool skipMeasure = false;

  if (Serial.available()) {
    runMenuSession();
    pendingIdleGrace = false;
  } else if (pendingIdleGrace) {
    skipMeasure = runIdleGraceBeforePowerSave();
    pendingIdleGrace = false;
  } else if (bootButtonPressed()) {
    serialPrintln("Button: measure + publish cycle.");
    waitBootButtonRelease();
    runMeasureCycle();
    skipMeasure = true;
  }

  if (!skipMeasure) {
    runMeasureCycle();
  }

  for (;;) {
    WakeSource wake = sleepUntilWake(cfg.intervalSec);
    if (wake == WakeSource::SerialIn) {
      runMenuSession();
      pendingIdleGrace = false;
      break;  // continue outer loop -> next measure
    }
    if (wake == WakeSource::Button) {
      serialPrintln("Button wake: measure + publish cycle.");
      waitBootButtonRelease();
      runMeasureCycle();
      continue;  // sleep again without an extra timer measure
    }
    // Timer: leave inner loop and run a normal measure cycle
    break;
  }
}
