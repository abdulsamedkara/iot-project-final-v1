// ws_client.c — WebSocket istemcisi
// esp_websocket_client (ESP-IDF 5.x) kullanır.
//
// Protokol:
//   BAĞLANTI → Sunucu text: {"type":"session","session_id":"...","key_b64":"..."}
//   ESP→Sunucu text:        {"type":"rfid","uid":"AABBCCDD","session_id":"..."}
//   Sunucu→ESP text:        {"type":"user","name":"..."} veya {"type":"transcript",...}
//   ESP→Sunucu binary:      [0x01][IV 16B][AES-CBC(PCM)]
//   Sunucu→ESP binary:      [IV 16B][AES-CBC(PCM yanıt)]
//
// Büyük ses yanıtları birden fazla WebSocket frame'e bölünebilir.
// payload_len / payload_offset ile accumulation buffer kullanılır.

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

// ─── Event bitleri ────────────────────────────────────────────────────────────
#define WS_BIT_CONNECTED  BIT0
#define WS_BIT_SESSION    BIT1

static EventGroupHandle_t            s_eg     = NULL;
static esp_websocket_client_handle_t s_client = NULL;

static uint8_t s_session_key[CRYPTO_KEY_LEN] = {0};
static char    s_session_id[64]              = {0};
static bool    s_has_session                 = false;

static ws_audio_cb_t s_audio_cb = NULL;
static ws_text_cb_t  s_text_cb  = NULL;

// ─── Accumulation buffer (PSRAM) — parçalı binary frame'ler için ─────────────
static uint8_t *s_accum     = NULL;
static size_t   s_accum_len = 0;
static uint8_t *s_plain     = NULL;

// ─── Basit JSON string ayıklayıcı ────────────────────────────────────────────
static bool json_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t n = (size_t)(end - p);
    if (n >= out_sz) n = out_sz - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// ─── Session JSON işle ───────────────────────────────────────────────────────
static void handle_session(const char *json)
{
    char key_b64[64] = {0};
    if (!json_str(json, "session_id", s_session_id, sizeof(s_session_id)) ||
        !json_str(json, "key_b64",   key_b64,      sizeof(key_b64))) {
        ESP_LOGE(TAG, "Session JSON parse hatasi: %.100s", json);
        return;
    }

    size_t decoded = 0;
    unsigned char tmp[CRYPTO_KEY_LEN + 4];
    int r = mbedtls_base64_decode(tmp, sizeof(tmp), &decoded,
                                   (const unsigned char *)key_b64,
                                   strlen(key_b64));
    if (r != 0 || decoded != CRYPTO_KEY_LEN) {
        ESP_LOGE(TAG, "Base64 decode hatasi: r=%d decoded=%zu", r, decoded);
        return;
    }
    memcpy(s_session_key, tmp, CRYPTO_KEY_LEN);
    s_has_session = true;
    xEventGroupSetBits(s_eg, WS_BIT_SESSION);
    ESP_LOGI(TAG, "Session hazir: id=%s", s_session_id);
}

// ─── Tam binary yanıtı işle ──────────────────────────────────────────────────
// Format: IV(16) | AES-CBC(PCM)
static void handle_binary_complete(const uint8_t *data, size_t len)
{
    if (len < CRYPTO_IV_LEN + CRYPTO_BLOCK) {
        ESP_LOGW(TAG, "Binary yanit cok kisa: %zu byte", len);
        return;
    }

    esp_err_t err = crypto_decrypt(s_session_key,
                                    data,
                                    data + CRYPTO_IV_LEN,
                                    len  - CRYPTO_IV_LEN,
                                    s_plain);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ses desifre hatasi");
        return;
    }

    size_t pcm_len = len - CRYPTO_IV_LEN;
    ESP_LOGI(TAG, "Ses desifre edildi: %zu byte PCM", pcm_len);
    if (s_audio_cb) s_audio_cb(s_plain, pcm_len);
}

