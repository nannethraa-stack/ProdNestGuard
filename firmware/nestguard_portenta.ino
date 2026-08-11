#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90640.h>

// --- Network Credentials ---
const char* ssid = "YOUR_HOSPITAL_WIFI_SSID";
const char* password = "YOUR_HOSPITAL_WIFI_PASSWORD";

// --- Backend Server Details ---
const char* serverIP = "YOUR_BACKEND_SERVER_IP"; // e.g., "192.168.1.50"
const int serverPort = 3000;

WiFiClient client;
Adafruit_MLX90640 mlx;
float mlx90640[32 * 24]; // Thermal camera frame buffer

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Initialize Thermal Camera (MLX90640)
  Wire.begin();
  if (!mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("❌ MLX90640 thermal camera not found! Check wiring.");
    while (1) delay(1000);
  }
  
  // Set thermal sensor refresh rate (e.g., 8Hz)
  mlx.setRefreshRate(MLX9064_8_HZ);
  Serial.println("✅ Thermal Camera Initialized.");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to Wi-Fi. IP Address: " + WiFi.localIP().toString());
}

void loop() {
  // 1. Read Thermal Frame Data
  int status = mlx.getFrame(mlx90640);
  if (status < 0) {
    Serial.println("⚠️ Failed to read thermal frame");
    delay(500);
    return;
  }

  // Find maximum temperature in the matrix (simple safety check)
  float maxTemp = -273.15;
  for (int i = 0; i < 768; i++) {
    if (mlx90640[i] > maxTemp) {
      maxTemp = mlx90640[i];
    }
  }

  Serial.print("Current Max Temperature: ");
  Serial.print(maxTemp);
  Serial.println(" °C");

  // 2. Send Data to Node.js Backend if connected
  if (client.connect(serverIP, serverPort)) {
    String payload = "{\"device_id\":\"portenta_01\",\"max_temp\":" + String(maxTemp) + "}";
    
    client.println("POST /api/telemetry HTTP/1.1");
    client.println("Host: " + String(serverIP));
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(payload.length()));
    client.println("Connection: close");
    client.println();
    client.println(payload);
    
    client.stop();
  }

  delay(2000); // Send update every 2 seconds
}