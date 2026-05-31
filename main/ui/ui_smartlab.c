// ui_smartlab.c — SmartLab LVGL ekran state machine
// Tek dosyada tüm ekran durumları: sade, kolayca genişletilebilir.

#include "ui_smartlab.h"
#include "lvgl.h"
#include <string.h>

// ─── Renk paleti — siyah arka plan, yüksek kontrast ──────────────────────────
#define CLR_BG_BLACK    lv_color_hex(0x000000)  // Tüm durumlar
#define CLR_WHITE       lv_color_hex(0xFFFFFF)  // Başlık
#define CLR_GREY        lv_color_hex(0x9E9E9E)  // Alt text
#define CLR_CYAN        lv_color_hex(0x00E5FF)  // Hazır / konuşuyor
#define CLR_GREEN       lv_color_hex(0x69F0AE)  // Hoşgeldin
#define CLR_YELLOW      lv_color_hex(0xFFEA00)  // Kayıt / ikon
#define CLR_RED         lv_color_hex(0xFF1744)  // Hata / duman
#define CLR_ORANGE      lv_color_hex(0xFF6D00)  // İşleniyor

// ─── LVGL nesneleri ──────────────────────────────────────────────────────────
static lv_obj_t *s_screen    = NULL;  // Tek aktif ekran
static lv_obj_t *s_icon_lbl  = NULL;  // Büyük emoji/ikon
static lv_obj_t *s_title_lbl = NULL;  // Başlık
static lv_obj_t *s_sub_lbl   = NULL;  // Alt mesaj
static lv_obj_t *s_anim_lbl  = NULL;  // Spinner / animasyon

// Spinner karakterleri
static const char *SPINNER[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static uint8_t s_spin_idx = 0;
static lv_timer_t *s_spin_timer = NULL;

static void spinner_cb(lv_timer_t *t)
{
    if (s_anim_lbl) {
        lv_label_set_text(s_anim_lbl, SPINNER[s_spin_idx % 10]);
        s_spin_idx++;
    }
}

// ─── Yardımcı: arka plan her zaman siyah ─────────────────────────────────────
static void set_bg(lv_color_t color)
{
    (void)color;  // artık kullanılmıyor — tüm arka planlar siyah
    lv_obj_set_style_bg_color(s_screen, CLR_BG_BLACK, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
}

// ─── ui_smartlab_init ─────────────────────────────────────────────────────────
void ui_smartlab_init(void)
{
    s_screen = lv_scr_act();
    lv_obj_clean(s_screen);
    set_bg(CLR_BG_DARK);

    // Büyük ikon/emoji (üst orta)
    s_icon_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_icon_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_icon_lbl, CLR_WHITE, 0);
    lv_obj_align(s_icon_lbl, LV_ALIGN_CENTER, 0, -60);
    lv_label_set_text(s_icon_lbl, "");

    // Spinner (ikon ile aynı yerde, gerektiğinde görünür)
    s_anim_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_anim_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_anim_lbl, CLR_ACCENT, 0);
    lv_obj_align(s_anim_lbl, LV_ALIGN_CENTER, 0, -60);
    lv_label_set_text(s_anim_lbl, "");
    lv_obj_add_flag(s_anim_lbl, LV_OBJ_FLAG_HIDDEN);

    // Başlık
    s_title_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_title_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_title_lbl, CLR_WHITE, 0);
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_title_lbl, 220);
    lv_obj_set_style_text_align(s_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_CENTER, 0, 10);
    lv_label_set_text(s_title_lbl, "SmartLab");

    // Alt mesaj
    s_sub_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_sub_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_sub_lbl, CLR_ACCENT, 0);
    lv_label_set_long_mode(s_sub_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_sub_lbl, 220);
    lv_obj_set_style_text_align(s_sub_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_sub_lbl, LV_ALIGN_CENTER, 0, 50);
    lv_label_set_text(s_sub_lbl, "");
}

