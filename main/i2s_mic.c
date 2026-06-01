// Driver implementation for the INMP441 I2S microphone.
// This module handles initialization, reading raw audio data, and cleanup.

#include "i2s_mic.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

// Logging tag used to identify microphone messages
static const char *TAG = "i2s_mic";
// Handle for the I2S receive channel
static i2s_chan_handle_t s_rx = NULL;

// Initialize the I2S microphone by setting up the DMA and GPIO pins
esp_err_t i2s_mic_init(void)
{
    // Configure the I2S channel to act as the master
    i2s_chan_config_t ch_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    // Allocate 8 DMA descriptors to handle incoming audio chunks
    ch_cfg.dma_desc_num  = 8;
    // Set the frame size for each DMA descriptor
    ch_cfg.dma_frame_num = 1024;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch_cfg, NULL, &s_rx), TAG, "new_channel");

    // Configure the standard I2S protocol settings for the microphone
    i2s_std_config_t std = {
        // Set the sample rate based on the project configuration
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        // The INMP441 uses a 32-bit slot width in mono mode
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        // Map the I2S signals to the correct GPIO pins
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK_GPIO,
            .ws   = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_SD_GPIO,
            .invert_flags = {false, false, false},
        },
    };
    
    // We connect the L/R pin of the INMP441 to ground, which places the data on the left channel
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    // Apply the standard mode configuration to the channel
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "init_std");
    // Enable the channel so it starts receiving data
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "enable");

    ESP_LOGI(TAG, "Microphone ready: SCK=%d WS=%d SD=%d @ %dHz",
             MIC_SCK_GPIO, MIC_WS_GPIO, MIC_SD_GPIO, MIC_SAMPLE_RATE);
    return ESP_OK;
}

// Read raw audio samples from the microphone into a provided buffer
esp_err_t i2s_mic_read(int16_t *buf, size_t buf_samples,
                        size_t *out_samples, uint32_t timeout_ms)
{
    // The INMP441 microphone outputs 32-bit words, but the actual audio data is 24-bit aligned to the MSB.
    // We allocate a temporary buffer to read the 32-bit data before converting it down to 16-bit.
    size_t raw_bytes = buf_samples * sizeof(int32_t);
    int32_t *raw = heap_caps_malloc(raw_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!raw) return ESP_ERR_NO_MEM;

    size_t bytes_read = 0;
    // Attempt to read the requested amount of data from the I2S channel within the given timeout
    esp_err_t ret = i2s_channel_read(s_rx, raw, raw_bytes,
                                     &bytes_read, pdMS_TO_TICKS(timeout_ms));
    
    // Calculate how many complete frames were actually read
    size_t frames = bytes_read / sizeof(int32_t);
    
    // Shift the 32-bit values down to 16-bit to match our desired output format
    for (size_t i = 0; i < frames; i++) {
        buf[i] = (int16_t)(raw[i] >> 14);
    }
    
    // Free the temporary 32-bit buffer
    free(raw);
    
    // Report back the number of 16-bit samples successfully processed
    *out_samples = frames;
    return ret;
}

// Stop the microphone and release any system resources it is using
void i2s_mic_deinit(void)
{
    // Only attempt to clean up if the channel is currently initialized
    if (s_rx) {
        // Disable the channel to stop incoming data
        i2s_channel_disable(s_rx);
        // Delete the channel to free up memory and I2S hardware
        i2s_del_channel(s_rx);
        s_rx = NULL;
    }
}
