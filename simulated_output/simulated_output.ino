// ESP32 Integrated SMS System with A7670C Module + MQ7 Sensor + Sound Monitor
// Sends violation SMS for both CO and Sound violations

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED setup (0.96" 128x64 SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C  // Try 0x3D if 0x3C doesn't work

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin definitions
#define MQ7_PIN 32

// SIM A7670C Module pins
#define RXD2 27    //VVM501 MODULE RXD INTERNALLY CONNECTED
#define TXD2 26    //VVM501 MODULE TXD INTERNALLY CONNECTED
#define powerPin 4 //VVM501 MODULE ESP32 PIN D4 CONNECTED TO POWER PIN OF A7670C CHIPSET, INTERNALLY CONNECTED

// Sound System Pins
const int BUTTON = 18;
const int BUZZER = 19;
const int SOUND_SENSOR = 34;  // ADC1_CH6 (analog input only) - SHOWCASE ONLY

// I2C Pins explicit
#define SDA_PIN 21
#define SCL_PIN 22

// SIM Module Variables (YOUR EXACT VARIABLES)
int rx = -1;
#define SerialAT Serial1
String rxString;
int _timeout;
String _buffer;
String number = "+916291175964"; // REPLACE WITH YOUR NUMBER

// MQ7 sensor variables
float runningAverage = 0.0;
float alpha = 0.1; // Smoothing factor for running average
bool isCalibrated = false;
float baselineValue = 0.0;
unsigned long calibrationStartTime = 0;
const unsigned long CALIBRATION_TIME = 30000; // 45 seconds
const float CO_THRESHOLD = 200.0; // ppm threshold

// Timing variables
unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL = 1000; // Read sensor every 1 second

// CO Violation management
int coViolationCounter = 0;
bool inCOViolation = false;
unsigned long coViolationStartTime = 0;
bool coViolationStable = false;

// Sound System Variables - UPDATED FOR SIMULATION
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 200;
int buttonState;
int lastButtonState = HIGH;
int pressCount = 0;
int highDbCount = 0;
int totalFine = 0;
int countExceedPenaltyCount = 0;
int soundViolationCounter = 0;

// Sound Sensor Simulation Variables
int simulatedSoundDB = 32; // Default normal level
bool isLoudPress = false;
const int SOUND_THRESHOLD = 60; // Changed from 66 to 60 as requested

// OLED Display Cycling
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_CYCLE_TIME = 3000; // 3 seconds
bool showingCOData = true;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize random seed
  randomSeed(analogRead(0));
  
  // Initialize MQ7 pin
  pinMode(MQ7_PIN, INPUT);
  
  // Initialize Sound System pins
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  
  // Initialize I2C and OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);
  
  Serial.println("Starting OLED initialization...");
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    Serial.println("Check wiring (SDA->21, SCL->22)");
    Serial.println("Try address 0x3D if 0x3C doesn't work");
    while (true) {
      delay(1000);
      Serial.println("Display not found!");
    }
  } else {
    Serial.println("Display initialized.");
  }
  display.clearDisplay();
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Beep"));
  display.println(F("Tracker"));
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(F("Honk Wisely"));
  display.display();
  
  delay(2000);
  display.clearDisplay();
  display.display();
  
  // Initialize SIM Module (YOUR EXACT CODE)
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);
  delay(100);
  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);
  delay(10000);
  
  Serial.println("Modem Reset, please wait");
  SerialAT.println("AT+CRESET");
  delay(1000);
  SerialAT.println("AT+CRESET");
  delay(20000);  // WAITING FOR SOME TIME TO CONFIGURE MODEM
  
  SerialAT.flush();
  
  Serial.println("Echo Off");
  SerialAT.println("ATE0");   //120s
  delay(1000);
  SerialAT.println("ATE0");   //120s
  rxString = SerialAT.readString();
  Serial.print("Got: ");
  Serial.println(rxString);
  rx = rxString.indexOf("OK");
  if (rx != -1)
    Serial.println("Modem Ready");
  delay(1000);
  
  Serial.println("SIM card check");
  SerialAT.println("AT+CPIN?"); //9s
  rxString = SerialAT.readString();
  Serial.print("Got: ");
  Serial.println(rxString);
  rx = rxString.indexOf("+CPIN: READY");
  if (rx != -1)
    Serial.println("SIM Card Ready");
  delay(1000);
  
  // Start MQ7 calibration
  Serial.println("=== STARTING MQ7 CALIBRATION (30 seconds) ===");
  calibrationStartTime = millis();
  
  Serial.println("=== SMS + MQ7 + SOUND SYSTEM READY ===");
  Serial.println("Commands:");
  Serial.println("'s' = Send test SMS");
  Serial.println("'r' = Receive SMS");
  Serial.println("'c' = Make call");
  Serial.println("Automatic violation SMS when CO > 75 ppm OR Sound violations");
  //Serial.println("Sound sensor is simulated (60% loud, 40% normal)");
}

