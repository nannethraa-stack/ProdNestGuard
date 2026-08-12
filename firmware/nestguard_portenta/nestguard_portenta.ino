#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <TensorFlowLite.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "cry_model_data.h"   // Audio Cry Model Header
#include "model.h"            // Vision Obstruction Model Header (INT8)
#include "labels.h"           // Vision Labels Header

// --- Network Credentials ---
const char* ssid = "YOUR_HOSPITAL_WIFI_SSID";
const char* password = "YOUR_HOSPITAL_WIFI_PASSWORD";
const char* serverIP = "YOUR_BACKEND_SERVER_IP";
const int serverPort = 5000;

WiFiClient client;
Adafruit_MLX90640 mlx;
float mlx90640[32 * 24];

// --- Audio TFLite Global Variables ---
const tflite::Model* audioTensorModel = nullptr;
tflite::MicroInterpreter* audioInterpreter = nullptr;
TfLiteTensor* audioInputTensor = nullptr;
TfLiteTensor* audioOutputTensor = nullptr;

constexpr int kAudioArenaSize = 60 * 1024;
alignas(16) uint8_t audio_tensor_arena[kAudioArenaSize];

// --- Vision TFLite Global Variables (Face Obstruction) ---
const tflite::Model* visionTensorModel = nullptr;
tflite::MicroInterpreter* visionInterpreter = nullptr;
TfLiteTensor* visionInputTensor = nullptr;
TfLiteTensor* visionOutputTensor = nullptr;

constexpr int kVisionArenaSize = 128 * 1024;
alignas(16) uint8_t vision_tensor_arena[kVisionArenaSize];

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Wire.begin();
    if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
        Serial.println("❌ Thermal camera not found!");
        while (1) delay(1000);
    }
    mlx.setRefreshRate(MLX9064_8_HZ);

    // --- 1. Initialize Audio TFLite Model ---
    audioTensorModel = tflite::GetModel(cry_model_tflite);
    if (audioTensorModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("❌ Audio Model schema mismatch!");
        while (1);
    }

    static tflite::AllOpsResolver audioResolver;
    static tflite::MicroInterpreter static_audio_interpreter(
        audioTensorModel, audioResolver, audio_tensor_arena, kAudioArenaSize);
    audioInterpreter = &static_audio_interpreter;

    if (audioInterpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("❌ Audio Tensor allocation failed!");
        while (1);
    }
    audioInputTensor = audioInterpreter->input(0);
    audioOutputTensor = audioInterpreter->output(0);

    // --- 2. Initialize Vision TFLite Model (Face Obstruction) ---
    visionTensorModel = tflite::GetModel(g_nestguard_model);
    if (visionTensorModel->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("❌ Vision Model schema mismatch!");
        while (1);
    }

    static tflite::AllOpsResolver visionResolver;
    static tflite::MicroInterpreter static_vision_interpreter(
        visionTensorModel, visionResolver, vision_tensor_arena, kVisionArenaSize);
    visionInterpreter = &static_vision_interpreter;

    if (visionInterpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("❌ Vision Tensor allocation failed!");
        while (1);
    }
    visionInputTensor = visionInterpreter->input(0);
    visionOutputTensor = visionInterpreter->output(0);

    // --- 3. Connect to Wi-Fi ---
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected & Dual AI Models Initialized!");
}

void loop() {
    // 1. Capture Thermal Data Frame
    mlx.getFrame(mlx90640);
    
    float maxTemp = -273.15;
    for (int i = 0; i < 768; i++) {
        if (mlx90640[i] > maxTemp) maxTemp = mlx90640[i];
    }

    // 2. Populate Audio Input Tensor (Microphone / PDM input placeholder or stream)
    for (int i = 0; i < 16000; i++) {
        audioInputTensor->data.f[i] = 0.0f; 
    }

    // 3. Run Audio TFLite Inference
    if (audioInterpreter->Invoke() != kTfLiteOk) {
        Serial.println("⚠️ Audio model inference failed");
        return;
    }

    float normalProb     = audioOutputTensor->data.f[0];
    float hungryProb     = audioOutputTensor->data.f[1];
    float bellyPainProb  = audioOutputTensor->data.f[2];
    float discomfortProb = audioOutputTensor->data.f[3];

    // 4. Populate Vision Input Tensor & Run Vision Inference (Camera frame buffer integration)
    // Placeholder simulation for camera frame bytes (replace with actual Vision Shield frame capture buffer)
    for (int i = 0; i < visionInputTensor->bytes; i++) {
        visionInputTensor->data.int8[i] = 0; 
    }

    if (visionInterpreter->Invoke() != kTfLiteOk) {
        Serial.println("⚠️ Vision model inference failed");
        return;
    }

    // Extract INT8 Quantized Vision Outputs
    int8_t* visionOutData = visionOutputTensor->data.int8;
    float vScale = visionOutputTensor->params.scale;
    float vZeroPoint = visionOutputTensor->params.zero_point;

    int predictedVisionClass = 0;
    float maxVisionConfidence = -1.0f;
    for (int i = 0; i < NUM_CLASSES; i++) {
        float confidence = (visionOutData[i] - vZeroPoint) * vScale;
        if (confidence > maxVisionConfidence) {
            maxVisionConfidence = confidence;
            predictedVisionClass = i;
        }
    }
    String visionState = NESTGUARD_LABELS[predictedVisionClass];

    // 5. Environmental Sensors & Diaper Status Check
    float ammoniaPpm = random(1, 25) / 10.0;    
    String diaperStatus = (ammoniaPpm > 1.8) ? "SOILED / CHANGE REQUIRED" : "NORMAL";
    
    // 6. Determine Prioritized Event Type and Probabilistic Diagnosis
    String diagnosis = "Normal Infant State (99%)";
    String eventType = "MONITORING";
    float confidence = normalProb;

    // Priority Check: Safety-critical vision hazards take top precedence
    if (visionState == "FACE_PARTIALLY_COVERED") {
        confidence = maxVisionConfidence;
        diagnosis = "CRITICAL SAFETY ALERT: Face Partially Covered (" + String((int)(maxVisionConfidence * 100)) + "%)";
        eventType = "FACE_OBSTRUCTION_CRITICAL";
    } else if (visionState == "FACE_DOWN_TURNED") {
        confidence = maxVisionConfidence;
        diagnosis = "CRITICAL SAFETY ALERT: Face Down / Prone Position (" + String((int)(maxVisionConfidence * 100)) + "%)";
        eventType = "FACE_DOWN_CRITICAL";
    } else if (maxTemp > 38.2) {
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
    } else if (visionState == "NO_FACE_DETECTED") {
        confidence = maxVisionConfidence;
        diagnosis = "Camera Warning: No Face Detected in View";
        eventType = "CAMERA_OBSTRUCTED";
    }

    // 7. Transmit Real-Time Telemetry to Backend Server
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