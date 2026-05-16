#pragma once
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief L298N fan PWM başlatma (LEDC).
 *        IN4 GPIO yön pini, ENA GPIO PWM hız pini.
 */
esp_err_t fan_control_init(void);

/**
 * @brief Ham duty cycle ayarla (0 = kapalı, 128 = %50, 255 = %100).
 */
void fan_set_duty(uint8_t duty);

void fan_off(void);
void fan_half(void);
void fan_full(void);
