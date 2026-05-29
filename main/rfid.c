// rfid.c — MFRC522 RFID okuyucu sürücüsü
// ESP-IDF SPI master API ile yazılmıştır.
// SPI bus (SPI2_HOST) main.c'de başlatılır; burada yalnızca cihaz eklenir.

#include "rfid.h"
#include "config.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "rfid";
static spi_device_handle_t s_spi = NULL;

// ─── MFRC522 Register Adresleri ───────────────────────────────────────────────
#define REG_COMMAND         0x01
#define REG_COM_IEN         0x02
#define REG_COM_IRQ         0x04
#define REG_ERROR           0x06
#define REG_FIFO_DATA       0x09
#define REG_FIFO_LEVEL      0x0A
#define REG_CONTROL         0x0C
#define REG_BIT_FRAMING     0x0D
#define REG_COLL            0x0E
#define REG_MODE            0x11
#define REG_TX_CONTROL      0x14
#define REG_TX_ASK          0x15
#define REG_RX_GAIN         0x26
#define REG_CRC_RESULT_MSB  0x21
#define REG_CRC_RESULT_LSB  0x22
#define REG_T_MODE          0x2A
#define REG_T_PRESCALER     0x2B
#define REG_T_RELOAD_H      0x2C
#define REG_T_RELOAD_L      0x2D
#define REG_VERSION         0x37

// ─── MFRC522 Komutları ────────────────────────────────────────────────────────
#define CMD_IDLE            0x00
#define CMD_CALC_CRC        0x03
#define CMD_TRANSCEIVE      0x0C
#define CMD_SOFT_RESET      0x0F

// ─── ISO 14443A PICC Komutları ────────────────────────────────────────────────
#define PICC_REQA           0x26
#define PICC_ANTICOLL       0x93
#define PICC_HLTA           0x50

// ─── SPI R/W ──────────────────────────────────────────────────────────────────
// MFRC522 SPI: 8-bit komut baytı → bit7=0 yaz, bit7=1 oku
// komut baytı: [R/W][ADDR 6bit][0]

static void mfrc_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0x7E), val };
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
    };
    spi_device_transmit(s_spi, &t);
}

static uint8_t mfrc_read(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(((reg << 1) & 0x7E) | 0x80), 0x00 };
    uint8_t rx[2] = {0};
    spi_transaction_t t = {
        .length    = 16,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_transmit(s_spi, &t);
    return rx[1];
}

static void mfrc_set_bits(uint8_t reg, uint8_t mask)
{
    mfrc_write(reg, mfrc_read(reg) | mask);
}

static void mfrc_clear_bits(uint8_t reg, uint8_t mask)
{
    mfrc_write(reg, mfrc_read(reg) & ~mask);
}

// ─── MFRC522 Başlatma ─────────────────────────────────────────────────────────
static void mfrc_reset(void)
{
    mfrc_write(REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void mfrc_antenna_on(void)
{
    uint8_t val = mfrc_read(REG_TX_CONTROL);
    if ((val & 0x03) != 0x03) {
        mfrc_set_bits(REG_TX_CONTROL, 0x03);
    }
}

esp_err_t rfid_init(void)
{
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = RFID_SPI_FREQ_HZ,
        .mode           = 0,    // CPOL=0, CPHA=0
        .spics_io_num   = RFID_CS_GPIO,
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev_cfg, &s_spi));

    mfrc_reset();

    // Timer: 25ms zaman aşımı
    mfrc_write(REG_T_MODE,      0x8D);
    mfrc_write(REG_T_PRESCALER, 0x3E);
    mfrc_write(REG_T_RELOAD_H,  0x00);
    mfrc_write(REG_T_RELOAD_L,  0x1E);

    // CRC ve Hassasiyet ön ayarı
    mfrc_write(REG_TX_ASK,  0x40);
    mfrc_write(REG_MODE,    0x3D);
    mfrc_write(REG_RX_GAIN, 0x70); // Anten hassasiyetini max (48dB) seviyesine çek (Klon kartlarda stabiliteyi çok artırır)

    mfrc_antenna_on();

    uint8_t ver = mfrc_read(REG_VERSION);
    if (ver == 0x91 || ver == 0x92 || ver == 0x18 || ver == 0x88) {
        ESP_LOGI(TAG, "MFRC522 hazir — Versiyon: 0x%02X", ver);
    } else {
        ESP_LOGW(TAG, "MFRC522 beklenmeyen versiyon: 0x%02X — baglanti kontrol edin", ver);
    }
    return ESP_OK;
}