// ─── ui_smartlab_show ─────────────────────────────────────────────────────────
void ui_smartlab_show(screen_id_t id, const char *msg)
{
    // Spinner timer'ı her geçişte durdur, gerekirse yeniden başlatılır
    if (s_spin_timer) {
        lv_timer_del(s_spin_timer);
        s_spin_timer = NULL;
    }
    lv_obj_add_flag(s_anim_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_icon_lbl, LV_OBJ_FLAG_HIDDEN);

    switch (id) {
    // ── IDLE ────────────────────────────────────────────────────────────────
    case SCREEN_IDLE:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_CYAN,  0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_WHITE, 0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY,  0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_WIFI);
        lv_label_set_text(s_title_lbl, "SmartLab Asistan");
        lv_label_set_text(s_sub_lbl,   msg ? msg : "RFID kartinizi okutun");
        break;

    // ── RFID_READ ────────────────────────────────────────────────────────────
    case SCREEN_RFID_READ:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_GREEN, 0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_WHITE, 0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY,  0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_OK);
        lv_label_set_text(s_title_lbl, msg ? msg : "Hosgeldiniz!");
        lv_label_set_text(s_sub_lbl,   "Oturum baslatiliyor...");
        break;

    // ── READY ────────────────────────────────────────────────────────────────
    case SCREEN_READY:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_CYAN,  0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_WHITE, 0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY,  0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_AUDIO);
        lv_label_set_text(s_title_lbl, msg ? msg : "Hazir");
        lv_label_set_text(s_sub_lbl,   "PTT'ye basarak sorun");
        break;

    // ── RECORDING ────────────────────────────────────────────────────────────
    case SCREEN_RECORDING:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_YELLOW, 0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_YELLOW, 0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY,   0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_AUDIO);
        lv_label_set_text(s_title_lbl, "Dinliyorum...");
        lv_label_set_text(s_sub_lbl,   "PTT birakinca gonderilir");
        break;

    // ── PROCESSING ───────────────────────────────────────────────────────────
    case SCREEN_PROCESSING:
        set_bg(CLR_BG_BLACK);
        lv_obj_add_flag(s_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_anim_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_anim_lbl,  CLR_ORANGE, 0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_WHITE,  0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY,   0);
        lv_label_set_text(s_anim_lbl,  SPINNER[0]);
        lv_label_set_text(s_title_lbl, "Dusunuyor...");
        lv_label_set_text(s_sub_lbl,   msg ? msg : "AI isleniyor");
        s_spin_timer = lv_timer_create(spinner_cb, 200, NULL);
        break;

    // ── SPEAKING ─────────────────────────────────────────────────────────────
    case SCREEN_SPEAKING:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_CYAN,  0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_WHITE, 0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY,  0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_VOLUME_MAX);
        lv_label_set_text(s_title_lbl, "Yanitlaniyor");
        lv_label_set_text(s_sub_lbl,   msg ? msg : "...");
        break;

    // ── SMOKE_ALERT ──────────────────────────────────────────────────────────
    case SCREEN_SMOKE_ALERT:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_RED,    0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_RED,    0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_YELLOW, 0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_WARNING);
        lv_label_set_text(s_title_lbl, "DUMAN TESPIT EDILDI!");
        lv_label_set_text(s_sub_lbl,   msg ? msg : "Fan aktif");
        break;

    // ── ERROR ────────────────────────────────────────────────────────────────
    case SCREEN_ERROR:
        set_bg(CLR_BG_BLACK);
        lv_obj_set_style_text_color(s_icon_lbl,  CLR_RED,  0);
        lv_obj_set_style_text_color(s_title_lbl, CLR_RED,  0);
        lv_obj_set_style_text_color(s_sub_lbl,   CLR_GREY, 0);
        lv_label_set_text(s_icon_lbl,  LV_SYMBOL_CLOSE);
        lv_label_set_text(s_title_lbl, "Hata");
        lv_label_set_text(s_sub_lbl,   msg ? msg : "Bilinmeyen hata");
        break;

    default:
        break;
    }
}
