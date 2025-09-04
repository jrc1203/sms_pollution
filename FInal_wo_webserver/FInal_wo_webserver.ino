// ESP32 Integrated SMS System with A7670C Module + MQ7 Sensor + Sound Monitor
// Sends violation SMS for both CO and Sound violations

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED setup (0.96" 128x64 SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C  // Try 0x3D if 0x3C doesn't work

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin definitions
#define MQ7_PIN 32

// SIM A7670C Module pins
#define RXD2 27     //VVM501 MODULE RXD INTERNALLY CONNECTED
#define TXD2 26     //VVM501 MODULE TXD INTERNALLY CONNECTED
#define powerPin 4  //VVM501 MODULE ESP32 PIN D4 CONNECTED TO POWER PIN OF A7670C CHIPSET, INTERNALLY CONNECTED

// Sound System Pins - UPDATED BUZZER PIN TO 25 (DAC)
const int BUTTON = 18;
const int BUZZER = 25;        // Changed to DAC pin 25
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
String number = "+916291175964";  // REPLACE WITH YOUR NUMBER

// MQ7 sensor variables
float runningAverage = 0.0;
float alpha = 0.1;  // Smoothing factor for running average
bool isCalibrated = false;
float baselineValue = 0.0;
unsigned long calibrationStartTime = 0;
const unsigned long CALIBRATION_TIME = 30000;  // 45 seconds
const float CO_THRESHOLD = 200.0;              // ppm threshold

// Timing variables
unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL = 1000;  // Read sensor every 1 second

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

// Sound Sensor Simulation Variables - UPDATED WITH ALL VARIABLES
int simulatedSoundDB = 32;  // Default normal level
int ambientSoundDB = 32;    // Real-time ambient sound level
bool isLoudPress = false;
const int SOUND_THRESHOLD = 60;  // Changed from 66 to 60 as requested
unsigned long lastAmbientUpdate = 0;
const unsigned long AMBIENT_UPDATE_INTERVAL = 2000;  // Update ambient every 2 seconds
bool buzzerActive = 500;
unsigned long buzzerStartTime = 0;



// OLED Display Variables - UPDATED FOR SINGLE SCREEN WITH ALL VARIABLES
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 500;  // Update every 500ms
bool initializationComplete = false;
unsigned long projectNameStartTime = 0;
bool projectNameShown = false;
String currentInitStatus = "Starting...";



void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize random seed
  randomSeed(analogRead(0));

  // Initialize MQ7 pin
  pinMode(MQ7_PIN, INPUT);

  // Initialize Sound System pins
  pinMode(BUTTON, INPUT_PULLUP);
  // No pinMode needed for DAC pin 25

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

  // Show project name for 3 seconds
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("SONIC"));
  display.println(F("GUARD"));
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.println(F("Smart Traffic Monitor"));
  display.display();

  projectNameStartTime = millis();
  projectNameShown = true;

  // Initialize SIM Module (YOUR EXACT CODE)
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);
  delay(100);
  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);
  delay(10000);

  Serial.println("Modem Reset, please wait");
  currentInitStatus = "Modem Reset...";
  SerialAT.println("AT+CRESET");
  delay(1000);
  SerialAT.println("AT+CRESET");
  //delay(20000);  // WAITING FOR SOME TIME TO CONFIGURE MODEM
  delay(3000);

  SerialAT.flush();

  Serial.println("Echo Off");
  currentInitStatus = "Echo Off...";
  SerialAT.println("ATE0");  //120s
  delay(1000);
  SerialAT.println("ATE0");  //120s
  rxString = SerialAT.readString();
  Serial.print("Got: ");
  Serial.println(rxString);
  rx = rxString.indexOf("OK");
  if (rx != -1) {
    Serial.println("Modem Ready");
    currentInitStatus = "Modem Ready";
  }
  delay(1000);

  Serial.println("SIM card check");
  currentInitStatus = "SIM Check...";
  SerialAT.println("AT+CPIN?");  //9s
  rxString = SerialAT.readString();
  Serial.print("Got: ");
  Serial.println(rxString);
  rx = rxString.indexOf("+CPIN: READY");
  if (rx != -1) {
    Serial.println("SIM Card Ready");
    currentInitStatus = "SIM Ready";
  }
  delay(1000);

  // Start MQ7 calibration
  Serial.println("=== STARTING MQ7 CALIBRATION (30 seconds) ===");
  currentInitStatus = "MQ7 Calibrating...";
  calibrationStartTime = millis();

  Serial.println("=== SMS + MQ7 + SOUND SYSTEM READY ===");
  Serial.println("Commands:");
  Serial.println("'s' = Send test SMS");
  Serial.println("'r' = Receive SMS");
  Serial.println("'c' = Make call");
  Serial.println("Automatic violation SMS when CO > 75 ppm OR Sound violations");
}

