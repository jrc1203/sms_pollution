//THIS EXAMPLE SHOWS HOW VVM501 ESP32 4G LTE MODULE CAN USE TO SEND AND RECEIVE SMS AND CALL
//WITH OLED 128x64 DISPLAY SUPPORT
//FOR VVM501 PRODUCT DETAILS VISIT www.vv-mobility.com

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // Common I2C address for SSD1306

// VVM501 Module pins
#define RXD2 27    //VVM501 MODULE RXD INTERNALLY CONNECTED
#define TXD2 26    //VVM501 MODULE TXD INTERNALLY CONNECTED
#define powerPin 4 //VVM501 MODULE ESP32 PIN D4 CONNECTED TO POWER PIN OF A7670C CHIPSET, INTERNALLY CONNECTED

// OLED I2C pins (ESP32 default)
#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int rx = -1;
#define SerialAT Serial1
String rxString;
int _timeout;
String _buffer;
String number = "+916291175964"; //REPLACE WITH YOUR NUMBER

// OLED display variables
String oledBuffer[8]; // Buffer to store last 8 lines for OLED
int oledLineCount = 0;

void setup() {
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);
  Serial.begin(115200);
  delay(100);
  
  // Initialize I2C for OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  
  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    printToOLED("OLED Init Failed!");
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("VVM501 4G Module");
  display.println("Initializing...");
  display.display();
  delay(2000);
  
  SerialAT.begin(115200, SERIAL_8N1, RXD2, TXD2);
  delay(10000);

  printToSerialAndOLED("Modem Reset, please wait");
  SerialAT.println("AT+CRESET");
  delay(1000);
  SerialAT.println("AT+CRESET");
  delay(20000);  // WAITING FOR SOME TIME TO CONFIGURE MODEM

  SerialAT.flush();

  printToSerialAndOLED("Echo Off");
  SerialAT.println("ATE0");   //120s
  delay(1000);
  SerialAT.println("ATE0");   //120s
  rxString = SerialAT.readString();
  printToSerialAndOLED("Got: " + rxString);
  rx = rxString.indexOf("OK");
  if (rx != -1)
    printToSerialAndOLED("Modem Ready");
  delay(1000);

  printToSerialAndOLED("SIM card check");
  SerialAT.println("AT+CPIN?"); //9s
  rxString = SerialAT.readString();
  printToSerialAndOLED("Got: " + rxString);
  rx = rxString.indexOf("+CPIN: READY");
  if (rx != -1)
    printToSerialAndOLED("SIM Card Ready");
  delay(1000);
  
  // Clear display and show menu
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("VVM501 Ready!");
  display.println("Commands:");
  display.println("s - Send SMS");
  display.println("r - Receive SMS");
  display.println("c - Make Call");
  display.display();
  
  printToSerialAndOLED("Type s to send SMS, r to receive SMS, c to make call");
}

void loop() {
  if (Serial.available() > 0)
    switch (Serial.read())
    {
      case 's':
        SendMessage();   //YOU CAN SEND MESSAGE FROM SIM TO THE MENTIONED PHONE NUMBER
        break;
      case 'r':
        RecieveMessage(); // RECEIVE MESSAGE FROM THE MENTIONED PHONE NUMBER TO SIM
        break;
      case 'c':
        callNumber();    // CALL
        break;
    }
  
  // Check for incoming SMS and other responses
  if (SerialAT.available() > 0) {
    String response = SerialAT.readString();
    Serial.print(response);
    
    // Check if it's an incoming SMS
    if(response.indexOf("+CMT:") != -1) {
      parseIncomingSMS(response);
    }
    // Check for other important responses
    else if(response.indexOf("RING") != -1) {
      printToSerialAndOLED("Incoming Call!");
    }
    else if(response.indexOf("NO CARRIER") != -1) {
      printToSerialAndOLED("Call Ended");
    }
  }
}

