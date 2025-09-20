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

//********************************************
// Global Variables
//********************************************
Preferences user_settings;

// BMS devices array
//123JKBMS jkBmsDevices[] = {
//123  JKBMS(BMS_MAC_ADDRESS_1),
//123  // Add more devices here if needed
//123  // JKBMS(BMS_MAC_ADDRESS_2),
//123  // JKBMS(BMS_MAC_ADDRESS_3)
//123};

JKBMS jkBmsDevices[];

//123const int bmsDeviceCount = sizeof(jkBmsDevices) / sizeof(jkBmsDevices[0]);
int bmsDeviceCount = sizeof(NULL);

// BLE scanning
NimBLEScan* pScan;
unsigned long lastScanTime = 0;
ScanCallbacks scanCallbacks;

//********************************************
// Scanning logic
//********************************************
void scanForJKDevices() {
  NimBLEDevice::init("MultiJKBMS-Client");
  NimBLEDevice::setPower(3);

  // Setup BLE scanning
  pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(&scanCallbacks);
  pScan->setInterval(BLE_SCAN_INTERVAL);
  pScan->setWindow(BLE_SCAN_WINDOW);
  pScan->setActiveScan(true);
  pScan->start(BLE_SCAN_TIME, false, true);
}

void setupJKDevices() {
  // Initialize BLE
  Serial.println("Initializing NimBLE Client...");

  // Print configured BMS devices
  for (int i = 0; i < bmsDeviceCount; i++) {
    DEBUG_PRINTF("BMS Device %d: MAC = %s\n", i, jkBmsDevices[i].targetMAC.c_str());
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
}

//********************************************
// Setup UI Navigation
//********************************************
void setupUINavigation() {
  // Back button callback
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e) -> void {
    ScreenID prev = nav_pop();
    switch (prev) {
      case SCREEN_MAIN:
        go_main();
        DEBUG_PRINTLN("going to scr_main");
        break;
      case SCREEN_MORE:
        go_more();
        DEBUG_PRINTLN("going to scr_more");
        break;
      case SCREEN_SETTINGS:
        go_settings();
        DEBUG_PRINTLN("going to scr_settings");
        break;
      case SCREEN_LED:
        go_led();
        DEBUG_PRINTLN("going to scr_led");
        break;
      case SCREEN_BL:
        go_backlight();
        DEBUG_PRINTLN("going to scr_bl");
        break;
      case SCREEN_TOUCH:
        go_touch();
        DEBUG_PRINTLN("going to scr_touch");
        break;
      case SCREEN_CELL_VOLTAGES:
        go_cell_voltages();
        DEBUG_PRINTLN("going to scr_cell_voltages");
        break;
      case SCREEN_CELL_RESISTANCES:
        go_wire_resistances();
        DEBUG_PRINTLN("going to scr_cell_resistances");
        break;
      default:
        go_main();
        DEBUG_PRINTF("%d not found! Defaulting to scr_main...", prev);
        break;
    }
  }, LV_EVENT_CLICKED, NULL);
}

//********************************************
// Setup Function
//********************************************
void setup() {
  Serial.begin(115200);
  lastMillis = millis();

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

  // Setup navigation callback for back button
  setupUINavigation();

  // Page header
  lbl_header = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_align(lbl_header, LV_ALIGN_TOP_MID, 5, 3);

  // Launch main screen on startup
  go_main();

  
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
  // TODO: change this to update only whenever a new notification is received
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    update_bms_display();
    lastDisplayUpdate = millis();
  }

  

  delay(10);
}