#include "screen_split_ring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../gui_settings.h"

#define ARC_WIDTH 16
#define ARC_RADIUS 105

#define COLOR_ALERT lv_palette_main(LV_PALETTE_RED)

static lv_obj_t *create_segment_arc(lv_obj_t *parent, int rotation, int span_angle, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, ARC_RADIUS * 2, ARC_RADIUS * 2);
    
    // Set rotation so that local angle 0 is the start of our arc
    lv_arc_set_rotation(arc, rotation);
    
    // Draw from local 0 to span_angle to avoid 360 degree wrap bugs
    lv_arc_set_bg_angles(arc, 0, span_angle);
    // Start with 0 value
    lv_arc_set_angles(arc, 0, 0);
    
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);

    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_INDICATOR);
    
    // Remove rounded ends so they touch perfectly
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    
    // Background arc color (dimmed)
    lv_obj_set_style_arc_color(arc, lv_color_darken(color, 180), LV_PART_MAIN);
    // Foreground arc color
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);

    return arc;
}

screen_split_ring_t *screen_split_ring_create(lv_display_t *disp)
{
    screen_split_ring_t *scr = (screen_split_ring_t *)malloc(sizeof(screen_split_ring_t));
    if (!scr) return NULL;

    memset(scr, 0, sizeof(screen_split_ring_t));
    scr->color_bg = lv_color_black();
    scr->color_text = lv_color_white();
    scr->color_cpu = lv_palette_main(LV_PALETTE_YELLOW);
    scr->color_gpu = lv_palette_main(LV_PALETTE_GREEN);
    scr->color_ram = lv_palette_main(LV_PALETTE_BLUE);

    scr->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr->screen, scr->color_bg, 0);

    // Create 3 segments (span of 120 degrees for a perfect circle)
    // CPU starts at 210
    scr->arc_cpu = create_segment_arc(scr->screen, 210, 120, scr->color_cpu);
    // GPU starts at 330
    scr->arc_gpu = create_segment_arc(scr->screen, 330, 120, scr->color_gpu);
    // RAM starts at 90
    scr->arc_ram = create_segment_arc(scr->screen, 90, 120, scr->color_ram);

    // Create a central container for text
    lv_obj_t *text_container = lv_obj_create(scr->screen);
    lv_obj_set_size(text_container, 160, 160);
    lv_obj_center(text_container);
    lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_container, 0, 0);
    lv_obj_set_layout(text_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(text_container, 0, 0);

    // Font style
    static lv_style_t style_text;
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, scr->color_text);
    lv_style_set_text_font(&style_text, &lv_font_montserrat_14); // Use default font

    // CPU Label
    scr->label_cpu = lv_label_create(text_container);
    lv_obj_add_style(scr->label_cpu, &style_text, 0);
    lv_label_set_text(scr->label_cpu, "CPU: --%  --C");
    lv_obj_set_style_text_color(scr->label_cpu, scr->color_cpu, 0);
    lv_obj_set_style_pad_bottom(scr->label_cpu, 10, 0);

    // GPU Label
    scr->label_gpu = lv_label_create(text_container);
    lv_obj_add_style(scr->label_gpu, &style_text, 0);
    lv_label_set_text(scr->label_gpu, "GPU: --%  --C");
    lv_obj_set_style_text_color(scr->label_gpu, scr->color_gpu, 0);
    lv_obj_set_style_pad_bottom(scr->label_gpu, 10, 0);

    // RAM Label
    scr->label_ram = lv_label_create(text_container);
    lv_obj_add_style(scr->label_ram, &style_text, 0);
    lv_label_set_text(scr->label_ram, "RAM: --%  --C");
    lv_obj_set_style_text_color(scr->label_ram, scr->color_ram, 0);

    return scr;
}

