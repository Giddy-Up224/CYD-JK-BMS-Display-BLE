#pragma once
// The UI: a main screen with pages swiped left/right (Overview, Cells, Wire
// resistance, Device) under a status bar, and the device picker.
// Safe to call from any task (they take the LVGL lock).

void uiStart();
void uiShowPicker();  // starts the BLE scan
void uiShowMain();
void uiGoToPage(int page);
// Prints the current screen over serial (tools/shots.py)
void uiScreenshot(const char *name);
