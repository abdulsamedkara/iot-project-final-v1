#pragma once
#include "esp_err.h"
#include <stdbool.h>

/**
 * SmartLab ekran durumları
 */
typedef enum {
    SCREEN_IDLE = 0,      // "Kart okutun" — bekleme
    SCREEN_RFID_READ,     // "Hoşgeldin [ad]"
    SCREEN_READY,         // "PTT'ye basarak sorunuzu sorun"
    SCREEN_RECORDING,     // Mikrofon + "Dinliyorum..."
    SCREEN_PROCESSING,    // Spinner + "Düşünüyor..."
    SCREEN_SPEAKING,      // Hoparlör + cevap metni
    SCREEN_SMOKE_ALERT,   // Kırmızı + "DUMAN TESPİT EDİLDİ!"
    SCREEN_ERROR,         // Hata mesajı
} screen_id_t;

/**
 * @brief  LVGL ve ILI9341 ekranı başlatır.
 */
esp_err_t display_init(void);

/**
 * @brief  Ekran durumunu değiştirir.
 * @param  id  Yeni ekran durumu
 * @param  msg İsteğe bağlı alt mesaj (NULL geçilebilir)
 */
void display_switch(screen_id_t id, const char *msg);

/**
 * @brief  LVGL mutex alır — UI güncellemeden önce çağır.
 */
bool display_lock(int timeout_ms);

/**
 * @brief  LVGL mutex bırakır.
 */
void display_unlock(void);
