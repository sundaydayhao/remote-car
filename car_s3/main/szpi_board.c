#include "szpi_board.h"
#include "rc_codec.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t pca9557;
static const char *TAG = "szpi";
static esp_err_t expander_write(uint8_t reg, uint8_t value) {
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(pca9557, bytes, 2, 50);
}
esp_err_t szpi_board_init(void) {
    i2c_master_bus_config_t cfg = {.i2c_port = 0, .sda_io_num = 1, .scl_io_num = 2,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &bus), TAG, "I2C");
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x19, .scl_speed_hz = 400000};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &pca9557), TAG, "PCA9557");
    // LCD CS high, PA off, DVP power-down low. Keep unused expander pins as inputs.
    ESP_RETURN_ON_ERROR(expander_write(1, 0x01), TAG, "safe latch");
    ESP_RETURN_ON_ERROR(expander_write(2, 0x00), TAG, "polarity");
    ESP_RETURN_ON_ERROR(expander_write(3, 0xf8), TAG, "direction");
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}
esp_err_t szpi_audio_init(void) {
    rc_codec_config_t cfg = {.bus = bus, .mclk = 38, .bclk = 14, .ws = 13,
        .dout = 45, .din = 12, .pa = -1, .es7210 = true};
    return rc_codec_init(&cfg);
}
esp_err_t szpi_audio_pa_enable(void) { return expander_write(1, 0x03); }
esp_err_t szpi_camera_init(void) {
    camera_config_t c = {
        .pin_pwdn = -1, .pin_reset = -1, .pin_xclk = 5,
        .pin_sccb_sda = -1, .pin_sccb_scl = -1, .sccb_i2c_port = 0,
        .pin_d7 = 9, .pin_d6 = 4, .pin_d5 = 6, .pin_d4 = 15,
        .pin_d3 = 17, .pin_d2 = 8, .pin_d1 = 18, .pin_d0 = 16,
        .pin_vsync = 3, .pin_href = 46, .pin_pclk = 7,
        .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_RGB565, .frame_size = FRAMESIZE_QVGA,
        .fb_location = CAMERA_FB_IN_PSRAM, .fb_count = 1, .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    };
    return esp_camera_init(&c);
}
