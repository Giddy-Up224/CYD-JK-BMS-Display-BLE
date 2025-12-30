#include <LVGL_CYD.h>
#include "screens.h"
#include "navigation.h"
#include "../utils/utils.h"
#include "../config/config.h"
#include "../bms/jkbms.h"

// Global LVGL elements
lv_obj_t* soc_gauge = nullptr;
lv_obj_t* soc_gauge_label = nullptr;
lv_obj_t* battery_current_label = nullptr;
lv_obj_t* battery_voltage_and_current_label = nullptr;
lv_obj_t* scr_cell_resistances = nullptr;
lv_obj_t* scr_cell_voltages = nullptr;
lv_obj_t* cell_voltage_table = nullptr;
lv_obj_t* delta_voltages_table = nullptr;
lv_obj_t* wire_res_table = nullptr;
lv_obj_t* res_high_low_avg_table = nullptr;

// Screen objects
lv_obj_t* scr_main = nullptr;
lv_obj_t* scr_connect_jk_device = nullptr;
lv_obj_t* jk_device_list = nullptr;
lv_obj_t* jk_devices_scroll_container = nullptr;
lv_obj_t* scr_more = nullptr;
lv_obj_t* scr_settings = nullptr;
lv_obj_t* scr_misc_settings = nullptr;
lv_obj_t* btn_back = nullptr;
lv_obj_t* btn_exit = nullptr;
lv_obj_t* lbl_header = nullptr;
lv_obj_t* scr_led = nullptr;
lv_obj_t* slider_led[3];
lv_obj_t* sw_led_true = nullptr;
lv_obj_t* scr_backlight = nullptr;
lv_obj_t* slider_bl = nullptr;
lv_obj_t* scr_touch = nullptr;
lv_obj_t* horizontal = nullptr;
lv_obj_t* vertical = nullptr;

// Global variables for LVGL components
float battery_voltage;
float battery_current;

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

