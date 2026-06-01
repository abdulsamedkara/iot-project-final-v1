// SmartLab Assistant Main Application (Phase 1)
// Handles WiFi connection, WebSocket communication, AES-256 encrypted sessions,
// RFID reading, PTT audio recording, AI response processing, and audio playback.
//
// Configuration parameters such as WIFI_SSID and SERVER_HOST should be edited in config.h

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"

#include "config.h"
#include "i2s_mic.h"
#include "i2s_player.h"
#include "crypto.h"
#include "ws_client.h"
#include "rfid.h"
#include "smoke_sensor.h"
#include "fan_control.h"
#include "led_strip_ctrl.h"
#include "dht11.h"
#include "ui/display.h"

static const char *TAG = "main";

// Event group bits for WiFi connection status
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

// Event group handle used to track WiFi events
static EventGroupHandle_t s_wifi_eg;
// Retry counter for WiFi connection attempts
static int s_retry = 0;

// Callback function to handle WiFi and IP events
static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // Start connection when WiFi station is initialized
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // Attempt to reconnect if disconnected, up to WIFI_MAX_RETRY times
        if (s_retry < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGW(TAG, "Reconnecting to WiFi (%d/%d)...", s_retry, WIFI_MAX_RETRY);
        } else {
            // Signal failure if maximum retries reached
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        // Connection successful, IP obtained
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

// Initializes the WiFi in station mode and connects to the configured AP
static esp_err_t wifi_init(void)
{
    s_wifi_eg = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t eh_wifi, eh_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &eh_wifi));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &eh_ip));

    wifi_config_t wc = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    // Disable power save mode to prevent disconnections during WebSocket and audio streaming tasks
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Block until connection is established or maximum retries are reached
    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

// Initializes the SPI bus which is shared between the MFRC522 RFID reader and ILI9341 display
static void spi_bus_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num     = SPI_MOSI_GPIO,
        .miso_io_num     = SPI_MISO_GPIO,
        .sclk_io_num     = SPI_SCK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        // The maximum transfer size allows for LVGL buffer updates
        .max_transfer_sz = TFT_WIDTH * LVGL_BUF_LINES * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus ready: MOSI=%d MISO=%d SCK=%d",
             SPI_MOSI_GPIO, SPI_MISO_GPIO, SPI_SCK_GPIO);
}

// Configures the Push-To-Talk (PTT) button GPIO pin
static void ptt_init(void)
{
    gpio_config_t gc = {
        .pin_bit_mask = 1ULL << PTT_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&gc);
}

// Checks if the PTT button is currently pressed (active low)
static inline bool ptt_pressed(void) { return gpio_get_level(PTT_GPIO) == 0; }

