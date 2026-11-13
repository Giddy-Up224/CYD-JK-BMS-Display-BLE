#include <LVGL_CYD.h>
#include "screens.h"
#include "navigation.h"
#include "../utils/utils.h"
#include "../config/config.h"
#include "../bms/jkbms.h"

// Global LVGL elements
lv_obj_t *soc_gauge = nullptr;
lv_obj_t *soc_gauge_label = nullptr;
lv_obj_t *battery_current_label = nullptr;
lv_obj_t *battery_voltage_and_current_label = nullptr;
lv_obj_t *cell_voltage_table = nullptr;
lv_obj_t *delta_voltages_table = nullptr;
lv_obj_t *wire_res_table = nullptr;
lv_obj_t *res_high_low_avg_table = nullptr;

// Screen objects
lv_obj_t *scr_main = nullptr;
lv_obj_t *scr_connect_jk_device = nullptr;
lv_obj_t *scr_more = nullptr;
lv_obj_t *scr_not_connected;
lv_obj_t *scr_cell_voltages = nullptr;
lv_obj_t *scr_cell_resistances = nullptr;
lv_obj_t *scr_settings = nullptr;
lv_obj_t *scr_display_settings = nullptr;
lv_obj_t *scr_backlight = nullptr;
lv_obj_t *jk_devices_scroll_container = nullptr;
lv_obj_t *btn_back = nullptr;
lv_obj_t *btn_exit = nullptr;
lv_obj_t *lbl_header = nullptr;
lv_obj_t *slider_bl = nullptr;
lv_obj_t *horizontal = nullptr;
lv_obj_t *vertical = nullptr;

// Global variables for LVGL components
float battery_voltage;
float battery_current;

