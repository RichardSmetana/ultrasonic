#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>
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
  float distEmptyCm;
  float distFullCm;
  float tankLiters;
  uint16_t intervalSec;
} cfg;

bool wifiOk = false;
bool mqttOk = false;

// -------------------- Serial helpers --------------------
void flushSerialInput() {
  while (Serial.available()) {
    Serial.read();
  }
}

String readLine(const char *prompt) {
  Serial.print(prompt);
  flushSerialInput();
  String line;
  while (true) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        Serial.println();
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
  cfg.distEmptyCm = CFG_DIST_EMPTY_SET ? CFG_DIST_EMPTY_CM : NAN;
  cfg.distFullCm = CFG_DIST_FULL_SET ? CFG_DIST_FULL_CM : NAN;
  cfg.tankLiters = CFG_TANK_LITERS_SET ? CFG_TANK_LITERS : NAN;
  cfg.intervalSec = CFG_INTERVAL_SEC;
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
  if (prefs.isKey("distEmpty")) {
    cfg.distEmptyCm = prefs.getFloat("distEmpty", NAN);
  }
  if (prefs.isKey("distFull")) {
    cfg.distFullCm = prefs.getFloat("distFull", NAN);
  }
  if (prefs.isKey("tankLiters")) {
    cfg.tankLiters = prefs.getFloat("tankLiters", NAN);
  }
  if (prefs.isKey("clrEmpty") && prefs.getBool("clrEmpty", false)) {
    cfg.distEmptyCm = NAN;
  }
  if (prefs.isKey("clrFull") && prefs.getBool("clrFull", false)) {
    cfg.distFullCm = NAN;
  }
  if (prefs.isKey("clrLiters") && prefs.getBool("clrLiters", false)) {
    cfg.tankLiters = NAN;
  }
  cfg.intervalSec = prefs.getUShort("interval", cfg.intervalSec);
  prefs.end();
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

  if (isnan(cfg.distEmptyCm)) {
    prefs.remove("distEmpty");
    prefs.putBool("clrEmpty", true);
  } else {
    prefs.putFloat("distEmpty", cfg.distEmptyCm);
    prefs.putBool("clrEmpty", false);
  }
  if (isnan(cfg.distFullCm)) {
    prefs.remove("distFull");
    prefs.putBool("clrFull", true);
  } else {
    prefs.putFloat("distFull", cfg.distFullCm);
    prefs.putBool("clrFull", false);
  }
  if (isnan(cfg.tankLiters)) {
    prefs.remove("tankLiters");
    prefs.putBool("clrLiters", true);
  } else {
    prefs.putFloat("tankLiters", cfg.tankLiters);
    prefs.putBool("clrLiters", false);
  }

  prefs.putUShort("interval", cfg.intervalSec);
  prefs.end();
  Serial.println("Configuration saved.");
}

void printOptionalCm(const char *label, float value) {
  Serial.print(label);
  if (isnan(value)) {
    Serial.println("(unset)");
  } else {
    Serial.printf("%.1f cm\n", value);
  }
}

void printOptionalLiters(const char *label, float value) {
  Serial.print(label);
  if (isnan(value)) {
    Serial.println("(unset)");
  } else {
    Serial.printf("%.0f liters\n", value);
  }
}

void printConfig() {
  Serial.println();
  Serial.println("---------- Current configuration ----------");
  Serial.printf("  WiFi enabled:   %s\n", cfg.wifiEnabled ? "yes" : "no");
  Serial.printf("  MQTT enabled:   %s\n", cfg.mqttEnabled ? "yes" : "no");
  Serial.printf("  WiFi SSID:      %s\n", cfg.wifiSsid[0] ? cfg.wifiSsid : "(unset)");
  Serial.printf("  WiFi password:  %s\n", cfg.wifiPass[0] ? "********" : "(unset)");
  Serial.printf("  MQTT broker:    %s\n", cfg.mqttHost[0] ? cfg.mqttHost : "(unset)");
  Serial.printf("  MQTT port:      %u\n", cfg.mqttPort);
  Serial.printf("  MQTT user:      %s\n", cfg.mqttUser[0] ? cfg.mqttUser : "(empty)");
  Serial.printf("  MQTT password:  %s\n", cfg.mqttPass[0] ? "********" : "(empty)");
  Serial.printf("  MQTT topic:     %s\n", cfg.mqttTopic);
  Serial.printf("  MQTT client ID: %s\n", cfg.mqttClientId);
  printOptionalCm("  Dist empty:     ", cfg.distEmptyCm);
  printOptionalCm("  Dist full:      ", cfg.distFullCm);
  printOptionalLiters("  Tank volume:    ", cfg.tankLiters);
  Serial.printf("  Interval:       %u s\n", cfg.intervalSec);
  Serial.println("-------------------------------------------");
}

