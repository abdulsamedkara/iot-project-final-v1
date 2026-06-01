// led_strip_ctrl.c — L298N Channel A üzerinden 12V şerit LED PWM kontrolü
// IN1 → 3.3V (sabit), IN2 → GND (sabit), ENB → LED_ENB_GPIO
// LEDC_TIMER_2 / LEDC_CHANNEL_2 — fan ile çakışmaz

#include "led_strip_ctrl.h"
#include "config.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "led_strip";

esp_err_t led_strip_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LED_LEDC_TIMER,
        .duty_resolution = LED_DUTY_RES,
        .freq_hz         = LED_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = LED_ENB_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LED_LEDC_CHANNEL,
        .timer_sel  = LED_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "LED şerit hazır: ENB=GPIO%d @ %luHz", LED_ENB_GPIO, (unsigned long)LED_PWM_FREQ_HZ);
    return ESP_OK;
}

void led_strip_set_brightness(uint8_t brightness)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CHANNEL, brightness);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LED_LEDC_CHANNEL);
}

void led_strip_on(void)  { led_strip_set_brightness(255); }
void led_strip_off(void) { led_strip_set_brightness(0);   }