void loop() {
  // Handle serial commands
  if (Serial.available() > 0) {
    char command = Serial.read();
    
    switch (command) {
      case 's':
        SendMessage();   // Your original test message
        break;
      case 'r':
        RecieveMessage(); // Receive messages
        break;
      case 'c':
        callNumber();    // Make call
        break;
    }
  }
  
  // Read MQ7 sensor
  if (millis() - lastReadTime >= READ_INTERVAL) {
    readMQ7Sensor();
    lastReadTime = millis();
  }
  
  // Handle Sound System (Button Logic) - UPDATED WITH SIMULATION
  handleSoundSystem();
  
  // Update OLED Display (Cycle between CO and Sound data) - UPDATED
  updateOLEDDisplay();
  
  // Handle incoming SIM responses
  if (SerialAT.available() > 0) {
    Serial.write(SerialAT.read());
  }
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
      Serial.println("=== MQ7 CALIBRATION COMPLETE ===");
      Serial.print("Baseline value: ");
      Serial.println(baselineValue);
      Serial.println("System ready for CO monitoring...");
    } else {
      unsigned long remainingTime = CALIBRATION_TIME - (millis() - calibrationStartTime);
      Serial.print("Calibrating MQ7... ");
      Serial.print(remainingTime / 1000);
      Serial.println(" seconds remaining");
    }
    return;
  }
  
  // Calculate CO concentration in ppm
  float coPPM = calculateCOPPM(runningAverage);
  
  // Debug output
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print(" | Voltage: ");
  Serial.print(voltage, 2);
  Serial.print("V | Running Avg: ");
  Serial.print(runningAverage, 1);
  Serial.print(" | CO: ");
  Serial.print(coPPM, 1);
  Serial.println(" ppm");
  
  // Check for CO violations
  checkCOViolation(coPPM);
}

void checkCOViolation(float coPPM) {
  if (coPPM > CO_THRESHOLD) {
    if (!inCOViolation) {
      // Start of potential violation
      inCOViolation = true;
      coViolationStartTime = millis();
      coViolationStable = false;
      Serial.println("🚨 Potential CO violation detected, checking stability...");
    } else {
      // Check if violation has been stable for 5 seconds
      if (!coViolationStable && (millis() - coViolationStartTime >= 5000)) {
        coViolationStable = true;
        Serial.println("🚨 CO VIOLATION CONFIRMED - Sending SMS!");
        
        // Send violation SMS with actual CO reading
        SendCOViolationSMS(coPPM);
      }
    }
  } else {
    // Reset violation state when CO goes below threshold
    if (inCOViolation) {
      Serial.println("✅ CO Violation cleared - CO levels back to normal");
      inCOViolation = false;
      coViolationStable = false;
    }
  }
}

void handleSoundSystem() {
  int reading = digitalRead(BUTTON);

  // Debounce logic
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && buttonState != LOW) {
      pressCount++;

      // SHOWCASE: Read analog sound sensor value (but don't use it)
      int soundValue = analogRead(SOUND_SENSOR);
      Serial.print("Showcase - Raw Sound Sensor: ");
      Serial.println(soundValue);

      // SIMULATION: Generate random sound level
      // 60% chance loud (60-70dB), 40% chance normal (30-35dB)
      int randomChance = random(1, 101); // 1-100
      if (randomChance <= 60) {
        // Loud press
        simulatedSoundDB = random(60, 71); // 60-70 dB
        isLoudPress = true;
      } else {
        // Normal press
        simulatedSoundDB = random(30, 36); // 30-35 dB
        isLoudPress = false;
      }
      
      // Debug outputs to Serial Monitor
      Serial.print("Button pressed ");
      Serial.print(pressCount);
      Serial.println(" times");

      Serial.print("Simulated Sound Level (dB): ");
      Serial.println(simulatedSoundDB);

      bool highDbViolation = false;
      bool countExceededViolation = false;

      // Check for high dB violation
      if (simulatedSoundDB >= SOUND_THRESHOLD) {
        highDbCount++;
        highDbViolation = true;
        Serial.println("High Sound - VIOLATION!");
        SendSoundViolationSMS(simulatedSoundDB, pressCount, "HIGH_DB");
      } else {
        Serial.println("Normal Sound");
      }

      // Check for count exceeded violation
      if (pressCount > 5) {
        Serial.println("Count exceeded - VIOLATION!");
        countExceedPenaltyCount++;
        countExceededViolation = true;
        SendSoundViolationSMS(simulatedSoundDB, pressCount, "COUNT_EXCEEDED");
        pressCount = 0; // Reset after violation
      }

      // Calculate total fines based on simulated values
      totalFine = (highDbCount * 20) + (countExceedPenaltyCount * 50);
    }
    buttonState = reading;
  }

  // Buzzer ON when button pressed
  if (reading == LOW) {
    digitalWrite(BUZZER, HIGH);
  } else {
    digitalWrite(BUZZER, LOW);
  }

  lastButtonState = reading;
}

