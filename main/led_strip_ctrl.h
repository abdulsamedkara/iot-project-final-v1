#pragma once
#include <stdint.h>
#include "esp_err.h"

esp_err_t led_strip_init(void);
void      led_strip_set_brightness(uint8_t brightness); // 0=kapalı, 255=tam
void      led_strip_on(void);
void      led_strip_off(void);
