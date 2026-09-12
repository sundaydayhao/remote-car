#include "remote_link.h"
#include "rc_network.h"
#include "rc_protocol.h"
#include "rc_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include <fcntl.h>
#include <errno.h>

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static remote_state_t state = {.speed = 35, .audio_enabled = true};
static uint32_t input_ms;
static bool armed;
remote_state_t remote_state(void) {
    portENTER_CRITICAL(&lock); remote_state_t s = state; portEXIT_CRITICAL(&lock); return s;
}
void remote_press(unsigned direction, bool new_press) {
    portENTER_CRITICAL(&lock);
    if (new_press && state.connected && state.motor_ready) armed = true;
    if (armed && direction <= 4) { state.direction = direction; input_ms = rc_now_ms(); }
    portEXIT_CRITICAL(&lock);
}
void remote_release(void) {
    portENTER_CRITICAL(&lock); armed = false; state.direction = 0; portEXIT_CRITICAL(&lock);
}
void remote_speed(unsigned v) {
    portENTER_CRITICAL(&lock); state.speed = v > 100 ? 100 : v; portEXIT_CRITICAL(&lock);
}
void remote_audio_toggle(void) {
    portENTER_CRITICAL(&lock); state.audio_enabled = !state.audio_enabled; portEXIT_CRITICAL(&lock);
}
static void disconnected(void) {
    portENTER_CRITICAL(&lock);
    state.connected = false; state.motor_ready = false; state.audio_ready = false;
    state.direction = 0; state.left = state.right = 0; armed = false;
    portEXIT_CRITICAL(&lock);
    rc_audio_peer(0, 0, false);
}
static int connect_car(struct sockaddr_in *address) {
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) return -1;
    fcntl(fd, F_SETFL, O_NONBLOCK);
    int e = connect(fd, (void *)address, sizeof(*address));
    if (e < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (e < 0) {
        fd_set writes; FD_ZERO(&writes); FD_SET(fd, &writes);
        struct timeval tv = {.tv_sec = 1};
        if (select(fd + 1, NULL, &writes, NULL, &tv) <= 0) { close(fd); return -1; }
        int error = 0; socklen_t size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) { close(fd); return -1; }
    }
    fcntl(fd, F_SETFL, 0); rc_socket_timeout(fd, 100);
    int yes = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return fd;
}
static void link_task(void *arg) {
    (void)arg;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(RC_CONTROL_PORT)};
    inet_pton(AF_INET, "192.168.4.1", &address.sin_addr);
    for (;;) {
        disconnected();
        if (!rc_wifi_connected()) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
        int fd = connect_car(&address);
        if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(400)); continue; }
        uint8_t packet[RC_CONTROL_BYTES]; rc_command_t hello;
        bool good = rc_recv_exact(fd, packet, sizeof(packet)) && rc_control_decode(packet, sizeof(packet), &hello) &&
                    hello.session && hello.seq == 0 && !hello.left && !hello.right;
        uint32_t sequence = 0;
        while (good && rc_wifi_connected()) {
            rc_command_t command = {.session = hello.session, .seq = ++sequence};
            portENTER_CRITICAL(&lock);
            if (rc_now_ms() - input_ms > RC_INPUT_MS) { state.direction = 0; armed = false; }
            if (sequence > 1) rc_mix(state.direction, state.speed, &command.left, &command.right);
            command.flags = state.audio_enabled ? RC_AUDIO_ON : 0;
            portEXIT_CRITICAL(&lock);
            rc_control_encode(packet, &command); uint32_t start = rc_now_ms();
            rc_command_t ack;
            good = rc_send_exact(fd, packet, sizeof(packet)) && rc_recv_exact(fd, packet, sizeof(packet)) &&
                   rc_control_decode(packet, sizeof(packet), &ack) && ack.session == hello.session && ack.seq == sequence &&
                   rc_now_ms() - start < 150;
            if (!good) break;
            portENTER_CRITICAL(&lock);
            state.connected = true; state.motor_ready = (ack.flags & RC_MOTOR_OK) != 0;
            state.audio_ready = (ack.flags & RC_AUDIO_OK) != 0;
            state.left = ack.left; state.right = ack.right;
            state.ack_ms = rc_now_ms(); state.rtt_ms = state.ack_ms - start;
            portEXIT_CRITICAL(&lock);
            rc_audio_peer(address.sin_addr.s_addr, hello.session, (command.flags & ack.flags & RC_AUDIO_ON) && (ack.flags & RC_AUDIO_OK));
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        disconnected(); shutdown(fd, SHUT_RDWR); close(fd);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
esp_err_t remote_link_start(void) {
    return xTaskCreate(link_task, "remote_control", 4096, NULL, 9, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
