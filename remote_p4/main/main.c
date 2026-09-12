#include "remote_ui.h"
#include "remote_link.h"
#include "rc_network.h"
#include "rc_codec.h"
#include "rc_audio.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t remote_video_start(void);
static void log_memory(const char *stage) {
    ESP_LOGI("remote", "%s: internal free=%u largest=%u, PSRAM free=%u", stage,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
void app_main(void) {
    ESP_ERROR_CHECK(remote_ui_start());
    ESP_ERROR_CHECK(bsp_i2c_init());
    rc_codec_config_t cfg = {.bus = bsp_i2c_get_handle(), .mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_SCLK,
        .ws = BSP_I2S_LCLK, .dout = BSP_I2S_DOUT, .din = BSP_I2S_DSIN, .pa = BSP_POWER_AMP_IO};
    esp_err_t audio = rc_codec_init(&cfg);
    ESP_ERROR_CHECK(rc_wifi_start(false));
    log_memory("Before AEC");
    if (audio == ESP_OK) audio = rc_audio_start();
    ESP_LOGI("remote", "Full duplex audio: %s", esp_err_to_name(audio));
    log_memory("After AEC");
    ESP_ERROR_CHECK(remote_link_start()); ESP_ERROR_CHECK(remote_video_start());
    log_memory("All tasks started");
    for (;;) {
        rc_audio_stats_t s = rc_audio_stats();
        ESP_LOGI("remote", "audio tx=%lu rx=%lu drop=%lu mic=%lu ref=%lu io=%lu",
            (unsigned long)s.sent, (unsigned long)s.received, (unsigned long)s.dropped,
            (unsigned long)s.mic_peak, (unsigned long)s.ref_peak, (unsigned long)s.io_errors);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
