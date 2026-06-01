// main.c — SmartLab Asistan Ana Uygulama (Faza 1)
// WiFi → WebSocket → AES-256 Session → RFID → PTT ses → AI yanıt → hoparlör
//
// Yapılandırma: config.h dosyasındaki WIFI_SSID, SERVER_HOST değerlerini düzenle.

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
#include "dht11.h"
#include "ui/display.h"

static const char *TAG = "main";

// ─── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_eg;
static int s_retry = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGW(TAG, "WiFi yeniden baglaniyor (%d/%d)...", s_retry, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_eg, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

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
    // Power save kapatılır — WS + STT sırasında bağlantı kesilmesin
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY);

    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

// ─── SPI Bus (MFRC522 + ILI9341 paylaşımlı, tek bus) ─────────────────────────
static void spi_bus_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num     = SPI_MOSI_GPIO,
        .miso_io_num     = SPI_MISO_GPIO,
        .sclk_io_num     = SPI_SCK_GPIO,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        // LVGL_BUF_LINES artık config.h'dan geliyor
        .max_transfer_sz = TFT_WIDTH * LVGL_BUF_LINES * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "SPI bus hazir: MOSI=%d MISO=%d SCK=%d",
             SPI_MOSI_GPIO, SPI_MISO_GPIO, SPI_SCK_GPIO);
}

// ─── PTT Butonu ───────────────────────────────────────────────────────────────
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
static inline bool ptt_pressed(void) { return gpio_get_level(PTT_GPIO) == 0; }

