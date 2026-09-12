#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t rc_wifi_start(bool access_point);
bool rc_wifi_connected(void);
uint32_t rc_now_ms(void);
int rc_socket_timeout(int socket, unsigned ms);
bool rc_recv_exact(int socket, void *data, unsigned len);
bool rc_send_exact(int socket, const void *data, unsigned len);
