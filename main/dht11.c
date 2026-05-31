#include "dht11.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

#define TIMEOUT_US 100

static int wait_for_state(gpio_num_t pin, int state, int timeout_us) {
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) != state) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

esp_err_t dht11_read(gpio_num_t pin, dht11_reading_t *data) {
    uint8_t bits[5] = {0};
    
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
    vTaskDelay(pdMS_TO_TICKS(10)); // Hat boşta kalsın
    
    // Start sinyali: MCU hattı en az 18ms LOW'a çeker
    gpio_set_level(pin, 0);
    // ets_delay_us(20000) yerine vTaskDelay kullanarak CPU'yu kilitlemeyi bırakıyoruz!
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Zamanlaması kritik bölüm başlıyor, diğer task'ların bizi bölmesini engelliyoruz
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    gpio_set_level(pin, 1);
    ets_delay_us(30);
    
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    // İlk LOW'a çekme süresi bazen sensör uyanırken uzun sürebilir
    if (wait_for_state(pin, 0, 1000) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }
    if (wait_for_state(pin, 1, 200) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }
    if (wait_for_state(pin, 0, 200) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }

    for (int i = 0; i < 40; i++) {
        if (wait_for_state(pin, 1, 200) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }
        
        int high_time = wait_for_state(pin, 0, 200);
        if (high_time == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }

        int byte_idx = i / 8;
        bits[byte_idx] <<= 1;
        if (high_time > 40) {
            bits[byte_idx] |= 1;
        }
    }
    
    // Kritik bölüm bitti
    portEXIT_CRITICAL(&mux);

    if (bits[4] != ((bits[0] + bits[1] + bits[2] + bits[3]) & 0xFF)) {
        return ESP_ERR_INVALID_CRC;
    }

    data->humidity = bits[0];
    data->temperature = bits[2];
    
    return ESP_OK;
}
