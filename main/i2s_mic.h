#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t i2s_mic_init(void);
esp_err_t i2s_mic_read(int16_t *buf, size_t buf_samples,
                        size_t *out_samples, uint32_t timeout_ms);
void i2s_mic_deinit(void);
