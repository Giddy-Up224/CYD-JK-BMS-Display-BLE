#include <Arduino.h>
#include <NimBLEDevice.h>
#include <LVGL_CYD.h>
#include <lvgl.h>
#include <Preferences.h>

// Project modules
#include "config/config.h"
#include "utils/utils.h"
#include "ui/navigation.h"
#include "bms/jkbms.h"
#include "ui/screens.h"
#include "prefs.h"

//********************************************
// Global Variables
//********************************************
#define BMS_MAC_ADDRESS_1 "c8:47:80:23:4f:95"
#define BMS_MAC_ADDRESS_2 "c8:47:80:3d:6f:b5"

// Create prefs object to store settings etc. 
Preferences prefs;

// get stored mac address, default to "c8:47:80:23:4f:95" if not set
// TODO: change to use results from scanning to populate this list. 
// Save to preferences when selected.
//String mac_address1 = prefs.getString("mac1", "c8:47:80:23:4f:95");
//char mac_addr [18];
  
// BMS devices array
JKBMS jkBmsDevices[] = {
  // cast to std::string
  JKBMS(BMS_MAC_ADDRESS_1),
  JKBMS(BMS_MAC_ADDRESS_2),
  // Add more devices here if needed
  // JKBMS(BMS_MAC_ADDRESS_3)
};

const int bmsDeviceCount = sizeof(jkBmsDevices) / sizeof(jkBmsDevices[0]);

// BLE scanning
unsigned long lastScanTime = 0;

// Function prototypes
void update_display();

//********************************************
// Setup Function
//********************************************
void setup() {
  Serial.begin(115200);
  lastMillis = millis();
  prefs.begin("JK BMS", false);
  
  //if(prefs.isKey("mac1")) {
  //    prefs.getString("mac1", mac_addr, sizeof(mac_addr));
  //} else {
  //    strncpy(mac_addr, "c8:47:80:23:4f:95", sizeof(mac_addr));
  //    mac_addr[sizeof(mac_addr)-1] = '\0'; // Ensure null-termination
  //}
  
  // init display and elements
  ui_init();

  // Initialize BLE
  DEBUG_PRINTLN("Initializing NimBLE Client...");
  // Print configured BMS devices
  for (int i = 0; i < bmsDeviceCount; i++) {
    DEBUG_PRINTF("BMS Device %d: MAC = %s\n", i, jkBmsDevices[i].targetMAC.c_str());
  }

  NimBLEDevice::init("MultiJKBMS-Client");
  NimBLEDevice::setPower(3);

  scanForDevices();
}

//********************************************
// Main Loop
//********************************************
void loop() {
  // Handle LVGL tasks
  lv_task_handler();

  update_display();

  // BMS Connection management
  int connectedCount = 0;

  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].targetMAC.empty()) continue;

    // Connect to BMS if needed
    if (jkBmsDevices[i].doConnect && !jkBmsDevices[i].connected) {
      DEBUG_PRINTF("Attempting to connect to: %d (%s)...\n", i, jkBmsDevices[i].targetMAC.c_str());
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
        NimBLEClient *pClient = NimBLEDevice::getClientByPeerAddress(jkBmsDevices[i].advDevice->getAddress());
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

// Global timer var
static unsigned long lastDisplayUpdate = 0;

void update_display() {
  // Update BMS display periodically
  if (millis() - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    update_bms_display();
    lastDisplayUpdate = millis();
  }
}