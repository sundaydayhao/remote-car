#include "remote_ui.h"
#include "remote_link.h"
#include "rc_network.h"
#include "esp_http_client.h"
#include "jpeg_decoder.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static void video_task(void *arg) {
    (void)arg;
    const size_t max_jpeg = 128 * 1024, rgb_size = 320 * 240 * 2;
    uint8_t *jpeg = heap_caps_malloc(max_jpeg, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *rgb = heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg || !rgb) { free(jpeg); free(rgb); vTaskDelete(NULL); }
    esp_http_client_config_t config = {.url = "http://192.168.4.1/jpg", .timeout_ms = 1500, .buffer_size = 4096, .keep_alive_enable = true};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(jpeg); free(rgb); vTaskDelete(NULL); }
    for (;;) {
        if (!remote_state().connected) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        bool ok = esp_http_client_open(client, 0) == ESP_OK;
        int64_t length = ok ? esp_http_client_fetch_headers(client) : -1;
        ok = ok && length > 0 && length <= max_jpeg && esp_http_client_get_status_code(client) == 200;
        size_t count = 0; uint32_t start = rc_now_ms();
        while (ok && count < length) {
            int n = esp_http_client_read(client, (char *)jpeg + count, length - count);
            if (n <= 0 || rc_now_ms() - start > 1500) { ok = false; break; }
            count += n;
        }
        esp_http_client_close(client);
        if (ok) {
            esp_jpeg_image_cfg_t cfg = {.indata = jpeg, .indata_size = count, .outbuf = rgb, .outbuf_size = rgb_size,
                .out_format = JPEG_IMAGE_FORMAT_RGB565, .out_scale = JPEG_IMAGE_SCALE_0};
            esp_jpeg_image_output_t output;
            ok = esp_jpeg_get_image_info(&cfg, &output) == ESP_OK && output.width == 320 && output.height == 240 && output.output_len == rgb_size;
            if (ok && esp_jpeg_decode(&cfg, &output) == ESP_OK) remote_ui_video(rgb, rgb_size);
        }
        vTaskDelay(pdMS_TO_TICKS(ok ? 50 : 250));
    }
}
esp_err_t remote_video_start(void) {
    return xTaskCreate(video_task, "jpeg_video", 8192, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
