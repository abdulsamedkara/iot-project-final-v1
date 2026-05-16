// i2s_mic.c — INMP441 I2S mikrofon sürücüsü
// Vize projesinden taşındı, config.h pinleriyle uyumlu.

#include "i2s_mic.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

static const char *TAG = "i2s_mic";
static i2s_chan_handle_t s_rx = NULL;

esp_err_t i2s_mic_init(void)
{
    i2s_chan_config_t ch_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    ch_cfg.dma_desc_num  = 8;
    ch_cfg.dma_frame_num = 1024;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch_cfg, NULL, &s_rx), TAG, "new_channel");

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK_GPIO,
            .ws   = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_SD_GPIO,
            .invert_flags = {false, false, false},
        },
    };
    // INMP441: L/R = GND → sol kanal
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "init_std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "enable");

    ESP_LOGI(TAG, "Mikrofon hazır: SCK=%d WS=%d SD=%d @ %dHz",
             MIC_SCK_GPIO, MIC_WS_GPIO, MIC_SD_GPIO, MIC_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t i2s_mic_read(int16_t *buf, size_t buf_samples,
                        size_t *out_samples, uint32_t timeout_ms)
{
    // INMP441 32-bit veri gönderir, MSB'de 24-bit ses, alt 8 bit 0.
    // 32-bit okuyup 16-bit'e dönüştürüyoruz.
    size_t raw_bytes = buf_samples * sizeof(int32_t);
    int32_t *raw = heap_caps_malloc(raw_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!raw) return ESP_ERR_NO_MEM;

    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(s_rx, raw, raw_bytes,
                                     &bytes_read, pdMS_TO_TICKS(timeout_ms));
    size_t frames = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < frames; i++) {
        buf[i] = (int16_t)(raw[i] >> 14);  // 32-bit MSB → 16-bit
    }
    free(raw);
    *out_samples = frames;
    return ret;
}

void i2s_mic_deinit(void)
{
    if (s_rx) {
        i2s_channel_disable(s_rx);
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
}
