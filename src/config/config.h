#pragma once

#include <LVGL_CYD.h>

// Screen orientation
// Takes position of USB connector relative to screen:
// USB_DOWN  (LV_DISPLAY_ROTATION_0)
// USB_RIGHT (LV_DISPLAY_ROTATION_90)
// USB_UP    (LV_DISPLAY_ROTATION_180)
// USB_LEFT  (LV_DISPLAY_ROTATION_270)
#define SCREEN_ORIENTATION USB_LEFT

// BMS Device configuration
// Devices are picked from the device list in the UI (More -> Devices) and
// saved to Preferences. This is the number of devices that can be saved and
// connected at the same time (NimBLE's default connection limit is 3).
#define MAX_BMS_DEVICES 3

// BLE Scan settings
#define BLE_SCAN_INTERVAL 100
#define BLE_SCAN_WINDOW 100
#define BLE_SCAN_TIME 5000   // Scan for 5 seconds
#define BLE_SCAN_PERIOD 10000    // Start new scan every 10 seconds if not connected
#define BLE_USER_SCAN_TIME 10000 // Scan duration when the user presses "Scan"

// BMS connection settings
#define BMS_CONNECTION_TIMEOUT 20000  // Connection timeout (ms)
#define BMS_NOTIFY_IGNORE_COUNT 10    // Number of notifications to ignore after parsing

// Display update interval
#define DISPLAY_UPDATE_INTERVAL 3000  // Update display every 3000ms