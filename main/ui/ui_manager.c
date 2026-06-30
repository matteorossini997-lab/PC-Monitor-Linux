/**
 * @file ui_manager.c
 * @brief UI Manager Implementation
 */

#include "ui_manager.h"
#include "screensaver_mgr.h"
#include "../gui_settings.h"
#include "../storage/hw_identity.h"
#include "../screens/screens_lvgl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "UI-MGR";

/* Screen references */
static ui_screens_t *s_screens = NULL;
static ui_screensavers_t *s_screensavers = NULL;
static ui_status_dots_t *s_dots = NULL;
static SemaphoreHandle_t s_lvgl_mutex = NULL;
static bool s_screensaver_active = false;

/* Screensaver image widget handles (for hot-swap updates) */
static lv_obj_t *s_ss_images[1] = {NULL};

/* Lock statistics for debugging */
static uint32_t s_lock_timeouts = 0;
static uint32_t s_lock_successes = 0;

/* =============================================================================
 * THREAD-SAFE LOCKING API (The Iron Gate)
 * ========================================================================== */

bool ui_acquire_lock(uint32_t timeout_ms)
{
    if (!s_lvgl_mutex) {
        ESP_LOGE(TAG, "UI Lock: Mutex not initialized!");
        return false;
    }

    if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        s_lock_successes++;
        return true;
    }

    /* Lock timeout - FAIL-SAFE: Skip update, don't freeze! */
    s_lock_timeouts++;
    ESP_LOGW(TAG, "UI Lock Timeout (%lu ms)! Skipping update. [timeouts: %lu, successes: %lu]",
             (unsigned long)timeout_ms, (unsigned long)s_lock_timeouts, (unsigned long)s_lock_successes);
    return false;
}

void ui_release_lock(void)
{
    if (s_lvgl_mutex) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}

/* =============================================================================
 * INITIALIZATION
 * ========================================================================== */

void ui_manager_init(SemaphoreHandle_t lvgl_mutex)
{
    s_lvgl_mutex = lvgl_mutex;
    ESP_LOGI(TAG, "UI Manager initialized");
}

void ui_manager_set_screens(ui_screens_t *screens)
{
    s_screens = screens;
}

void ui_manager_set_screensavers(ui_screensavers_t *screensavers)
{
    s_screensavers = screensavers;
}

void ui_manager_set_status_dots(ui_status_dots_t *dots)
{
    s_dots = dots;
}

/* =============================================================================
 * THEME APPLICATION
 * ========================================================================== */

void ui_manager_apply_theme(void)
{
    ESP_LOGI(TAG, "Applying theme...");

    /* --- Unified Screen --- */
    if (s_screens && s_screens->main) {
        screen_unified_t *scr = s_screens->main;
        if (scr->screen) {
            lv_obj_set_style_bg_color(scr->screen, lv_color_hex(gui_settings.bg_color[0]), 0);
        }
        
        // Use individual colors for arcs based on settings if available, else keep defaults.
        // The screen creation applies default palettes (red, green, blue, yellow).
        // This can be expanded to fully support theme color application.
    }

    /* --- Screensaver background --- */
    if (s_screensavers && s_screensavers->main) {
        lv_obj_set_style_bg_color(s_screensavers->main, lv_color_hex(gui_settings.ss_bg_color[0]), 0);
    }

    ESP_LOGI(TAG, "Theme applied");
}

/* =============================================================================
 * HARDWARE NAMES
 * ========================================================================== */

void ui_manager_apply_hardware_names(void)
{
    // hw_identity_t *id = hw_identity_get();
    // In unified screen, we don't have separate large title labels, we just show "CPU:", "GPU:".
    // Could potentially add a scrolling label with hw names later.
}

/* =============================================================================
 * SCREEN UPDATES
 * ========================================================================== */

void ui_manager_update_screens(const pc_stats_t *stats)
{
    if (!s_screens || !stats) return;

    if (s_screens->main) screen_unified_update(s_screens->main, stats);
}

