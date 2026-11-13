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

// Create prefs object to store settings, saved devices etc.
Preferences prefs;

// Function prototypes
void update_display();
void handle_serial();

//********************************************
// Setup Function
//********************************************
void setup() {
  Serial.begin(115200);
  lastMillis = millis();
  prefs.begin("JK BMS", false);

  // init display and elements
  ui_init();

  // Initialize BLE, load saved devices and start looking for them
  bms_init();
}

//********************************************
// Main Loop
//********************************************
void loop() {
  // Handle LVGL tasks
  lv_task_handler();

  update_display();

  // BMS connection management
  bms_loop();

  handle_serial();

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

//********************************************
// Serial commands (handy for debugging without touching the screen)
//********************************************
static void print_devices() {
  Serial.println("Saved devices:");
  for (int i = 0; i < bmsDeviceCount; i++) {
    const JKBMS &d = jkBmsDevices[i];
    if (!d.inUse()) {
      Serial.printf("  [%d] <empty>\n", i);
      continue;
    }
    Serial.printf("  [%d] %s %s %s proto=%s model=%s hw=%s cells=%d %.2fV\n", i, d.targetMAC.c_str(),
                  d.name.c_str(), d.connected ? "CONNECTED" : "disconnected", jk_protocol_name(d.protocol),
                  d.vendorId.c_str(), d.hardwareVersion.c_str(), d.cell_count, d.Battery_Voltage);
  }
  Serial.printf("Found devices%s:\n", bms_is_scanning() ? " (scanning)" : "");
  for (const FoundDevice &f : bms_get_found_devices()) {
    Serial.printf("  %s %-20s %d dBm\n", f.mac.c_str(), f.name.c_str(), f.rssi);
  }
}

void handle_serial() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (line.length() < 64) line += c;
      continue;
    }
    line.trim();
    if (line.isEmpty()) continue;

    if (line == "scan") {
      bms_start_user_scan();
    } else if (line == "list") {
      print_devices();
    } else if (line == "dump") {
      int n = 0;
      for (int i = 0; i < bmsDeviceCount; i++) {
        if (jkBmsDevices[i].connected) { jkBmsDevices[i].dumpFrames(Serial); n++; }
      }
      if (!n) Serial.println("No connected devices to dump");
    } else if (line.startsWith("add ")) {
      String mac = line.substring(4);
      mac.trim();
      mac.toLowerCase();
      // Use the name/address type from the scan results if we saw it
      std::string name;
      uint8_t type = BLE_ADDR_PUBLIC;
      for (const FoundDevice &f : bms_get_found_devices()) {
        if (f.mac == mac.c_str()) { name = f.name; type = f.addrType; }
      }
      int slot = bms_add_device(mac.c_str(), type, name);
      if (slot >= 0) Serial.printf("Added to slot %d\n", slot);
      else Serial.println("No free slot, forget a device first");
    } else if (line.startsWith("forget ")) {
      String mac = line.substring(7);
      mac.trim();
      mac.toLowerCase();
      Serial.println(bms_forget_device(mac.c_str()) ? "Forgotten" : "Not saved");
    } else {
      Serial.println("Commands: scan | list | dump | add <mac> | forget <mac>");
    }
    line = "";
  }
}
