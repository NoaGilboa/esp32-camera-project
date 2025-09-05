/*
 * XIAO ESP32S3 Sense – Record to memory, upload on stop
 * Records full video to PSRAM/DRAM, uploads when stop command received
 */

#include <WiFi.h>
#include <esp_wifi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"

const char* ssid     = "Noa";
const char* password = "12345678";

const char* COMMAND_URL = "https://walkinglab-hbesf8g3aaa8hafz.westeurope-01.azurewebsites.net/api/device/command";
const char* HOST        = "walkinglab-hbesf8g3aaa8hafz.westeurope-01.azurewebsites.net";
const int   HTTPS_PORT  = 443;

#define VIDEO_UPLOAD_PATH_FORMAT   "/api/video/%s/upload-video"
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ===== State =====
bool isRecording = false;
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 3000;

String patientId = "";
int DEVICE_MEAS_ID = 0;
String lastCmdSeen = "", lastPatientSeen = "";
int lastMeasSeen = 0;

// ===== Video Recording in Memory =====
struct VideoFrame {
    uint8_t* data;
    size_t length;
    unsigned long timestamp;
};

std::vector<VideoFrame> videoFrames;
unsigned long recordingStartTime = 0;
const size_t MAX_MEMORY_MB = 6; // השאר 2MB לפעולות אחרות
const size_t MAX_MEMORY_BYTES = MAX_MEMORY_MB * 1024 * 1024;
size_t currentMemoryUsage = 0;

String boundary = "----XIAOFormBoundary7MA4YWxkTrZu0gW";

// ===== Helper Functions =====
static inline void put4(uint8_t* p, uint32_t v) { 
    p[0]=v&255; p[1]=(v>>8)&255; p[2]=(v>>16)&255; p[3]=(v>>24)&255; 
}

// ===== Wi-Fi =====
bool wifiInitAndConnect(uint16_t timeout_ms_per_try = 20000, int tries = 4) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setHostname("xiao-cam");

    wifi_country_t ctry = { "IL", 1, 13, WIFI_COUNTRY_POLICY_AUTO };
    esp_wifi_set_country(&ctry);
    
    Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());

    for (int t=1; t<=tries; t++){
        Serial.printf("Connecting to \"%s\" (try %d/%d)…\n", ssid, t, tries);
        WiFi.disconnect(); 
        delay(150);
        WiFi.begin(ssid, password);
        
        unsigned long t0=millis();
        while (millis()-t0 < timeout_ms_per_try) {
            if (WiFi.status()==WL_CONNECTED) break;
            delay(250); 
            Serial.print(".");
        }
        Serial.println();
        
        if (WiFi.status()==WL_CONNECTED) {
            Serial.printf("WiFi OK  IP=%s  RSSI=%d dBm  CH=%d\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
            return true;
        }
        Serial.println("WiFi connect failed.");
        WiFi.disconnect(); 
        delay(500);
    }
    return false;
}

// ===== Camera =====
bool initCamera() {
    camera_config_t config = {
        .pin_pwdn     = PWDN_GPIO_NUM,
        .pin_reset    = RESET_GPIO_NUM,
        .pin_xclk     = XCLK_GPIO_NUM,
        .pin_sccb_sda = SIOD_GPIO_NUM,
        .pin_sccb_scl = SIOC_GPIO_NUM,
        .pin_d7  = Y9_GPIO_NUM, .pin_d6 = Y8_GPIO_NUM, .pin_d5 = Y7_GPIO_NUM, .pin_d4 = Y6_GPIO_NUM,
        .pin_d3  = Y5_GPIO_NUM, .pin_d2 = Y4_GPIO_NUM, .pin_d1 = Y3_GPIO_NUM, .pin_d0 = Y2_GPIO_NUM,
        .pin_vsync = VSYNC_GPIO_NUM, .pin_href = HREF_GPIO_NUM, .pin_pclk = PCLK_GPIO_NUM,
        .xclk_freq_hz = 20000000,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = FRAMESIZE_QVGA,   // 320x240 - מתאים לזיכרון
        .jpeg_quality = 25,               // איכות בינונית
        .fb_count     = 1,
        .grab_mode    = CAMERA_GRAB_LATEST
    };
    config.fb_location = psramFound()? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("esp_camera_init failed: 0x%08X\n", err);
        return false;
    }
    
    size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("Free PSRAM: %u bytes (%.1f MB)\n", freePsram, freePsram/1024.0/1024.0);
    return true;
}