// -------------------- WiFi / MQTT --------------------
void disconnectNetwork() {
  mqtt.disconnect();
  WiFi.disconnect(true);
  wifiOk = false;
  mqttOk = false;
}

bool connectWifi(uint32_t timeoutMs = CFG_WIFI_TIMEOUT_MS) {
  if (!cfg.wifiEnabled) {
    Serial.println("WiFi: disabled.");
    wifiOk = false;
    return false;
  }
  if (cfg.wifiSsid[0] == '\0') {
    Serial.println("WiFi: no SSID configured.");
    wifiOk = false;
    return false;
  }

  Serial.printf("WiFi: connecting to '%s' ...\n", cfg.wifiSsid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.wifiSsid, cfg.wifiPass);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi: connected, IP ");
    Serial.println(WiFi.localIP());
    wifiOk = true;
    return true;
  }

  Serial.println("WiFi: connection failed.");
  wifiOk = false;
  return false;
}

bool connectMqtt() {
  if (!cfg.mqttEnabled) {
    Serial.println("MQTT: disabled.");
    mqttOk = false;
    return false;
  }
  if (!wifiOk || cfg.mqttHost[0] == '\0') {
    if (cfg.mqttEnabled && cfg.wifiEnabled) {
      Serial.println("MQTT: broker or WiFi missing.");
    }
    mqttOk = false;
    return false;
  }

  mqtt.setServer(cfg.mqttHost, cfg.mqttPort);
  Serial.printf("MQTT: connecting to %s:%u ...\n", cfg.mqttHost, cfg.mqttPort);

  bool ok;
  if (cfg.mqttUser[0] != '\0') {
    ok = mqtt.connect(cfg.mqttClientId, cfg.mqttUser, cfg.mqttPass);
  } else {
    ok = mqtt.connect(cfg.mqttClientId);
  }

  if (ok) {
    Serial.println("MQTT: connected.");
    mqttOk = true;
  } else {
    Serial.printf("MQTT: failed (state=%d).\n", mqtt.state());
    mqttOk = false;
  }
  return ok;
}

