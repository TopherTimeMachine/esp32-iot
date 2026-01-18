/*
 * ESP32 Web Server with Sensors and Webhook Support
 * 
 * Features:
 * - Web server to display sensor readings (temperature, humidity, motion, light)
 * - REST API endpoints for accessing sensor data
 * - Control outputs (relay, LED)
 * - Send webhook notifications when events occur
 * - Auto-updating web interface using AJAX
 * 
 * Hardware Setup:
 * - DHT22 sensor on GPIO 15 (temperature & humidity)
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

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Webhook configuration
const char* webhookURL = "https://maker.ifttt.com/trigger/YOUR_EVENT/with/key/YOUR_IFTTT_KEY";
// Alternative webhook URL examples:
// const char* webhookURL = "http://192.168.1.100:1880/webhook"; // Node-RED
// const char* webhookURL = "https://hooks.slack.com/services/YOUR/WEBHOOK/URL"; // Slack

// Sensor pins
#define DHTPIN 15
#define DHTTYPE DHT22
#define PIR_PIN 14
#define LDR_PIN 34

// Output pins
#define RELAY_PIN 26
#define LED_PIN 27

// Thresholds for webhook triggers
#define TEMP_THRESHOLD 30.0  // Trigger webhook if temp exceeds this
#define MOTION_COOLDOWN 60000 // 1 minute cooldown between motion webhooks

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// Global variables
float temperature = 0;
float humidity = 0;
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
void handleNotFound();
void sendWebhook(String event, String value1, String value2, String value3);
void readSensors();
String generateHTML();

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
  server.onNotFound(handleNotFound);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  readSensors();
  
  // Check for motion and send webhook if detected (with cooldown)
  if (motionDetected && (millis() - lastMotionWebhook > MOTION_COOLDOWN)) {
    Serial.println("Motion detected! Sending webhook...");
    sendWebhook("motion_detected", 
                "Motion sensor triggered", 
                String(temperature), 
                String(humidity));
    lastMotionWebhook = millis();
  }
  
  // Check for high temperature and send webhook
  static unsigned long lastTempCheck = 0;
  if (millis() - lastTempCheck > 300000) { // Check every 5 minutes
    if (temperature > TEMP_THRESHOLD) {
      Serial.println("High temperature! Sending webhook...");
      sendWebhook("high_temperature", 
                  String(temperature), 
                  String(humidity), 
                  "Temperature threshold exceeded");
    }
    lastTempCheck = millis();
  }
  
  delay(100); // Small delay to prevent overwhelming the system
}

void readSensors() {
  // Read DHT22 (temperature & humidity)
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
  }
  
  // Read PIR motion sensor
  motionDetected = digitalRead(PIR_PIN);
  
  // Read LDR (light sensor) - convert ADC value to percentage
  int rawLight = analogRead(LDR_PIN);
  lightLevel = map(rawLight, 0, 4095, 0, 100);
}

void handleRoot() {
  String html = generateHTML();
  server.send(200, "text/html", html);
}

void handleSensorData() {
  // Create JSON response with all sensor data
  StaticJsonDocument<200> doc;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
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
      server.send(400, "text/plain", "Invalid state. Use 'on' or 'off'");
    }
  } else {
    server.send(400, "text/plain", "Missing 'state' parameter");
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
      server.send(400, "text/plain", "Invalid state. Use 'on' or 'off'");
    }
  } else {
    server.send(400, "text/plain", "Missing 'state' parameter");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not found");
}

void sendWebhook(String event, String value1, String value2, String value3) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // For IFTTT webhooks, format the URL with values
    String url = String(webhookURL);
    
    // If using IFTTT format, add query parameters
    if (url.indexOf("ifttt.com") > 0) {
      url += "?value1=" + value1 + "&value2=" + value2 + "&value3=" + value3;
      http.begin(url);
      int httpResponseCode = http.GET();
      Serial.print("Webhook response code: ");
      Serial.println(httpResponseCode);
    } else {
      // For generic webhooks, send JSON POST
      http.begin(url);
      http.addHeader("Content-Type", "application/json");
      
      StaticJsonDocument<300> doc;
      doc["event"] = event;
      doc["value1"] = value1;
      doc["value2"] = value2;
      doc["value3"] = value3;
      doc["device"] = "ESP32";
      doc["timestamp"] = millis();
      
      String json;
      serializeJson(doc, json);
      
      int httpResponseCode = http.POST(json);
      Serial.print("Webhook response code: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  } else {
    Serial.println("WiFi not connected - webhook not sent");
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
    body {
      font-family: Arial, sans-serif;
      margin: 20px;
      background-color: #f0f0f0;
    }
    .container {
      max-width: 800px;
      margin: 0 auto;
      background-color: white;
      padding: 20px;
      border-radius: 10px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    }
    h1 {
      color: #333;
      text-align: center;
    }
    .sensor-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 15px;
      margin: 20px 0;
    }
    .sensor-card {
      background-color: #f9f9f9;
      border: 1px solid #ddd;
      border-radius: 8px;
      padding: 15px;
      text-align: center;
    }
    .sensor-label {
      font-size: 14px;
      color: #666;
      margin-bottom: 5px;
    }
    .sensor-value {
      font-size: 28px;
      font-weight: bold;
      color: #2196F3;
    }
    .control-section {
      margin-top: 30px;
    }
    .button {
      background-color: #4CAF50;
      color: white;
      border: none;
      padding: 12px 24px;
      text-align: center;
      font-size: 16px;
      margin: 5px;
      cursor: pointer;
      border-radius: 5px;
      transition: 0.3s;
    }
    .button:hover {
      opacity: 0.8;
    }
    .button-off {
      background-color: #f44336;
    }
    .status-indicator {
      display: inline-block;
      width: 12px;
      height: 12px;
      border-radius: 50%;
      margin-left: 5px;
    }
    .status-on {
      background-color: #4CAF50;
    }
    .status-off {
      background-color: #ccc;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32 Sensor Dashboard</h1>
    
    <div class="sensor-grid">
      <div class="sensor-card">
        <div class="sensor-label">Temperature</div>
        <div class="sensor-value" id="temperature">--</div>
        <div class="sensor-label">°C</div>
      </div>
      
      <div class="sensor-card">
        <div class="sensor-label">Humidity</div>
        <div class="sensor-value" id="humidity">--</div>
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
        .then(response => response.json())
        .then(data => {
          document.getElementById('temperature').textContent = data.temperature.toFixed(1);
          document.getElementById('humidity').textContent = data.humidity.toFixed(1);
          document.getElementById('light').textContent = data.light;
          document.getElementById('motion').textContent = data.motion ? 'DETECTED' : 'Clear';
          
          // Update status indicators
          document.getElementById('relay-status').className = 
            'status-indicator ' + (data.relay ? 'status-on' : 'status-off');
          document.getElementById('led-status').className = 
            'status-indicator ' + (data.led ? 'status-on' : 'status-off');
        })
        .catch(error => console.error('Error:', error));
    }
    
    function controlRelay(state) {
      fetch('/relay?state=' + state)
        .then(response => response.text())
        .then(data => {
          console.log(data);
          updateSensorData();
        })
        .catch(error => console.error('Error:', error));
    }
    
    function controlLED(state) {
      fetch('/led?state=' + state)
        .then(response => response.text())
        .then(data => {
          console.log(data);
          updateSensorData();
        })
        .catch(error => console.error('Error:', error));
    }
    
    // Update sensor data every 2 seconds
    setInterval(updateSensorData, 2000);
    
    // Initial update
    updateSensorData();
  </script>
</body>
</html>
)rawliteral";
  
  return html;
}