// ===== Memory Recording =====
void startRecording() {
    // נקה זיכרון קודם
    clearVideoMemory();
    
    isRecording = true;
    recordingStartTime = millis();
    currentMemoryUsage = 0;
    
    Serial.println("▶️ Recording started - saving to memory");
    Serial.printf("Available memory for recording: %.1f MB\n", MAX_MEMORY_BYTES/1024.0/1024.0);
}

bool captureFrameToMemory() {
    if (!isRecording) return false;
    
    // בדוק אם יש מספיק זיכרון
    if (currentMemoryUsage >= MAX_MEMORY_BYTES) {
        Serial.println("⚠️ Memory full - stopping recording");
        return false;
    }
    
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ Failed to capture frame");
        return false;
    }
    
    if (fb->format != PIXFORMAT_JPEG) {
        Serial.println("❌ Frame is not JPEG");
        esp_camera_fb_return(fb);
        return false;
    }
    
    // בדוק אם הפריים יתאים בזיכרון
    if (currentMemoryUsage + fb->len > MAX_MEMORY_BYTES) {
        Serial.println("⚠️ Frame too large for remaining memory");
        esp_camera_fb_return(fb);
        return false;
    }
    
    // העתק את הנתונים לזיכרון
    uint8_t* frameData = (uint8_t*)ps_malloc(fb->len);
    if (!frameData) {
        // נסה זיכרון רגיל אם PSRAM נגמר
        frameData = (uint8_t*)malloc(fb->len);
        if (!frameData) {
            Serial.println("❌ Failed to allocate memory for frame");
            esp_camera_fb_return(fb);
            return false;
        }
    }
    
    memcpy(frameData, fb->buf, fb->len);
    
    VideoFrame frame;
    frame.data = frameData;
    frame.length = fb->len;
    frame.timestamp = millis() - recordingStartTime;
    
    videoFrames.push_back(frame);
    currentMemoryUsage += fb->len;
    
    esp_camera_fb_return(fb);
    
    // הדפס סטטוס כל 50 פריימים
    if (videoFrames.size() % 50 == 0) {
        Serial.printf("📹 Frames: %d, Memory: %.1f/%.1f MB, Time: %.1fs\n", 
            videoFrames.size(), 
            currentMemoryUsage/1024.0/1024.0, 
            MAX_MEMORY_BYTES/1024.0/1024.0,
            (millis() - recordingStartTime)/1000.0);
    }
    
    return true;
}

void clearVideoMemory() {
    for (auto& frame : videoFrames) {
        free(frame.data);
    }
    videoFrames.clear();
    currentMemoryUsage = 0;
}

