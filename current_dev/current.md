  5. List Refresh Function

  Add this function to update the displayed device list:

  // Add to src/ui/screens.cpp
  void refresh_device_list() {
      if (!device_list) return;

      // Clear existing list items
      lv_obj_clean(device_list);

      if (scannedDevices.empty()) {
          lv_list_add_text(device_list, "No devices found. Press 'Scan'");
          return;
      }

      // Add header
      lv_list_add_text(device_list, "Found Devices:");

      // Add each device as a button
      for (size_t i = 0; i < scannedDevices.size(); i++) {
          const ScannedDevice& device = scannedDevices[i];

          // Create button text with device info
          char btn_text[100];
          snprintf(btn_text, sizeof(btn_text), "%s\nMAC: %s | RSSI: %d",
                   device.deviceName.c_str(),
                   device.macAddress.c_str(),
                   device.rssi);

          lv_obj_t* btn = lv_list_add_btn(device_list, LV_SYMBOL_BLUETOOTH, btn_text);

          // Store device index in button user data
          lv_obj_set_user_data(btn, (void*)i);

          // Add click handler for connection
          lv_obj_add_event_cb(btn, device_connect_cb, LV_EVENT_CLICKED, NULL);

          // Color code based on connection status
          bool is_connected = false;
          for (int j = 0; j < bmsDeviceCount; j++) {
              if (jkBmsDevices[j].targetMAC == device.macAddress && jkBmsDevices[j].connected) {
                  is_connected = true;
                  break;
              }
          }

          if (is_connected) {
              lv_obj_set_style_bg_color(btn, lv_color_hex(0x4CAF50), LV_PART_MAIN);
          }
      }
  }

  6. Device Connection Handler

  Add this callback to handle device selection:

  // Add to src/ui/screens.cpp
  void device_connect_cb(lv_event_t* e) {
      lv_obj_t* btn = lv_event_get_target(e);
      size_t device_index = (size_t)lv_obj_get_user_data(btn);

      if (device_index >= scannedDevices.size()) return;

      const ScannedDevice& selected = scannedDevices[device_index];

      DEBUG_PRINTF("Selected device: %s (%s)\n",
                   selected.deviceName.c_str(),
                   selected.macAddress.c_str());

      // Find available BMS slot or use first one
      int slot = -1;
      for (int i = 0; i < bmsDeviceCount; i++) {
          if (!jkBmsDevices[i].connected) {
              slot = i;
              break;
          }
      }

      if (slot == -1) {
          // All slots occupied, try slot 0
          slot = 0;
          // Disconnect existing if needed
          if (jkBmsDevices[slot].connected) {
              NimBLEClient* pClient = NimBLEDevice::getClientByPeerAddress(
                  NimBLEAddress(jkBmsDevices[slot].targetMAC));
              if (pClient) {
                  pClient->disconnect();
              }
          }
      }

      // Setup connection
      jkBmsDevices[slot].targetMAC = selected.macAddress;
      jkBmsDevices[slot].advDevice = selected.advDevice;
      jkBmsDevices[slot].doConnect = true;

      // Show connection status
      lv_obj_t* mbox = lv_msgbox_create(NULL, "Connecting",
                                        ("Connecting to:\n" + selected.deviceName +
                                         "\n" + selected.macAddress).c_str(),
                                        NULL, false);
      lv_obj_center(mbox);

      // Auto close after 2 seconds
      lv_timer_t* timer = lv_timer_create([](lv_timer_t* t) {
          lv_msgbox_close((lv_obj_t*)t->user_data);
          lv_timer_del(t);
      }, 2000, mbox);
  }

  7. Add Function Declarations

  Add these to your screens.h:

  // In src/ui/screens.h
  void go_select_devices();
  void refresh_device_list();
  void device_connect_cb(lv_event_t* e);

  8. Add Navigation Button

  To access the device selection screen, add a button in your main or more screen:

  // In go_main() or go_more() function
  lv_obj_t* select_device_btn = lv_btn_create(scr_main);  // or scr_more
  lv_obj_set_size(select_device_btn, 120, 40);
  lv_obj_align(select_device_btn, LV_ALIGN_CENTER, 0, 60);  // Adjust position

  lv_obj_t* select_device_label = lv_label_create(select_device_btn);
  lv_label_set_text(select_device_label, "Select Device");
  lv_obj_center(select_device_label);

  lv_obj_add_event_cb(select_device_btn, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_MAIN);  // or appropriate screen ID
      go_select_devices();
  }, LV_EVENT_CLICKED, NULL);

  9. Add Screen ID

  Add to your navigation enum in navigation.h:

  // In src/navigation.h
  enum ScreenID {
      // ... existing screens ...
      SCREEN_SELECT_DEVICES,
      // ...
  };

  Key Features:

  1. Storage: ScannedDevice struct stores MAC address, name, RSSI, and device reference
  2. Scanning: Modified callback stores ALL discovered devices
  3. Display: List shows device name, MAC, and RSSI
  4. Selection: Click any device to attempt connection
  5. Connection: Uses stored MAC address and device reference to connect
  6. Status: Connected devices show in green
  7. Management: Can scan for new devices or refresh the display

  The MAC address is stored in ScannedDevice.macAddress and used in device_connect_cb() to initiate connections. The
  advDevice pointer is preserved for the actual connection process.