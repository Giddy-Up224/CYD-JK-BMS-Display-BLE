#include <Arduino.h>
#include <NimBLEDevice.h>
#include <LVGL_CYD.h>
#include <lvgl.h>

// Project modules
#include "config.h"

// Enable or disable debugging output
#define DEBUG_ENABLED true

// Debugging macro
#if DEBUG_ENABLED
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#define DEBUG_PRINTF(...)
#endif

//********************************************
// Global Variables
//********************************************

// Screen objects
lv_obj_t* scr_main = nullptr;
lv_obj_t* place_holder_btn = nullptr;

// BLE scanning
NimBLEScan* pScan;
bool isScanning = false;

// Maybe add this vector to store the scan data??
/*
struct ScannedDevice {
  std::string macAddress;
  std::string deviceName;
  int rssi;
  bool isConnectable;
  const NimBLEAdvertisedDevice* advDevice; // store pointer to use for connection
};
std::vector<ScannedDevice> scannedDevices;
*/

//********************************************
// Scanning logic
//********************************************
// I want to make this functional once I get the types figured out
// create a button for each scanned device
//lv_obj_t* new_scan_button(lv_obj_t* parent, const char* label) {
//  lv_obj_t* list_item = lv_btn_create(parent);
//  lv_obj_set_layout(list_item, LV_FLEX_FLOW_COLUMN);
//  lv_obj_add_event_cb(list_item, [](lv_event_t* e) -> void {
//      // add function call here
//    }, LV_EVENT_CLICKED, NULL);
//
//  // label
//  lv_obj_t* list_item_lbl = lv_label_create(list_item);
//  lv_label_set_text_fmt(list_item, "%s", label);
//  lv_obj_align_to(list_item_lbl, list_item, LV_ALIGN_CENTER, 0, 0);
//  
//  return list_item;
//}

class ScanCallbacks : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice* advertisedDevice) override {
        Serial.printf("Discovered Advertised Device: %s \n", advertisedDevice->toString().c_str());
    }

    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        Serial.printf("Advertised Device Result: %s \n", advertisedDevice->toString().c_str());
        const char* name = advertisedDevice->getName().c_str();
        uint8_t rssi = advertisedDevice->getRSSI();
        //uint8_t mac_addr[] {};
        const char* mac_addr = advertisedDevice->getAddress().toString().c_str();
        std::string p_mac_addr = advertisedDevice->getAddress().toString().c_str();
        Serial.printf("Name: %s RSSI: %d MAC: %s\n", name, rssi, p_mac_addr);

        lv_obj_t* place_holder_btn = lv_btn_create(scr_main);
        //lv_obj_align(place_holder_btn, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_add_event_cb(place_holder_btn, [](lv_event_t* e) -> void {
          // add callback here
        }, LV_EVENT_CLICKED, NULL);
      
        lv_obj_t* place_holder_btn_label = lv_label_create(place_holder_btn);
        lv_label_set_text_fmt(place_holder_btn_label, "%s  rssi: %d", name, rssi);
        lv_obj_align_to(place_holder_btn_label, place_holder_btn, LV_ALIGN_CENTER, 0, 0);

    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.printf("Scan Ended; reason = %d\n", reason);
        isScanning = false;
        //pScan->setActiveScan(true);
        //pScan->start(BLE_SCAN_TIME);
    }
} scanCallbacks;

void scanForDevices() {
  if(!isScanning) {
    // Setup BLE scanning
    isScanning = true;
    Serial.println("Started scanning...");
    pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCallbacks);
    pScan->setActiveScan(true);
    pScan->setInterval(BLE_SCAN_INTERVAL);
    pScan->setWindow(BLE_SCAN_WINDOW);
    pScan->start(BLE_SCAN_TIME);
  }else{
    Serial.println("Already scanning!");
  }
}



//********************************************
// Screens
//********************************************
// for some reason when I use this to create a screen 
// everything goes weird when adding buttons etc.
// Creates a new obj to use as base screen
lv_obj_t* new_screen(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(obj, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_row(obj, 10, LV_PART_MAIN);
  return obj;
}


void go_main() {
  if (!scr_main) {
    scr_main = lv_obj_create(NULL);
    lv_obj_set_size(scr_main, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_add_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(scr_main, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scr_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr_main, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* scan_btn = lv_btn_create(scr_main);
    //lv_obj_align(scan_btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) -> void {
      scanForDevices();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_btn_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_btn_label, "Scan");
    lv_obj_align_to(scan_btn_label, scan_btn, LV_ALIGN_CENTER, 0, 0);

    // add scroll container for device list
    /*lv_obj_t* scroll_container = lv_obj_create(scr_main);
    lv_obj_align_to(scroll_container, scr_main, LV_ALIGN_CENTER, 10, 10);
    lv_obj_set_size(scroll_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(scroll_container, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_container, 0, 0);
    lv_obj_set_layout(scroll_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll_container, LV_FLEX_FLOW_COLUMN);
    */
  }
  lv_screen_load(scr_main);
}

void setup() {
  Serial.begin(115200);

  // Initialize LVGL and display
  LVGL_CYD::begin(SCREEN_ORIENTATION);
  //Setup BLE
  NimBLEDevice::init("BMS Reader");
  delay(200);
  go_main();
  delay(200);
  //scanForDevices();
}

void loop() {
  // Handle LVGL tasks
  lv_task_handler();
}