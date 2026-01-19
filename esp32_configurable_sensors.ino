/*
 * ESP32 Configurable IoT Sensor Hub with Alarms & Webhooks
 *
 * Features:
 * - DS18B20 waterproof temperature sensor support (multiple sensors on one bus)
 * - DHT22 temperature & humidity sensor
 * - PIR motion sensor
 * - LDR light sensor
 * - Relay and LED control
 * - Dynamic sensor configuration via web interface
 * - Configurable alarm thresholds (min/max) with webhook notifications
 * - Multiple webhook endpoints with scheduled updates
 * - Persistent storage of all configurations
 * - REST API for all operations
 * - Auto-updating web dashboard
 *
 * Hardware Setup:
 * - DS18B20 sensors on GPIO 13 (OneWire bus with 4.7kΩ pull-up)
 * - DHT22 sensor on GPIO 15
 * - PIR motion sensor on GPIO 14
 * - LDR (light sensor) on GPIO 34 (ADC)
 * - Relay on GPIO 26
 * - LED on GPIO 27
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Sensor pins
#define ONE_WIRE_BUS 13  // DS18B20 sensors
#define DHTPIN 15
#define DHTTYPE DHT22
#define PIR_PIN 14
#define LDR_PIN 34

// Output pins
#define RELAY_PIN 26
#define LED_PIN 27

// Maximum counts
#define MAX_DS18B20_SENSORS 8
#define MAX_ALARMS 16
#define MAX_WEBHOOKS 8

// Alarm types
enum SensorType {
  SENSOR_DS18B20,
  SENSOR_DHT_TEMP,
  SENSOR_DHT_HUM,
  SENSOR_LIGHT,
  SENSOR_MOTION
};

// Alarm structure
struct Alarm {
  char id[32];
  char name[64];
  SensorType sensorType;
  char sensorId[32];  // For DS18B20, use sensor ID. For others, use "dht_temp", "dht_hum", "light", "motion"
  float minValue;
  float maxValue;
  bool enabled;
  char webhookUrl[256];
  unsigned long lastTriggered;
  unsigned long cooldownMs;  // Minimum time between notifications (default 60000 = 1 min)
};

// Webhook configuration structure
struct WebhookConfig {
  char id[32];
  char name[64];
  char url[256];
  bool enabled;
  unsigned long updateIntervalMs;  // 0 = disabled, else send updates every X ms
  unsigned long lastUpdate;
};

// Sensor structure for DS18B20
struct DS18B20Sensor {
  uint8_t address[8];
  char id[32];
  char name[64];
  bool enabled;
  float lastReading;
};

// OneWire and DallasTemperature setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20Sensors(&oneWire);

// DHT sensor
DHT dht(DHTPIN, DHTTYPE);

// Web server
WebServer server(80);

// Persistent storage
Preferences preferences;

// Global data
DS18B20Sensor ds18b20List[MAX_DS18B20_SENSORS];
int ds18b20Count = 0;

Alarm alarms[MAX_ALARMS];
int alarmCount = 0;

WebhookConfig webhooks[MAX_WEBHOOKS];
int webhookCount = 0;

float dhtTemperature = 0;
float dhtHumidity = 0;
int motionDetected = 0;
int lightLevel = 0;
bool relayState = false;
bool ledState = false;

// Function prototypes
void handleRoot();
void handleSensorData();
void handleRelay();
void handleLED();
void handleConfig();
void handleAlarmConfig();
void handleWebhookConfig();

// DS18B20 endpoints
void handleScanDS18B20();
void handleAddDS18B20();
void handleRemoveDS18B20();
void handleListDS18B20();
void handleUpdateDS18B20();

// Alarm endpoints
void handleListAlarms();
void handleAddAlarm();
void handleUpdateAlarm();
void handleDeleteAlarm();

// Webhook endpoints
void handleListWebhooks();
void handleAddWebhook();
void handleUpdateWebhook();
void handleDeleteWebhook();

void handleNotFound();
void sendWebhookPost(String url, String event, String message, String sensorId, float value);
void readSensors();
void checkAlarms();
void sendScheduledWebhooks();
void loadConfiguration();
void saveConfiguration();
void scanDS18B20Sensors();
String generateHTML();
String generateAlarmConfigHTML();
String generateWebhookConfigHTML();
String addressToString(uint8_t* addr);
void stringToAddress(String str, uint8_t* addr);
String sensorTypeToString(SensorType type);
SensorType stringToSensorType(String str);
float getSensorValue(SensorType type, String sensorId);

void setup() {
  Serial.begin(115200);

  // Initialize pins
  pinMode(PIR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Initialize DHT sensor
  dht.begin();

  // Initialize DS18B20 sensors
  ds18b20Sensors.begin();

  // Initialize persistent storage
  preferences.begin("sensor-hub", false);

  // Load saved configuration
  loadConfiguration();

  // Scan for DS18B20 sensors
  scanDS18B20Sensors();

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/data", handleSensorData);
  server.on("/relay", handleRelay);
  server.on("/led", handleLED);
  server.on("/config", handleConfig);
  server.on("/config/alarms", handleAlarmConfig);
  server.on("/config/webhooks", handleWebhookConfig);

  // DS18B20 endpoints
  server.on("/api/scan-ds18b20", handleScanDS18B20);
  server.on("/api/add-ds18b20", HTTP_POST, handleAddDS18B20);
  server.on("/api/remove-ds18b20", HTTP_POST, handleRemoveDS18B20);
  server.on("/api/list-ds18b20", handleListDS18B20);
  server.on("/api/update-ds18b20", HTTP_POST, handleUpdateDS18B20);

  // Alarm endpoints
  server.on("/api/alarms", HTTP_GET, handleListAlarms);
  server.on("/api/alarm/add", HTTP_POST, handleAddAlarm);
  server.on("/api/alarm/update", HTTP_POST, handleUpdateAlarm);
  server.on("/api/alarm/delete", HTTP_POST, handleDeleteAlarm);

  // Webhook endpoints
  server.on("/api/webhooks", HTTP_GET, handleListWebhooks);
  server.on("/api/webhook/add", HTTP_POST, handleAddWebhook);
  server.on("/api/webhook/update", HTTP_POST, handleUpdateWebhook);
  server.on("/api/webhook/delete", HTTP_POST, handleDeleteWebhook);

  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Dashboard: http://" + WiFi.localIP().toString());
  Serial.println("Sensor Config: http://" + WiFi.localIP().toString() + "/config");
  Serial.println("Alarm Config: http://" + WiFi.localIP().toString() + "/config/alarms");
  Serial.println("Webhook Config: http://" + WiFi.localIP().toString() + "/config/webhooks");
}

void loop() {
  server.handleClient();
  readSensors();
  checkAlarms();
  sendScheduledWebhooks();
  delay(100);
}

void readSensors() {
  // Read DHT22 (temperature & humidity)
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    dhtHumidity = h;
    dhtTemperature = t;
  }

  // Read DS18B20 sensors
  ds18b20Sensors.requestTemperatures();
  for (int i = 0; i < ds18b20Count; i++) {
    if (ds18b20List[i].enabled) {
      float temp = ds18b20Sensors.getTempC(ds18b20List[i].address);
      if (temp != DEVICE_DISCONNECTED_C) {
        ds18b20List[i].lastReading = temp;
      }
    }
  }

  // Read PIR motion sensor
  motionDetected = digitalRead(PIR_PIN);

  // Read LDR (light sensor)
  int rawLight = analogRead(LDR_PIN);
  lightLevel = map(rawLight, 0, 4095, 0, 100);
}

void checkAlarms() {
  for (int i = 0; i < alarmCount; i++) {
    if (!alarms[i].enabled) continue;

    float currentValue = getSensorValue(alarms[i].sensorType, String(alarms[i].sensorId));

    // Check if alarm should trigger
    bool triggered = false;
    String message = "";

    if (currentValue < alarms[i].minValue) {
      triggered = true;
      message = String(alarms[i].name) + " is below minimum threshold (" +
                String(currentValue, 2) + " < " + String(alarms[i].minValue, 2) + ")";
    } else if (currentValue > alarms[i].maxValue) {
      triggered = true;
      message = String(alarms[i].name) + " is above maximum threshold (" +
                String(currentValue, 2) + " > " + String(alarms[i].maxValue, 2) + ")";
    }

    // Send webhook if triggered and cooldown period has passed
    if (triggered && (millis() - alarms[i].lastTriggered > alarms[i].cooldownMs)) {
      Serial.println("Alarm triggered: " + String(alarms[i].name));
      Serial.println("Sending webhook to: " + String(alarms[i].webhookUrl));

      sendWebhookPost(
        String(alarms[i].webhookUrl),
        "alarm_triggered",
        message,
        String(alarms[i].sensorId),
        currentValue
      );

      alarms[i].lastTriggered = millis();
    }
  }
}

void sendScheduledWebhooks() {
  for (int i = 0; i < webhookCount; i++) {
    if (!webhooks[i].enabled || webhooks[i].updateIntervalMs == 0) continue;

    if (millis() - webhooks[i].lastUpdate >= webhooks[i].updateIntervalMs) {
      Serial.println("Sending scheduled update to: " + String(webhooks[i].name));

      // Build comprehensive sensor data
      DynamicJsonDocument doc(1024);
      doc["event"] = "scheduled_update";
      doc["webhook_name"] = webhooks[i].name;
      doc["timestamp"] = millis();

      // Add all sensor data
      doc["dht_temperature"] = dhtTemperature;
      doc["dht_humidity"] = dhtHumidity;
      doc["light"] = lightLevel;
      doc["motion"] = motionDetected;
      doc["relay"] = relayState;
      doc["led"] = ledState;

      JsonArray ds18b20Array = doc.createNestedArray("ds18b20_sensors");
      for (int j = 0; j < ds18b20Count; j++) {
        if (ds18b20List[j].enabled) {
          JsonObject sensor = ds18b20Array.createNestedObject();
          sensor["id"] = ds18b20List[j].id;
          sensor["name"] = ds18b20List[j].name;
          sensor["temperature"] = ds18b20List[j].lastReading;
        }
      }

      String json;
      serializeJson(doc, json);

      // Send webhook
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(webhooks[i].url);
        http.addHeader("Content-Type", "application/json");
        int code = http.POST(json);
        Serial.println("Webhook response: " + String(code));
        http.end();
      }

      webhooks[i].lastUpdate = millis();
    }
  }
}

float getSensorValue(SensorType type, String sensorId) {
  switch (type) {
    case SENSOR_DHT_TEMP:
      return dhtTemperature;
    case SENSOR_DHT_HUM:
      return dhtHumidity;
    case SENSOR_LIGHT:
      return lightLevel;
    case SENSOR_MOTION:
      return motionDetected;
    case SENSOR_DS18B20:
      for (int i = 0; i < ds18b20Count; i++) {
        if (String(ds18b20List[i].id) == sensorId) {
          return ds18b20List[i].lastReading;
        }
      }
      return -999.0;
    default:
      return -999.0;
  }
}

void scanDS18B20Sensors() {
  Serial.println("Scanning for DS18B20 sensors...");
  ds18b20Sensors.begin();
  int deviceCount = ds18b20Sensors.getDeviceCount();
  Serial.print("Found ");
  Serial.print(deviceCount);
  Serial.println(" DS18B20 device(s)");

  // If no sensors in config, auto-add discovered ones
  if (ds18b20Count == 0) {
    for (int i = 0; i < deviceCount && i < MAX_DS18B20_SENSORS; i++) {
      uint8_t addr[8];
      if (ds18b20Sensors.getAddress(addr, i)) {
        memcpy(ds18b20List[ds18b20Count].address, addr, 8);
        sprintf(ds18b20List[ds18b20Count].id, "ds18b20_%d", ds18b20Count + 1);
        sprintf(ds18b20List[ds18b20Count].name, "DS18B20 Sensor %d", ds18b20Count + 1);
        ds18b20List[ds18b20Count].enabled = true;
        ds18b20List[ds18b20Count].lastReading = -127.0;
        ds18b20Count++;
        Serial.print("Auto-added sensor: ");
        Serial.println(addressToString(addr));
      }
    }
    if (ds18b20Count > 0) {
      saveConfiguration();
    }
  }
}

void loadConfiguration() {
  // Load DS18B20 sensors
  ds18b20Count = preferences.getInt("ds18Count", 0);
  Serial.print("Loading ");
  Serial.print(ds18b20Count);
  Serial.println(" DS18B20 sensors");

  for (int i = 0; i < ds18b20Count; i++) {
    String prefix = "ds" + String(i) + "_";
    String addrStr = preferences.getString((prefix + "addr").c_str(), "");
    if (addrStr.length() > 0) {
      stringToAddress(addrStr, ds18b20List[i].address);
    }
    String id = preferences.getString((prefix + "id").c_str(), "");
    id.toCharArray(ds18b20List[i].id, 32);
    String name = preferences.getString((prefix + "name").c_str(), "");
    name.toCharArray(ds18b20List[i].name, 64);
    ds18b20List[i].enabled = preferences.getBool((prefix + "en").c_str(), true);
    ds18b20List[i].lastReading = -127.0;
  }

  // Load alarms
  alarmCount = preferences.getInt("alarmCount", 0);
  Serial.print("Loading ");
  Serial.print(alarmCount);
  Serial.println(" alarms");

  for (int i = 0; i < alarmCount; i++) {
    String prefix = "al" + String(i) + "_";
    String id = preferences.getString((prefix + "id").c_str(), "");
    id.toCharArray(alarms[i].id, 32);
    String name = preferences.getString((prefix + "name").c_str(), "");
    name.toCharArray(alarms[i].name, 64);
    String type = preferences.getString((prefix + "type").c_str(), "");
    alarms[i].sensorType = stringToSensorType(type);
    String sensorId = preferences.getString((prefix + "sid").c_str(), "");
    sensorId.toCharArray(alarms[i].sensorId, 32);
    alarms[i].minValue = preferences.getFloat((prefix + "min").c_str(), -999.0);
    alarms[i].maxValue = preferences.getFloat((prefix + "max").c_str(), 999.0);
    alarms[i].enabled = preferences.getBool((prefix + "en").c_str(), true);
    String url = preferences.getString((prefix + "url").c_str(), "");
    url.toCharArray(alarms[i].webhookUrl, 256);
    alarms[i].cooldownMs = preferences.getULong((prefix + "cd").c_str(), 60000);
    alarms[i].lastTriggered = 0;
  }

  // Load webhooks
  webhookCount = preferences.getInt("whCount", 0);
  Serial.print("Loading ");
  Serial.print(webhookCount);
  Serial.println(" webhooks");

  for (int i = 0; i < webhookCount; i++) {
    String prefix = "wh" + String(i) + "_";
    String id = preferences.getString((prefix + "id").c_str(), "");
    id.toCharArray(webhooks[i].id, 32);
    String name = preferences.getString((prefix + "name").c_str(), "");
    name.toCharArray(webhooks[i].name, 64);
    String url = preferences.getString((prefix + "url").c_str(), "");
    url.toCharArray(webhooks[i].url, 256);
    webhooks[i].enabled = preferences.getBool((prefix + "en").c_str(), true);
    webhooks[i].updateIntervalMs = preferences.getULong((prefix + "int").c_str(), 0);
    webhooks[i].lastUpdate = 0;
  }
}

void saveConfiguration() {
  // Save DS18B20 sensors
  preferences.putInt("ds18Count", ds18b20Count);
  for (int i = 0; i < ds18b20Count; i++) {
    String prefix = "ds" + String(i) + "_";
    preferences.putString((prefix + "addr").c_str(), addressToString(ds18b20List[i].address));
    preferences.putString((prefix + "id").c_str(), String(ds18b20List[i].id));
    preferences.putString((prefix + "name").c_str(), String(ds18b20List[i].name));
    preferences.putBool((prefix + "en").c_str(), ds18b20List[i].enabled);
  }

  // Save alarms
  preferences.putInt("alarmCount", alarmCount);
  for (int i = 0; i < alarmCount; i++) {
    String prefix = "al" + String(i) + "_";
    preferences.putString((prefix + "id").c_str(), String(alarms[i].id));
    preferences.putString((prefix + "name").c_str(), String(alarms[i].name));
    preferences.putString((prefix + "type").c_str(), sensorTypeToString(alarms[i].sensorType));
    preferences.putString((prefix + "sid").c_str(), String(alarms[i].sensorId));
    preferences.putFloat((prefix + "min").c_str(), alarms[i].minValue);
    preferences.putFloat((prefix + "max").c_str(), alarms[i].maxValue);
    preferences.putBool((prefix + "en").c_str(), alarms[i].enabled);
    preferences.putString((prefix + "url").c_str(), String(alarms[i].webhookUrl));
    preferences.putULong((prefix + "cd").c_str(), alarms[i].cooldownMs);
  }

  // Save webhooks
  preferences.putInt("whCount", webhookCount);
  for (int i = 0; i < webhookCount; i++) {
    String prefix = "wh" + String(i) + "_";
    preferences.putString((prefix + "id").c_str(), String(webhooks[i].id));
    preferences.putString((prefix + "name").c_str(), String(webhooks[i].name));
    preferences.putString((prefix + "url").c_str(), String(webhooks[i].url));
    preferences.putBool((prefix + "en").c_str(), webhooks[i].enabled);
    preferences.putULong((prefix + "int").c_str(), webhooks[i].updateIntervalMs);
  }

  Serial.println("Configuration saved");
}

String addressToString(uint8_t* addr) {
  String result = "";
  for (int i = 0; i < 8; i++) {
    if (addr[i] < 16) result += "0";
    result += String(addr[i], HEX);
    if (i < 7) result += ":";
  }
  result.toUpperCase();
  return result;
}

void stringToAddress(String str, uint8_t* addr) {
  int pos = 0;
  for (int i = 0; i < 8; i++) {
    String byteStr = str.substring(pos, pos + 2);
    addr[i] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
    pos += 3;
  }
}

String sensorTypeToString(SensorType type) {
  switch (type) {
    case SENSOR_DS18B20: return "ds18b20";
    case SENSOR_DHT_TEMP: return "dht_temp";
    case SENSOR_DHT_HUM: return "dht_hum";
    case SENSOR_LIGHT: return "light";
    case SENSOR_MOTION: return "motion";
    default: return "unknown";
  }
}

SensorType stringToSensorType(String str) {
  if (str == "ds18b20") return SENSOR_DS18B20;
  if (str == "dht_temp") return SENSOR_DHT_TEMP;
  if (str == "dht_hum") return SENSOR_DHT_HUM;
  if (str == "light") return SENSOR_LIGHT;
  if (str == "motion") return SENSOR_MOTION;
  return SENSOR_DHT_TEMP;
}

void sendWebhookPost(String url, String event, String message, String sensorId, float value) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    DynamicJsonDocument doc(512);
    doc["event"] = event;
    doc["message"] = message;
    doc["sensor_id"] = sensorId;
    doc["value"] = value;
    doc["device"] = "ESP32";
    doc["timestamp"] = millis();

    String json;
    serializeJson(doc, json);

    int httpResponseCode = http.POST(json);
    Serial.print("Webhook response: ");
    Serial.println(httpResponseCode);
    http.end();
  }
}

// ========== WEB HANDLERS ==========

void handleRoot() {
  String html = generateHTML();
  server.send(200, "text/html", html);
}

void handleSensorData() {
  DynamicJsonDocument doc(2048);

  // DHT sensor
  doc["dht"]["temperature"] = dhtTemperature;
  doc["dht"]["humidity"] = dhtHumidity;

  // DS18B20 sensors
  JsonArray ds18b20Array = doc.createNestedArray("ds18b20");
  for (int i = 0; i < ds18b20Count; i++) {
    if (ds18b20List[i].enabled) {
      JsonObject sensor = ds18b20Array.createNestedObject();
      sensor["id"] = ds18b20List[i].id;
      sensor["name"] = ds18b20List[i].name;
      sensor["temperature"] = ds18b20List[i].lastReading;
      sensor["address"] = addressToString(ds18b20List[i].address);
    }
  }

  // Other sensors
  doc["motion"] = motionDetected;
  doc["light"] = lightLevel;
  doc["relay"] = relayState;
  doc["led"] = ledState;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleRelay() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "on") {
      digitalWrite(RELAY_PIN, HIGH);
      relayState = true;
      server.send(200, "text/plain", "Relay ON");
    } else if (state == "off") {
      digitalWrite(RELAY_PIN, LOW);
      relayState = false;
      server.send(200, "text/plain", "Relay OFF");
    } else {
      server.send(400, "text/plain", "Invalid state");
    }
  } else {
    server.send(400, "text/plain", "Missing state parameter");
  }
}

void handleLED() {
  if (server.hasArg("state")) {
    String state = server.arg("state");
    if (state == "on") {
      digitalWrite(LED_PIN, HIGH);
      ledState = true;
      server.send(200, "text/plain", "LED ON");
    } else if (state == "off") {
      digitalWrite(LED_PIN, LOW);
      ledState = false;
      server.send(200, "text/plain", "LED OFF");
    } else {
      server.send(400, "text/plain", "Invalid state");
    }
  } else {
    server.send(400, "text/plain", "Missing state parameter");
  }
}

void handleConfig() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Sensor Configuration</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
    .container { max-width: 1000px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .section { margin: 20px 0; padding: 15px; background-color: #f9f9f9; border-radius: 5px; }
    table { width: 100%; border-collapse: collapse; margin: 10px 0; }
    th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #4CAF50; color: white; }
    button { background-color: #4CAF50; color: white; border: none; padding: 8px 16px; cursor: pointer; border-radius: 4px; margin: 2px; }
    button:hover { opacity: 0.8; }
    button.danger { background-color: #f44336; }
    button.primary { background-color: #2196F3; }
    input[type="text"] { padding: 8px; border: 1px solid #ddd; border-radius: 4px; width: 200px; }
    .nav { margin-bottom: 20px; }
    .nav a { text-decoration: none; color: white; background-color: #2196F3; padding: 10px 20px; border-radius: 5px; margin: 5px; display: inline-block; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
      <a href="/">← Dashboard</a>
      <a href="/config/alarms">Alarms</a>
      <a href="/config/webhooks">Webhooks</a>
    </div>

    <h1>DS18B20 Sensor Configuration</h1>

    <div class="section">
      <h2>Configured DS18B20 Sensors</h2>
      <button class="primary" onclick="scanSensors()">Scan for New Sensors</button>
      <button onclick="refreshList()">Refresh List</button>
      <div id="sensor-list"></div>
    </div>

    <div class="section">
      <h2>Available DS18B20 Sensors (Not Configured)</h2>
      <button class="primary" onclick="scanAvailable()">Scan Bus</button>
      <div id="available-list"></div>
    </div>
  </div>

  <script>
    function refreshList() {
      fetch('/api/list-ds18b20')
        .then(r => r.json())
        .then(data => {
          let html = '<table><tr><th>ID</th><th>Name</th><th>Address</th><th>Temperature</th><th>Status</th><th>Actions</th></tr>';
          data.sensors.forEach(s => {
            html += '<tr>';
            html += '<td>' + s.id + '</td>';
            html += '<td><input type="text" id="name-' + s.id + '" value="' + s.name + '"></td>';
            html += '<td>' + s.address + '</td>';
            html += '<td>' + s.temperature.toFixed(2) + '°C</td>';
            html += '<td>' + (s.enabled ? 'Enabled' : 'Disabled') + '</td>';
            html += '<td>';
            html += '<button onclick="updateSensor(\'' + s.id + '\')">Update</button>';
            html += '<button class="danger" onclick="removeSensor(\'' + s.address + '\')">Remove</button>';
            html += '</td>';
            html += '</tr>';
          });
          html += '</table>';
          document.getElementById('sensor-list').innerHTML = html;
        });
    }

    function scanSensors() {
      fetch('/api/scan-ds18b20')
        .then(r => r.json())
        .then(data => {
          alert('Scan complete! Found ' + data.count + ' sensor(s)');
          refreshList();
        });
    }

    function scanAvailable() {
      fetch('/api/scan-ds18b20')
        .then(r => r.json())
        .then(data => {
          let html = '<table><tr><th>Address</th><th>Actions</th></tr>';
          data.discovered.forEach(addr => {
            html += '<tr>';
            html += '<td>' + addr + '</td>';
            html += '<td><button class="primary" onclick="addSensor(\'' + addr + '\')">Add Sensor</button></td>';
            html += '</tr>';
          });
          html += '</table>';
          document.getElementById('available-list').innerHTML = html;
        });
    }

    function addSensor(address) {
      let id = prompt('Enter sensor ID (e.g., pool_temp):');
      let name = prompt('Enter sensor name (e.g., Pool Temperature):');
      if (id && name) {
        fetch('/api/add-ds18b20', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({address: address, id: id, name: name})
        })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshList();
        });
      }
    }

    function updateSensor(id) {
      let name = document.getElementById('name-' + id).value;
      fetch('/api/update-ds18b20', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({id: id, name: name})
      })
      .then(r => r.text())
      .then(msg => {
        alert(msg);
        refreshList();
      });
    }

    function removeSensor(address) {
      if (confirm('Remove this sensor?')) {
        fetch('/api/remove-ds18b20', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({address: address})
        })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshList();
        });
      }
    }

    refreshList();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleAlarmConfig() {
  server.send(200, "text/html", generateAlarmConfigHTML());
}

void handleWebhookConfig() {
  server.send(200, "text/html", generateWebhookConfigHTML());
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}

// ========== DS18B20 API HANDLERS ==========

void handleScanDS18B20() {
  ds18b20Sensors.begin();
  int deviceCount = ds18b20Sensors.getDeviceCount();

  DynamicJsonDocument doc(1024);
  doc["count"] = deviceCount;

  JsonArray discovered = doc.createNestedArray("discovered");
  for (int i = 0; i < deviceCount; i++) {
    uint8_t addr[8];
    if (ds18b20Sensors.getAddress(addr, i)) {
      discovered.add(addressToString(addr));
    }
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleAddDS18B20() {
  if (ds18b20Count >= MAX_DS18B20_SENSORS) {
    server.send(400, "text/plain", "Maximum sensors reached");
    return;
  }

  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    deserializeJson(doc, server.arg("plain"));

    String address = doc["address"];
    String id = doc["id"];
    String name = doc["name"];

    stringToAddress(address, ds18b20List[ds18b20Count].address);
    id.toCharArray(ds18b20List[ds18b20Count].id, 32);
    name.toCharArray(ds18b20List[ds18b20Count].name, 64);
    ds18b20List[ds18b20Count].enabled = true;
    ds18b20List[ds18b20Count].lastReading = -127.0;

    ds18b20Count++;
    saveConfiguration();

    server.send(200, "text/plain", "Sensor added successfully");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void handleRemoveDS18B20() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));

    String address = doc["address"];

    for (int i = 0; i < ds18b20Count; i++) {
      if (addressToString(ds18b20List[i].address) == address) {
        for (int j = i; j < ds18b20Count - 1; j++) {
          memcpy(&ds18b20List[j], &ds18b20List[j + 1], sizeof(DS18B20Sensor));
        }
        ds18b20Count--;
        saveConfiguration();
        server.send(200, "text/plain", "Sensor removed");
        return;
      }
    }

    server.send(404, "text/plain", "Sensor not found");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void handleListDS18B20() {
  DynamicJsonDocument doc(2048);
  JsonArray sensors = doc.createNestedArray("sensors");

  for (int i = 0; i < ds18b20Count; i++) {
    JsonObject sensor = sensors.createNestedObject();
    sensor["id"] = ds18b20List[i].id;
    sensor["name"] = ds18b20List[i].name;
    sensor["address"] = addressToString(ds18b20List[i].address);
    sensor["enabled"] = ds18b20List[i].enabled;
    sensor["temperature"] = ds18b20List[i].lastReading;
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleUpdateDS18B20() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(512);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];
    String name = doc["name"];

    for (int i = 0; i < ds18b20Count; i++) {
      if (String(ds18b20List[i].id) == id) {
        name.toCharArray(ds18b20List[i].name, 64);
        saveConfiguration();
        server.send(200, "text/plain", "Sensor updated");
        return;
      }
    }

    server.send(404, "text/plain", "Sensor not found");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

// ========== ALARM API HANDLERS ==========

void handleListAlarms() {
  DynamicJsonDocument doc(4096);
  JsonArray alarmsArray = doc.createNestedArray("alarms");

  for (int i = 0; i < alarmCount; i++) {
    JsonObject alarm = alarmsArray.createNestedObject();
    alarm["id"] = alarms[i].id;
    alarm["name"] = alarms[i].name;
    alarm["sensor_type"] = sensorTypeToString(alarms[i].sensorType);
    alarm["sensor_id"] = alarms[i].sensorId;
    alarm["min_value"] = alarms[i].minValue;
    alarm["max_value"] = alarms[i].maxValue;
    alarm["enabled"] = alarms[i].enabled;
    alarm["webhook_url"] = alarms[i].webhookUrl;
    alarm["cooldown_ms"] = alarms[i].cooldownMs;
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleAddAlarm() {
  if (alarmCount >= MAX_ALARMS) {
    server.send(400, "text/plain", "Maximum alarms reached");
    return;
  }

  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];
    id.toCharArray(alarms[alarmCount].id, 32);

    String name = doc["name"];
    name.toCharArray(alarms[alarmCount].name, 64);

    alarms[alarmCount].sensorType = stringToSensorType(doc["sensor_type"]);

    String sensorId = doc["sensor_id"];
    sensorId.toCharArray(alarms[alarmCount].sensorId, 32);

    alarms[alarmCount].minValue = doc["min_value"];
    alarms[alarmCount].maxValue = doc["max_value"];
    alarms[alarmCount].enabled = doc["enabled"] | true;

    String url = doc["webhook_url"];
    url.toCharArray(alarms[alarmCount].webhookUrl, 256);

    alarms[alarmCount].cooldownMs = doc["cooldown_ms"] | 60000;
    alarms[alarmCount].lastTriggered = 0;

    alarmCount++;
    saveConfiguration();

    server.send(200, "text/plain", "Alarm added successfully");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void handleUpdateAlarm() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];

    for (int i = 0; i < alarmCount; i++) {
      if (String(alarms[i].id) == id) {
        String name = doc["name"];
        name.toCharArray(alarms[i].name, 64);

        alarms[i].sensorType = stringToSensorType(doc["sensor_type"]);

        String sensorId = doc["sensor_id"];
        sensorId.toCharArray(alarms[i].sensorId, 32);

        alarms[i].minValue = doc["min_value"];
        alarms[i].maxValue = doc["max_value"];
        alarms[i].enabled = doc["enabled"];

        String url = doc["webhook_url"];
        url.toCharArray(alarms[i].webhookUrl, 256);

        alarms[i].cooldownMs = doc["cooldown_ms"];

        saveConfiguration();
        server.send(200, "text/plain", "Alarm updated");
        return;
      }
    }

    server.send(404, "text/plain", "Alarm not found");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void handleDeleteAlarm() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];

    for (int i = 0; i < alarmCount; i++) {
      if (String(alarms[i].id) == id) {
        for (int j = i; j < alarmCount - 1; j++) {
          memcpy(&alarms[j], &alarms[j + 1], sizeof(Alarm));
        }
        alarmCount--;
        saveConfiguration();
        server.send(200, "text/plain", "Alarm deleted");
        return;
      }
    }

    server.send(404, "text/plain", "Alarm not found");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

// ========== WEBHOOK API HANDLERS ==========

void handleListWebhooks() {
  DynamicJsonDocument doc(4096);
  JsonArray webhooksArray = doc.createNestedArray("webhooks");

  for (int i = 0; i < webhookCount; i++) {
    JsonObject webhook = webhooksArray.createNestedObject();
    webhook["id"] = webhooks[i].id;
    webhook["name"] = webhooks[i].name;
    webhook["url"] = webhooks[i].url;
    webhook["enabled"] = webhooks[i].enabled;
    webhook["update_interval_ms"] = webhooks[i].updateIntervalMs;
  }

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleAddWebhook() {
  if (webhookCount >= MAX_WEBHOOKS) {
    server.send(400, "text/plain", "Maximum webhooks reached");
    return;
  }

  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];
    id.toCharArray(webhooks[webhookCount].id, 32);

    String name = doc["name"];
    name.toCharArray(webhooks[webhookCount].name, 64);

    String url = doc["url"];
    url.toCharArray(webhooks[webhookCount].url, 256);

    webhooks[webhookCount].enabled = doc["enabled"] | true;
    webhooks[webhookCount].updateIntervalMs = doc["update_interval_ms"] | 0;
    webhooks[webhookCount].lastUpdate = 0;

    webhookCount++;
    saveConfiguration();

    server.send(200, "text/plain", "Webhook added successfully");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void handleUpdateWebhook() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];

    for (int i = 0; i < webhookCount; i++) {
      if (String(webhooks[i].id) == id) {
        String name = doc["name"];
        name.toCharArray(webhooks[i].name, 64);

        String url = doc["url"];
        url.toCharArray(webhooks[i].url, 256);

        webhooks[i].enabled = doc["enabled"];
        webhooks[i].updateIntervalMs = doc["update_interval_ms"];

        saveConfiguration();
        server.send(200, "text/plain", "Webhook updated");
        return;
      }
    }

    server.send(404, "text/plain", "Webhook not found");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

void handleDeleteWebhook() {
  if (server.hasArg("plain")) {
    DynamicJsonDocument doc(256);
    deserializeJson(doc, server.arg("plain"));

    String id = doc["id"];

    for (int i = 0; i < webhookCount; i++) {
      if (String(webhooks[i].id) == id) {
        for (int j = i; j < webhookCount - 1; j++) {
          memcpy(&webhooks[j], &webhooks[j + 1], sizeof(WebhookConfig));
        }
        webhookCount--;
        saveConfiguration();
        server.send(200, "text/plain", "Webhook deleted");
        return;
      }
    }

    server.send(404, "text/plain", "Webhook not found");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

// ========== HTML GENERATION ==========

String generateHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Sensor Dashboard</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
    .container { max-width: 1200px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { margin-bottom: 20px; text-align: center; }
    .nav a { text-decoration: none; color: white; background-color: #2196F3; padding: 10px 20px; border-radius: 5px; margin: 0 10px; display: inline-block; }
    .sensor-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }
    .sensor-card { background-color: #f9f9f9; border: 1px solid #ddd; border-radius: 8px; padding: 15px; text-align: center; }
    .sensor-label { font-size: 14px; color: #666; margin-bottom: 5px; }
    .sensor-value { font-size: 28px; font-weight: bold; color: #2196F3; }
    .sensor-id { font-size: 11px; color: #999; margin-top: 5px; }
    .control-section { margin-top: 30px; }
    .button { background-color: #4CAF50; color: white; border: none; padding: 12px 24px; font-size: 16px; margin: 5px; cursor: pointer; border-radius: 5px; }
    .button:hover { opacity: 0.8; }
    .button-off { background-color: #f44336; }
    .status-indicator { display: inline-block; width: 12px; height: 12px; border-radius: 50%; margin-left: 5px; }
    .status-on { background-color: #4CAF50; }
    .status-off { background-color: #ccc; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32 Sensor Dashboard</h1>

    <div class="nav">
      <a href="/config">⚙️ Sensors</a>
      <a href="/config/alarms">🔔 Alarms</a>
      <a href="/config/webhooks">🔗 Webhooks</a>
    </div>

    <h2>DS18B20 Waterproof Sensors</h2>
    <div class="sensor-grid" id="ds18b20-grid"></div>

    <h2>Other Sensors</h2>
    <div class="sensor-grid">
      <div class="sensor-card">
        <div class="sensor-label">DHT Temperature</div>
        <div class="sensor-value" id="dht-temp">--</div>
        <div class="sensor-label">°C</div>
      </div>

      <div class="sensor-card">
        <div class="sensor-label">DHT Humidity</div>
        <div class="sensor-value" id="dht-hum">--</div>
        <div class="sensor-label">%</div>
      </div>

      <div class="sensor-card">
        <div class="sensor-label">Light Level</div>
        <div class="sensor-value" id="light">--</div>
        <div class="sensor-label">%</div>
      </div>

      <div class="sensor-card">
        <div class="sensor-label">Motion</div>
        <div class="sensor-value" id="motion">--</div>
      </div>
    </div>

    <div class="control-section">
      <h2>Controls</h2>
      <div>
        <button class="button" onclick="controlRelay('on')">Relay ON</button>
        <button class="button button-off" onclick="controlRelay('off')">Relay OFF</button>
        <span class="status-indicator" id="relay-status"></span>
      </div>
      <div>
        <button class="button" onclick="controlLED('on')">LED ON</button>
        <button class="button button-off" onclick="controlLED('off')">LED OFF</button>
        <span class="status-indicator" id="led-status"></span>
      </div>
    </div>
  </div>

  <script>
    function updateSensorData() {
      fetch('/data')
        .then(r => r.json())
        .then(data => {
          document.getElementById('dht-temp').textContent = data.dht.temperature.toFixed(1);
          document.getElementById('dht-hum').textContent = data.dht.humidity.toFixed(1);
          document.getElementById('light').textContent = data.light;
          document.getElementById('motion').textContent = data.motion ? 'DETECTED' : 'Clear';

          let ds18b20HTML = '';
          if (data.ds18b20 && data.ds18b20.length > 0) {
            data.ds18b20.forEach(s => {
              ds18b20HTML += '<div class="sensor-card">';
              ds18b20HTML += '<div class="sensor-label">' + s.name + '</div>';
              ds18b20HTML += '<div class="sensor-value">' + s.temperature.toFixed(2) + '</div>';
              ds18b20HTML += '<div class="sensor-label">°C</div>';
              ds18b20HTML += '<div class="sensor-id">ID: ' + s.id + '</div>';
              ds18b20HTML += '</div>';
            });
          } else {
            ds18b20HTML = '<div class="sensor-card"><div class="sensor-label">No DS18B20 sensors</div><div class="sensor-value">--</div></div>';
          }
          document.getElementById('ds18b20-grid').innerHTML = ds18b20HTML;

          document.getElementById('relay-status').className = 'status-indicator ' + (data.relay ? 'status-on' : 'status-off');
          document.getElementById('led-status').className = 'status-indicator ' + (data.led ? 'status-on' : 'status-off');
        })
        .catch(error => console.error('Error:', error));
    }

    function controlRelay(state) {
      fetch('/relay?state=' + state)
        .then(() => updateSensorData());
    }

    function controlLED(state) {
      fetch('/led?state=' + state)
        .then(() => updateSensorData());
    }

    setInterval(updateSensorData, 2000);
    updateSensorData();
  </script>
</body>
</html>
)rawliteral";

  return html;
}

String generateAlarmConfigHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Alarm Configuration</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
    .container { max-width: 1200px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { margin-bottom: 20px; }
    .nav a { text-decoration: none; color: white; background-color: #2196F3; padding: 10px 20px; border-radius: 5px; margin: 5px; display: inline-block; }
    table { width: 100%; border-collapse: collapse; margin: 10px 0; }
    th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; font-size: 12px; }
    th { background-color: #FF9800; color: white; }
    button { background-color: #4CAF50; color: white; border: none; padding: 8px 16px; cursor: pointer; border-radius: 4px; margin: 2px; }
    button:hover { opacity: 0.8; }
    button.danger { background-color: #f44336; }
    button.primary { background-color: #2196F3; }
    .form-group { margin: 10px 0; }
    label { display: inline-block; width: 150px; font-weight: bold; }
    input, select { padding: 8px; border: 1px solid #ddd; border-radius: 4px; width: 300px; }
    .add-form { background-color: #f9f9f9; padding: 20px; border-radius: 5px; margin: 20px 0; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
      <a href="/">← Dashboard</a>
      <a href="/config">Sensors</a>
      <a href="/config/webhooks">Webhooks</a>
    </div>

    <h1>🔔 Alarm Configuration</h1>

    <div class="add-form">
      <h2>Add New Alarm</h2>
      <div class="form-group">
        <label>Alarm ID:</label>
        <input type="text" id="new-id" placeholder="pool_temp_high">
      </div>
      <div class="form-group">
        <label>Alarm Name:</label>
        <input type="text" id="new-name" placeholder="Pool Temperature High">
      </div>
      <div class="form-group">
        <label>Sensor Type:</label>
        <select id="new-type" onchange="updateSensorIdField()">
          <option value="ds18b20">DS18B20</option>
          <option value="dht_temp">DHT Temperature</option>
          <option value="dht_hum">DHT Humidity</option>
          <option value="light">Light Level</option>
          <option value="motion">Motion</option>
        </select>
      </div>
      <div class="form-group">
        <label>Sensor ID:</label>
        <input type="text" id="new-sensor-id" placeholder="pool_temp">
        <small id="sensor-id-hint"></small>
      </div>
      <div class="form-group">
        <label>Min Value:</label>
        <input type="number" step="0.1" id="new-min" value="-999">
      </div>
      <div class="form-group">
        <label>Max Value:</label>
        <input type="number" step="0.1" id="new-max" value="30">
      </div>
      <div class="form-group">
        <label>Webhook URL:</label>
        <input type="text" id="new-url" placeholder="https://your-webhook-url.com/endpoint">
      </div>
      <div class="form-group">
        <label>Cooldown (ms):</label>
        <input type="number" id="new-cooldown" value="60000">
        <small>(60000 = 1 minute)</small>
      </div>
      <button class="primary" onclick="addAlarm()">Add Alarm</button>
    </div>

    <h2>Configured Alarms</h2>
    <button onclick="refreshList()">Refresh List</button>
    <div id="alarm-list"></div>
  </div>

  <script>
    function updateSensorIdField() {
      const type = document.getElementById('new-type').value;
      const sensorIdInput = document.getElementById('new-sensor-id');
      const hint = document.getElementById('sensor-id-hint');

      if (type === 'ds18b20') {
        sensorIdInput.placeholder = 'pool_temp';
        hint.textContent = 'Enter DS18B20 sensor ID';
      } else {
        sensorIdInput.value = type;
        sensorIdInput.placeholder = type;
        hint.textContent = 'Auto-filled for non-DS18B20 sensors';
      }
    }

    function refreshList() {
      fetch('/api/alarms')
        .then(r => r.json())
        .then(data => {
          let html = '<table><tr><th>ID</th><th>Name</th><th>Sensor</th><th>Min</th><th>Max</th><th>Webhook URL</th><th>Cooldown</th><th>Status</th><th>Actions</th></tr>';
          data.alarms.forEach(a => {
            html += '<tr>';
            html += '<td>' + a.id + '</td>';
            html += '<td>' + a.name + '</td>';
            html += '<td>' + a.sensor_type + '/' + a.sensor_id + '</td>';
            html += '<td>' + a.min_value + '</td>';
            html += '<td>' + a.max_value + '</td>';
            html += '<td style="max-width:200px;overflow:hidden;text-overflow:ellipsis;">' + a.webhook_url + '</td>';
            html += '<td>' + (a.cooldown_ms / 1000) + 's</td>';
            html += '<td>' + (a.enabled ? '✅' : '❌') + '</td>';
            html += '<td><button class="danger" onclick="deleteAlarm(\'' + a.id + '\')">Delete</button></td>';
            html += '</tr>';
          });
          html += '</table>';
          document.getElementById('alarm-list').innerHTML = html;
        });
    }

    function addAlarm() {
      const alarm = {
        id: document.getElementById('new-id').value,
        name: document.getElementById('new-name').value,
        sensor_type: document.getElementById('new-type').value,
        sensor_id: document.getElementById('new-sensor-id').value,
        min_value: parseFloat(document.getElementById('new-min').value),
        max_value: parseFloat(document.getElementById('new-max').value),
        webhook_url: document.getElementById('new-url').value,
        cooldown_ms: parseInt(document.getElementById('new-cooldown').value),
        enabled: true
      };

      fetch('/api/alarm/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(alarm)
      })
      .then(r => r.text())
      .then(msg => {
        alert(msg);
        refreshList();
        document.getElementById('new-id').value = '';
        document.getElementById('new-name').value = '';
      });
    }

    function deleteAlarm(id) {
      if (confirm('Delete alarm: ' + id + '?')) {
        fetch('/api/alarm/delete', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({id: id})
        })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshList();
        });
      }
    }

    updateSensorIdField();
    refreshList();
  </script>
</body>
</html>
)rawliteral";

  return html;
}

String generateWebhookConfigHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Webhook Configuration</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
    .container { max-width: 1200px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    h1 { color: #333; text-align: center; }
    .nav { margin-bottom: 20px; }
    .nav a { text-decoration: none; color: white; background-color: #2196F3; padding: 10px 20px; border-radius: 5px; margin: 5px; display: inline-block; }
    table { width: 100%; border-collapse: collapse; margin: 10px 0; }
    th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ddd; }
    th { background-color: #9C27B0; color: white; }
    button { background-color: #4CAF50; color: white; border: none; padding: 8px 16px; cursor: pointer; border-radius: 4px; margin: 2px; }
    button:hover { opacity: 0.8; }
    button.danger { background-color: #f44336; }
    button.primary { background-color: #2196F3; }
    .form-group { margin: 10px 0; }
    label { display: inline-block; width: 150px; font-weight: bold; }
    input, select { padding: 8px; border: 1px solid #ddd; border-radius: 4px; width: 300px; }
    .add-form { background-color: #f9f9f9; padding: 20px; border-radius: 5px; margin: 20px 0; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
      <a href="/">← Dashboard</a>
      <a href="/config">Sensors</a>
      <a href="/config/alarms">Alarms</a>
    </div>

    <h1>🔗 Webhook Configuration</h1>

    <div class="add-form">
      <h2>Add New Webhook</h2>
      <div class="form-group">
        <label>Webhook ID:</label>
        <input type="text" id="new-id" placeholder="slack_updates">
      </div>
      <div class="form-group">
        <label>Name:</label>
        <input type="text" id="new-name" placeholder="Slack Updates">
      </div>
      <div class="form-group">
        <label>Webhook URL:</label>
        <input type="text" id="new-url" placeholder="https://hooks.slack.com/services/YOUR/WEBHOOK">
      </div>
      <div class="form-group">
        <label>Update Interval (ms):</label>
        <input type="number" id="new-interval" value="0">
        <small>(0 = disabled, 300000 = 5 min, 3600000 = 1 hour)</small>
      </div>
      <button class="primary" onclick="addWebhook()">Add Webhook</button>
    </div>

    <h2>Configured Webhooks</h2>
    <button onclick="refreshList()">Refresh List</button>
    <div id="webhook-list"></div>
  </div>

  <script>
    function refreshList() {
      fetch('/api/webhooks')
        .then(r => r.json())
        .then(data => {
          let html = '<table><tr><th>ID</th><th>Name</th><th>URL</th><th>Update Interval</th><th>Status</th><th>Actions</th></tr>';
          data.webhooks.forEach(w => {
            let interval = w.update_interval_ms === 0 ? 'Disabled' : (w.update_interval_ms / 1000) + 's';
            html += '<tr>';
            html += '<td>' + w.id + '</td>';
            html += '<td>' + w.name + '</td>';
            html += '<td style="max-width:300px;overflow:hidden;text-overflow:ellipsis;">' + w.url + '</td>';
            html += '<td>' + interval + '</td>';
            html += '<td>' + (w.enabled ? '✅' : '❌') + '</td>';
            html += '<td><button class="danger" onclick="deleteWebhook(\'' + w.id + '\')">Delete</button></td>';
            html += '</tr>';
          });
          html += '</table>';
          document.getElementById('webhook-list').innerHTML = html;
        });
    }

    function addWebhook() {
      const webhook = {
        id: document.getElementById('new-id').value,
        name: document.getElementById('new-name').value,
        url: document.getElementById('new-url').value,
        update_interval_ms: parseInt(document.getElementById('new-interval').value),
        enabled: true
      };

      fetch('/api/webhook/add', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(webhook)
      })
      .then(r => r.text())
      .then(msg => {
        alert(msg);
        refreshList();
        document.getElementById('new-id').value = '';
        document.getElementById('new-name').value = '';
        document.getElementById('new-url').value = '';
        document.getElementById('new-interval').value = '0';
      });
    }

    function deleteWebhook(id) {
      if (confirm('Delete webhook: ' + id + '?')) {
        fetch('/api/webhook/delete', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({id: id})
        })
        .then(r => r.text())
        .then(msg => {
          alert(msg);
          refreshList();
        });
      }
    }

    refreshList();
  </script>
</body>
</html>
)rawliteral";

  return html;
}
