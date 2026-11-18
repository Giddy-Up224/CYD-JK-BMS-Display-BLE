#pragma once
// The screen: ILI9341 on SPI, driven through esp_lcd, with LVGL on top.
// LVGL runs in its own task. Any other task that touches LVGL objects must
// hold the lock:
//
//   LvglLock lock;
//   lv_label_set_text(label, "...");

#include "esp_err.h"

// Panel, touch and LVGL; starts the LVGL task
esp_err_t displayInit();

// Prints the next frame over serial (tools/shots.py turns it into a PNG)
void displayScreenshot(const char *name);
// Presses the screen at x, y for ms, as if touched (console `tap`)
void displayTap(int x, int y, int ms);

void lvglLock();
void lvglUnlock();

struct LvglLock {
    LvglLock() { lvglLock(); }
    ~LvglLock() { lvglUnlock(); }
    LvglLock(const LvglLock &) = delete;
    LvglLock &operator=(const LvglLock &) = delete;
};