// Update BMS display with latest data from notify callback
void update_bms_display() {
  // TODO: change this to not use a single connected bool for all BMSes
  bool connected = false;
  JKBMS* connectedBMS = nullptr;
  
  // Find first connected BMS
  for (int i = 0; i < bmsDeviceCount; i++) {
    if (jkBmsDevices[i].connected) {
      connected = true;
      connectedBMS = &jkBmsDevices[i];
      break;
    }
  }
  
  // Update SOC gauge
  if (soc_gauge && soc_gauge_label) {
    if (connected) {
      lv_arc_set_value(soc_gauge, connectedBMS->Percent_Remain);
      lv_label_set_text_fmt(soc_gauge_label, "%d%%", connectedBMS->Percent_Remain);
    } else {
      lv_arc_set_value(soc_gauge, 0);
      lv_label_set_text(soc_gauge_label, "0%");
    }
  }

  // Update voltage and current on main screen
  if (battery_voltage_and_current_label) {
    if (connected) {
      battery_voltage = connectedBMS->Battery_Voltage;
      battery_current = connectedBMS->Charge_Current;
      lv_label_set_text_fmt(battery_voltage_and_current_label, "V: %.2f   A: %.3f", battery_voltage, battery_current);
    } else {
      lv_label_set_text(battery_voltage_and_current_label, "V: --.--   A: ---.---");
    }
  }

  // Update delta voltage table
  if (delta_voltages_table) {
    if (connected) {
      float high = -1000.0f, low = 1000.0f;
      int high_idx = -1, low_idx = -1;
      for (int i = 0; i < connectedBMS->cell_count; i++) {
        float v = connectedBMS->cellVoltage[i];
        if (v > high) { high = v; high_idx = i; }
        if (v < low)  { low = v;  low_idx = i; }
      }

      lv_table_set_cell_value_fmt(delta_voltages_table, 1, 1, "%.3f", high);
      lv_table_set_cell_value_fmt(delta_voltages_table, 1, 2, "%d", high_idx + 1);
      lv_table_set_cell_value_fmt(delta_voltages_table, 2, 1, "%.3f", low);
      lv_table_set_cell_value_fmt(delta_voltages_table, 2, 2, "%d", low_idx + 1);
      lv_table_set_cell_value_fmt(delta_voltages_table, 3, 1, "%.3f", connectedBMS->Delta_Cell_Voltage);
      lv_table_set_cell_value_fmt(delta_voltages_table, 4, 1, "%.3f", connectedBMS->Average_Cell_Voltage);
    } else {
      lv_table_set_cell_value(delta_voltages_table, 1, 1, "-");
      lv_table_set_cell_value(delta_voltages_table, 2, 1, "-");
      lv_table_set_cell_value(delta_voltages_table, 3, 1, "-");
      lv_table_set_cell_value(delta_voltages_table, 4, 1, "-");
    }
  }
  
  // Update cell voltage table
  if (cell_voltage_table) {
    if (connected) {
      for (int i = 0; i < 16; i++) {
        lv_table_set_cell_value_fmt(cell_voltage_table, i + 1, 1, "%.3f", connectedBMS->cellVoltage[i]);
      }
    } else {
      for (int i = 0; i < 16; i++) {
        lv_table_set_cell_value(cell_voltage_table, i + 1, 1, "0.000");
      }
    }
  }

  // Update wire resistance high/low/average table
  if (res_high_low_avg_table) {
    if (connected) {
      float high = -1000.0f, low = 1000.0f;
      int high_idx = -1, low_idx = -1;
      float sum_res = 0;
      float avg_res = 0;
      for (int i = 0; i < connectedBMS->cell_count; i++) {
        float res = connectedBMS->wireResist[i];
        if (res > high) { high = res; high_idx = i; }
        if (res < low)  { low = res;  low_idx = i; }
        sum_res += res;
      }
      avg_res = sum_res / connectedBMS->cell_count;

      lv_table_set_cell_value_fmt(res_high_low_avg_table, 1, 1, "%.3f", high);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 1, 2, "%d", high_idx + 1);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 2, 1, "%.3f", low);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 2, 2, "%d", low_idx + 1);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 3, 1, "%.3f", high - low);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 4, 1, "%.3f", avg_res);
    } else {
      lv_table_set_cell_value(res_high_low_avg_table, 1, 1, "-");
      lv_table_set_cell_value(res_high_low_avg_table, 2, 1, "-");
      lv_table_set_cell_value(res_high_low_avg_table, 3, 1, "-");
      lv_table_set_cell_value(res_high_low_avg_table, 4, 1, "-");
    }
  }
  
  // Update wire resistance table
  if (wire_res_table) {
    if (connected) {
      for (int i = 0; i < 16; i++) {
        lv_table_set_cell_value_fmt(wire_res_table, i + 1, 1, "%.3f", connectedBMS->wireResist[i]);
      }
    } else {
      for (int i = 0; i < 16; i++) {
        lv_table_set_cell_value(wire_res_table, i + 1, 1, "-");
      }
    }
  }
}

