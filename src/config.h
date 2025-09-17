#pragma once

#include <LVGL_CYD.h>

// Screen orientation
// Takes position of USB connector relative to screen:
// USB_DOWN  (LV_DISPLAY_ROTATION_0)
// USB_RIGHT (LV_DISPLAY_ROTATION_90)
// USB_UP    (LV_DISPLAY_ROTATION_180)
// USB_LEFT  (LV_DISPLAY_ROTATION_270)
#define SCREEN_ORIENTATION USB_RIGHT

// BMS Device configuration
// Add your JK-BMS MAC addresses here
#define BMS_MAC_ADDRESS_1 "c8:47:80:23:4f:95"
// #define BMS_MAC_ADDRESS_2 "20:aa:08:25:26:8b"
// #define BMS_MAC_ADDRESS_3 "MAC_ADDRESS_3"

// BLE Scan settings
#define BLE_SCAN_INTERVAL 100
#define BLE_SCAN_WINDOW 100
#define BLE_SCAN_TIMEOUT 5000   // Scan for 5 seconds
#define BLE_SCAN_PERIOD 10000    // Start new scan every 10 seconds if not connected

// BMS connection settings
#define BMS_CONNECTION_TIMEOUT 20000  // Connection timeout (ms)
#define BMS_NOTIFY_IGNORE_COUNT 10    // Number of notifications to ignore after parsing

// Display update interval
#define DISPLAY_UPDATE_INTERVAL 1000  // Update display every 1000ms