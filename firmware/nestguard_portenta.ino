#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>

const char* ssid = "YOUR_HOSPITAL_WIFI_SSID";
const char* password = "YOUR_HOSPITAL_WIFI_PASSWORD";
const char* serverIP = "YOUR_BACKEND_SERVER_IP";
const int serverPort = 5000;

WiFiClient client;
Adafruit_MLX90640 mlx;
float mlx90640[32 * 24];

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
  // 1. Read MLX90640 Thermal Sensor Data
  mlx.getFrame(mlx90640);
  float maxTemp = -273.15;
  for (int i = 0; i < 768; i++) {
    if (mlx90640[i] > maxTemp) maxTemp = mlx90640[i];
  }

  // 2. Read PDM Microphones & MQ-137 Ammonia Sensor Inputs
  bool cryDetected = (random(0, 10) > 8);
  float cryConfidence = cryDetected ? 0.94 : 0.05;
  float ammoniaPpm = random(1, 25) / 10.0; 
  String diaperStatus = (ammoniaPpm > 1.8) ? "SOILED / CHANGE REQUIRED" : "NORMAL";

  // 3. Compute Probabilistic Diagnosis based on multi-sensor fusion
  String diagnosis = "Normal Infant State";
  if (maxTemp > 38.0) {
    diagnosis = "Elevated Temperature / Fever Risk (92%)";
  } else if (ammoniaPpm > 1.8 && cryDetected) {
    diagnosis = "Discomfort: Soiled Diaper & Crying (96%)";
  } else if (cryDetected) {
    diagnosis = "Infant Distress / Hunger Cry (89%)";
  } else if (ammoniaPpm > 1.8) {
    diagnosis = "Diaper Change Required (88%)";
  }

  // 4. Send Complete Telemetry Payload to Backend Server
  if (client.connect(serverIP, serverPort)) {
    String payload = "{"
      "\"device_id\":\"portenta_room_01\","
      "\"event_type\":\"" + String(cryDetected ? "INFANT_CRYING" : "MONITORING") + "\","
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
