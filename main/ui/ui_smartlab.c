// SmartLab TFT UI implementation
// ILI9341 240x320, LVGL 8.3, black theme with colorful accents

#include "ui_smartlab.h"
#include "lvgl.h"
#include "config.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

// Color palette definitions for UI elements
#define CLR_BG       lv_color_hex(0x0A0A0F)   // Main background color
#define CLR_SURF     lv_color_hex(0x12121E)   // Card surface color
#define CLR_SURF2    lv_color_hex(0x1A1A2E)   // Lighter card surface color
#define CLR_WHITE    lv_color_hex(0xFFFFFF)   // Standard white text color
#define CLR_GREY     lv_color_hex(0x7A8899)   // Standard grey text color
#define CLR_GREY2    lv_color_hex(0x3A4455)   // Darker grey for borders and lines
#define CLR_CYAN     lv_color_hex(0x00E5FF)   // Cyan accent color
#define CLR_GREEN    lv_color_hex(0x00E676)   // Green accent color for success states
#define CLR_YELLOW   lv_color_hex(0xFFD740)   // Yellow accent color for warnings or active states
#define CLR_RED      lv_color_hex(0xFF1744)   // Red accent color for errors or critical alerts
#define CLR_ORANGE   lv_color_hex(0xFF6D00)   // Orange accent color for processing states
#define CLR_BLUE     lv_color_hex(0x2979FF)   // Blue accent color
#define CLR_MAGENTA  lv_color_hex(0xE040FB)   // Magenta accent color for audio output

