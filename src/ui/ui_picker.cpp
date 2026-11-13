// Device picker: a scrolling list of the JK devices found, strongest signal
// first. Each is an island with the name, the address and signal bars; the
// saved device is tagged. Tap one to save it and connect. "Back" (top left)
// returns to the main screen when a device is already saved.

#include <stdio.h>
#include <string.h>
#include "ble/jk_ble.h"
#include "prefs/prefs.h"
#include "ui_internal.h"

static const int MAX_ENTRIES = 16;
static const int LIST_Y = UI_BAR_H + 4;
static const int LIST_H = 240 - LIST_Y;
static const int CARD_H = 46;
static const int BARS = 4;
static const int BAR_W = 4, BAR_GAP = 2, BAR_STEP_H = 4;
static const int SPIN_TURN_MS = 1000;

struct Card {
    lv_obj_t *bars[BARS];
};

static lv_obj_t *screen_, *list_, *empty_, *back_;
static Card cards_[MAX_ENTRIES];
static ScanEntry entries_[MAX_ENTRIES];
static int count_ = -1;  // -1: rebuild
static SavedDevice saved_;
static bool haveSaved_;

// Signal strength as 0..4 bars
static int bars(int8_t rssi) {
    return rssi >= -60 ? 4 : rssi >= -70 ? 3 : rssi >= -80 ? 2 : rssi >= -90 ? 1 : 0;
}

static void showBars(int i) {
    int n = bars(entries_[i].rssi);
    for(int b = 0; b < BARS; b++) uiSetBg(cards_[i].bars[b], b < n ? UI_ACCENT : UI_FAINT);
}

static void onPick(lv_event_t *e) {
    intptr_t i = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if(i < 0 || i >= count_) return;
    bleSelect(entries_[i]);
    uiShowMainLocked();
}

static void addCard(int i) {
    const ScanEntry &e = entries_[i];
    int w = UI_CONTENT_W;
    lv_obj_t *card = uiCard(list_, 0, 0, w, CARD_H);
    lv_obj_set_style_bg_color(card, UI_CARD_HI, LV_STATE_PRESSED);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, onPick, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));

    bool current = haveSaved_ && memcmp(saved_.addr, e.addr, 6) == 0;
    if(current) uiBox(card, 0, 10, 3, CARD_H - 20, UI_ACCENT, 2);

    lv_obj_t *name = uiLabel(card, &lv_font_montserrat_16, e.name[0] ? UI_TEXT : UI_DIM);
    lv_label_set_text(name, e.name[0] ? e.name : "Unnamed JK device");
    lv_obj_set_pos(name, 14, 6);

    char addr[18];
    bleAddrStr(e.addr, addr);
    lv_obj_t *sub = uiLabel(card, &lv_font_montserrat_12, UI_DIM);
    lv_label_set_text(sub, addr);
    lv_obj_set_pos(sub, 14, 26);

    if(current) {
        lv_obj_t *tag = uiLabel(card, &lv_font_montserrat_12, UI_ACCENT);
        lv_label_set_text(tag, "Saved");
        lv_obj_align(tag, LV_ALIGN_RIGHT_MID, -44, 0);
    }

    // Signal bars, rising left to right, bottom-aligned
    int x0 = w - 14 - (BARS * BAR_W + (BARS - 1) * BAR_GAP);
    for(int b = 0; b < BARS; b++) {
        int h = (b + 1) * BAR_STEP_H;
        cards_[i].bars[b] = uiBox(card, x0 + b * (BAR_W + BAR_GAP), CARD_H / 2 + 8 - h, BAR_W, h, UI_FAINT, 1);
    }
    showBars(i);
}

static void update(lv_timer_t *) {
    if(lv_screen_active() != screen_) return;
    ScanEntry found[MAX_ENTRIES];
    int n = blePickResults(found, MAX_ENTRIES);
    // rebuild only when the set or order changes, so taps don't land on a
    // moving list; otherwise just refresh the signal bars and names
    bool same = n == count_;
    for(int i = 0; same && i < n; i++) {
        same = memcmp(found[i].addr, entries_[i].addr, 6) == 0 && strcmp(found[i].name, entries_[i].name) == 0;
    }
    memcpy(entries_, found, n * sizeof(ScanEntry));
    if(same) {
        for(int i = 0; i < n; i++) showBars(i);
        return;
    }
    count_ = n;
    lv_obj_clean(list_);
    for(int i = 0; i < n; i++) addCard(i);
    uiSetHidden(empty_, n > 0);
}

lv_obj_t *pickerBuild() {
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, UI_BG, 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    // Header: back, title, scanning indicator
    back_ = uiBox(screen_, 4, 2, 64, UI_BAR_H - 2, UI_BG, 8);
    lv_obj_add_flag(back_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back_, UI_CARD, LV_STATE_PRESSED);
    lv_obj_add_event_cb(back_, [](lv_event_t *) { uiShowMainLocked(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = uiLabel(back_, &lv_font_montserrat_14, UI_ACCENT);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(bl);

    lv_obj_t *title = uiLabel(screen_, &lv_font_montserrat_16, UI_TEXT);
    lv_label_set_text(title, "Select device");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // Spinner: a fixed-length arc turning at constant speed
    lv_obj_t *spin = lv_arc_create(screen_);
    lv_obj_remove_style_all(spin);
    lv_obj_set_size(spin, 16, 16);
    lv_obj_align(spin, LV_ALIGN_TOP_RIGHT, -UI_PAD - 2, 5);
    lv_obj_remove_flag(spin, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(spin, 0, 360);
    lv_arc_set_angles(spin, 0, 100);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spin, UI_CARD_HI, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spin, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spin, UI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(spin, true, LV_PART_INDICATOR);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, spin);
    lv_anim_set_exec_cb(&a, [](void *arc, int32_t deg) { lv_arc_set_rotation(static_cast<lv_obj_t *>(arc), deg); });
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_duration(&a, SPIN_TURN_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    // Scrolling column of cards
    list_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_pos(list_, UI_PAD, LIST_Y);
    lv_obj_set_size(list_, UI_CONTENT_W, LIST_H);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, UI_GAP, 0);
    lv_obj_set_style_pad_bottom(list_, UI_PAD, 0);

    // Shown while nothing is found
    empty_ = uiCard(screen_, UI_PAD, LIST_Y, UI_CONTENT_W, 92);
    lv_obj_t *t1 = uiLabel(empty_, &lv_font_montserrat_16, UI_TEXT);
    lv_label_set_text(t1, "Looking for JK devices...");
    lv_obj_align(t1, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_t *t2 = uiLabel(empty_, &lv_font_montserrat_12, UI_DIM);
    lv_label_set_text(t2, "A device connected to the JK app\ndoesn't show up: close the app first.");
    lv_obj_set_style_text_align(t2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t2, LV_ALIGN_TOP_MID, 0, 44);

    lv_timer_create(update, 1000, nullptr);
    return screen_;
}

void pickerShow() {
    blePickStart();
    haveSaved_ = prefsGetDevice(saved_);
    count_ = -1;
    lv_obj_clean(list_);
    uiSetHidden(empty_, false);
    uiSetHidden(back_, !haveSaved_);
}
