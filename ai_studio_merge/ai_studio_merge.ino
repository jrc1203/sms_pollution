// ================================================================= //
//      ESP32 CO POLLUTION MONITOR WITH A7670C GSM ALERTS            //
//            (VERSION 2.2 - PPM CALCULATION FIX)                    //
// ================================================================= //

// --- Core Libraries ---
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

// --- WiFi Credentials ---
const char* ssid = "MySpyCar";
const char* password = "123456789";

// ================================================================= //
//                 PIN & HARDWARE DEFINITIONS                        //
// ================================================================= //

#define MQ7_PIN     32
#define LED_PIN     18
#define BUZZER_PIN  5
#define SDA_PIN     21
#define SCL_PIN     22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET  -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define RXD2 27
#define TXD2 26
#define powerPin 4
#define SerialAT Serial1

// ================================================================= //
//                   GLOBAL VARIABLES & SETTINGS                     //
// ================================================================= //

WebServer server(80);
WebSocketsServer webSocket(81);

float runningAverage = 0.0;
const float SMOOTHING_ALPHA = 0.1;
bool isCalibrated = false;
unsigned long calibrationStartTime = 0;
const unsigned long CALIBRATION_TIME = 45000;
const float CO_THRESHOLD = 75.0;

// --- FIX: New constants for a more accurate PPM calculation ---
#define RL_VALUE         (5.0)    // The value of the load resistor in kilo-ohms
#define RO_CLEAN_AIR_FACTOR (27.0)  // Ro/Rs ratio in clean air for MQ-7, from datasheet
float Ro = 10.0;                  // Calibrated value of the sensor's resistance in clean air

unsigned long lastReadTime = 0;
unsigned long lastWebSocketUpdate = 0;
const unsigned long READ_INTERVAL = 1000;
const unsigned long WEBSOCKET_UPDATE_INTERVAL = 3000;

bool alertActive = false;

enum GsmState { IDLE, VIOLATION_CONFIRMATION, SENDING_SMS, COOLDOWN, BLOCKED };
GsmState gsmState = IDLE;
unsigned long violationStartTime = 0;
unsigned long cooldownStartTime = 0;
const unsigned long VIOLATION_CONFIRM_TIME = 5000;
const unsigned long COOLDOWN_PERIOD = 30000;
const int MAX_SMS_COUNT = 10;
int smsSentCount = 0;
unsigned int violationID_counter = 0;

Preferences preferences;
String targetPhoneNumber;

String gsm_rxString;
String gsm_buffer;

// ================================================================= //
//                          SETUP FUNCTION                           //
// ================================================================= //
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("System Booting Up...");

    pinMode(MQ7_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    Wire.begin(SDA_PIN, SCL_PIN);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (true);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    preferences.begin("pollution-app", false);
    targetPhoneNumber = preferences.getString("phoneNum", "+916291175964");
    smsSentCount = preferences.getInt("smsCount", 0);
    Serial.printf("Loaded Settings: Phone=[%s], SMS Count=[%d]\n", targetPhoneNumber.c_str(), smsSentCount);

    display.setCursor(0, 0);
    display.println("Connecting WiFi...");
    display.display();
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Connected!");
    display.println("IP: " + WiFi.localIP().toString());
    display.display();
    delay(2000);
    
    configTime(19800, 0, "pool.ntp.org");

    server.on("/", handleRoot);
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.println("Web server started.");

    pinMode(powerPin, OUTPUT);
    digitalWrite(powerPin, LOW);
    SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Initializing GSM...");
    display.display();
    
    Serial.println("Initializing GSM Module... Please Wait.");
    delay(10000);
    Serial.println("Modem Reset...");
    SerialAT.println("AT+CRESET"); delay(1000);
    SerialAT.println("AT+CRESET"); delay(20000);
    SerialAT.flush();
    Serial.println("SIM card check...");
    SerialAT.println("AT+CPIN?"); delay(1000);
    gsm_rxString = SerialAT.readString();
    Serial.print("Got: "); Serial.println(gsm_rxString);
    if (gsm_rxString.indexOf("+CPIN: READY") != -1) {
        Serial.println("SIM Card Ready.");
        display.println("SIM Ready.");
    } else {
        Serial.println("SIM Card FAILED!");
        display.println("SIM FAILED!");
    }
    display.display();
    delay(2000);

    Serial.println("Starting MQ7 calibration (45 seconds)...");
    calibrationStartTime = millis();
}


