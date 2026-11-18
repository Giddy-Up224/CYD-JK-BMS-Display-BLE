// Settings screen: the device's name and balancing settings, changed on the
// display. Opened from the Device page.
//
// A row per setting; tap one to edit it in a dialog with - / + (hold to
// repeat), or flip the balancing switch. The name opens a keyboard. Every
// change is read back: a JK device ignores values it doesn't accept without
// saying so, so the dialog waits for the next settings (or info) frame and
// shows whether the device took it.
//
// Cell count and power off voltage are shown but not editable here: they are
// set once when the balancer is installed, and a wrong cell count upsets it.

#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ble/jk_ble.h"
#include "esp_timer.h"
#include "ui_internal.h"

static const int LIST_Y = UI_BAR_H + 4;
static const int ROW_H = 40;
static const int64_t ANSWER_AFTER_US = 300 * 1000;   // the write's read back comes after this
static const int64_t ANSWER_TIMEOUT_US = 4 * 1000 * 1000;
static const int TOAST_MS = 2500;

// A part and a state as one style selector (LVGL's two enums don't mix in C++)
static constexpr lv_style_selector_t sel(uint32_t part, uint32_t state) {
    return static_cast<lv_style_selector_t>(part | state);
}

// Values are handled in thousandths (mV, mA) so the steps add up exactly
static int32_t milli(float v) {
    return static_cast<int32_t>(v * 1000.0f + (v >= 0 ? 0.5f : -0.5f));
}

static void fmtValue(JkSetting s, int32_t m, char *buf, size_t size) {
    switch(s) {
    case JkSetting::TriggerDeltaV: snprintf(buf, size, "%ld mV", static_cast<long>(m)); break;
    case JkSetting::MaxBalanceA: snprintf(buf, size, "%.1f A", m / 1000.0f); break;
    case JkSetting::CellCount: snprintf(buf, size, "%ld", static_cast<long>(m / 1000)); break;
    case JkSetting::Balancing: snprintf(buf, size, "%s", m ? "On" : "Off"); break;
    default: snprintf(buf, size, "%.2f V", m / 1000.0f); break;
    }
}

// --- Rows

enum class RowKind : uint8_t { Name, Switch, Value, ReadOnly };

struct Row {
    RowKind kind;
    JkSetting setting;
    lv_obj_t *card, *value, *sw;
};

static const int MAX_ROWS = 8;
static Row rows_[MAX_ROWS];
static int rowCount_;
static lv_obj_t *screen_, *list_, *note_, *toast_;
static lv_timer_t *toastTimer_;

// --- What we're waiting on

enum class Wait : uint8_t { None, Value, Switch, Name };
static struct {
    Wait kind;
    JkSetting setting;
    int32_t target;   // milli
    char name[JK_NAME_MAX + 1];
    int64_t sinceUs;
} wait_;

// --- Editor dialog

static lv_obj_t *editScrim_, *editTitle_, *editValue_, *editRange_, *editStatus_, *editSave_, *editMinus_, *editPlus_;
static JkSetting editSetting_;
static int32_t editMilli_, editStep_, editMin_, editMax_;
static int repeats_;

// --- Rename

static lv_obj_t *renameView_, *renameText_, *renameStatus_, *keyboard_;

// --- Helpers

