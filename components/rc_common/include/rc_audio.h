#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool ready;
    uint32_t sent, received, dropped, underruns, io_errors;
    uint32_t mic_peak, ref_peak;
} rc_audio_stats_t;
esp_err_t rc_audio_start(void);
void rc_audio_peer(uint32_t ip_network_order, uint32_t session, bool enabled);
rc_audio_stats_t rc_audio_stats(void);
