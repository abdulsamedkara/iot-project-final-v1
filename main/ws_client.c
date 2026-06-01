// WebSocket client implementation
// This file manages the WebSocket connection to the server using the ESP-IDF esp_websocket_client.
//
// Protocol Overview:
// - Connection Establishment:
//   The ESP connects to the server, and the server sends a JSON text frame to establish the session:
//   {"type":"session","session_id":"...","key_b64":"..."}
// - RFID Authentication:
//   The ESP sends the scanned RFID UID as a JSON text frame:
//   {"type":"rfid","uid":"AABBCCDD","session_id":"..."}
// - Server Text Responses:
//   The server may respond with a user profile or a voice transcript:
//   {"type":"user","name":"..."} or {"type":"transcript",...}
// - Audio Transmission (ESP -> Server):
//   The ESP sends recorded audio as a binary frame formatted as:
//   [1-byte Type (0x01)][16-byte IV][AES-CBC Encrypted PCM]
// - Audio Reception (Server -> ESP):
//   The server sends generated audio as a binary frame formatted as:
//   [16-byte IV][AES-CBC Encrypted PCM]
//
// To handle large audio responses that exceed a single WebSocket frame,
// the client accumulates fragmented binary frames into an accumulation buffer.

#include "ws_client.h"
#include "config.h"
#include "crypto.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_websocket_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ws_client";

// Event group bits used to track connection state
#define WS_BIT_CONNECTED  BIT0
#define WS_BIT_SESSION    BIT1

// FreeRTOS event group for signaling connection and session states
static EventGroupHandle_t            s_eg     = NULL;

// Handle for the active WebSocket client
static esp_websocket_client_handle_t s_client = NULL;

// The 32-byte AES session key used for audio encryption and decryption
static uint8_t s_session_key[CRYPTO_KEY_LEN] = {0};

// The unique string ID identifying the current session
static char    s_session_id[64]              = {0};

// Flag to track whether a valid session has been established
static bool    s_has_session                 = false;

// Callbacks registered during initialization to handle incoming audio and text messages
static ws_audio_cb_t s_audio_cb = NULL;
static ws_text_cb_t  s_text_cb  = NULL;

// Buffer allocated in PSRAM for accumulating fragmented binary WebSocket frames
static uint8_t *s_accum     = NULL;
static size_t   s_accum_len = 0;

// Buffer allocated in PSRAM to hold the decrypted plaintext audio
static uint8_t *s_plain     = NULL;

// Simple JSON string extractor to parse specific keys from incoming text frames.
// It searches for the key, extracts the string value enclosed in quotes, and copies it to out.
// Returns true if the key was found and successfully extracted.
static bool json_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    
    // Find the key in the JSON string
    const char *p = strstr(json, search);
    if (!p) return false;
    
    // Move the pointer past the key and the opening quote
    p += strlen(search);
    
    // Find the closing quote of the value
    const char *end = strchr(p, '"');
    if (!end) return false;
    
    // Calculate the length of the value and ensure it fits in the output buffer
    size_t n = (size_t)(end - p);
    if (n >= out_sz) n = out_sz - 1;
    
    // Copy the value and null-terminate it
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// Processes an incoming session JSON message from the server.
// It extracts the session ID and the Base64-encoded AES key, decodes the key,
// and sets the appropriate event group bits to indicate the session is ready.
static void handle_session(const char *json)
{
    char key_b64[64] = {0};
    
    // Extract the session ID and the Base64 key
    if (!json_str(json, "session_id", s_session_id, sizeof(s_session_id)) ||
        !json_str(json, "key_b64",   key_b64,      sizeof(key_b64))) {
        ESP_LOGE(TAG, "Session JSON parse error: %.100s", json);
        return;
    }

    size_t decoded = 0;
    unsigned char tmp[CRYPTO_KEY_LEN + 4];
    
    // Decode the Base64 AES key
    int r = mbedtls_base64_decode(tmp, sizeof(tmp), &decoded,
                                   (const unsigned char *)key_b64,
                                   strlen(key_b64));
                                   
    // Verify that the decoding was successful and the key is the correct length
    if (r != 0 || decoded != CRYPTO_KEY_LEN) {
        ESP_LOGE(TAG, "Base64 decode error: r=%d decoded=%zu", r, decoded);
        return;
    }
    
    // Copy the decoded key into the global session key buffer
    memcpy(s_session_key, tmp, CRYPTO_KEY_LEN);
    s_has_session = true;
    
    // Signal that the session is fully established
    xEventGroupSetBits(s_eg, WS_BIT_SESSION);
    ESP_LOGI(TAG, "Session ready: id=%s", s_session_id);
}

