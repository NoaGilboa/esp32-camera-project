
```markdown
# ESP32-S3 Camera Streaming & Upload Project

## 📌 Overview
This project implements **real-time video capture and upload** from a **Seeed Studio XIAO ESP32-S3 Sense Camera**.  
The ESP32 connects to Wi-Fi, captures video frames using the onboard camera, and uploads them to a remote server (e.g., Azure backend) via HTTP(S).

The main goals are:
- Initialize and configure the ESP32-S3 camera.
- Capture MJPEG/AVI video or fallback to JPEG frames.
- Stream or upload captured data to a server in real-time.
- Use secure HTTPS communication with APIs for commands and uploads.

---

## ⚙️ Hardware Requirements
- **Seeed Studio XIAO ESP32-S3 Sense** (with camera module).  
- USB-C cable for programming and power.  
- Wi-Fi access point (can be smartphone hotspot).  
- Optional SD card module (if storing video locally).

---

## 🛠️ Software Requirements
- **Arduino IDE** (≥ 2.0) or **PlatformIO**.  
- ESP32 board support (`esp32` by Espressif).  
- Required Arduino libraries:
  - `WiFi.h`
  - `esp_wifi.h`
  - `WiFiClientSecure.h`
  - `HTTPClient.h`
  - `ArduinoJson.h`
  - `esp_camera.h`

---

## 📂 Project Structure
```

project/
│── sketch\_aug30a.ino      # Main Arduino sketch
│── camera\_pins.h          # Pin definitions for XIAO ESP32S3 Camera

````

---

## 🔧 Pin Configuration
Defined in **camera_pins.h**:

- **Data pins (Y2–Y9)** → GPIO 15, 17, 18, 16, 14, 12, 11, 48  
- **Clock & sync pins** → XCLK: 10, VSYNC: 38, HREF: 47, PCLK: 13  
- **I2C control pins** → SIOC: 39, SIOD: 40  
- **No PWDN/RESET control** (set to -1).  

Alternative **SD card pins** are also included (CS, MOSI, MISO, SCK).

---

## 🚀 Setup & Deployment
1. Clone or download this repository.  
2. Open `sketch_aug30a.ino` in Arduino IDE.  
3. Install ESP32 board support in Arduino Boards Manager.  
4. Select **XIAO ESP32S3** as the target board.  
5. Update Wi-Fi credentials inside `sketch_aug30a.ino`:
   ```cpp
   const char* ssid = "YourNetwork";
   const char* password = "YourPassword";
````

6. Update server API URLs (for commands & video upload).
7. Connect the device and upload the sketch.
8. Open **Serial Monitor** (115200 baud) to view logs.

---

## 🧩 Core Functions & Logic

### 🔹 Camera Initialization

```cpp
#include "camera_pins.h"
esp_camera_init(&config);
```

* Loads pin configuration.
* Initializes the OV2640/OV5640 sensor.
* Sets frame size, pixel format, and buffer options.

### 🔹 Wi-Fi Connection

```cpp
WiFi.begin(ssid, password);
```

* Connects to given SSID & password.
* Uses `WiFiClientSecure` for HTTPS communication.

### 🔹 Command Polling

```cpp
HTTPClient http;
http.begin(COMMAND_URL);
```

* Periodically fetches commands (`start`, `stop`, `idle`) from server.
* Decides whether to start recording or stay idle.

### 🔹 Video Capture

```cpp
camera_fb_t* fb = esp_camera_fb_get();
```

* Captures a frame buffer from the camera.
* Depending on mode:

  * Stream AVI (MJPEG) in chunks.
  * Or send single JPEG frames.

### 🔹 Upload to Server

```cpp
http.POST(fb->buf, fb->len);
```

* Sends multipart/form-data to backend.
* Uses `Transfer-Encoding: chunked` (no Content-Length).

### 🔹 Error Handling

* Retries on failed Wi-Fi or HTTP request.
* Falls back from AVI streaming → JPEG per second.

---

## 🔄 Typical Workflow

```mermaid
flowchart TD
    A[Boot ESP32] --> B[Connect to Wi-Fi]
    B --> C[Poll server for command]
    C -->|Start| D[Initialize camera & begin recording]
    C -->|Idle| E[Do nothing, keep polling]
    D --> F[Capture video frames]
    F --> G[Upload to server (AVI/JPEG)]
    G --> C
```

---

## 📊 Example Serial Output

```
Connecting to WiFi...
WiFi connected: 192.168.1.45
Polling server for command...
Command: start (patient=1)
▶️ Recording started
Uploading video chunk...
Uploading video chunk...
⏹ Recording stopped
```

---

## 📌 Future Improvements

* Add **SD card recording fallback** when Wi-Fi is unavailable.
* Implement **adaptive bitrate** streaming.
* Encrypt video chunks for added security.
* Improve error logging and retries.

---

## 👤 Author

Developed by **Noa Gilboa** as part of the **Mobile Walking Lab Project** (2025).
Server-side repository: [MobileWalkingLab\_server](https://github.com/NoaGilboa/MobileWalkingLab_server)

---

```

---