// Creates a new obj to use as base screen
lv_obj_t *new_screen(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_create(parent);
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

// Device currently shown on the data screens
static int activeSlot = 0;
static JKBMS *connectedBMS = nullptr;
static bool connected = false;
static lv_obj_t *btn_next_device = nullptr;

static int connected_count() {
  int n = 0;
  for (int i = 0; i < bmsDeviceCount; i++) n += jkBmsDevices[i].connected;
  return n;
}

// Name to show for a device: advertised name, model or MAC
static const char *device_display_name(const JKBMS &d) {
  if (!d.name.empty()) return d.name.c_str();
  if (!d.vendorId.empty()) return d.vendorId.c_str();
  return d.targetMAC.c_str();
}

// Keep the active device if it's still connected, otherwise use the first connected one
static void select_active_bms() {
  if (activeSlot < 0 || activeSlot >= bmsDeviceCount || !jkBmsDevices[activeSlot].connected) {
    for (int i = 0; i < bmsDeviceCount; i++) {
      if (jkBmsDevices[i].connected) {
        activeSlot = i;
        break;
      }
    }
  }
  connected = activeSlot >= 0 && activeSlot < bmsDeviceCount && jkBmsDevices[activeSlot].connected;
  connectedBMS = connected ? &jkBmsDevices[activeSlot] : nullptr;
}

// Switch the data screens to the next connected device
static void cycle_active_bms() {
  for (int k = 1; k <= bmsDeviceCount; k++) {
    int i = (activeSlot + k) % bmsDeviceCount;
    if (jkBmsDevices[i].connected) {
      activeSlot = i;
      break;
    }
  }
  update_bms_display();
}

// Make a cell table have one row per cell (+ header), numbering column 0
static void ensure_cell_rows(lv_obj_t *table, int cells, const char *placeholder) {
  uint32_t rows = cells + 1;
  if (lv_table_get_row_count(table) == rows) return;
  lv_table_set_row_count(table, rows);
  for (int i = 1; i <= cells; i++) {
    lv_table_set_cell_value_fmt(table, i, 0, "%d", i);
    lv_table_set_cell_value(table, i, 1, placeholder);
  }
}

static void clear_table_column(lv_obj_t *table, uint32_t col) {
  for (uint32_t r = 1; r < lv_table_get_row_count(table); r++) lv_table_set_cell_value(table, r, col, "-");
}

// Update BMS display with latest data from notify callback
void update_bms_display() {
  select_active_bms();
  int cells = connected ? min(connectedBMS->cell_count, JK_MAX_CELLS) : 0;

  // Main screen header shows which device is displayed
  if (scr_main && lv_screen_active() == scr_main && lbl_header) {
    if (connected) lv_label_set_text(lbl_header, device_display_name(*connectedBMS));
    else if (bms_saved_count() > 0) lv_label_set_text(lbl_header, "Connecting...");
    else lv_label_set_text(lbl_header, "No device saved");
  }
  if (btn_next_device) {
    if (connected_count() > 1) lv_obj_clear_flag(btn_next_device, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(btn_next_device, LV_OBJ_FLAG_HIDDEN);
  }

  // Update SOC gauge
  if (soc_gauge && soc_gauge_label) {
    if (connected && connectedBMS->hasSoc()) {
      lv_arc_set_value(soc_gauge, connectedBMS->Percent_Remain);
      lv_label_set_text_fmt(soc_gauge_label, "%d%%", connectedBMS->Percent_Remain);
    } else {
      // Not connected, or a balancer which has no SOC
      lv_arc_set_value(soc_gauge, 0);
      lv_label_set_text(soc_gauge_label, connected && connectedBMS->hasData() ? "--%" : "0%");
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
    if (connected && cells > 0) {
      float high = -1000.0f, low = 1000.0f;
      int high_idx = -1, low_idx = -1;
      for (int i = 0; i < cells; i++) {
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
      clear_table_column(delta_voltages_table, 1);
      clear_table_column(delta_voltages_table, 2);
    }
  }

  // Update cell voltage table
  if (cell_voltage_table) {
    if (connected) {
      ensure_cell_rows(cell_voltage_table, cells, "0.000");
      for (int i = 0; i < cells; i++) {
        lv_table_set_cell_value_fmt(cell_voltage_table, i + 1, 1, "%.3f", connectedBMS->cellVoltage[i]);
      }
    } else {
      clear_table_column(cell_voltage_table, 1);
    }
  }

  // Update wire resistance high/low/average table
  if (res_high_low_avg_table) {
    if (connected && cells > 0) {
      float high = -1000.0f, low = 1000.0f;
      int high_idx = -1, low_idx = -1;
      float sum_res = 0;
      for (int i = 0; i < cells; i++) {
        float res = connectedBMS->wireResist[i];
        if (res > high) { high = res; high_idx = i; }
        if (res < low)  { low = res;  low_idx = i; }
        sum_res += res;
      }
      float avg_res = sum_res / cells;

      lv_table_set_cell_value_fmt(res_high_low_avg_table, 1, 1, "%.3f", high);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 1, 2, "%d", high_idx + 1);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 2, 1, "%.3f", low);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 2, 2, "%d", low_idx + 1);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 3, 1, "%.3f", high - low);
      lv_table_set_cell_value_fmt(res_high_low_avg_table, 4, 1, "%.3f", avg_res);
    } else {
      clear_table_column(res_high_low_avg_table, 1);
      clear_table_column(res_high_low_avg_table, 2);
    }
  }

  // Update wire resistance table
  if (wire_res_table) {
    if (connected) {
      ensure_cell_rows(wire_res_table, cells, "-");
      for (int i = 0; i < cells; i++) {
        lv_table_set_cell_value_fmt(wire_res_table, i + 1, 1, "%.3f", connectedBMS->wireResist[i]);
      }
    } else {
      clear_table_column(wire_res_table, 1);
    }
  }
}

// Not connected! warning screen
void go_not_connected() {
  if(!scr_not_connected) {
    scr_not_connected = new_screen(NULL);

    lv_obj_t *lbl_not_connected = lv_label_create(scr_not_connected);
    lv_label_set_text(lbl_not_connected, "No BMS is connected!\nPlease connect to a\nBMS to view its data.");
  }
  lv_label_set_text(lbl_header, "No BMS Connected!");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_not_connected);
}

