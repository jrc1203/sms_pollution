---

# Sonic Guard – Smart SMS-Based Pollution & Noise Traffic Monitor

## Overview
**Sonic Guard** is an ESP32-based IoT system designed for real-time traffic pollution and noise monitoring. It integrates an MQ7 CO sensor, a sound monitor, and a SIM A7670C module to automatically detect violations and send SMS alerts to authorities or users. The system also provides a live web dashboard for monitoring, device management, and configuration.

## Key Features

- **Real-Time CO & Sound Monitoring**:  
  - **CO Detection**: MQ7 sensor monitors carbon monoxide (CO) levels; violations trigger when levels exceed 200 ppm.
  - **Sound Detection**: Monitors ambient and simulated horn sound levels; violations trigger above 60 dB or excessive horn use.
- **Automated SMS Alerts**:  
  - Sends detailed violation messages via SMS using the A7670C SIM module.
  - Customizable recipient phone number.
- **OLED Display**:  
  - Live display of sensor readings, system status, fines, and violations.
- **Web Dashboard**:  
  - Modern responsive dashboard (AsyncWebServer + WebSocket) for live monitoring, historical violations, system status, and remote controls.
- **Remote Device Management**:  
  - Send test SMS, force sensor recalibration, reboot device, change SMS recipient.
- **Fine Calculation**:  
  - CO violation: Rs 500 base + Rs 300 per 50ppm above threshold.
  - Sound violations: Rs 20 per high dB event, Rs 50 for every horn count excess.
- **NTP Time Sync**:  
  - Ensures accurate timestamps for violations and SMS records.

## Hardware Requirements

- **ESP32 board**
- **MQ7 CO Sensor** connected to GPIO32
- **Sound Sensor** simulation via button (GPIO18), analog input (GPIO34)
- **OLED Display** (SSD1306 128x64, I2C on GPIO21/SDA, GPIO22/SCL)
- **A7670C SIM Module** (RXD2=GPIO27, TXD2=GPIO26, Power=GPIO4)
- **Buzzer** on DAC pin GPIO25

## Installation & Setup

1. **Clone the repository:**
    ```sh
    git clone https://github.com/jrc1203/sms_pollution.git
    cd sms_pollution/final_4.0
    ```

2. **Flash the ESP32:**
    - Open `final_4.0.ino` in Arduino IDE or VSCode with PlatformIO.
    - Select the correct board and COM port.
    - Upload the sketch.

3. **Configure WiFi in the Code:**
    ```c++
    const char* ssid = "YOUR_SSID";
    const char* password = "YOUR_PASSWORD";
    ```
    > Replace with your credentials.

4. **Connect all hardware as per pin assignments in the code.**

## Usage

### Web Dashboard

- Connect to the same WiFi network as the ESP32.
- Open your browser and visit:  
  ```
  http://<ESP32_IP_ADDRESS>
  ```
- The dashboard displays live CO and sound levels, fines, violations, system info, and includes remote controls.

### SMS Alerts

- SMS are sent automatically for:
  - CO > 200 ppm (with fine calculation)
  - Sound level > 60 dB
  - Horn count > 5
- You can change the SMS recipient number from the dashboard.

### OLED Display

- Shows real-time readings and system status directly on the device.

## Main Functional Blocks

- **Sensor Reading & Calibration**
  - MQ7 sensor is calibrated for 15 seconds at startup.
  - Running average for stable readings.
- **Violation Detection**
  - CO violation requires sustained high value for 5 seconds before SMS.
  - Sound violation based on button press simulation and analog sensor value.
- **SMS Sending**
  - Uses SIM module AT commands for sending formatted violation messages.
- **Web & WebSocket Communication**
  - Live updates to dashboard clients.
  - Handles commands (send SMS, recalibrate, reboot, change number).
- **Fine & Violation Management**
  - Tracks number of violations, calculates fines, and displays all on both dashboard and OLED.

## Example Violation SMS

**CO Violation:**
```
EMISSION VIOLATION
CO:1100 (Limit:1000)
Fine: Rs800
ID:#E3
10/09/25 16:14
Pay: https://echallan.parivahan.gov.in
-Alipore RTO Kolkata
```
**Sound Violation:**
```
SOUND VIOLATION
dB:78 (Limit:60)
Fine: Rs140
ID:#S5
10/09/25 16:15
Pay: https://echallan.parivahan.gov.in
-Alipore RTO Kolkata
```

## Customization

- **Change CO/Sound Thresholds:**  
  Edit `CO_THRESHOLD` and `SOUND_THRESHOLD` in the code.
- **Change Recipient Number:**  
  Update via dashboard or directly in code (`String number = "+91xxxxxxxxxx";`).
- **Add More Sensors:**  
  Expand sensor reading blocks and dashboard cards.

## Troubleshooting

- **OLED not working?** Double-check wiring and I2C address (`0x3C` or `0x3D`).
- **Web dashboard not loading?** Ensure ESP32 is connected to WiFi, and you use correct IP.
- **SMS not sent?** Check SIM card status and number format.

## License

Currently, no license specified.  
If you wish to open source, add a license file (MIT recommended).

## Author

Created by [jrc1203](https://github.com/jrc1203)

---

**For more details, see the main code:**  
[final_4.0.ino](https://github.com/jrc1203/sms_pollution/blob/main/final_4.0/final_4.0.
