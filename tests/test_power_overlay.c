#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "power_overlay.h"

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

int main(void)
{
    lv_init();
    lv_display_t *display = lv_display_create(240, 240);
    static uint8_t pixels[240 * 24 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, NULL, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    bc_power_overlay_t overlay;
    bc_power_overlay_init(&overlay, display);
    bc_power_overlay_update(&overlay, BC_DISPLAY_TELEMETRY, 0, 0);
    assert(lv_obj_has_flag(overlay.root, LV_OBJ_FLAG_HIDDEN));
    bc_power_overlay_update(&overlay, BC_DISPLAY_SHUTDOWN, 0, 1000);
    assert(strcmp(lv_label_get_text(overlay.caption), "SPEGNIMENTO...") == 0);
    assert(!lv_obj_has_flag(overlay.root, LV_OBJ_FLAG_HIDDEN));
    bc_power_overlay_update(&overlay, BC_DISPLAY_OFF_ANIMATION, 1000, 1000);
    assert(strcmp(lv_label_get_text(overlay.caption), "POWER OFF") == 0);
    assert(lv_arc_get_value(overlay.ring) == 1000);
    bc_power_overlay_update(&overlay, BC_DISPLAY_OFF_ANIMATION, 1000, 6000);
    assert(lv_arc_get_value(overlay.ring) == 500);
    bc_power_overlay_update(&overlay, BC_DISPLAY_OFF_ANIMATION, 1000, 10999);
    assert(lv_arc_get_value(overlay.ring) == 0);
    bc_power_overlay_update(&overlay, BC_DISPLAY_BLANK, 1000, 11000);
    assert(lv_obj_has_flag(overlay.symbol, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(overlay.ring, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(overlay.caption, LV_OBJ_FLAG_HIDDEN));
    bc_power_overlay_update(&overlay, BC_DISPLAY_STARTING, 12000, 12000);
    assert(strcmp(lv_label_get_text(overlay.caption), "AVVIO...") == 0);
    bc_power_overlay_update(&overlay, BC_DISPLAY_SUSPENDING, 13000, 13000);
    assert(strcmp(lv_label_get_text(overlay.caption), "SOSPENSIONE...") == 0);
    assert(lv_obj_has_flag(overlay.zzz, LV_OBJ_FLAG_HIDDEN));
    bc_power_overlay_update(&overlay, BC_DISPLAY_SLEEP, 14000, 14000);
    assert(strcmp(lv_label_get_text(overlay.zzz), "Zzzz") == 0);
    assert(!lv_obj_has_flag(overlay.zzz, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(overlay.symbol, LV_OBJ_FLAG_HIDDEN));
    lv_opa_t before = lv_obj_get_style_opa(overlay.zzz, 0);
    bc_power_overlay_update(&overlay, BC_DISPLAY_SLEEP, 14000, 15000);
    assert(lv_obj_get_style_opa(overlay.zzz, 0) != before);
    /* The top-layer overlay survives an ordinary telemetry screen switch. */
    lv_screen_load(lv_obj_create(NULL));
    assert(lv_obj_get_parent(overlay.root) == lv_display_get_layer_top(display));
    lv_refr_now(display);
    bc_power_overlay_update(&overlay, BC_DISPLAY_WAKING, 16000, 16000);
    assert(strcmp(lv_label_get_text(overlay.caption), "RISVEGLIO...") == 0);
    bc_power_overlay_update(&overlay, BC_DISPLAY_TELEMETRY, 17000, 17000);
    assert(lv_obj_has_flag(overlay.root, LV_OBJ_FLAG_HIDDEN));
    puts("PASS: real LVGL overlay labels, countdown, breathing Zzzz, blank, screen switch and restore");
}