// ─── PICC İletişim ───────────────────────────────────────────────────────────
typedef struct {
    uint8_t data[16];
    uint8_t len;
    bool    valid;
} mfrc_resp_t;

static mfrc_resp_t mfrc_transceive(const uint8_t *tx_data, uint8_t tx_len,
                                    uint8_t last_bits)
{
    mfrc_resp_t resp = {0};

    mfrc_write(REG_COM_IEN,    0xF7);
    mfrc_write(REG_COM_IRQ,    0x7F);
    mfrc_write(REG_FIFO_LEVEL, 0x80);  // FIFO temizle
    mfrc_write(REG_COMMAND,    CMD_IDLE);

    for (uint8_t i = 0; i < tx_len; i++) {
        mfrc_write(REG_FIFO_DATA, tx_data[i]);
    }

    mfrc_write(REG_BIT_FRAMING, last_bits);  // sadece bit sayısı, StartSend yok
    mfrc_write(REG_COMMAND, CMD_TRANSCEIVE);
    mfrc_set_bits(REG_BIT_FRAMING, 0x80);    // StartSend — her zaman set et

    // IRQ bekle (max 50ms — donanım timer ~25ms, HLTA yanıt vermez bu yüzden kısa tut)
    uint16_t timeout = 50;
    uint8_t irq;
    do {
        irq = mfrc_read(REG_COM_IRQ);
        timeout--;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (!(irq & 0x31) && timeout);  // RxIRq | IdleIRq | TimerIRq

    mfrc_clear_bits(REG_BIT_FRAMING, 0x80);

    if (!timeout) {
        ESP_LOGD(TAG, "Transceive zaman aşımı");
        return resp;
    }

    uint8_t err = mfrc_read(REG_ERROR);
    if (err & 0x1B) {
        ESP_LOGD(TAG, "Transceive hata: 0x%02X", err);
        return resp;
    }

    uint8_t fifo_len = mfrc_read(REG_FIFO_LEVEL);
    if (fifo_len > sizeof(resp.data)) fifo_len = sizeof(resp.data);

    for (uint8_t i = 0; i < fifo_len; i++) {
        resp.data[i] = mfrc_read(REG_FIFO_DATA);
    }
    resp.len   = fifo_len;
    resp.valid = true;
    return resp;
}

// ─── Kart Algılama + UID Okuma ────────────────────────────────────────────────
bool rfid_poll(rfid_card_t *card)
{
    // 1. REQA — kart var mı?
    uint8_t reqa = PICC_REQA;
    mfrc_resp_t atqa = mfrc_transceive(&reqa, 1, 7);  // 7 bit (son byte 7 bit)
    if (!atqa.valid || atqa.len < 2) return false;

    // 2. AntiCollision — UID oku
    uint8_t anticoll[2] = {PICC_ANTICOLL, 0x20};
    mfrc_resp_t uid_resp = mfrc_transceive(anticoll, 2, 0);
    if (!uid_resp.valid || uid_resp.len < 5) return false;

    // UID: 4 byte + 1 byte BCC (XOR checksum)
    uint8_t bcc = 0;
    for (int i = 0; i < 4; i++) bcc ^= uid_resp.data[i];
    if (bcc != uid_resp.data[4]) {
        ESP_LOGD(TAG, "UID BCC hatası");
        return false;
    }

    memcpy(card->uid, uid_resp.data, 4);
    card->uid_len = 4;

    // 3. HLTA — kartı durdur (bir sonraki poll'de tekrar REQA'ya cevap vermesi için)
    uint8_t hlta[2] = {PICC_HLTA, 0x00};
    mfrc_transceive(hlta, 2, 0);

    return true;
}

void rfid_uid_to_str(const rfid_card_t *card, char *buf, size_t buf_sz)
{
    buf[0] = '\0';
    for (uint8_t i = 0; i < card->uid_len && (i * 2 + 2) < buf_sz; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X", card->uid[i]);
        strcat(buf, hex);
    }
}

bool rfid_uid_equal(const rfid_card_t *a, const rfid_card_t *b)
{
    if (a->uid_len != b->uid_len) return false;
    return memcmp(a->uid, b->uid, a->uid_len) == 0;
}