// ===== AVI Creation and Upload =====
std::vector<uint8_t> createAviFile(uint16_t width, uint16_t height, uint32_t fps) {
    std::vector<uint8_t> aviData;
    
    if (videoFrames.empty()) {
        Serial.println("❌ No frames to create AVI");
        return aviData;
    }
    
    Serial.printf("📹 Creating AVI with %d frames...\n", videoFrames.size());
    
    // חשב גודל הנתונים בדיוק
    size_t moviSize = 4; // "movi" signature
    for (const auto& frame : videoFrames) {
        moviSize += 8; // chunk header ("00dc" + size)
        moviSize += frame.length;
        if (frame.length & 1) moviSize++; // WORD alignment padding
    }
    
    const size_t HEADER_SIZE = 220; // גודל קבוע של ה-header
    size_t totalFileSize = 4 + HEADER_SIZE + 8 + moviSize; // "AVI " + headers + "LIST" + movi content
    
    // בנה header
    aviData.reserve(totalFileSize);
    
    auto writeU32 = [&](uint32_t v) {
        aviData.push_back(v & 0xFF);
        aviData.push_back((v >> 8) & 0xFF);
        aviData.push_back((v >> 16) & 0xFF);
        aviData.push_back((v >> 24) & 0xFF);
    };
    
    auto write4CC = [&](const char* cc) {
        aviData.insert(aviData.end(), cc, cc + 4);
    };
    
    // RIFF header
    write4CC("RIFF");
    writeU32(totalFileSize - 8); // file size minus RIFF header
    write4CC("AVI ");
    
    // hdrl LIST
    write4CC("LIST");
    writeU32(HEADER_SIZE - 8); // size of entire hdrl content
    write4CC("hdrl");
    
    // Main AVI header (avih)
    write4CC("avih");
    writeU32(56); // avih chunk size
    writeU32(1000000 / fps);  // microseconds per frame
    writeU32(0);              // max bytes per second  
    writeU32(0);              // padding/reserved
    writeU32(0x0010);         // flags (AVIF_HASINDEX)
    writeU32(videoFrames.size()); // total frames
    writeU32(0);              // initial frames (for interleaved files)
    writeU32(1);              // number of streams
    writeU32(65536);          // suggested buffer size
    writeU32(width);          // width
    writeU32(height);         // height
    writeU32(0); writeU32(0); writeU32(0); writeU32(0); // reserved fields
    
    // Stream list
    write4CC("LIST");
    writeU32(132); // strl size
    write4CC("strl");
    
    // Stream header (strh)
    write4CC("strh");
    writeU32(56); // strh size
    write4CC("vids");         // stream type = video
    write4CC("MJPG");         // codec
    writeU32(0);              // flags
    writeU32(0);              // priority & language
    writeU32(0);              // initial frames
    writeU32(1);              // scale
    writeU32(fps);            // rate (fps = rate/scale)
    writeU32(0);              // start time
    writeU32(videoFrames.size()); // stream length in frames
    writeU32(65536);          // suggested buffer size
    writeU32(0xFFFFFFFF);     // quality (-1 = default)
    writeU32(0);              // sample size (0 for video)
    writeU32(0);              // frame rectangle (left, top)
    writeU32((width << 16) | height); // frame rectangle (right, bottom)
    
    // Stream format (strf) - BITMAPINFOHEADER for video
    write4CC("strf");
    writeU32(40); // BITMAPINFOHEADER size
    writeU32(40);             // structure size
    writeU32(width);          // width
    writeU32(height);         // height  
    writeU32(1 | (24 << 16)); // planes | bits per pixel
    write4CC("MJPG");         // compression
    writeU32(width * height * 3); // image size estimate
    writeU32(0);              // X pixels per meter
    writeU32(0);              // Y pixels per meter  
    writeU32(0);              // colors used
    writeU32(0);              // important colors
    
    // Movie data list
    write4CC("LIST");
    writeU32(moviSize);
    write4CC("movi");
    
    // הוסף כל הפריימים
    for (size_t i = 0; i < videoFrames.size(); i++) {
        const auto& frame = videoFrames[i];
        
        // בדוק שהפריים הוא JPEG תקני
        if (frame.length < 4 || frame.data[0] != 0xFF || frame.data[1] != 0xD8) {
            Serial.printf("⚠️ Frame %d is not valid JPEG (starts with 0x%02X%02X)\n", 
                         i, frame.data[0], frame.data[1]);
            continue;
        }
        
        // כתוב chunk header
        write4CC("00dc"); // uncompressed DIB data chunk
        writeU32(frame.length);
        
        // כתוב frame data
        aviData.insert(aviData.end(), frame.data, frame.data + frame.length);
        
        // WORD alignment padding
        if (frame.length & 1) {
            aviData.push_back(0);
        }
    }
    
    Serial.printf("✅ AVI created: %.1f MB, %d frames\n", 
        aviData.size()/1024.0/1024.0, videoFrames.size());
    Serial.printf("   File structure: RIFF(%.1fKB) + Headers(%.1fKB) + Data(%.1fKB)\n",
        8/1024.0, HEADER_SIZE/1024.0, moviSize/1024.0);
    
    return aviData;
}

bool uploadVideoToServer() {
    if (videoFrames.empty()) {
        Serial.println("❌ No video data to upload");
        return false;
    }
    
    Serial.println("📤 Creating AVI file...");
    std::vector<uint8_t> aviData = createAviFile(320, 240, 10); // 10 fps
    
    if (aviData.empty()) {
        Serial.println("❌ Failed to create AVI");
        return false;
    }
    
    Serial.println("📤 Uploading video to server...");
    
    WiFiClientSecure client;
    client.setInsecure();
    
    if (!client.connect(HOST, HTTPS_PORT)) {
        Serial.println("❌ TLS connect failed");
        return false;
    }
    
    char path[128];
    snprintf(path, sizeof(path), VIDEO_UPLOAD_PATH_FORMAT, patientId.c_str());
    
    // חשב גודל הבקשה
    String headers = String("--") + boundary + "\r\n";
    if (DEVICE_MEAS_ID > 0) {
        headers += "Content-Disposition: form-data; name=\"device_measurement_id\"\r\n\r\n";
        headers += String(DEVICE_MEAS_ID) + "\r\n";
        headers += "--" + boundary + "\r\n";
    }
    headers += "Content-Disposition: form-data; name=\"video\"; filename=\"stream.avi\"\r\n";
    headers += "Content-Type: video/avi\r\n\r\n";
    
    String tail = "\r\n--" + boundary + "--\r\n";
    size_t contentLength = headers.length() + aviData.size() + tail.length();
    
    // שלח HTTP headers
    client.printf("POST %s HTTP/1.1\r\n", path);
    client.printf("Host: %s\r\n", HOST);
    client.println("User-Agent: XIAO-ESP32S3");
    client.println("Connection: close");
    client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    client.printf("Content-Length: %u\r\n", contentLength);
    client.print("\r\n");
    
    // שלח multipart data
    client.print(headers);
    
    // שלח AVI data בחלקים
    const size_t CHUNK_SIZE = 4096;
    for (size_t i = 0; i < aviData.size(); i += CHUNK_SIZE) {
        size_t chunkSize = min(CHUNK_SIZE, aviData.size() - i);
        size_t written = client.write(aviData.data() + i, chunkSize);
        if (written != chunkSize) {
            Serial.printf("❌ Write failed: %u/%u\n", written, chunkSize);
            client.stop();
            return false;
        }
        
        // הצג התקדמות
        if (i % (CHUNK_SIZE * 10) == 0) {
            Serial.printf("📤 Uploaded %.1f%%\n", (i * 100.0) / aviData.size());
        }
    }
    
    client.print(tail);
    
    // קרא תגובה
    bool success = false;
    String status;
    unsigned long t0 = millis();
    while (millis() - t0 < 30000) { // 30 second timeout
        while (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            Serial.println(line);
            if (status.isEmpty() && line.startsWith("HTTP/1.1")) {
                status = line;
            }
            if (line.startsWith("HTTP/1.1 201")) {
                success = true;
            }
        }
        if (!client.connected()) break;
        delay(10);
    }
    
    if (!status.isEmpty()) {
        Serial.println("Server response: " + status);
    }
    
    client.stop();
    return success;
}