void loop() {
  // Handle serial commands
  if (Serial.available() > 0) {
    char command = Serial.read();

    switch (command) {
      case 's':
        SendMessage();  // Your original test message
        break;
      case 'r':
        RecieveMessage();  // Receive messages
        break;
      case 'c':
        callNumber();  // Make call
        break;
    }
  }

  // Read MQ7 sensor
  if (millis() - lastReadTime >= READ_INTERVAL) {
    readMQ7Sensor();
    lastReadTime = millis();
  }

  // Update ambient sound levels (real-time changing background dB)
  updateAmbientSound();

  // Handle Sound System (Button Logic) - UPDATED WITH SIMULATION AND DAC
  handleSoundSystem();

  // Update OLED Display (Single Screen) - UPDATED
  updateOLEDDisplay();

  // Handle incoming SIM responses
  if (SerialAT.available() > 0) {
    Serial.write(SerialAT.read());
  }

  //if (buzzerActive && (millis() - buzzerStartTime >= 1000)) {
  dacWrite(BUZZER, 0);   // Turn OFF after 1 second
  buzzerActive = false;  // Clear flag
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
      currentInitStatus = "System Ready!";
      initializationComplete = true;
    } else {
      unsigned long remainingTime = CALIBRATION_TIME - (millis() - calibrationStartTime);
      Serial.print("Calibrating MQ7... ");
      Serial.print(remainingTime / 1000);
      Serial.println(" seconds remaining");
      currentInitStatus = "MQ7: " + String(remainingTime / 1000) + "s left";
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

void updateAmbientSound() {
  // Update ambient sound levels every 2 seconds for realistic variation
  if (millis() - lastAmbientUpdate >= AMBIENT_UPDATE_INTERVAL) {
    // Generate realistic ambient sound (28-36 dB range)
    ambientSoundDB = random(28, 37);
    lastAmbientUpdate = millis();
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

      // SIMULATION: Generate random sound level - UPDATED TO 70-30 PERCENTAGE
      // 70% chance normal (30-35dB), 30% chance loud (60-70dB)
      int randomChance = random(1, 101);  // 1-100
      if (randomChance <= 30) {
        // Loud press (30% chance)
        simulatedSoundDB = random(60, 71);  // 60-70 dB
        isLoudPress = true;
        dacWrite(BUZZER, 255);  // Max intensity for loud
        Serial.println("LOUD PRESS - High intensity buzzer");
        delay(1000);
        dacWrite(BUZZER, 0);
        buzzerStartTime = millis();  // Record start time
        buzzerActive = true;         // Flag as active

      } else {
        // Normal press (70% chance)
        simulatedSoundDB = random(30, 36);  // 30-35 dB
        isLoudPress = false;
        dacWrite(BUZZER, 50);  // Low intensity for normal
        Serial.println("NORMAL PRESS - Low intensity buzzer");
        delay(1000);
        dacWrite(BUZZER, 0);
        buzzerStartTime = millis();  // Record start time
        buzzerActive = true;         // Flag as active
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
        pressCount = 0;  // Reset after violation
        // Don't change buzzer intensity - let it follow normal pattern
      }

      // Calculate total fines based on simulated values
      totalFine = (highDbCount * 20) + (countExceedPenaltyCount * 50);
    }
    buttonState = reading;
  }

  // DAC Buzzer control when button pressed - UPDATED
  if (reading == LOW) {
    // Buzzer intensity already set during button press logic above
    // No additional action needed as DAC value persists
  } else {
    dacWrite(BUZZER, 0);  // Turn off buzzer when button released
  }

  lastButtonState = reading;
}

