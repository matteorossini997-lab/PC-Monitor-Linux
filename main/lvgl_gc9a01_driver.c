/**
 * @file lvgl_gc9a01_driver.c
 * @brief LVGL Display Driver for GC9A01 - Desert-Spec Phase 2 Edition
 *
 * Key Features:
 * - 20 MHz SPI clock for signal stability with 4 displays
 * - BLOCKING mode (trans_queue_depth=1) - no async issues
 * - PSRAM buffers for full-frame double buffering
 * - Simple, crash-resistant design
 */

#include "lvgl_gc9a01_driver.h"
#include <string.h>
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_panel_st7789.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LVGL_GC9A01";

/**
 * @brief LVGL Flush Callback - TRUE BLOCKING MODE
 *
 * With trans_queue_depth=1, esp_lcd_panel_draw_bitmap blocks
 * until the SPI transfer is complete. Safe for 4 displays.
 */
static SemaphoreHandle_t s_flush_sem = NULL;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    if (s_flush_sem) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(s_flush_sem, &xHigherPriorityTaskWoken);
        return (xHigherPriorityTaskWoken == pdTRUE);
    }
    return false;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    lvgl_gc9a01_handle_t *handle = (lvgl_gc9a01_handle_t *)lv_display_get_user_data(disp);

    if (!handle || !handle->panel_handle) {
        lv_display_flush_ready(disp);
        return;
    }

    if (!s_flush_sem) {
        s_flush_sem = xSemaphoreCreateBinary();
    }

    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;

    // SPI LCD is big-endian, swap RGB565 byte order before sending
    lv_draw_sw_rgb565_swap(px_map, (x2 + 1 - x1) * (y2 + 1 - y1));

    // Queue the transfer and wait for it to finish synchronously!
    esp_lcd_panel_draw_bitmap(handle->panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
    
    if (s_flush_sem) {
        xSemaphoreTake(s_flush_sem, portMAX_DELAY);
    }

    // Now safely call flush_ready from the LVGL task context
    lv_display_flush_ready(disp);
}

/**
 * @brief Initialize GC9A01 display with LVGL
 */
esp_err_t lvgl_gc9a01_init(const lvgl_gc9a01_config_t *config, lvgl_gc9a01_handle_t *handle)
{
    ESP_LOGI(TAG, "Initializing GC9A01 (CS=%d, DC=%d, RST=%d)",
             config->pin_cs, config->pin_dc, config->pin_rst);

    memset(handle, 0, sizeof(lvgl_gc9a01_handle_t));

    // =========================================================================
    // 1. SPI Panel IO - BLOCKING MODE (queue_depth=1)
    // =========================================================================
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = config->pin_dc,
        .cs_gpio_num = config->pin_cs,
        .pclk_hz = 20 * 1000 * 1000,    // 20 MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,        // Allow a few queued transactions
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = handle,
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)config->spi_host,
        &io_config,
        &io_handle
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    // =========================================================================
    // 2. Panel Configuration
    // =========================================================================
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->pin_rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &handle->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GC9A01 panel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize display hardware
    esp_lcd_panel_reset(handle->panel_handle);
    esp_lcd_panel_init(handle->panel_handle);
    esp_lcd_panel_invert_color(handle->panel_handle, true); // ST7789 needs this!
    esp_lcd_panel_mirror(handle->panel_handle, true, false);  // No mirror
    esp_lcd_panel_disp_on_off(handle->panel_handle, true);

    ESP_LOGI(TAG, "Hardware initialized");

    // =========================================================================
    // 3. LVGL Display Setup
    // =========================================================================
    handle->lv_disp = lv_display_create(240, 240);
    if (!handle->lv_disp) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return ESP_ERR_NO_MEM;
    }

    lv_display_set_user_data(handle->lv_disp, handle);
    lv_display_set_flush_cb(handle->lv_disp, lvgl_flush_cb);

    // =========================================================================
    // 4. PSRAM Frame Buffers - Use PARTIAL mode for less blocking time
    // =========================================================================
    // Allocate display buffers in INTERNAL SRAM (DMA capable) to avoid PSRAM DMA corruption/slowness
    size_t buf_size = 240 * 240 * 2 / 10; // 1/10th screen size = ~11.5 KB
    handle->draw_buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    handle->draw_buf2 = NULL; // Use single buffer to guarantee no DMA tearing

    if (!handle->draw_buf1) {
        ESP_LOGE(TAG, "Failed to allocate display buffers in internal RAM");
        return ESP_ERR_NO_MEM;
    }

    memset(handle->draw_buf1, 0, buf_size);

    lv_display_set_buffers(
        handle->lv_disp,
        handle->draw_buf1,
        NULL,
        buf_size,
        LV_DISPLAY_RENDER_MODE_PARTIAL  // Partial updates = less blocking
    );

    ESP_LOGI(TAG, "GC9A01 ready (CS=%d, 20MHz, BLOCKING)", config->pin_cs);
    return ESP_OK;
}

/**
 * @brief Get LVGL display object
 */
lv_display_t *lvgl_gc9a01_get_display(lvgl_gc9a01_handle_t *handle)
{
    return handle ? handle->lv_disp : NULL;
}