// ================================================================= //
//                           MAIN LOOP                               //
// ================================================================= //
void loop() {
    server.handleClient();
    webSocket.loop();

    unsigned long currentTime = millis();

    if (currentTime - lastReadTime >= READ_INTERVAL) {
        readMQ7Sensor();
        lastReadTime = currentTime;
    }

    handleGSMStateMachine();
    updateOLEDDisplay();
    if (currentTime - lastWebSocketUpdate >= WEBSOCKET_UPDATE_INTERVAL) {
        sendWebSocketData();
        lastWebSocketUpdate = currentTime;
    }
    
    handleAlerts();

    if (SerialAT.available() > 0) {
        Serial.write(SerialAT.read());
    }
}


// ================================================================= //
//                  MQ7 SENSOR & CALCULATION FUNCTIONS               //
// ================================================================= //

// --- NEW - Function to get sensor resistance Rs ---
float getResistance(int raw_adc) {
  float sensor_voltage = (raw_adc / 4095.0) * 3.3;
  // Prevent division by zero if sensor_voltage is 0
  if (sensor_voltage == 0) return -1;
  float Rs = (3.3 * RL_VALUE) / sensor_voltage - RL_VALUE;
  return Rs;
}

// --- REPLACED - Using a standard datasheet-based formula ---
float calculateCOPPM(float rs) {
  if (!isCalibrated) return 0.0;
  if (rs < 0) return 0.0; // Check for invalid resistance

  float ratio = rs / Ro;
  // Formula derived from the sensor's datasheet log-log graph: ppm = A * (Rs/Ro)^B
  // For MQ-7 CO, a common approximation is A=99.042 and B=-1.458
  float ppm = 99.042 * pow(ratio, -1.458);
  return ppm > 0 ? ppm : 0;
}

// --- MODIFIED - To use the new resistance-based method ---
void readMQ7Sensor() {
    int rawValue = analogRead(MQ7_PIN);

    if (runningAverage == 0.0) runningAverage = rawValue;
    else runningAverage = (SMOOTHING_ALPHA * rawValue) + ((1 - SMOOTHING_ALPHA) * runningAverage);
    
    float rs = getResistance(runningAverage);
    
    if (!isCalibrated) {
        if (millis() - calibrationStartTime >= CALIBRATION_TIME) {
            Ro = (rs > 0) ? (rs / RO_CLEAN_AIR_FACTOR) : 10.0;
            isCalibrated = true;
            Serial.println("Calibration complete!");
            Serial.printf("Baseline Ro set to: %.2f\n", Ro);
        }
        return;
    }

    float coPPM = calculateCOPPM(rs);
    
    Serial.printf("Raw: %d | Avg: %.1f | Rs: %.2f kOhms | CO: %.1f ppm\n", rawValue, runningAverage, rs, coPPM);
    alertActive = (coPPM > CO_THRESHOLD);
}


// ================================================================= //
//               GSM STATE MACHINE & VIOLATION LOGIC                 //
// ================================================================= //
void handleGSMStateMachine() {
    if (!isCalibrated) return;

    switch (gsmState) {
        case IDLE:
            if (alertActive) {
                gsmState = VIOLATION_CONFIRMATION;
                violationStartTime = millis();
                Serial.println("STATE CHANGE: IDLE -> VIOLATION_CONFIRMATION");
            }
            break;

        case VIOLATION_CONFIRMATION:
            if (!alertActive) {
                gsmState = IDLE;
                Serial.println("STATE CHANGE: VIOLATION_CONFIRMATION -> IDLE (Violation ended)");
            } else if (millis() - violationStartTime >= VIOLATION_CONFIRM_TIME) {
                if (smsSentCount >= MAX_SMS_COUNT) {
                    gsmState = BLOCKED;
                    Serial.println("STATE CHANGE: VIOLATION_CONFIRMATION -> BLOCKED (Max SMS count reached)");
                } else {
                    gsmState = SENDING_SMS;
                    Serial.println("STATE CHANGE: VIOLATION_CONFIRMATION -> SENDING_SMS");
                }
            }
            break;

        case SENDING_SMS:
            {
                float rs = getResistance(runningAverage);
                float currentPPM = calculateCOPPM(rs);
                String message = createViolationMessage(currentPPM);
                String number = targetPhoneNumber;

                Serial.println("--- Sending Violation SMS ---");
                SendMessage(message, number);
                smsSentCount++;
                preferences.putInt("smsCount", smsSentCount);
                Serial.println("-----------------------------");
                gsmState = COOLDOWN;
                cooldownStartTime = millis();
                Serial.println("STATE CHANGE: SENDING_SMS -> COOLDOWN");
            }
            break;

        case COOLDOWN:
            if (millis() - cooldownStartTime >= COOLDOWN_PERIOD) {
                gsmState = IDLE;
                Serial.println("STATE CHANGE: COOLDOWN -> IDLE");
            }
            break;

        case BLOCKED:
            break;
    }
}


