#include "ui.h"
#include <stdio.h>
#include <string.h>
#include "ble/jk_ble.h"
#include "display/display.h"
#include "esp_timer.h"
#include "ui_internal.h"

static const int DOT = 6;
static const int DOT_ACTIVE_W = 16;
static const int DOT_GAP = 6;
static const char *const TITLES[UI_PAGE_COUNT] = {"Overview", "Cells", "Wire resistance", "Device"};

static lv_obj_t *main_, *picker_, *tiles_;
static lv_obj_t *statusDot_, *statusName_, *pageTitle_;
static lv_obj_t *dots_[UI_PAGE_COUNT];
static int page_ = 0;

// --- Helpers

lv_obj_t *uiBox(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *uiCard(lv_obj_t *parent, int x, int y, int w, int h) {
    return uiBox(parent, x, y, w, h, UI_CARD, UI_RADIUS);
}

lv_obj_t *uiLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

void uiSetText(lv_obj_t *label, const char *text) {
    if(strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

void uiSetTextColor(lv_obj_t *obj, lv_color_t color) {
    if(!lv_color_eq(lv_obj_get_style_text_color(obj, 0), color)) lv_obj_set_style_text_color(obj, color, 0);
}

void uiSetBg(lv_obj_t *obj, lv_color_t color) {
    if(!lv_color_eq(lv_obj_get_style_bg_color(obj, 0), color)) lv_obj_set_style_bg_color(obj, color, 0);
}

void uiSetHidden(lv_obj_t *obj, bool hidden) {
    if(hidden == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
    if(hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *uiCaption(lv_obj_t *card, const char *text) {
    lv_obj_t *l = uiLabel(card, &lv_font_montserrat_12, UI_DIM);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, 10, 6);
    return l;
}

bool uiFresh(const BmsSnapshot &d) {
    return d.hasCells && d.link == LinkState::Connected && esp_timer_get_time() - d.cellsUs < UI_STALE_US;
}

bool uiIsBalancer(const BmsSnapshot &d) {
    if(d.proto == JkProto::JK04) return true;
    return d.hasCells && (!d.cells.hasBms || d.cells.nominalAh <= 0);
}

// --- Status bar and page dots

static void showPage(int page) {
    page_ = page;
    int total = (UI_PAGE_COUNT - 1) * (DOT + DOT_GAP) + DOT_ACTIVE_W;
    int x = (UI_PAGE_W - total) / 2;
    for(int i = 0; i < UI_PAGE_COUNT; i++) {
        int w = i == page ? DOT_ACTIVE_W : DOT;
        lv_obj_set_pos(dots_[i], x, 240 - 10);
        lv_obj_set_width(dots_[i], w);
        uiSetBg(dots_[i], i == page ? UI_ACCENT : UI_FAINT);
        x += w + DOT_GAP;
    }
    uiSetText(pageTitle_, TITLES[page]);
}

static void onTileChanged(lv_event_t *) {
    lv_obj_t *tile = lv_tileview_get_tile_active(tiles_);
    showPage(lv_obj_get_x(tile) / UI_PAGE_W);
}

static void updateStatus(const BmsSnapshot &d) {
    char name[32];
    if(d.name[0]) {
        strlcpy(name, d.name, sizeof(name));
    } else if(d.info.valid && d.info.model[0]) {
        strlcpy(name, d.info.model, sizeof(name));
    } else if(d.addr[0]) {
        strlcpy(name, d.addr, sizeof(name));
    } else {
        strlcpy(name, "No device", sizeof(name));
    }
    uiSetText(statusName_, name);
    lv_color_t c = UI_FAINT;
    if(uiFresh(d)) {
        c = UI_GOOD;
    } else if(d.link == LinkState::Connecting || d.link == LinkState::Connected) {
        c = UI_WARN;
    }
    uiSetBg(statusDot_, c);
}

static void update(lv_timer_t *) {
    if(lv_screen_active() != main_) return;
    BmsSnapshot d;
    bmsDataGet(d);
    updateStatus(d);
    homeUpdate(d);
    cellsUpdate(d);
    resistUpdate(d);
    infoUpdate(d);
}

static void buildMain() {
    main_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(main_, UI_BG, 0);
    lv_obj_remove_flag(main_, LV_OBJ_FLAG_SCROLLABLE);

    // Status bar: connection dot, device name, page title. Long press: picker.
    lv_obj_t *bar = lv_obj_create(main_);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, UI_PAGE_W, UI_BAR_H);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, [](lv_event_t *) { uiShowPickerLocked(); }, LV_EVENT_LONG_PRESSED, nullptr);
    statusDot_ = uiBox(bar, UI_PAD + 2, (UI_BAR_H - 8) / 2 + 1, 8, 8, UI_FAINT, LV_RADIUS_CIRCLE);
    statusName_ = uiLabel(bar, &lv_font_montserrat_14, UI_TEXT);
    lv_obj_set_width(statusName_, 170);
    lv_label_set_long_mode(statusName_, LV_LABEL_LONG_DOT);
    lv_obj_align(statusName_, LV_ALIGN_LEFT_MID, UI_PAD + 16, 1);
    pageTitle_ = uiLabel(bar, &lv_font_montserrat_12, UI_DIM);
    lv_obj_align(pageTitle_, LV_ALIGN_RIGHT_MID, -UI_PAD - 2, 1);

    tiles_ = lv_tileview_create(main_);
    lv_obj_set_pos(tiles_, 0, UI_BAR_H);
    lv_obj_set_size(tiles_, UI_PAGE_W, UI_PAGE_H);
    lv_obj_set_style_bg_opa(tiles_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tiles_, LV_SCROLLBAR_MODE_OFF);
    homeBuild(lv_tileview_add_tile(tiles_, UI_PAGE_HOME, 0, LV_DIR_RIGHT));
    cellsBuild(lv_tileview_add_tile(tiles_, UI_PAGE_CELLS, 0, LV_DIR_HOR));
    resistBuild(lv_tileview_add_tile(tiles_, UI_PAGE_RESIST, 0, LV_DIR_HOR));
    infoBuild(lv_tileview_add_tile(tiles_, UI_PAGE_INFO, 0, LV_DIR_LEFT));
    lv_obj_add_event_cb(tiles_, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

    for(int i = 0; i < UI_PAGE_COUNT; i++) dots_[i] = uiBox(main_, 0, 0, DOT, DOT, UI_FAINT, LV_RADIUS_CIRCLE);
    showPage(0);

    lv_timer_create(update, 500, nullptr);
}

// --- Navigation

void uiShowMainLocked() {
    blePickCancel();
    update(nullptr);
    if(lv_screen_active() != main_) lv_screen_load_anim(main_, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
    update(nullptr);
}

void uiShowPickerLocked() {
    pickerShow();
    if(lv_screen_active() != picker_) lv_screen_load_anim(picker_, LV_SCR_LOAD_ANIM_FADE_IN, 150, 0, false);
}

void uiShowMain() {
    LvglLock lock;
    uiShowMainLocked();
}

void uiShowPicker() {
    LvglLock lock;
    uiShowPickerLocked();
}

void uiGoToPage(int page) {
    if(page < 0 || page >= UI_PAGE_COUNT) return;
    LvglLock lock;
    if(lv_screen_active() != main_) uiShowMainLocked();
    lv_tileview_set_tile_by_index(tiles_, page, 0, LV_ANIM_OFF);
    showPage(page);
}

void uiScreenshot(const char *name) {
    displayScreenshot(name);
}

void uiStart() {
    LvglLock lock;
    buildMain();
    picker_ = pickerBuild();
    BmsSnapshot d;
    bmsDataGet(d);
    if(d.addr[0]) {
        update(nullptr);
        lv_screen_load(main_);
    } else {
        pickerShow();
        lv_screen_load(picker_);
    }
}
