/*
 * ESP32 Simple Web Server with Single Sensor
 * 
 * This is a simplified version for beginners
 * Features:
 * - Temperature sensor (DS18B20 or DHT11)
 * - Single relay control
 * - Basic web interface
 * - Simple webhook on temperature threshold
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Webhook URL (optional - leave blank to disable)
const char* webhookURL = ""; // Add your webhook URL here

// Pin definitions
#define TEMP_SENSOR_PIN 4  // Analog temperature sensor
#define RELAY_PIN 26

// Settings
#define TEMP_THRESHOLD 28.0

WebServer server(80);
float temperature = 0;
bool relayState = false;

void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(TEMP_SENSOR_PIN, INPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Setup routes
  server.on("/", []() {
    String html = getHTML();
    server.send(200, "text/html", html);
  });
  
  server.on("/data", []() {
    String json = "{\"temperature\":" + String(temperature) + 
                  ",\"relay\":" + String(relayState ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });
  
  server.on("/relay", []() {
    if (server.hasArg("state")) {
      String state = server.arg("state");
      relayState = (state == "on");
      digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
      server.send(200, "text/plain", relayState ? "ON" : "OFF");
    } else {
      server.send(400, "text/plain", "Missing state parameter");
    }
  });
  
  server.begin();
  Serial.println("Server started");
}

void loop() {
  server.handleClient();
  
  // Read temperature (simplified - adjust for your sensor)
  int rawValue = analogRead(TEMP_SENSOR_PIN);
  temperature = (rawValue / 4095.0) * 50.0; // Scale to 0-50°C
  
  // Check threshold and send webhook
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) { // Every minute
    if (temperature > TEMP_THRESHOLD && strlen(webhookURL) > 0) {
      sendWebhook();
    }
    lastCheck = millis();
  }
  
  delay(100);
}

void sendWebhook() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(webhookURL) + "?temp=" + String(temperature);
    http.begin(url);
    int code = http.GET();
    Serial.println("Webhook sent: " + String(code));
    http.end();
  }
}

String getHTML() {
  return R"(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Control</title>
  <style>
    body { font-family: Arial; text-align: center; margin: 20px; }
    .value { font-size: 48px; color: #2196F3; }
    button { padding: 15px 30px; font-size: 18px; margin: 10px; 
             cursor: pointer; border-radius: 5px; border: none; }
    .on { background-color: #4CAF50; color: white; }
    .off { background-color: #f44336; color: white; }
  </style>
</head>
<body>
  <h1>ESP32 Dashboard</h1>
  <h2>Temperature</h2>
  <div class="value" id="temp">--</div>
  <p>°C</p>
  
  <h2>Relay Control</h2>
  <button class="on" onclick="control('on')">ON</button>
  <button class="off" onclick="control('off')">OFF</button>
  
  <script>
    function update() {
      fetch('/data')
        .then(r => r.json())
        .then(d => {
          document.getElementById('temp').textContent = d.temperature.toFixed(1);
        });
    }
    
    function control(state) {
      fetch('/relay?state=' + state).then(update);
    }
    
    setInterval(update, 2000);
    update();
  </script>
</body>
</html>
  )";
}
