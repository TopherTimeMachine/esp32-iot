# ESP32 Web Server with Sensors and Webhooks

Complete ESP32 code for creating a web service that can:
- Display sensor readings (temperature, humidity, motion, light)
- Control outputs (relay, LED)
- Send webhook notifications when events occur
- Provide REST API endpoints for integration
- **NEW:** Configure sensors dynamically via web interface
- **NEW:** Support for DS18B20 waterproof temperature sensors

## Which Version Should I Use?

### ⭐ Configurable Version (`esp32_configurable_sensors.ino`) - RECOMMENDED
**Best for:** Waterproof temperature sensing, pool monitoring, outdoor applications, multi-sensor deployments

**Choose this if you need:**
- Waterproof DS18B20 temperature sensors
- Multiple temperature sensors (up to 8 on one bus)
- Web-based sensor configuration
- Custom sensor IDs and names
- Persistent storage of settings
- Full sensor suite (DHT22, motion, light, relay, LED)

### Full Version (`esp32_webserver_webhook.ino`)
**Best for:** Complete home automation, indoor monitoring

**Choose this if you need:**
- DHT22 temperature/humidity sensor only
- Motion detection
- Light level monitoring
- Relay and LED control
- Webhook notifications
- Simple, fixed configuration

### Simple Version (`esp32_simple_webserver.ino`)
**Best for:** Beginners, learning, minimal setups

**Choose this if you need:**
- One temperature sensor
- One relay
- Basic web interface
- Minimal complexity

## Hardware Requirements

### Configurable Version (esp32_configurable_sensors.ino) - RECOMMENDED
- ESP32 development board (ESP32 DevKit V1 or similar)
- **DS18B20 waterproof temperature sensors** (1-8 sensors on one bus)
- 4.7kΩ resistor (pull-up for OneWire bus)
- DHT22 temperature/humidity sensor
- PIR motion sensor (HC-SR501 or similar)
- LDR (Light Dependent Resistor) with 10kΩ resistor
- Relay module (5V)
- LED with 220Ω resistor
- Breadboard and jumper wires

### Full Version (esp32_webserver_webhook.ino)
- ESP32 development board (ESP32 DevKit V1 or similar)
- DHT22 temperature/humidity sensor
- PIR motion sensor (HC-SR501 or similar)
- LDR (Light Dependent Resistor) with 10kΩ resistor
- Relay module (5V)
- LED with 220Ω resistor
- Breadboard and jumper wires

### Simple Version (esp32_simple_webserver.ino)
- ESP32 development board
- Temperature sensor (analog or DHT11)
- Relay module
- Basic components

## Pin Connections

### Configurable Version
| Component | ESP32 Pin |
|-----------|-----------|
| DS18B20 Data (OneWire) | GPIO 13 |
| DHT22 Data | GPIO 15 |
| PIR Sensor | GPIO 14 |
| LDR | GPIO 34 (ADC) |
| Relay | GPIO 26 |
| LED | GPIO 27 |

### Full Version
| Component | ESP32 Pin |
|-----------|-----------|
| DHT22 Data | GPIO 15 |
| PIR Sensor | GPIO 14 |
| LDR | GPIO 34 (ADC) |
| Relay | GPIO 26 |
| LED | GPIO 27 |

### DS18B20 OneWire Circuit (Configurable Version)
```
3.3V ---- 4.7kΩ Resistor ---- GPIO 13 (Data)
                |
         DS18B20 Sensor(s) ---- GND
```
**Note:** Multiple DS18B20 sensors can share the same OneWire bus

### LDR Circuit
```
3.3V ---- LDR ---- GPIO 34 (ADC) ---- 10kΩ Resistor ---- GND
```

## Software Requirements

### Arduino IDE Setup
1. Install Arduino IDE (version 1.8.x or 2.x)
2. Add ESP32 board support:
   - Go to File → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to Tools → Board → Board Manager
   - Search for "esp32" and install "ESP32 by Espressif Systems"

