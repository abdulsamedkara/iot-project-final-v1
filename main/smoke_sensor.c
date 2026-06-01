// MQ135 Smoke/Gas Sensor and LDR Sensor Driver
// Utilizes analog readings, enforces warmup times, and applies averaging for noise filtration.

#include "smoke_sensor.h"
#include "config.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Tag used for ESP logging to identify messages related to the smoke sensor.
static const char *TAG = "smoke";

// Handle for the ADC one-shot unit used to read analog values.
static adc_oneshot_unit_handle_t s_adc    = NULL;

// Timestamp of when the sensor was initialized, used to calculate the warmup period.
static int64_t                   s_t_init = 0;

// Initializes the ADC unit and configures the specific channel for the smoke sensor.
// This sets up the hardware so we can read the analog voltage produced by the MQ135.
esp_err_t smoke_sensor_init(void)
{
    // Create and configure a new ADC one-shot unit.
    // We disable the ULP (Ultra Low Power) mode because we don't need it here.
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    // Configure the specific channel for the smoke sensor.
    // We use 11dB attenuation to allow measuring voltages up to roughly 3.3V,
    // and set the resolution to the default 12 bits (0-4095 range).
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, SMOKE_ADC_CHANNEL, &ch_cfg));

    // Record the current time so we know when the warmup period started.
    s_t_init = esp_timer_get_time();
    
    ESP_LOGI(TAG, "MQ135 initialized (GPIO%d). Waiting %d ms for warmup.",
             SMOKE_AOUT_GPIO, SMOKE_WARMUP_MS);
    return ESP_OK;
}

// Checks if the MQ135 sensor has been powered on long enough to provide stable readings.
// The chemical sensor elements need time to heat up and stabilize.
bool smoke_sensor_warmup_done(void)
{
    // Calculate how many milliseconds have passed since initialization.
    int64_t elapsed_ms = (esp_timer_get_time() - s_t_init) / 1000LL;
    return elapsed_ms >= (int64_t)SMOKE_WARMUP_MS;
}

// Reads multiple samples from the smoke sensor and returns their average.
// Averaging helps eliminate random electrical noise and brief spikes in the sensor's output.
int smoke_sensor_read_avg(void)
{
    // Make sure the ADC has been initialized before trying to read.
    if (!s_adc) return -1;

    int32_t sum = 0;
    
    // Take multiple readings defined by SMOKE_SAMPLE_COUNT.
    for (int i = 0; i < SMOKE_SAMPLE_COUNT; i++) {
        int raw = 0;
        
        // Perform a single ADC read operation.
        if (adc_oneshot_read(s_adc, SMOKE_ADC_CHANNEL, &raw) != ESP_OK) {
            return -1;
        }
        
        sum += raw;
        
        // Delay 10 milliseconds between samples to get a more diverse and stable average.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Return the calculated average.
    return (int)(sum / SMOKE_SAMPLE_COUNT);
}

// LDR Light Sensor Functions

// Initializes the ADC channel for the LDR (Light Dependent Resistor) light sensor.
// It shares the same ADC unit as the smoke sensor.
esp_err_t ldr_sensor_init(void)
{
    // The ADC unit must be created by smoke_sensor_init() first.
    if (!s_adc) return ESP_ERR_INVALID_STATE;
    
    // Configure the LDR channel with standard settings (11dB attenuation, 12-bit width).
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    return adc_oneshot_config_channel(s_adc, LDR_ADC_CHANNEL, &ch_cfg);
}

// Reads multiple samples from the LDR sensor and calculates the average.
// Similar to the smoke sensor, this smooths out minor fluctuations in the light reading.
int ldr_sensor_read_avg(void)
{
    if (!s_adc) return -1;
    
    int32_t sum = 0;
    
    // Hardcoded to take 10 samples for the LDR.
    for (int i = 0; i < 10; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, LDR_ADC_CHANNEL, &raw) != ESP_OK) return -1;
        sum += raw;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    return (int)(sum / 10);
}