void SendMessage()
{
  printToSerialAndOLED("Sending Message...");
  SerialAT.println("AT+CMGF=1");    //Sets the GSM Module in Text Mode
  delay(1000);
  
  printToSerialAndOLED("Set SMS Number");
  SerialAT.println("AT+CMGS=\"" + number + "\"\r"); //Mobile phone number to send message
  delay(1000);
  
  String SMS = "I AM JOY";
  SerialAT.println(SMS);
  delay(100);
  SerialAT.println((char)26);// ASCII code of CTRL+Z
  delay(1000);
  _buffer = _readSerial();
  
  if(_buffer.indexOf("OK") != -1) {
    printToSerialAndOLED("SMS Sent Successfully!");
  } else {
    printToSerialAndOLED("SMS Send Failed");
  }
}

void RecieveMessage()
{
  printToSerialAndOLED("VVM501 AT7670C Read SMS");
  delay (1000);
  SerialAT.println("AT+CNMI=2,2,0,0,0"); // AT Command to receive a live SMS
  delay(1000);
  printToSerialAndOLED("Waiting for SMS...");
}

String _readSerial() {
  _timeout = 0;
  while  (!SerialAT.available() && _timeout < 12000  )
  {
    delay(13);
    _timeout++;
  }
  if (SerialAT.available()) {
    return SerialAT.readString();
  }
  return "";
}

void callNumber() {
  printToSerialAndOLED("Calling: " + number);
  SerialAT.print (F("ATD"));
  SerialAT.print (number);
  SerialAT.print (F(";\r\n"));
  _buffer = _readSerial();
  printToSerialAndOLED("Call Status: " + _buffer);
}

// Function to print to both Serial Monitor and OLED
void printToSerialAndOLED(String message) {
  // Print to Serial Monitor
  Serial.println(message);
  
  // Add to OLED buffer
  addToOLEDBuffer(message);
  
  // Update OLED display
  updateOLED();
}

// Function to add message to OLED buffer
void addToOLEDBuffer(String message) {
  // Wrap long messages to fit OLED width (approximately 21 characters per line)
  while(message.length() > 21) {
    String line = message.substring(0, 21);
    
    // Shift buffer up
    for(int i = 0; i < 7; i++) {
      oledBuffer[i] = oledBuffer[i + 1];
    }
    oledBuffer[7] = line;
    message = message.substring(21);
  }
  
  if(message.length() > 0) {
    // Shift buffer up
    for(int i = 0; i < 7; i++) {
      oledBuffer[i] = oledBuffer[i + 1];
    }
    oledBuffer[7] = message;
  }
}

// Function to update OLED display
void updateOLED() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Display last 8 lines from buffer
  for(int i = 0; i < 8; i++) {
    if(oledBuffer[i].length() > 0) {
      display.println(oledBuffer[i]);
    }
  }
  
  display.display();
}

// Alternative function to print only to OLED (useful for status updates)
void printToOLED(String message) {
  addToOLEDBuffer(message);
  updateOLED();
}

// Function to parse incoming SMS messages
void parseIncomingSMS(String response) {
  printToSerialAndOLED("=== INCOMING SMS ===");
  
  // Extract phone number from +CMT response
  int firstQuote = response.indexOf("\"");
  int secondQuote = response.indexOf("\"", firstQuote + 1);
  String senderNumber = "";
  
  if(firstQuote != -1 && secondQuote != -1) {
    senderNumber = response.substring(firstQuote + 1, secondQuote);
    printToSerialAndOLED("From: " + senderNumber);
  }
  
  // Extract timestamp
  int thirdQuote = response.indexOf("\"", secondQuote + 1);
  int fourthQuote = response.indexOf("\"", thirdQuote + 1);
  int fifthQuote = response.indexOf("\"", fourthQuote + 1);
  int sixthQuote = response.indexOf("\"", fifthQuote + 1);
  String timestamp = "";
  
  if(fifthQuote != -1 && sixthQuote != -1) {
    timestamp = response.substring(fifthQuote + 1, sixthQuote);
    printToSerialAndOLED("Time: " + timestamp);
  }
  
  // Extract message content (after the last quote and newline)
  int lastQuote = response.lastIndexOf("\"");
  if(lastQuote != -1) {
    String messageContent = response.substring(lastQuote + 1);
    messageContent.trim(); // Remove whitespace and newlines
    
    if(messageContent.length() > 0) {
      printToSerialAndOLED("Message:");
      printToSerialAndOLED(messageContent);
    }
  }
  
  printToSerialAndOLED("================");
}