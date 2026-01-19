/*
 * ESP32 Configurable IoT Sensor Hub with DS18B20 Support
 *
 * Features:
 * - DS18B20 waterproof temperature sensor support (multiple sensors on one bus)
 * - DHT22 temperature & humidity sensor
 * - PIR motion sensor
 * - LDR light sensor
 * - Relay and LED control
 * - Dynamic sensor configuration via web interface
 * - Persistent storage of sensor IDs and names
 * - REST API for all operations
 * - Webhook notifications
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

// Webhook configuration
const char* webhookURL = "https://maker.ifttt.com/trigger/YOUR_EVENT/with/key/YOUR_IFTTT_KEY";

// Sensor pins
#define ONE_WIRE_BUS 13  // DS18B20 sensors
#define DHTPIN 15
#define DHTTYPE DHT22
#define PIR_PIN 14
#define LDR_PIN 34

// Output pins
#define RELAY_PIN 26
#define LED_PIN 27

// Thresholds for webhook triggers
#define TEMP_THRESHOLD 30.0
#define MOTION_COOLDOWN 60000

// Maximum number of DS18B20 sensors
#define MAX_DS18B20_SENSORS 8

// OneWire and DallasTemperature setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20Sensors(&oneWire);

// DHT sensor
DHT dht(DHTPIN, DHTTYPE);

// Web server
WebServer server(80);

// Persistent storage
Preferences preferences;

// Sensor structure for DS18B20
struct DS18B20Sensor {
  uint8_t address[8];
  char id[32];
  char name[64];
  bool enabled;
  float lastReading;
};

// Global variables
DS18B20Sensor ds18b20List[MAX_DS18B20_SENSORS];
int ds18b20Count = 0;

float dhtTemperature = 0;
float dhtHumidity = 0;
int motionDetected = 0;
int lightLevel = 0;
bool relayState = false;
bool ledState = false;
unsigned long lastMotionWebhook = 0;

// Function prototypes
void handleRoot();
void handleSensorData();
void handleRelay();
void handleLED();
void handleConfig();
void handleScanDS18B20();
void handleAddDS18B20();
void handleRemoveDS18B20();
void handleListDS18B20();
void handleUpdateDS18B20();
void handleNotFound();
void sendWebhook(String event, String value1, String value2, String value3);
void readSensors();
void loadConfiguration();
void saveConfiguration();
void scanDS18B20Sensors();
String generateHTML();
String addressToString(uint8_t* addr);
void stringToAddress(String str, uint8_t* addr);

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
  preferences.begin("sensor-config", false);

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
  server.on("/scan-ds18b20", handleScanDS18B20);
  server.on("/add-ds18b20", HTTP_POST, handleAddDS18B20);
  server.on("/remove-ds18b20", HTTP_POST, handleRemoveDS18B20);
  server.on("/list-ds18b20", handleListDS18B20);
  server.on("/update-ds18b20", HTTP_POST, handleUpdateDS18B20);
  server.onNotFound(handleNotFound);

  // Start server
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Access configuration at: http://" + WiFi.localIP().toString() + "/config");
}

void loop() {
  server.handleClient();
  readSensors();

  // Check for motion and send webhook if detected (with cooldown)
  if (motionDetected && (millis() - lastMotionWebhook > MOTION_COOLDOWN)) {
    Serial.println("Motion detected! Sending webhook...");
    sendWebhook("motion_detected",
                "Motion sensor triggered",
                String(dhtTemperature),
                String(dhtHumidity));
    lastMotionWebhook = millis();
  }

  // Check for high temperature and send webhook
  static unsigned long lastTempCheck = 0;
  if (millis() - lastTempCheck > 300000) { // Check every 5 minutes
    if (dhtTemperature > TEMP_THRESHOLD) {
      Serial.println("High temperature! Sending webhook...");
      sendWebhook("high_temperature",
                  String(dhtTemperature),
                  String(dhtHumidity),
                  "Temperature threshold exceeded");
    }
    lastTempCheck = millis();
  }

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
  ds18b20Count = preferences.getInt("ds18b20Count", 0);
  Serial.print("Loading ");
  Serial.print(ds18b20Count);
  Serial.println(" DS18B20 sensors from storage");

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
}

void saveConfiguration() {
  preferences.putInt("ds18b20Count", ds18b20Count);

  for (int i = 0; i < ds18b20Count; i++) {
    String prefix = "ds" + String(i) + "_";

    preferences.putString((prefix + "addr").c_str(), addressToString(ds18b20List[i].address));
    preferences.putString((prefix + "id").c_str(), String(ds18b20List[i].id));
    preferences.putString((prefix + "name").c_str(), String(ds18b20List[i].name));
    preferences.putBool((prefix + "en").c_str(), ds18b20List[i].enabled);
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
    pos += 3; // Skip the colon
  }
}

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
      sendWebhook("relay_changed", "Relay turned ON", "", "");
    } else if (state == "off") {
      digitalWrite(RELAY_PIN, LOW);
      relayState = false;
      server.send(200, "text/plain", "Relay OFF");
      sendWebhook("relay_changed", "Relay turned OFF", "", "");
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
    .nav a { text-decoration: none; color: #2196F3; margin-right: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <div class="nav">
      <a href="/">← Back to Dashboard</a>
    </div>

    <h1>Sensor Configuration</h1>

    <div class="section">
      <h2>DS18B20 Temperature Sensors</h2>
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
      fetch('/list-ds18b20')
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
      fetch('/scan-ds18b20')
        .then(r => r.json())
        .then(data => {
          alert('Scan complete! Found ' + data.count + ' sensor(s)');
          refreshList();
        });
    }

    function scanAvailable() {
      fetch('/scan-ds18b20')
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
        fetch('/add-ds18b20', {
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
      fetch('/update-ds18b20', {
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
        fetch('/remove-ds18b20', {
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
        // Shift all sensors down
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

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}

void sendWebhook(String event, String value1, String value2, String value3) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(webhookURL);

    if (url.indexOf("ifttt.com") > 0) {
      url += "?value1=" + value1 + "&value2=" + value2 + "&value3=" + value3;
      http.begin(url);
      int httpResponseCode = http.GET();
      Serial.print("Webhook response: ");
      Serial.println(httpResponseCode);
    } else {
      http.begin(url);
      http.addHeader("Content-Type", "application/json");

      DynamicJsonDocument doc(512);
      doc["event"] = event;
      doc["value1"] = value1;
      doc["value2"] = value2;
      doc["value3"] = value3;
      doc["device"] = "ESP32";
      doc["timestamp"] = millis();

      String json;
      serializeJson(doc, json);

      int httpResponseCode = http.POST(json);
      Serial.print("Webhook response: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  }
}

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
    .nav a { text-decoration: none; color: white; background-color: #2196F3; padding: 10px 20px; border-radius: 5px; margin: 0 10px; }
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
      <a href="/config">⚙️ Configuration</a>
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
          // Update DHT sensors
          document.getElementById('dht-temp').textContent = data.dht.temperature.toFixed(1);
          document.getElementById('dht-hum').textContent = data.dht.humidity.toFixed(1);
          document.getElementById('light').textContent = data.light;
          document.getElementById('motion').textContent = data.motion ? 'DETECTED' : 'Clear';

          // Update DS18B20 sensors
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
            ds18b20HTML = '<div class="sensor-card"><div class="sensor-label">No DS18B20 sensors configured</div><div class="sensor-value">--</div></div>';
          }
          document.getElementById('ds18b20-grid').innerHTML = ds18b20HTML;

          // Update status indicators
          document.getElementById('relay-status').className = 'status-indicator ' + (data.relay ? 'status-on' : 'status-off');
          document.getElementById('led-status').className = 'status-indicator ' + (data.led ? 'status-on' : 'status-off');
        })
        .catch(error => console.error('Error:', error));
    }

    function controlRelay(state) {
      fetch('/relay?state=' + state)
        .then(r => r.text())
        .then(data => {
          console.log(data);
          updateSensorData();
        });
    }

    function controlLED(state) {
      fetch('/led?state=' + state)
        .then(r => r.text())
        .then(data => {
          console.log(data);
          updateSensorData();
        });
    }

    setInterval(updateSensorData, 2000);
    updateSensorData();
  </script>
</body>
</html>
)rawliteral";

  return html;
}
