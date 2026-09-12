#include "rc_network.h"
#include "rc_protocol.h"
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include <string.h>

static EventGroupHandle_t wifi_events;
uint32_t rc_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == WIFI_EVENT && (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)) {
        xEventGroupClearBits(wifi_events, 1);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) xEventGroupSetBits(wifi_events, 1);
}
bool rc_wifi_connected(void) { return wifi_events && (xEventGroupGetBits(wifi_events) & 1); }
esp_err_t rc_wifi_start(bool ap) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase()); e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_events = xEventGroupCreate();
    if (!wifi_events) return ESP_ERR_NO_MEM;
    if (ap) esp_netif_create_default_wifi_ap(); else esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t cfg = {0};
    if (strlen(CONFIG_RC_WIFI_PASSWORD) < 8 || strlen(CONFIG_RC_WIFI_PASSWORD) > 63) return ESP_ERR_INVALID_ARG;
    if (ap) {
        strlcpy((char *)cfg.ap.ssid, CONFIG_RC_WIFI_SSID, sizeof(cfg.ap.ssid));
        strlcpy((char *)cfg.ap.password, CONFIG_RC_WIFI_PASSWORD, sizeof(cfg.ap.password));
        cfg.ap.channel = 6; cfg.ap.max_connection = 1; cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        strlcpy((char *)cfg.sta.ssid, CONFIG_RC_WIFI_SSID, sizeof(cfg.sta.ssid));
        strlcpy((char *)cfg.sta.password, CONFIG_RC_WIFI_PASSWORD, sizeof(cfg.sta.password));
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(ap ? WIFI_MODE_AP : WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ap ? WIFI_IF_AP : WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (!ap) ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return ESP_OK;
}
int rc_socket_timeout(int fd, unsigned ms) {
    struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    int e = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return e | setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
bool rc_recv_exact(int fd, void *data, unsigned len) {
    uint8_t *p = data;
    uint32_t start = rc_now_ms();
    while (len) {
        int n = recv(fd, p, len, 0);
        if (n <= 0 || rc_now_ms() - start > RC_DEADMAN_MS) return false;
        p += n; len -= n;
    }
    return true;
}
bool rc_send_exact(int fd, const void *data, unsigned len) {
    const uint8_t *p = data;
    while (len) {
        int n = send(fd, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}
