#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "camera_driver.h"
#include "i2s_mic.h"
#include "ai_inference.h"

static const char *TAG = "NESTGUARD_MAIN";

// Task handles for Core allocation
TaskHandle_t xNetworkTaskHandle = NULL;
TaskHandle_t xInferenceTaskHandle = NULL;

// Core 0: Communications & Network Management
void network_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting Network & Telemetry Task on Core 0");
    while (1) {
        // Placeholder for MQTT/WebSocket connection maintenance and payload push
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// Core 1: Sensor Ingestion & Edge AI Inference
void inference_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting Sensor & Edge AI Inference Task on Core 1");
    
    size_t audio_bytes_read = 0;
    int32_t audio_buffer[512];

    while (1) {
        // 1. Read audio chunk from I2S DMA buffer and run edge cry-matching inference
        i2s_read_audio(audio_buffer, sizeof(audio_buffer), &audio_bytes_read);
        bool cry_detected = run_audio_inference(audio_buffer, audio_bytes_read);
        if (cry_detected) {
            ESP_LOGW(TAG, "CRITICAL: Audio anomaly/cry detected by edge model!");
            // TODO: Trigger alert payload queue for Core 0 network transmission
        }
        
        // 2. Grab frame from OV2640 Camera via PSRAM buffer and run obstruction check
        camera_fb_t *fb = capture_camera_frame();
        if (fb) {
            bool obstruction_detected = run_vision_inference(fb);
            if (obstruction_detected) {
                ESP_LOGW(TAG, "CRITICAL: Breathing zone obstruction detected by vision model!");
                // TODO: Trigger visual alert queue
            }
            
            // Return frame buffer back to driver pool
            release_camera_frame(fb);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Yield to prevent CPU starvation
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing Nestguard Firmware System...");

    // Initialize Non-Volatile Storage (required for Wi-Fi stack)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NEW_VERSION_FOUND || ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Hardware Drivers
    ESP_ERROR_CHECK(init_camera());
    init_i2s_mic();
    
    // Initialize Edge AI Models & Tensor Arena in PSRAM
    init_edge_ai_models();
    
    ESP_LOGI(TAG, "Hardware drivers and AI runtime initialized successfully.");

    // Create FreeRTOS Tasks pinned to specific cores
    xTaskCreatePinnedToCore(
        network_task, "NetworkTask", 4096, NULL, 5, &xNetworkTaskHandle, 0
    );

    xTaskCreatePinnedToCore(
        inference_task, "InferenceTask", 8192, NULL, 5, &xInferenceTaskHandle, 1
    );

    ESP_LOGI(TAG, "Nestguard system running and tasks scheduled.");
}
