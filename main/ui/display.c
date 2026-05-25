// display.c — LVGL + ILI9341 başlatma ve ekran yöneticisi
// DMA callback kullanılmaz; lv_disp_flush_ready() doğrudan çağrılır.
// Bu yaklaşım tüm ESP-IDF 5.x versiyonlarında derlenir.

#include "display.h"
#include "ui_smartlab.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "display";

#define LVGL_TICK_MS  5

static SemaphoreHandle_t      s_mux   = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;

// ─── LVGL flush callback ──────────────────────────────────────────────────────
// draw_bitmap senkron tamamlanır, ardından flush_ready çağrılır.
// DMA interrupt gerekmez — versiyondan bağımsız, güvenli yaklaşım.
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    esp_lcd_panel_draw_bitmap(s_panel,
                               area->x1, area->y1,
                               area->x2 + 1, area->y2 + 1,
                               color_p);
    lv_disp_flush_ready(drv);
}

// ─── LVGL tick timer ──────────────────────────────────────────────────────────
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_MS);
}

// ─── LVGL güncelleme görevi ───────────────────────────────────────────────────
static void lvgl_task(void *arg)
{
    while (1) {
        if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── display_init ─────────────────────────────────────────────────────────────
esp_err_t display_init(void)
{
    // Arka ışık açık
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << TFT_BL_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(TFT_BL_GPIO, 1);

    // SPI panel IO (SPI_HOST daha önce spi_bus_initialize ile açılmış olmalı)
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = TFT_DC_GPIO,
        .cs_gpio_num       = TFT_CS_GPIO,
        .pclk_hz           = TFT_SPI_FREQ_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI_HOST, &io_cfg, &io));

    // ILI9341 panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = TFT_RST_GPIO,
        .rgb_endian     = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // LVGL başlat
    lv_init();

    // Çift tampon — LVGL_BUF_LINES config.h'dan gelir
    static lv_color_t buf1[TFT_WIDTH * LVGL_BUF_LINES];
    static lv_color_t buf2[TFT_WIDTH * LVGL_BUF_LINES];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_WIDTH * LVGL_BUF_LINES);

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res  = TFT_WIDTH;
    drv.ver_res  = TFT_HEIGHT;
    drv.flush_cb = lvgl_flush_cb;
    drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);

    // Tick timer (her LVGL_TICK_MS ms'de bir lv_tick_inc çağrılır)
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    // Mutex oluştur
    s_mux = xSemaphoreCreateMutex();

    // UI nesnelerini önce oluştur, sonra task'ı başlat
    ui_smartlab_init();
    display_switch(SCREEN_IDLE, NULL);

    // LVGL task (Core 1 — WiFi/WS Core 0'da çalışır)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "Ekran hazir: %dx%d", TFT_WIDTH, TFT_HEIGHT);
    return ESP_OK;
}

void display_switch(screen_id_t id, const char *msg)
{
    if (display_lock(200)) {
        ui_smartlab_show(id, msg);
        display_unlock();
    }
}

bool display_lock(int timeout_ms)
{
    return xSemaphoreTake(s_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void display_unlock(void)
{
    xSemaphoreGive(s_mux);
}
