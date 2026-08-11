#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>

// --- Network Credentials ---
const char* ssid = "YOUR_HOSPITAL_WIFI_SSID";
const char* password = "YOUR_HOSPITAL_WIFI_PASSWORD";
const char* serverIP = "YOUR_BACKEND_SERVER_IP";
const int serverPort = 5000;

WiFiClient client;
Adafruit_MLX90640 mlx;
float mlx90640[32 * 24]; // Thermal frame buffer

// --- ML Model Output Structure ---
struct MLInferenceResult {
  String diagnosis;
  float confidence;
  String diaperStatus;
  String eventType;
};

// --- TinyML Model Inference Engine ---
// In production, replace this block with your compiled TFLite Micro interpreter execution code
MLInferenceResult runTinyMLInference(float* thermalFrame, float ammoniaPpm, float audioConfidence) {
  MLInferenceResult result;

  // Compute max temperature from thermal matrix tensor
  float maxTemp = -273.15;
  for (int i = 0; i < 768; i++) {
    if (thermalFrame[i] > maxTemp) {
      maxTemp = thermalFrame[i];
    }
  }

  // Multi-sensor feature fusion and probabilistic classification simulation
  if (maxTemp > 38.2) {
    result.diagnosis = "Elevated Temperature / Fever Risk (92%)";
    result.confidence = 0.92;
    result.eventType = "THERMAL_ANOMALY";
  } else if (ammoniaPpm > 1.8 && audioConfidence > 0.80) {
    result.diagnosis = "Discomfort: Soiled Diaper & Crying (96%)";
    result.confidence = 0.96;
    result.eventType = "INFANT_DISTRESS";
  } else if (audioConfidence > 0.80) {
    result.diagnosis = "Infant Distress / Cry Detected (89%)";
    result.confidence = audioConfidence;
    result.eventType = "INFANT_CRYING";
  } else if (ammoniaPpm > 1.8) {
    result.diagnosis = "Diaper Change Required / Ammonia Spike (88%)";
    result.confidence = 0.88;
    result.eventType = "AMMONIA_SPIKE";
  } else {
    result.diagnosis = "Normal Infant State (99%)";
    result.confidence = 0.99;
    result.eventType = "MONITORING";
  }

  result.diaperStatus = (ammoniaPpm > 1.8) ? "SOILED / CHANGE REQUIRED" : "NORMAL";
  return result;
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Wire.begin();
  if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("❌ Thermal camera not found!");
    while (1) delay(1000);
  }
  mlx.setRefreshRate(MLX9064_8_HZ);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
}

void loop() {
  // 1. Capture Sensor Data Streams (Thermal Matrix, PDM Audio, MQ-137 Analog)
  mlx.getFrame(mlx90640);
  float audioConfidence = (random(0, 10) > 8) ? 0.94 : 0.05; // Mock PDM mic input
  float ammoniaPpm = random(1, 25) / 10.0;                    // Mock MQ-137 input via ADC

  // 2. Execute On-Device ML Model Inference
  MLInferenceResult mlResult = runTinyMLInference(mlx90640, ammoniaPpm, audioConfidence);

  // Calculate overall max temp for payload
  float maxTemp = -273.15;
  for (int i = 0; i < 768; i++) {
    if (mlx90640[i] > maxTemp) maxTemp = mlx90640[i];
  }

  // 3. Transmit Structured Payload to Backend Server
  if (client.connect(serverIP, serverPort)) {
    String payload = "{"
      "\"device_id\":\"portenta_room_01\","
      "\"event_type\":\"" + mlResult.eventType + "\","
      "\"confidence\":" + String(mlResult.confidence) + ","
      "\"max_temp\":" + String(maxTemp) + ","
      "\"ammonia_ppm\":" + String(ammoniaPpm) + ","
      "\"diaper_status\":\"" + mlResult.diaperStatus + "\","
      "\"probabilistic_diagnosis\":\"" + mlResult.diagnosis + "\""
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
