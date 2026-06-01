// AES-256-CBC encryption and decryption module
// This module uses the ESP32-S3 hardware AES accelerator through mbedTLS.

#include "crypto.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/aes.h"
#include <string.h>

static const char *TAG = "crypto";

void crypto_random_bytes(uint8_t *buf, size_t len)
{
    // Fill the buffer with random bytes using the ESP32 hardware random number generator.
    // This provides cryptographic quality randomness.
    esp_fill_random(buf, len);
}

size_t crypto_pad_pcm(uint8_t *buf, size_t data_len, size_t buf_capacity)
{
    // Apply PKCS#7 padding by adding the missing byte count as the padding value.
    // This ensures the data length is a multiple of the AES block size.
    size_t pad = CRYPTO_BLOCK - (data_len % CRYPTO_BLOCK);
    if (pad == 0) pad = CRYPTO_BLOCK;

    if (data_len + pad > buf_capacity) {
        ESP_LOGE(TAG, "Not enough buffer capacity for pad: %zu + %zu > %zu",
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
        ESP_LOGE(TAG, "Data to encrypt is not a multiple of block size: %zu", plain_len);
        return ESP_ERR_INVALID_ARG;
    }

    // Generate a new random initialization vector for this encryption operation.
    crypto_random_bytes(iv_out, CRYPTO_IV_LEN);

    // Initialize the mbedTLS AES context.
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    // Set the encryption key. The key length is multiplied by 8 to get bits (32 bytes * 8 = 256 bits).
    int ret = mbedtls_aes_setkey_enc(&ctx, key, CRYPTO_KEY_LEN * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "AES key error: -0x%04X", -ret);
        mbedtls_aes_free(&ctx);
        return ESP_FAIL;
    }

    // Make a copy of the initialization vector.
    // CBC mode encryption modifies the IV during the process, so we must use a working copy
    // to preserve the original IV for transmission.
    uint8_t iv_work[CRYPTO_IV_LEN];
    memcpy(iv_work, iv_out, CRYPTO_IV_LEN);

    // Perform the CBC mode encryption.
    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                                 plain_len, iv_work,
                                 plain, cipher_out);
    
    // Free the AES context once encryption is done.
    mbedtls_aes_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES encryption error: -0x%04X", -ret);
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
        ESP_LOGE(TAG, "Data to decrypt is not a multiple of block size: %zu", cipher_len);
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize the mbedTLS AES context for decryption.
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);

    // Set the decryption key. Key length is in bits.
    int ret = mbedtls_aes_setkey_dec(&ctx, key, CRYPTO_KEY_LEN * 8);
    if (ret != 0) {
        ESP_LOGE(TAG, "AES key error: -0x%04X", -ret);
        mbedtls_aes_free(&ctx);
        return ESP_FAIL;
    }

    // Make a copy of the initialization vector.
    // Decryption in CBC mode also modifies the IV, requiring a working copy.
    uint8_t iv_work[CRYPTO_IV_LEN];
    memcpy(iv_work, iv, CRYPTO_IV_LEN);

    // Perform the CBC mode decryption.
    ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                                 cipher_len, iv_work,
                                 cipher, plain_out);
    
    // Free the AES context.
    mbedtls_aes_free(&ctx);

    if (ret != 0) {
        ESP_LOGE(TAG, "AES decryption error: -0x%04X", -ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}
