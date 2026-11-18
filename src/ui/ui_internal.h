#pragma once
// Shared by the UI's pages: palette, sizes and small helpers.
// Style: dark background, flat rounded "islands" (cards), one accent color
// per meaning.

#include "bms/bms_data.h"
#include "lvgl.h"

// Palette
#define UI_BG        lv_color_hex(0x0B0D12)  // screen
#define UI_CARD      lv_color_hex(0x161A22)  // islands
#define UI_CARD_HI   lv_color_hex(0x222834)  // pressed island, tracks, inner chips
#define UI_TEXT      lv_color_hex(0xE8EAED)
#define UI_DIM       lv_color_hex(0x8A93A3)  // captions, units
#define UI_FAINT     lv_color_hex(0x4A5160)  // placeholders, inactive dots
#define UI_ACCENT    lv_color_hex(0x2DD4BF)  // teal: SOC, active things
#define UI_GOOD      lv_color_hex(0x34D399)  // green: charging, connected
#define UI_WARN      lv_color_hex(0xF5A524)  // amber: discharging, balancing, connecting
#define UI_BAD       lv_color_hex(0xF87171)  // red: highest cell, problems
#define UI_COOL      lv_color_hex(0x60A5FA)  // blue: lowest cell

#define UI_RADIUS    12
#define UI_GAP       6

// Geometry of a page: below the status bar, above the page dots
#define UI_BAR_H     26
#define UI_PAGE_W    320
#define UI_PAGE_H    (240 - UI_BAR_H)
#define UI_PAD       8                         // page margin left/right/top
#define UI_CONTENT_W (UI_PAGE_W - 2 * UI_PAD)  // 304
#define UI_CONTENT_H (UI_PAGE_H - UI_PAD - 14)  // 192, room for the dots below

// Data older than this shows as missing
#define UI_STALE_US  (10 * 1000 * 1000LL)

// An island: flat rounded box, no theme styles, not scrollable
lv_obj_t *uiCard(lv_obj_t *parent, int x, int y, int w, int h);
// A plain box with a color (dots, bars)
lv_obj_t *uiBox(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color, int radius);
// A label with font and color, no text yet
lv_obj_t *uiLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color);
// Sets the text only if it changed (each change redraws over SPI)
void uiSetText(lv_obj_t *label, const char *text);
void uiSetTextColor(lv_obj_t *obj, lv_color_t color);
void uiSetBg(lv_obj_t *obj, lv_color_t color);
void uiSetHidden(lv_obj_t *obj, bool hidden);
// A small caption in the top-left corner of an island
lv_obj_t *uiCaption(lv_obj_t *card, const char *text);
// True if the snapshot has cell data from the last UI_STALE_US
bool uiFresh(const BmsSnapshot &d);
// A JK04 balancer, or a JK02 device without a current sensor / SOC
bool uiIsBalancer(const BmsSnapshot &d);

// Pages of the main screen
enum { UI_PAGE_HOME, UI_PAGE_INFO, UI_PAGE_CELLS, UI_PAGE_RESIST, UI_PAGE_COUNT };
void homeBuild(lv_obj_t *page);
void homeUpdate(const BmsSnapshot &d);
void cellsBuild(lv_obj_t *page);
void cellsUpdate(const BmsSnapshot &d);
void resistBuild(lv_obj_t *page);
void resistUpdate(const BmsSnapshot &d);
void infoBuild(lv_obj_t *page);
void infoUpdate(const BmsSnapshot &d);

// Device picker screen
lv_obj_t *pickerBuild();
void pickerShow();  // starts the BLE scan

// Settings screen (ui_settings.cpp)
lv_obj_t *settingsBuild();
void settingsShow();     // resets it before it's loaded
void settingsRefresh();  // its update, right away

// Navigation inside the LVGL task (lock held)
void uiShowPickerLocked();
void uiShowMainLocked();
void uiShowSettingsLocked();
