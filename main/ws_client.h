#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "crypto.h"

// Sunucu → ESP32 binary yanıt: IV(16) | AES-CBC(PCM)
// ESP32 → Sunucu binary ses: TYPE(1) | IV(16) | AES-CBC(PCM)
// JSON mesajlar: kontrol için (session, rfid, transcript)

#define WS_FRAME_TYPE_AUDIO   0x01
#define WS_FRAME_TYPE_RFID    0x02

// ws_client'in aldığı ses yanıtı için callback
// data: şifresi çözülmüş ham PCM, len: byte sayısı
typedef void (*ws_audio_cb_t)(const uint8_t *pcm, size_t len);

// Transcript/durum mesajları için callback (JSON text frame)
typedef void (*ws_text_cb_t)(const char *json, size_t len);

/**
 * @brief  WebSocket bağlantısını başlatır.
 *         Bağlantı kurulunca sunucu session JSON'u gönderir ve
 *         ws_client oturumu depolar.
 *
 * @param  audio_cb  Sunucudan ses yanıtı gelince çağrılır (şifresi zaten çözülmüş)
 * @param  text_cb   Sunucudan JSON metin frame'i gelince çağrılır
 */
esp_err_t ws_client_init(ws_audio_cb_t audio_cb, ws_text_cb_t text_cb);

/**
 * @brief  Session anahtarının sunucudan alınmasını bekler.
 * @param  timeout_ms  Maksimum bekleme süresi
 */
esp_err_t ws_client_wait_session(uint32_t timeout_ms);

/**
 * @brief  RFID UID'sini sunucuya gönderir.
 *         Sunucu kullanıcı adıyla JSON yanıt verir.
 *
 * @param  uid_str  Hex string UID (ör. "A1B2C3D4")
 */
esp_err_t ws_client_send_rfid(const char *uid_str);

/**
 * @brief  Şifreli ses verisini sunucuya gönderir.
 *         IV + şifreli PCM binary frame olarak iletilir.
 *
 * @param  iv         16 byte IV
 * @param  cipher     Şifreli PCM
 * @param  cipher_len Şifreli veri uzunluğu
 */
esp_err_t ws_client_send_audio(const uint8_t *iv,
                                const uint8_t *cipher, size_t cipher_len);

/**
 * @brief  Geçerli session anahtarını döndürür.
 *         NULL: henüz session başlamadı.
 */
const uint8_t *ws_client_get_session_key(void);

/**
 * @brief  Bağlantı ve session durumu
 */
bool ws_client_is_connected(void);
bool ws_client_has_session(void);

/**
 * @brief  WebSocket bağlantısını kapatır.
 */
void ws_client_deinit(void);
