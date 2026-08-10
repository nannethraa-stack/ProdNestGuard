#include "i2s_mic.h"
#include "driver/i2s.h"
#include "esp_log.h"

static const char *TAG = "I2S_MIC";

#define I2S_PORT I2S_NUM_0
#define I2S_SCK_PIN  (GPIO_NUM_4)
#define I2S_WS_PIN   (GPIO_NUM_5)
#define I2S_SD_PIN   (GPIO_NUM_6)

void init_i2s_mic(void) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 512,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pin_config));
    i2s_zero_dma_buffer(I2S_PORT);
    
    ESP_LOGI(TAG, "I2S Digital Microphone Driver Installed on Port 0.");
}

void i2s_read_audio(void *dest, size_t size, size_t *bytes_read) {
    i2s_read(I2S_PORT, dest, size, bytes_read, portMAX_DELAY);
}
