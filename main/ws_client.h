#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "crypto.h"

// Communication Protocol Layout
// The server sends binary responses to the ESP32 in this format:
// 16 bytes for the Initialization Vector (IV) followed by the AES-CBC encrypted PCM audio data.
// 
// The ESP32 sends binary audio to the server in this format:
// 1 byte for the frame type, 16 bytes for the IV, and then the AES-CBC encrypted PCM audio data.
//
// JSON messages are sent over standard text frames to handle control flow, such as creating sessions,
// authenticating RFIDs, and sending transcripts or status updates.

// Frame types used to distinguish binary messages going from the ESP32 to the server
#define WS_FRAME_TYPE_AUDIO   0x01
#define WS_FRAME_TYPE_RFID    0x02

// Callback function type for handling audio responses received by the WebSocket client.
// The pcm parameter points to the already decrypted raw audio data, and len specifies its size in bytes.
typedef void (*ws_audio_cb_t)(const uint8_t *pcm, size_t len);

// Callback function type for handling text-based transcript and status messages.
// The json parameter points to the raw JSON string received from the server.
typedef void (*ws_text_cb_t)(const char *json, size_t len);

// Initializes the WebSocket connection.
// When the connection is successfully established, the server will send a JSON message containing
// session details. The client automatically stores this session information.
// The audio_cb function is triggered when an encrypted audio response is received and decrypted.
// The text_cb function is triggered when the server sends a text-based JSON message.
esp_err_t ws_client_init(ws_audio_cb_t audio_cb, ws_text_cb_t text_cb);

// Blocks the current task until a session key is successfully received from the server.
// The timeout_ms parameter specifies the maximum time to wait before giving up.
// Returns ESP_OK if the session is ready, or a timeout error if it fails.
esp_err_t ws_client_wait_session(uint32_t timeout_ms);

// Sends the scanned RFID UID to the server for authentication.
// The uid_str should be a null-terminated hexadecimal string like "A1B2C3D4".
// The server is expected to respond with a JSON message containing the user's details.
esp_err_t ws_client_send_rfid(const char *uid_str);

// Sends encrypted audio data to the server.
// The data is packaged into a binary WebSocket frame that includes the type byte, the IV, and the ciphertext.
// The iv parameter is the 16-byte initialization vector used for encryption.
// The cipher and cipher_len define the actual encrypted PCM audio payload.
esp_err_t ws_client_send_audio(const uint8_t *iv,
                                const uint8_t *cipher, size_t cipher_len);

// Retrieves the active 32-byte AES session key.
// Returns a pointer to the key if the session has been established, or NULL if there is no active session.
const uint8_t *ws_client_get_session_key(void);

// Checks if the WebSocket connection is currently active.
bool ws_client_is_connected(void);

// Checks if a valid session key has been received and stored.
bool ws_client_has_session(void);

// Sends a raw JSON text frame to the server.
// This is typically used to upload sensor data telemetry.
esp_err_t ws_client_send_text(const char *json);

// Disconnects from the server and cleans up all allocated resources.
void ws_client_deinit(void);
