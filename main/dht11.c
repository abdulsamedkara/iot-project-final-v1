#include "dht11.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

// Maximum wait time in microseconds for a state change.
// Used to prevent infinite loops if the sensor stops responding.
#define TIMEOUT_US 100

// Helper function to block and wait until the given GPIO pin reaches the desired state.
// We need this to carefully measure the duration of HIGH and LOW pulses from the DHT11 sensor,
// because the pulse lengths determine whether a bit is 0 or 1.
// Returns the duration it waited in microseconds, or -1 if the timeout is reached.
static int wait_for_state(gpio_num_t pin, int state, int timeout_us) {
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(pin) != state) {
        if ((esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

// Main function to read data from the DHT11 sensor.
// The DHT11 uses a single-wire protocol, so we have to switch the pin direction
// between output (to send the start signal) and input (to read the response).
esp_err_t dht11_read(gpio_num_t pin, dht11_reading_t *data) {
    // Array to hold the 5 bytes of data sent by the sensor:
    // humidity integer, humidity decimal, temperature integer, temperature decimal, and checksum.
    uint8_t bits[5] = {0};
    
    // Set the pin to output to send the start signal to the sensor.
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 1);
    
    // Let the line remain idle in the HIGH state for a short time before starting.
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Pull the line LOW for at least 18 milliseconds to wake up the DHT11 and signal it to send data.
    gpio_set_level(pin, 0);
    
    // We use vTaskDelay here instead of ets_delay_us so we don't lock up the CPU for 20ms.
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Disable interrupts to ensure our timing isn't messed up by the OS scheduling.
    // The DHT11 timings are in the order of microseconds, so any interruption would corrupt the read.
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);
    
    // Release the line by pulling it HIGH and wait for the sensor to pull it LOW as an acknowledgment.
    gpio_set_level(pin, 1);
    ets_delay_us(30);
    
    // Switch the pin to input mode so we can read the sensor's response.
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    // Wait for the sensor to pull the line LOW, then HIGH, then LOW again.
    // This sequence indicates the sensor is ready to start transmitting data.
    // We use timeouts to avoid hanging the system if the sensor is disconnected.
    if (wait_for_state(pin, 0, 1000) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }
    if (wait_for_state(pin, 1, 200) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }
    if (wait_for_state(pin, 0, 200) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }

    // Read the 40 bits (5 bytes) of data from the sensor.
    for (int i = 0; i < 40; i++) {
        // Wait for the pin to go HIGH. This indicates the start of a new bit transmission.
        if (wait_for_state(pin, 1, 200) == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }
        
        // Measure how long the pin stays HIGH.
        // A short HIGH duration (26-28us) means the bit is '0'.
        // A long HIGH duration (70us) means the bit is '1'.
        int high_time = wait_for_state(pin, 0, 200);
        if (high_time == -1) { portEXIT_CRITICAL(&mux); return ESP_ERR_TIMEOUT; }

        // Determine which byte we are currently writing to.
        int byte_idx = i / 8;
        
        // Shift the current byte left by 1 to make room for the new bit.
        bits[byte_idx] <<= 1;
        
        // If the HIGH pulse was longer than 40us, we consider it a '1' and set the least significant bit.
        if (high_time > 40) {
            bits[byte_idx] |= 1;
        }
    }
    
    // We have finished reading the timing-critical data, so we can re-enable interrupts.
    portEXIT_CRITICAL(&mux);

    // Verify the data integrity using the checksum (the 5th byte).
    // The checksum must equal the sum of the first 4 bytes.
    if (bits[4] != ((bits[0] + bits[1] + bits[2] + bits[3]) & 0xFF)) {
        return ESP_ERR_INVALID_CRC;
    }

    // Populate the return structure with the parsed integer values.
    // We only care about the integer parts (byte 0 and byte 2) for this application.
    data->humidity = bits[0];
    data->temperature = bits[2];
    
    return ESP_OK;
}
