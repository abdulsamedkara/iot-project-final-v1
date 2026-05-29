// fan_control.c — L298N DC Fan PWM Sürücüsü
// LEDC TIMER_1 / CHANNEL_1 — 25 kHz, 8-bit çözünürlük
// FAN_IN4_GPIO: yön (HIGH = fan ileri), FAN_ENA_GPIO: PWM hız

#include "fan_control.h"
#include "config.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "fan";

esp_err_t fan_control_init(void)
{
    // Yön pini — sabit HIGH (tek yönlü fan)
    gpio_config_t gc = {
        .pin_bit_mask = 1ULL << FAN_IN4_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gc));
    gpio_set_level(FAN_IN4_GPIO, 0);   // Başlangıçta kapalı

    // LEDC timer
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = FAN_LEDC_TIMER,
        .duty_resolution = FAN_DUTY_RES,
        .freq_hz         = FAN_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // LEDC channel
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

    ESP_LOGI(TAG, "Fan hazır: IN4=GPIO%d ENA=GPIO%d @ %luHz",
             FAN_IN4_GPIO, FAN_ENA_GPIO, (unsigned long)FAN_PWM_FREQ_HZ);
    return ESP_OK;
}

void fan_set_duty(uint8_t duty)
{
    gpio_set_level(FAN_IN4_GPIO, duty > 0 ? 1 : 0);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, FAN_LEDC_CHANNEL);
}

void fan_off(void)  { fan_set_duty(0);   }
void fan_half(void) { fan_set_duty(80);  } // Eskiden 128'di. Yüksek voltaj için %30 güç.
void fan_full(void) { fan_set_duty(160); } // Eskiden 255'ti. Yüksek voltaj için %60 güç.
