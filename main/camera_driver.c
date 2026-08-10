#include "camera_driver.h"
#include "esp_log.h"

static const char *TAG = "CAM_DRIVER";

#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    41
#define CAM_PIN_SIOD    17
#define CAM_PIN_SIOC    18

#define CAM_PIN_D7      39
#define CAM_PIN_D6      40
#define CAM_PIN_D5      42
#define CAM_PIN_D4      48
#define CAM_PIN_D3      45
#define CAM_PIN_D2      38
#define CAM_PIN_D1      37
#define CAM_PIN_D0      36
#define CAM_PIN_VSYNC   21
#define CAM_PIN_HREF    38
#define CAM_PIN_PCLK    11

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,
    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST
};

esp_err_t init_camera(void) {
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Initialization Failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "OV2640 Camera Initialized with PSRAM Buffers.");
    return ESP_OK;
}

camera_fb_t* capture_camera_frame(void) {
    return esp_camera_fb_get();
}

void release_camera_frame(camera_fb_t *fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}
