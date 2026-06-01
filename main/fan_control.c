// L298N DC Fan PWM Driver
// LEDC TIMER_1 / CHANNEL_1
// 25 kHz, 8-bit resolution
// FAN_IN4_GPIO controls direction (HIGH means fan moves forward)
// FAN_ENA_GPIO controls the PWM speed

#include "fan_control.h"
#include "config.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Tag used for ESP logging to identify messages from the fan controller.
static const char *TAG = "fan";

// Configures the GPIO and LEDC timer/channel required to control the fan.
// We use the ESP32's LEDC peripheral to generate a hardware PWM signal
// so the CPU doesn't have to manually toggle the pin.
esp_err_t fan_control_init(void)
{
    // Set up the direction pin for the motor driver.
    // We configure it as an output without pull-up or pull-down resistors.
    gpio_config_t gc = {
        .pin_bit_mask = 1ULL << FAN_IN4_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gc));
    
    // Start with the fan turned off by default.
    gpio_set_level(FAN_IN4_GPIO, 0);

    // Configure the LEDC timer.
    // We set the frequency and resolution for the PWM signal.
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = FAN_LEDC_TIMER,
        .duty_resolution = FAN_DUTY_RES,
        .freq_hz         = FAN_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // Configure the LEDC channel and link it to the timer we just created.
    // This attaches the PWM signal to the specific GPIO pin connected to the motor driver.
    ledc_channel_config_t ch_cfg = {
        .gpio_num   = FAN_ENA_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = FAN_LEDC_CHANNEL,
        .timer_sel  = FAN_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "Fan ready: IN4=GPIO%d ENA=GPIO%d @ %luHz",
             FAN_IN4_GPIO, FAN_ENA_GPIO, (unsigned long)FAN_PWM_FREQ_HZ);
    return ESP_OK;
}

// Adjusts the fan speed by updating the PWM duty cycle.
// Also ensures the direction pin is HIGH if the fan should be spinning.
void fan_set_duty(uint8_t duty)
{
    // If duty is greater than 0, enable the direction pin so the motor can turn.
    // Otherwise, disable it to ensure the motor stops completely.
    gpio_set_level(FAN_IN4_GPIO, duty > 0 ? 1 : 0);
    
    // Apply the new duty cycle to the LEDC hardware.
    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL);
}

// Helper functions for common fan speeds.

// Stops the fan completely.
void fan_off(void)  { fan_set_duty(0);   }

// Runs the fan at partial capacity (roughly 30% power for high voltage setups).
void fan_half(void) { fan_set_duty(80);  }

// Runs the fan at a higher capacity (roughly 60% power for high voltage setups).
void fan_full(void) { fan_set_duty(160); }
