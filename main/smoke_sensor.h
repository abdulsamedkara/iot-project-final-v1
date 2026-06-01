#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Initializes the ADC to read values from the MQ135 smoke/gas sensor.
// The sensor requires a warmup period before its readings can be trusted.
// Returns ESP_OK on success, or an error code if initialization fails.
esp_err_t smoke_sensor_init(void);

// Takes multiple analog measurements from the smoke sensor and averages them.
// This helps filter out electrical noise and small fluctuations in the sensor.
// Returns the averaged ADC value (0-4095), or -1 if a read error occurs.
int smoke_sensor_read_avg(void);

// LDR Light Sensor functions
// Initializes the ADC channel specifically for the Light Dependent Resistor.
esp_err_t ldr_sensor_init(void);

// Reads multiple samples from the LDR and returns the average value to smooth out the data.
int ldr_sensor_read_avg(void);

// Checks if the mandatory warmup time for the MQ135 sensor has passed.
// The sensor heater needs time to reach operating temperature for accurate readings.
// Returns true if the sensor is ready, false if it is still warming up.
bool smoke_sensor_warmup_done(void);