// Backlight brightness screen
void go_backlight() {
  if (!scr_backlight) {
    scr_backlight = new_screen(NULL);

    slider_bl = lv_slider_create(scr_backlight);
    lv_obj_set_width(slider_bl, lv_pct(80));
    lv_slider_set_range(slider_bl, 0, 255);
    lv_obj_add_event_cb(slider_bl, [](lv_event_t* e) -> void {
      LVGL_CYD::backlight(lv_slider_get_value(slider_bl));
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // Turn on backlight initially
    lv_slider_set_value(slider_bl, 255, LV_ANIM_OFF);

    // Set backlight to initial slider value
    LVGL_CYD::backlight(lv_slider_get_value(slider_bl));
  }

  lv_label_set_text(lbl_header, "Backlight brightness");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_backlight);
}

// Rotate display 90 degrees clockwise
void go_rotate() {
  lv_disp_t* display = lv_disp_get_default();
  lv_display_rotation_t rotation = lv_display_get_rotation(display);
  if (rotation == USB_LEFT) lv_display_set_rotation(display, USB_DOWN);
  else if (rotation == USB_DOWN) lv_display_set_rotation(display, USB_RIGHT);
  else if (rotation == USB_RIGHT) lv_display_set_rotation(display, USB_UP);
  else lv_display_set_rotation(display, USB_LEFT);
}

// Misc settings screen
void go_display_settings() {
  if (!scr_misc_settings) {
    scr_misc_settings = new_screen(NULL);
    lv_obj_set_size(scr_misc_settings, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t* btn_bl = lv_button_create(scr_misc_settings);
    lv_obj_t* lbl_bl = lv_label_create(btn_bl);
    lv_label_set_text(lbl_bl, "Backlight brightness");
    lv_obj_add_event_cb(btn_bl, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_DISPLAY_SETTINGS);
      go_backlight();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_rotate = lv_button_create(scr_misc_settings);
    lv_obj_t* lbl_rotate = lv_label_create(btn_rotate);
    lv_label_set_text(lbl_rotate, "Rotate display");
    lv_obj_add_event_cb(btn_rotate, [](lv_event_t* e) -> void {
      go_rotate();
    }, LV_EVENT_CLICKED, NULL);
  }

  lv_label_set_text(lbl_header, "Misc Settings");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_misc_settings);
}

// Settings screen
void go_settings() {
  if (!scr_settings) {
    scr_settings = new_screen(NULL);
    lv_obj_set_size(scr_settings, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t* btn_misc_settings = lv_button_create(scr_settings);
    lv_obj_t* lbl_display_settings = lv_label_create(btn_misc_settings);
    lv_label_set_text(lbl_display_settings, "Display Settings");
    lv_obj_add_event_cb(btn_misc_settings, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_SETTINGS);
      go_display_settings();
    }, LV_EVENT_CLICKED, NULL);

    // TODO: Implement checkbox state 
    //TODO: set as option for individual BMSes ??
    lv_obj_t* chb_bms_auto_conn_on_boot = lv_checkbox_create(scr_settings);
    lv_checkbox_set_text(chb_bms_auto_conn_on_boot, "Auto conn BMS on boot");
  }

  lv_label_set_text(lbl_header, "Settings");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_settings);
}

