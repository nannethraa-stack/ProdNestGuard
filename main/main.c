#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "camera_driver.h"
#include "i2s_mic.h"
#include "ai_inference.h"
#include "dashboard_html.h"

static const char *TAG = "NESTGUARD_MAIN";

// Wi-Fi Station Configuration (Home Router)
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
#define MAX_RETRY      5

// Wi-Fi SoftAP Configuration (Offline Fallback Hotspot)
#define AP_SSID        "ProdNestGuard_Direct"
#define AP_PASS        "nestguard123"
#define AP_CHANNEL     1
#define MAX_STA_CONN   4

static int s_retry_num = 0;
static bool is_fallback_ap_active = false;
static httpd_handle_t server = NULL;

// Alert Queue for Inter-Core Communication (Core 1 -> Core 0)
typedef enum {
    ALERT_TYPE_CRY = 1,
    ALERT_TYPE_OBSTRUCTION = 2
} alert_type_t;

typedef struct {
    alert_type_t type;
    uint32_t timestamp;
} alert_message_t;

static QueueHandle_t xAlertQueue = NULL;

// --- HTTP SERVER HANDLERS ---

// Handler for root URL ("/"): Serves the embedded HTML dashboard
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, dashboard_html, strlen(dashboard_html));
    return ESP_OK;
}

// Handler for camera stream ("/stream"): Grabs frames from PSRAM buffer and streams MJPEG
static esp_err_t stream_get_handler(httpd_req_t *req) {
    char part_buf[64];
    esp_err_t res = ESP_OK;
    
    // Set headers for Multipart X-Mixed-Replace stream (MJPEG)
    res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=123456789000000000000987654321");
    if (res != ESP_OK) {
        return res;
    }

    while (1) {
        camera_fb_t *fb = capture_camera_frame();
        if (!fb) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t hlen = snprintf(part_buf, sizeof(part_buf), "--123456789000000000000987654321\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", (unsigned int)fb->len);
        
        if (httpd_resp_send_chunk(req, part_buf, hlen) != ESP_OK) {
            release_camera_frame(fb);
            break;
        }
        if (httpd_resp_send_chunk(req, (char *)fb->buf, fb->len) != ESP_OK) {
            release_camera_frame(fb);
            break;
        }
        if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
            release_camera_frame(fb);
            break;
        }
        
        release_camera_frame(fb);
        vTaskDelay(pdMS_TO_TICKS(50)); // Limit stream framerate ~20 FPS
    }
    return ESP_OK;
}

// Register HTTP server endpoints
static httpd_handle_t start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;

    httpd_handle_t local_server = NULL;
    if (httpd_start(&local_server, &config) == ESP_OK) {
        // Register root URI handler
        httpd_uri_t root_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(local_server, &root_uri);

        // Register camera stream URI handler
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_get_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(local_server, &stream_uri);
        
        ESP_LOGI(TAG, "Embedded HTTP Server started successfully on port 80.");
    }
    return local_server;
}

// --- WI-FI & EVENT HANDLING ---

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Wi-Fi connection failed. Retry attempt %d/%d", s_retry_num, MAX_RETRY);
        } else if (!is_fallback_ap_active) {
            ESP_LOGW(TAG, "Router unreachable. Activating offline SoftAP fallback mode (%s)...", AP_SSID);
            
            wifi_config_t ap_config = {
                .ap = {
                    .ssid = AP_SSID,
                    .ssid_len = strlen(AP_SSID),
                    .channel = AP_CHANNEL,
                    .password = AP_PASS,
                    .max_connection = MAX_STA_CONN,
                    .authmode = WIFI_AUTH_WPA2_PSK,
                },
            };
            if (strlen(AP_PASS) == 0) {
                ap_config.ap.authmode = WIFI_AUTH_OPEN;
            }

            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
            ESP_ERROR_CHECK(esp_wifi_start());
            
            is_fallback_ap_active = true;
            ESP_LOGI(TAG, "SoftAP active. Connect to Wi-Fi '%s' and browse to http://192.168.4.1", AP_SSID);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Wi-Fi connected successfully! Access dashboard at: http://<device_ip>");
        s_retry_num = 0;
        is_fallback_ap_active = false;
    }
}

void wifi_init_sta_with_fallback(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi initialization and fallback monitor started.");
}

// Core 0: Communications & Local Dashboard Management
void network_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting Network & Dashboard Task on Core 0");
    
    // Start the local embedded HTTP server
    server = start_webserver();

    alert_message_t alert;
    while (1) {
        if (xQueueReceive(xAlertQueue, &alert, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (alert.type == ALERT_TYPE_CRY) {
                ESP_LOGW(TAG, "LOCAL EVENT: Cry detected and logged locally.");
            } else if (alert.type == ALERT_TYPE_OBSTRUCTION) {
                ESP_LOGW(TAG, "LOCAL EVENT: Obstruction detected and logged locally.");
            }
        }
    }
}

// Core 1: Sensor Ingestion & Edge AI Inference
void inference_task(void *pvParameters) {
    ESP_LOGI(TAG, "Starting Sensor & Edge AI Inference Task on Core 1");
    
    size_t audio_bytes_read = 0;
    int32_t audio_buffer[512];
    alert_message_t alert;

    while (1) {
        // 1. Audio Inference Loop
        i2s_read_audio(audio_buffer, sizeof(audio_buffer), &audio_bytes_read);
        if (audio_bytes_read > 0 && run_audio_inference(audio_buffer, audio_bytes_read)) {
            ESP_LOGW(TAG, "CRITICAL: Audio anomaly/cry detected! Dispatching alert...");
            alert.type = ALERT_TYPE_CRY;
            alert.timestamp = xthal_get_ccount();
            xQueueSend(xAlertQueue, &alert, 0);
        }
        
        // 2. Vision Inference Loop
        camera_fb_t *fb = capture_camera_frame();
        if (fb != NULL) {
            if (run_vision_inference(fb)) {
                ESP_LOGW(TAG, "CRITICAL: Breathing obstruction detected! Dispatching alert...");
                alert.type = ALERT_TYPE_OBSTRUCTION;
                alert.timestamp = xthal_get_ccount();
                xQueueSend(xAlertQueue, &alert, 0);
            }
            release_camera_frame(fb);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Initializing Nestguard Firmware System...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NEW_VERSION_FOUND || ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xAlertQueue = xQueueCreate(10, sizeof(alert_message_t));
    if (xAlertQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create inter-core alert queue!");
        return;
    }

    ESP_ERROR_CHECK(init_camera());
    init_i2s_mic();
    init_edge_ai_models();

    wifi_init_sta_with_fallback();
    
    ESP_LOGI(TAG, "System initialization complete. Launching partitioned tasks.");

    xTaskCreatePinnedToCore(network_task, "NetworkTask", 4096, NULL, 5, &xNetworkTaskHandle, 0);
    xTaskCreatePinnedToCore(inference_task, "InferenceTask", 8192, NULL, 5, &xInferenceTaskHandle, 1);
}