static lv_obj_t *button(lv_obj_t *parent, int w, int h, const char *text, lv_color_t bg, lv_color_t fg,
                        lv_event_cb_t cb) {
    lv_obj_t *b = uiBox(parent, 0, 0, w, h, bg, h / 2 < 12 ? h / 2 : 12);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(b, UI_FAINT, LV_STATE_PRESSED);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t *l = uiLabel(b, &lv_font_montserrat_16, fg);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

static void setEnabled(lv_obj_t *o, bool on) {
    if(on) {
        lv_obj_remove_state(o, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(o, LV_STATE_DISABLED);
    }
}

static void showToast(const char *text, lv_color_t color) {
    lv_obj_t *l = lv_obj_get_child(toast_, 0);
    lv_label_set_text(l, text);
    uiSetTextColor(l, color);
    uiSetHidden(toast_, false);
    lv_obj_move_foreground(toast_);
    lv_timer_reset(toastTimer_);
    lv_timer_resume(toastTimer_);
}

static bool connected(const BmsSnapshot &d) {
    return d.link == LinkState::Connected && d.settings.valid && d.proto != JkProto::Unknown;
}

// --- Editor

static void editShowValue() {
    char buf[24];
    fmtValue(editSetting_, editMilli_, buf, sizeof(buf));
    lv_label_set_text(editValue_, buf);
    setEnabled(editMinus_, editMilli_ > editMin_);
    setEnabled(editPlus_, editMilli_ < editMax_);
}

static void editStatus(const char *text, lv_color_t color) {
    lv_label_set_text(editStatus_, text);
    uiSetTextColor(editStatus_, color);
}

static void editBusy(bool busy) {
    setEnabled(editSave_, !busy);
    if(busy) {
        setEnabled(editMinus_, false);
        setEnabled(editPlus_, false);
    } else {
        editShowValue();
    }
}

static void editClose() {
    if(wait_.kind == Wait::Value) wait_.kind = Wait::None;
    uiSetHidden(editScrim_, true);
}

static void editStep(lv_event_t *e) {
    int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_PRESSED) {
        repeats_ = 0;
        return;
    }
    if(code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        // holding: speeds up to 10 steps at a time after a second or so
        int n = (code == LV_EVENT_LONG_PRESSED_REPEAT && ++repeats_ > 10) ? 10 : 1;
        int32_t v = editMilli_ + dir * n * editStep_;
        v = (v / editStep_) * editStep_;  // stay on the step grid
        if(v < editMin_) v = editMin_;
        if(v > editMax_) v = editMax_;
        editMilli_ = v;
        editShowValue();
        editStatus("", UI_DIM);
    }
}

static void editOpen(JkSetting s) {
    BmsSnapshot d;
    bmsDataGet(d);
    if(!connected(d)) {
        showToast("Not connected", UI_WARN);
        return;
    }
    const JkSettingInfo &info = jkSettingInfo(s);
    editSetting_ = s;
    editStep_ = milli(info.step);
    editMin_ = (milli(info.min) + editStep_ - 1) / editStep_ * editStep_;
    editMax_ = milli(info.max) / editStep_ * editStep_;
    editMilli_ = milli(jkSettingValue(d.settings, s));
    lv_label_set_text(editTitle_, info.label);
    char lo[16], hi[16], buf[40];
    fmtValue(s, editMin_, lo, sizeof(lo));
    fmtValue(s, editMax_, hi, sizeof(hi));
    snprintf(buf, sizeof(buf), "%s - %s", lo, hi);
    lv_label_set_text(editRange_, buf);
    editStatus("", UI_DIM);
    editBusy(false);
    uiSetHidden(editScrim_, false);
    lv_obj_move_foreground(editScrim_);
}

static void editSave(lv_event_t *) {
    if(!bleWriteSetting(editSetting_, editMilli_ / 1000.0f)) {
        editStatus("Not connected", UI_BAD);
        return;
    }
    wait_.kind = Wait::Value;
    wait_.setting = editSetting_;
    wait_.target = editMilli_;
    wait_.sinceUs = esp_timer_get_time();
    editStatus("Saving...", UI_DIM);
    editBusy(true);
}

static void buildEditor() {
    editScrim_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(editScrim_);
    lv_obj_set_size(editScrim_, 320, 240);
    lv_obj_set_style_bg_color(editScrim_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(editScrim_, LV_OPA_70, 0);
    lv_obj_add_flag(editScrim_, LV_OBJ_FLAG_CLICKABLE);  // swallows taps behind the dialog
    lv_obj_remove_flag(editScrim_, LV_OBJ_FLAG_SCROLLABLE);

    const int W = 272, H = 180;
    lv_obj_t *card = uiCard(editScrim_, 0, 0, W, H);
    lv_obj_center(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    editTitle_ = uiLabel(card, &lv_font_montserrat_16, UI_TEXT);
    lv_obj_align(editTitle_, LV_ALIGN_TOP_MID, 0, 10);
    editValue_ = uiLabel(card, &lv_font_montserrat_28, UI_ACCENT);
    lv_obj_align(editValue_, LV_ALIGN_TOP_MID, 0, 42);
    editRange_ = uiLabel(card, &lv_font_montserrat_12, UI_DIM);
    lv_obj_align(editRange_, LV_ALIGN_TOP_MID, 0, 80);
    editStatus_ = uiLabel(card, &lv_font_montserrat_12, UI_DIM);
    lv_obj_align(editStatus_, LV_ALIGN_TOP_MID, 0, 102);

    editMinus_ = button(card, 52, 48, LV_SYMBOL_MINUS, UI_CARD_HI, UI_TEXT, nullptr);
    lv_obj_align(editMinus_, LV_ALIGN_TOP_LEFT, 12, 36);
    editPlus_ = button(card, 52, 48, LV_SYMBOL_PLUS, UI_CARD_HI, UI_TEXT, nullptr);
    lv_obj_align(editPlus_, LV_ALIGN_TOP_RIGHT, -12, 36);
    for(lv_obj_t *b : {editMinus_, editPlus_}) {
        void *dir = reinterpret_cast<void *>(static_cast<intptr_t>(b == editPlus_ ? 1 : -1));
        lv_obj_add_event_cb(b, editStep, LV_EVENT_PRESSED, dir);
        lv_obj_add_event_cb(b, editStep, LV_EVENT_SHORT_CLICKED, dir);
        lv_obj_add_event_cb(b, editStep, LV_EVENT_LONG_PRESSED_REPEAT, dir);
    }

    lv_obj_t *cancel = button(card, 118, 40, "Cancel", UI_CARD_HI, UI_TEXT, [](lv_event_t *) { editClose(); });
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    editSave_ = button(card, 118, 40, "Save", UI_ACCENT, UI_BG, editSave);
    lv_obj_align(editSave_, LV_ALIGN_BOTTOM_RIGHT, -12, -12);

    lv_obj_add_flag(editScrim_, LV_OBJ_FLAG_HIDDEN);
}

// --- Rename

static void renameClose() {
    if(wait_.kind == Wait::Name) wait_.kind = Wait::None;
    uiSetHidden(renameView_, true);
}

static void renameStatus(const char *text, lv_color_t color) {
    lv_label_set_text(renameStatus_, text);
    uiSetTextColor(renameStatus_, color);
}

static void renameSave() {
    const char *name = lv_textarea_get_text(renameText_);
    if(!bleRename(name)) {
        BmsSnapshot d;
        bmsDataGet(d);
        renameStatus(connected(d) ? "1 to 13 letters, digits or symbols" : "Not connected", UI_BAD);
        return;
    }
    wait_.kind = Wait::Name;
    strlcpy(wait_.name, name, sizeof(wait_.name));
    wait_.sinceUs = esp_timer_get_time();
    renameStatus("Saving...", UI_DIM);
}

static void renameOpen() {
    BmsSnapshot d;
    bmsDataGet(d);
    if(!connected(d)) {
        showToast("Not connected", UI_WARN);
        return;
    }
    lv_textarea_set_text(renameText_, d.info.valid ? d.info.name : "");
    renameStatus("Up to 13 characters", UI_DIM);
    uiSetHidden(renameView_, false);
    lv_obj_move_foreground(renameView_);
}

static void styleKeyboard(lv_obj_t *kb) {
    lv_obj_set_style_bg_color(kb, UI_BG, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_pad_all(kb, 4, 0);
    lv_obj_set_style_pad_gap(kb, 4, 0);
    lv_obj_set_style_bg_color(kb, UI_CARD, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, UI_CARD_HI, sel(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_bg_color(kb, UI_FAINT, sel(LV_PART_ITEMS, LV_STATE_PRESSED));
    lv_obj_set_style_text_color(kb, UI_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, UI_TEXT, sel(LV_PART_ITEMS, LV_STATE_CHECKED));
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
}

static void buildRename() {
    renameView_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(renameView_);
    lv_obj_set_size(renameView_, 320, 240);
    lv_obj_set_style_bg_color(renameView_, UI_BG, 0);
    lv_obj_set_style_bg_opa(renameView_, LV_OPA_COVER, 0);
    lv_obj_add_flag(renameView_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(renameView_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cancel = button(renameView_, 76, UI_BAR_H - 2, "Cancel", UI_BG, UI_ACCENT,
                              [](lv_event_t *) { renameClose(); });
    lv_obj_set_style_text_font(lv_obj_get_child(cancel, 0), &lv_font_montserrat_14, 0);
    lv_obj_set_pos(cancel, 4, 2);
    lv_obj_t *title = uiLabel(renameView_, &lv_font_montserrat_16, UI_TEXT);
    lv_label_set_text(title, "Rename");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_t *save = button(renameView_, 64, UI_BAR_H - 2, "Save", UI_BG, UI_ACCENT,
                            [](lv_event_t *) { renameSave(); });
    lv_obj_set_style_text_font(lv_obj_get_child(save, 0), &lv_font_montserrat_14, 0);
    lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -4, 2);

    renameText_ = lv_textarea_create(renameView_);
    lv_obj_set_pos(renameText_, UI_PAD, UI_BAR_H + 4);
    lv_obj_set_size(renameText_, UI_CONTENT_W, 38);
    lv_textarea_set_one_line(renameText_, true);
    lv_textarea_set_max_length(renameText_, JK_NAME_MAX);
    lv_obj_set_style_text_font(renameText_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(renameText_, UI_TEXT, 0);
    for(lv_style_selector_t sel : {static_cast<lv_style_selector_t>(0), static_cast<lv_style_selector_t>(LV_STATE_FOCUSED)}) {
        lv_obj_set_style_bg_color(renameText_, UI_CARD, sel);
        lv_obj_set_style_border_width(renameText_, 0, sel);
        lv_obj_set_style_outline_width(renameText_, 0, sel);
        lv_obj_set_style_radius(renameText_, 8, sel);
    }
    lv_obj_set_style_border_color(renameText_, UI_ACCENT, sel(LV_PART_CURSOR, LV_STATE_FOCUSED));
    lv_obj_add_state(renameText_, LV_STATE_FOCUSED);  // shows the cursor

    renameStatus_ = uiLabel(renameView_, &lv_font_montserrat_12, UI_DIM);
    lv_obj_set_pos(renameStatus_, UI_PAD + 4, UI_BAR_H + 46);

    keyboard_ = lv_keyboard_create(renameView_);
    lv_obj_set_size(keyboard_, 320, 240 - (UI_BAR_H + 64));
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard_, renameText_);
    styleKeyboard(keyboard_);
    lv_obj_add_event_cb(keyboard_, [](lv_event_t *) { renameSave(); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(keyboard_, [](lv_event_t *) { renameClose(); }, LV_EVENT_CANCEL, nullptr);

    lv_obj_add_flag(renameView_, LV_OBJ_FLAG_HIDDEN);
}

// --- Rows

static void onRow(lv_event_t *e) {
    Row &r = rows_[reinterpret_cast<intptr_t>(lv_event_get_user_data(e))];
    if(r.kind == RowKind::Name) {
        renameOpen();
    } else if(r.kind == RowKind::Value) {
        editOpen(r.setting);
    }
}

static void onSwitch(lv_event_t *e) {
    Row &r = rows_[reinterpret_cast<intptr_t>(lv_event_get_user_data(e))];
    bool on = lv_obj_has_state(r.sw, LV_STATE_CHECKED);
    if(!bleWriteSetting(r.setting, on ? 1.0f : 0.0f)) {
        showToast("Not connected", UI_WARN);
        lv_obj_set_state(r.sw, LV_STATE_CHECKED, !on);
        return;
    }
    wait_.kind = Wait::Switch;
    wait_.setting = r.setting;
    wait_.target = on ? 1000 : 0;
    wait_.sinceUs = esp_timer_get_time();
    lv_obj_add_state(r.sw, LV_STATE_DISABLED);
}

static Row &addRow(RowKind kind, JkSetting s, const char *key) {
    int i = rowCount_++;
    Row &r = rows_[i];
    r.kind = kind;
    r.setting = s;
    r.sw = nullptr;
    r.card = uiCard(list_, 0, 0, UI_CONTENT_W, ROW_H);
    bool tappable = kind == RowKind::Name || kind == RowKind::Value;
    if(tappable) {
        lv_obj_add_flag(r.card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(r.card, UI_CARD_HI, LV_STATE_PRESSED);
        lv_obj_add_event_cb(r.card, onRow, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }
    lv_obj_t *k = uiLabel(r.card, &lv_font_montserrat_14, kind == RowKind::ReadOnly ? UI_DIM : UI_TEXT);
    lv_label_set_text(k, key);
    lv_obj_align(k, LV_ALIGN_LEFT_MID, 12, 0);

    if(kind == RowKind::Switch) {
        r.sw = lv_switch_create(r.card);
        lv_obj_set_size(r.sw, 46, 24);
        lv_obj_align(r.sw, LV_ALIGN_RIGHT_MID, -12, 0);
        lv_obj_set_style_bg_color(r.sw, UI_CARD_HI, 0);
        lv_obj_set_style_bg_color(r.sw, UI_ACCENT, sel(LV_PART_INDICATOR, LV_STATE_CHECKED));
        lv_obj_set_style_bg_color(r.sw, UI_TEXT, LV_PART_KNOB);
        lv_obj_set_style_opa(r.sw, LV_OPA_50, LV_STATE_DISABLED);
        lv_obj_add_event_cb(r.sw, onSwitch, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        r.value = nullptr;
        return r;
    }
    r.value = uiLabel(r.card, &lv_font_montserrat_14, kind == RowKind::ReadOnly ? UI_DIM : UI_ACCENT);
    lv_obj_align(r.value, LV_ALIGN_RIGHT_MID, tappable ? -30 : -12, 0);
    if(tappable) {
        lv_obj_t *chev = uiLabel(r.card, &lv_font_montserrat_12, UI_FAINT);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -12, 0);
    }
    return r;
}

// --- Updates

static void finishWait(bool ok, const BmsSnapshot &d) {
    Wait kind = wait_.kind;
    wait_.kind = Wait::None;
    char buf[48], now[24];
    switch(kind) {
    case Wait::Value:
        if(ok) {
            editStatus(LV_SYMBOL_OK " Saved", UI_GOOD);
            lv_timer_t *t = lv_timer_create([](lv_timer_t *) { editClose(); }, 700, nullptr);
            lv_timer_set_repeat_count(t, 1);
        } else {
            if(d.settingsUs > wait_.sinceUs) {
                fmtValue(wait_.setting, milli(jkSettingValue(d.settings, wait_.setting)), now, sizeof(now));
                snprintf(buf, sizeof(buf), "Not accepted, still %s", now);
            } else {
                snprintf(buf, sizeof(buf), "No answer from the device");
            }
            editStatus(buf, UI_BAD);
            editBusy(false);
        }
        break;
    case Wait::Switch:
        if(!ok) showToast(d.settingsUs > wait_.sinceUs ? "Not accepted by the device" : "No answer from the device", UI_BAD);
        break;  // the row shows the device's state on the next update
    case Wait::Name:
        if(ok) {
            renameClose();
            showToast(LV_SYMBOL_OK " Renamed", UI_GOOD);
        } else {
            renameStatus(d.infoUs > wait_.sinceUs ? "Not accepted by the device" : "No answer from the device", UI_BAD);
        }
        break;
    default: break;
    }
}

static void checkWait(const BmsSnapshot &d) {
    if(wait_.kind == Wait::None) return;
    int64_t now = esp_timer_get_time();
    int64_t after = wait_.sinceUs + ANSWER_AFTER_US;
    if(wait_.kind == Wait::Name) {
        if(d.infoUs > after) {
            finishWait(strcmp(d.info.name, wait_.name) == 0, d);
            return;
        }
    } else if(d.settingsUs > after) {
        int32_t got = milli(jkSettingValue(d.settings, wait_.setting));
        int32_t tol = wait_.kind == Wait::Switch ? 0 : milli(jkSettingInfo(wait_.setting).step) / 2;
        finishWait(abs(got - wait_.target) <= tol, d);
        return;
    }
    if(now - wait_.sinceUs > ANSWER_TIMEOUT_US) finishWait(false, d);
}

static void update(lv_timer_t *) {
    if(lv_screen_active() != screen_) return;
    BmsSnapshot d;
    bmsDataGet(d);
    checkWait(d);
    bool on = connected(d);
    char buf[24];
    for(int i = 0; i < rowCount_; i++) {
        Row &r = rows_[i];
        bool supported = r.kind == RowKind::Name || !on || jkSettingSupported(d.proto, r.setting);
        uiSetHidden(r.card, !supported);
        if(r.kind == RowKind::Name) {
            uiSetText(r.value, on && d.info.valid ? (d.info.name[0] ? d.info.name : "-") : "--");
        } else if(r.kind == RowKind::Switch) {
            bool waiting = wait_.kind == Wait::Switch && wait_.setting == r.setting;
            if(!waiting) {
                bool state = on && jkSettingValue(d.settings, r.setting) >= 0.5f;
                if(state != lv_obj_has_state(r.sw, LV_STATE_CHECKED)) lv_obj_set_state(r.sw, LV_STATE_CHECKED, state);
                if(on) {
                    lv_obj_remove_state(r.sw, LV_STATE_DISABLED);
                } else {
                    lv_obj_add_state(r.sw, LV_STATE_DISABLED);
                }
            }
        } else if(on) {
            fmtValue(r.setting, milli(jkSettingValue(d.settings, r.setting)), buf, sizeof(buf));
            uiSetText(r.value, buf);
        } else {
            uiSetText(r.value, "--");
        }
    }
    uiSetText(note_, on ? "Cell count and power off voltage: change them in the JK app."
                        : "Not connected. Settings show once the device sends them.");
}

// --- Screen

lv_obj_t *settingsBuild() {
    screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen_, UI_BG, 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = button(screen_, 64, UI_BAR_H - 2, LV_SYMBOL_LEFT " Back", UI_BG, UI_ACCENT,
                            [](lv_event_t *) { uiShowMainLocked(); });
    lv_obj_set_style_text_font(lv_obj_get_child(back, 0), &lv_font_montserrat_14, 0);
    lv_obj_set_pos(back, 4, 2);
    lv_obj_t *title = uiLabel(screen_, &lv_font_montserrat_16, UI_TEXT);
    lv_label_set_text(title, "Settings");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    list_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_pos(list_, UI_PAD, LIST_Y);
    lv_obj_set_size(list_, UI_CONTENT_W, 240 - LIST_Y);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, UI_GAP, 0);
    lv_obj_set_style_pad_bottom(list_, UI_PAD, 0);

    addRow(RowKind::Name, JkSetting::Count, "Name");
    addRow(RowKind::Switch, JkSetting::Balancing, "Balancing");
    addRow(RowKind::Value, JkSetting::StartBalanceV, "Start voltage");
    addRow(RowKind::Value, JkSetting::TriggerDeltaV, "Trigger delta");
    addRow(RowKind::Value, JkSetting::MaxBalanceA, "Max balance current");
    addRow(RowKind::ReadOnly, JkSetting::CellCount, "Cell count");
    addRow(RowKind::ReadOnly, JkSetting::PowerOffV, "Power off voltage");

    note_ = uiLabel(list_, &lv_font_montserrat_12, UI_DIM);
    lv_obj_set_width(note_, UI_CONTENT_W - 8);
    lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_pad_left(note_, 4, 0);

    toast_ = uiBox(screen_, 0, 0, 240, 32, UI_CARD_HI, 16);
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_t *tl = uiLabel(toast_, &lv_font_montserrat_14, UI_TEXT);
    lv_obj_center(tl);
    lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
    toastTimer_ = lv_timer_create([](lv_timer_t *t) {
        uiSetHidden(toast_, true);
        lv_timer_pause(t);
    }, TOAST_MS, nullptr);
    lv_timer_pause(toastTimer_);

    buildEditor();
    buildRename();
    lv_timer_create(update, 150, nullptr);
    return screen_;
}

void settingsShow() {
    wait_.kind = Wait::None;
    uiSetHidden(editScrim_, true);
    uiSetHidden(renameView_, true);
    uiSetHidden(toast_, true);
    lv_obj_scroll_to_y(list_, 0, LV_ANIM_OFF);
}

bool settingsActive() {
    return lv_screen_active() == screen_;
}

// The screen's update, run once right away (when it was just loaded)
void settingsRefresh() {
    update(nullptr);
}