void updateOLEDDisplay() {
  if (millis() - lastDisplayUpdate >= DISPLAY_CYCLE_TIME) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    if (showingCOData) {
      // Display CO Data
      if (isCalibrated) {
        float coPPM = calculateCOPPM(runningAverage);
        display.setCursor(0, 0);
        display.println(F("CO MONITOR"));
        display.setCursor(0, 16);
        display.print(F("Level: "));
        display.print(coPPM, 1);
        display.println(F(" ppm"));
        display.setCursor(0, 32);
        if (coPPM > CO_THRESHOLD) {
          display.println(F("VIOLATION!"));
        } else {
          display.println(F("Normal"));
        }
      } else {
        display.setCursor(0, 0);
        display.println(F("CO MONITOR"));
        display.setCursor(0, 16);
        display.println(F("Calibrating..."));
        unsigned long remainingTime = CALIBRATION_TIME - (millis() - calibrationStartTime);
        display.setCursor(0, 32);
        display.print(remainingTime / 1000);
        display.println(F(" sec left"));
      }
    } else {
      // Display Sound Data - UPDATED TO SHOW SIMULATED VALUES
      display.setCursor(0, 0);
      display.print(F("Pressed: "));
      display.print(pressCount);
      
      display.setCursor(0, 12);
      display.print(F("dB Level: "));
      display.print(simulatedSoundDB);

      display.setCursor(0, 24);
      if (simulatedSoundDB >= SOUND_THRESHOLD) {
        display.print(F("High DB: PENALTY"));
      } else {
        display.print(F("Normal DB: OK"));
      }
      
      display.setCursor(0, 36);
      display.print(F("High DB: "));
      display.print(highDbCount);

      display.setCursor(0, 48);
      display.print(F("Fine Rs."));
      display.print(totalFine);
    }
    
    display.display();
    showingCOData = !showingCOData;
    lastDisplayUpdate = millis();
  }
}

float calculateCOPPM(float sensorValue) {
  if (!isCalibrated) return 0.0;
  
  // Simplified conversion formula for MQ7
  float ratio = sensorValue / baselineValue;
  float ppm = 0.0;
  
  if (ratio > 1.0) {
    // Basic exponential relationship for CO concentration
    ppm = pow(10, (log10(ratio) * 2.0 + 1.3));
    if (ppm > 2000) ppm = 2000; // Cap at sensor maximum
  }
  
  return ppm;
}

// Send CO Violation SMS with actual CO reading
void SendCOViolationSMS(float coPPM) {
  Serial.println("=== SENDING CO VIOLATION SMS ===");
  
  // Calculate fine based on actual CO reading
  float excessPPM = coPPM - CO_THRESHOLD;
  int baseFine = 2500;
  int variableFine = (int)(excessPPM * 100); // ₹100 per ppm
  int totalCOFine = baseFine + variableFine;
  
  coViolationCounter++;
  
  // Create violation message with ACTUAL CO reading
  String SMS = "EMISSION VIOLATION\n";
  SMS += "CO:" + String(coPPM, 0) + "ppm (Limit:75)\n";  // ACTUAL PPM HERE
  SMS += "Fine: Rs" + String(totalCOFine) + "\n";
  SMS += "ID:#E" + String(coViolationCounter) + "\n";
  SMS += "02/09/25 03:14\n";
  SMS += "Pay: bit.ly/paytraffic\n";
  SMS += "-Pollution Control Board";
  
  Serial.println("CO Message to send:");
  Serial.println(SMS);
  Serial.println("Length: " + String(SMS.length()) + " chars");
  Serial.println("Actual CO Level: " + String(coPPM, 1) + " ppm");
  Serial.println("Fine Amount: Rs" + String(totalCOFine));
  Serial.println("------------------------");
  
  // Your exact SMS sending sequence
  SerialAT.println("AT+CMGF=1");    //Sets the GSM Module in Text Mode
  delay(1000);
  SerialAT.println("AT+CMGS=\"" + number + "\"\r"); //Mobile phone number to send message
  delay(1000);
  SerialAT.println(SMS);  // Send violation message with actual CO reading
  delay(100);
  SerialAT.println((char)26);// ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();
  
  Serial.println("CO Violation SMS sent to: " + number);
  Serial.println("=== CO SMS SENDING COMPLETE ===");
}