static void update_segment(lv_obj_t *arc, lv_obj_t *label, const char *prefix, 
                           int usage_pct, float temp, lv_color_t base_color,
                           int span_angle)
{
    // Alert logic
    bool alert = false;
    if (temp > 70.0f) alert = true;

    lv_color_t final_color = alert ? COLOR_ALERT : base_color;

    // Update arc color
    lv_obj_set_style_arc_color(arc, lv_color_darken(final_color, 180), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, final_color, LV_PART_INDICATOR);
    
    // Update label color
    lv_obj_set_style_text_color(label, final_color, 0);

    // Update arc angle based on usage
    if (usage_pct >= 0) {
        int current_end = (span_angle * usage_pct) / 100;
        lv_arc_set_angles(arc, 0, current_end);
    } else {
        lv_arc_set_angles(arc, 0, 0); // Empty
    }

    // Update text
    char temp_str[16] = "--";
    if (temp >= 0) {
        snprintf(temp_str, sizeof(temp_str), "%.0f", temp);
    }
    
    if (usage_pct >= 0) {
        if (strcmp(prefix, "RAM") == 0) {
            lv_label_set_text_fmt(label, "%s: %d%% %sW", prefix, usage_pct, temp_str);
        } else {
            lv_label_set_text_fmt(label, "%s: %d%% %s°C", prefix, usage_pct, temp_str);
        }
    } else {
        if (strcmp(prefix, "RAM") == 0) {
            lv_label_set_text_fmt(label, "%s: N/A %sW", prefix, temp_str);
        } else {
            lv_label_set_text_fmt(label, "%s: N/A %s°C", prefix, temp_str);
        }
    }
}

void screen_split_ring_update(screen_split_ring_t *scr, const pc_stats_t *stats)
{
    if (!scr || !stats) return;

    // Convert RAM to percentage
    int ram_pct = 0;
    if (stats->ram_total_gb > 0.0f && stats->ram_used_gb >= 0.0f) {
        ram_pct = (int)((stats->ram_used_gb / stats->ram_total_gb) * 100.0f);
    }

    // Update CPU (span 120)
    update_segment(scr->arc_cpu, scr->label_cpu, "CPU", 
                   stats->cpu_percent, stats->cpu_temp, scr->color_cpu, 120);

    // Update GPU (span 120)
    update_segment(scr->arc_gpu, scr->label_gpu, "GPU", 
                   stats->gpu_percent, stats->gpu_temp, scr->color_gpu, 120);

    // Update RAM (span 120)
    update_segment(scr->arc_ram, scr->label_ram, "RAM", 
                   ram_pct, stats->gpu_power, scr->color_ram, 120);
}

void screen_split_ring_apply_colors(screen_split_ring_t *scr, const void *gui_settings_ptr)
{
    if (!scr || !gui_settings_ptr) return;
    const gui_settings_t *cfg = (const gui_settings_t *)gui_settings_ptr;

    scr->color_bg = lv_color_hex(cfg->bg_color[0]);
    scr->color_text = lv_color_hex(cfg->text_value);
    scr->color_cpu = lv_color_hex(cfg->arc_color_cpu);
    scr->color_gpu = lv_color_hex(cfg->arc_color_gpu);
    scr->color_ram = lv_color_hex(cfg->bar_color_ram);

    // In LVGL 9, if bg is bright (e.g. > 128 luminance), text should be dark.
    // For simplicity, we just use color_text from gui_settings, but we can also hardcode it or compute luminance.
    // Since we are setting bg white, we must set text to black. Let's make text color inverse of bg or use a config.
    // For now, let's determine text color automatically based on background brightness
    if (lv_color_brightness(scr->color_bg) > 128) {
        scr->color_text = lv_color_black();
    } else {
        scr->color_text = lv_color_white();
    }

    lv_obj_set_style_bg_color(scr->screen, scr->color_bg, 0);

    // We don't need to update arc styles right here because `screen_split_ring_update` applies `scr->color_xxx` to styles dynamically every time data arrives!
    // But we might want to trigger a redraw if data is not arriving:
    lv_obj_set_style_text_color(scr->label_cpu, scr->color_cpu, 0);
    lv_obj_set_style_text_color(scr->label_gpu, scr->color_gpu, 0);
    lv_obj_set_style_text_color(scr->label_ram, scr->color_ram, 0);
    
    // Also update arc indicator base colors
    lv_obj_set_style_arc_color(scr->arc_cpu, lv_color_darken(scr->color_cpu, 180), LV_PART_MAIN);
    lv_obj_set_style_arc_color(scr->arc_cpu, scr->color_cpu, LV_PART_INDICATOR);
    
    lv_obj_set_style_arc_color(scr->arc_gpu, lv_color_darken(scr->color_gpu, 180), LV_PART_MAIN);
    lv_obj_set_style_arc_color(scr->arc_gpu, scr->color_gpu, LV_PART_INDICATOR);
    
    lv_obj_set_style_arc_color(scr->arc_ram, lv_color_darken(scr->color_ram, 180), LV_PART_MAIN);
    lv_obj_set_style_arc_color(scr->arc_ram, scr->color_ram, LV_PART_INDICATOR);
}
