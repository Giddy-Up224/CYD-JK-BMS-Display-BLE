#include <Arduino.h>
#include <NimBLEDevice.h>
#include <LVGL_CYD.h>
#include <lvgl.h>
#include <Preferences.h>

// Project modules
#include "config.h"
#include "utils/utils.h"
#include "navigation.h"
#include "jkbms.h"
#include "ui/screens.h"
#include "prefs.h"

//********************************************
// Global Variables
//********************************************

// Create prefs object to store settings etc. 
Preferences prefs;

// get stored mac address, default to "c8:47:80:23:4f:95" if not set
// TODO: change to use results from scanning to populate this list. 
// Save to preferences when selected.
//String mac_address1 = prefs.getString("mac1", "c8:47:80:23:4f:95");
char mac_addr [18];
  
// BMS devices array
JKBMS jkBmsDevices[] = {
  // cast to std::string
  JKBMS(mac_addr),
  // Add more devices here if needed
  // JKBMS(BMS_MAC_ADDRESS_2),
  // JKBMS(BMS_MAC_ADDRESS_3)
};

const int bmsDeviceCount = sizeof(jkBmsDevices) / sizeof(jkBmsDevices[0]);

// BLE scanning
unsigned long lastScanTime = 0;

//********************************************
// Setup Function
//********************************************
void setup() {
  Serial.begin(115200);
  lastMillis = millis();
  prefs.begin("JK BMS", false);
  
  
  
  if(prefs.isKey("mac1")) {
      prefs.getString("mac1", mac_addr, sizeof(mac_addr));
  } else {
      strncpy(mac_addr, "c8:47:80:23:4f:95", sizeof(mac_addr));
      mac_addr[sizeof(mac_addr)-1] = '\0'; // Ensure null-termination
  }
  
  // Initialize LVGL and display
  LVGL_CYD::begin(SCREEN_ORIENTATION);

  // Setup LVGL layers
  // Bottom layer: opaque white background
  lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(lv_layer_bottom(), lv_color_white(), LV_PART_MAIN);

  // Create exit button (top-right)
  btn_exit = lv_obj_create(lv_layer_top());
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(btn_exit, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn_exit, 0, LV_PART_MAIN);
  lv_obj_set_size(btn_exit, 40, 40);
  lv_obj_align(btn_exit, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_add_event_cb(btn_exit, [](lv_event_t* e) -> void {
    go_main();
  }, LV_EVENT_CLICKED, NULL);

  // Exit button symbol
  lv_obj_t* lbl_exit_symbol = lv_label_create(btn_exit);
  lv_obj_set_style_text_font(lbl_exit_symbol, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl_exit_symbol, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(lbl_exit_symbol, LV_SYMBOL_CLOSE);
  lv_obj_align(lbl_exit_symbol, LV_ALIGN_TOP_RIGHT, 5, -10);

  // Create back button (top-left)
  btn_back = lv_obj_create(lv_layer_top());
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn_back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
  lv_obj_set_size(btn_back, 40, 40);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);

  // Back button symbol
  lv_obj_t* lbl_back_symbol = lv_label_create(btn_back);
  lv_obj_set_style_text_font(lbl_back_symbol, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_set_style_text_align(lbl_back_symbol, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(lbl_back_symbol, LV_SYMBOL_BACKSPACE);
  lv_obj_align(lbl_back_symbol, LV_ALIGN_TOP_MID, 5, -10);

  // Init navigation callback for back button
  ui_navigation_init();

  // Page header
  lbl_header = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_align(lbl_header, LV_ALIGN_TOP_MID, 5, 3);

  // Launch main screen on startup
  go_main();

  // Initialize BLE
  DEBUG_PRINTLN("Initializing NimBLE Client...");

  // Print configured BMS devices
  for (int i = 0; i < bmsDeviceCount; i++) {
    DEBUG_PRINTF("BMS Device %d: MAC = %s\n", i, jkBmsDevices[i].targetMAC.c_str());
  }

  NimBLEDevice::init("MultiJKBMS-Client");
  NimBLEDevice::setPower(3);

  // Setup BLE scanning
  //pScan = NimBLEDevice::getScan();
  //pScan->setScanCallbacks(&scanCallbacks);
  //pScan->setInterval(BLE_SCAN_INTERVAL);
  //pScan->setWindow(BLE_SCAN_WINDOW);
  //pScan->setActiveScan(true);
}

//********************************************
// Main Loop
//********************************************
void loop() {
  // Handle LVGL tasks
  lv_task_handler();

  // Update monitoring
  calculateUptime();
  monitorFreeHeap();

  // Update BMS display periodically
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    update_bms_display();
    lastDisplayUpdate = millis();
  }

  // BMS Connection management
  int connectedCount = 0;

  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].targetMAC.empty()) continue;

    // Connect to BMS if needed
    if (jkBmsDevices[i].doConnect && !jkBmsDevices[i].connected) {
      DEBUG_PRINTF("Attempting to connect to BMS %d (%s)...\n", i, jkBmsDevices[i].targetMAC.c_str());
      if (jkBmsDevices[i].connectToServer()) {
        DEBUG_PRINTF("%s connected successfully\n", jkBmsDevices[i].targetMAC.c_str());
      } else {
        DEBUG_PRINTF("%s connection failed\n", jkBmsDevices[i].targetMAC.c_str());
      }
      jkBmsDevices[i].doConnect = false;
    }

    // Check for connection timeout
    if (jkBmsDevices[i].connected) {
      connectedCount++;
      if (millis() - jkBmsDevices[i].lastNotifyTime > BMS_CONNECTION_TIMEOUT) {
        DEBUG_PRINTF("%s connection timeout\n", jkBmsDevices[i].targetMAC.c_str());
        NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(jkBmsDevices[i].advDevice->getAddress());
        if (pClient) pClient->disconnect();
      }
    }
  }

  // Start scan if not all devices are connected
  if (connectedCount < bmsDeviceCount && (millis() - lastScanTime >= BLE_SCAN_PERIOD)) {
    DEBUG_PRINTLN("Starting scan...");
    pScan->start(BLE_SCAN_TIME, false, true);
    lastScanTime = millis();
  }

  delay(10);
}