// Send Sound Violation SMS - UPDATED FOR SEPARATE MESSAGES
void SendSoundViolationSMS(int soundDB, int honkCount, String violationType) {
  Serial.println("=== SENDING SOUND VIOLATION SMS ===");
  
  soundViolationCounter++;
  
  String SMS = "";
  int violationFine = 0;
  
  // Create different messages based on violation type
  if (violationType == "HIGH_DB") {
    // High Decibel Violation Message
    violationFine = highDbCount * 20;
    SMS = "SOUND VIOLATION\n";
    SMS += "dB:" + String(soundDB) + " (Limit:" + String(SOUND_THRESHOLD) + ")\n";
    SMS += "Fine: Rs" + String(violationFine) + "\n";
    SMS += "ID:#S" + String(soundViolationCounter) + "\n";
    SMS += "02/09/25 03:14\n";
    SMS += "Pay: bit.ly/paytraffic\n";
    SMS += "-Traffic Control Board";
    
    Serial.println("HIGH DECIBEL VIOLATION");
    Serial.println("Actual Sound Level: " + String(soundDB) + " dB");
  } 
  else if (violationType == "COUNT_EXCEEDED") {
    // Horn Count Exceeded Violation Message
    violationFine = countExceedPenaltyCount * 50;
    SMS = "HORN COUNT VIOLATION\n";
    SMS += "Honks:" + String(honkCount) + "/5 (Exceeded)\n";
    SMS += "Fine: Rs" + String(violationFine) + "\n";
    SMS += "ID:#H" + String(soundViolationCounter) + "\n";
    SMS += "02/09/25 03:14\n";
    SMS += "Pay: bit.ly/paytraffic\n";
    SMS += "-Traffic Control Board";
    
    Serial.println("HORN COUNT EXCEEDED VIOLATION");
    Serial.println("Horn Count: " + String(honkCount) + " (Limit: 5)");
  }
  
  Serial.println("Sound Message to send:");
  Serial.println(SMS);
  Serial.println("Length: " + String(SMS.length()) + " chars");
  Serial.println("Violation Type: " + violationType);
  Serial.println("Fine Amount: Rs" + String(violationFine));
  Serial.println("------------------------");
  
  // Your exact SMS sending sequence
  SerialAT.println("AT+CMGF=1");    //Sets the GSM Module in Text Mode
  delay(1000);
  SerialAT.println("AT+CMGS=\"" + number + "\"\r"); //Mobile phone number to send message
  delay(1000);
  SerialAT.println(SMS);  // Send violation message
  delay(100);
  SerialAT.println((char)26);// ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();
  
  Serial.println("Sound Violation SMS sent to: " + number);
  Serial.println("=== SOUND SMS SENDING COMPLETE ===");
}

// YOUR EXACT ORIGINAL FUNCTIONS - UNCHANGED
void SendMessage() {
  Serial.println("Sending test message...");
  SerialAT.println("AT+CMGF=1");    //Sets the GSM Module in Text Mode
  delay(1000);
  SerialAT.println("AT+CMGS=\"" + number + "\"\r"); //Mobile phone number to send message
  delay(1000);
  String SMS = "I AM JOY";
  SerialAT.println(SMS);
  delay(100);
  SerialAT.println((char)26);// ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();
}

void RecieveMessage() {
  Serial.println("VVM501 AT7670C Read an SMS");
  delay(1000);
  SerialAT.println("AT+CNMI=2,2,0,0,0"); // AT Command to receive a live SMS
  delay(1000);
  Serial.println("Unread Message done");
}

String _readSerial() {
  _timeout = 0;
  while (!SerialAT.available() && _timeout < 12000) {
    delay(13);
    _timeout++;
  }
  if (SerialAT.available()) {
    return SerialAT.readString();
  }
}

void callNumber() {
  SerialAT.print(F("ATD"));
  SerialAT.print(number);
  SerialAT.print(F(";\r\n"));
  _buffer = _readSerial();
  Serial.println(_buffer);
}