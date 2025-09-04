///### BASE CODE

// ESP32 SMS System with A7670C Module + MQ7 Sensor
// Sends violation SMS when CO exceeds threshold

// Pin definitions
#define MQ7_PIN 32

// SIM A7670C Module pins
#define RXD2 27    //VVM501 MODULE RXD INTERNALLY CONNECTED
#define TXD2 26    //VVM501 MODULE TXD INTERNALLY CONNECTED
#define powerPin 4 //VVM501 MODULE ESP32 PIN D4 CONNECTED TO POWER PIN OF A7670C CHIPSET, INTERNALLY CONNECTED

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
const unsigned long CALIBRATION_TIME = 10000; // 45 seconds
const float CO_THRESHOLD = 75.0; // ppm threshold

// Timing variables
unsigned long lastReadTime = 0;
const unsigned long READ_INTERVAL = 1000; // Read sensor every 1 second

// Violation management
int violationCounter = 0;
bool inViolation = false;
unsigned long violationStartTime = 0;
bool violationStable = false;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize MQ7 pin
  pinMode(MQ7_PIN, INPUT);
  
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
  Serial.println("=== STARTING MQ7 CALIBRATION (45 seconds) ===");
  calibrationStartTime = millis();
  
  Serial.println("=== SMS + MQ7 SYSTEM READY ===");
  Serial.println("Commands:");
  Serial.println("'s' = Send test SMS");
  Serial.println("'r' = Receive SMS");
  Serial.println("'c' = Make call");
  Serial.println("Automatic violation SMS when CO > 75 ppm");
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
  
  // Check for violations
  checkViolation(coPPM);
}

void checkViolation(float coPPM) {
  if (coPPM > CO_THRESHOLD) {
    if (!inViolation) {
      // Start of potential violation
      inViolation = true;
      violationStartTime = millis();
      violationStable = false;
      Serial.println("🚨 Potential violation detected, checking stability...");
    } else {
      // Check if violation has been stable for 5 seconds
      if (!violationStable && (millis() - violationStartTime >= 5000)) {
        violationStable = true;
        Serial.println("🚨 VIOLATION CONFIRMED - Sending SMS!");
        
        // Send violation SMS with actual CO reading
        SendViolationSMS(coPPM);
      }
    }
  } else {
    // Reset violation state when CO goes below threshold
    if (inViolation) {
      Serial.println("✅ Violation cleared - CO levels back to normal");
      inViolation = false;
      violationStable = false;
    }
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

// UPDATED: Send Violation SMS with actual CO reading
void SendViolationSMS(float coPPM) {
  Serial.println("=== SENDING VIOLATION SMS ===");
  
  // Calculate fine based on actual CO reading
  float excessPPM = coPPM - CO_THRESHOLD;
  int baseFine = 2500;
  int variableFine = (int)(excessPPM * 100); // ₹100 per ppm
  int totalFine = baseFine + variableFine;
  
  violationCounter++;
  
  // Create violation message with ACTUAL CO reading
  String SMS = "EMISSION VIOLATION\n";
  SMS += "CO:" + String(coPPM, 0) + "ppm (Limit:75)\n";  // ACTUAL PPM HERE
  SMS += "Fine: Rs" + String(totalFine) + "\n";
  SMS += "ID:#E" + String(violationCounter) + "\n";
  SMS += "02/09/25 03:14\n";
  SMS += "Pay: bit.ly/paytraffic\n";
  SMS += "-Pollution Control Board";
  
  Serial.println("Message to send:");
  Serial.println(SMS);
  Serial.println("Length: " + String(SMS.length()) + " chars");
  Serial.println("Actual CO Level: " + String(coPPM, 1) + " ppm");
  Serial.println("Fine Amount: Rs" + String(totalFine));
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
  
  Serial.println("Violation SMS sent to: " + number);
  Serial.println("=== SMS SENDING COMPLETE ===");
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
