#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    i2c_master_bus_handle_t bus;
    int mclk, bclk, ws, dout, din, pa;
    bool es7210;
} rc_codec_config_t;
esp_err_t rc_codec_init(const rc_codec_config_t *cfg);
esp_err_t rc_codec_read(int16_t *stereo, size_t samples);
esp_err_t rc_codec_write(const int16_t *stereo, size_t samples);
