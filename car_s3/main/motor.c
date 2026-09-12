#include "motor.h"
#include "rc_network.h"
#include "rc_protocol.h"
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static i2c_master_dev_handle_t pwm;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static motor_state_t state;
static int16_t wanted_left, wanted_right;
static const char *TAG = "motor";
void motor_command(int16_t left, int16_t right) {
    if (left < -100 || left > 100 || right < -100 || right > 100) left = right = 0;
    portENTER_CRITICAL(&lock);
    wanted_left = left; wanted_right = right; state.command_ms = rc_now_ms();
    portEXIT_CRITICAL(&lock);
}
motor_state_t motor_state(void) {
    portENTER_CRITICAL(&lock); motor_state_t s = state; portEXIT_CRITICAL(&lock); return s;
}
static esp_err_t reg_write(uint8_t reg, uint8_t v) {
    uint8_t b[] = {reg, v}; return i2c_master_transmit(pwm, b, sizeof(b), 20);
}
static void channel(uint8_t *p, unsigned duty) {
    memset(p, 0, 4);
    if (duty == 0) p[3] = 0x10; // full off
    else if (duty >= 4096) p[1] = 0x10; // full on
    else { p[2] = duty & 0xff; p[3] = duty >> 8; }
}
static esp_err_t apply(int l, int r) {
    uint8_t b[29] = {0x06};
#if CONFIG_RC_MOTOR_INVERT_LEFT
    l = -l;
#endif
#if CONFIG_RC_MOTOR_INVERT_RIGHT
    r = -r;
#endif
    channel(b + 1, l > 0 ? 4096 : 0); channel(b + 5, l < 0 ? 4096 : 0);
    channel(b + 9, abs(l) * 4096 / 100);
    channel(b + 13, r > 0 ? 4096 : 0); channel(b + 17, r < 0 ? 4096 : 0);
    channel(b + 21, abs(r) * 4096 / 100);
    channel(b + 25, l || r ? 4096 : 0); // TB6612 STBY
    return i2c_master_transmit(pwm, b, sizeof(b), 20);
}
static void motor_task(void *arg) {
    (void)arg;
    int l = 0, r = 0;
    for (;;) {
        int target_l, target_r; uint32_t stamp;
        portENTER_CRITICAL(&lock);
        target_l = wanted_left; target_r = wanted_right; stamp = state.command_ms;
        portEXIT_CRITICAL(&lock);
        bool stop = rc_command_expired(rc_now_ms(), stamp) || (!target_l && !target_r);
        l = stop ? 0 : rc_motor_ramp(l, target_l); r = stop ? 0 : rc_motor_ramp(r, target_r);
        if (apply(l, r) != ESP_OK) {
            apply(0, 0); // best effort; hardware STBY/power cut is needed if the bus has failed.
            portENTER_CRITICAL(&lock); state.ready = false; state.left = state.right = 0; portEXIT_CRITICAL(&lock);
            ESP_LOGE(TAG, "I2C motor fault latched; check adapter and reset");
            vTaskDelete(NULL);
        }
        portENTER_CRITICAL(&lock); state.left = l; state.right = r; portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t motor_start(void) {
#if !CONFIG_RC_MOTOR_ENABLE
    return ESP_ERR_NOT_SUPPORTED;
#endif
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t cfg = {.i2c_port = 1, .sda_io_num = 10, .scl_io_num = 11,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true};
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &bus), TAG, "extension bus");
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x40, .scl_speed_hz = 400000};
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev, &pwm), TAG, "PCA9685");
    ESP_RETURN_ON_ERROR(reg_write(0, 0x30), TAG, "sleep, auto increment, ALLCALL off");
    ESP_RETURN_ON_ERROR(reg_write(1, 0x04), TAG, "totem pole; change on STOP");
    ESP_RETURN_ON_ERROR(reg_write(0xfe, 5), TAG, "1017 Hz PWM");
    ESP_RETURN_ON_ERROR(apply(0, 0), TAG, "safe outputs");
    ESP_RETURN_ON_ERROR(reg_write(0, 0x20), TAG, "wake");
    vTaskDelay(pdMS_TO_TICKS(2));
    if (xTaskCreate(motor_task, "motor_deadman", 3072, NULL, 10, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    portENTER_CRITICAL(&lock); state.ready = true; portEXIT_CRITICAL(&lock);
    return ESP_OK;
}
