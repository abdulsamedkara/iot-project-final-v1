// ui_smartlab.c — SmartLab TFT UI (yeniden tasarım)
// ILI9341 240×320, LVGL 8.3, siyah tema + renkli aksanlar

#include "ui_smartlab.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

// ─── Renk paleti ──────────────────────────────────────────────────────────────
#define CLR_BG       lv_color_hex(0x0A0A0F)   // Arka plan
#define CLR_SURF     lv_color_hex(0x12121E)   // Kart yüzeyi
#define CLR_SURF2    lv_color_hex(0x1A1A2E)   // Açık kart
#define CLR_WHITE    lv_color_hex(0xFFFFFF)
#define CLR_GREY     lv_color_hex(0x7A8899)
#define CLR_GREY2    lv_color_hex(0x3A4455)
#define CLR_CYAN     lv_color_hex(0x00E5FF)
#define CLR_GREEN    lv_color_hex(0x00E676)
#define CLR_YELLOW   lv_color_hex(0xFFD740)
#define CLR_RED      lv_color_hex(0xFF1744)
#define CLR_ORANGE   lv_color_hex(0xFF6D00)
#define CLR_BLUE     lv_color_hex(0x2979FF)
#define CLR_MAGENTA  lv_color_hex(0xE040FB)

// ─── Spinner ──────────────────────────────────────────────────────────────────
static const char *SPINNER[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
static uint8_t      s_spin_idx  = 0;
static lv_timer_t  *s_spin_timer = NULL;
static lv_obj_t    *s_spin_lbl   = NULL;

static void spinner_cb(lv_timer_t *t)
{
    if (s_spin_lbl && lv_obj_is_valid(s_spin_lbl)) {
        lv_label_set_text(s_spin_lbl, SPINNER[s_spin_idx % 10]);
        s_spin_idx++;
    }
}

// ─── Yardımcılar ─────────────────────────────────────────────────────────────

// Temiz kap (border/pad sıfır)
static lv_obj_t *mk_box(lv_obj_t *parent, int16_t w, int16_t h, lv_color_t bg, int16_t r)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Kenarlıklı kap
static lv_obj_t *mk_card(lv_obj_t *parent, int16_t w, int16_t h,
                          lv_color_t bg, lv_color_t border, int16_t r)
{
    lv_obj_t *o = mk_box(parent, w, h, bg, r);
    lv_obj_set_style_border_color(o, border, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_70, 0);
    return o;
}

// Label
static lv_obj_t *mk_lbl(lv_obj_t *parent, const lv_font_t *font,
                          lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 220);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, text);
    return l;
}