void updateOLEDDisplay() {
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Show project name for 3 seconds (non-blocking)
    if (projectNameShown && (millis() - projectNameStartTime < 3000)) {
      // Keep showing project name
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println(F("SONIC"));
      display.println(F("GUARD"));
      display.setTextSize(1);
      display.setCursor(0, 48);
      display.println(F("Smart Traffic Monitor"));
    }
    // Show initialization status with live updates
    else if (!initializationComplete) {
      projectNameShown = false;
      display.setCursor(0, 0);
      display.setTextSize(1);
      display.println(F("INITIALIZING SYSTEM"));
      display.println("");

      display.println(currentInitStatus);

      if (currentInitStatus.indexOf("MQ7") != -1) {
        display.println("");
        display.print(F("Baseline: "));
        display.println((int)runningAverage);
      }
    }
    // Show main operation data with improved layout
    else {
      // Draw border around the entire display first
      display.drawRect(0, 0, 128, 64, SSD1306_WHITE);  // Complete border rectangle

      // Left side data (Lines 1-4) - BIGGER TEXT SIZE (adjusted for border)
      display.setTextSize(1);
      display.setCursor(3, 3);  // Move slightly inside border
      float coPPM = calculateCOPPM(runningAverage);
      display.setTextSize(1);  // Keep labels normal
      display.print(F("CO:"));
      display.setTextSize(1);  // Make values bigger
      display.print(coPPM, 0);
      display.setTextSize(1);
      display.print(F(" ppm"));
      if (coPPM > CO_THRESHOLD) {
        display.print(F("!"));
      }

      // Line 2: Current dB (ambient + button press) - BIGGER VALUES
      display.setCursor(3, 17);  // Adjusted for border
      display.setTextSize(1);
      display.print(F("dB:"));
      display.setTextSize(1);  // Bigger dB values
      if (digitalRead(BUTTON) == LOW) {
        display.print(simulatedSoundDB);  // Show button press dB
        if (simulatedSoundDB >= SOUND_THRESHOLD) {
          display.setTextSize(1);
          display.print(F("!"));
        }
      } else {
        // Show simulated dB value for 2 seconds after button press, then ambient
        if (buzzerActive || (millis() - buzzerStartTime < 2000)) {
          display.print(simulatedSoundDB);  // Show simulated dB for 2 seconds
          if (simulatedSoundDB >= SOUND_THRESHOLD) {
            display.setTextSize(1);
            display.print(F("!"));
          }
        } else {
          display.print(ambientSoundDB);  // Show ambient dB when not pressed and timeout passed
        }
      }


      // Line 3: Press Count - BIGGER VALUES
      display.setCursor(3, 31);  // Adjusted for border
      display.setTextSize(1);
      display.print(F("Honk:"));
      display.setTextSize(1);
      display.print(pressCount);
      display.setTextSize(1);
      display.print(F("/5"));

      // Line 4: High dB Count - BIGGER VALUES
      display.setCursor(3, 45);  // Adjusted for border
      display.setTextSize(1);
      display.print(F("Loud:"));
      display.setTextSize(1);
      display.print(highDbCount);

      // Draw separator line (adjusted for border)
      display.drawLine(75, 1, 75, 62, SSD1306_WHITE);

      // Right side - Fine amount (CENTERED AND BIGGER, adjusted for border)
      display.setTextSize(1);
      display.setCursor(85, 10);  // Adjusted for border
      display.println(F("FINE"));

      // Center the Rs and amount better
      display.setTextSize(1);
      display.setCursor(88, 24);  // Adjusted for border
      display.print(F("Rs"));

      display.setTextSize(2);
      // Center the fine amount based on number of digits
      int xPos = 85;
      if (totalFine >= 1000) xPos = 78;      // 4+ digits
      else if (totalFine >= 100) xPos = 82;  // 3 digits
      else if (totalFine >= 10) xPos = 86;   // 2 digits
      else xPos = 90;                        // 1 digit

      display.setCursor(xPos, 40);  // Adjusted for border
      display.print(totalFine);
    }

    display.display();
    lastDisplayUpdate = millis();
  }
}



float calculateCOPPM(float sensorValue) {
  if (!isCalibrated) return 0.0;

  // FIXED: Better CO PPM calculation that handles all ratio values
  float ratio = sensorValue / baselineValue;
  float ppm = 0.0;

  if (ratio > 0.1) {  // Only calculate if ratio is meaningful
    // Improved exponential relationship for CO concentration
    // This formula ensures values can go both up and down properly
    ppm = (ratio - 1.0) * 100.0;  // Simple linear relationship

    // Ensure ppm is never negative
    if (ppm < 0) ppm = 0.0;

    // Cap at sensor maximum
    if (ppm > 2000) ppm = 2000;
  }

  return ppm;
}