// ─── WebSocket event handler ──────────────────────────────────────────────────
static void ws_event(void *arg, esp_event_base_t base,
                      int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *ev = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {

    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket baglandi");
        s_accum_len = 0;
        xEventGroupSetBits(s_eg, WS_BIT_CONNECTED);
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket baglantisi kesildi");
        xEventGroupClearBits(s_eg, WS_BIT_CONNECTED | WS_BIT_SESSION);
        s_has_session = false;
        s_accum_len   = 0;
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!ev || !ev->data_ptr || ev->data_len <= 0) break;

        if (ev->op_code == 0x01) {
            // ── Text frame ───────────────────────────────────────────────────
            char json[512] = {0};
            size_t cp = (ev->data_len < 511) ? ev->data_len : 511;
            memcpy(json, ev->data_ptr, cp);

            char type_val[32] = {0};
            if (json_str(json, "type", type_val, sizeof(type_val))) {
                if (strcmp(type_val, "session") == 0) {
                    handle_session(json);
                } else if (s_text_cb) {
                    s_text_cb(json, ev->data_len);
                }
            }

        } else if (ev->op_code == 0x02 || ev->op_code == 0x00) {
            // ── Binary frame (ilk parça 0x02, devam parçası 0x00) ────────────
            if (!s_accum) break;

            if (ev->op_code == 0x02 && ev->payload_offset == 0) {
                // Yeni mesaj başlıyor
                s_accum_len = 0;
            }

            if (s_accum_len + (size_t)ev->data_len <= RESP_BUF_SIZE) {
                memcpy(s_accum + s_accum_len, ev->data_ptr, ev->data_len);
                s_accum_len += ev->data_len;
            } else {
                ESP_LOGW(TAG, "Buffer dolu, frame atlaniyor");
                s_accum_len = 0;
                break;
            }

            // Mesaj tamamlandı mı?
            bool complete = ev->fin &&
                            (ev->payload_len == 0 ||
                             s_accum_len >= (size_t)ev->payload_len);
            if (complete) {
                handle_binary_complete(s_accum, s_accum_len);
                s_accum_len = 0;
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket hata");
        break;

    default:
        break;
    }
}

// ─── Genel API ────────────────────────────────────────────────────────────────
esp_err_t ws_client_init(ws_audio_cb_t audio_cb, ws_text_cb_t text_cb)
{
    s_audio_cb = audio_cb;
    s_text_cb  = text_cb;

    s_eg = xEventGroupCreate();
    if (!s_eg) return ESP_ERR_NO_MEM;

    s_accum = heap_caps_malloc(RESP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    s_plain = heap_caps_malloc(RESP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!s_accum || !s_plain) {
        ESP_LOGE(TAG, "PSRAM tahsis hatasi");
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_config_t cfg = {
        .uri                  = SERVER_WS_URI,
        .buffer_size          = 8192,
        .reconnect_timeout_ms = 5000,
        .network_timeout_ms   = 10000,
    };

    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) return ESP_FAIL;

    ESP_ERROR_CHECK(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY,
                                                   ws_event, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(s_client));

    ESP_LOGI(TAG, "WebSocket baslatildi: %s", SERVER_WS_URI);
    return ESP_OK;
}

esp_err_t ws_client_wait_session(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_eg,
                           WS_BIT_CONNECTED | WS_BIT_SESSION,
                           pdFALSE, pdTRUE,
                           pdMS_TO_TICKS(timeout_ms));
    if ((bits & (WS_BIT_CONNECTED | WS_BIT_SESSION)) ==
               (WS_BIT_CONNECTED | WS_BIT_SESSION))
        return ESP_OK;
    ESP_LOGE(TAG, "Session zaman asimi (%lu ms)", (unsigned long)timeout_ms);
    return ESP_ERR_TIMEOUT;
}

esp_err_t ws_client_send_rfid(const char *uid_str)
{
    if (!ws_client_is_connected()) return ESP_ERR_INVALID_STATE;
    char json[128];
    int n = snprintf(json, sizeof(json),
        "{\"type\":\"rfid\",\"uid\":\"%s\",\"session_id\":\"%s\"}",
        uid_str, s_session_id);
    return esp_websocket_client_send_text(s_client, json, n,
                                           pdMS_TO_TICKS(3000)) >= 0
        ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_client_send_audio(const uint8_t *iv,
                                const uint8_t *cipher, size_t cipher_len)
{
    if (!ws_client_is_connected() || !s_has_session)
        return ESP_ERR_INVALID_STATE;

    // Frame: [0x01][IV 16B][cipher]
    size_t total = 1 + CRYPTO_IV_LEN + cipher_len;
    uint8_t *frame = heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!frame) return ESP_ERR_NO_MEM;

    frame[0] = WS_FRAME_TYPE_AUDIO;
    memcpy(frame + 1,                  iv,     CRYPTO_IV_LEN);
    memcpy(frame + 1 + CRYPTO_IV_LEN, cipher, cipher_len);

    int r = esp_websocket_client_send_bin(s_client, (const char *)frame,
                                           (int)total, pdMS_TO_TICKS(15000));
    free(frame);

    if (r < 0) { ESP_LOGE(TAG, "Ses gonderilemedi"); return ESP_FAIL; }
    ESP_LOGI(TAG, "Ses gonderildi: %zu byte", total);
    return ESP_OK;
}

const uint8_t *ws_client_get_session_key(void)
{
    return s_has_session ? s_session_key : NULL;
}

bool ws_client_is_connected(void)
{
    return s_eg && (xEventGroupGetBits(s_eg) & WS_BIT_CONNECTED);
}

bool ws_client_has_session(void)
{
    return s_has_session;
}

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
