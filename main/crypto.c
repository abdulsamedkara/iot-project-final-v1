// crypto.c — AES-256-CBC şifreleme/deşifre modülü
// ESP32-S3 donanım AES hızlandırıcısı kullanır (mbedTLS üzerinden).

#include "crypto.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/aes.h"
#include <string.h>

static const char *TAG = "crypto";

void crypto_random_bytes(uint8_t *buf, size_t len)
{
    // ESP32 donanım RNG — kriptografik kalite
    esp_fill_random(buf, len);
}

size_t crypto_pad_pcm(uint8_t *buf, size_t data_len, size_t buf_capacity)
{
    // PKCS#7 padding: eksik byte sayısı kadar o değeri ekle
    size_t pad = CRYPTO_BLOCK - (data_len % CRYPTO_BLOCK);
    if (pad == 0) pad = CRYPTO_BLOCK;

    if (data_len + pad > buf_capacity) {
        ESP_LOGE(TAG, "Pad için yeter tampon yok: %zu + %zu > %zu",
                 data_len, pad, buf_capacity);
        return 0;
    }
    memset(buf + data_len, (uint8_t)pad, pad);
    return data_len + pad;
}

esp_err_t crypto_encrypt(const uint8_t *key,
                         const uint8_t *plain, size_t plain_len,
                         uint8_t *iv_out, uint8_t *cipher_out)
{
    if (plain_len % CRYPTO_BLOCK != 0) {
        ESP_LOGE(TAG, "Şifrelenecek veri blok boyutunun katı değil: %zu", plain_len);
        return ESP_ERR_INVALID_ARG;
    }

    // Rastgele IV üret
    crypto_random_bytes(iv_out, CRYPTO_IV_LEN);

    // mbedTLS AES bağlamı
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int ret = mbedtls_aes_setkey_enc(&ctx, key, CRYPTO_KEY_LEN * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "AES anahtar hatası: -0x%04X", -ret);
        mbedtls_aes_free(&ctx);
        return ESP_FAIL;
    }

    // IV'yi kopyala — CBC modu IV'yi değiştirdiği için ayrı bir kopyayla çalışırız
    uint8_t iv_work[CRYPTO_IV_LEN];
    memcpy(iv_work, iv_out, CRYPTO_IV_LEN);

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                                 plain_len, iv_work,
                                 plain, cipher_out);
    mbedtls_aes_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES şifreleme hatası: -0x%04X", -ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t crypto_decrypt(const uint8_t *key,
                         const uint8_t *iv,
                         const uint8_t *cipher, size_t cipher_len,
                         uint8_t *plain_out)
{
    if (cipher_len % CRYPTO_BLOCK != 0) {
        ESP_LOGE(TAG, "Deşifre edilecek veri blok boyutunun katı değil: %zu", cipher_len);
        return ESP_ERR_INVALID_ARG;
    }

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    int ret = mbedtls_aes_setkey_dec(&ctx, key, CRYPTO_KEY_LEN * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "AES anahtar hatası: -0x%04X", -ret);
        mbedtls_aes_free(&ctx);
        return ESP_FAIL;
    }

    uint8_t iv_work[CRYPTO_IV_LEN];
    memcpy(iv_work, iv, CRYPTO_IV_LEN);

    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                                 cipher_len, iv_work,
                                 cipher, plain_out);
    mbedtls_aes_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES deşifre hatası: -0x%04X", -ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}