// Independent FreeRTOS task running on Core 0 to monitor the smoke sensor
// Checks the sensor every 500 ms and triggers an alarm if thresholds are exceeded
static void smoke_task(void *arg)
{
    static const char *TAG2 = "smoke_task";

    // Wait for the analog sensor to warm up and stabilize
    ESP_LOGI(TAG2, "Waiting for sensor warmup (%d ms)...", SMOKE_WARMUP_MS);
    while (!smoke_sensor_warmup_done()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG2, "Warmup complete. Starting measurement.");

    int  alert_count = 0;
    bool in_alert    = false;

    while (1) {
        int adc = smoke_sensor_read_avg();

        if (adc < 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        ESP_LOGI(TAG2, "ADC=%d", adc);

        // Control fan speed based on smoke concentration
        // Overrides temperature-based fan control when smoke is detected
        if (adc >= SMOKE_ADC_FULL) {
            fan_full();
        } else if (adc >= SMOKE_ADC_HALF) {
            fan_half();
        } else if (adc >= SMOKE_ADC_CLEAR) {
            fan_half();
        }
        // If adc < SMOKE_ADC_CLEAR, leave fan control to the sensor_broadcast_task

        // Debounce logic for the alarm: requires 3 consecutive high readings to trigger
        if (adc >= SMOKE_ADC_HALF) {
            if (++alert_count >= 3 && !in_alert) {
                in_alert = true;
                ESP_LOGW(TAG2, "SMOKE ALARM! ADC=%d", adc);
                char msg[32];
                snprintf(msg, sizeof(msg), "ADC: %d", adc);
                display_switch(SCREEN_SMOKE_ALERT, msg);
            }
        } else {
            if (alert_count > 0) alert_count--;
            // Clear the alarm if smoke levels drop below the clear threshold
            if (in_alert && adc < SMOKE_ADC_CLEAR) {
                in_alert = false;
                ESP_LOGI(TAG2, "Smoke decreased. ADC=%d", adc);
                display_switch(SCREEN_READY, NULL);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Shared variables for DHT11 sensor data
// Written by dht11_task and read by sensor_broadcast_task
static volatile int s_dht_temp = 0;
static volatile int s_dht_hum  = 0;

// Task to read temperature and humidity from the DHT11 sensor periodically
static void dht11_task(void *arg)
{
    static const char *TAG3 = "dht11_task";
    dht11_reading_t dht_data;

    ESP_LOGI(TAG3, "Starting DHT11 reading...");

    while (1) {
        esp_err_t res = dht11_read(DHT11_GPIO, &dht_data);
        if (res == ESP_OK) {
            s_dht_temp = dht_data.temperature;
            s_dht_hum  = dht_data.humidity;
            ESP_LOGI(TAG3, "Temperature: %dC, Humidity: %%%d", dht_data.temperature, dht_data.humidity);
        } else {
            ESP_LOGW(TAG3, "DHT11 read failed (error: %d)", res);
        }
        vTaskDelay(pdMS_TO_TICKS(DHT11_UPDATE_MS));
    }
}

// LED operation mode flag: 0 indicates manual mode via web UI, 1 indicates automatic mode via LDR
static volatile int s_led_auto = 1;

// Task to read the Light Dependent Resistor (LDR) analog value and control the LED strip brightness
static void ldr_task(void *arg)
{
    static const char *TAG_LDR = "ldr_task";
    
    ESP_LOGI(TAG_LDR, "Starting LDR reading (Analog GPIO %d)...", LDR_AOUT_GPIO);

    while (1) {
        int ldr_adc = ldr_sensor_read_avg();
        if (ldr_adc >= 0) {
            ESP_LOGI(TAG_LDR, "LDR ADC = %d", ldr_adc);
            // Adjust LED brightness automatically if auto mode is enabled
            if (s_led_auto) {
                if (ldr_adc <= LDR_LED_DARK_ADC) {
                    // Maximum brightness in dark conditions
                    led_strip_set_brightness(255);
                } else if (ldr_adc >= LDR_LED_BRIGHT_ADC) {
                    // Turn off LED in bright conditions
                    led_strip_off();
                } else {
                    // Linearly interpolate brightness for intermediate light levels
                    // LDR_LED_DARK_ADC maps to full brightness (255)
                    // LDR_LED_BRIGHT_ADC maps to off (0)
                    int range = LDR_LED_BRIGHT_ADC - LDR_LED_DARK_ADC;
                    int adc_diff = ldr_adc - LDR_LED_DARK_ADC;
                    uint8_t brightness = (uint8_t)(255 - (adc_diff * 255 / range));
                    led_strip_set_brightness(brightness);
                }
            }
        } else {
            ESP_LOGW(TAG_LDR, "LDR read error!");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Task to monitor the PIR (Passive Infrared) motion sensor
static void pir_task(void *arg)
{
    static const char *TAG_PIR = "pir_task";
    
    // Configure the PIR sensor data pin as input
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .mode = GPIO_MODE_INPUT,
        // Most PIR modules (e.g., HC-SR501) have their own pull-down resistor
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = -1;
    ESP_LOGI(TAG_PIR, "Listening to PIR motion sensor (GPIO %d)...", PIR_GPIO);

    while (1) {
        int current_state = gpio_get_level(PIR_GPIO);
        
        // Log state changes when motion starts or stops
        if (current_state != last_state) {
            if (current_state == 1) {
                ESP_LOGI(TAG_PIR, "ALARM: Motion detected!");
            } else {
                ESP_LOGI(TAG_PIR, "Motion ended, environment is calm.");
            }
            last_state = current_state;
        }
        
        // Check the sensor state every 500 ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Task to monitor the flame sensor for fire detection
static void flame_task(void *arg)
{
    static const char *TAG_FLAME = "flame_task";
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FLAME_GPIO),
        .mode = GPIO_MODE_INPUT,
        // Enable pull-up to keep the sensor DO (Digital Out) signal stable
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = -1;
    ESP_LOGI(TAG_FLAME, "Listening to flame sensor (GPIO %d)...", FLAME_GPIO);

    while (1) {
        int current_state = gpio_get_level(FLAME_GPIO);
        
        if (current_state != last_state) {
            // Most flame modules output 0 (LOW) when a flame is detected, and normally 1 (HIGH).
            if (current_state == 0) {
                ESP_LOGE(TAG_FLAME, "FIRE ALARM: Flame detected!");
            } else {
                ESP_LOGI(TAG_FLAME, "Flame ended, environment is safe.");
            }
            last_state = current_state;
        }
        
        // Check the sensor state every 500 ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Task to monitor the vibration sensor for physical shocks or movements
static void vib_task(void *arg)
{
    static const char *TAG_VIB = "vib_task";
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VIB_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = -1;
    ESP_LOGI(TAG_VIB, "Listening to vibration sensor (GPIO %d)...", VIB_GPIO);

    while (1) {
        int current_state = gpio_get_level(VIB_GPIO);
        
        if (current_state != last_state) {
            // SW-420 rapidly switches between 1 and 0 when vibration is detected
            if (current_state == 1) {
                ESP_LOGE(TAG_VIB, "WARNING: Vibration / Shock detected!");
            }
            last_state = current_state;
        }
        
        // Read slightly faster at 100ms since vibration events are instantaneous
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Sensor Broadcast Task to send telemetry to the WebSocket server

// Fan mode: 0 for manual control via UI, 1 for automatic control based on temperature
static volatile int  s_fan_mode  = 1;   // Auto at startup
static volatile int  s_fan_speed = 0;   // Manual mode speed (0-100)

// Periodically collects data from all sensors and broadcasts it via WebSocket
// Fast sensors (vibration, PIR, flame) are checked for state changes every 50ms for instant alerts
// Slow sensors (temperature, humidity, smoke, LDR) are updated every 1s
static void sensor_broadcast_task(void *arg)
{
    static const char *TAG_SB = "sensor_bcast";

    // Cache for slow sensor readings
    int      s_temp  = 0, s_hum = 0, s_smoke = -1, s_ldr = -1;
    int      s_pir   = -1, s_flame = -1, s_vib = -1;   // Previous states for fast sensors
    uint32_t slow_tick = 0;  // 1000ms / 50ms = update slow sensors every 20 loops

    while (1) {
        if (!ws_client_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Fast sensor check: execute every 50ms
        int pir   = gpio_get_level(PIR_GPIO);
        int flame = gpio_get_level(FLAME_GPIO);
        int vib   = gpio_get_level(VIB_GPIO);

        bool fast_changed = (pir != s_pir) || (flame != s_flame) || (vib != s_vib);
        s_pir = pir; s_flame = flame; s_vib = vib;

        // Slow sensor check: execute every 1s (20 loops of 50ms)
        bool slow_update = false;
        if (++slow_tick >= 20) {
            slow_tick = 0;
            slow_update = true;

            int smoke = smoke_sensor_read_avg();
            int ldr   = ldr_sensor_read_avg();
            
            // Note: dht11_read uses vTaskSuspendAll, which disrupts RFID polling
            // so we read from cached variables updated by dht11_task
            s_temp  = s_dht_temp;
            s_hum   = s_dht_hum;
            s_smoke = (smoke >= 0) ? smoke : -1;
            s_ldr   = (ldr   >= 0) ? ldr   : -1;

            // Temperature-based auto-fan control (active only in auto mode)
            // Does not override smoke_task emergency fan control
            if (s_fan_mode == 1 && s_smoke < SMOKE_ADC_HALF) {
                if (s_temp >= 27) {
                    uint8_t duty = (s_temp >= 35) ? 255 : (uint8_t)((s_temp - 27) * 32);
                    fan_set_duty(duty);
                } else if (s_temp < 25) {
                    fan_off();
                }
            }
        }

        // Broadcast data if there is a fast state change OR it's time for a 1s update
        if (fast_changed || slow_update) {
            char json[300];
            snprintf(json, sizeof(json),
                "{\"type\":\"sensors\","
                "\"temperature\":%d,\"humidity\":%d,"
                "\"smoke\":%d,\"ldr\":%d,"
                "\"pir\":%d,\"flame\":%d,\"vib\":%d,"
                "\"fan_mode\":%d,\"fan_speed\":%d,"
                "\"led_mode\":%d}",
                s_temp, s_hum, s_smoke, s_ldr,
                pir, flame, vib,
                s_fan_mode, s_fan_speed,
                s_led_auto);

            ws_client_send_text(json);

            if (fast_changed) {
                ESP_LOGI(TAG_SB, "Fast change: pir=%d flame=%d vib=%d", pir, flame, vib);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// WebSocket incoming message callbacks
static volatile bool    s_rsp_ready = false;
static const uint8_t   *s_rsp_pcm   = NULL;
static volatile size_t  s_rsp_len   = 0;

// Callback for receiving PCM audio data from the server
static void on_audio(const uint8_t *pcm, size_t len)
{
    s_rsp_pcm   = pcm;
    s_rsp_len   = len;
    s_rsp_ready = true;
}

// Buffer to store the current username obtained from the RFID server response
static char s_username[64] = {0};
// Flag indicating a logout request from the server
static volatile bool s_logout_req = false;

// Callback for receiving text messages (JSON) from the server
static void on_text(const char *json, size_t len)
{
    static const char *TAG_T = "on_text";

    // Handle logout command: {"type":"logout"}
    if (strstr(json, "\"type\":\"logout\"")) {
        s_logout_req = true;
        ESP_LOGI(TAG_T, "Logout command received");
        return;
    }

    // Handle user info command: {"type":"user","name":"John Doe"}
    const char *p = strstr(json, "\"name\":\"");
    if (p) {
        p += 8;
        const char *end = strchr(p, '"');
        if (end) {
            size_t n = (size_t)(end - p);
            if (n >= sizeof(s_username)) n = sizeof(s_username) - 1;
            memcpy(s_username, p, n);
            s_username[n] = '\0';
            ESP_LOGI(TAG_T, "User: %s", s_username);
        }
    }

    // Handle LED strip commands: {"type":"led","on":true/false,"brightness":75,"mode":"manual"/"auto"}
    if (strstr(json, "\"type\":\"led\"")) {
        if (strstr(json, "\"mode\":\"auto\"")) {
            s_led_auto = 1;
            ESP_LOGI(TAG_T, "LED mode: automatic (LDR)");
        } else {
            // Switch to manual mode when explicit command is received from the web UI
            s_led_auto = 0;
            if (strstr(json, "\"on\":false")) {
                led_strip_off();
                ESP_LOGI(TAG_T, "LED off (manual)");
            } else {
                const char *bp = strstr(json, "\"brightness\":");
                if (bp) {
                    int pct = atoi(bp + 13);
                    if (pct < 0)   pct = 0;
                    if (pct > 100) pct = 100;
                    led_strip_set_brightness((uint8_t)(pct * 255 / 100));
                    ESP_LOGI(TAG_T, "LED brightness %d%% (manual)", pct);
                } else {
                    led_strip_on();
                    ESP_LOGI(TAG_T, "LED on (manual)");
                }
            }
        }
    }

    // Handle Fan commands: {"type":"fan","on":true/false,"speed":75,"mode":"manual"/"auto"}
    if (strstr(json, "\"type\":\"fan\"")) {
        // Update fan control mode
        if (strstr(json, "\"mode\":\"auto\""))   s_fan_mode = 1;
        if (strstr(json, "\"mode\":\"manual\"")) s_fan_mode = 0;

        if (s_fan_mode == 0) {
            // Manual control: apply on/off state and speed
            bool fan_on = strstr(json, "\"on\":true") != NULL;
            // Parse speed percentage
            const char *sp = strstr(json, "\"speed\":");
            if (sp) {
                s_fan_speed = atoi(sp + 8);
                if (s_fan_speed < 0)   s_fan_speed = 0;
                if (s_fan_speed > 100) s_fan_speed = 100;
            }
            if (!fan_on) {
                fan_off();
                ESP_LOGI(TAG_T, "Fan off (manual)");
            } else {
                fan_set_duty((uint8_t)(s_fan_speed * 255 / 100));
                ESP_LOGI(TAG_T, "Fan on (manual) %d%%", s_fan_speed);
            }
        }
    }
}

// Main entry point for the SmartLab Assistant application
void app_main(void)
{
    // 1. Initialize NVS (Non-Volatile Storage)
    // Used by WiFi to store connection settings
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack and default event loop
    // Must be done before WiFi and LVGL initialization
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Initialize the shared SPI Bus for TFT and RFID modules
    spi_bus_init();

    // 3. Initialize Display and show startup message
    ESP_ERROR_CHECK(display_init());
    display_switch(SCREEN_IDLE, "Initializing...");

    // 4. Initialize WiFi and attempt to connect
    display_switch(SCREEN_IDLE, "Connecting to WiFi...");
    if (wifi_init() != ESP_OK) {
        display_switch(SCREEN_ERROR, "WiFi connection error!");
        ESP_LOGE(TAG, "Failed to start WiFi.");
        return;
    }

    // 5. Initialize I2S for microphone (input) and speaker (output)
    ESP_ERROR_CHECK(i2s_mic_init());
    ESP_ERROR_CHECK(i2s_player_init(SPK_SAMPLE_RATE, 16, 1));

    // 6. Initialize Push-To-Talk button
    ptt_init();

    // 7. Initialize MFRC522 RFID reader
    ESP_ERROR_CHECK(rfid_init());

    // Initialize environmental sensors, fan control, and LED strip
    ESP_ERROR_CHECK(smoke_sensor_init());
    ESP_ERROR_CHECK(ldr_sensor_init());
    ESP_ERROR_CHECK(fan_control_init());
    ESP_ERROR_CHECK(led_strip_init());

    // Start background tasks for each sensor
    xTaskCreatePinnedToCore(smoke_task, "smoke", 3072, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(dht11_task, "dht11", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(ldr_task, "ldr", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(pir_task, "pir", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(flame_task, "flame", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(vib_task, "vib", 4096, NULL, 3, NULL, 0);

    // Start the broadcast task to periodically send sensor data to the web UI
    xTaskCreatePinnedToCore(sensor_broadcast_task, "sensor_bc", 4096, NULL, 2, NULL, 0);

    // 8. Initialize WebSocket client and acquire session key
    // Reconnection is handled automatically if the server is unreachable
    display_switch(SCREEN_IDLE, "Connecting to server...");
    ESP_ERROR_CHECK(ws_client_init(on_audio, on_text));

    while (ws_client_wait_session(15000) != ESP_OK) {
        display_switch(SCREEN_ERROR, "Server not found\nRetrying...");
        ESP_LOGW(TAG, "Failed to get session, retrying in 10s...");
        ws_client_deinit();
        vTaskDelay(pdMS_TO_TICKS(10000));
        display_switch(SCREEN_IDLE, "Connecting to server...");
        ESP_ERROR_CHECK(ws_client_init(on_audio, on_text));
    }

    // 9. Allocate PSRAM recording buffer for audio capture
    // Buffer size: 16kHz × 2 Bytes × 15 seconds = 480KB
    int16_t *rec_buf = heap_caps_malloc(RECORD_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!rec_buf) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM recording buffer!");
        return;
    }

    display_switch(SCREEN_IDLE, "Please swipe your RFID card");
    ESP_LOGI(TAG, "Ready - Waiting for RFID card...");

    // Application Main Loop: Handles RFID, Audio Recording, Encryption, and Playback
    rfid_card_t last_card  = {0};
    bool        session_ok = false;
    int         rfid_fail_count = 0;

    while (1) {

        // Check if a logout request was received from the server
        if (s_logout_req) {
            s_logout_req = false;
            session_ok   = false;
            memset(&last_card, 0, sizeof(last_card));
            s_username[0] = '\0';
            fan_off();
            display_switch(SCREEN_IDLE, "Please swipe your RFID card");
            ESP_LOGI(TAG, "Logout - Returned to RFID waiting screen");
        }

        // Wait for a valid RFID card scan before allowing PTT interaction
        if (!session_ok) {
            rfid_card_t card;
            if (rfid_poll(&card)) {
                rfid_fail_count = 0;
                // Process the card only if it's a new swipe
                if (!rfid_uid_equal(&card, &last_card)) {
                    last_card = card;
                    char uid[32];
                    rfid_uid_to_str(&card, uid, sizeof(uid));
                    ESP_LOGI(TAG, "Card: %s", uid);

                    // Send the scanned UID to the server for authentication
                    s_username[0] = '\0';
                    ws_client_send_rfid(uid);
                    vTaskDelay(pdMS_TO_TICKS(600));  // Wait for server to respond with user details

                    char msg[80];
                    snprintf(msg, sizeof(msg), "Welcome%s%s!",
                             strlen(s_username) ? ", " : "",
                             strlen(s_username) ? s_username : "");

                    display_switch(SCREEN_RFID_READ, msg);
                    vTaskDelay(pdMS_TO_TICKS(2000));

                    session_ok = true;
                    display_switch(SCREEN_READY, NULL);
                }
            } else {
                // Recover the MFRC522 module if reading fails 5 consecutive times
                if (++rfid_fail_count >= 5) {
                    rfid_fail_count = 0;
                    rfid_init();  // Soft reset and reopen the antenna
                    ESP_LOGW(TAG, "RFID recovery performed");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Wait for the user to press the Push-To-Talk button
        if (!ptt_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(50));   // Debounce delay
        if (!ptt_pressed()) continue;

        // Record audio from the microphone while the PTT button is held
        display_switch(SCREEN_RECORDING, NULL);
        ESP_LOGI(TAG, "Recording started...");

        size_t total   = 0;
        const size_t CHUNK   = 512;
        const size_t MAX_SMP = RECORD_BUF_SIZE / sizeof(int16_t);

        while (ptt_pressed() && total + CHUNK <= MAX_SMP) {
            size_t got = 0;
            i2s_mic_read(rec_buf + total, CHUNK, &got, 200);
            total += got;
        }

        size_t pcm_bytes = total * sizeof(int16_t);
        ESP_LOGI(TAG, "Recording finished: %.2fs (%zu B)",
                 (float)total / MIC_SAMPLE_RATE, pcm_bytes);

        // Discard very short recordings (less than 0.5 seconds)
        if (pcm_bytes < MIC_SAMPLE_RATE / 2) {
            ESP_LOGW(TAG, "Too short, skipping.");
            display_switch(SCREEN_READY, NULL);
            continue;
        }

        // Encrypt the recorded audio using AES-256 before transmission
        display_switch(SCREEN_PROCESSING, "AI processing...");

        const uint8_t *key = ws_client_get_session_key();
        if (!key) {
            display_switch(SCREEN_ERROR, "Session error");
            session_ok = false;
            continue;
        }

        // Apply PKCS#7 padding to the audio buffer
        size_t padded = crypto_pad_pcm((uint8_t *)rec_buf, pcm_bytes,
                                        RECORD_BUF_SIZE);
        if (!padded) { display_switch(SCREEN_ERROR, "Padding error"); continue; }

        uint8_t *cipher = heap_caps_malloc(padded, MALLOC_CAP_SPIRAM);
        uint8_t  iv[CRYPTO_IV_LEN];
        if (!cipher) { display_switch(SCREEN_ERROR, "Memory error"); continue; }

        if (crypto_encrypt(key, (uint8_t *)rec_buf, padded, iv, cipher) != ESP_OK) {
            free(cipher);
            display_switch(SCREEN_ERROR, "Encryption error");
            continue;
        }

        // Transmit the encrypted audio frame over WebSocket
        s_rsp_ready = false;
        esp_err_t err = ws_client_send_audio(iv, cipher, padded);
        free(cipher);

        if (err != ESP_OK) {
            display_switch(SCREEN_ERROR, "Transmission error");
            continue;
        }

        // Wait for the AI's audio response (up to 60 seconds timeout)
        uint32_t wait = 60000 / 50;
        while (!s_rsp_ready && wait-- > 0) vTaskDelay(pdMS_TO_TICKS(50));

        if (!s_rsp_ready) {
            display_switch(SCREEN_ERROR, "Response timeout");
            continue;
        }

        // Play the received PCM audio response through the speaker
        display_switch(SCREEN_SPEAKING, NULL);
        ESP_LOGI(TAG, "Playing: %zu bytes", s_rsp_len);
        i2s_player_play(s_rsp_pcm, s_rsp_len);

        display_switch(SCREEN_READY, NULL);
        ESP_LOGI(TAG, "Completed.\n");
    }
}
