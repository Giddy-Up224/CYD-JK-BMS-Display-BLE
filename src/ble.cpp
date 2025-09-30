#include "ble.h"
#include "jkbms.h"

bool isScanning = false;
NimBLEScan* pScan;

void scan_for_devices() {
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