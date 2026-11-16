#include "ui_grid.h"
#include <stdio.h>
#include "ui_internal.h"

static const int CHIPS = 4;
static const int CHIP_H = 40;
static const int AREA_Y = UI_PAD + CHIP_H + UI_GAP;
static const int AREA_H = UI_CONTENT_H - CHIP_H - UI_GAP;  // 146
static const int BAR_H = 3;

void gridBuild(CellGrid &g, lv_obj_t *page) {
    int chipW = (UI_CONTENT_W - (CHIPS - 1) * UI_GAP) / CHIPS;
    for(int i = 0; i < CHIPS; i++) {
        lv_obj_t *chip = uiCard(page, UI_PAD + i * (chipW + UI_GAP), UI_PAD, chipW, CHIP_H);
        g.chipCaption[i] = uiLabel(chip, &lv_font_montserrat_12, UI_DIM);
        lv_obj_set_pos(g.chipCaption[i], 8, 4);
        g.chipValue[i] = uiLabel(chip, &lv_font_montserrat_14, UI_TEXT);
        lv_obj_set_pos(g.chipValue[i], 8, 20);
    }

    g.area = lv_obj_create(page);
    lv_obj_remove_style_all(g.area);
    lv_obj_set_pos(g.area, UI_PAD, AREA_Y);
    lv_obj_set_size(g.area, UI_CONTENT_W, AREA_H);
    lv_obj_remove_flag(g.area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(g.area, LV_OBJ_FLAG_CLICKABLE);

    for(int i = 0; i < JK_MAX_CELLS; i++) {
        GridTile &t = g.tiles[i];
        t.box = uiBox(g.area, 0, 0, 10, 10, UI_CARD, 8);
        lv_obj_set_style_clip_corner(t.box, true, 0);
        t.num = uiLabel(t.box, &lv_font_montserrat_12, UI_DIM);
        lv_label_set_text_fmt(t.num, "%d", i + 1);
        t.value = uiLabel(t.box, &lv_font_montserrat_14, UI_TEXT);
        t.bar = uiBox(t.box, 0, 0, 0, BAR_H, UI_FAINT, 0);
        lv_obj_add_flag(t.box, LV_OBJ_FLAG_HIDDEN);
    }
    g.count = -1;
}

// Grid shape and fonts for n cells
static void layout(CellGrid &g, int n) {
    g.count = n;
    int cols = n <= 4 ? (n ? n : 1) : n <= 16 ? 4 : n <= 24 ? 6 : 8;
    int rows = (n + cols - 1) / cols;
    if(rows < 1) rows = 1;
    int gap = cols >= 6 ? 4 : UI_GAP;
    int w = (UI_CONTENT_W - (cols - 1) * gap) / cols;
    int maxH = rows <= 2 ? 64 : (AREA_H - (rows - 1) * gap) / rows;
    int h = (AREA_H - (rows - 1) * gap) / rows;
    if(h > maxH) h = maxH;
    const lv_font_t *font = h >= 50 ? &lv_font_montserrat_20 : cols >= 8 ? &lv_font_montserrat_12 : &lv_font_montserrat_14;
    for(int i = 0; i < JK_MAX_CELLS; i++) {
        GridTile &t = g.tiles[i];
        if(i >= n) {
            uiSetHidden(t.box, true);
            continue;
        }
        lv_obj_set_pos(t.box, (i % cols) * (w + gap), (i / cols) * (h + gap));
        lv_obj_set_size(t.box, w, h);
        lv_obj_set_pos(t.num, cols >= 8 ? 4 : 7, 2);
        lv_obj_set_style_text_font(t.value, font, 0);
        lv_obj_align(t.value, LV_ALIGN_BOTTOM_RIGHT, cols >= 8 ? -3 : -7, -BAR_H - 1);
        lv_obj_set_y(t.bar, h - BAR_H);
        uiSetHidden(t.box, false);
    }
}

void gridChip(CellGrid &g, int i, const char *caption, const char *value, lv_color_t color) {
    uiSetText(g.chipCaption[i], caption);
    uiSetText(g.chipValue[i], value);
    uiSetTextColor(g.chipValue[i], color);
}

void gridUpdate(CellGrid &g, int n, const float *values, int lo, int hi, int decimals) {
    if(n < 0) n = 0;
    if(n > JK_MAX_CELLS) n = JK_MAX_CELLS;
    if(n != g.count) layout(g, n);
    float vmin = 0, vmax = 0;
    for(int i = 0; i < n; i++) {
        if(i == 0 || values[i] < vmin) vmin = values[i];
        if(i == 0 || values[i] > vmax) vmax = values[i];
    }
    float span = vmax - vmin;
    char buf[16];
    for(int i = 0; i < n; i++) {
        GridTile &t = g.tiles[i];
        snprintf(buf, sizeof(buf), "%.*f", decimals, values[i]);
        uiSetText(t.value, buf);
        lv_color_t c = i == hi ? UI_BAD : i == lo ? UI_COOL : UI_TEXT;
        uiSetTextColor(t.value, c);
        uiSetTextColor(t.num, i == hi ? UI_BAD : i == lo ? UI_COOL : UI_DIM);
        // level bar: the lowest cell a sliver, the highest the full width
        int w = lv_obj_get_width(t.box);
        int bw = span > 0 ? static_cast<int>(w * (0.15f + 0.85f * (values[i] - vmin) / span)) : w;
        if(lv_obj_get_width(t.bar) != bw) lv_obj_set_width(t.bar, bw);
        uiSetBg(t.bar, i == hi ? UI_BAD : i == lo ? UI_COOL : UI_FAINT);
    }
}

void gridEmpty(CellGrid &g) {
    if(g.count < 0) layout(g, 16);
    for(int i = 0; i < g.count; i++) {
        GridTile &t = g.tiles[i];
        uiSetText(t.value, "--");
        uiSetTextColor(t.value, UI_FAINT);
        uiSetTextColor(t.num, UI_FAINT);
        if(lv_obj_get_width(t.bar) != 0) lv_obj_set_width(t.bar, 0);
    }
    for(int i = 0; i < CHIPS; i++) {
        uiSetText(g.chipValue[i], "--");
        uiSetTextColor(g.chipValue[i], UI_FAINT);
    }
}
