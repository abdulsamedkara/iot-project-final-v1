#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Set up the I2S peripheral and DMA buffers for the microphone
esp_err_t i2s_mic_init(void);

// Read audio samples from the microphone
// buf is the destination array for the 16-bit audio data
// buf_samples is the maximum number of samples we can store
// out_samples returns how many samples were actually read
// timeout_ms limits how long we will wait for data before returning
esp_err_t i2s_mic_read(int16_t *buf, size_t buf_samples,
                        size_t *out_samples, uint32_t timeout_ms);

// Shut down the I2S channel and free up its resources
void i2s_mic_deinit(void);
