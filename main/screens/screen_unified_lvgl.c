#include "screens_lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARC_WIDTH 10

static lv_obj_t *create_concentric_arc(lv_obj_t *parent, int radius, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, radius * 2, radius * 2);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(arc);

    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_WIDTH, LV_PART_INDICATOR);
    
    // Background arc color (dimmed)
    lv_obj_set_style_arc_color(arc, lv_color_darken(color, 150), LV_PART_MAIN);
    // Foreground arc color
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);

    return arc;
}

screen_unified_t *screen_unified_create(lv_display_t *disp)
{
    screen_unified_t *scr = (screen_unified_t *)malloc(sizeof(screen_unified_t));
    if (!scr) return NULL;

    memset(scr, 0, sizeof(screen_unified_t));
    scr->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr->screen, lv_color_black(), 0);

    // Create 4 concentric arcs
    // Outer to inner: CPU, GPU, RAM, NET
    scr->arc_cpu = create_concentric_arc(scr->screen, 110, lv_palette_main(LV_PALETTE_RED));
    scr->arc_gpu = create_concentric_arc(scr->screen, 95, lv_palette_main(LV_PALETTE_GREEN));
    scr->arc_ram = create_concentric_arc(scr->screen, 80, lv_palette_main(LV_PALETTE_BLUE));
    scr->arc_net = create_concentric_arc(scr->screen, 65, lv_palette_main(LV_PALETTE_YELLOW));

    // Create a container for text in the center
    lv_obj_t *text_container = lv_obj_create(scr->screen);
    lv_obj_set_size(text_container, 100, 100);
    lv_obj_center(text_container);
    lv_obj_set_style_bg_opa(text_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text_container, 0, 0);
    lv_obj_set_layout(text_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(text_container, 0, 0);

    // Create labels for numerical values
    static lv_style_t style_text;
    lv_style_init(&style_text);
    lv_style_set_text_color(&style_text, lv_color_white());
    lv_style_set_text_font(&style_text, &lv_font_montserrat_14);

    scr->label_cpu = lv_label_create(text_container);
    lv_obj_add_style(scr->label_cpu, &style_text, 0);
    lv_label_set_text(scr->label_cpu, "CPU: --%");
    lv_obj_set_style_text_color(scr->label_cpu, lv_palette_main(LV_PALETTE_RED), 0);

    scr->label_gpu = lv_label_create(text_container);
    lv_obj_add_style(scr->label_gpu, &style_text, 0);
    lv_label_set_text(scr->label_gpu, "GPU: --%");
    lv_obj_set_style_text_color(scr->label_gpu, lv_palette_main(LV_PALETTE_GREEN), 0);

    scr->label_ram = lv_label_create(text_container);
    lv_obj_add_style(scr->label_ram, &style_text, 0);
    lv_label_set_text(scr->label_ram, "RAM: --%");
    lv_obj_set_style_text_color(scr->label_ram, lv_palette_main(LV_PALETTE_BLUE), 0);

    scr->label_net = lv_label_create(text_container);
    lv_obj_add_style(scr->label_net, &style_text, 0);
    lv_label_set_text(scr->label_net, "NET: --");
    lv_obj_set_style_text_color(scr->label_net, lv_palette_main(LV_PALETTE_YELLOW), 0);

    // Temp labels
    scr->label_cpu_temp = lv_label_create(text_container);
    lv_obj_add_style(scr->label_cpu_temp, &style_text, 0);
    lv_label_set_text(scr->label_cpu_temp, "C:-- G:--");
    lv_obj_set_style_text_font(scr->label_cpu_temp, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(scr->label_cpu_temp, lv_color_hex(0xAAAAAA), 0);

    return scr;
}

void screen_unified_update(screen_unified_t *scr, const pc_stats_t *stats)
{
    if (!scr || !stats) return;

    // CPU Update
    if (stats->cpu_percent >= 0) {
        lv_arc_set_value(scr->arc_cpu, stats->cpu_percent);
        lv_label_set_text_fmt(scr->label_cpu, "CPU: %d%%", stats->cpu_percent);
    } else {
        lv_arc_set_value(scr->arc_cpu, 0);
        lv_label_set_text(scr->label_cpu, "CPU: N/A");
    }

    // GPU Update
    if (stats->gpu_percent >= 0) {
        lv_arc_set_value(scr->arc_gpu, stats->gpu_percent);
        lv_label_set_text_fmt(scr->label_gpu, "GPU: %d%%", stats->gpu_percent);
    } else {
        lv_arc_set_value(scr->arc_gpu, 0);
        lv_label_set_text(scr->label_gpu, "GPU: N/A");
    }

    // RAM Update
    if (stats->ram_total_gb > 0) {
        int ram_pct = (int)((stats->ram_used_gb / stats->ram_total_gb) * 100.0f);
        lv_arc_set_value(scr->arc_ram, ram_pct);
        lv_label_set_text_fmt(scr->label_ram, "RAM: %d%%", ram_pct);
    }

    // NET Update
    // Max scale assumption for net arc: 1000 Mbps
    float total_net = stats->net_down_mbps + stats->net_up_mbps;
    int net_pct = (int)((total_net / 1000.0f) * 100.0f);
    if (net_pct > 100) net_pct = 100;
    lv_arc_set_value(scr->arc_net, net_pct);
    
    if (total_net > 0) {
        lv_label_set_text_fmt(scr->label_net, "%.1f M/s", total_net);
    } else {
        lv_label_set_text(scr->label_net, "NET: IDLE");
    }

    // Temp labels
    lv_label_set_text_fmt(scr->label_cpu_temp, "C:%.0f G:%.0f", stats->cpu_temp, stats->gpu_temp);
}