// Wire resistances screen
void go_wire_resistances() {
  if (!scr_cell_resistances) {
    scr_cell_resistances = lv_obj_create(NULL);
    lv_obj_set_style_bg_opa(scr_cell_resistances, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr_cell_resistances, 0, 0);
    lv_obj_set_size(scr_cell_resistances, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    // Create scrollable container
    lv_obj_t* scroll_container = lv_obj_create(scr_cell_resistances);
    lv_obj_set_size(scroll_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(scroll_container, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_container, 0, 0);
    lv_obj_set_layout(scroll_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll_container, LV_FLEX_FLOW_COLUMN);

    // Create table for high/low resistances
    res_high_low_avg_table = lv_table_create(scroll_container);
    lv_obj_set_size(res_high_low_avg_table, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    int num_rows = 5; // Header + 4 rows
    int num_cols = 3; // Description, Resistance, Cell#

    lv_table_set_column_count(res_high_low_avg_table, num_cols);
    lv_table_set_row_count(res_high_low_avg_table, num_rows);

    lv_table_set_column_width(res_high_low_avg_table, 0, 100);
    lv_table_set_column_width(res_high_low_avg_table, 1, 100);
    lv_table_set_column_width(res_high_low_avg_table, 2, 100);

    lv_table_set_cell_value(res_high_low_avg_table, 0, 1, "Res.");
    lv_table_set_cell_value(res_high_low_avg_table, 0, 2, "Cell#");

    lv_table_set_cell_value(res_high_low_avg_table, 1, 0, "High_Res.");
    lv_table_set_cell_value(res_high_low_avg_table, 2, 0, "Low_Res.");
    lv_table_set_cell_value(res_high_low_avg_table, 3, 0, "Delta_Res.");
    lv_table_set_cell_value(res_high_low_avg_table, 4, 0, "Avg_Res.");

    lv_obj_set_style_bg_color(res_high_low_avg_table, lv_color_hex(0xE0E0E0), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT));
    lv_obj_set_style_text_font(res_high_low_avg_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    for (int r = 1; r < num_rows; r++) {
      lv_table_set_cell_value(res_high_low_avg_table, r, 1, "-");
      lv_table_set_cell_value(res_high_low_avg_table, r, 2, "-");
    }

    // Create table for wire resistances
    wire_res_table = lv_table_create(scroll_container);
    lv_obj_set_size(wire_res_table, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_table_set_column_count(wire_res_table, 2);
    lv_table_set_row_count(wire_res_table, 17); // Header + 16 cells

    lv_table_set_column_width(wire_res_table, 0, 80);
    lv_table_set_column_width(wire_res_table, 1, 100);

    lv_table_set_cell_value(wire_res_table, 0, 0, "Cell");
    lv_table_set_cell_value(wire_res_table, 0, 1, "Res.");

    lv_obj_set_style_bg_color(wire_res_table, lv_color_hex(0xE0E0E0), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT));
    lv_obj_set_style_text_font(wire_res_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    for (int i = 1; i <= 16; i++) {
      lv_table_set_cell_value_fmt(wire_res_table, i, 0, "%d", i);
      lv_table_set_cell_value(wire_res_table, i, 1, "");
    }
  }

  lv_label_set_text(lbl_header, "Wire Resistances");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_cell_resistances);
}

// Cell voltages screen
void go_cell_voltages() {
  if (!scr_cell_voltages) {
    scr_cell_voltages = lv_obj_create(NULL);
    lv_obj_set_style_bg_opa(scr_cell_voltages, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr_cell_voltages, 0, 0);
    lv_obj_set_size(scr_cell_voltages, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    // Create scrollable container
    lv_obj_t* scroll_container = lv_obj_create(scr_cell_voltages);
    lv_obj_set_size(scroll_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(scroll_container, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_container, 0, 0);
    lv_obj_set_layout(scroll_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll_container, LV_FLEX_FLOW_COLUMN);

    // Create table for high/low voltages
    delta_voltages_table = lv_table_create(scroll_container);
    lv_obj_set_size(delta_voltages_table, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    int num_rows = 5; // Header + 4 rows
    int num_cols = 3; // Description, Voltage, Cell#

    lv_table_set_column_count(delta_voltages_table, num_cols);
    lv_table_set_row_count(delta_voltages_table, num_rows);

    lv_table_set_column_width(delta_voltages_table, 0, 100);
    lv_table_set_column_width(delta_voltages_table, 1, 100);
    lv_table_set_column_width(delta_voltages_table, 2, 100);

    lv_table_set_cell_value(delta_voltages_table, 0, 1, "Voltage");
    lv_table_set_cell_value(delta_voltages_table, 0, 2, "Cell#");

    lv_table_set_cell_value(delta_voltages_table, 1, 0, "High_V");
    lv_table_set_cell_value(delta_voltages_table, 2, 0, "Low_V");
    lv_table_set_cell_value(delta_voltages_table, 3, 0, "Delta_V");
    lv_table_set_cell_value(delta_voltages_table, 4, 0, "Avg_V");

    lv_obj_set_style_bg_color(delta_voltages_table, lv_color_hex(0xE0E0E0), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT));
    lv_obj_set_style_text_font(delta_voltages_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    for (int r = 1; r < num_rows; r++) {
      lv_table_set_cell_value(delta_voltages_table, r, 1, "-");
      lv_table_set_cell_value(delta_voltages_table, r, 2, "-");
    }

    // Create table for cell voltages
    cell_voltage_table = lv_table_create(scroll_container);
    lv_obj_set_size(cell_voltage_table, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_table_set_column_count(cell_voltage_table, 2);
    lv_table_set_row_count(cell_voltage_table, 17); // Header + 16 cells

    lv_table_set_column_width(cell_voltage_table, 0, 80);
    lv_table_set_column_width(cell_voltage_table, 1, 100);

    lv_table_set_cell_value(cell_voltage_table, 0, 0, "Cell");
    lv_table_set_cell_value(cell_voltage_table, 0, 1, "Voltage (V)");

    lv_obj_set_style_bg_color(cell_voltage_table, lv_color_hex(0xE0E0E0), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT));
    lv_obj_set_style_text_font(cell_voltage_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    for (int i = 1; i <= 16; i++) {
      lv_table_set_cell_value_fmt(cell_voltage_table, i, 0, "%d", i);
      lv_table_set_cell_value(cell_voltage_table, i, 1, "0.000");
    }
  }

  lv_label_set_text(lbl_header, "Cell Voltages");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_cell_voltages);
}

void save_mac_addr(const char* mac_addr) {
  // TODO: add prefs logic here to save mac address
}

void connect_selected_device(const char* mac) {
  DEBUG_PRINTF("Connecting to device with MAC: %s\n", mac);
  // TODO: Add code to connect to the device using the provided MAC address
  // TODO: read from prefs on startup if any mac addresses are saved to prefs
}

// Adds a button to the device list for the given device info
// Returns the button object created
lv_obj_t* create_device_list_button(const char* name, const char* mac_address)
{
  if (jk_devices_scroll_container) {
    DEBUG_PRINTLN("Adding button...");

    lv_obj_t* btn = lv_btn_create(jk_devices_scroll_container);
    //lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(btn, 4, 0);

    // set button to flex row so labels are side by side
    lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    
    lv_obj_t * name_lbl = lv_label_create(btn);
    lv_label_set_text(name_lbl, name);
    //lv_obj_set_grid_cell(name_lbl, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    
    //lv_obj_t * rssi_lbl = lv_label_create(btn);
    //lv_label_set_text(rssi_lbl, rssi);
    //lv_obj_set_grid_cell(rssi_lbl, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    
    lv_obj_t * mac_lbl = lv_label_create(btn);
    lv_label_set_text(mac_lbl, mac_address);
    //lv_obj_set_grid_cell(mac_lbl, LV_GRID_ALIGN_START, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);

    lv_obj_set_style_pad_column(btn, 10, 0);

    // allocate a copy of the MAC address on the heap
    char* mac_copy = strdup(mac_address);
    
    //lv_obj_add_style(btn, &style_btn, 0);
    //lv_obj_add_style(btn, &style_button_pr, LV_STATE_PRESSED);
    //lv_obj_add_style(btn, &style_button_chk, LV_STATE_CHECKED);
    //lv_obj_add_style(btn, &style_button_dis, LV_STATE_DISABLED);

    lv_obj_add_event_cb(btn, [](lv_event_t* e) -> void {
      // add code to connect to the selected device here
      const char* mac = static_cast<const char*>(lv_event_get_user_data(e));
      if(mac) {
        DEBUG_PRINTF("Button clicked for device with MAC: %s\n", mac);
        //connect_selected_device(mac);
      } else {
        DEBUG_PRINTLN("Button clicked but MAC address is NULL!");
      }
    }, LV_EVENT_CLICKED, mac_copy); // pass the MAC address copy as user data
    DEBUG_PRINTLN("Added device button to list!");
    return btn;
  } else {
    DEBUG_PRINTLN("no list or container! (Or something like that...)");
  }
  return nullptr;
}

// a test functiuon to add static buttons to the list
// for testing the UI without needing to scan for devices
// TODO: convert this to a real function that scans for devices
// and adds them to the list
void add_button_to_list() {
  if (jk_devices_scroll_container) {
    DEBUG_PRINTLN("Adding button...");
    create_device_list_button("JK BMS", "AA:BB:CC:DD:EE:FF");
    lv_obj_scroll_to_y(jk_devices_scroll_container, lv_obj_get_height(jk_devices_scroll_container), LV_ANIM_ON);
    lv_obj_update_layout(jk_devices_scroll_container);
  } else {
    DEBUG_PRINTLN("no list or container! (Or something like that...)");
  }
  DEBUG_PRINTLN("Added device button to list!");
}

// screen for selecting and connecting to JK BMS devices
void go_connect_bms() {
  if(!scr_connect_jk_device) {
    scr_connect_jk_device = new_screen(NULL);
    lv_obj_set_size(scr_connect_jk_device, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    // Add Scan button
    lv_obj_t* scan_btn = lv_btn_create(scr_connect_jk_device);
    lv_obj_set_size(scan_btn, 120, 40);
    lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) -> void {
      //scan_for_jk_devices();
      DEBUG_PRINTLN("Scan button pressed!");
      scanForDevices();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_btn_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_btn_label, "Scan");
    lv_obj_center(scan_btn_label);

    // Create scrollable container for device list
    jk_devices_scroll_container = lv_obj_create(scr_connect_jk_device);
    lv_obj_set_size(jk_devices_scroll_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(jk_devices_scroll_container, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(jk_devices_scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(jk_devices_scroll_container, 0, 0);
    lv_obj_set_layout(jk_devices_scroll_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(jk_devices_scroll_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_grow(jk_devices_scroll_container, 1);
    lv_obj_set_scrollbar_mode(jk_devices_scroll_container, LV_SCROLLBAR_MODE_AUTO);
  }

  lv_label_set_text(lbl_header, "Choose Device");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_connect_jk_device);
}

void go_more() {
  if (!scr_more) {
    scr_more = new_screen(NULL);
    lv_obj_set_size(scr_more, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    // Add connect BMS button
    lv_obj_t* connect_bms_btn = lv_btn_create(scr_more);
    lv_obj_set_size(connect_bms_btn, 120, 40);
    lv_obj_align(connect_bms_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(connect_bms_btn, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_connect_bms();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* connect_bms_btn_label = lv_label_create(connect_bms_btn);
    lv_label_set_text(connect_bms_btn_label, "Scan devices");
    lv_obj_center(connect_bms_btn_label);

    // Add cell voltages button
    lv_obj_t* cell_voltages_btn = lv_btn_create(scr_more);
    lv_obj_set_size(cell_voltages_btn, 120, 40);
    lv_obj_align(cell_voltages_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(cell_voltages_btn, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_cell_voltages();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* cell_voltages_btn_label = lv_label_create(cell_voltages_btn);
    lv_label_set_text(cell_voltages_btn_label, "Cell Voltages");
    lv_obj_center(cell_voltages_btn_label);

    // Add wire resistances button
    lv_obj_t* wire_res_button = lv_btn_create(scr_more);
    lv_obj_set_size(wire_res_button, 120, 40);
    lv_obj_align(wire_res_button, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(wire_res_button, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_wire_resistances();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* wire_res_button_label = lv_label_create(wire_res_button);
    lv_label_set_text(wire_res_button_label, "Wire Res.");
    lv_obj_center(wire_res_button_label);

    // Settings button
    lv_obj_t* go_to_settings_btn = lv_btn_create(scr_more);
    lv_obj_set_size(go_to_settings_btn, 120, 40);
    lv_obj_align(go_to_settings_btn, LV_ALIGN_BOTTOM_MID, 0, 20);
    lv_obj_add_event_cb(go_to_settings_btn, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_settings();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* go_to_settings_btn_label = lv_label_create(go_to_settings_btn);
    lv_label_set_text(go_to_settings_btn_label, LV_SYMBOL_SETTINGS);
    lv_obj_align_to(go_to_settings_btn_label, go_to_settings_btn, LV_ALIGN_CENTER, 0, 0);
  }

  lv_label_set_text(lbl_header, "");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_more);
}

// Home screen
void go_main() {
  if (!scr_main) {
    scr_main = new_screen(NULL);
    lv_obj_set_size(scr_main, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t* go_to_more_btn = lv_btn_create(scr_main);
    lv_obj_align(go_to_more_btn, LV_ALIGN_TOP_LEFT, -10, 10);
    lv_obj_add_event_cb(go_to_more_btn, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_MAIN);
      go_more();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* go_to_settings_btn_label = lv_label_create(go_to_more_btn);
    lv_label_set_text(go_to_settings_btn_label, "More");
    lv_obj_align_to(go_to_settings_btn_label, go_to_more_btn, LV_ALIGN_CENTER, 0, 0);

    soc_gauge = lv_arc_create(scr_main);
    lv_arc_set_range(soc_gauge, 0, 100);
    lv_obj_set_size(soc_gauge, 150, 150);
    lv_arc_set_rotation(soc_gauge, 135);
    lv_arc_set_bg_angles(soc_gauge, 0, 270);

    // Make arc read-only
    lv_obj_clear_flag(soc_gauge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(soc_gauge, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(soc_gauge, 0, LV_PART_KNOB);

    lv_obj_align(soc_gauge, LV_ALIGN_CENTER, 0, 0);

    // Create label for percentage text
    soc_gauge_label = lv_label_create(soc_gauge);
    lv_obj_set_style_text_font(soc_gauge_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(soc_gauge_label, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(soc_gauge_label, "0%");
    lv_obj_center(soc_gauge_label);

    // Display battery voltage and current
    battery_voltage_and_current_label = lv_label_create(scr_main);
    lv_obj_set_style_text_font(battery_voltage_and_current_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(battery_voltage_and_current_label, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(battery_voltage_and_current_label, "V: --.--");
    lv_obj_set_flex_align(battery_voltage_and_current_label, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);

    // Initialize display with current BMS data
    update_bms_display();
  }

  lv_label_set_text(lbl_header, "");
  lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_main);
}

// Handle back navigation based on previous screen
void handle_back_navigation() {
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
    case SCREEN_CONNECT_JK_DEVICE:
      go_connect_bms();
      DEBUG_PRINTLN("going to scr_connect_bms");
      break;
    case SCREEN_SETTINGS:
      go_settings();
      DEBUG_PRINTLN("going to scr_settings");
      break;
    case SCREEN_DISPLAY_SETTINGS:
      go_display_settings();
      DEBUG_PRINTLN("going to scr_misc_settings");
      break;
    case SCREEN_BL:
      go_backlight();
      DEBUG_PRINTLN("going to scr_bl");
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
  }

// Initialize UI Navigation
void ui_navigation_init() {
  // Back button callback
  lv_obj_add_event_cb(btn_back, [](lv_event_t* e) -> void {
    handle_back_navigation();
  }, LV_EVENT_CLICKED, NULL);
}

void setup_lv_layers() {
  // Setup LVGL layers
  // Bottom layer: opaque white background
  lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(lv_layer_bottom(), lv_color_white(), LV_PART_MAIN);
}

void create_exit_btn() {
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
}

void create_back_btn() {
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
}

void ui_init() {
  DEBUG_PRINTLN("Initializing UI...");
  // Initialize LVGL and display
  LVGL_CYD::begin(SCREEN_ORIENTATION);

  setup_lv_layers();

  create_exit_btn();

  create_back_btn();

  // Init navigation callback for back button
  ui_navigation_init();

  // Page header
  lbl_header = lv_label_create(lv_layer_top());
  lv_obj_set_style_text_font(lbl_header, &lv_font_montserrat_18, LV_PART_MAIN);
  lv_obj_align(lbl_header, LV_ALIGN_TOP_MID, 5, 3);

  // Launch main screen on startup
  go_main();
}