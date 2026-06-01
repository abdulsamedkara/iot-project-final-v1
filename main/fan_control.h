#pragma once
#include <stdint.h>
#include "esp_err.h"

// Initializes the L298N motor driver for the fan using LEDC PWM.
// Configures the direction pin and the PWM speed pin.
esp_err_t fan_control_init(void);

// Sets the raw duty cycle for the fan speed.
// 0 means the fan is off, 128 is around 50% speed, and 255 is 100% speed.
void fan_set_duty(uint8_t duty);

// Turns the fan completely off by setting the duty cycle to 0.
void fan_off(void);

// Sets the fan to a medium speed, typically used to save power or reduce noise.
void fan_half(void);

// Sets the fan to full speed for maximum cooling.
void fan_full(void);
