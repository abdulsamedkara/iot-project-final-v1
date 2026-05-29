// smoke_sensor.c — MQ135 Duman/Gaz Sensörü
// ADC1_CH2 (GPIO3) analog okuma, ısınma süresi, gürültü filtreli ortalama

#include "smoke_sensor.h"
#include "config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "smoke";

static adc_oneshot_unit_handle_t s_adc    = NULL;
static int64_t                   s_t_init = 0;

esp_err_t smoke_sensor_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,      // 0–3.3 V aralığı
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, SMOKE_ADC_CHANNEL, &ch_cfg));

    s_t_init = esp_timer_get_time();
    ESP_LOGI(TAG, "MQ135 başlatıldı (GPIO%d). %d ms ısınma bekleniyor.",
             SMOKE_AOUT_GPIO, SMOKE_WARMUP_MS);
    return ESP_OK;
}

bool smoke_sensor_warmup_done(void)
{
    int64_t elapsed_ms = (esp_timer_get_time() - s_t_init) / 1000LL;
    return elapsed_ms >= (int64_t)SMOKE_WARMUP_MS;
}

int smoke_sensor_read_avg(void)
{
    if (!s_adc) return -1;

    int32_t sum = 0;
    for (int i = 0; i < SMOKE_SAMPLE_COUNT; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, SMOKE_ADC_CHANNEL, &raw) != ESP_OK) {
            return -1;
        }
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(10)); // Her örnek arasında 10ms bekle (Daha stabil ortalama için)
    }
    return (int)(sum / SMOKE_SAMPLE_COUNT);
}

// ─── LDR Işık Sensörü (Analog) ────────────────────────────────────────────────
esp_err_t ldr_sensor_init(void)
{
    if (!s_adc) return ESP_ERR_INVALID_STATE; // s_adc başlatılmış olmalı
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_oneshot_config_channel(s_adc, LDR_ADC_CHANNEL, &ch_cfg);
}

int ldr_sensor_read_avg(void)
{
    if (!s_adc) return -1;
    int32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, LDR_ADC_CHANNEL, &raw) != ESP_OK) return -1;
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return (int)(sum / 10);
}
