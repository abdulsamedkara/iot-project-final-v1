#pragma once
#include "display.h"

/**
 * @brief  Tüm LVGL ekranlarını oluşturur (bir kez çağrılır).
 */
void ui_smartlab_init(void);

/**
 * @brief  Belirtilen ekran durumuna geçer ve isteğe bağlı mesajı gösterir.
 * @param  id   Ekran durumu
 * @param  msg  Ek mesaj (NULL geçilebilir)
 */
void ui_smartlab_show(screen_id_t id, const char *msg);