// Processes a complete binary audio response assembled from WebSocket frames.
// The data format is expected to be: 16 bytes IV followed by the AES-CBC encrypted PCM data.
static void handle_binary_complete(const uint8_t *data, size_t len)
{
    // Validate that the message is long enough to contain the IV and at least one AES block
    if (len < CRYPTO_IV_LEN + CRYPTO_BLOCK) {
        ESP_LOGW(TAG, "Binary response is too short: %zu bytes", len);
        return;
    }

    // Decrypt the audio data
    esp_err_t err = crypto_decrypt(s_session_key,
                                    data,                  // The IV is at the beginning of the data
                                    data + CRYPTO_IV_LEN,  // The encrypted data starts after the IV
                                    len  - CRYPTO_IV_LEN,  // The length of the encrypted data
                                    s_plain);              // The output buffer for the plaintext PCM
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio decryption error");
        return;
    }

    // Calculate the length of the decrypted PCM data
    size_t pcm_len = len - CRYPTO_IV_LEN;
    ESP_LOGI(TAG, "Audio decrypted: %zu bytes of PCM", pcm_len);
    
    // Pass the decrypted audio to the registered callback
    if (s_audio_cb) s_audio_cb(s_plain, pcm_len);
}

// Event handler for WebSocket events.
// This function processes connection state changes and incoming data frames.
static void ws_event(void *arg, esp_event_base_t base,
                      int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ev = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {

    case WEBSOCKET_EVENT_CONNECTED:
        // Handle successful connection to the server
        ESP_LOGI(TAG, "WebSocket connected");
        s_accum_len = 0;
        xEventGroupSetBits(s_eg, WS_BIT_CONNECTED);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        // Handle disconnection and clean up session state
        ESP_LOGW(TAG, "WebSocket disconnected");
        xEventGroupClearBits(s_eg, WS_BIT_CONNECTED | WS_BIT_SESSION);
        s_has_session = false;
        s_accum_len   = 0;
        break;

    case WEBSOCKET_EVENT_DATA:
        // Validate that the data pointer and length are valid
        if (!ev || !ev->data_ptr || ev->data_len <= 0) break;

        // Opcode 0x01 indicates a text frame
        if (ev->op_code == 0x01) {
            char json[512] = {0};
            
            // Copy the incoming text data into a null-terminated buffer
            size_t cp = (ev->data_len < 511) ? ev->data_len : 511;
            memcpy(json, ev->data_ptr, cp);

            char type_val[32] = {0};
            
            // Check the type of the JSON message
            if (json_str(json, "type", type_val, sizeof(type_val))) {
                if (strcmp(type_val, "session") == 0) {
                    // Handle session initialization messages
                    handle_session(json);
                } else if (s_text_cb) {
                    // Pass any other JSON messages to the registered text callback
                    s_text_cb(json, ev->data_len);
                }
            }

        // Opcode 0x02 indicates a binary frame, and 0x00 indicates a continuation frame
        } else if (ev->op_code == 0x02 || ev->op_code == 0x00) {
            
            if (!s_accum) break;

            // If this is the start of a new binary message, reset the accumulation buffer length
            if (ev->op_code == 0x02 && ev->payload_offset == 0) {
                s_accum_len = 0;
            }

            // Append the incoming chunk to the accumulation buffer, making sure we don't overflow
            if (s_accum_len + (size_t)ev->data_len <= RESP_BUF_SIZE) {
                memcpy(s_accum + s_accum_len, ev->data_ptr, ev->data_len);
                s_accum_len += ev->data_len;
            } else {
                ESP_LOGW(TAG, "Buffer full, skipping frame");
                s_accum_len = 0;
                break;
            }

            // Check if the current frame is the final one and if we have received the full payload
            bool complete = ev->fin &&
                            (ev->payload_len == 0 ||
                             s_accum_len >= (size_t)ev->payload_len);
                             
            if (complete) {
                // Once the entire binary message is received, process it
                handle_binary_complete(s_accum, s_accum_len);
                s_accum_len = 0;
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

// General API Functions

// Initializes the WebSocket client, allocates necessary memory, and starts the connection process.
esp_err_t ws_client_init(ws_audio_cb_t audio_cb, ws_text_cb_t text_cb)
{
    s_audio_cb = audio_cb;
    s_text_cb  = text_cb;

    // Create the event group to track connection and session states
    s_eg = xEventGroupCreate();
    if (!s_eg) return ESP_ERR_NO_MEM;

    // Allocate memory in PSRAM for the accumulation buffer and the plaintext output buffer.
    // PSRAM is used because the audio responses can be very large.
    s_accum = heap_caps_malloc(RESP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    s_plain = heap_caps_malloc(RESP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_accum || !s_plain) {
        ESP_LOGE(TAG, "PSRAM allocation error");
        return ESP_ERR_NO_MEM;
    }

    // Configure the WebSocket client
    esp_websocket_client_config_t cfg = {
        .uri                  = SERVER_WS_URI,
        .buffer_size          = 8192,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
    };

    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) return ESP_FAIL;

    // Register the event handler to process all WebSocket events
    ESP_ERROR_CHECK(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                                   ws_event, NULL));
                                                   
    // Start the connection process
    ESP_ERROR_CHECK(esp_websocket_client_start(s_client));

    ESP_LOGI(TAG, "WebSocket started: %s", SERVER_WS_URI);
    return ESP_OK;
}

// Blocks the caller until a session is established or the timeout expires.
esp_err_t ws_client_wait_session(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_eg,
                           WS_BIT_CONNECTED | WS_BIT_SESSION,
                           pdFALSE, pdTRUE,
                           pdMS_TO_TICKS(timeout_ms));
                           
    if ((bits & (WS_BIT_CONNECTED | WS_BIT_SESSION)) ==
               (WS_BIT_CONNECTED | WS_BIT_SESSION))
        return ESP_OK;
        
    ESP_LOGE(TAG, "Session timeout (%lu ms)", (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

// Sends an RFID UID to the server formatted as a JSON string.
esp_err_t ws_client_send_rfid(const char *uid_str)
{
    if (!ws_client_is_connected()) return ESP_ERR_INVALID_STATE;
    
    char json[128];
    int n = snprintf(json, sizeof(json),
        "{\"type\":\"rfid\",\"uid\":\"%s\",\"session_id\":\"%s\"}",
        uid_str, s_session_id);
        
    // Send the JSON message as a text frame
    return esp_websocket_client_send_text(s_client, json, n,
                                           pdMS_TO_TICKS(3000)) >= 0
        ? ESP_OK : ESP_FAIL;
}

// Packages the initialization vector and the encrypted audio data into a binary frame and sends it.
esp_err_t ws_client_send_audio(const uint8_t *iv,
                                const uint8_t *cipher, size_t cipher_len)
{
    if (!ws_client_is_connected() || !s_has_session)
        return ESP_ERR_INVALID_STATE;

    // The total frame size is 1 byte for the type, 16 bytes for the IV, and the length of the ciphertext.
    size_t total = 1 + CRYPTO_IV_LEN + cipher_len;
    
    // Allocate temporary memory in PSRAM to build the final frame before sending
    uint8_t *frame = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
    if (!frame) return ESP_ERR_NO_MEM;

    // Construct the frame
    frame[0] = WS_FRAME_TYPE_AUDIO;
    memcpy(frame + 1,                  iv,     CRYPTO_IV_LEN);
    memcpy(frame + 1 + CRYPTO_IV_LEN, cipher, cipher_len);

    // Transmit the binary frame to the server
    int r = esp_websocket_client_send_bin(s_client, (const char *)frame,
                                           (int)total, pdMS_TO_TICKS(15000));
    
    // Free the temporary buffer
    free(frame);

    if (r < 0) { 
        ESP_LOGE(TAG, "Failed to send audio"); 
        return ESP_FAIL; 
    }
    
    ESP_LOGI(TAG, "Audio sent: %zu bytes", total);
    return ESP_OK;
}

// Sends raw text data (like JSON telemetry) to the server.
esp_err_t ws_client_send_text(const char *json)
{
    if (!ws_client_is_connected()) return ESP_ERR_INVALID_STATE;
    int n = (int)strlen(json);
    return esp_websocket_client_send_text(s_client, json, n,
                                           pdMS_TO_TICKS(3000)) >= 0
        ? ESP_OK : ESP_FAIL;
}

// Provides read-only access to the AES session key.
const uint8_t *ws_client_get_session_key(void)
{
    return s_has_session ? s_session_key : NULL;
}

// Returns whether the WebSocket is actively connected to the server.
bool ws_client_is_connected(void)
{
    return s_eg && (xEventGroupGetBits(s_eg) & WS_BIT_CONNECTED);
}

// Returns whether a valid session has been initialized.
bool ws_client_has_session(void)
{
    return s_has_session;
}

// Stops the client, frees memory, and deletes synchronization primitives.
void ws_client_deinit(void)
{
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    if (s_accum) { free(s_accum); s_accum = NULL; }
    if (s_plain)  { free(s_plain);  s_plain  = NULL; }
    if (s_eg)     { vEventGroupDelete(s_eg); s_eg = NULL; }
}
