#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// WiFi credentials
const char* ssid = "MySpyCar";
const char* password = "123456789";

// Pin definitions
#define MQ7_PIN 32
#define LED_PIN 18
#define BUZZER_PIN 5
#define SDA_PIN 21
#define SCL_PIN 22

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Web server and WebSocket
WebServer server(80);
WebSocketsServer webSocket(81);

// MQ7 sensor variables
float runningAverage = 0.0;
float alpha = 0.1; // Smoothing factor for running average
bool isCalibrated = false;
float baselineValue = 0.0;
unsigned long calibrationStartTime = 0;
const unsigned long CALIBRATION_TIME = 180000; // 3 minutes in milliseconds
const float CO_THRESHOLD = 75.0; // ppm threshold for Indian car emission standards

// Timing variables
unsigned long lastReadTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastWebSocketUpdate = 0;
const unsigned long READ_INTERVAL = 1000; // Read sensor every 1 second
const unsigned long DISPLAY_UPDATE_INTERVAL = 2000; // Update OLED every 2 seconds
const unsigned long WEBSOCKET_UPDATE_INTERVAL = 3000; // Update webpage every 3 seconds

// Alert variables
bool alertActive = false;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize pins
  pinMode(MQ7_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize I2C for OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("CO Monitor Starting...");
  display.display();
  delay(2000);
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Connecting WiFi...");
  display.display();
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("WiFi Connected!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
  
  // Initialize web server routes
  server.on("/", handleRoot);
  server.begin();
  
  // Initialize WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  
  Serial.println("Web server started");
  Serial.println("Starting MQ7 calibration (3 minutes)...");
  
  // Start calibration
  calibrationStartTime = millis();
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("Calibrating MQ7...");
  display.println("Please wait 3 min");
  display.display();
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Read MQ7 sensor
  if (millis() - lastReadTime >= READ_INTERVAL) {
    readMQ7Sensor();
    lastReadTime = millis();
  }
  
  // Update OLED display
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    updateOLEDDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Update webpage via WebSocket
  if (millis() - lastWebSocketUpdate >= WEBSOCKET_UPDATE_INTERVAL) {
    sendWebSocketData();
    lastWebSocketUpdate = millis();
  }
  
  // Handle alerts
  handleAlerts();
  
  delay(100);
}

void readMQ7Sensor() {
  int rawValue = analogRead(MQ7_PIN);
  float voltage = (rawValue * 3.3) / 4095.0;
  
  // Calculate running average
  if (runningAverage == 0.0) {
    runningAverage = rawValue;
  } else {
    runningAverage = (alpha * rawValue) + ((1 - alpha) * runningAverage);
  }
  
  // Calibration phase
  if (!isCalibrated) {
    if (millis() - calibrationStartTime >= CALIBRATION_TIME) {
      baselineValue = runningAverage;
      isCalibrated = true;
      Serial.println("Calibration complete!");
      Serial.print("Baseline value: ");
      Serial.println(baselineValue);
    }
    return;
  }
  
  // Calculate CO concentration in ppm (simplified conversion)
  float coPPM = calculateCOPPM(runningAverage);
  
  // Debug output to serial monitor
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print(" | Voltage: ");
  Serial.print(voltage, 2);
  Serial.print("V | Running Avg: ");
  Serial.print(runningAverage, 1);
  Serial.print(" | CO: ");
  Serial.print(coPPM, 1);
  Serial.println(" ppm");
  
  // Check threshold for alerts
  if (coPPM > CO_THRESHOLD) {
    alertActive = true;
  } else {
    alertActive = false;
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
}

float calculateCOPPM(float sensorValue) {
  if (!isCalibrated) return 0.0;
  
  // Simplified conversion formula for MQ7
  // This is a basic approximation - for precise measurements, you'd need proper calibration
  float ratio = sensorValue / baselineValue;
  float ppm = 0.0;
  
  if (ratio > 1.0) {
    // Basic exponential relationship for CO concentration
    ppm = pow(10, (log10(ratio) * 2.0 + 1.3));
    if (ppm > 2000) ppm = 2000; // Cap at sensor maximum
  }
  
  return ppm;
}

void updateOLEDDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  if (!isCalibrated) {
    unsigned long remainingTime = CALIBRATION_TIME - (millis() - calibrationStartTime);
    display.println("CALIBRATING...");
    display.print("Time left: ");
    display.print(remainingTime / 1000);
    display.println("s");
    display.print("Raw value: ");
    display.println((int)runningAverage);
  } else {
    float coPPM = calculateCOPPM(runningAverage);
    
    display.setTextSize(1);
    display.println("CO POLLUTION MONITOR");
    display.println("--------------------");
    
    display.setTextSize(1);
    display.print("CO Level: ");
    display.setTextSize(2);
    display.print(coPPM, 1);
    display.println(" ppm");
    
    display.setTextSize(1);
    display.print("Status: ");
    if (coPPM > CO_THRESHOLD) {
      display.println("DANGER!");
      display.println("Emission Limit");
      display.println("Exceeded!");
    } else {
      display.println("SAFE");
    }
    
    display.print("IP: ");
    display.println(WiFi.localIP());
  }
  
  display.display();
}

void handleAlerts() {
  if (alertActive) {
    // Blink LED
    digitalWrite(LED_PIN, (millis() / 500) % 2);
    
    // Beep buzzer intermittently
    if (millis() - lastBuzzerToggle >= 1000) {
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
      lastBuzzerToggle = millis();
    }
  }
}

void sendWebSocketData() {
  if (!isCalibrated) return;
  
  float coPPM = calculateCOPPM(runningAverage);
  
  StaticJsonDocument<200> doc;
  doc["co_ppm"] = coPPM;
  doc["raw_value"] = (int)runningAverage;
  doc["status"] = (coPPM > CO_THRESHOLD) ? "DANGER" : "SAFE";
  doc["threshold"] = CO_THRESHOLD;
  doc["alert"] = alertActive;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  webSocket.broadcastTXT(jsonString);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        sendWebSocketData(); // Send current data to new connection
      }
      break;
    case WStype_TEXT:
      Serial.printf("[%u] Received: %s\n", num, payload);
      break;
  }
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CO Pollution Monitor</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Arial', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            color: white;
        }
        
        .container {
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
            border: 1px solid rgba(255, 255, 255, 0.2);
            max-width: 600px;
            width: 90%;
            text-align: center;
        }
        
        .header {
            margin-bottom: 30px;
        }
        
        .title {
            font-size: 2.5em;
            font-weight: bold;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.3);
        }
        
        .subtitle {
            font-size: 1.2em;
            opacity: 0.8;
        }
        
        .status-card {
            background: rgba(255, 255, 255, 0.2);
            border-radius: 15px;
            padding: 25px;
            margin: 20px 0;
            transition: all 0.3s ease;
        }
        
        .co-level {
            font-size: 4em;
            font-weight: bold;
            margin: 20px 0;
            text-shadow: 2px 2px 4px rgba(0, 0, 0, 0.3);
        }
        
        .unit {
            font-size: 1.5em;
            opacity: 0.8;
        }
        
        .status {
            font-size: 1.8em;
            font-weight: bold;
            padding: 15px;
            border-radius: 10px;
            margin: 20px 0;
            transition: all 0.3s ease;
        }
        
        .safe {
            background: linear-gradient(135deg, #4CAF50, #45a049);
            animation: pulse-safe 2s infinite;
        }
        
        .danger {
            background: linear-gradient(135deg, #f44336, #d32f2f);
            animation: pulse-danger 1s infinite;
        }
        
        @keyframes pulse-safe {
            0% { transform: scale(1); }
            50% { transform: scale(1.05); }
            100% { transform: scale(1); }
        }
        
        @keyframes pulse-danger {
            0% { transform: scale(1); }
            50% { transform: scale(1.1); }
            100% { transform: scale(1); }
        }
        
        .info-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            margin: 20px 0;
        }
        
        .info-item {
            background: rgba(255, 255, 255, 0.1);
            padding: 15px;
            border-radius: 10px;
            border: 1px solid rgba(255, 255, 255, 0.2);
        }
        
        .info-label {
            font-size: 0.9em;
            opacity: 0.7;
            margin-bottom: 5px;
        }
        
        .info-value {
            font-size: 1.3em;
            font-weight: bold;
        }
        
        .connection-status {
            margin-top: 20px;
            padding: 10px;
            border-radius: 8px;
            font-size: 0.9em;
        }
        
        .connected {
            background: rgba(76, 175, 80, 0.3);
        }
        
        .disconnected {
            background: rgba(244, 67, 54, 0.3);
        }
        
        .alert-icon {
            font-size: 2em;
            margin-bottom: 10px;
        }
        
        @media (max-width: 600px) {
            .title {
                font-size: 2em;
            }
            
            .co-level {
                font-size: 3em;
            }
            
            .info-grid {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <div class="title">🚗 CO Monitor</div>
            <div class="subtitle">Vehicle Emission Control System</div>
        </div>
        
        <div class="status-card">
            <div class="alert-icon" id="alertIcon">🌱</div>
            <div class="co-level" id="coLevel">--</div>
            <div class="unit">ppm CO</div>
            <div class="status safe" id="statusBox">LOADING...</div>
        </div>
        
        <div class="info-grid">
            <div class="info-item">
                <div class="info-label">Raw Sensor Value</div>
                <div class="info-value" id="rawValue">--</div>
            </div>
            <div class="info-item">
                <div class="info-label">Danger Threshold</div>
                <div class="info-value" id="threshold">75 ppm</div>
            </div>
        </div>
        
        <div class="connection-status connected" id="connectionStatus">
            🔗 Connected to ESP32
        </div>
    </div>

    <script>
        let socket;
        let reconnectInterval;
        
        function connectWebSocket() {
            socket = new WebSocket('ws://' + window.location.hostname + ':81/');
            
            socket.onopen = function(event) {
                console.log('WebSocket connected');
                document.getElementById('connectionStatus').className = 'connection-status connected';
                document.getElementById('connectionStatus').innerHTML = '🔗 Connected to ESP32';
                clearInterval(reconnectInterval);
            };
            
            socket.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    updateDisplay(data);
                } catch (e) {
                    console.error('Error parsing JSON:', e);
                }
            };
            
            socket.onclose = function(event) {
                console.log('WebSocket disconnected');
                document.getElementById('connectionStatus').className = 'connection-status disconnected';
                document.getElementById('connectionStatus').innerHTML = '❌ Disconnected - Reconnecting...';
                
                // Try to reconnect every 3 seconds
                reconnectInterval = setInterval(() => {
                    console.log('Attempting to reconnect...');
                    connectWebSocket();
                }, 3000);
            };
            
            socket.onerror = function(error) {
                console.error('WebSocket error:', error);
            };
        }
        
        function updateDisplay(data) {
            document.getElementById('coLevel').textContent = data.co_ppm.toFixed(1);
            document.getElementById('rawValue').textContent = data.raw_value;
            document.getElementById('threshold').textContent = data.threshold + ' ppm';
            
            const statusBox = document.getElementById('statusBox');
            const alertIcon = document.getElementById('alertIcon');
            
            if (data.status === 'DANGER') {
                statusBox.textContent = '⚠️ EMISSION LIMIT EXCEEDED!';
                statusBox.className = 'status danger';
                alertIcon.textContent = '🚨';
                document.title = '🚨 DANGER - CO Monitor';
            } else {
                statusBox.textContent = '✅ EMISSION LEVELS SAFE';
                statusBox.className = 'status safe';
                alertIcon.textContent = '🌱';
                document.title = '🚗 CO Monitor';
            }
        }
        
        // Connect when page loads
        connectWebSocket();
    </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}