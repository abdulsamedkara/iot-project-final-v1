#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
    int temperature;
    int humidity;
} dht11_reading_t;

/**
 * @brief DHT11 sensöründen sıcaklık ve nem okur.
 * 
 * @param pin DHT11'in bağlı olduğu GPIO pini
 * @param data Okunan verilerin yazılacağı struct
 * @return esp_err_t ESP_OK(0) başarılı, aksi halde hata kodu
 */
esp_err_t dht11_read(gpio_num_t pin, dht11_reading_t *data);
