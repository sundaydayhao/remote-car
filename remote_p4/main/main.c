#include "remote_ui.h"
#include "remote_link.h"
#include "rc_network.h"
#include "rc_codec.h"
#include "rc_audio.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t remote_video_start(void);
void app_main(void) {
    ESP_ERROR_CHECK(remote_ui_start());
    ESP_ERROR_CHECK(bsp_i2c_init());
    rc_codec_config_t cfg = {.bus = bsp_i2c_get_handle(), .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK,
        .ws = BSP_I2S_LCLK, .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN, .pa = BSP_POWER_AMP_IO};
    esp_err_t audio = rc_codec_init(&cfg);
    ESP_ERROR_CHECK(rc_wifi_start(false));
    if (audio == ESP_OK) audio = rc_audio_start();
    ESP_LOGI("remote", "Full duplex audio: %s", esp_err_to_name(audio));
    ESP_ERROR_CHECK(remote_link_start()); ESP_ERROR_CHECK(remote_video_start());
    for (;;) {
        rc_audio_stats_t s = rc_audio_stats();
        ESP_LOGI("remote", "audio tx=%lu rx=%lu drop=%lu mic=%lu ref=%lu io=%lu",
            (unsigned long)s.sent, (unsigned long)s.received, (unsigned long)s.dropped,
            (unsigned long)s.mic_peak, (unsigned long)s.ref_peak, (unsigned long)s.io_errors);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