### Required Libraries
Install these via Library Manager (Sketch → Include Library → Manage Libraries):

**For Configurable Version:**
1. **DHT sensor library** by Adafruit (version 1.4.x or newer)
2. **Adafruit Unified Sensor** (dependency for DHT)
3. **ArduinoJson** by Benoit Blanchon (version 6.x)
4. **OneWire** by Paul Stoffregen (for DS18B20)
5. **DallasTemperature** by Miles Burton (for DS18B20)

**For Full/Simple Versions:**
1. **DHT sensor library** by Adafruit (version 1.4.x or newer)
2. **Adafruit Unified Sensor** (dependency for DHT)
3. **ArduinoJson** by Benoit Blanchon (version 6.x)

Built-in libraries (no installation needed):
- WiFi
- WebServer
- HTTPClient
- Preferences (for configurable version)

## Configuration

### 1. WiFi Setup
Edit these lines in the code:
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### 2. Webhook Configuration

#### Option A: IFTTT (Recommended for beginners)
1. Create account at https://ifttt.com
2. Go to https://ifttt.com/maker_webhooks
3. Click "Documentation" to get your key
4. Create an applet:
   - "If This" → Webhooks → "Receive a web request"
   - Event name: `motion_detected` (or your choice)
   - "Then That" → Choose action (Email, SMS, Notification, etc.)
5. Update code:
```cpp
const char* webhookURL = "https://maker.ifttt.com/trigger/YOUR_EVENT/with/key/YOUR_KEY";
```

#### Option B: Custom Webhook
```cpp
const char* webhookURL = "http://192.168.1.100:1880/webhook"; // Your server
```

#### Option C: Slack Webhook
1. Create Slack incoming webhook
2. Use URL:
```cpp
const char* webhookURL = "https://hooks.slack.com/services/YOUR/WEBHOOK/URL";
```

### 3. Thresholds
Adjust these in the code:
```cpp
#define TEMP_THRESHOLD 30.0      // Temperature alert threshold (°C)
#define MOTION_COOLDOWN 60000    // Time between motion alerts (ms)
```

## Upload and Run

1. Connect ESP32 to computer via USB
2. Select board: Tools → Board → ESP32 Arduino → ESP32 Dev Module
3. Select port: Tools → Port → (your COM port)
4. Click Upload button
5. Open Serial Monitor (115200 baud)
6. Press EN button on ESP32 to see IP address

## API Endpoints

### Common Endpoints (All Versions)

#### GET /
Returns the main web dashboard with live updating sensor display

#### GET /relay?state=on
#### GET /relay?state=off
Controls the relay
- Response: "Relay ON" or "Relay OFF"

#### GET /led?state=on
#### GET /led?state=off
Controls the LED
- Response: "LED ON" or "LED OFF"

### Configurable Version Endpoints

#### GET /data
Returns JSON with all sensor readings including DS18B20 sensors
```json
{
  "dht": {
    "temperature": 25.5,
    "humidity": 60.2
  },
  "ds18b20": [
    {
      "id": "pool_temp",
      "name": "Pool Temperature",
      "temperature": 28.3,
      "address": "28:AA:BB:CC:DD:EE:FF:00"
    },
    {
      "id": "outdoor_temp",
      "name": "Outdoor Temperature",
      "temperature": 22.1,
      "address": "28:11:22:33:44:55:66:77"
    }
  ],
  "motion": 0,
  "light": 75,
  "relay": false,
  "led": false
}
```

#### GET /config
Web interface for sensor configuration

#### GET /scan-ds18b20
Scans the OneWire bus for DS18B20 sensors
```json
{
  "count": 2,
  "discovered": [
    "28:AA:BB:CC:DD:EE:FF:00",
    "28:11:22:33:44:55:66:77"
  ]
}
```

#### POST /add-ds18b20
Add a new DS18B20 sensor to configuration
```json
{
  "address": "28:AA:BB:CC:DD:EE:FF:00",
  "id": "pool_temp",
  "name": "Pool Temperature"
}
```

