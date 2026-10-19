#pragma once

#include "lvgl.h"
#include "../power/power_display.h"

typedef struct {
    lv_obj_t *root, *ring, *symbol, *zzz, *caption;
    bc_display_mode_t mode;
} bc_power_overlay_t;

/* All calls require the existing LVGL mutex. No LVGL animation callbacks/tasks. */
void bc_power_overlay_init(bc_power_overlay_t *overlay, lv_display_t *display);
void bc_power_overlay_update(bc_power_overlay_t *overlay, bc_display_mode_t mode,
                             uint64_t since, uint64_t now);
