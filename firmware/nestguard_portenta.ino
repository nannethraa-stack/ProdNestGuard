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
  // 1. Read Thermal Data
  mlx.getFrame(mlx90640);
  float maxTemp = -273.15;
  for (int i = 0; i < 768; i++) {
    if (mlx90640[i] > maxTemp) maxTemp = mlx90640[i];
  }

  // 2. Simulate/Read Audio (Cry Detection) & Gas Sensor (Ammonia / Diaper)
  // In production, PDM microphone buffers feed your on-chip TinyML audio model
  bool cryDetected = (random(0, 10) > 8); // Example trigger
  float cryConfidence = cryDetected ? 0.94 : 0.05;
  
  // Gas sensor (e.g., MQ-137 / MQ-135 for ammonia VOCs)
  float ammoniaPpm = random(1, 25) / 10.0; // ppm reading
  String diaperStatus = (ammoniaPpm > 1.8) ? "SOILED / CHANGE REQUIRED" : "NORMAL";
  String eventType = cryDetected ? "INFANT_CRYING" : (ammoniaPpm > 1.8 ? "AMMONIA_SPIKE" : "NORMAL_MONITORING");

  // 3. Send Payload to Node.js Backend
  if (client.connect(serverIP, serverPort)) {
    String payload = "{"
      "\"device_id\":\"room_101\","
      "\"event_type\":\"" + eventType + "\","
      "\"confidence\":" + String(cryConfidence) + ","
      "\"max_temp\":" + String(maxTemp) + ","
      "\"ammonia_ppm\":" + String(ammoniaPpm) + ","
      "\"diaper_status\":\"" + diaperStatus + "\""
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

  delay(5000); // Send update every 5 seconds
}