#### POST /update-ds18b20
Update sensor name or settings
```json
{
  "id": "pool_temp",
  "name": "Swimming Pool Temperature"
}
```

#### POST /remove-ds18b20
Remove a sensor from configuration
```json
{
  "address": "28:AA:BB:CC:DD:EE:FF:00"
}
```

#### GET /list-ds18b20
List all configured DS18B20 sensors
```json
{
  "sensors": [
    {
      "id": "pool_temp",
      "name": "Pool Temperature",
      "address": "28:AA:BB:CC:DD:EE:FF:00",
      "enabled": true,
      "temperature": 28.3
    }
  ]
}
```

### Full Version Endpoints

#### GET /data
Returns JSON with all sensor readings
```json
{
  "temperature": 25.5,
  "humidity": 60.2,
  "motion": 0,
  "light": 75,
  "relay": false,
  "led": false
}
```

## Usage Examples

### Access Web Dashboard
Open browser and navigate to: `http://ESP32_IP_ADDRESS`

### Access Configuration Interface (Configurable Version)
Open browser and navigate to: `http://ESP32_IP_ADDRESS/config`

### Read Sensors via API
```bash
# Configurable version (includes DS18B20 sensors)
curl http://192.168.1.XXX/data

# Get specific sensor by ID (parse JSON response)
curl http://192.168.1.XXX/data | jq '.ds18b20[] | select(.id=="pool_temp")'
```

### Manage DS18B20 Sensors (Configurable Version)
```bash
# Scan for available sensors
curl http://192.168.1.XXX/scan-ds18b20

# Add a new sensor
curl -X POST http://192.168.1.XXX/add-ds18b20 \
  -H "Content-Type: application/json" \
  -d '{"address":"28:AA:BB:CC:DD:EE:FF:00","id":"pool_temp","name":"Pool Temperature"}'

# Update sensor name
curl -X POST http://192.168.1.XXX/update-ds18b20 \
  -H "Content-Type: application/json" \
  -d '{"id":"pool_temp","name":"Swimming Pool Temp"}'

# Remove a sensor
curl -X POST http://192.168.1.XXX/remove-ds18b20 \
  -H "Content-Type: application/json" \
  -d '{"address":"28:AA:BB:CC:DD:EE:FF:00"}'

# List all configured sensors
curl http://192.168.1.XXX/list-ds18b20
```

### Control Relay via API
```bash
curl http://192.168.1.XXX/relay?state=on
curl http://192.168.1.XXX/relay?state=off
```

### Integration with Home Assistant

**For Configurable Version (DS18B20 sensors):**
```yaml
# REST sensor for DS18B20 pool temperature
sensor:
  - platform: rest
    resource: http://192.168.1.XXX/data
    name: Pool Temperature
    value_template: '{{ value_json.ds18b20 | selectattr("id", "equalto", "pool_temp") | map(attribute="temperature") | first }}'
    unit_of_measurement: "°C"

  - platform: rest
    resource: http://192.168.1.XXX/data
    name: DHT Temperature
    value_template: '{{ value_json.dht.temperature }}'
    unit_of_measurement: "°C"

  - platform: rest
    resource: http://192.168.1.XXX/data
    name: DHT Humidity
    value_template: '{{ value_json.dht.humidity }}'
    unit_of_measurement: "%"

switch:
  - platform: rest
    resource: http://192.168.1.XXX/relay
    name: ESP32 Relay
    body_on: 'state=on'
    body_off: 'state=off'
```

**For Full Version:**
```yaml
sensor:
  - platform: rest
    resource: http://192.168.1.XXX/data
    name: ESP32 Sensors
    json_attributes:
      - temperature
      - humidity
      - light
      - motion
    value_template: '{{ value_json.temperature }}'

switch:
  - platform: rest
    resource: http://192.168.1.XXX/relay
    name: ESP32 Relay
    body_on: 'state=on'
    body_off: 'state=off'
```

### Integration with Node-RED
1. Use HTTP Request node
2. Set URL: `http://ESP32_IP/data`
3. Method: GET
4. Returns JSON object with sensor data

