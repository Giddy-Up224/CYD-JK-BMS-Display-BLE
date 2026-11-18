// Device page: what is connected (model, versions, protocol) with a button to
// pick another device, the balancing settings (tap: Settings screen), and
// status figures (uptime; temperatures and cycles on a BMS).

#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

static const int TOP_H = 62;
static const int ROWS = 6;
static const int ROW_H = 16;
static const int COL_W = (UI_CONTENT_W - UI_GAP) / 2;  // 149

struct KvList {
    lv_obj_t *key[ROWS], *value[ROWS];
};

static lv_obj_t *model_, *versions_, *serial_;
static KvList settings_, status_;

static lv_obj_t *kvBuild(KvList &kv, lv_obj_t *page, int x, const char *caption) {
    int y = UI_PAD + TOP_H + UI_GAP;
    lv_obj_t *card = uiCard(page, x, y, COL_W, UI_CONTENT_H - TOP_H - UI_GAP);
    uiCaption(card, caption);
    for(int i = 0; i < ROWS; i++) {
        kv.key[i] = uiLabel(card, &lv_font_montserrat_12, UI_DIM);
        lv_obj_set_pos(kv.key[i], 10, 24 + i * ROW_H);
        kv.value[i] = uiLabel(card, &lv_font_montserrat_12, UI_TEXT);
        lv_obj_set_width(kv.value[i], COL_W - 20 - 50);
        lv_obj_set_style_text_align(kv.value[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(kv.value[i], 60, 24 + i * ROW_H);
    }
    return card;
}

static void kvSet(KvList &kv, int i, const char *key, const char *value) {
    uiSetText(kv.key[i], key);
    uiSetText(kv.value[i], value);
}

static void kvClearFrom(KvList &kv, int from) {
    for(int i = from; i < ROWS; i++) kvSet(kv, i, "", "");
}

void infoBuild(lv_obj_t *page) {
    lv_obj_t *top = uiCard(page, UI_PAD, UI_PAD, UI_CONTENT_W, TOP_H);
    model_ = uiLabel(top, &lv_font_montserrat_16, UI_TEXT);
    lv_obj_set_pos(model_, 12, 8);
    versions_ = uiLabel(top, &lv_font_montserrat_12, UI_DIM);
    lv_obj_set_pos(versions_, 12, 29);
    serial_ = uiLabel(top, &lv_font_montserrat_12, UI_DIM);
    lv_obj_set_pos(serial_, 12, 44);

    // Change device: a pill button on the right
    lv_obj_t *btn = uiBox(top, 0, 0, 92, 34, UI_CARD_HI, 17);
    lv_obj_align(btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, UI_FAINT, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, [](lv_event_t *) { uiShowPickerLocked(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = uiLabel(btn, &lv_font_montserrat_14, UI_ACCENT);
    lv_label_set_text(l, LV_SYMBOL_BLUETOOTH " Change");
    lv_obj_center(l);

    // Balancing card: tap to change the settings (and the name)
    lv_obj_t *bal = kvBuild(settings_, page, UI_PAD, "Balancing");
    lv_obj_add_flag(bal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(bal, UI_CARD_HI, LV_STATE_PRESSED);
    lv_obj_add_event_cb(bal, [](lv_event_t *) { uiShowSettingsLocked(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *edit = uiLabel(bal, &lv_font_montserrat_12, UI_ACCENT);
    lv_label_set_text(edit, LV_SYMBOL_EDIT " Edit");
    lv_obj_align(edit, LV_ALIGN_TOP_RIGHT, -10, 6);
    kvBuild(status_, page, UI_PAD + COL_W + UI_GAP, "Status");
}

static void fmtUptime(uint32_t s, char *buf, size_t size) {
    uint32_t d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
    if(d) {
        snprintf(buf, size, "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)m);
    } else {
        snprintf(buf, size, "%luh %lum", (unsigned long)h, (unsigned long)m);
    }
}

void infoUpdate(const BmsSnapshot &d) {
    char buf[48];
    const JkInfo &info = d.info;
    if(info.valid) {
        uiSetText(model_, info.model[0] ? info.model : "JK device");
        snprintf(buf, sizeof(buf), "HW %s  SW %s  %s", info.hw, info.sw, jkProtoName(d.proto));
        uiSetText(versions_, buf);
        if(info.serial[0]) {
            snprintf(buf, sizeof(buf), "%s  SN %s", d.addr, info.serial);
        } else {
            snprintf(buf, sizeof(buf), "%s", d.addr);
        }
        uiSetText(serial_, buf);
    } else {
        uiSetText(model_, d.name[0] ? d.name : "No device");
        uiSetText(versions_, d.link == LinkState::NoDevice ? "Tap Change to pick one" : "Waiting for device info");
        uiSetText(serial_, d.addr);
    }

    const JkSettings &s = d.settings;
    if(s.valid) {
        snprintf(buf, sizeof(buf), "%s", s.balanceEnabled ? "On" : "Off");
        kvSet(settings_, 0, "Balancer", buf);
        snprintf(buf, sizeof(buf), "%.3f V", s.startBalanceV);
        kvSet(settings_, 1, "Start", buf);
        snprintf(buf, sizeof(buf), "%.0f mV", s.triggerDeltaV * 1000);
        kvSet(settings_, 2, "Trigger", buf);
        snprintf(buf, sizeof(buf), "%.2f A", s.maxBalanceA);
        kvSet(settings_, 3, "Max curr.", buf);
        snprintf(buf, sizeof(buf), "%d", s.cellCount);
        kvSet(settings_, 4, "Cells", buf);
        snprintf(buf, sizeof(buf), "%.3f V", s.powerOffV);
        kvSet(settings_, 5, "Power off", buf);
    } else {
        kvSet(settings_, 0, "Waiting...", "");
        kvClearFrom(settings_, 1);
    }

    if(!uiFresh(d)) {
        kvSet(status_, 0, d.link == LinkState::Connected ? "Waiting..." : "Offline", "");
        kvClearFrom(status_, 1);
        return;
    }
    const JkCells &c = d.cells;
    int row = 0;
    fmtUptime(c.uptimeS, buf, sizeof(buf));
    kvSet(status_, row++, "Uptime", buf);
    snprintf(buf, sizeof(buf), "%d", c.cellCount);
    kvSet(status_, row++, "Cells", buf);
    if(c.hasBms && !uiIsBalancer(d)) {
        snprintf(buf, sizeof(buf), "%.1f / %.1f C", c.t1C, c.t2C);
        kvSet(status_, row++, "Battery", buf);
        snprintf(buf, sizeof(buf), "%.1f C", c.mosC);
        kvSet(status_, row++, "MOS", buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)c.cycles);
        kvSet(status_, row++, "Cycles", buf);
        snprintf(buf, sizeof(buf), "%s%s", c.chargeOn ? "CHG " : "", c.dischargeOn ? "DSG" : "");
        kvSet(status_, row++, "Switches", buf[0] ? buf : "off");
    } else {
        snprintf(buf, sizeof(buf), "%.3f V", c.maxV - c.minV);
        kvSet(status_, row++, "Spread", buf);
        if(info.valid) {
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)info.powerOnCount);
            kvSet(status_, row++, "Power-ons", buf);
        }
    }
    kvClearFrom(status_, row);
}