void stopRecording() {
    if (!isRecording) return;
    
    isRecording = false;
    unsigned long recordingDuration = millis() - recordingStartTime;
    
    Serial.printf("⏹️ Recording stopped after %.1fs\n", recordingDuration/1000.0);
    Serial.printf("📹 Captured %d frames (%.1f MB)\n", 
        videoFrames.size(), currentMemoryUsage/1024.0/1024.0);
    
    if (WiFi.status() == WL_CONNECTED) {
        bool success = uploadVideoToServer();
        Serial.println(success ? "✅ Upload successful!" : "❌ Upload failed");
    } else {
        Serial.println("❌ No WiFi - cannot upload");
    }
    
    // נקה זיכרון
    clearVideoMemory();
}

// ===== Arduino =====
void setup() {
    Serial.begin(115200);
    delay(300);

    if (!wifiInitAndConnect()) {
        Serial.println("WiFi FAIL (check coverage/password).");
    } else {
        Serial.println("WiFi connected.");
    }

    if (!initCamera()) {
        Serial.println("Camera init failed");
        while (true) delay(1000);
    } else {
        Serial.println("Camera ready");
    }
}

void loop() {
    unsigned long now = millis();

    // Poll server command
    if (WiFi.status() == WL_CONNECTED && now - lastPollTime >= POLL_INTERVAL) {
        lastPollTime = now;

        WiFiClientSecure scli; 
        scli.setInsecure();
        HTTPClient http;
        
        if (http.begin(scli, COMMAND_URL)) {
            int code = http.GET();
            if (code == 200) {
                String payload = http.getString();
                Serial.println("Command received: " + payload);

                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, payload) == DeserializationError::Ok && doc.containsKey("command")) {
                    JsonObject cmdObj = doc["command"];
                    String cmd = (const char*)(cmdObj["command"] | "");
                    String pid = String(cmdObj["patientId"]);
                    int measId = cmdObj["deviceMeasurementId"] | 0;

                    bool isNew = (cmd != lastCmdSeen) || (pid != lastPatientSeen) || (measId != lastMeasSeen);
                    if (isNew) {
                        lastCmdSeen = cmd; 
                        lastPatientSeen = pid; 
                        lastMeasSeen = measId;

                        if (cmd == "start" && !isRecording) {
                            patientId = pid; 
                            DEVICE_MEAS_ID = measId;
                            Serial.println("▶️ Start command received");
                            startRecording();
                        } else if (cmd == "stop" && isRecording) {
                            Serial.println("⏹️ Stop command received");
                            stopRecording();
                        }
                    }
                }
            } else {
                Serial.printf("⚠️ GET command failed: %d\n", code);
            }
            http.end();
        }
    }

    // Record frame if recording
    if (isRecording) {
        bool success = captureFrameToMemory();
        if (!success) {
            Serial.println("⚠️ Failed to capture frame or memory full - stopping recording");
            stopRecording();
        }
        delay(100); // ~10 fps - adjust as needed
    } else {
        delay(100); // לא מקליט - חסוך CPU
    }
}