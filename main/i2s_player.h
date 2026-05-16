#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t i2s_player_init(uint32_t sample_rate, uint8_t bits, uint8_t channels);
esp_err_t i2s_player_play(const uint8_t *pcm, size_t pcm_len);
void i2s_player_deinit(void);
