// Overview page. Left: a ring island with the main figure; right: three stat
// islands.
//   BMS:      ring = SOC, state pill (Charging / Discharging / Idle), Ah left;
//             stats: voltage, current, power
//   Balancer: ring = balance current against the max balance current, state
//             pill (Balancing / Waiting / Off), trigger delta;
//             stats: pack voltage, cell delta, average cell

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

static const float IDLE_A = 0.1f;  // current below this counts as idle
static const int RING_W = 150, STAT_X = UI_PAD + RING_W + UI_GAP;
static const int STAT_W = UI_PAGE_W - UI_PAD - STAT_X;  // 148
static const int STAT_H = (UI_CONTENT_H - 2 * UI_GAP) / 3;
static const int ARC = 112;

struct Stat {
    lv_obj_t *caption, *value, *unit;
};

static lv_obj_t *arc_, *big_, *bigCaption_, *pill_, *pillText_, *sub_;
static Stat stats_[3];

static void statBuild(lv_obj_t *page, int i) {
    lv_obj_t *card = uiCard(page, STAT_X, UI_PAD + i * (STAT_H + UI_GAP), STAT_W, STAT_H);
    Stat &s = stats_[i];
    s.caption = uiCaption(card, "");
    s.value = uiLabel(card, &lv_font_montserrat_20, UI_TEXT);
    lv_obj_align(s.value, LV_ALIGN_BOTTOM_LEFT, 10, -7);
    s.unit = uiLabel(card, &lv_font_montserrat_14, UI_DIM);
}

static void statSet(int i, const char *caption, const char *value, const char *unit, lv_color_t color) {
    Stat &s = stats_[i];
    uiSetText(s.caption, caption);
    uiSetText(s.value, value);
    uiSetTextColor(s.value, color);
    uiSetText(s.unit, unit);
    lv_obj_update_layout(s.value);
    lv_obj_align_to(s.unit, s.value, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);
}

void homeBuild(lv_obj_t *page) {
    lv_obj_t *card = uiCard(page, UI_PAD, UI_PAD, RING_W, UI_CONTENT_H);

    arc_ = lv_arc_create(card);
    lv_obj_remove_style_all(arc_);
    lv_obj_set_size(arc_, ARC, ARC);
    lv_obj_align(arc_, LV_ALIGN_TOP_MID, 0, 12);
    lv_arc_set_rotation(arc_, 135);
    lv_arc_set_bg_angles(arc_, 0, 270);
    lv_arc_set_range(arc_, 0, 100);
    lv_arc_set_value(arc_, 0);
    lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc_, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, UI_CARD_HI, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, UI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);

    big_ = uiLabel(arc_, &lv_font_montserrat_28, UI_TEXT);
    lv_obj_align(big_, LV_ALIGN_CENTER, 0, -4);
    bigCaption_ = uiLabel(arc_, &lv_font_montserrat_12, UI_DIM);
    lv_obj_align(bigCaption_, LV_ALIGN_CENTER, 0, 20);

    // State pill: colored dot + text on a darker chip
    pill_ = uiBox(card, 0, 0, 120, 24, UI_CARD_HI, 12);
    lv_obj_align(pill_, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_flex_flow(pill_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill_, 6, 0);
    uiBox(pill_, 0, 0, 8, 8, UI_FAINT, LV_RADIUS_CIRCLE);
    pillText_ = uiLabel(pill_, &lv_font_montserrat_14, UI_TEXT);

    sub_ = uiLabel(card, &lv_font_montserrat_12, UI_DIM);
    lv_obj_align(sub_, LV_ALIGN_BOTTOM_MID, 0, -9);

    for(int i = 0; i < 3; i++) statBuild(page, i);
}

static void setPill(const char *text, lv_color_t color) {
    uiSetText(pillText_, text);
    uiSetBg(lv_obj_get_child(pill_, 0), color);
}

static void setRing(int value, lv_color_t color) {
    if(value < 0) value = 0;
    if(value > 100) value = 100;
    if(lv_arc_get_value(arc_) != value) lv_arc_set_value(arc_, value);
    if(!lv_color_eq(lv_obj_get_style_arc_color(arc_, LV_PART_INDICATOR), color)) {
        lv_obj_set_style_arc_color(arc_, color, LV_PART_INDICATOR);
    }
}

static void setSub(const char *text) {
    uiSetText(sub_, text);
    lv_obj_align(sub_, LV_ALIGN_BOTTOM_MID, 0, -9);
}

