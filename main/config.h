#pragma once

// ─── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID           "Raspi"
#define WIFI_PASSWORD       "00000000"
#define WIFI_MAX_RETRY      10

// ─── AI Sunucu ────────────────────────────────────────────────────────────────
#define SERVER_HOST         "10.162.138.241"   // PC'nin yerel IP adresi
#define SERVER_PORT         8080
#define SERVER_WS_URI       "ws://" SERVER_HOST ":8080/ws"

// ─── SPI Bus (MFRC522 + ILI9341 paylaşımlı) ──────────────────────────────────
#define SPI_MOSI_GPIO       11
#define SPI_MISO_GPIO       13
#define SPI_SCK_GPIO        12
#define SPI_HOST            SPI2_HOST

// ─── MFRC522 RFID ─────────────────────────────────────────────────────────────
#define RFID_CS_GPIO        10
#define RFID_RST_GPIO       (-1)   // RST yerine yazılımsal reset kullanılır
#define RFID_SPI_FREQ_HZ    (2 * 1000 * 1000)  // 2 MHz (Breadboard'da çok daha stabildir)

// ─── ILI9341 TFT Ekran ────────────────────────────────────────────────────────
#define TFT_CS_GPIO         9
#define TFT_DC_GPIO         8
#define TFT_RST_GPIO        7
#define TFT_BL_GPIO         46
#define TFT_SPI_FREQ_HZ     (40 * 1000 * 1000) // 40 MHz
#define TFT_WIDTH           240
#define TFT_HEIGHT          320

// ─── INMP441 I2S Mikrofon ────────────────────────────────────────────────────
#define MIC_I2S_PORT        I2S_NUM_0
#define MIC_SCK_GPIO        42
#define MIC_WS_GPIO         2
#define MIC_SD_GPIO         41
#define MIC_SAMPLE_RATE     16000   // 16 kHz — Whisper için ideal

// ─── MAX98357A I2S Amfi/Hoparlör ─────────────────────────────────────────────
#define SPK_I2S_PORT        I2S_NUM_1
#define SPK_BCK_GPIO        16
#define SPK_WS_GPIO         17
#define SPK_DIN_GPIO        15
#define SPK_SAMPLE_RATE     22050   // Piper TTS çıkış hızı

// ─── Push-to-Talk Butonu ──────────────────────────────────────────────────────
#define PTT_GPIO            0       // INPUT_PULLUP, LOW = basılı

// ─── MQ135 Gaz/Duman Sensörü ─────────────────────────────────────────────────
#define SMOKE_AOUT_GPIO     4       // ADC1_CH3 — analog yoğunluk (GPIO3 strapping pin, kullanma)
#define SMOKE_DOUT_GPIO     14      // Dijital eşik çıkışı (isteğe bağlı)
#define SMOKE_ADC_CHANNEL   ADC_CHANNEL_3   // GPIO4 = ADC1_CH3
#define SMOKE_ADC_CLEAR     800     // Bu değerin altı temiz hava
#define SMOKE_ADC_HALF      1500    // Fan %50
#define SMOKE_ADC_FULL      2000    // Fan %100 + uyarı
#define SMOKE_WARMUP_MS     30000   // MQ135 ısınma süresi
#define SMOKE_SAMPLE_COUNT  32      // Gürültü filtreleme için ortalama

// ─── L298N Fan Motor Sürücü ──────────────────────────────────────────────────
#define FAN_IN4_GPIO        18      // Yön pini (HIGH = ileri)
#define FAN_ENA_GPIO        21      // PWM hız pini
#define FAN_LEDC_TIMER      LEDC_TIMER_1
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_1
#define FAN_PWM_FREQ_HZ     25000   // 25 kHz — motor sessiz frekansı
#define FAN_DUTY_RES        LEDC_TIMER_8_BIT    // 0–255

// ─── Tampon Boyutları (PSRAM) ─────────────────────────────────────────────────
// 16000 örnek/s × 2 byte × 15 s = 480.000 byte
#define MAX_RECORD_SECONDS  15
#define RECORD_BUF_SIZE     (MIC_SAMPLE_RATE * 2 * MAX_RECORD_SECONDS)

// 22050 × 2 × 30 s + WAV başlık = ~1.3 MB
#define RESP_BUF_SIZE       (SPK_SAMPLE_RATE * 2 * 30 + 1024)

// ─── Sensor Güncelleme Aralığı ────────────────────────────────────────────────
#define SENSOR_UPDATE_MS    5000    // Her 5 saniyede duman sensörü oku

// ─── DHT11 Sıcaklık ve Nem Sensörü ───────────────────────────────────────────
#define DHT11_GPIO          47      // DHT11 Data pini
#define DHT11_UPDATE_MS     2000    // DHT11 okuma aralığı (en az 2000 ms olmalı)

// ─── LVGL Ekran Tamponu ───────────────────────────────────────────────────────
// display.c ve main.c'de ortak kullanılır — buradan import edilir
#define LVGL_BUF_LINES      40      // Çift tampon için satır sayısı

// ─── LDR Işık Sensörü (Analog) ────────────────────────────────────────────────
#define LDR_AOUT_GPIO       6               // LDR Analog pini (Gerilim bölücü ile)
#define LDR_ADC_CHANNEL     ADC_CHANNEL_5   // GPIO6 = ADC1_CH5

// ─── PIR Hareket Sensörü ──────────────────────────────────────────────────────
#define PIR_GPIO            5               // PIR Sensör Veri (OUT) pini

// ─── Alev Sensörü ─────────────────────────────────────────────────────────────
#define FLAME_GPIO          48              // Alev Sensörü DO (Dijital Çıkış) pini

// ─── Titreşim Sensörü ─────────────────────────────────────────────────────────
#define VIB_GPIO            1               // SW-420 Titreşim Sensörü DO pini