## Webhook Events

The code automatically sends webhooks for these events:

### 1. Motion Detected
Triggers when PIR sensor detects motion
```json
{
  "event": "motion_detected",
  "value1": "Motion sensor triggered",
  "value2": "25.5",  // Current temperature
  "value3": "60.2"   // Current humidity
}
```

### 2. High Temperature
Triggers when temperature exceeds threshold (checked every 5 minutes)
```json
{
  "event": "high_temperature",
  "value1": "31.5",  // Current temperature
  "value2": "65.0",  // Current humidity
  "value3": "Temperature threshold exceeded"
}
```

### 3. Relay Changed
Triggers when relay state changes
```json
{
  "event": "relay_changed",
  "value1": "Relay turned ON",
  "value2": "",
  "value3": ""
}
```

## Customization

### Add More DS18B20 Sensors (Configurable Version)
The configurable version supports up to 8 DS18B20 sensors on the same OneWire bus:
1. Connect new DS18B20 sensor to the same GPIO 13 bus (parallel connection)
2. Navigate to `http://ESP32_IP/config`
3. Click "Scan Bus" to discover the new sensor
4. Click "Add Sensor" and give it an ID and name
5. The sensor will automatically appear on the dashboard

### Change Maximum Sensor Count
Edit in code:
```cpp
#define MAX_DS18B20_SENSORS 16  // Increase from 8 to 16
```

### Add More Sensors (Other Types)
1. Define new pin:
```cpp
#define SENSOR_PIN 32
```

2. Read in `readSensors()`:
```cpp
int sensorValue = analogRead(SENSOR_PIN);
```

3. Add to JSON in `handleSensorData()`:
```cpp
doc["sensor"] = sensorValue;
```

4. Update HTML to display new sensor

### Change Update Frequency
In the HTML JavaScript section:
```javascript
setInterval(updateSensorData, 2000); // 2000ms = 2 seconds
```

### Add Authentication
Add basic auth to server setup:
```cpp
server.on("/", []() {
  if (!server.authenticate("admin", "password")) {
    return server.requestAuthentication();
  }
  handleRoot();
});
```

## Troubleshooting

### WiFi Won't Connect
- Check SSID and password
- Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- Move closer to router

### Sensor Reads NaN or 0
- Check wiring connections
- Verify correct sensor library is installed
- Try different GPIO pins
- Check sensor power supply (3.3V for most sensors)

### Webhooks Not Sending
- Check webhook URL is correct
- Verify internet connection
- Check Serial Monitor for error codes
- Test webhook URL in browser or Postman first

### Web Page Not Loading
- Verify ESP32 IP address from Serial Monitor
- Check that device is on same WiFi network
- Try pinging the ESP32 IP address
- Check firewall settings

### Compilation Errors
- Verify all libraries are installed
- Check ESP32 board package is up to date
- Ensure correct board is selected

## Security Considerations

1. **Change Default Credentials**: Never use default WiFi passwords in production
2. **Use HTTPS**: For production webhooks, use HTTPS endpoints
3. **Add Authentication**: Implement authentication for control endpoints
4. **Local Network Only**: Keep ESP32 on local network, don't expose to internet without proper security
5. **Rate Limiting**: Consider adding rate limits to prevent abuse

## Power Consumption Tips

- Use deep sleep between readings for battery operation
- Disable WiFi when not needed
- Reduce sensor polling frequency
- Use ESP32's power management features

## License

This code is provided as-is for educational and personal use. Feel free to modify and adapt for your projects.

## Additional Resources

- [ESP32 Official Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [Random Nerd Tutorials - ESP32](https://randomnerdtutorials.com/projects-esp32/)
- [IFTTT Webhooks Documentation](https://ifttt.com/maker_webhooks)
- [ArduinoJson Documentation](https://arduinojson.org/)

## Support

For issues or questions:
1. Check the Serial Monitor output for error messages
2. Verify all connections and component specifications
3. Test individual components separately
4. Review the example code and comments