// ─── Duman/Fan Görevi ─────────────────────────────────────────────────────────
// Bağımsız FreeRTOS task — Core 0, 500 ms döngü
// Eşikler: config.h'dan SMOKE_ADC_CLEAR / SMOKE_ADC_HALF
static void smoke_task(void *arg)
{
    static const char *TAG2 = "smoke_task";

    // Isınma bekle
    ESP_LOGI(TAG2, "Isınma bekleniyor (%d ms)...", SMOKE_WARMUP_MS);
    while (!smoke_sensor_warmup_done()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG2, "Isınma tamamlandı. Ölçüm başlıyor.");

    int  alert_count = 0;
    bool in_alert    = false;

    while (1) {
        int adc = smoke_sensor_read_avg();

        if (adc < 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        ESP_LOGI(TAG2, "ADC=%d", adc);

        // Fan hız kontrolü — sadece duman varsa override et
        // Temiz hava durumunda temperature-based fan (sensor_broadcast_task) devreye girer
        if (adc >= SMOKE_ADC_FULL) {
            fan_full();
        } else if (adc >= SMOKE_ADC_HALF) {
            fan_half();
        } else if (adc >= SMOKE_ADC_CLEAR) {
            fan_half();
        }
        // adc < SMOKE_ADC_CLEAR → fan kontrolünü sensor_broadcast_task'e bırak

        // Alarm debounce: 3 ardışık yüksek okuma = alarm
        if (adc >= SMOKE_ADC_HALF) {
            if (++alert_count >= 3 && !in_alert) {
                in_alert = true;
                ESP_LOGW(TAG2, "DUMAN ALARMI! ADC=%d", adc);
                char msg[32];
                snprintf(msg, sizeof(msg), "ADC: %d", adc);
                display_switch(SCREEN_SMOKE_ALERT, msg);
            }
        } else {
            if (alert_count > 0) alert_count--;
            if (in_alert && adc < SMOKE_ADC_CLEAR) {
                in_alert = false;
                ESP_LOGI(TAG2, "Duman azaldı. ADC=%d", adc);
                display_switch(SCREEN_READY, NULL);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ─── DHT11 shared data (dht11_task yazar, sensor_broadcast_task okur) ────────
static volatile int s_dht_temp = 0;
static volatile int s_dht_hum  = 0;

// ─── DHT11 Görevi ─────────────────────────────────────────────────────────────
static void dht11_task(void *arg)
{
    static const char *TAG3 = "dht11_task";
    dht11_reading_t dht_data;

    ESP_LOGI(TAG3, "DHT11 okuma basliyor...");

    while (1) {
        esp_err_t res = dht11_read(DHT11_GPIO, &dht_data);
        if (res == ESP_OK) {
            s_dht_temp = dht_data.temperature;
            s_dht_hum  = dht_data.humidity;
            ESP_LOGI(TAG3, "Sicaklik: %dC, Nem: %%%d", dht_data.temperature, dht_data.humidity);
        } else {
            ESP_LOGW(TAG3, "DHT11 okuma basarisiz (hata: %d)", res);
        }
        vTaskDelay(pdMS_TO_TICKS(DHT11_UPDATE_MS));
    }
}

// ─── LDR Görevi (Analog) ──────────────────────────────────────────────────────
static void ldr_task(void *arg)
{
    static const char *TAG_LDR = "ldr_task";
    
    ESP_LOGI(TAG_LDR, "LDR okuma basliyor (Analog GPIO %d)...", LDR_AOUT_GPIO);

    while (1) {
        int ldr_adc = ldr_sensor_read_avg();
        if (ldr_adc >= 0) {
            ESP_LOGI(TAG_LDR, "LDR ADC = %d (Işık şiddeti)", ldr_adc);
        } else {
            ESP_LOGW(TAG_LDR, "LDR okuma hatası!");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Saniyede bir kontrol et
    }
}

// ─── PIR Görevi (Hareket Sensörü) ─────────────────────────────────────────────
static void pir_task(void *arg)
{
    static const char *TAG_PIR = "pir_task";
    
    // PIR sensörünün veri pini giriş olarak ayarlanır
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // Çoğu PIR modülü (HC-SR501 vb.) kendi direncine sahiptir
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = -1;
    ESP_LOGI(TAG_PIR, "PIR hareket sensoru dinleniyor (GPIO %d)...", PIR_GPIO);

    while (1) {
        int current_state = gpio_get_level(PIR_GPIO);
        
        // Durum değişimi varsa logla
        if (current_state != last_state) {
            if (current_state == 1) {
                ESP_LOGI(TAG_PIR, "ALARM: Hareket algilandi!");
            } else {
                ESP_LOGI(TAG_PIR, "Hareket bitti, ortam sakin.");
            }
            last_state = current_state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // 500 ms'de bir kontrol et
    }
}

// ─── Alev Sensörü Görevi ──────────────────────────────────────────────────────
static void flame_task(void *arg)
{
    static const char *TAG_FLAME = "flame_task";
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << FLAME_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Sensör DO sinyalini stabil tutmak için
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    int last_state = -1;
    ESP_LOGI(TAG_FLAME, "Alev sensoru dinleniyor (GPIO %d)...", FLAME_GPIO);

    while (1) {
        int current_state = gpio_get_level(FLAME_GPIO);
        
        if (current_state != last_state) {
            // Çoğu alev modülü alev algıladığında 0 (LOW), normalde 1 (HIGH) verir.
            if (current_state == 0) {
                ESP_LOGE(TAG_FLAME, "YANGIN ALARMI: Alev algilandi!");
            } else {
                ESP_LOGI(TAG_FLAME, "Alev bitti, ortam guvenli.");
            }
            last_state = current_state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // 500 ms'de bir kontrol et
    }
}

// ─── Titreşim Sensörü Görevi ────────────────────────────────────────────────────
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
    ESP_LOGI(TAG_VIB, "Titreşim sensörü dinleniyor (GPIO %d)...", VIB_GPIO);

    while (1) {
        int current_state = gpio_get_level(VIB_GPIO);
        
        if (current_state != last_state) {
            // SW-420 titreşim algıladığında hızlıca 1 ve 0 arasında gidip gelir
            if (current_state == 1) {
                ESP_LOGE(TAG_VIB, "DİKKAT: Titreşim / Sarsıntı algılandı!");
            }
            last_state = current_state;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Titreşim anlık olduğu için 100ms ile biraz daha hızlı okuyoruz
    }
}

// ─── Sensör Broadcast Görevi ──────────────────────────────────────────────────

// Fan modu: 0=manual, 1=auto_temp
static volatile int  s_fan_mode  = 1;   // başlangıçta auto
static volatile int  s_fan_speed = 0;   // 0-100, manuel mod için

// Fast sensörler (vib/pir/flame): 50ms'de bir state değişimi kontrol et → anında gönder
// Yavaş sensörler (sıcaklık/nem/duman/ldr): 1s'de bir güncelle
static void sensor_broadcast_task(void *arg)
{
    static const char *TAG_SB = "sensor_bcast";

    // Yavaş sensör cache
    int      s_temp  = 0, s_hum = 0, s_smoke = -1, s_ldr = -1;
    int      s_pir   = -1, s_flame = -1, s_vib = -1;   // önceki fast state
    uint32_t slow_tick = 0;  // 1000ms / 50ms = 20 döngüde bir yavaş güncelle

    while (1) {
        if (!ws_client_is_connected()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ── Fast sensörler: her 50ms ──────────────────────────────────────
        int pir   = gpio_get_level(PIR_GPIO);
        int flame = gpio_get_level(FLAME_GPIO);
        int vib   = gpio_get_level(VIB_GPIO);

        bool fast_changed = (pir != s_pir) || (flame != s_flame) || (vib != s_vib);
        s_pir = pir; s_flame = flame; s_vib = vib;

        // ── Yavaş sensörler: her 1s (20 × 50ms) ─────────────────────────
        bool slow_update = false;
        if (++slow_tick >= 20) {
            slow_tick = 0;
            slow_update = true;

            int smoke = smoke_sensor_read_avg();
            int ldr   = ldr_sensor_read_avg();
            // dht11_read çağırma — vTaskSuspendAll kullanır, RFID polling bozar
            s_temp  = s_dht_temp;
            s_hum   = s_dht_hum;
            s_smoke = (smoke >= 0) ? smoke : -1;
            s_ldr   = (ldr   >= 0) ? ldr   : -1;

            // Sıcaklık auto-fan (sadece auto modda, smoke_task override etmez)
            if (s_fan_mode == 1 && s_smoke < SMOKE_ADC_HALF) {
                if (s_temp >= 27) {
                    uint8_t duty = (s_temp >= 35) ? 255 : (uint8_t)((s_temp - 27) * 32);
                    fan_set_duty(duty);
                } else if (s_temp < 25) {
                    fan_off();
                }
            }
        }

        // ── Gönder: state değişimi VEYA 1s güncelleme ────────────────────
        if (fast_changed || slow_update) {
            char json[300];
            snprintf(json, sizeof(json),
                "{\"type\":\"sensors\","
                "\"temperature\":%d,\"humidity\":%d,"
                "\"smoke\":%d,\"ldr\":%d,"
                "\"pir\":%d,\"flame\":%d,\"vib\":%d,"
                "\"fan_mode\":%d,\"fan_speed\":%d}",
                s_temp, s_hum, s_smoke, s_ldr,
                pir, flame, vib,
                s_fan_mode, s_fan_speed);

            ws_client_send_text(json);

            if (fast_changed) {
                ESP_LOGI(TAG_SB, "Fast change: pir=%d flame=%d vib=%d", pir, flame, vib);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ─── WebSocket callback'leri ──────────────────────────────────────────────────
static volatile bool    s_rsp_ready = false;
static const uint8_t   *s_rsp_pcm   = NULL;
static volatile size_t  s_rsp_len   = 0;

static void on_audio(const uint8_t *pcm, size_t len)
{
    s_rsp_pcm   = pcm;
    s_rsp_len   = len;
    s_rsp_ready = true;
}

// Kullanıcı adını buraya yaz (RFID yanıtından)
static char s_username[64] = {0};
static volatile bool s_logout_req = false;

static void on_text(const char *json, size_t len)
{
    static const char *TAG_T = "on_text";

    // {"type":"logout"}
    if (strstr(json, "\"type\":\"logout\"")) {
        s_logout_req = true;
        ESP_LOGI(TAG_T, "Logout komutu alindi");
        return;
    }

    // {"type":"user","name":"Samed Kara"}
    const char *p = strstr(json, "\"name\":\"");
    if (p) {
        p += 8;
        const char *end = strchr(p, '"');
        if (end) {
            size_t n = (size_t)(end - p);
            if (n >= sizeof(s_username)) n = sizeof(s_username) - 1;
            memcpy(s_username, p, n);
            s_username[n] = '\0';
            ESP_LOGI(TAG_T, "Kullanici: %s", s_username);
        }
    }

    // {"type":"fan","on":true/false,"speed":75,"mode":"manual"/"auto"}
    if (strstr(json, "\"type\":\"fan\"")) {
        // mode
        if (strstr(json, "\"mode\":\"auto\""))   s_fan_mode = 1;
        if (strstr(json, "\"mode\":\"manual\"")) s_fan_mode = 0;

        if (s_fan_mode == 0) {
            // Manuel: on/off + speed
            bool fan_on = strstr(json, "\"on\":true") != NULL;
            // speed değerini parse et
            const char *sp = strstr(json, "\"speed\":");
            if (sp) {
                s_fan_speed = atoi(sp + 8);
                if (s_fan_speed < 0)   s_fan_speed = 0;
                if (s_fan_speed > 100) s_fan_speed = 100;
            }
            if (!fan_on) {
                fan_off();
                ESP_LOGI(TAG_T, "Fan kapat (manuel)");
            } else {
                fan_set_duty((uint8_t)(s_fan_speed * 255 / 100));
                ESP_LOGI(TAG_T, "Fan ac (manuel) %d%%", s_fan_speed);
            }
        }
    }
}

// ─── Ana Uygulama ─────────────────────────────────────────────────────────────
void app_main(void)
{
    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // TCP/IP stack ve event loop — WiFi'den önce, LVGL'den önce
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. SPI Bus (TFT ve RFID bunu paylaşır)
    spi_bus_init();

    // 3. Ekran
    ESP_ERROR_CHECK(display_init());
    display_switch(SCREEN_IDLE, "Baslatiliyor...");

    // 4. WiFi
    display_switch(SCREEN_IDLE, "WiFi baglaniyor...");
    if (wifi_init() != ESP_OK) {
        display_switch(SCREEN_ERROR, "WiFi baglanti hatasi!");
        ESP_LOGE(TAG, "WiFi baslatılamadı.");
        return;
    }

    // 5. I2S
    ESP_ERROR_CHECK(i2s_mic_init());
    ESP_ERROR_CHECK(i2s_player_init(SPK_SAMPLE_RATE, 16, 1));

    // 6. PTT
    ptt_init();

    // 7. RFID
    ESP_ERROR_CHECK(rfid_init());

    // 7b. Duman sensörü + fan + LDR ADC başlatma
    ESP_ERROR_CHECK(smoke_sensor_init());
    ESP_ERROR_CHECK(ldr_sensor_init());
    ESP_ERROR_CHECK(fan_control_init());
    xTaskCreatePinnedToCore(smoke_task, "smoke", 3072, NULL, 3, NULL, 0);

    // 7c. DHT11 Sıcaklık ve Nem
    xTaskCreatePinnedToCore(dht11_task, "dht11", 4096, NULL, 3, NULL, 0);

    // 7d. LDR Işık Sensörü
    xTaskCreatePinnedToCore(ldr_task, "ldr", 4096, NULL, 3, NULL, 0);

    // 7e. PIR Hareket Sensörü
    xTaskCreatePinnedToCore(pir_task, "pir", 4096, NULL, 3, NULL, 0);

    // 7f. Alev Sensörü
    xTaskCreatePinnedToCore(flame_task, "flame", 4096, NULL, 3, NULL, 0);

    // 7g. Titreşim Sensörü
    xTaskCreatePinnedToCore(vib_task, "vib", 4096, NULL, 3, NULL, 0);

    // 7h. Sensör broadcast (web UI için 5s'de bir JSON gönderir)
    xTaskCreatePinnedToCore(sensor_broadcast_task, "sensor_bc", 4096, NULL, 2, NULL, 0);

    // 8. WebSocket → session key al (PTT ile yeniden deneme destekli)
    display_switch(SCREEN_IDLE, "Sunucuya baglaniliyor...");
    ESP_ERROR_CHECK(ws_client_init(on_audio, on_text));

    while (ws_client_wait_session(15000) != ESP_OK) {
        display_switch(SCREEN_ERROR, "Sunucu bulunamadi\nYeniden deneniyor...");
        ESP_LOGW(TAG, "Session alinamadi, 10s sonra tekrar denenecek...");
        ws_client_deinit();
        vTaskDelay(pdMS_TO_TICKS(10000));
        display_switch(SCREEN_IDLE, "Sunucuya baglaniliyor...");
        ESP_ERROR_CHECK(ws_client_init(on_audio, on_text));
    }

    // 9. PSRAM kayıt tamponu (16kHz × 2B × 15s = 480KB)
    int16_t *rec_buf = heap_caps_malloc(RECORD_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!rec_buf) {
        ESP_LOGE(TAG, "PSRAM kayit tamponu alinamadi!");
        return;
    }

    display_switch(SCREEN_IDLE, "RFID kartinizi okutun");
    ESP_LOGI(TAG, "Hazir — RFID kart bekleniyor...");

    // ─── Ana Döngü ────────────────────────────────────────────────────────────
    rfid_card_t last_card  = {0};
    bool        session_ok = false;
    int         rfid_fail_count = 0;

    while (1) {

        // ── 0. Logout kontrolü ────────────────────────────────────────────────
        if (s_logout_req) {
            s_logout_req = false;
            session_ok   = false;
            memset(&last_card, 0, sizeof(last_card));
            s_username[0] = '\0';
            fan_off();
            display_switch(SCREEN_IDLE, "RFID kartinizi okutun");
            ESP_LOGI(TAG, "Logout — RFID bekleme ekranina donuldu");
        }

        // ── 1. RFID Kart Okuma ────────────────────────────────────────────────
        if (!session_ok) {
            rfid_card_t card;
            if (rfid_poll(&card)) {
                rfid_fail_count = 0;
                if (!rfid_uid_equal(&card, &last_card)) {
                    last_card = card;
                    char uid[32];
                    rfid_uid_to_str(&card, uid, sizeof(uid));
                    ESP_LOGI(TAG, "Kart: %s", uid);

                    s_username[0] = '\0';
                    ws_client_send_rfid(uid);
                    vTaskDelay(pdMS_TO_TICKS(600));  // Sunucu yanıtı bekle

                    char msg[80];
                    snprintf(msg, sizeof(msg), "Hosgeldiniz%s%s!",
                             strlen(s_username) ? ", " : "",
                             strlen(s_username) ? s_username : "");

                    display_switch(SCREEN_RFID_READ, msg);
                    vTaskDelay(pdMS_TO_TICKS(2000));

                    session_ok = true;
                    display_switch(SCREEN_READY, NULL);
                }
            } else {
                // Başarısız okuma — 5 art arda hata sonrası MFRC522 recovery
                if (++rfid_fail_count >= 5) {
                    rfid_fail_count = 0;
                    rfid_init();  // Soft reset + antenna yeniden aç
                    ESP_LOGW(TAG, "RFID recovery yapildi");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── 2. PTT Bekleme ────────────────────────────────────────────────────
        if (!ptt_pressed()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(50));   // debounce
        if (!ptt_pressed()) continue;

        // ── 3. Ses Kayıt ─────────────────────────────────────────────────────
        display_switch(SCREEN_RECORDING, NULL);
        ESP_LOGI(TAG, "Kayit basladi...");

        size_t total   = 0;
        const size_t CHUNK   = 512;
        const size_t MAX_SMP = RECORD_BUF_SIZE / sizeof(int16_t);

        while (ptt_pressed() && total + CHUNK <= MAX_SMP) {
            size_t got = 0;
            i2s_mic_read(rec_buf + total, CHUNK, &got, 200);
            total += got;
        }

        size_t pcm_bytes = total * sizeof(int16_t);
        ESP_LOGI(TAG, "Kayit bitti: %.2fs (%zu B)",
                 (float)total / MIC_SAMPLE_RATE, pcm_bytes);

        if (pcm_bytes < MIC_SAMPLE_RATE / 2) {   // < 0.5s
            ESP_LOGW(TAG, "Cok kisa, atlaniyor.");
            display_switch(SCREEN_READY, NULL);
            continue;
        }

        // ── 4. AES-256 Şifrele ──────────────────────────────────────────────
        display_switch(SCREEN_PROCESSING, "AI isleniyor...");

        const uint8_t *key = ws_client_get_session_key();
        if (!key) {
            display_switch(SCREEN_ERROR, "Session hatasi");
            session_ok = false;
            continue;
        }

        // PKCS#7 pad
        size_t padded = crypto_pad_pcm((uint8_t *)rec_buf, pcm_bytes,
                                        RECORD_BUF_SIZE);
        if (!padded) { display_switch(SCREEN_ERROR, "Pad hatasi"); continue; }

        uint8_t *cipher = heap_caps_malloc(padded, MALLOC_CAP_SPIRAM);
        uint8_t  iv[CRYPTO_IV_LEN];
        if (!cipher) { display_switch(SCREEN_ERROR, "Bellek hatasi"); continue; }

        if (crypto_encrypt(key, (uint8_t *)rec_buf, padded, iv, cipher) != ESP_OK) {
            free(cipher);
            display_switch(SCREEN_ERROR, "Sifreleme hatasi");
            continue;
        }

        // ── 5. WebSocket ile Gönder ──────────────────────────────────────────
        s_rsp_ready = false;
        esp_err_t err = ws_client_send_audio(iv, cipher, padded);
        free(cipher);

        if (err != ESP_OK) {
            display_switch(SCREEN_ERROR, "Gonderim hatasi");
            continue;
        }

        // ── 6. Yanıt Bekle (max 60s) ─────────────────────────────────────────
        uint32_t wait = 60000 / 50;
        while (!s_rsp_ready && wait-- > 0) vTaskDelay(pdMS_TO_TICKS(50));

        if (!s_rsp_ready) {
            display_switch(SCREEN_ERROR, "Yanit zaman asimi");
            continue;
        }

        // ── 7. Sesi Oynat ────────────────────────────────────────────────────
        display_switch(SCREEN_SPEAKING, NULL);
        ESP_LOGI(TAG, "Oynatiliyor: %zu byte", s_rsp_len);
        i2s_player_play(s_rsp_pcm, s_rsp_len);

        display_switch(SCREEN_READY, NULL);
        ESP_LOGI(TAG, "Tamamlandi.\n");
    }
}