// ─── Header bar (her ekranda ortak) ─────────────────────────────────────────
static void draw_header(const char *state_label, lv_color_t dot_color)
{
    lv_obj_t *scr = lv_scr_act();

    // Header arka planı
    lv_obj_t *hdr = mk_box(scr, 240, 38, CLR_SURF, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

    // Alt çizgi
    lv_obj_t *line = mk_box(scr, 240, 1, CLR_GREY2, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 38);

    // "SmartLab" — sol
    lv_obj_t *app_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(app_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(app_lbl, CLR_CYAN, 0);
    lv_obj_set_style_bg_opa(app_lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(app_lbl, "SmartLab");
    lv_obj_align(app_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    // Durum etiketi — sağ
    lv_obj_t *st_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(st_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st_lbl, dot_color, 0);
    lv_obj_set_style_bg_opa(st_lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(st_lbl, state_label);
    lv_obj_align(st_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
}

// ─── İkon dairesi ────────────────────────────────────────────────────────────
static lv_obj_t *draw_icon_circle(lv_color_t bg, lv_color_t border,
                                   const char *icon, lv_color_t icon_color,
                                   int16_t y_ofs)
{
    lv_obj_t *scr = lv_scr_act();

    // Dış glow ring
    lv_obj_t *glow = mk_box(scr, 92, 92, CLR_BG, 46);
    lv_obj_set_style_border_color(glow, border, 0);
    lv_obj_set_style_border_width(glow, 1, 0);
    lv_obj_set_style_border_opa(glow, LV_OPA_30, 0);
    lv_obj_align(glow, LV_ALIGN_CENTER, 0, y_ofs - 1);

    // İkon dairesi
    lv_obj_t *circle = mk_card(scr, 80, 80, bg, border, 40);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, y_ofs);

    // İkon sembolü
    lv_obj_t *ico = lv_label_create(circle);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ico, icon_color, 0);
    lv_obj_set_style_bg_opa(ico, LV_OPA_TRANSP, 0);
    lv_label_set_text(ico, icon);
    lv_obj_center(ico);

    return circle;
}

// ─── Alt bilgi şeridi ─────────────────────────────────────────────────────────
static void draw_footer(const char *text, lv_color_t color)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_t *line = mk_box(scr, 240, 1, CLR_GREY2, 0);
    lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, -28);

    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ─── Init ────────────────────────────────────────────────────────────────────
void ui_smartlab_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

// ─── Show ────────────────────────────────────────────────────────────────────
void ui_smartlab_show(screen_id_t id, const char *msg)
{
    if (s_spin_timer) { lv_timer_del(s_spin_timer); s_spin_timer = NULL; }
    s_spin_lbl = NULL;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    switch (id) {

    // ── IDLE ─────────────────────────────────────────────────────────────────
    case SCREEN_IDLE: {
        draw_header("IDLE", CLR_GREY);

        // Dekoratif arka plan kartı
        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_GREY2, 16);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(CLR_SURF2, CLR_CYAN, LV_SYMBOL_WIFI, CLR_CYAN, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE, "SmartLab Asistan");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "Kimlik karti bekleniyor...");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer("v2.0  |  IoT Lab", CLR_GREY);
        break;
    }

    // ── RFID_READ ────────────────────────────────────────────────────────────
    case SCREEN_RFID_READ: {
        draw_header("RFID", CLR_GREEN);

        // Yeşil glow arka plan
        lv_obj_t *glow_bg = mk_box(scr, 240, 320, CLR_BG, 0);
        lv_obj_set_style_bg_color(glow_bg, lv_color_hex(0x00100A), 0);
        lv_obj_align(glow_bg, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_GREEN, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_40, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x0A1F0F), CLR_GREEN,
                         LV_SYMBOL_OK, CLR_GREEN, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  msg ? msg : "Hosgeldiniz!");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREEN,
                                "Kimlik dogrulandi");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer(LV_SYMBOL_OK "  Giris basarili", CLR_GREEN);
        break;
    }

    // ── READY ────────────────────────────────────────────────────────────────
    case SCREEN_READY: {
        draw_header("HAZIR", CLR_CYAN);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_CYAN, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_25, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x001820), CLR_CYAN,
                         LV_SYMBOL_AUDIO, CLR_CYAN, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  msg ? msg : "Hazir");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                "PTT'ye basarak sorun");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer(LV_SYMBOL_WIFI "  Bagli", CLR_CYAN);
        break;
    }

    // ── RECORDING ────────────────────────────────────────────────────────────
    case SCREEN_RECORDING: {
        draw_header("KAYIT", CLR_YELLOW);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_YELLOW, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_35, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x1A1400), CLR_YELLOW,
                         LV_SYMBOL_AUDIO, CLR_YELLOW, -30);

        // REC badge
        lv_obj_t *rec = mk_card(scr, 60, 24, lv_color_hex(0x1A0500), CLR_RED, 12);
        lv_obj_align(rec, LV_ALIGN_CENTER, 0, 12);
        lv_obj_t *rec_lbl = lv_label_create(rec);
        lv_obj_set_style_text_font(rec_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rec_lbl, CLR_RED, 0);
        lv_obj_set_style_bg_opa(rec_lbl, LV_OPA_TRANSP, 0);
        lv_label_set_text(rec_lbl, "● REC");
        lv_obj_center(rec_lbl);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  "Dinliyorum...");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 44);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                "Birakinca gonderilir");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 68);

        draw_footer("PTT birakin → gonder", CLR_YELLOW);
        break;
    }

    // ── PROCESSING ───────────────────────────────────────────────────────────
    case SCREEN_PROCESSING: {
        draw_header("AI", CLR_ORANGE);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_ORANGE, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_25, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        // Spinner dairesi
        lv_obj_t *spin_circle = mk_card(scr, 80, 80,
                                         lv_color_hex(0x120800), CLR_ORANGE, 40);
        lv_obj_align(spin_circle, LV_ALIGN_CENTER, 0, -30);

        s_spin_lbl = lv_label_create(spin_circle);
        lv_obj_set_style_text_font(s_spin_lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_spin_lbl, CLR_ORANGE, 0);
        lv_obj_set_style_bg_opa(s_spin_lbl, LV_OPA_TRANSP, 0);
        lv_label_set_text(s_spin_lbl, SPINNER[0]);
        lv_obj_center(s_spin_lbl);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  "Dusunuyor...");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "AI isliyor");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        s_spin_timer = lv_timer_create(spinner_cb, 150, NULL);
        draw_footer("Lutfen bekleyiniz...", CLR_ORANGE);
        break;
    }

    // ── SPEAKING ─────────────────────────────────────────────────────────────
    case SCREEN_SPEAKING: {
        draw_header("KONUSUYOR", CLR_MAGENTA);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_MAGENTA, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_25, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x130A1A), CLR_MAGENTA,
                         LV_SYMBOL_VOLUME_MAX, CLR_MAGENTA, -30);

        // Ses dalgası efekti (3 çizgi)
        for (int i = 0; i < 3; i++) {
            int16_t heights[] = {10, 18, 10};
            lv_obj_t *bar = mk_box(scr, 6, heights[i], CLR_MAGENTA, 3);
            lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
            lv_obj_align(bar, LV_ALIGN_CENTER, (i - 1) * 14, 8);
        }

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  "Yanitlaniyor");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 38);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "...");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 64);

        draw_footer(LV_SYMBOL_VOLUME_MAX "  Ses cikiyor", CLR_MAGENTA);
        break;
    }

    // ── SMOKE_ALERT ──────────────────────────────────────────────────────────
    case SCREEN_SMOKE_ALERT: {
        draw_header("! ALARM !", CLR_RED);

        // Kırmızı uyarı arka planı
        lv_obj_t *alert_bg = mk_box(scr, 240, 282, lv_color_hex(0x0F0000), 0);
        lv_obj_align(alert_bg, LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, lv_color_hex(0x1A0000), CLR_RED, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_80, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x200000), CLR_RED,
                         LV_SYMBOL_WARNING, CLR_RED, -35);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_RED,
                                  "DUMAN ALARM!");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 25);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_YELLOW,
                                msg ? msg : "Fan aktif - Havalandirma");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 52);

        // Kırmızı uyarı şeridi
        lv_obj_t *warn_bar = mk_box(scr, 240, 26, CLR_RED, 0);
        lv_obj_align(warn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_t *warn_lbl = lv_label_create(warn_bar);
        lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(warn_lbl, CLR_WHITE, 0);
        lv_obj_set_style_bg_opa(warn_lbl, LV_OPA_TRANSP, 0);
        lv_label_set_text(warn_lbl, "ORTAMI TERK EDIN!");
        lv_obj_center(warn_lbl);
        break;
    }

    // ── ERROR ────────────────────────────────────────────────────────────────
    case SCREEN_ERROR: {
        draw_header("HATA", CLR_RED);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, lv_color_hex(0x110000), CLR_RED, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_50, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x1A0000), CLR_RED,
                         LV_SYMBOL_CLOSE, CLR_RED, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_RED, "Hata");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "Bilinmeyen hata");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer("Yeniden baslatin", CLR_RED);
        break;
    }

    default:
        break;
    }
}
