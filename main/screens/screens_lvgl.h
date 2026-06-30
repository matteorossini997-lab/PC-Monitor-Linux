/**
 * @file screens_lvgl.h
 * @brief LVGL-based unified screen for single GC9A01 display
 */

#ifndef SCREENS_LVGL_H
#define SCREENS_LVGL_H

#include "lvgl.h"
#include <stdint.h>
#include "../core/system_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct screen_unified_t {
    lv_obj_t *screen;
    // Arcs for concentric circles
    lv_obj_t *arc_cpu;
    lv_obj_t *arc_gpu;
    lv_obj_t *arc_ram;
    lv_obj_t *arc_net;
    
    // Labels for numerical values
    lv_obj_t *label_cpu;
    lv_obj_t *label_gpu;
    lv_obj_t *label_ram;
    lv_obj_t *label_net;
    
    // Secondary labels (temp, vram, etc)
    lv_obj_t *label_cpu_temp;
    lv_obj_t *label_gpu_temp;
};

typedef struct screen_unified_t screen_unified_t;

/* ============================================================================
 * UNIFIED SCREEN
 * ========================================================================== */
screen_unified_t *screen_unified_create(lv_display_t *disp);
void screen_unified_update(screen_unified_t *screen, const pc_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* SCREENS_LVGL_H */
