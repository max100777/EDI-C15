# EDI-C15
TItle -  Multi-Layered GNSS Security Framework for Autonomous UAV and UGV Systems Using Single-GPS Sensor Fusion, LoRa Telemetry, and Machine Learning Classification
A low-cost, real-time GNSS spoofing detection and autonomous navigation security framework for UAVs and UGVs using ESP32, LoRa telemetry, sensor fusion, and Machine Learning.

🚀 Project Overview

Modern autonomous drones and unmanned ground vehicles heavily rely on GPS/GNSS signals for navigation. However, civilian GNSS signals are highly vulnerable to spoofing attacks, where fake satellite signals mislead vehicles into following false routes.
This project introduces a 3-Layer GNSS Security Framework that combines:
1. Signal anomaly detection
2. Multi-sensor fusion
3. Machine learning classification
to detect spoofing attempts in real time with high accuracy.

The system uses:

ESP32 as the sensing and detection node
LoRa (SX1278) for long-range telemetry
Arduino Nano as the receiver interface
Python + ML models for advanced analysis
Grafana + InfluxDB for real-time monitoring dashboards

🛡️ Key Features

✅ Real-time GNSS spoofing detection
✅ Multi-layer security architecture
✅ IMU-based dead reckoning fallback navigation
✅ LoRa wireless telemetry communication
✅ XGBoost + LSTM machine learning classification
✅ Polygon geofencing support
✅ OLED alert system with threat levels
✅ Real-time Grafana dashboard visualization
✅ Low-cost implementation (< $80)

🧠 Three-Layer Detection System

Layer 1 — Signal Anomaly Detection
Detects suspicious GPS behavior using:
Sudden position jumps
Abnormal altitude changes
RSSI anomalies

Layer 2 — Sensor Fusion Validation
Cross-checks GPS data using:
MPU6050 IMU
BMP280 Barometer
Tilt Sensor
PIR Motion Sensor
This helps verify whether the reported movement is physically possible.

Layer 3 — Machine Learning Detection
Uses:
XGBoost
LSTM Neural Networks
to classify spoofing attacks based on telemetry and sensor patterns.

⚙️ Hardware Used
Component	Purpose
ESP32 DevKit V1	Main sensing & processing node
Arduino Nano	LoRa receiver & USB bridge
NEO-M8N GPS	GNSS positioning
MPU6050	Accelerometer & Gyroscope
BMP280	Barometric altitude
SX1278 LoRa	Long-range telemetry
OLED SSD1306	Status display
PIR Sensor	Motion detection
Tilt Sensor	Orientation monitoring
DHT22	Environmental sensing
ESP-CAM	Visual reference module

💻 Software Stack
Python 3.11+
XGBoost
TensorFlow / Keras
scikit-learn
InfluxDB
Grafana
Arduino IDE
TinyGPS++
pySerial

🔄 System Architecture
ESP32 Sensor Node
       ↓
LoRa Telemetry (SX1278)
       ↓
Arduino Nano Receiver
       ↓
PC Analytics Pipeline
       ↓
ML Detection + Dashboard + Alerts

📊 Performance Results

Detection Accuracy	94.7%
Precision	93.1%
Recall	96.2%
False Positive Rate	5.3%
Detection Latency	312 ms
LoRa Packet Reliability	>99%

📡 Working Principle
ESP32 collects GPS and sensor data.
Sensor fusion checks are performed locally.
Data is transmitted via LoRa every 2 seconds.
Arduino Nano forwards telemetry to PC.
Python pipeline performs:
Signal anomaly analysis
ML inference
Geofencing validation
Threat score is generated.
OLED + dashboard alerts are triggered.

🧪 Technologies & Concepts Used
GNSS Security
Sensor Fusion
Machine Learning
LoRa Communication
Edge Computing
Dead Reckoning
Geofencing
Real-Time Monitoring
Embedded Systems
Autonomous Navigation Security

📈 Future Improvements
Galileo OSNMA integration
TensorFlow Lite on-device inference
Federated learning support
Kalman Filter based navigation
Custom PCB design
Integration with Pixhawk autopilots

👨‍💻 Team
Pradyumna Waghmode
Asad Sayyed
Yash Zanwar
Eklavya Puri

Guided by Dr. Anup Ingle
Vishwakarma Institute of Technology, Pune
