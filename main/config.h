#pragma once

// WiFi Configuration
// These are the credentials used by the ESP32 to connect to the local network.
#define WIFI_SSID           "Raspi"
#define WIFI_PASSWORD       "00000000"
#define WIFI_MAX_RETRY      10

// AI Server Configuration
// Defines the endpoint where the AI backend is running.
#define SERVER_HOST         "10.162.138.241"   // The local IP address of the PC running the server
#define SERVER_PORT         8080
#define SERVER_WS_URI       "ws://" SERVER_HOST ":8080/ws"

// SPI Bus Configuration
// This SPI bus is shared between the MFRC522 RFID reader and the ILI9341 TFT display.
#define SPI_MOSI_GPIO       11
#define SPI_MISO_GPIO       13
#define SPI_SCK_GPIO        12
#define SPI_HOST            SPI2_HOST

// MFRC522 RFID Configuration
// Pinout and speed settings for the RFID module.
#define RFID_CS_GPIO        10
#define RFID_RST_GPIO       (-1)               // Hardware reset pin is not used; we rely on software reset instead
#define RFID_SPI_FREQ_HZ    (2 * 1000 * 1000)  // Running at 2 MHz provides much better stability on a breadboard setup

// ILI9341 TFT Display Configuration
// Pinout and screen dimensions for the display module.
#define TFT_CS_GPIO         9
#define TFT_DC_GPIO         8
#define TFT_RST_GPIO        7
#define TFT_BL_GPIO         46
#define TFT_SPI_FREQ_HZ     (40 * 1000 * 1000) // The display handles 40 MHz SPI speed smoothly
#define TFT_WIDTH           240
#define TFT_HEIGHT          320

// INMP441 I2S Microphone Configuration
// Pinout and sampling settings for audio input.
#define MIC_I2S_PORT        I2S_NUM_0
#define MIC_SCK_GPIO        42
#define MIC_WS_GPIO         2
#define MIC_SD_GPIO         41
#define MIC_SAMPLE_RATE     16000   // 16 kHz is the ideal sampling rate for Whisper AI models

// MAX98357A I2S Amplifier/Speaker Configuration
// Pinout and sampling settings for audio output.
#define SPK_I2S_PORT        I2S_NUM_1
#define SPK_BCK_GPIO        16
#define SPK_WS_GPIO         17
#define SPK_DIN_GPIO        15
#define SPK_SAMPLE_RATE     22050   // 22.05 kHz matches the default output rate of Piper TTS

// Push-to-Talk Button Configuration
#define PTT_GPIO            0       // Uses INPUT_PULLUP, so a LOW state means the button is pressed

// MQ135 Gas and Smoke Sensor Configuration
// Defines pins and thresholds for air quality monitoring.
#define SMOKE_AOUT_GPIO     4               // Analog pin for density reading (ADC1_CH3)
#define SMOKE_DOUT_GPIO     14              // Optional digital threshold output pin
#define SMOKE_ADC_CHANNEL   ADC_CHANNEL_3   // GPIO4 corresponds to ADC1_CH3
#define SMOKE_ADC_CLEAR     800             // ADC values below this indicate clean air
#define SMOKE_ADC_HALF      1500            // Triggers the fan to run at 50% speed
#define SMOKE_ADC_FULL      2000            // Triggers the fan to run at 100% speed and issues a warning
#define SMOKE_WARMUP_MS     30000           // The MQ135 sensor requires 30 seconds to heat up and stabilize
#define SMOKE_SAMPLE_COUNT  32              // We average 32 samples to filter out electrical noise

// L298N Fan Motor Driver Configuration
// PWM settings for controlling the cooling fan speed.
#define FAN_IN4_GPIO        18              // Direction control pin (HIGH = forward)
#define FAN_ENA_GPIO        21              // PWM speed control pin
#define FAN_LEDC_TIMER      LEDC_TIMER_1
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_1
#define FAN_PWM_FREQ_HZ     25000           // 25 kHz prevents audible coil whine from the motor
#define FAN_DUTY_RES        LEDC_TIMER_8_BIT // 8-bit resolution means duty cycle goes from 0 to 255

// Buffer Sizes (PSRAM)
// Memory allocations for holding audio data during recording and playback.
#define MAX_RECORD_SECONDS  15
// Calculation: 16000 samples/s * 2 bytes/sample * 15 seconds = 480,000 bytes
#define RECORD_BUF_SIZE     (MIC_SAMPLE_RATE * 2 * MAX_RECORD_SECONDS)

// Calculation: 22050 samples/s * 2 bytes/sample * 30 seconds + WAV header = ~1.3 MB
#define RESP_BUF_SIZE       (SPK_SAMPLE_RATE * 2 * 30 + 1024)

// Sensor Update Interval
#define SENSOR_UPDATE_MS    5000            // Time between smoke sensor readings in milliseconds

// DHT11 Temperature and Humidity Sensor Configuration
#define DHT11_GPIO          47              // Data pin for the DHT11 sensor
#define DHT11_UPDATE_MS     2000            // The DHT11 cannot be read faster than once every 2 seconds

// LVGL Display Buffer Configuration
// Shared between display.c and main.c.
#define LVGL_BUF_LINES      40              // Number of lines allocated for LVGL's double buffering system

// LDR Light Sensor (Analog) Configuration
#define LDR_AOUT_GPIO       6               // Analog pin for reading light intensity
#define LDR_ADC_CHANNEL     ADC_CHANNEL_5   // GPIO6 corresponds to ADC1_CH5

// PIR Motion Sensor Configuration
#define PIR_GPIO            5               // Digital output pin from the PIR sensor

// Flame Sensor Configuration
#define FLAME_GPIO          48              // Digital output pin from the flame sensor

// SW-420 Vibration Sensor Configuration
#define VIB_GPIO            1               // Digital output pin from the vibration sensor

// LDR to LED Automatic Control Thresholds
// Defines how the ambient light levels map to the LED brightness.
#define LDR_LED_DARK_ADC     2200           // Below this value means total darkness, so turn LED to 100%
#define LDR_LED_BRIGHT_ADC   3100           // Above this value means it's bright enough, so turn LED to 0%

// Strip LED Configuration
// Controlled via channel A of an L298N motor driver.
// Note: IN1 is wired to 3.3V, IN2 to GND, and ENB to the PWM pin.
#define LED_ENB_GPIO        38
#define LED_LEDC_TIMER      LEDC_TIMER_2
#define LED_LEDC_CHANNEL    LEDC_CHANNEL_2
#define LED_PWM_FREQ_HZ     1000            // 1 kHz is perfectly adequate for smooth LED dimming
#define LED_DUTY_RES        LEDC_TIMER_8_BIT // 8-bit resolution (0-255)