// ================================================================= //
//             USER'S A7670C GSM FUNCTIONS (INTEGRATED)              //
// ================================================================= //
String gsm_readSerial() {
    int timeout = 0;
    while (!SerialAT.available() && timeout < 12000) {
        delay(13);
        timeout++;
    }
    if (SerialAT.available()) { return SerialAT.readString(); }
    return "";
}

void SendMessage(String message, String number) {
    Serial.println("Preparing to send SMS via user function...");
    SerialAT.println("AT+CMGF=1"); delay(1000);
    SerialAT.println("AT+CMGS=\"" + number + "\"\r"); delay(1000);
    SerialAT.println(message); delay(100);
    SerialAT.println((char)26); delay(1000);
    gsm_buffer = gsm_readSerial();
    Serial.println("GSM Module Response: " + gsm_buffer);
}


// ================================================================= //
//            DISPLAYS, ALERTS, AND WEB SERVER FUNCTIONS             //
// ================================================================= //
void updateOLEDDisplay() {
    static unsigned long lastUpdateTime = 0;
    if (millis() - lastUpdateTime < 2000 && isCalibrated) return;
    lastUpdateTime = millis();
    
    display.clearDisplay();
    display.setCursor(0, 0);

    if (!isCalibrated) {
        unsigned long remainingTime = (CALIBRATION_TIME - (millis() - calibrationStartTime)) / 1000;
        display.println("CALIBRATING MQ7...");
        display.printf("Time left: %lu s\n", remainingTime);
    } else {
        float rs = getResistance(runningAverage);
        float coPPM = calculateCOPPM(rs);
        display.setTextSize(2);
        display.printf("CO:%.1f\n", coPPM);
        display.setTextSize(1);
        display.print("ppm\n");
        display.println(alertActive ? "STATUS: DANGER!" : "STATUS: SAFE");
        display.print("SMS Sent: "); display.print(smsSentCount); display.print("/"); display.println(MAX_SMS_COUNT);
        String gsmStatusStr = "GSM: ";
        switch (gsmState) {
          case IDLE: gsmStatusStr += "IDLE"; break;
          case VIOLATION_CONFIRMATION: gsmStatusStr += "DETECT.."; break;
          case COOLDOWN: gsmStatusStr += "COOLDOWN"; break;
          case BLOCKED: gsmStatusStr += "BLOCKED"; break;
          case SENDING_SMS: gsmStatusStr += "SENDING!"; break;
        }
        display.println(gsmStatusStr);
    }
    display.display();
}

void handleAlerts() {
    if (alertActive) {
        digitalWrite(LED_PIN, (millis() / 500) % 2);
        digitalWrite(BUZZER_PIN, HIGH);
    } else {
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
    }
}

String createViolationMessage(float coPPM) {
    float excessPPM = coPPM - CO_THRESHOLD;
    int baseFine = 2500;
    int variableFine = (int)(excessPPM * 100);
    int totalFine = baseFine + variableFine;

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char dateTimeStr[20];
    strftime(dateTimeStr, sizeof(dateTimeStr), "%d/%m/%y %H:%M:%S", &timeinfo);
    char violationID[20];
    violationID_counter++;
    sprintf(violationID, "#EMI%02d%02d%02d%03d", timeinfo.tm_year % 100, timeinfo.tm_mon + 1, timeinfo.tm_mday, violationID_counter);

    String message = "TRAFFIC VIOLATION ALERT\n";
    message += "Vehicle Emission Violation\n";
    message += "CO Level: " + String(coPPM, 1) + " ppm (Limit: " + String(CO_THRESHOLD, 0) + " ppm)\n";
    message += "Fine: Rs " + String(totalFine) + "\n";
    message += "ID: " + String(violationID) + "\n";
    message += "Date: " + String(dateTimeStr) + "\n";
    message += "- Pollution Control Board";
    return message;
}

void sendWebSocketData() {
    StaticJsonDocument<512> doc;
    float rs = getResistance(runningAverage);
    doc["co_ppm"] = calculateCOPPM(rs);
    doc["status"] = alertActive ? "DANGER" : "SAFE";
    doc["sms_sent"] = smsSentCount;
    doc["max_sms"] = MAX_SMS_COUNT;
    doc["target_phone"] = targetPhoneNumber;
    
    String gsmStatusStr;
    switch(gsmState){
      case IDLE: gsmStatusStr="Idle"; break;
      case VIOLATION_CONFIRMATION: gsmStatusStr="Confirming Violation..."; break;
      case SENDING_SMS: gsmStatusStr="Sending SMS..."; break;
      case COOLDOWN: gsmStatusStr="Cooldown (" + String( (COOLDOWN_PERIOD - (millis() - cooldownStartTime))/1000 ) + "s)"; break;
      case BLOCKED: gsmStatusStr="Blocked (Limit Reached)"; break;
    }
    doc["gsm_state"] = gsmStatusStr;
    
    String jsonString;
    serializeJson(doc, jsonString);
    webSocket.broadcastTXT(jsonString);
}


