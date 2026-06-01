#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

// Struct to hold the temperature and humidity readings from the DHT11 sensor.
// We use this to return both values from the read function cleanly.
typedef struct {
    int temperature;
    int humidity;
} dht11_reading_t;

// Reads temperature and humidity from the DHT11 sensor.
// It takes the GPIO pin where the DHT11 is connected and a pointer to the data struct.
// Returns ESP_OK(0) on success, otherwise an error code indicating the failure reason.
esp_err_t dht11_read(gpio_num_t pin, dht11_reading_t *data);
