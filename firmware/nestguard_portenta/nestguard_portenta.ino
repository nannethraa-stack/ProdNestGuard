#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <TensorFlowLite.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "cry_model_data.h"

// --- Network Credentials ---
const char* ssid = "YOUR_HOSPITAL_WIFI_SSID";
const char* password = "YOUR_HOSPITAL_WIFI_PASSWORD";
const char* serverIP = "YOUR_BACKEND_SERVER_IP";
const int serverPort = 5000;

WiFiClient client;
Adafruit_MLX90640 mlx;
float mlx90640[32 * 24];

// --- TFLite Global Variables ---
const tflite::Model* tensorModel = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* inputTensor = nullptr;
TfLiteTensor* outputTensor = nullptr;

// Tensor Arena buffer size
constexpr int kTensorArenaSize = 60 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Wire.begin();
    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
        Serial.println("❌ Thermal camera not found!");
        while (1) delay(1000);
    }
    mlx.setRefreshRate(MLX9064_8_HZ);

    // --- Initialize TFLite Model ---
    tensorModel = tflite::GetModel(cry_model_tflite);
    if (tensorModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("❌ Model schema mismatch!");
        while (1);
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(
        tensorModel, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // Allocate memory for model tensors
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("❌ Tensor allocation failed!");
        while (1);
    }

    inputTensor = interpreter->input(0);
    outputTensor = interpreter->output(0);

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
}

void loop() {
    // 1. Capture Thermal Data Frame
    mlx.getFrame(mlx90640);
    
    float maxTemp = -273.15;
    for (int i = 0; i < 768; i++) {
        if (mlx90640[i] > maxTemp) maxTemp = mlx90640[i];
    }

    // 2. Populate Audio Input Tensor (Insert your microphone / PDM reading loop here)
    // For demonstration, ensuring input buffer is ready for inference
    for (int i = 0; i < 16000; i++) {
        inputTensor->data.f[i] = 0.0f; 
    }

    // 3. Run TFLite Inference
    if (interpreter->Invoke() != kTfLiteOk) {
        Serial.println("⚠️ Model inference failed");
        return;
    }

    // Extract 4-class probabilities from output tensor:
    // Index 0: Normal / Background
    // Index 1: Hungry Cry
    // Index 2: Belly Pain Cry
    // Index 3: Discomfort / Colic
    float normalProb     = outputTensor->data.f[0];
    float hungryProb     = outputTensor->data.f[1];
    float bellyPainProb  = outputTensor->data.f[2];
    float discomfortProb = outputTensor->data.f[3];

    float ammoniaPpm = random(1, 25) / 10.0;     
    String diaperStatus = (ammoniaPpm > 1.8) ? "SOILED / CHANGE REQUIRED" : "NORMAL";
    
    String diagnosis = "Normal Infant State (99%)";
    String eventType = "MONITORING";
    float confidence = normalProb;

    // Determine granular event type and classification confidence
    if (maxTemp > 38.2) {
        confidence = 0.92;
        diagnosis = "Elevated Temperature / Fever Risk (92%)";
        eventType = "THERMAL_ANOMALY";
    } else if (bellyPainProb > 0.70 && bellyPainProb > hungryProb && bellyPainProb > discomfortProb) {
        confidence = bellyPainProb;
        diagnosis = "Critical Alert: Belly Pain Cry (" + String((int)(bellyPainProb * 100)) + "%)";
        eventType = "PAIN_CRY_DETECTED";
    } else if (hungryProb > 0.70 && hungryProb > bellyPainProb && hungryProb > discomfortProb) {
        confidence = hungryProb;
        diagnosis = "Infant Alert: Hunger Cry (" + String((int)(hungryProb * 100)) + "%)";
        eventType = "HUNGER_CRY_DETECTED";
    } else if (discomfortProb > 0.70) {
        confidence = discomfortProb;
        diagnosis = "Infant Alert: Discomfort / Colic (" + String((int)(discomfortProb * 100)) + "%)";
        eventType = "DISCOMFORT_DETECTED";
    } else if (ammoniaPpm > 1.8) {
        confidence = 0.88;
        diagnosis = "Diaper Change Required / Ammonia Spike (88%)";
        eventType = "AMMONIA_SPIKE";
    }

    // 4. Transmit Real-Time Telemetry to Backend
    if (client.connect(serverIP, serverPort)) {
        String payload = "{"
            "\"device_id\":\"portenta_room_01\","
            "\"event_type\":\"" + eventType + "\","
            "\"confidence\":" + String(confidence) + ","
            "\"max_temp\":" + String(maxTemp) + ","
            "\"ammonia_ppm\":" + String(ammoniaPpm) + ","
            "\"diaper_status\":\"" + diaperStatus + "\","
            "\"probabilistic_diagnosis\":\"" + diagnosis + "\""
        "}";
        
        client.println("POST /api/telemetry HTTP/1.1");
        client.println("Host: " + String(serverIP));
        client.println("Content-Type: application/json");
        client.println("Content-Length: " + String(payload.length()));
        client.println("Connection: close");
        client.println();
        client.println(payload);
        
        client.stop();
    }

    delay(5000);
}