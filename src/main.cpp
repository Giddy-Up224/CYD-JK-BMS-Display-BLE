#include <Arduino.h>
#include <NimBLEDevice.h>
#include <LVGL_CYD.h>
#include <lvgl.h>

// Project modules
#include "config.h"

//********************************************
// Global Variables
//********************************************

// Screen objects
lv_obj_t* scr_main = nullptr;
lv_obj_t* scr_scan = nullptr;

// BLE scanning
NimBLEScan* pScan;
unsigned long lastScanTime = 0;

//********************************************
// Scanning logic
//********************************************
void scanForDevices() {
  NimBLEDevice::init("test");
  NimBLEDevice::setPower(3);

  // Setup BLE scanning
  pScan = NimBLEDevice::getScan();
  pScan->setInterval(BLE_SCAN_INTERVAL);
  pScan->setWindow(BLE_SCAN_WINDOW);
  pScan->setActiveScan(true);
  pScan->start(BLE_SCAN_TIME, false, true);
}

//********************************************
// Screens
//********************************************
// Creates a new obj to use as base screen
lv_obj_t* new_screen(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(obj, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_row(obj, 10, LV_PART_MAIN);
  return obj;
}

void add_discovered_device(lv_obj_t* btn_parent) {
  lv_obj_t* device_btn = lv_btn_create(btn_parent);

}

void go_main() {
  if (!scr_main) {
    scr_main = new_screen(NULL);
    lv_obj_set_size(scr_main, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t* scan_btn = lv_btn_create(scr_main);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_LEFT, -10, 10);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) -> void {
      scanForDevices();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_btn_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_btn_label, "...");
    lv_obj_align_to(scan_btn_label, scan_btn, LV_ALIGN_CENTER, 0, 0);
  }

  lv_screen_load(scr_main);
}


//********************************************
// Setup Function
//********************************************
void setup() {
  Serial.begin(115200);

  // Initialize LVGL and display
  LVGL_CYD::begin(SCREEN_ORIENTATION);
  
}

//********************************************
// Main Loop
//********************************************
void loop() {
  // Handle LVGL tasks
  lv_task_handler();
  delay(10);
}