void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_TEXT) {
        StaticJsonDocument<128> doc;
        deserializeJson(doc, payload, length);
        if (doc.containsKey("command") && doc["command"] == "resetCounter") {
            smsSentCount = 0;
            preferences.putInt("smsCount", 0);
            if(gsmState == BLOCKED) gsmState = IDLE;
            Serial.println("SMS counter has been reset from webpage.");
        }
        if (doc.containsKey("command") && doc["command"] == "setPhone") {
            String newNumber = doc["phone"];
            if (newNumber.length() > 10) {
                targetPhoneNumber = newNumber;
                preferences.putString("phoneNum", targetPhoneNumber);
                Serial.println("Target phone number updated to: " + targetPhoneNumber);
            }
        }
    }
}


void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Pollution Control Panel</title>
    <style>
        body{font-family:system-ui; background:#1e1e2f; color:#fff; display:flex; justify-content:center; align-items:center; min-height:100vh; margin:0;}
        .container{background:#282a3d; border-radius:15px; padding:2rem; width:90%; max-width:500px; box-shadow:0 10px 30px rgba(0,0,0,0.3);}
        h1{margin:0 0 1rem; text-align:center; color:#8d98ff;}
        .card{background:#34374f; border-radius:10px; padding:1.5rem; margin-bottom:1rem; text-align:center;}
        .co-level{font-size:4em; font-weight:bold; margin:0;} .unit{opacity:0.7;}
        .status{padding:0.5rem; border-radius:5px; margin-top:1rem; font-weight:bold;}
        .safe{background:#28a745;}.danger{background:#dc3545; animation:pulse 1s infinite;}
        .grid{display:grid; grid-template-columns:1fr 1fr; gap:1rem; margin-bottom:1rem;}
        .info{background:#34374f; border-radius:10px; padding:1rem; text-align:center;} .info label{display:block; font-size:0.8em; opacity:0.7; margin-bottom:0.3rem;} .info span{font-size:1.4em; font-weight:bold;}
        .controls .info{text-align:left; padding:0.5rem 1rem;} input, button{width:100%; padding:0.8rem; border-radius:5px; border:none; margin-top:0.5rem; background:#4a4e69; color:#fff; font-size:1em;}
        button{background:#8d98ff; cursor:pointer; font-weight:bold;}
        @keyframes pulse{0%{transform:scale(1);box-shadow:0 0 0 0 rgba(220,53,69,0.7);} 50%{transform:scale(1.02);} 100%{transform:scale(1);box-shadow:0 0 0 10px rgba(220,53,69,0);}}
    </style>
</head>
<body>
    <div class="container">
        <h1>Pollution Control Panel</h1>
        <div class="card">
            <div id="coLevel" class="co-level">--</div>
            <div class="unit">ppm CO</div>
            <div id="statusBox" class="status">Connecting...</div>
        </div>
        <div class="grid">
            <div class="info">
                <label>SMS Sent</label>
                <span id="smsCount">--</span>
            </div>
            <div class="info">
                <label>GSM Status</label>
                <span id="gsmState">--</span>
            </div>
        </div>
        <div class="controls">
            <div class="info">
                <label for="phoneNum">Target Phone Number</label>
                <input type="text" id="phoneNum" placeholder="+91...">
                <button onclick="setPhone()">Set Number</button>
            </div>
             <div class="info">
                 <label>System Controls</label>
                <button onclick="resetCounter()">Reset SMS Counter</button>
            </div>
        </div>
    </div>
    <script>
        const ws = new WebSocket('ws://' + window.location.hostname + ':81/');
        ws.onmessage = function(event){
            const data = JSON.parse(event.data);
            document.getElementById('coLevel').textContent = data.co_ppm.toFixed(1);
            document.getElementById('statusBox').textContent = data.status;
            document.getElementById('statusBox').className = 'status ' + (data.status === 'DANGER' ? 'danger' : 'safe');
            document.getElementById('smsCount').textContent = `${data.sms_sent} / ${data.max_sms}`;
            document.getElementById('gsmState').textContent = data.gsm_state;
            document.getElementById('phoneNum').placeholder = data.target_phone;
        };
        function resetCounter(){ ws.send(JSON.stringify({command: 'resetCounter'})); }
        function setPhone(){
            const phone = document.getElementById('phoneNum').value;
            ws.send(JSON.stringify({command: 'setPhone', phone: phone}));
            document.getElementById('phoneNum').value = '';
        }
    </script>
</body>
</html>)rawliteral";
    server.send(200, "text/html", html);
}