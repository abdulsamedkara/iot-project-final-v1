// Driver for controlling a 12V LED strip through an L298N motor controller.
// We manage the brightness by adjusting the PWM duty cycle on the enable pin.
// A specific timer is chosen here to prevent any hardware conflicts with the fan.

#include "led_strip_ctrl.h"
#include "config.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "led_strip";

// Prepares the LED controller hardware by setting up the timer and channel
esp_err_t led_strip_init(void)
{
    // First we configure the hardware timer that will generate the PWM waveform
    ledc_timer_config_t timer_cfg = {
        // Low speed mode is sufficient for simple LED dimming
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LED_LEDC_TIMER,
        // The resolution dictates how fine our brightness steps will be
        .duty_resolution = LED_DUTY_RES,
        // Set the frequency at which the PWM toggles
        .freq_hz         = LED_PWM_FREQ_HZ,
        // Let the system automatically choose an appropriate clock source
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // Now we bind the configured timer to a specific GPIO pin
    ledc_channel_config_t ch_cfg = {
        // The physical pin connected to the motor driver's enable input
        .gpio_num   = LED_ENB_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LED_LEDC_CHANNEL,
        .timer_sel  = LED_LEDC_TIMER,
        // Start with the LEDs completely turned off
        .duty       = 0,
        .hpoint     = 0,
        // We do not need interrupts for simple PWM generation
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "LED strip ready: ENB=GPIO%d @ %luHz", LED_ENB_GPIO, (unsigned long)LED_PWM_FREQ_HZ);
    return ESP_OK;
}

// Adjusts the light output by changing the duty cycle of the PWM signal
void led_strip_set_brightness(uint8_t brightness)
{
    // Write the new duty cycle value to the channel
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CHANNEL, brightness);
    // Apply the update so the hardware begins using the new value
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CHANNEL);
}

// Maximizes the brightness by setting the highest possible duty cycle
void led_strip_on(void)  { led_strip_set_brightness(255); }

// Removes power from the LEDs completely
void led_strip_off(void) { led_strip_set_brightness(0);   }
