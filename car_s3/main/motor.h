#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool ready; int16_t left, right; uint32_t command_ms; } motor_state_t;
esp_err_t motor_start(void);
void motor_command(int16_t left, int16_t right);
motor_state_t motor_state(void);
