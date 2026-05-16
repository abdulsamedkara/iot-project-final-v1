#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// AES-256-CBC sabitleri
#define CRYPTO_KEY_LEN  32  // 256 bit
#define CRYPTO_IV_LEN   16  // 128 bit blok
#define CRYPTO_BLOCK    16

/**
 * @brief  Veriyi AES-256-CBC ile şifreler.
 *         plain_len AES blok boyutunun (16) katı olmalıdır.
 *         IV rastgele üretilir ve iv_out'a yazılır.
 *
 * @param  key        32 byte AES anahtarı
 * @param  plain      Düz metin
 * @param  plain_len  Düz metin uzunluğu (16'nın katı)
 * @param  iv_out     16 byte IV çıktısı (gönderilmek üzere)
 * @param  cipher_out Şifreli çıktı (en az plain_len byte)
 */
esp_err_t crypto_encrypt(const uint8_t *key,
                         const uint8_t *plain, size_t plain_len,
                         uint8_t *iv_out, uint8_t *cipher_out);

/**
 * @brief  AES-256-CBC ile deşifre eder.
 *
 * @param  key        32 byte AES anahtarı
 * @param  iv         16 byte IV (mesajın başında gelir)
 * @param  cipher     Şifreli veri
 * @param  cipher_len Şifreli veri uzunluğu (16'nın katı)
 * @param  plain_out  Düz metin çıktısı (en az cipher_len byte)
 */
esp_err_t crypto_decrypt(const uint8_t *key,
                         const uint8_t *iv,
                         const uint8_t *cipher, size_t cipher_len,
                         uint8_t *plain_out);

/**
 * @brief  Kriptografik kalitede rastgele byte üretir (ESP32 HW RNG).
 */
void crypto_random_bytes(uint8_t *buf, size_t len);

/**
 * @brief  PCM tamponunu AES blok boyutuna pad'ler (PKCS#7).
 *         Döndürülen değer padlenmiş uzunluktur.
 */
size_t crypto_pad_pcm(uint8_t *buf, size_t data_len, size_t buf_capacity);
