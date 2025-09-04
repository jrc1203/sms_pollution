// ESP32 Integrated SMS System with A7670C Module + MQ7 Sensor + Sound Monitor
// Sends violation SMS for both CO and Sound violations

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "time.h" // Added for NTP functionality

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED setup (0.96" 128x64 SSD1306)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C  // Try 0x3D if 0x3C doesn't work

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// WiFi credentials
const char* ssid = "MySpyCar";
const char* password = "123456789";

// NTP Server and Timezone Configuration
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800; // 5 hours 30 minutes for IST (5*3600 + 30*60)
const int   daylightOffset_sec = 0;   // No daylight saving for IST

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
const unsigned long CALIBRATION_TIME = 15000;  // 45 seconds
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

// Web server and WebSocket
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// JSON document for sending data
DynamicJsonDocument jsonDoc(1024);

const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sonic Guard - Traffic Monitor Dashboard</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            color: white;
            overflow-x: hidden;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }

        .header {
            text-align: center;
            margin-bottom: 30px;
            animation: fadeInDown 1s ease-out;
        }

        .header h1 {
            font-size: 3rem;
            font-weight: 700;
            text-shadow: 0 4px 8px rgba(0,0,0,0.3);
            margin-bottom: 10px;
            background: linear-gradient(45deg, #ffd700, #ffed4a);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }

        .header p {
            font-size: 1.2rem;
            opacity: 0.9;
        }

        .status-bar {
            display: flex;
            justify-content: center;
            gap: 20px;
            margin-bottom: 30px;
            flex-wrap: wrap;
        }

        .status-item {
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255,255,255,0.2);
            padding: 15px 25px;
            border-radius: 15px;
            text-align: center;
            animation: fadeInUp 1s ease-out;
        }

        .status-online {
            background: rgba(34, 197, 94, 0.2);
            border-color: rgba(34, 197, 94, 0.3);
        }

        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 25px;
            margin-bottom: 30px;
        }

        .card {
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(15px);
            border: 1px solid rgba(255,255,255,0.2);
            border-radius: 20px;
            padding: 25px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.1);
            transition: all 0.3s ease;
            animation: fadeInUp 1s ease-out;
        }

        .card:hover {
            transform: translateY(-5px);
            box-shadow: 0 15px 40px rgba(0,0,0,0.2);
        }

        .card-header {
            display: flex;
            align-items: center;
            margin-bottom: 20px;
        }

        .card-icon {
            width: 50px;
            height: 50px;
            border-radius: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-right: 15px;
            font-size: 24px;
        }

        .co-icon { background: linear-gradient(45deg, #ff6b6b, #ee5a24); }
        .sound-icon { background: linear-gradient(45deg, #4ecdc4, #44a08d); }
        .fine-icon { background: linear-gradient(45deg, #feca57, #ff9ff3); }
        .system-icon { background: linear-gradient(45deg, #54a0ff, #5f27cd); }

        .card-title {
            font-size: 1.4rem;
            font-weight: 600;
            margin-bottom: 5px;
        }

        .card-subtitle {
            opacity: 0.8;
            font-size: 0.9rem;
        }

        .metric {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin: 15px 0;
            padding: 12px 0;
            border-bottom: 1px solid rgba(255,255,255,0.1);
        }

        .metric:last-child {
            border-bottom: none;
        }

        .metric-label {
            font-weight: 500;
            opacity: 0.9;
        }

        .metric-value {
            font-size: 1.3rem;
            font-weight: 700;
        }

        .value-normal { color: #2ecc71; }
        .value-warning { color: #f39c12; }
        .value-danger { color: #e74c3c; }

        .progress-bar {
            width: 100%;
            height: 8px;
            background: rgba(255,255,255,0.2);
            border-radius: 4px;
            overflow: hidden;
            margin-top: 10px;
        }

        .progress-fill {
            height: 100%;
            border-radius: 4px;
            transition: all 0.3s ease;
        }

        .progress-normal { background: linear-gradient(90deg, #2ecc71, #27ae60); }
        .progress-warning { background: linear-gradient(90deg, #f39c12, #e67e22); }
        .progress-danger { background: linear-gradient(90deg, #e74c3c, #c0392b); }

        .alert {
            background: rgba(231, 76, 60, 0.2);
            border: 1px solid rgba(231, 76, 60, 0.3);
            border-radius: 15px;
            padding: 20px;
            margin: 20px 0;
            text-align: center;
            animation: pulse 2s infinite;
            display: none;
        }

        .alert.show {
            display: block;
        }

        .alert-icon {
            font-size: 2rem;
            margin-bottom: 10px;
        }

        .violations-list {
            max-height: 200px;
            overflow-y: auto;
            background: rgba(0,0,0,0.1);
            border-radius: 10px;
            padding: 10px;
        }

        .violation-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 8px 0;
            border-bottom: 1px solid rgba(255,255,255,0.1);
            animation: slideInLeft 0.5s ease-out;
        }

        .violation-item:last-child {
            border-bottom: none;
        }

        .violation-time {
            font-size: 0.8rem;
            opacity: 0.7;
        }

        @keyframes fadeInDown {
            from { opacity: 0; transform: translateY(-30px); }
            to { opacity: 1; transform: translateY(0); }
        }

        @keyframes fadeInUp {
            from { opacity: 0; transform: translateY(30px); }
            to { opacity: 1; transform: translateY(0); }
        }

        @keyframes slideInLeft {
            from { opacity: 0; transform: translateX(-30px); }
            to { opacity: 1; transform: translateX(0); }
        }

        @keyframes pulse {
            0%, 100% { transform: scale(1); }
            50% { transform: scale(1.02); }
        }

        @media (max-width: 768px) {
            .container { padding: 15px; }
            .header h1 { font-size: 2rem; }
            .dashboard { grid-template-columns: 1fr; gap: 20px; }
            .status-bar { flex-direction: column; align-items: center; }
            .card { padding: 20px; }
        }

        .connection-status {
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 10px 15px;
            border-radius: 25px;
            font-size: 0.9rem;
            font-weight: 500;
            z-index: 1000;
            transition: all 0.3s ease;
        }

        .connected {
            background: rgba(34, 197, 94, 0.9);
            color: white;
        }

        .disconnected {
            background: rgba(239, 68, 68, 0.9);
            color: white;
        }

        .large-metric {
            text-align: center;
            padding: 20px 0;
        }

        .large-metric .value {
            font-size: 3rem;
            font-weight: 700;
            line-height: 1;
            margin-bottom: 10px;
        }

        .large-metric .unit {
            font-size: 1.2rem;
            opacity: 0.8;
        }
    </style>
</head>
<body>
    <div class="connection-status" id="connectionStatus">🔴 Disconnected</div>
    
    <div class="container">
        <div class="header">
            <h1>SONIC GUARD</h1>
            <p>Smart Traffic Monitoring System</p>
        </div>

        <div class="status-bar">
            <div class="status-item status-online" id="systemStatus">
                <strong>System Status:</strong> <span id="statusText">Initializing...</span>
            </div>
            <div class="status-item">
                <strong>Last Update:</strong> <span id="lastUpdate">--:--:--</span>
            </div>
            <div class="status-item">
                <strong>Uptime:</strong> <span id="uptime">00:00:00</span>
            </div>
        </div>

        <div class="alert" id="violationAlert">
            <div class="alert-icon">🚨</div>
            <div id="alertMessage">Violation Detected!</div>
        </div>

        <div class="dashboard">
            <!-- CO Monitoring Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon co-icon">🏭</div>
                    <div>
                        <div class="card-title">CO Monitoring</div>
                        <div class="card-subtitle">Carbon Monoxide Levels</div>
                    </div>
                </div>
                <div class="large-metric">
                    <div class="value" id="coValue">0</div>
                    <div class="unit">ppm</div>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill progress-normal" id="coProgress" style="width: 0%"></div>
                </div>
                <div class="metric">
                    <span class="metric-label">Threshold:</span>
                    <span class="metric-value">200 ppm</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Status:</span>
                    <span class="metric-value value-normal" id="coStatus">Normal</span>
                </div>
            </div>

            <!-- Sound Monitoring Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon sound-icon">🔊</div>
                    <div>
                        <div class="card-title">Sound Monitoring</div>
                        <div class="card-subtitle">Noise Level Detection</div>
                    </div>
                </div>
                <div class="large-metric">
                    <div class="value" id="soundValue">32</div>
                    <div class="unit">dB</div>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill progress-normal" id="soundProgress" style="width: 20%"></div>
                </div>
                <div class="metric">
                    <span class="metric-label">Threshold:</span>
                    <span class="metric-value">60 dB</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Horn Count:</span>
                    <span class="metric-value" id="hornCount">0/5</span>
                </div>
            </div>

            <!-- Fine Management Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon fine-icon">💰</div>
                    <div>
                        <div class="card-title">Fine Management</div>
                        <div class="card-subtitle">Violation Penalties</div>
                    </div>
                </div>
                <div class="large-metric">
                    <div class="value" id="totalFine">0</div>
                    <div class="unit">Rs</div>
                </div>
                <div class="metric">
                    <span class="metric-label">CO Violations:</span>
                    <span class="metric-value" id="coViolations">0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Sound Violations:</span>
                    <span class="metric-value" id="soundViolations">0</span>
                </div>
                <div class="metric">
                    <span class="metric-label">High dB Count:</span>
                    <span class="metric-value" id="highDbCount">0</span>
                </div>
            </div>

            <!-- System Information Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon system-icon">⚙️</div>
                    <div>
                        <div class="card-title">System Info</div>
                        <div class="card-subtitle">Device Status</div>
                    </div>
                </div>
                <div class="metric">
                    <span class="metric-label">MQ7 Calibration:</span>
                    <span class="metric-value value-normal" id="calibrationStatus">Ready</span>
                </div>
                <div class="metric">
                    <span class="metric-label">SIM Module:</span>
                    <span class="metric-value value-normal" id="simStatus">Connected</span>
                </div>
                <div class="metric">
                    <span class="metric-label">WiFi Signal:</span>
                    <span class="metric-value value-normal" id="wifiSignal">Strong</span>
                </div>
                <div class="metric">
                    <span class="metric-label">Free RAM:</span>
                    <span class="metric-value" id="freeRam">--</span>
                </div>
            </div>

            <!-- Recent Violations Card -->
            <div class="card">
                <div class="card-header">
                    <div class="card-icon" style="background: linear-gradient(45deg, #ff7675, #fd79a8);">📋</div>
                    <div>
                        <div class="card-title">Recent Violations</div>
                        <div class="card-subtitle">Latest Incidents</div>
                    </div>
                </div>
                <div class="violations-list" id="violationsList">
                    <div style="text-align: center; opacity: 0.6; padding: 20px;">
                        No violations recorded
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        class SonicGuardDashboard {
            constructor() {
                this.websocket = null;
                this.startTime = Date.now();
                this.violations = [];
                this.isConnected = false;
                this.demoInterval = null; // To hold the demo mode interval timer
                
                this.initializeWebSocket();
                this.startUptimeCounter();
                this.updateConnectionStatus(); // Set initial status
            }

            initializeWebSocket() {
                // The WebSocket server runs on port 81
                const wsUrl = `ws://${window.location.hostname}:81`;
                
                this.websocket = new WebSocket(wsUrl);
                
                this.websocket.onopen = () => {
                    console.log('WebSocket connected');
                    this.isConnected = true;
                    this.updateConnectionStatus();
                    this.stopDemoMode(); // Stop demo mode on successful connection
                };
                
                this.websocket.onmessage = (event) => {
                    try {
                        const data = JSON.parse(event.data);
                        this.updateDashboard(data);
                    } catch (error) {
                        console.error('Error parsing WebSocket data:', error);
                    }
                };
                
                this.websocket.onclose = () => {
                    console.log('WebSocket disconnected');
                    this.isConnected = false;
                    this.updateConnectionStatus();
                    this.startDemoMode(); // Start demo mode when connection is lost
                    
                    // Attempt to reconnect after 3 seconds
                    setTimeout(() => {
                        this.initializeWebSocket();
                    }, 3000);
                };
                
                this.websocket.onerror = (error) => {
                    console.error('WebSocket error:', error);
                    this.isConnected = false;
                    this.updateConnectionStatus();
                    // onclose will be called after onerror, so no need to start demo here
                };
            }
            
            startDemoMode() {
                if (this.demoInterval) return; // Demo is already running
                console.log("Connection lost. Starting Demo Mode.");

                const updateDemo = () => {
                    const demoData = {
                        coPPM: Math.random() * 150 + 20,
                        soundDB: Math.random() * 40 + 30,
                        pressCount: Math.floor(Math.random() * 4),
                        totalFine: Math.floor(Math.random() * 500),
                        highDbCount: Math.floor(Math.random() * 3),
                        coViolationCounter: 0,
                        soundViolationCounter: 0,
                        isCalibrated: true,
                        simReady: true,
                        systemStatus: 'Demo Mode',
                        freeRAM: '45',
                        wifiSignal: 'Medium'
                    };
                    this.updateDashboard(demoData);
                };

                updateDemo(); // Show first demo data immediately
                this.demoInterval = setInterval(updateDemo, 3000);
            }

            stopDemoMode() {
                if (this.demoInterval) {
                    console.log("Live connection active. Stopping Demo Mode.");
                    clearInterval(this.demoInterval);
                    this.demoInterval = null;
                }
            }

            updateConnectionStatus() {
                const statusElement = document.getElementById('connectionStatus');
                if (this.isConnected) {
                    statusElement.textContent = '🟢 Connected';
                    statusElement.className = 'connection-status connected';
                } else {
                    statusElement.textContent = '🔴 Disconnected';
                    statusElement.className = 'connection-status disconnected';
                }
            }

            updateDashboard(data) {
                // Update last update time
                const now = new Date();
                document.getElementById('lastUpdate').textContent = now.toLocaleTimeString();
                
                // Update system status
                document.getElementById('statusText').textContent = data.systemStatus || 'Running';
                
                // Update CO data
                this.updateCOData(data);
                
                // Update Sound data
                this.updateSoundData(data);
                
                // Update Fine data
                this.updateFineData(data);
                
                // Update System info
                this.updateSystemInfo(data);
                
                // Check for violations
                this.checkViolations(data);
            }

            updateCOData(data) {
                const coValue = parseFloat(data.coPPM || 0);
                const threshold = 200;
                
                document.getElementById('coValue').textContent = Math.round(coValue);
                
                // Update progress bar
                const percentage = Math.min((coValue / threshold) * 100, 100);
                const progressBar = document.getElementById('coProgress');
                progressBar.style.width = percentage + '%';
                
                // Update status and colors
                const statusElement = document.getElementById('coStatus');
                if (coValue > threshold) {
                    statusElement.textContent = 'VIOLATION';
                    statusElement.className = 'metric-value value-danger';
                    progressBar.className = 'progress-fill progress-danger';
                } else if (coValue > threshold * 0.8) {
                    statusElement.textContent = 'Warning';
                    statusElement.className = 'metric-value value-warning';
                    progressBar.className = 'progress-fill progress-warning';
                } else {
                    statusElement.textContent = 'Normal';
                    statusElement.className = 'metric-value value-normal';
                    progressBar.className = 'progress-fill progress-normal';
                }
            }

            updateSoundData(data) {
                const soundLevel = parseInt(data.soundDB || 32);
                const threshold = 60;
                
                document.getElementById('soundValue').textContent = soundLevel;
                document.getElementById('hornCount').textContent = `${data.pressCount || 0}/5`;
                
                // Update progress bar
                const percentage = Math.min((soundLevel / 100) * 100, 100);
                const progressBar = document.getElementById('soundProgress');
                progressBar.style.width = percentage + '%';
                
                // Update colors based on threshold
                if (soundLevel >= threshold) {
                    progressBar.className = 'progress-fill progress-danger';
                } else if (soundLevel >= threshold * 0.8) {
                    progressBar.className = 'progress-fill progress-warning';
                } else {
                    progressBar.className = 'progress-fill progress-normal';
                }
            }

            updateFineData(data) {
                document.getElementById('totalFine').textContent = data.totalFine || 0;
                document.getElementById('coViolations').textContent = data.coViolationCounter || 0;
                document.getElementById('soundViolations').textContent = data.soundViolationCounter || 0;
                document.getElementById('highDbCount').textContent = data.highDbCount || 0;
            }

            updateSystemInfo(data) {
                document.getElementById('calibrationStatus').textContent = 
                    data.isCalibrated ? 'Ready' : 'Calibrating...';
                document.getElementById('simStatus').textContent = 
                    data.simReady ? 'Connected' : 'Disconnected';
                document.getElementById('freeRam').textContent = 
                    data.freeRAM ? `${data.freeRAM} KB` : '--';
                document.getElementById('wifiSignal').textContent = 
                    data.wifiSignal || 'Strong';
            }

            checkViolations(data) {
                const alertElement = document.getElementById('violationAlert');
                const alertMessage = document.getElementById('alertMessage');
                
                let hasViolation = false;
                let message = '';
                
                // Check CO violation
                if (data.coPPM > 200) {
                    hasViolation = true;
                    message = `CO Violation: ${Math.round(data.coPPM)} ppm detected!`;
                }
                
                // Check Sound violation
                if (data.soundDB >= 60) {
                    hasViolation = true;
                    message = hasViolation ? 
                        `Multiple Violations: CO & Sound!` : 
                        `Sound Violation: ${data.soundDB} dB detected!`;
                }
                
                // Check horn count
                if (data.pressCount > 5) {
                    hasViolation = true;
                    message = hasViolation ? 
                        `Multiple Violations Detected!` : 
                        `Horn Count Exceeded: ${data.pressCount}/5`;
                }
                
                if (hasViolation) {
                    alertElement.className = 'alert show';
                    alertMessage.textContent = message;
                    
                    // Add to violations list
                    this.addViolation(message);
                } else {
                    alertElement.className = 'alert';
                }
            }

            addViolation(message) {
                const now = new Date();
                const violation = {
                    message: message,
                    time: now.toLocaleTimeString(),
                    timestamp: now.getTime()
                };
                
                this.violations.unshift(violation);
                
                // Keep only last 10 violations
                if (this.violations.length > 10) {
                    this.violations = this.violations.slice(0, 10);
                }
                
                this.updateViolationsList();
            }

            updateViolationsList() {
                const listElement = document.getElementById('violationsList');
                
                if (this.violations.length === 0) {
                    listElement.innerHTML = `
                        <div style="text-align: center; opacity: 0.6; padding: 20px;">
                            No violations recorded
                        </div>
                    `;
                    return;
                }
                
                let html = '';
                this.violations.forEach(violation => {
                    html += `
                        <div class="violation-item">
                            <div>
                                <div>${violation.message}</div>
                                <div class="violation-time">${violation.time}</div>
                            </div>
                            <div style="color: #e74c3c;">⚠️</div>
                        </div>
                    `;
                });
                
                listElement.innerHTML = html;
            }

            startUptimeCounter() {
                setInterval(() => {
                    const uptimeMs = Date.now() - this.startTime;
                    const hours = Math.floor(uptimeMs / (1000 * 60 * 60));
                    const minutes = Math.floor((uptimeMs % (1000 * 60 * 60)) / (1000 * 60));
                    const seconds = Math.floor((uptimeMs % (1000 * 60)) / 1000);
                    
                    const uptimeStr = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
                    document.getElementById('uptime').textContent = uptimeStr;
                }, 1000);
            }
        }

        // Initialize dashboard when page loads
        document.addEventListener('DOMContentLoaded', () => {
            window.dashboard = new SonicGuardDashboard();
        });
    </script>
</body>
</html>)rawliteral";

String getFormattedDateTime(); // Function prototype

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED:
      Serial.printf("[%u] Connected from %s\n", num, webSocket.remoteIP(num).toString().c_str());
      break;
    case WStype_TEXT:
      Serial.printf("[%u] Received Text: %s\n", num, payload);
      break;
    default:
      break;
  }
}

void sendWebSocketData() {
  jsonDoc.clear();
  
  // Calculate current values
  float coPPM = isCalibrated ? calculateCOPPM(runningAverage) : 0;
  int currentSoundDB = (digitalRead(BUTTON) == LOW) ? simulatedSoundDB : ambientSoundDB;
  
  // Populate JSON with current sensor data
  jsonDoc["coPPM"] = coPPM;
  jsonDoc["soundDB"] = currentSoundDB;
  jsonDoc["pressCount"] = pressCount;
  jsonDoc["totalFine"] = totalFine;
  jsonDoc["highDbCount"] = highDbCount;
  jsonDoc["coViolationCounter"] = coViolationCounter;
  jsonDoc["soundViolationCounter"] = soundViolationCounter;
  jsonDoc["isCalibrated"] = isCalibrated;
  jsonDoc["simReady"] = true; // Based on your SIM status
  jsonDoc["systemStatus"] = initializationComplete ? "Running" : currentInitStatus;
  jsonDoc["freeRAM"] = String(ESP.getFreeHeap() / 1024);
  jsonDoc["wifiSignal"] = WiFi.RSSI() > -50 ? "Strong" : WiFi.RSSI() > -70 ? "Medium" : "Weak";
  
  String jsonString;
  serializeJson(jsonDoc, jsonString);
  webSocket.broadcastTXT(jsonString);
}

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
  updateOLEDDisplay(); // Update display with current status
  SerialAT.println("AT+CRESET");
  delay(1000);
  SerialAT.println("AT+CRESET");
  delay(3000);

  SerialAT.flush();

  Serial.println("Echo Off");
  currentInitStatus = "Echo Off...";
  updateOLEDDisplay();
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
    updateOLEDDisplay();
  }
  delay(1000);

  Serial.println("SIM card check");
  currentInitStatus = "SIM Check...";
  updateOLEDDisplay();
  SerialAT.println("AT+CPIN?");  //9s
  rxString = SerialAT.readString();
  Serial.print("Got: ");
  Serial.println(rxString);
  rx = rxString.indexOf("+CPIN: READY");
  if (rx != -1) {
    Serial.println("SIM Card Ready");
    currentInitStatus = "SIM Ready";
    updateOLEDDisplay();
  }
  delay(1000);

  // Start MQ7 calibration
  Serial.println("=== STARTING MQ7 CALIBRATION (15 seconds) ===");
  currentInitStatus = "MQ7 Calibrating...";
  calibrationStartTime = millis();

  // Block execution and wait for MQ7 calibration to finish
  while(!isCalibrated) {
    if (millis() - lastReadTime >= READ_INTERVAL) {
        readMQ7Sensor(); // This function will update calibration status
        lastReadTime = millis();
    }
    updateOLEDDisplay(); // Show calibration progress on OLED
    delay(100); // Small delay to prevent watchdog reset
  }
  
  // Initialize WiFi only after SIM and MQ7 are ready
  currentInitStatus = "Connecting WiFi...";
  updateOLEDDisplay();
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected! IP: ");
  Serial.println(WiFi.localIP());
  
  // Initialize NTP Client after WiFi is connected
  currentInitStatus = "Syncing Time...";
  updateOLEDDisplay();
  Serial.println("Initializing NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Wait for time to be synchronized
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time. Retrying...");
    delay(1000);
  }
  Serial.println("Time synchronized successfully!");
  currentInitStatus = "Time Synced!";
  updateOLEDDisplay();
  delay(1500);
  
  // Setup web server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", html_page);
  });
  
  server.begin();
  Serial.println("HTTP server started");
  
  // Setup WebSocket
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.println("WebSocket server started on port 81");

  Serial.println("=== SMS + MQ7 + SOUND SYSTEM READY ===");
  Serial.println("Commands:");
  Serial.println("'s' = Send test SMS");
  Serial.println("'r' = Receive SMS");
  Serial.println("'c' = Make call");
  Serial.println("Automatic violation SMS when CO > 200 ppm OR Sound violations");
}

void loop() {
  // Handle WebSocket connections
  webSocket.loop();
  
  // Send data to all connected clients periodically
  if(millis() - lastReadTime >= READ_INTERVAL) {
      sendWebSocketData();
  }
  
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

  // Calculate difference from baseline instead of ratio
  float difference = sensorValue - baselineValue;
  float ppm = 0.0;

  if (difference > 0) {
    // Use linear relationship based on difference, not ratio
    ppm = difference * 1.0;  // 2 ppm per unit difference
  } else {
    ppm = 0.0;  // No CO detected if reading is below baseline
  }

  // Apply reasonable limits but allow dynamic changes
  if (ppm > 2000) ppm = 2000;

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
  SMS += getFormattedDateTime() + "\n"; // Replaced hardcoded date/time
  SMS += "Pay: https://echallan.parivahan.gov.in\n";
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
    SMS += getFormattedDateTime() + "\n"; // Replaced hardcoded date/time
    SMS += "Pay: https://echallan.parivahan.gov.in\n";
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
    SMS += getFormattedDateTime() + "\n"; // Replaced hardcoded date/time
    SMS += "Pay: https://echallan.parivahan.gov.in\n";
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

// Function to get the current date and time formatted as a string
String getFormattedDateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "N/A"; // Fallback value
  }
  char timeStringBuff[20];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%y %H:%M", &timeinfo);
  return String(timeStringBuff);
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