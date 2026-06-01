// LVGL and ILI9341 initialization and display manager
// The DMA callback is not utilized; instead, lv_disp_flush_ready() is called directly.
// This ensures compatibility across all ESP-IDF 5.x versions.

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

// Tag used for logging display events
static const char *TAG = "display";

// Time interval for LVGL tick increment in milliseconds
#define LVGL_TICK_MS  5

// Mutex for thread-safe LVGL operations
static SemaphoreHandle_t      s_mux   = NULL;
// Handle for the LCD panel device
static esp_lcd_panel_handle_t s_panel = NULL;

// LVGL flush callback function
// The draw_bitmap function completes synchronously, after which flush_ready is called.
// This approach avoids the need for a DMA interrupt, making it version independent and safe.
static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p)
{
    esp_lcd_panel_draw_bitmap(s_panel,
                               area->x1, area->y1,
                               area->x2 + 1, area->y2 + 1,
                               color_p);
    lv_disp_flush_ready(drv);
}

// Timer callback function to increment the LVGL tick
static void lvgl_tick_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_MS);
}

// Dedicated FreeRTOS task to handle LVGL updates and timers
static void lvgl_task(void *arg)
{
    while (1) {
        // Attempt to take the mutex before calling the LVGL timer handler
        if (xSemaphoreTake(s_mux, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_mux);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Initializes the display hardware, panel, and LVGL library
esp_err_t display_init(void)
{
    // Configure and enable the TFT backlight GPIO pin
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << TFT_BL_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(TFT_BL_GPIO, 1);

    // Initialize SPI panel IO
    // Note that the SPI_HOST must have been initialized previously via spi_bus_initialize
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

    // Configure and initialize the ILI9341 LCD panel
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

    // Initialize the core LVGL library
    lv_init();

    // Setup double buffering for LVGL
    // The LVGL_BUF_LINES definition is provided by config.h
    static lv_color_t buf1[TFT_WIDTH * LVGL_BUF_LINES];
    static lv_color_t buf2[TFT_WIDTH * LVGL_BUF_LINES];
    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_WIDTH * LVGL_BUF_LINES);

    // Register the display driver with LVGL
    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res  = TFT_WIDTH;
    drv.ver_res  = TFT_HEIGHT;
    drv.flush_cb = lvgl_flush_cb;
    drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&drv);

    // Setup and start the LVGL tick timer
    // This timer calls lv_tick_inc at a regular interval defined by LVGL_TICK_MS
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name     = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000));

    // Create the mutex to protect LVGL API calls across different tasks
    s_mux = xSemaphoreCreateMutex();

    // Initialize the UI objects before starting the LVGL task
    ui_smartlab_init();
    display_switch(SCREEN_IDLE, NULL);

    // Create and start the FreeRTOS task for handling LVGL updates
    // Pinned to Core 1, as Core 0 handles WiFi and WebSocket tasks
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "Display ready: %dx%d", TFT_WIDTH, TFT_HEIGHT);
    return ESP_OK;
}

// Switches the active screen display to a new state and updates the message
void display_switch(screen_id_t id, const char *msg)
{
    if (display_lock(200)) {
        ui_smartlab_show(id, msg);
        display_unlock();
    }
}

// Tries to take the LVGL mutex with the specified timeout
bool display_lock(int timeout_ms)
{
    return xSemaphoreTake(s_mux, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

// Releases the LVGL mutex
void display_unlock(void)
{
    xSemaphoreGive(s_mux);
}
