// Driver code for the MAX98357A I2S audio amplifier and speaker.
// Handles the configuration of the I2S interface for audio playback.

#include "i2s_player.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_check.h"

// Logging tag used to identify the speaker output module
static const char *TAG = "i2s_player";
// Handle to the I2S transmit channel used for playback
static i2s_chan_handle_t s_tx = NULL;

// Configure the hardware for audio playback using the provided format settings
esp_err_t i2s_player_init(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    // If the channel is already set up, clean it up before reinitializing
    if (s_tx) i2s_player_deinit();

    // Prepare the configuration for the I2S transmit channel acting as master
    i2s_chan_config_t ch_cfg = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S_PORT, I2S_ROLE_MASTER);
    // Automatically clear the DMA buffers when underflow occurs
    ch_cfg.auto_clear    = true;
    // Set up 8 DMA descriptors to manage the outgoing audio data
    ch_cfg.dma_desc_num  = 8;
    // Each descriptor holds 512 frames
    ch_cfg.dma_frame_num = 512;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch_cfg, &s_tx, NULL), TAG, "new_channel");

    // Determine the bit width based on the requested format
    i2s_data_bit_width_t bw = (bits == 32) ? I2S_DATA_BIT_WIDTH_32BIT
                                           : I2S_DATA_BIT_WIDTH_16BIT;
    // Determine the slot mode based on the number of channels
    i2s_slot_mode_t sm = (channels == 2) ? I2S_SLOT_MODE_STEREO
                                         : I2S_SLOT_MODE_MONO;

    // Apply the standard I2S protocol settings, using our customized bit width and slot mode
    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bw, sm),
        // Map the required signals to our specific GPIO pins
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = SPK_BCK_GPIO,
            .ws   = SPK_WS_GPIO,
            .dout = SPK_DIN_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };

    // Apply the standard mode configuration to the channel
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "init_std");
    // Enable the channel so it is ready to output sound
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "enable");

    ESP_LOGI(TAG, "Speaker ready: BCK=%d WS=%d DIN=%d @ %luHz %ubit %uch",
             SPK_BCK_GPIO, SPK_WS_GPIO, SPK_DIN_GPIO,
             (unsigned long)sample_rate, bits, channels);
    return ESP_OK;
}

// Send a buffer of PCM audio data to the speaker output
esp_err_t i2s_player_play(const uint8_t *pcm, size_t pcm_len)
{
    // Prevent operation if the channel has not been initialized
    if (!s_tx) return ESP_ERR_INVALID_STATE;

    size_t offset = 0;
    // We send data in small chunks to avoid stalling the system and triggering the watchdog timer
    const size_t CHUNK = 2048;
    
    // Loop until we have written the entire buffer
    while (offset < pcm_len) {
        // Calculate how much we can write in this iteration
        size_t to_write = (pcm_len - offset) < CHUNK ? (pcm_len - offset) : CHUNK;
        size_t written  = 0;
        
        // Push the chunk to the I2S hardware
        esp_err_t ret = i2s_channel_write(s_tx, pcm + offset, to_write,
                                          &written, pdMS_TO_TICKS(2000));
        // Stop completely if an error occurs during writing
        if (ret != ESP_OK) return ret;
        
        // Advance our position in the buffer by the number of bytes actually written
        offset += written;
    }
    return ESP_OK;
}

// Turn off the audio output and release the hardware resources
void i2s_player_deinit(void)
{
    // Only attempt cleanup if the channel is currently active
    if (s_tx) {
        // Stop data transmission
        i2s_channel_disable(s_tx);
        // Free the underlying channel object
        i2s_del_channel(s_tx);
        s_tx = NULL;
    }
}
