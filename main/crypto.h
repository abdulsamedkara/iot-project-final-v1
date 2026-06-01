#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Constants for AES-256-CBC encryption
#define CRYPTO_KEY_LEN  32  // 256-bit key length
#define CRYPTO_IV_LEN   16  // 128-bit block size for the initialization vector
#define CRYPTO_BLOCK    16  // Standard AES block size in bytes

// Encrypts data using AES-256-CBC.
// The length of the plaintext must be a multiple of the AES block size, which is 16 bytes.
// A random initialization vector (IV) is generated and written to the iv_out buffer so the receiver can use it.
// The key is a 32-byte AES key.
// The plain parameter holds the plaintext, and cipher_out will store the encrypted ciphertext.
esp_err_t crypto_encrypt(const uint8_t *key,
                         const uint8_t *plain, size_t plain_len,
                         uint8_t *iv_out, uint8_t *cipher_out);

// Decrypts ciphertext using AES-256-CBC.
// The key is a 32-byte AES key.
// The iv parameter is the 16-byte initialization vector that usually comes at the beginning of the message.
// The cipher and cipher_len define the ciphertext data and its length, which must be a multiple of 16.
// The decrypted plaintext is written to plain_out, which must be large enough to hold it.
esp_err_t crypto_decrypt(const uint8_t *key,
                         const uint8_t *iv,
                         const uint8_t *cipher, size_t cipher_len,
                         uint8_t *plain_out);

// Generates cryptographic quality random bytes using the ESP32 hardware random number generator.
// This is used for generating random IVs and other cryptographic needs.
void crypto_random_bytes(uint8_t *buf, size_t len);

// Pads the PCM audio buffer to a multiple of the AES block size using PKCS#7 padding.
// This ensures the data can be encrypted properly since AES block ciphers require aligned sizes.
// It returns the new padded length of the buffer.
size_t crypto_pad_pcm(uint8_t *buf, size_t data_len, size_t buf_capacity);
