#include "rc_codec.h"
#include "sdkconfig.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"

static i2s_chan_handle_t tx, rx;
static esp_codec_dev_handle_t speaker, microphone;
static const char *TAG = "codec";

esp_err_t rc_codec_init(const rc_codec_config_t *cfg) {
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch.dma_desc_num = 4; ch.dma_frame_num = 128; ch.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch, &tx, &rx), TAG, "channels");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = cfg->mclk, .bclk = cfg->bclk, .ws = cfg->ws,
                     .dout = cfg->dout, .din = cfg->es7210 ? -1 : cfg->din}
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "TX");
    if (cfg->es7210) {
        i2s_tdm_config_t tdm = {
            .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(16000),
            .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO, I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .gpio_cfg = {.mclk = cfg->mclk, .bclk = cfg->bclk, .ws = cfg->ws, .dout = -1, .din = cfg->din}
        };
        tdm.slot_cfg.total_slot = 4;
        ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(rx, &tdm), TAG, "ES7210 TDM RX");
    } else ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std), TAG, "ES8311 RX");
    audio_codec_i2s_cfg_t data_cfg = {.port = 0, .tx_handle = tx, .rx_handle = rx};
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&data_cfg);
    audio_codec_i2c_cfg_t ctrl_cfg = {.port = 0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = cfg->bus};
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!data || !ctrl || !gpio) return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t dac_cfg = {
        .ctrl_if = ctrl, .gpio_if = gpio,
        .codec_mode = cfg->es7210 ? ESP_CODEC_DEV_WORK_MODE_DAC : ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = cfg->pa, .use_mclk = true, .no_dac_ref = false,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *dac = es8311_codec_new(&dac_cfg);
    if (!dac) return ESP_FAIL;
    esp_codec_dev_cfg_t dev = {.dev_type = cfg->es7210 ? ESP_CODEC_DEV_TYPE_OUT : ESP_CODEC_DEV_TYPE_IN_OUT,
                              .codec_if = dac, .data_if = data};
    speaker = esp_codec_dev_new(&dev);
    if (!speaker) return ESP_ERR_NO_MEM;
    if (cfg->es7210) {
        ctrl_cfg.addr = 0x82; // esp_codec_dev expects 8-bit address; SZPI ES7210 is 7-bit 0x41.
        const audio_codec_ctrl_if_t *adc_ctrl = audio_codec_new_i2c_ctrl(&ctrl_cfg);
        if (!adc_ctrl) return ESP_ERR_NO_MEM;
        es7210_codec_cfg_t adc_cfg = {.ctrl_if = adc_ctrl,
            .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4};
        const audio_codec_if_t *adc = es7210_codec_new(&adc_cfg);
        if (!adc_ctrl || !adc) return ESP_FAIL;
        dev.dev_type = ESP_CODEC_DEV_TYPE_IN; dev.codec_if = adc;
        microphone = esp_codec_dev_new(&dev);
    } else microphone = speaker;
    if (!microphone) return ESP_ERR_NO_MEM;
    esp_codec_dev_sample_info_t fs = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16};
    if (esp_codec_dev_open(speaker, &fs) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    if (microphone != speaker) {
        // ES7210 TDM order: MIC1, MIC3(reference), MIC2, MIC4. Receive slots 0 and 1.
        // codec_dev expands TX SLOT width to 32 bits to share the 64-BCLK frame,
        // keeping 16-bit DATA width (DMA samples remain int16_t).
        fs.channel = 4; fs.channel_mask = 0x03;
        if (esp_codec_dev_open(microphone, &fs) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    }
    if (esp_codec_dev_set_out_vol(speaker, CONFIG_RC_SPEAKER_VOLUME) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    if (cfg->es7210) {
        // Channel mask refers to physical ADC input, not packed stereo position.
        if (esp_codec_dev_set_in_channel_gain(microphone, 1 << 0, CONFIG_RC_MIC_GAIN_DB) != ESP_CODEC_DEV_OK ||
            esp_codec_dev_set_in_channel_gain(microphone, 1 << 2, CONFIG_RC_REF_GAIN_DB) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    } else if (esp_codec_dev_set_in_gain(microphone, CONFIG_RC_MIC_GAIN_DB) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    return ESP_OK;
}
esp_err_t rc_codec_read(int16_t *stereo, size_t samples) {
    size_t bytes = 0;
    esp_err_t e = i2s_channel_read(rx, stereo, samples * 4, &bytes, 100);
    return e == ESP_OK && bytes == samples * 4 ? ESP_OK : ESP_FAIL;
}
esp_err_t rc_codec_write(const int16_t *stereo, size_t samples) {
    size_t bytes = 0;
    esp_err_t e = i2s_channel_write(tx, stereo, samples * 4, &bytes, 100);
    return e == ESP_OK && bytes == samples * 4 ? ESP_OK : ESP_FAIL;
}
