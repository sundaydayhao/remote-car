#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t remote_ui_start(void);
void remote_ui_video(const uint8_t *rgb565, uint32_t bytes);
