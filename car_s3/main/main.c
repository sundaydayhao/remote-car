#include "szpi_board.h"
#include "motor.h"
#include "car_server.h"
#include "rc_network.h"
#include "rc_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void) {
    esp_err_t m = motor_start();
    ESP_LOGI("car", "Motor adapter: %s", esp_err_to_name(m));
    ESP_ERROR_CHECK(szpi_board_init());
    esp_err_t cam = szpi_camera_init();
    ESP_LOGI("car", "GC0308: %s", esp_err_to_name(cam));
    ESP_ERROR_CHECK(rc_wifi_start(true));
    esp_err_t a = szpi_audio_init();
    if (a == ESP_OK) a = rc_audio_start();
    if (a == ESP_OK) a = szpi_audio_pa_enable();
    ESP_LOGI("car", "Full duplex audio: %s", esp_err_to_name(a));
    ESP_ERROR_CHECK(car_server_start(cam == ESP_OK));
    for (;;) {
        rc_audio_stats_t s = rc_audio_stats();
        ESP_LOGI("car", "audio tx=%lu rx=%lu drop=%lu mic=%lu ref=%lu io=%lu",
            (unsigned long)s.sent, (unsigned long)s.received, (unsigned long)s.dropped,
            (unsigned long)s.mic_peak, (unsigned long)s.ref_peak, (unsigned long)s.io_errors);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
