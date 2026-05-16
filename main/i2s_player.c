// i2s_player.c — MAX98357A I2S amfi/hoparlör sürücüsü
// Vize projesinden taşındı, config.h pinleriyle uyumlu.

#include "i2s_player.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "i2s_player";
static i2s_chan_handle_t s_tx = NULL;

esp_err_t i2s_player_init(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    if (s_tx) i2s_player_deinit();

    i2s_chan_config_t ch_cfg = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S_PORT, I2S_ROLE_MASTER);
    ch_cfg.auto_clear    = true;
    ch_cfg.dma_desc_num  = 8;
    ch_cfg.dma_frame_num = 512;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch_cfg, &s_tx, NULL), TAG, "new_channel");

    i2s_data_bit_width_t bw = (bits == 32) ? I2S_DATA_BIT_WIDTH_32BIT
                                           : I2S_DATA_BIT_WIDTH_16BIT;
    i2s_slot_mode_t sm = (channels == 2) ? I2S_SLOT_MODE_STEREO
                                         : I2S_SLOT_MODE_MONO;

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bw, sm),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCK_GPIO,
            .ws   = SPK_WS_GPIO,
            .dout = SPK_DIN_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "init_std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");

    ESP_LOGI(TAG, "Hoparlör hazır: BCK=%d WS=%d DIN=%d @ %luHz %ubit %uch",
             SPK_BCK_GPIO, SPK_WS_GPIO, SPK_DIN_GPIO,
             (unsigned long)sample_rate, bits, channels);
    return ESP_OK;
}

esp_err_t i2s_player_play(const uint8_t *pcm, size_t pcm_len)
{
    if (!s_tx) return ESP_ERR_INVALID_STATE;

    size_t offset = 0;
    const size_t CHUNK = 2048;
    while (offset < pcm_len) {
        size_t to_write = (pcm_len - offset) < CHUNK ? (pcm_len - offset) : CHUNK;
        size_t written  = 0;
        esp_err_t ret = i2s_channel_write(s_tx, pcm + offset, to_write,
                                          &written, pdMS_TO_TICKS(2000));
        if (ret != ESP_OK) return ret;
        offset += written;
    }
    return ESP_OK;
}

void i2s_player_deinit(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
}
