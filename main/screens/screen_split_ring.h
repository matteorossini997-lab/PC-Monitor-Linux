/**
 * @file screen_split_ring.h
 * @brief LVGL-based split ring dashboard screen
 */

#ifndef SCREEN_SPLIT_RING_H
#define SCREEN_SPLIT_RING_H

#include "lvgl.h"
#include <stdint.h>
#include "../core/system_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct screen_split_ring_t {
    lv_obj_t *screen;
    
    // Arcs
    lv_obj_t *arc_cpu;
    lv_obj_t *arc_gpu;
    lv_obj_t *arc_ram;
    
    // Labels
    lv_obj_t *label_cpu;
    lv_obj_t *label_gpu;
    lv_obj_t *label_ram;
    
    // Dynamic Colors
    lv_color_t color_cpu;
    lv_color_t color_gpu;
    lv_color_t color_ram;
    lv_color_t color_bg;
    lv_color_t color_text;
};

typedef struct screen_split_ring_t screen_split_ring_t;

/* ============================================================================
 * SPLIT RING SCREEN
 * ========================================================================== */
screen_split_ring_t *screen_split_ring_create(lv_display_t *disp);
void screen_split_ring_update(screen_split_ring_t *scr, const pc_stats_t *stats);
void screen_split_ring_apply_colors(screen_split_ring_t *scr, const void *gui_settings_ptr);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SPLIT_RING_H */
