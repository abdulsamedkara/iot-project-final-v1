#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define RFID_UID_MAX_LEN  10

typedef struct {
    uint8_t uid[RFID_UID_MAX_LEN];
    uint8_t uid_len;
} rfid_card_t;

/**
 * @brief  MFRC522 SPI sürücüsünü başlatır.
 *         SPI bus daha önce spi_bus_initialize() ile başlatılmış olmalıdır.
 */
esp_err_t rfid_init(void);

/**
 * @brief  Kart varlığını sorgular ve UID'sini okur.
 * @return true: kart algılandı, card dolduruldu.
 *         false: kart yok veya iletişim hatası.
 */
bool rfid_poll(rfid_card_t *card);

/**
 * @brief  UID'yi hex string'e dönüştürür (ör. "A1B2C3D4").
 */
void rfid_uid_to_str(const rfid_card_t *card, char *buf, size_t buf_sz);

/**
 * @brief  İki kart UID'sinin aynı olup olmadığını karşılaştırır.
 */
bool rfid_uid_equal(const rfid_card_t *a, const rfid_card_t *b);
