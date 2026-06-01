#pragma once
#include <stdint.h>
#include "esp_err.h"

// Configures the PWM timer and connects it to the appropriate output pin
esp_err_t led_strip_init(void);

// Changes the duty cycle to adjust how bright the LEDs are
void      led_strip_set_brightness(uint8_t brightness);

// Brings the LEDs to their maximum brightness level
void      led_strip_on(void);

// Turns off the LEDs entirely
void      led_strip_off(void);
