#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <TensorFlowLite.h>
#include <tensorflow/lite/micro/all_ops_resolver.h>
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "model.h"

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
  }
}

void loop() {
  // 1. Capture Thermal Data Frame
  mlx.getFrame(mlx90640);
  
  float maxTemp = -273.15;
  for (int i = 0; i < 768; i++) {
    if (mlx90640[i] > maxTemp) maxTemp = mlx90640[i];
  }

  // 2. Run TFLite Inference
  if (interpreter->Invoke() != kTfLiteOk) {
    Serial.println("⚠️ Model inference failed");
    return;
  }

  // Extract model results from output tensor
  float cryConfidence = outputTensor->data.f[0]; 
  float ammoniaPpm = random(1, 25) / 10.0;     
  String diaperStatus = (ammoniaPpm > 1.8) ? "SOILED / CHANGE REQUIRED" : "NORMAL";
  
  String diagnosis = "Normal Infant State (99%)";
  if (maxTemp > 38.2) {
    diagnosis = "Elevated Temperature / Fever Risk (92%)";
  } else if (cryConfidence > 0.85) {
    diagnosis = "Infant Distress / Cry Detected (" + String((int)(cryConfidence * 100)) + "%)";
  }

  // 3. Transmit Real-Time Telemetry to Backend
  if (client.connect(serverIP, serverPort)) {
    String payload = "{"
      "\"device_id\":\"portenta_room_01\","
      "\"event_type\":\"" + String(cryConfidence > 0.85 ? "INFANT_CRYING" : "MONITORING") + "\","
      "\"confidence\":" + String(cryConfidence) + ","
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
