#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Sets up the speaker hardware to match the desired audio format parameters
esp_err_t i2s_player_init(uint32_t sample_rate, uint8_t bits, uint8_t channels);

// Streams a block of raw PCM data to the speaker output
esp_err_t i2s_player_play(const uint8_t *pcm, size_t pcm_len);

// Tears down the I2S channel and frees the used memory
void i2s_player_deinit(void);
