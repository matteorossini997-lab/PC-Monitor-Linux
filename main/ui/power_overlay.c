#include "power_overlay.h"

static void visible(lv_obj_t *obj, bool show)
{
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *arc(lv_obj_t *parent, int size, int width)
{
    lv_obj_t *obj = lv_arc_create(parent);
    lv_obj_set_size(obj, size, size);
    lv_obj_center(obj);
    lv_obj_remove_style(obj, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(obj, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(obj, width, LV_PART_INDICATOR);
    return obj;
}

void bc_power_overlay_init(bc_power_overlay_t *o, lv_display_t *display)
{
    /* Top layer survives the user's normal/split-ring screen and theme changes. */
    o->root = lv_obj_create(lv_display_get_layer_top(display));
    lv_obj_remove_style_all(o->root);
    lv_obj_set_size(o->root, 240, 240);
    lv_obj_set_style_bg_color(o->root, lv_color_hex(0x05080d), 0);
    lv_obj_set_style_bg_opa(o->root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o->root, LV_OBJ_FLAG_SCROLLABLE);

    o->ring = arc(o->root, 216, 3);
    lv_arc_set_range(o->ring, 0, 1000);
    lv_arc_set_rotation(o->ring, 270);
    lv_arc_set_bg_angles(o->ring, 0, 360);
    lv_obj_set_style_arc_color(o->ring, lv_color_hex(0x18212e), LV_PART_MAIN);

    o->symbol = lv_obj_create(o->root);
    lv_obj_remove_style_all(o->symbol);
    lv_obj_set_size(o->symbol, 88, 88);
    lv_obj_align(o->symbol, LV_ALIGN_CENTER, 0, -18);
    lv_obj_t *power_arc = arc(o->symbol, 84, 7);
    lv_arc_set_bg_angles(power_arc, 135, 405);
    lv_arc_set_rotation(power_arc, 180); /* Opening at the top. */
    lv_obj_set_style_arc_color(power_arc, lv_color_hex(0xff9d45), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(power_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_t *stroke = lv_obj_create(o->symbol);
    lv_obj_remove_style_all(stroke);
    lv_obj_set_size(stroke, 7, 43);
    lv_obj_set_style_radius(stroke, 4, 0);
    lv_obj_set_style_bg_color(stroke, lv_color_hex(0xff9d45), 0);
    lv_obj_set_style_bg_opa(stroke, LV_OPA_COVER, 0);
    lv_obj_align(stroke, LV_ALIGN_TOP_MID, 0, -2);

    o->zzz = lv_label_create(o->root);
    lv_label_set_text(o->zzz, "Zzzz");
#if LV_FONT_MONTSERRAT_42
    lv_obj_set_style_text_font(o->zzz, &lv_font_montserrat_42, 0);
#endif
    lv_obj_set_style_text_color(o->zzz, lv_color_hex(0x78cfff), 0);
    o->caption = lv_label_create(o->root);
    lv_obj_set_style_text_color(o->caption, lv_color_hex(0xd9e5ef), 0);
    lv_obj_align(o->caption, LV_ALIGN_CENTER, 0, 52);
    o->mode = BC_DISPLAY_TELEMETRY;
    visible(o->root, false);
}

void bc_power_overlay_update(bc_power_overlay_t *o, bc_display_mode_t mode,
                             uint64_t since, uint64_t now)
{
    if (o->mode == mode && (mode == BC_DISPLAY_BLANK || mode == BC_DISPLAY_TELEMETRY))
        return;
    o->mode = mode;
    visible(o->root, mode != BC_DISPLAY_TELEMETRY);
    if (mode == BC_DISPLAY_TELEMETRY) return;

    bool blank = mode == BC_DISPLAY_BLANK;
    bool sleep = mode == BC_DISPLAY_SLEEP;
    visible(o->symbol, !blank && !sleep);
    visible(o->zzz, sleep);
    visible(o->ring, !blank);
    visible(o->caption, !blank);
    if (blank) return;

    uint64_t elapsed = now >= since ? now - since : 0;
    unsigned phase = (unsigned)(elapsed % 2400);
    unsigned breath = phase <= 1200 ? phase : 2400 - phase;
    lv_opa_t opacity = (lv_opa_t)(100 + breath * 155 / 1200);
    lv_obj_set_style_opa(o->symbol, opacity, 0);
    lv_obj_set_style_opa(o->zzz, opacity, 0);
    lv_obj_align(o->zzz, LV_ALIGN_CENTER, 0, -18 - (int)(breath / 150));
    lv_obj_set_style_arc_color(o->ring,
        lv_color_hex(sleep ? 0x78cfff : 0xff9d45), LV_PART_INDICATOR);
    lv_obj_set_style_opa(o->ring, sleep ? opacity : LV_OPA_COVER, 0);

    const char *caption;
    int value = 200 + (int)(elapsed % 2000) * 800 / 2000;
    switch (mode) {
    case BC_DISPLAY_SHUTDOWN: caption = "SPEGNIMENTO..."; break;
    case BC_DISPLAY_OFF_ANIMATION:
        caption = "POWER OFF";
        value = elapsed >= BC_DISPLAY_OFF_DELAY_MS ? 0 :
            (int)((BC_DISPLAY_OFF_DELAY_MS - elapsed) * 1000 / BC_DISPLAY_OFF_DELAY_MS);
        lv_obj_set_style_opa(o->symbol, (lv_opa_t)(value * 255 / 1000), 0);
        break;
    case BC_DISPLAY_SLEEP: caption = "SLEEP"; value = 1000; break;
    case BC_DISPLAY_SUSPENDING: caption = "SOSPENSIONE..."; break;
    case BC_DISPLAY_STARTING: caption = "AVVIO..."; break;
    case BC_DISPLAY_WAKING: caption = "RISVEGLIO..."; break;
    default: caption = ""; break;
    }
    lv_arc_set_value(o->ring, value);
    lv_label_set_text(o->caption, caption);
    lv_obj_align(o->caption, LV_ALIGN_CENTER, 0, 52);
}
