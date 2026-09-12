#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool connected, motor_ready, audio_ready, audio_enabled;
    int16_t left, right;
    unsigned speed, direction;
    uint32_t ack_ms, rtt_ms;
} remote_state_t;
esp_err_t remote_link_start(void);
void remote_press(unsigned direction, bool new_press);
void remote_release(void);
void remote_speed(unsigned percent);
void remote_audio_toggle(void);
remote_state_t remote_state(void);
