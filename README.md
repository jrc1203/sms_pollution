# Sonic Guard: Smart Traffic Monitoring System
![Project Status](https://img.shields.io/badge/status-active-brightgreen) ![ESP32](https://img.shields.io/badge/platform-ESP32-blue) ![License](https://img.shields.io/badge/license-MIT-green)

**Sonic Guard** is an ESP32-based IoT system that monitors traffic for emission (Carbon Monoxide) and sound violations in real-time. It integrates an MQ7 CO sensor, sound monitoring, and an A7670C GSM module to automatically detect violations and send SMS alerts. The system features a modern web dashboard for remote monitoring, device management, and configuration.

## 🚀 Key Features

- **🌡️ Real-Time CO \& Sound Monitoring**
    - **CO Detection**: MQ7 sensor monitors carbon monoxide levels; violations trigger when levels exceed 200 ppm for 5+ seconds
    - **Sound Detection**: Monitors ambient and horn sound levels; violations trigger above 60 dB or excessive horn use (>5 presses)
- **📱 Automated SMS Alerts**
    - Sends detailed violation messages via SMS using A7670C SIM module
    - Dynamic fine calculation based on violation severity
    - Customizable recipient phone number
- **🖥️ Live Web Dashboard**
    - Modern responsive dashboard with real-time monitoring
    - WebSocket-based live updates
    - Historical violations tracking
    - Remote device controls (reboot, recalibrate, test SMS)
- **📟 OLED Display**
    - Local 0.96" OLED screen showing live sensor readings
    - System status, fines, and violation counts
    - Real-time feedback without needing internet access
- **💰 Smart Fine Calculation**
    - CO violation: ₹500 base + ₹300 per 50ppm above threshold
    - Sound violations: ₹20 per high dB event, ₹50 for horn count excess
- **🕐 NTP Time Synchronization**
    - Accurate timestamps for violations and SMS records


## 🛠️ Hardware Requirements

| Component | Purpose | Pin Connection | Notes |
| :-- | :-- | :-- | :-- |
| ESP32 Development Board | Main microcontroller | - | WROOM-32 variant recommended |
| A7670C GSM Module | SMS alerts via cellular | TX→GPIO26, RX→GPIO27, PWR→GPIO4 | **Requires external 5V 2A+ power supply** |
| MQ-7 Gas Sensor | CO detection | Analog→GPIO32 | 15-second calibration required |
| SSD1306 OLED Display | Local data display | SDA→GPIO21, SCL→GPIO22 | I2C 0.96" 128x64 |
| Push Button | Horn simulation | Signal→GPIO18 | Uses `INPUT_PULLUP` |
| Buzzer | Audible feedback | Positive→GPIO25 (DAC) | Active buzzer recommended |
| Sound Sensor | Demonstration | Analog→GPIO34 | KY-038 or similar |
| **Power Supply** | **GSM Module Power** | **5V 2A minimum** | **Critical - do not use ESP32 5V pin** |
| Capacitor | Power stabilization | 100µF-1000µF across GSM VCC/GND | Prevents voltage drops |

> ⚠️ **Critical**: The A7670C module can draw up to 2A when connecting to network. Always use a dedicated external power supply with common ground to ESP32.

## 📦 Software \& Libraries

### Prerequisites

1. **Arduino IDE** - [Download here](https://www.arduino.cc/en/software)
2. **ESP32 Board Support** - [Installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)

### Required Libraries

Install via Arduino IDE Library Manager (`Sketch > Include Library > Manage Libraries...`):

```
- ESPAsyncWebServer
- AsyncTCP  
- WebSockets by Markus Sattler
- ArduinoJson by Benoit Blanchon
- Adafruit GFX Library
- Adafruit SSD1306
```


## 🔧 Installation \& Setup

### 1. Clone/Download Code

```bash
git clone https://github.com/your-repo/sonic-guard.git
cd sms_pollution/Final_5.0_jrc/
```


### 2. Configure Credentials

Edit these lines in the code:

```cpp
// WiFi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// SMS recipient
String number = "+91XXXXXXXXXX";  // Replace with your number
```


### 3. Hardware Wiring

Wire components according to the pin definitions:


| ESP32 Pin | Component | Wire Color Suggestion |
| :-- | :-- | :-- |
| GPIO32 | MQ7 Analog Out | Yellow |
| GPIO21 | OLED SDA | Blue |
| GPIO22 | OLED SCL | Green |
| GPIO18 | Button Signal | Orange |
| GPIO25 | Buzzer Positive | Red |
| GPIO26 | GSM Module TX | Purple |
| GPIO27 | GSM Module RX | Gray |
| GPIO4 | GSM Power Control | Brown |
| GPIO34 | Sound Sensor | White |

### 4. Upload Code

- Connect ESP32 via USB
- Select correct board: `Tools > Board > ESP32 Dev Module`
- Select correct port: `Tools > Port`
- Click **Upload**


## 🎯 Usage Guide

### Initial Startup

1. **Power On**: Device shows "SONIC GUARD" on OLED
2. **Initialization**: Watch status messages (WiFi connection, modem setup, etc.)
3. **MQ7 Calibration**: **Critical 15-second calibration period** - do not disturb
4. **Ready**: System displays "System Ready!" and begins monitoring

### Web Dashboard Access

1. Open Serial Monitor (115200 baud) to find IP address
2. Look for: `WiFi connected! IP: xxx.xxx.xxx.xxx`
3. Open browser and navigate to that IP address
4. Dashboard shows live data and controls

### Testing the System

- **Horn Simulation**: Press the button to simulate horn honks
    - 70% chance normal sound (30-35 dB)
    - 30% chance loud sound (60-70 dB) - triggers violation
- **CO Testing**: Expose MQ7 sensor to smoke/gas source
- **Remote Controls**: Use dashboard buttons for testing


## 📨 SMS Violation Examples

### CO Emission Violation

```
EMISSION VIOLATION
CO2:1100 mg/km (Limit:1000 mg/km)
Fine: Rs800
ID:#E3
15/09/25 14:30
Pay: https://echallan.parivahan.gov.in
-Alipore RTO Kolkata
```


### Sound Violation

```
SOUND VIOLATION
dB:68 dB (Limit:60)
Fine: Rs20
ID:#S5
15/09/25 14:32
Pay: https://echallan.parivahan.gov.in
-Alipore RTO Kolkata
```


### Horn Count Violation

```
HORN COUNT VIOLATION
Honks:6/5 (Exceeded)
Fine: Rs50
ID:#H2
15/09/25 14:35
Pay: https://echallan.parivahan.gov.in
-Alipore RTO Kolkata
```


## 🔧 Project Logic Explained

### Violation Detection

- **CO Violations**: Triggered when CO > 200 ppm **sustained for 5 seconds**
- **Sound Violations**: Two types:
    - High dB: Sound level ≥ 60 dB
    - Count exceeded: More than 5 horn presses


### Fine Calculation

- **CO**: ₹500 base + ₹300 per 50ppm above threshold
- **High dB**: ₹20 per incident
- **Horn excess**: ₹50 per violation


### SMS Limitations

- Standard SMS limit: 160 characters
- Messages designed to fit within this limit
- Exceeding may split into multiple SMS (extra cost)


## 🚨 Troubleshooting

### Common Issues

| Problem | Likely Cause | Solution |
| :-- | :-- | :-- |
| ESP32 keeps restarting | Insufficient GSM module power | Use external 5V 2A+ supply |
| OLED display blank | Wiring or I2C address | Check connections, try 0x3D address |
| No SMS sent | SIM card issues | Check activation, credit, PIN lock |
| Dashboard not loading | WiFi connection | Verify ESP32 WiFi connection |
| GSM not connecting | No antenna/poor signal | Attach antenna, check signal strength |

### Power Issues

- **Most common failure**: Inadequate GSM module power
- **Solution**: Dedicated 5V 2A+ power supply + large capacitor (100µF-1000µF)
- **Never** power GSM module from ESP32 5V pin


### Library Conflicts

- Use exact library versions mentioned
- AsyncWebServer libraries can have version conflicts
- Clear Arduino cache if needed: `Arduino15/packages`


## 🔄 Customization

### Modify Thresholds

```cpp
const float CO_THRESHOLD = 200.0;              // ppm
const int SOUND_THRESHOLD = 60;                // dB
```


### Change Fine Structure

Edit functions: `SendCOViolationSMS()` and `SendSoundViolationSMS()`

### Add Sensors

1. Define new pins
2. Add sensor reading in `loop()`
3. Update dashboard HTML and WebSocket data
4. Add to OLED display function

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 👨‍💻 Author

Created by **Joy** ([jrc1203](https://github.com/jrc1203))

For questions, issues, or contributions, please open an issue on GitHub.

***

## 📊 Technical Specifications

- **Microcontroller**: ESP32 (240MHz dual-core)
- **Connectivity**: WiFi 802.11b/g/n, GSM/4G via A7670C
- **Sensors**: MQ7 (CO), Sound level simulation
- **Display**: SSD1306 OLED 128x64
- **Power**: 5V external for GSM, 3.3V for sensors
- **Memory**: Real-time processing, no data logging
- **Communication**: HTTP server (port 80), WebSocket (port 81)

***

*This README provides comprehensive setup and usage instructions. For detailed code documentation, see the inline comments in the source code.*
<span style="display:none">[^1]</span>

<div style="text-align: center">⁂</div>

[^1]: paste.txt