// Spinner animation frames
static const char *SPINNER[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
// Current index for spinner frame
static uint8_t      s_spin_idx  = 0;
// Timer handle for animating the spinner
static lv_timer_t  *s_spin_timer = NULL;
// Label object used for the spinner
static lv_obj_t    *s_spin_lbl   = NULL;

// Timer callback to update the spinner animation frame
static void spinner_cb(lv_timer_t *t)
{
    if (s_spin_lbl && lv_obj_is_valid(s_spin_lbl)) {
        lv_label_set_text(s_spin_lbl, SPINNER[s_spin_idx % 10]);
        s_spin_idx++;
    }
}

// Creates a clean container box with zero border and padding
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

// Creates a card container with a specified background and border color
static lv_obj_t *mk_card(lv_obj_t *parent, int16_t w, int16_t h,
                          lv_color_t bg, lv_color_t border, int16_t r)
{
    lv_obj_t *o = mk_box(parent, w, h, bg, r);
    lv_obj_set_style_border_color(o, border, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_70, 0);
    return o;
}

// Creates a centered text label with the specified font and color
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

// Draws the top header bar which is common across all screens
static void draw_header(const char *state_label, lv_color_t dot_color)
{
    lv_obj_t *scr = lv_scr_act();

    // Header background container
    lv_obj_t *hdr = mk_box(scr, 240, 38, CLR_SURF, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);

    // Bottom separator line for the header
    lv_obj_t *line = mk_box(scr, 240, 1, CLR_GREY2, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 38);

    // Application title label, left-aligned
    lv_obj_t *app_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(app_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(app_lbl, CLR_CYAN, 0);
    lv_obj_set_style_bg_opa(app_lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(app_lbl, "SmartLab");
    lv_obj_align(app_lbl, LV_ALIGN_LEFT_MID, 10, 0);

    // Status label indicating the current state, right-aligned
    lv_obj_t *st_lbl = lv_label_create(hdr);
    lv_obj_set_style_text_font(st_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(st_lbl, dot_color, 0);
    lv_obj_set_style_bg_opa(st_lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(st_lbl, state_label);
    lv_obj_align(st_lbl, LV_ALIGN_RIGHT_MID, -10, 0);
}

// Draws a circular icon with an outer glow effect
static lv_obj_t *draw_icon_circle(lv_color_t bg, lv_color_t border,
                                   const char *icon, lv_color_t icon_color,
                                   int16_t y_ofs)
{
    lv_obj_t *scr = lv_scr_act();

    // Outer glow ring behind the main circle
    lv_obj_t *glow = mk_box(scr, 92, 92, CLR_BG, 46);
    lv_obj_set_style_border_color(glow, border, 0);
    lv_obj_set_style_border_width(glow, 1, 0);
    lv_obj_set_style_border_opa(glow, LV_OPA_30, 0);
    lv_obj_align(glow, LV_ALIGN_CENTER, 0, y_ofs - 1);

    // Inner circle container holding the icon
    lv_obj_t *circle = mk_card(scr, 80, 80, bg, border, 40);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, y_ofs);

    // Icon symbol placed inside the circle
    lv_obj_t *ico = lv_label_create(circle);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ico, icon_color, 0);
    lv_obj_set_style_bg_opa(ico, LV_OPA_TRANSP, 0);
    lv_label_set_text(ico, icon);
    lv_obj_center(ico);

    return circle;
}

// Draws the bottom footer information strip
static void draw_footer(const char *text, lv_color_t color)
{
    lv_obj_t *scr = lv_scr_act();

    // Separator line above the footer text
    lv_obj_t *line = mk_box(scr, 240, 1, CLR_GREY2, 0);
    lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, -28);

    // Footer text label
    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// Initializes the active screen with the default background styling
void ui_smartlab_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

// Switches the UI to the requested screen state and renders its components
void ui_smartlab_show(screen_id_t id, const char *msg)
{
    // Clean up any running spinner animation before switching screens
    if (s_spin_timer) { lv_timer_del(s_spin_timer); s_spin_timer = NULL; }
    s_spin_lbl = NULL;

    // Reset the screen objects and background
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    switch (id) {

    // Idle state: waiting for the user to swipe their RFID card
    case SCREEN_IDLE: {
        draw_header("IDLE", CLR_GREY);

        // Decorative background card
        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_GREY2, 16);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(CLR_SURF2, CLR_CYAN, LV_SYMBOL_WIFI, CLR_CYAN, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE, "SmartLab Assistant");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "Waiting for ID card...");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer("v2.0  |  IoT Lab", CLR_GREY);
        break;
    }

    // RFID read state: displays a welcome message after a successful scan
    case SCREEN_RFID_READ: {
        draw_header("RFID", CLR_GREEN);

        // Green glow background effect indicating success
        lv_obj_t *glow_bg = mk_box(scr, 240, 320, CLR_BG, 0);
        lv_obj_set_style_bg_color(glow_bg, lv_color_hex(0x00100A), 0);
        lv_obj_align(glow_bg, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_GREEN, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_40, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x0A1F0F), CLR_GREEN,
                         LV_SYMBOL_OK, CLR_GREEN, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  msg ? msg : "Welcome!");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREEN,
                                "Identity verified");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer(LV_SYMBOL_OK "  Login successful", CLR_GREEN);
        break;
    }

    // Ready state: system is authenticated and waiting for user input via PTT
    case SCREEN_READY: {
        draw_header("READY", CLR_CYAN);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_CYAN, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_20, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x001820), CLR_CYAN,
                         LV_SYMBOL_AUDIO, CLR_CYAN, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  msg ? msg : "Ready");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                "Press PTT to ask");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        char url_buf[48];
        snprintf(url_buf, sizeof(url_buf), LV_SYMBOL_HOME "  %s:%d", SERVER_HOST, SERVER_PORT);
        lv_obj_t *url_lbl = mk_lbl(scr, &lv_font_montserrat_14, CLR_CYAN, url_buf);
        lv_obj_align(url_lbl, LV_ALIGN_BOTTOM_MID, 0, -28);

        draw_footer(LV_SYMBOL_WIFI "  Connected", CLR_CYAN);
        break;
    }

    // Recording state: user is holding PTT and speaking
    case SCREEN_RECORDING: {
        draw_header("RECORDING", CLR_YELLOW);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_YELLOW, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_30, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x1A1400), CLR_YELLOW,
                         LV_SYMBOL_AUDIO, CLR_YELLOW, -30);

        // Recording (REC) badge indicator in red
        lv_obj_t *rec = mk_card(scr, 60, 24, lv_color_hex(0x1A0500), CLR_RED, 12);
        lv_obj_align(rec, LV_ALIGN_CENTER, 0, 12);
        lv_obj_t *rec_lbl = lv_label_create(rec);
        lv_obj_set_style_text_font(rec_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rec_lbl, CLR_RED, 0);
        lv_obj_set_style_bg_opa(rec_lbl, LV_OPA_TRANSP, 0);
        lv_label_set_text(rec_lbl, "● REC");
        lv_obj_center(rec_lbl);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  "Listening...");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 44);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                "Release to send");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 68);

        draw_footer("Release PTT → send", CLR_YELLOW);
        break;
    }

    // Processing state: audio sent to server, waiting for response
    case SCREEN_PROCESSING: {
        draw_header("AI", CLR_ORANGE);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_ORANGE, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_20, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        // Spinner circle container for the waiting animation
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
                                  "Thinking...");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "AI is processing");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        // Start the spinner animation timer
        s_spin_timer = lv_timer_create(spinner_cb, 150, NULL);
        draw_footer("Please wait...", CLR_ORANGE);
        break;
    }

    // Speaking state: playing the received audio response
    case SCREEN_SPEAKING: {
        draw_header("SPEAKING", CLR_MAGENTA);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, CLR_SURF, CLR_MAGENTA, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_20, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x130A1A), CLR_MAGENTA,
                         LV_SYMBOL_VOLUME_MAX, CLR_MAGENTA, -30);

        // Visual sound wave effect consisting of 3 vertical bars
        for (int i = 0; i < 3; i++) {
            int16_t heights[] = {10, 18, 10};
            lv_obj_t *bar = mk_box(scr, 6, heights[i], CLR_MAGENTA, 3);
            lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
            lv_obj_align(bar, LV_ALIGN_CENTER, (i - 1) * 14, 8);
        }

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_WHITE,
                                  "Replying");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 38);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "...");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 64);

        draw_footer(LV_SYMBOL_VOLUME_MAX "  Playing audio", CLR_MAGENTA);
        break;
    }

    // Smoke alert state: critical warning triggered by sensor
    case SCREEN_SMOKE_ALERT: {
        draw_header("! ALARM !", CLR_RED);

        // Red alert background to capture attention
        lv_obj_t *alert_bg = mk_box(scr, 240, 282, lv_color_hex(0x0F0000), 0);
        lv_obj_align(alert_bg, LV_ALIGN_BOTTOM_MID, 0, 0);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, lv_color_hex(0x1A0000), CLR_RED, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_80, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x200000), CLR_RED,
                         LV_SYMBOL_WARNING, CLR_RED, -35);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_RED,
                                  "SMOKE ALARM!");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 25);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_YELLOW,
                                msg ? msg : "Fan active - Ventilating");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 52);

        // Bold red warning stripe at the bottom
        lv_obj_t *warn_bar = mk_box(scr, 240, 26, CLR_RED, 0);
        lv_obj_align(warn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_t *warn_lbl = lv_label_create(warn_bar);
        lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(warn_lbl, CLR_WHITE, 0);
        lv_obj_set_style_bg_opa(warn_lbl, LV_OPA_TRANSP, 0);
        lv_label_set_text(warn_lbl, "EVACUATE THE AREA!");
        lv_obj_center(warn_lbl);
        break;
    }

    // Error state: system encountered an unrecoverable issue
    case SCREEN_ERROR: {
        draw_header("ERROR", CLR_RED);

        lv_obj_t *bg_card = mk_card(scr, 200, 200, lv_color_hex(0x110000), CLR_RED, 16);
        lv_obj_set_style_border_opa(bg_card, LV_OPA_50, 0);
        lv_obj_align(bg_card, LV_ALIGN_CENTER, 0, 10);

        draw_icon_circle(lv_color_hex(0x1A0000), CLR_RED,
                         LV_SYMBOL_CLOSE, CLR_RED, -30);

        lv_obj_t *title = mk_lbl(scr, &lv_font_montserrat_20, CLR_RED, "Error");
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 30);

        lv_obj_t *sub = mk_lbl(scr, &lv_font_montserrat_16, CLR_GREY,
                                msg ? msg : "Unknown error");
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 58);

        draw_footer("Please restart", CLR_RED);
        break;
    }

    default:
        break;
    }
}