void ensureConnectivity() {
  if (!cfg.wifiEnabled) {
    if (WiFi.status() == WL_CONNECTED || mqtt.connected()) {
      disconnectNetwork();
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    wifiOk = false;
    mqttOk = false;
    connectWifi();
  }

  if (!cfg.mqttEnabled) {
    if (mqtt.connected()) {
      mqtt.disconnect();
    }
    mqttOk = false;
    return;
  }

  if (wifiOk && !mqtt.connected()) {
    mqttOk = false;
    connectMqtt();
  }
}

void applyNetworkFromConfig() {
  disconnectNetwork();
  if (cfg.wifiEnabled) {
    connectWifi();
    if (cfg.mqttEnabled) {
      connectMqtt();
    }
  }
}

// -------------------- Sensor / level --------------------
bool readDistanceCm(float &distanceCm) {
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

  uint16_t distanceMm = ((uint16_t)buf[1] << 8) | buf[2];
  distanceCm = distanceMm / 10.0f;
  return true;
}

bool hasLevelCalibration() {
  return !isnan(cfg.distEmptyCm) && !isnan(cfg.distFullCm);
}

bool distanceToLevelPct(float distanceCm, float &pct) {
  if (!hasLevelCalibration()) {
    return false;
  }
  float span = cfg.distEmptyCm - cfg.distFullCm;
  if (fabsf(span) < 0.1f) {
    return false;
  }
  pct = (cfg.distEmptyCm - distanceCm) / span * 100.0f;
  if (pct < 0.0f) {
    pct = 0.0f;
  }
  if (pct > 100.0f) {
    pct = 100.0f;
  }
  return true;
}

void printMeasurement(float distanceCm) {
  Serial.printf("Distance: %.1f cm", distanceCm);

  float pct;
  if (distanceToLevelPct(distanceCm, pct)) {
    Serial.printf(" | Level: %.1f %%", pct);
    if (!isnan(cfg.tankLiters)) {
      float liters = pct / 100.0f * cfg.tankLiters;
      Serial.printf(" | %.1f liters", liters);
    }
  }
  Serial.println();
}

bool publishDistance(float distanceCm) {
  if (!cfg.mqttEnabled || !mqtt.connected()) {
    return false;
  }

  char payload[24];
  snprintf(payload, sizeof(payload), "%.1f", distanceCm);

  bool ok = mqtt.publish(cfg.mqttTopic, payload, true);
  Serial.printf("MQTT -> %s : %s %s\n", cfg.mqttTopic, payload, ok ? "OK" : "FAIL");
  return ok;
}

// -------------------- Menu --------------------
void showMenuHelp() {
  Serial.println();
  Serial.println("=========== Oil tank configuration ===========");
  Serial.println("  0  WiFi enabled (0/1)");
  Serial.println("  1  WiFi SSID");
  Serial.println("  2  WiFi password");
  Serial.println("  3  MQTT enabled (0/1)");
  Serial.println("  4  MQTT broker");
  Serial.println("  5  MQTT port");
  Serial.println("  6  MQTT user");
  Serial.println("  7  MQTT password");
  Serial.println("  8  MQTT topic");
  Serial.println("  9  MQTT client ID");
  Serial.println("  a  Distance empty (cm, optional)");
  Serial.println("  b  Distance full (cm, optional)");
  Serial.println("  c  Tank volume (liters, optional)");
  Serial.println("  d  Measure interval (s)");
  Serial.println("  s  Show status / configuration");
  Serial.println("  t  Test measurement");
  Serial.println("  w  Reconnect WiFi + MQTT");
  Serial.println("  x  Save and leave menu");
  Serial.println("  q  Quit without saving");
  Serial.println("==============================================");
  Serial.println("Empty input = keep value, '-' = clear field");
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
        readOptionalFloat("Distance empty (cm)", cfg.distEmptyCm);
        break;
      case 'b':
      case 'B':
        readOptionalFloat("Distance full (cm)", cfg.distFullCm);
        break;
      case 'c':
      case 'C':
        readOptionalFloat("Tank volume (liters)", cfg.tankLiters);
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
      case 's':
      case 'S':
        printConfig();
        Serial.printf("  WiFi status:    %s\n",
                      !cfg.wifiEnabled ? "disabled"
                                       : (WiFi.status() == WL_CONNECTED ? "connected" : "disconnected"));
        Serial.printf("  MQTT status:    %s\n",
                      !cfg.mqttEnabled ? "disabled"
                                       : (mqtt.connected() ? "connected" : "disconnected"));
        break;
      case 't':
      case 'T': {
        float d;
        if (readDistanceCm(d)) {
          printMeasurement(d);
        } else {
          Serial.println("Measurement failed.");
        }
        break;
      }
      case 'w':
      case 'W':
        applyNetworkFromConfig();
        break;
      case 'x':
      case 'X':
        saveConfig();
        applyNetworkFromConfig();
        Serial.println("Menu closed. Measurement continues.");
        flushSerialInput();
        return;
      case 'q':
      case 'Q':
        cfg = backup;
        Serial.println("Changes discarded. Menu closed.");
        flushSerialInput();
        return;
      case 'h':
      case 'H':
      case '?':
        showMenuHelp();
        break;
      default:
        Serial.println("Unknown choice. Type ? for help.");
        break;
    }
  }
}

bool waitIntervalOrMenu(uint16_t seconds) {
  uint32_t end = millis() + (uint32_t)seconds * 1000UL;
  while (millis() < end) {
    if (Serial.available()) {
      flushSerialInput();
      return true;
    }
    if (cfg.mqttEnabled && mqtt.connected()) {
      mqtt.loop();
    }
    delay(50);
  }
  return false;
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  SensorSerial.begin(SENSOR_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  loadConfig();

  Serial.println();
  Serial.println("=========================================");
  Serial.println("  Oil tank level (JSN-SR04T + MQTT)");
  Serial.println("  Any key -> configuration menu");
  Serial.println("=========================================");
  printConfig();

  applyNetworkFromConfig();
}

void loop() {
  if (Serial.available()) {
    flushSerialInput();
    runMenu();
  }

  ensureConnectivity();

  float distanceCm = 0.0f;
  if (readDistanceCm(distanceCm)) {
    printMeasurement(distanceCm);

    if (cfg.mqttEnabled) {
      if (mqtt.connected()) {
        publishDistance(distanceCm);
      } else {
        Serial.println("MQTT not connected – local only.");
      }
    }
  } else {
    Serial.println("Sensor: measurement failed / timeout.");
  }

  if (waitIntervalOrMenu(cfg.intervalSec)) {
    runMenu();
  }
}