// Send CO Violation SMS with UPDATED FINE STRUCTURE
void SendCOViolationSMS(float coPPM) {
  Serial.println("=== SENDING CO VIOLATION SMS ===");

  // UPDATED FINE CALCULATION: Rs 500 base + Rs 300 per 50ppm above threshold
  float excessPPM = coPPM - 75.0;                // Using actual threshold of 75ppm
  int baseFine = 500;                            // Rs 500 base fine
  int additionalSets = (int)(excessPPM / 50.0);  // Number of 50ppm sets
  int variableFine = additionalSets * 300;       // Rs 300 per 50ppm set
  int totalCOFine = baseFine + variableFine;

  coViolationCounter++;

  // Create violation message with ACTUAL CO reading
  String SMS = "EMISSION VIOLATION\n";
  SMS += "CO:" + String(coPPM, 0) + "ppm (Limit:75)\n";  // ACTUAL PPM HERE
  SMS += "Fine: Rs" + String(totalCOFine) + "\n";
  SMS += "ID:#E" + String(coViolationCounter) + "\n";
  SMS += "02/09/25 03:14\n";
  SMS += "Pay: bit.ly/paytraffic\n";
  SMS += "-Alipore RTO Kolkata";  // UPDATED AUTHORITY

  Serial.println("CO Message to send:");
  Serial.println(SMS);
  Serial.println("Length: " + String(SMS.length()) + " chars");
  Serial.println("Actual CO Level: " + String(coPPM, 1) + " ppm");
  Serial.println("Fine Calculation: Rs" + String(baseFine) + " + Rs" + String(variableFine) + " = Rs" + String(totalCOFine));
  Serial.println("------------------------");

  // Your exact SMS sending sequence
  SerialAT.println("AT+CMGF=1");  //Sets the GSM Module in Text Mode
  delay(1000);
  SerialAT.println("AT+CMGS=\"" + number + "\"\r");  //Mobile phone number to send message
  delay(1000);
  SerialAT.println(SMS);  // Send violation message with actual CO reading
  delay(100);
  SerialAT.println((char)26);  // ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();

  Serial.println("CO Violation SMS sent to: " + number);
  Serial.println("=== CO SMS SENDING COMPLETE ===");
}

// Send Sound Violation SMS - UPDATED AUTHORITY
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
    SMS += "-Alipore RTO Kolkata";  // UPDATED AUTHORITY

    Serial.println("HIGH DECIBEL VIOLATION");
    Serial.println("Actual Sound Level: " + String(soundDB) + " dB");
  } else if (violationType == "COUNT_EXCEEDED") {
    // Horn Count Exceeded Violation Message
    violationFine = countExceedPenaltyCount * 50;
    SMS = "HORN COUNT VIOLATION\n";
    SMS += "Honks:" + String(honkCount) + "/5 (Exceeded)\n";
    SMS += "Fine: Rs" + String(violationFine) + "\n";
    SMS += "ID:#H" + String(soundViolationCounter) + "\n";
    SMS += "02/09/25 03:14\n";
    SMS += "Pay: bit.ly/paytraffic\n";
    SMS += "-Alipore RTO Kolkata";  // UPDATED AUTHORITY

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
  SerialAT.println("AT+CMGF=1");  //Sets the GSM Module in Text Mode
  delay(1000);
  SerialAT.println("AT+CMGS=\"" + number + "\"\r");  //Mobile phone number to send message
  delay(1000);
  SerialAT.println(SMS);  // Send violation message
  delay(100);
  SerialAT.println((char)26);  // ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();

  Serial.println("Sound Violation SMS sent to: " + number);
  Serial.println("=== SOUND SMS SENDING COMPLETE ===");
}

// YOUR EXACT ORIGINAL FUNCTIONS - UNCHANGED
void SendMessage() {
  Serial.println("Sending test message...");
  SerialAT.println("AT+CMGF=1");  //Sets the GSM Module in Text Mode
  delay(1000);
  SerialAT.println("AT+CMGS=\"" + number + "\"\r");  //Mobile phone number to send message
  delay(1000);
  String SMS = "I AM JOY";
  SerialAT.println(SMS);
  delay(100);
  SerialAT.println((char)26);  // ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();
}

void RecieveMessage() {
  Serial.println("VVM501 AT7670C Read an SMS");
  delay(1000);
  SerialAT.println("AT+CNMI=2,2,0,0,0");  // AT Command to receive a live SMS
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