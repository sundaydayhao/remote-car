#include "car_server.h"
#include "motor.h"
#include "rc_protocol.h"
#include "rc_network.h"
#include "rc_audio.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include <stdlib.h>
#include <stdio.h>

static bool camera_ready;
static const char *TAG = "car_server";
static uint8_t capabilities(void) {
    return (motor_state().ready ? RC_MOTOR_OK : 0) | (rc_audio_stats().ready ? RC_AUDIO_OK : 0);
}
static void control_task(void *arg) {
    (void)arg;
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(RC_CONTROL_PORT), .sin_addr.s_addr = INADDR_ANY};
    int yes = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (listener < 0 || bind(listener, (void *)&addr, sizeof(addr)) || listen(listener, 1)) {
        ESP_LOGE(TAG, "control listen failed"); vTaskDelete(NULL);
    }
    for (;;) {
        struct sockaddr_in client; socklen_t len = sizeof(client);
        int fd = accept(listener, (void *)&client, &len);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        motor_command(0, 0); rc_socket_timeout(fd, 150);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        uint32_t session = esp_random() | 1u;
        rc_guard_t guard; rc_guard_init(&guard, session, rc_now_ms());
        rc_command_t v = {.session = session, .flags = capabilities()};
        uint8_t packet[RC_CONTROL_BYTES]; rc_control_encode(packet, &v);
        bool good = rc_send_exact(fd, packet, sizeof(packet));
        while (good && rc_recv_exact(fd, packet, sizeof(packet))) {
            if (!rc_control_decode(packet, sizeof(packet), &v) || !rc_guard_accept(&guard, &v, rc_now_ms())) break;
            motor_command(v.left, v.right);
            rc_audio_peer(client.sin_addr.s_addr, session, (v.flags & RC_AUDIO_ON) != 0);
            motor_state_t m = motor_state();
            v.left = m.left; v.right = m.right; v.flags = (v.flags & RC_AUDIO_ON) | capabilities();
            rc_control_encode(packet, &v); good = rc_send_exact(fd, packet, sizeof(packet));
        }
        motor_command(0, 0); rc_audio_peer(0, 0, false);
        shutdown(fd, SHUT_RDWR); close(fd);
    }
}
static esp_err_t jpg(httpd_req_t *req) {
    if (!camera_ready) { httpd_resp_set_status(req, "503 Service Unavailable"); return httpd_resp_sendstr(req, "Camera unavailable"); }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed");
    uint8_t *jpeg = NULL; size_t size = 0;
    bool ok = frame2jpg(fb, CONFIG_RC_CAMERA_JPEG_QUALITY, &jpeg, &size);
    esp_camera_fb_return(fb);
    if (!ok || !jpeg || size > 128 * 1024) { free(jpeg); return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JPEG failed"); }
    httpd_resp_set_type(req, "image/jpeg"); httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, (const char *)jpeg, size); free(jpeg); return e;
}
static esp_err_t status(httpd_req_t *req) {
    char out[400]; motor_state_t m = motor_state(); rc_audio_stats_t a = rc_audio_stats();
    snprintf(out, sizeof(out), "{\"camera\":%s,\"motor\":%s,\"left\":%d,\"right\":%d,\"aec\":%s,\"mic_peak\":%lu,\"ref_peak\":%lu,\"audio_tx\":%lu,\"audio_rx\":%lu,\"drops\":%lu,\"io_errors\":%lu}",
        camera_ready ? "true" : "false", m.ready ? "true" : "false", m.left, m.right, a.ready ? "true" : "false",
        (unsigned long)a.mic_peak, (unsigned long)a.ref_peak, (unsigned long)a.sent, (unsigned long)a.received,
        (unsigned long)a.dropped, (unsigned long)a.io_errors);
    httpd_resp_set_type(req, "application/json"); return httpd_resp_sendstr(req, out);
}
esp_err_t car_server_start(bool ready) {
    camera_ready = ready;
    if (xTaskCreate(control_task, "car_control", 4096, NULL, 9, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; config.task_priority = 3; config.recv_wait_timeout = 2; config.send_wait_timeout = 2;
    httpd_handle_t http = NULL;
    esp_err_t e = httpd_start(&http, &config); if (e != ESP_OK) return e;
    httpd_uri_t image_uri = {.uri = "/jpg", .method = HTTP_GET, .handler = jpg};
    httpd_uri_t status_uri = {.uri = "/status", .method = HTTP_GET, .handler = status};
    ESP_ERROR_CHECK(httpd_register_uri_handler(http, &image_uri));
    return httpd_register_uri_handler(http, &status_uri);
}