/* =============================================================================
 * SCREENSAVER CONTROL
 * ========================================================================== */

void ui_manager_show_screensavers(bool show)
{
    if (!s_screensavers) return;

    if (show) {
        if (s_screensavers->main) lv_obj_clear_flag(s_screensavers->main, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_screensavers->main) lv_obj_add_flag(s_screensavers->main, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_manager_show_status_dots(bool show)
{
    if (!s_dots) return;

    if (show) {
        if (s_dots->main) lv_obj_clear_flag(s_dots->main, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (s_dots->main) lv_obj_add_flag(s_dots->main, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_manager_is_screensaver_active(void)
{
    return s_screensaver_active;
}

void ui_manager_set_screensaver_active(bool active)
{
    s_screensaver_active = active;
}

/* =============================================================================
 * COLOR COMMAND HANDLER
 * ========================================================================== */
static uint32_t parse_hex_color(const char *hex_str)
{
    if (hex_str[0] == '0' && (hex_str[1] == 'x' || hex_str[1] == 'X')) {
        hex_str += 2;
    }
    return (uint32_t)strtoul(hex_str, NULL, 16);
}

bool ui_manager_handle_color_command(const char *line)
{
    bool needs_save = false;
    bool needs_theme_update = false;

    /* RESET_THEME */
    if (strcmp(line, "RESET_THEME") == 0) {
        gui_settings_init_defaults(&gui_settings);
        ESP_LOGI(TAG, "Reset to default Desert-Spec theme");
        needs_save = needs_theme_update = true;
    }
    else {
        // Ignored most color commands for unified screen to simplify,
        // but this can easily be restored if theme overrides are needed for individual arcs.
        return false;
    }

    /* Save to LittleFS if needed */
    if (needs_save) {
        gui_settings_save();
    }

    /* Apply theme if needed (must be done in LVGL context) */
    if (needs_theme_update && s_lvgl_mutex) {
        if (xSemaphoreTake(s_lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ui_manager_apply_theme();
            xSemaphoreGive(s_lvgl_mutex);
        }
    }

    return true;
}

/* =============================================================================
 * UI ELEMENT CREATORS
 * ========================================================================== */

lv_obj_t *ui_manager_create_status_dot(lv_obj_t *parent)
{
    if (!parent) return NULL;

    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, lv_color_white(), 0);
    lv_obj_align(dot, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);

    return dot;
}

lv_obj_t *ui_manager_create_screensaver_ex(lv_obj_t *parent, lv_color_t bg_color,
                                           const lv_image_dsc_t *icon_src, int slot_index)
{
    if (!parent) return NULL;

    /* Fullscreen overlay container */
    lv_obj_t *overlay = lv_obj_create(parent);
    lv_obj_set_size(overlay, 240, 240);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, bg_color, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Centered image */
    lv_obj_t *img = lv_img_create(overlay);
    lv_img_set_src(img, icon_src);
    lv_obj_center(img);

    /* Store image handle for hot-swap updates */
    if (slot_index >= 0 && slot_index < 1) { // Only 1 slot now
        s_ss_images[slot_index] = img;
    }

    /* Start hidden */
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

    return overlay;
}

lv_obj_t *ui_manager_create_screensaver(lv_obj_t *parent, lv_color_t bg_color, const lv_image_dsc_t *icon_src)
{
    return ui_manager_create_screensaver_ex(parent, bg_color, icon_src, -1);
}

/* =============================================================================
 * SCREENSAVER IMAGE HOT-SWAP (Thread-Safe Refresh)
 * ========================================================================== */

void ui_manager_on_image_reload(int slot, const lv_image_dsc_t *new_dsc)
{
    if (slot < 0 || slot >= 1 || !new_dsc) return;

    lv_obj_t *img = s_ss_images[slot];
    if (img) {
        lv_img_set_src(img, new_dsc);
        ESP_LOGI(TAG, "Screensaver slot %d image source refreshed", slot);
    } else {
        ESP_LOGW(TAG, "Screensaver slot %d image handle not found", slot);
    }
}
