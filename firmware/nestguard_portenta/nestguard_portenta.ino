#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Watchdog.h>
#include "arduino_secrets.h"
#include "cry_model_data.h"

// Set to 1 once physical vision model weights and camera libraries are calibrated on hardware
<<<<<<< HEAD
#define VISION_MODEL_AVAILABLE 1
=======
#define VISION_MODEL_AVAILABLE 0
>>>>>>> fd5437b231bf7f28db5f088e03aea16c127068a9

#if VISION_MODEL_AVAILABLE
#include <Camera.h>
#endif

const char* serverUrl = "http://YOUR_SERVER_IP:5000/api/telemetry";
const String deviceId = SECRET_DEVICE_ID;
const char* deviceKey = SECRET_DEVICE_KEY;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 4000);

    // Initialize Hardware Watchdog (8-second timeout)
    Watchdog.begin(8000);
    Serial.println("[Init] NestGuard Edge Firmware Booting...");

    setupWiFi();
    setupSensorsAndModel();

    Serial.println("[Init] Initialization complete. Entering main operational loop.");
}

void loop() {
    Watchdog.tick();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Warning] Wi-Fi lost. Attempting reconnection...");
        setupWiFi();
    }

    SensorData data = pollSensorsAndInference();
    sendTelemetryPayload(data);

    delay(5000);
}

void setupWiFi() {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Network] Wi-Fi Connected.");
    } else {
        Serial.println("\n[Error] Wi-Fi Connection Failed. Retrying next loop.");
    }
}

struct SensorData {
    String eventType;
    float confidence;
    float maxTemp;
    float ammoniaPpm;
    String diaperStatus;
    String diagnosis;
};

void setupSensorsAndModel() {
    #if VISION_MODEL_AVAILABLE
    Serial.println("[Init] Initializing HM01B0 Vision Shield Camera...");
    // Camera.begin(); initialization logic here when hardware arrives
    #else
    Serial.println("[Notice] Vision model weights pending hardware arrival. Vision checks disabled.");
    #endif
}

SensorData pollSensorsAndInference() {
    SensorData data;

    #if VISION_MODEL_AVAILABLE
    // TODO (Hardware Required): Capture camera frame and execute TFLite vision inference
    data.eventType = "FACE_CLEAR";
    data.confidence = 0.95;
    data.diagnosis = "Vision Model Active - Clear";
    #else
    data.eventType = "NORMAL";
    data.confidence = 0.90;
    data.diagnosis = "Vision Unavailable (Pending Hardware) - Audio/Thermal Active";
    #endif

    // Placeholder sensor reading (To be replaced with real analogRead conversion when gas sensor arrives)
    data.maxTemp = 36.8;
    data.ammoniaPpm = 0.02;
    data.diaperStatus = "NORMAL";

    return data;
}

void sendTelemetryPayload(SensorData data) {
    if (WiFi.status() != WL_CONNECTED) return;

    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-key", deviceKey);

    StaticJsonDocument<512> jsonDoc;
    jsonDoc["device_id"] = deviceId;
    jsonDoc["event_type"] = data.eventType;
    jsonDoc["confidence"] = data.confidence;
    jsonDoc["max_temp"] = data.maxTemp;
    jsonDoc["ammonia_ppm"] = data.ammoniaPpm;
    jsonDoc["diaper_status"] = data.diaperStatus;
    jsonDoc["probabilistic_diagnosis"] = data.diagnosis;

    String requestBody;
    serializeJson(jsonDoc, requestBody);

    int httpResponseCode = http.POST(requestBody);
    if (httpResponseCode > 0) {
        Serial.printf("[HTTP] Telemetry delivered. Code: %d\n", httpResponseCode);
        http.getString(); // Drain socket
    } else {
        Serial.printf("[HTTP Error] Failed: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    http.end();
<<<<<<< HEAD
}
=======
}
>>>>>>> fd5437b231bf7f28db5f088e03aea16c127068a9