static void showEmpty(const BmsSnapshot &d) {
    setRing(0, UI_ACCENT);
    uiSetText(big_, "--");
    uiSetText(bigCaption_, "");
    const char *state = d.link == LinkState::Connecting ? "Connecting"
                        : d.link == LinkState::Connected ? "Waiting"
                                                          : "No device";
    setPill(state, d.link == LinkState::NoDevice ? UI_FAINT : UI_WARN);
    setSub(d.link == LinkState::NoDevice ? "Hold the top bar to pick" : "");
    statSet(0, "Voltage", "--", "V", UI_TEXT);
    statSet(1, "Current", "--", "A", UI_TEXT);
    statSet(2, "Power", "--", "W", UI_TEXT);
}

static void showBms(const BmsSnapshot &d) {
    const JkCells &c = d.cells;
    char buf[32], buf2[16];
    lv_color_t socColor = c.soc <= 10 ? UI_BAD : c.soc <= 25 ? UI_WARN : UI_ACCENT;
    setRing(c.soc, socColor);
    snprintf(buf, sizeof(buf), "%d%%", c.soc);
    uiSetText(big_, buf);
    uiSetText(bigCaption_, "State of charge");

    if(c.currentA > IDLE_A) {
        setPill("Charging", UI_GOOD);
    } else if(c.currentA < -IDLE_A) {
        setPill("Discharging", UI_WARN);
    } else {
        setPill("Idle", UI_DIM);
    }
    snprintf(buf, sizeof(buf), "%.1f / %.0f Ah", c.remainAh, c.nominalAh);
    setSub(buf);

    snprintf(buf, sizeof(buf), "%.2f", c.packV);
    statSet(0, "Voltage", buf, "V", UI_TEXT);
    snprintf(buf, sizeof(buf), "%.2f", c.currentA);
    lv_color_t ic = c.currentA > IDLE_A ? UI_GOOD : c.currentA < -IDLE_A ? UI_WARN : UI_TEXT;
    statSet(1, "Current", buf, "A", ic);
    float w = fabsf(c.powerW);
    if(w >= 1000) {
        snprintf(buf, sizeof(buf), "%.2f", w / 1000);
        strlcpy(buf2, "kW", sizeof(buf2));
    } else {
        snprintf(buf, sizeof(buf), "%.0f", w);
        strlcpy(buf2, "W", sizeof(buf2));
    }
    statSet(2, "Power", buf, buf2, UI_TEXT);
}

static void showBalancer(const BmsSnapshot &d) {
    const JkCells &c = d.cells;
    const JkSettings &s = d.settings;
    char buf[32];
    float a = fabsf(c.balanceA);
    bool active = c.balancing != 0;
    float maxA = s.valid && s.maxBalanceA > 0 ? s.maxBalanceA : 2.0f;
    setRing(static_cast<int>(lroundf(a / maxA * 100)), UI_WARN);
    snprintf(buf, sizeof(buf), "%.2f", active ? a : 0.0f);
    uiSetText(big_, buf);
    uiSetText(bigCaption_, "Balance A");

    if(s.valid && !s.balanceEnabled) {
        setPill("Off", UI_FAINT);
    } else if(active) {
        setPill("Balancing", UI_WARN);
    } else {
        setPill("Waiting", UI_DIM);
    }
    if(s.valid) {
        snprintf(buf, sizeof(buf), "Starts at %.0f mV delta", s.triggerDeltaV * 1000);
    } else {
        buf[0] = '\0';
    }
    setSub(buf);

    snprintf(buf, sizeof(buf), "%.2f", c.packV);
    statSet(0, "Pack voltage", buf, "V", UI_TEXT);
    snprintf(buf, sizeof(buf), "%.0f", c.deltaV * 1000);
    bool over = s.valid && s.triggerDeltaV > 0 && c.deltaV >= s.triggerDeltaV;
    statSet(1, "Cell delta", buf, "mV", over ? UI_WARN : UI_TEXT);
    snprintf(buf, sizeof(buf), "%.3f", c.avgV);
    statSet(2, "Average cell", buf, "V", UI_TEXT);
}

void homeUpdate(const BmsSnapshot &d) {
    if(!uiFresh(d)) {
        showEmpty(d);
    } else if(uiIsBalancer(d)) {
        showBalancer(d);
    } else {
        showBms(d);
    }
}