// Backlight brightness screen
void go_backlight() {
  if (!scr_backlight) {
    scr_backlight = new_screen(NULL);

    slider_bl = lv_slider_create(scr_backlight);
    lv_obj_set_width(slider_bl, lv_pct(80));
    lv_slider_set_range(slider_bl, 0, 255);
    lv_obj_add_event_cb(slider_bl, [](lv_event_t *e) -> void {
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
  lv_disp_t *display = lv_disp_get_default();
  lv_display_rotation_t rotation = lv_display_get_rotation(display);
  if (rotation == USB_LEFT) lv_display_set_rotation(display, USB_DOWN);
  else if (rotation == USB_DOWN) lv_display_set_rotation(display, USB_RIGHT);
  else if (rotation == USB_RIGHT) lv_display_set_rotation(display, USB_UP);
  else lv_display_set_rotation(display, USB_LEFT);
}

// Misc settings screen
void go_display_settings() {
  if (!scr_display_settings) {
    scr_display_settings = new_screen(NULL);
    lv_obj_set_size(scr_display_settings, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t *btn_bl = lv_button_create(scr_display_settings);
    lv_obj_t *lbl_bl = lv_label_create(btn_bl);
    lv_label_set_text(lbl_bl, "Backlight brightness");
    lv_obj_add_event_cb(btn_bl, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_DISPLAY_SETTINGS);
      go_backlight();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_rotate = lv_button_create(scr_display_settings);
    lv_obj_t *lbl_rotate = lv_label_create(btn_rotate);
    lv_label_set_text(lbl_rotate, "Rotate display");
    lv_obj_add_event_cb(btn_rotate, [](lv_event_t *e) -> void {
      go_rotate();
    }, LV_EVENT_CLICKED, NULL);
  }

  lv_label_set_text(lbl_header, "Misc Settings");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_display_settings);
}

// Settings screen
void go_settings() {
  if (!scr_settings) {
    scr_settings = new_screen(NULL);
    lv_obj_set_size(scr_settings, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t *btn_misc_settings = lv_button_create(scr_settings);
    lv_obj_t *lbl_display_settings = lv_label_create(btn_misc_settings);
    lv_label_set_text(lbl_display_settings, "Display Settings");
    lv_obj_add_event_cb(btn_misc_settings, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_SETTINGS);
      go_display_settings();
    }, LV_EVENT_CLICKED, NULL);

    // TODO: Implement checkbox state 
    //TODO: set as option for individual BMSes ??
    lv_obj_t *chb_bms_auto_conn_on_boot = lv_checkbox_create(scr_settings);
    lv_checkbox_set_text(chb_bms_auto_conn_on_boot, "Auto conn BMS on boot");
  }

  lv_label_set_text(lbl_header, "Settings");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_settings);
}

// Wire resistances screen
void go_wire_resistances() {
  select_active_bms();
  if(connected) {
    if (!scr_cell_resistances) {
      scr_cell_resistances = lv_obj_create(NULL);
      lv_obj_set_style_bg_opa(scr_cell_resistances, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(scr_cell_resistances, 0, 0);
      lv_obj_set_size(scr_cell_resistances, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

      // Create scrollable container
      lv_obj_t *scroll_container = lv_obj_create(scr_cell_resistances);
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
      lv_table_set_row_count(wire_res_table, 1); // Header, cell rows are added by update_bms_display()

      lv_table_set_column_width(wire_res_table, 0, 80);
      lv_table_set_column_width(wire_res_table, 1, 100);

      lv_table_set_cell_value(wire_res_table, 0, 0, "Cell");
      lv_table_set_cell_value(wire_res_table, 0, 1, "Res.");

      lv_obj_set_style_bg_color(wire_res_table, lv_color_hex(0xE0E0E0), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT));
      lv_obj_set_style_text_font(wire_res_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    }

    lv_label_set_text(lbl_header, "Wire Resistances");
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(scr_cell_resistances);
    update_bms_display();
  }else {
    go_not_connected();
  }
}

// Cell voltages screen
void go_cell_voltages() {
  select_active_bms();
  if(connected) {
    if (!scr_cell_voltages) {
      scr_cell_voltages = lv_obj_create(NULL);
      lv_obj_set_style_bg_opa(scr_cell_voltages, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(scr_cell_voltages, 0, 0);
      lv_obj_set_size(scr_cell_voltages, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

      // Create scrollable container
      lv_obj_t *scroll_container = lv_obj_create(scr_cell_voltages);
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
      lv_table_set_row_count(cell_voltage_table, 1); // Header, cell rows are added by update_bms_display()

      lv_table_set_column_width(cell_voltage_table, 0, 80);
      lv_table_set_column_width(cell_voltage_table, 1, 100);

      lv_table_set_cell_value(cell_voltage_table, 0, 0, "Cell");
      lv_table_set_cell_value(cell_voltage_table, 0, 1, "Voltage (V)");

      lv_obj_set_style_bg_color(cell_voltage_table, lv_color_hex(0xE0E0E0), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT));
      lv_obj_set_style_text_font(cell_voltage_table, &lv_font_montserrat_14, LV_PART_ITEMS);

    }

    lv_label_set_text(lbl_header, "Cell Voltages");
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(scr_cell_voltages);
    update_bms_display();
  } else {
    go_not_connected();
  }
}

//********************************************
// Device list screen
//********************************************

// One row in the device list. Saved devices come first, then the JK devices
// found by the latest scan.
struct DeviceRow {
  std::string mac;
  std::string name;
  uint8_t addrType = 0;
  bool saved = false;
  std::string line1, line2;
  uint32_t color = 0;
  lv_obj_t *btn = nullptr;
  lv_obj_t *lbl1 = nullptr;
  lv_obj_t *lbl2 = nullptr;
};

static std::vector<DeviceRow> device_rows;
static lv_obj_t *lbl_scan_status = nullptr;
static lv_obj_t *btn_scan = nullptr;
static lv_obj_t *lbl_list_empty = nullptr;
static std::string pending_forget_mac;

#define COLOR_ROW_FOUND     0x1E88E5  // Blue: found, tap to save + connect
#define COLOR_ROW_SAVED     0xEF6C00  // Orange: saved, not connected
#define COLOR_ROW_CONNECTED 0x2E7D32  // Green: connected

static void refresh_device_list(bool force);

// Build the list of rows we want to show right now
static std::vector<DeviceRow> build_device_rows() {
  std::vector<DeviceRow> rows;
  std::vector<FoundDevice> found = bms_get_found_devices();

  auto find_found = [&](const std::string &mac) -> const FoundDevice * {
    for (const FoundDevice &f : found) if (f.mac == mac) return &f;
    return nullptr;
  };

  char buf[96];
  for (int i = 0; i < bmsDeviceCount; i++) {
    const JKBMS &d = jkBmsDevices[i];
    if (!d.inUse()) continue;
    DeviceRow r;
    r.mac = d.targetMAC;
    r.name = d.name;
    r.addrType = d.addrType;
    r.saved = true;
    snprintf(buf, sizeof(buf), "%s %s", d.connected ? LV_SYMBOL_OK : LV_SYMBOL_REFRESH, device_display_name(d));
    r.line1 = buf;
    if (d.connected && d.hasData()) {
      snprintf(buf, sizeof(buf), "%s | %s | %d cells", d.vendorId.empty() ? d.targetMAC.c_str() : d.vendorId.c_str(),
               jk_protocol_name(d.protocol), d.cell_count);
    } else if (d.connected) {
      snprintf(buf, sizeof(buf), "%s | Connected, waiting for data", d.targetMAC.c_str());
    } else {
      const FoundDevice *f = find_found(d.targetMAC);
      if (f) snprintf(buf, sizeof(buf), "%s | %d dBm | Connecting...", d.targetMAC.c_str(), f->rssi);
      else snprintf(buf, sizeof(buf), "%s | Saved, not in range", d.targetMAC.c_str());
    }
    r.line2 = buf;
    r.color = d.connected ? COLOR_ROW_CONNECTED : COLOR_ROW_SAVED;
    rows.push_back(r);
  }

  for (const FoundDevice &f : found) {
    if (bms_find_slot(f.mac) >= 0) continue;
    DeviceRow r;
    r.mac = f.mac;
    r.name = f.name;
    r.addrType = f.addrType;
    snprintf(buf, sizeof(buf), "%s %s", LV_SYMBOL_BLUETOOTH, f.name.empty() ? "(no name)" : f.name.c_str());
    r.line1 = buf;
    snprintf(buf, sizeof(buf), "%s | %d dBm", f.mac.c_str(), f.rssi);
    r.line2 = buf;
    r.color = COLOR_ROW_FOUND;
    rows.push_back(r);
  }
  return rows;
}

static void close_msgbox_cb(lv_event_t *e) {
  lv_msgbox_close(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
}

static void show_message(const char *title, const char *text) {
  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, title);
  lv_msgbox_add_text(mbox, text);
  lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "OK");
  lv_obj_add_event_cb(ok, close_msgbox_cb, LV_EVENT_CLICKED, mbox);
}

// Show device details and offer to forget a saved device
static void show_forget_dialog(const DeviceRow &row) {
  int slot = bms_find_slot(row.mac);
  if (slot < 0) return;
  pending_forget_mac = row.mac;
  const JKBMS &d = jkBmsDevices[slot];

  lv_obj_t *mbox = lv_msgbox_create(NULL);
  lv_msgbox_add_title(mbox, device_display_name(d));
  if (d.connected && !d.vendorId.empty()) {
    lv_msgbox_add_text_fmt(mbox, "%s\nModel: %s\nHW %s  SW %s\nProtocol: %s", d.targetMAC.c_str(), d.vendorId.c_str(),
                           d.hardwareVersion.c_str(), d.softwareVersion.c_str(), jk_protocol_name(d.protocol));
  } else {
    lv_msgbox_add_text_fmt(mbox, "%s\n%s", d.targetMAC.c_str(), d.connected ? "Connected" : "Not connected");
  }

  lv_obj_t *btn_forget = lv_msgbox_add_footer_button(mbox, "Forget");
  lv_obj_set_style_bg_color(btn_forget, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
  lv_obj_add_event_cb(btn_forget, [](lv_event_t *e) -> void {
    bms_forget_device(pending_forget_mac);
    pending_forget_mac.clear();
    lv_msgbox_close(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
    refresh_device_list(true);
  }, LV_EVENT_CLICKED, mbox);

  lv_obj_t *btn_close = lv_msgbox_add_footer_button(mbox, "Close");
  lv_obj_add_event_cb(btn_close, close_msgbox_cb, LV_EVENT_CLICKED, mbox);
}

static void device_row_clicked(lv_event_t *e) {
  lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
  int idx = lv_obj_get_index(btn);
  if (idx < 0 || idx >= (int)device_rows.size()) return;
  const DeviceRow row = device_rows[idx];  // Copy, refresh may rebuild the vector
  DEBUG_PRINTF("Device row clicked: %s (%s)\n", row.name.c_str(), row.mac.c_str());

  if (row.saved) {
    show_forget_dialog(row);
    return;
  }
  if (bms_saved_count() >= bmsDeviceCount) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Up to %d devices can be saved.\nTap a saved device to forget it.", bmsDeviceCount);
    show_message("Device limit reached", msg);
    return;
  }
  if (bms_add_device(row.mac, row.addrType, row.name) >= 0) {
    // The (blocking) connect happens in loop(), so draw the new state first
    refresh_device_list(true);
    lv_refr_now(NULL);
  }
}

static void set_label_if_changed(lv_obj_t *lbl, const std::string &text) {
  if (strcmp(lv_label_get_text(lbl), text.c_str()) != 0) lv_label_set_text(lbl, text.c_str());
}

static void create_device_row(lv_obj_t *parent, DeviceRow &row) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(btn, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_row(btn, 2, LV_PART_MAIN);
  lv_obj_set_layout(btn, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);

  row.lbl1 = lv_label_create(btn);
  lv_obj_set_style_text_font(row.lbl1, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_label_set_long_mode(row.lbl1, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(row.lbl1, lv_pct(100));
  lv_label_set_text(row.lbl1, "");

  row.lbl2 = lv_label_create(btn);
  lv_obj_set_style_text_font(row.lbl2, &lv_font_montserrat_12, LV_PART_MAIN);
  lv_label_set_long_mode(row.lbl2, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(row.lbl2, lv_pct(100));
  lv_label_set_text(row.lbl2, "");

  lv_obj_add_event_cb(btn, device_row_clicked, LV_EVENT_CLICKED, NULL);
  row.btn = btn;
}

// Sync the on-screen list with the saved/found devices. Rows are updated in
// place; the list is only rebuilt when devices are added or removed so
// scrolling isn't interrupted while a scan updates signal strengths.
static void refresh_device_list(bool force) {
  if (!jk_devices_scroll_container) return;
  std::vector<DeviceRow> rows = build_device_rows();
  bool scanning = bms_is_scanning();

  bool same = !force && rows.size() == device_rows.size();
  for (size_t i = 0; same && i < rows.size(); i++) {
    same = rows[i].mac == device_rows[i].mac && rows[i].saved == device_rows[i].saved;
  }

  if (!same) {
    int32_t scroll_y = lv_obj_get_scroll_y(jk_devices_scroll_container);
    lv_obj_clean(jk_devices_scroll_container);
    lbl_list_empty = nullptr;
    for (DeviceRow &r : rows) create_device_row(jk_devices_scroll_container, r);
    if (rows.empty()) lbl_list_empty = lv_label_create(jk_devices_scroll_container);
    lv_obj_update_layout(jk_devices_scroll_container);
    lv_obj_scroll_to_y(jk_devices_scroll_container, scroll_y, LV_ANIM_OFF);
  } else {
    // Keep the existing objects
    for (size_t i = 0; i < rows.size(); i++) {
      rows[i].btn = device_rows[i].btn;
      rows[i].lbl1 = device_rows[i].lbl1;
      rows[i].lbl2 = device_rows[i].lbl2;
    }
  }
  device_rows = rows;

  for (DeviceRow &r : device_rows) {
    set_label_if_changed(r.lbl1, r.line1);
    set_label_if_changed(r.lbl2, r.line2);
    lv_obj_set_style_bg_color(r.btn, lv_color_hex(r.color), LV_PART_MAIN);
  }

  if (lbl_list_empty) {
    set_label_if_changed(lbl_list_empty, scanning ? "Searching for JK devices..."
                                                  : "No JK devices found.\nMake sure they are powered\nand not connected to the app.");
  }

  // Scan status
  if (lbl_scan_status) {
    int found = 0;
    for (const DeviceRow &r : device_rows) found += !r.saved;
    if (scanning) lv_label_set_text(lbl_scan_status, "Scanning...");
    else lv_label_set_text_fmt(lbl_scan_status, "%d new, %d/%d saved", found, bms_saved_count(), bmsDeviceCount);
  }
  if (btn_scan) {
    if (scanning) lv_obj_add_state(btn_scan, LV_STATE_DISABLED);
    else lv_obj_clear_state(btn_scan, LV_STATE_DISABLED);
  }
}

// screen for selecting and connecting to JK BMS / balancer devices
void go_connect_bms() {
  if (!scr_connect_jk_device) {
    scr_connect_jk_device = new_screen(NULL);
    lv_obj_set_size(scr_connect_jk_device, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_pad_top(scr_connect_jk_device, 30, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(scr_connect_jk_device, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr_connect_jk_device, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr_connect_jk_device, 6, LV_PART_MAIN);
    lv_obj_set_flex_align(scr_connect_jk_device, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Scrollable device list, takes all the space above the bottom bar
    jk_devices_scroll_container = lv_obj_create(scr_connect_jk_device);
    lv_obj_set_width(jk_devices_scroll_container, lv_pct(100));
    lv_obj_set_flex_grow(jk_devices_scroll_container, 1);
    lv_obj_set_style_pad_all(jk_devices_scroll_container, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(jk_devices_scroll_container, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(jk_devices_scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(jk_devices_scroll_container, 0, 0);
    lv_obj_set_layout(jk_devices_scroll_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(jk_devices_scroll_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(jk_devices_scroll_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(jk_devices_scroll_container, LV_SCROLLBAR_MODE_AUTO);

    // Bottom bar: status + scan button
    lv_obj_t *bar = lv_obj_create(scr_connect_jk_device);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lbl_scan_status = lv_label_create(bar);
    lv_obj_set_style_text_font(lbl_scan_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(lbl_scan_status, "");

    btn_scan = lv_button_create(bar);
    lv_obj_set_size(btn_scan, 100, 36);
    lv_obj_add_event_cb(btn_scan, [](lv_event_t *e) -> void {
      DEBUG_PRINTLN("Scan button pressed!");
      bms_start_user_scan();
      refresh_device_list(true);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_btn_label = lv_label_create(btn_scan);
    lv_label_set_text(scan_btn_label, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(scan_btn_label);

    // Keep the list in sync with scan results / connection state
    lv_timer_create([](lv_timer_t *t) -> void {
      if (lv_screen_active() == scr_connect_jk_device) refresh_device_list(false);
    }, 500, NULL);
  }

  lv_label_set_text(lbl_header, "Devices");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_connect_jk_device);

  // Look for devices whenever the list is opened
  if (!bms_is_scanning()) bms_start_user_scan();
  refresh_device_list(true);
}

void go_more() {
  if (!scr_more) {
    scr_more = new_screen(NULL);
    lv_obj_set_size(scr_more, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    // Add connect BMS button
    lv_obj_t *connect_bms_btn = lv_btn_create(scr_more);
    lv_obj_set_size(connect_bms_btn, 120, 40);
    lv_obj_align(connect_bms_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(connect_bms_btn, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_connect_bms();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connect_bms_btn_label = lv_label_create(connect_bms_btn);
    lv_label_set_text(connect_bms_btn_label, "Devices");
    lv_obj_center(connect_bms_btn_label);

    // Add cell voltages button
    lv_obj_t *cell_voltages_btn = lv_btn_create(scr_more);
    lv_obj_set_size(cell_voltages_btn, 120, 40);
    lv_obj_align(cell_voltages_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(cell_voltages_btn, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_cell_voltages();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cell_voltages_btn_label = lv_label_create(cell_voltages_btn);
    lv_label_set_text(cell_voltages_btn_label, "Cell Voltages");
    lv_obj_center(cell_voltages_btn_label);

    // Add wire resistances button
    lv_obj_t *wire_res_button = lv_btn_create(scr_more);
    lv_obj_set_size(wire_res_button, 120, 40);
    lv_obj_align(wire_res_button, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(wire_res_button, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_wire_resistances();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wire_res_button_label = lv_label_create(wire_res_button);
    lv_label_set_text(wire_res_button_label, "Wire Res.");
    lv_obj_center(wire_res_button_label);

    // Settings button
    lv_obj_t *go_to_settings_btn = lv_btn_create(scr_more);
    lv_obj_set_size(go_to_settings_btn, 120, 40);
    lv_obj_align(go_to_settings_btn, LV_ALIGN_BOTTOM_MID, 0, 20);
    lv_obj_add_event_cb(go_to_settings_btn, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_MORE);
      go_settings();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *go_to_settings_btn_label = lv_label_create(go_to_settings_btn);
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

    lv_obj_t *go_to_more_btn = lv_btn_create(scr_main);
    // Floating so the flex layout doesn't put it in the column (the header shows the device name)
    lv_obj_add_flag(go_to_more_btn, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(go_to_more_btn, LV_ALIGN_TOP_LEFT, -10, 10);
    lv_obj_add_event_cb(go_to_more_btn, [](lv_event_t *e) -> void {
      nav_push(ScreenID::SCREEN_MAIN);
      go_more();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *go_to_settings_btn_label = lv_label_create(go_to_more_btn);
    lv_label_set_text(go_to_settings_btn_label, "More");
    lv_obj_align_to(go_to_settings_btn_label, go_to_more_btn, LV_ALIGN_CENTER, 0, 0);

    // Switch between connected devices (only shown when more than one is connected)
    btn_next_device = lv_btn_create(scr_main);
    lv_obj_add_flag(btn_next_device, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(btn_next_device, LV_ALIGN_TOP_RIGHT, 10, 10);
    lv_obj_add_event_cb(btn_next_device, [](lv_event_t *e) -> void {
      cycle_active_bms();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_next = lv_label_create(btn_next_device);
    lv_label_set_text(lbl_next, LV_SYMBOL_RIGHT);
    lv_obj_center(lbl_next);
    lv_obj_add_flag(btn_next_device, LV_OBJ_FLAG_HIDDEN);

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
  }

  lv_label_set_text(lbl_header, "");
  lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_main);
  // Fill in current BMS data and the device name header
  update_bms_display();
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
      DEBUG_PRINTLN("going to scr_display_settings");
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
    case SCREEN_NOT_CONNECTED:
      go_not_connected();
      DEBUG_PRINTLN("going to scr_not_connected");
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
  lv_obj_add_event_cb(btn_back, [](lv_event_t *e) -> void {
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
  lv_obj_add_event_cb(btn_exit, [](lv_event_t *e) -> void {
    go_main();
  }, LV_EVENT_CLICKED, NULL);

  // Exit button symbol
  lv_obj_t *lbl_exit_symbol = lv_label_create(btn_exit);
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
  lv_obj_t *lbl_back_symbol = lv_label_create(btn_back);
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