#include "screens.h"
#include "../navigation.h"
#include "../utils/utils.h"
#include "../config.h"
#include "../jkbms.h"
#include <LVGL_CYD.h>

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

void update_bms_display() {
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

// LED screen callback
void scr_led_cb(lv_event_t* e) {
  LVGL_CYD::led(
    lv_slider_get_value(slider_led[0]),
    lv_slider_get_value(slider_led[1]),
    lv_slider_get_value(slider_led[2]),
    lv_obj_has_state(sw_led_true, LV_STATE_CHECKED)
  );
}

void go_led() {
  if (!scr_led) {
    scr_led = new_screen(NULL);

    // Switch between true colors and max brightness
    lv_obj_t* sw_container = lv_obj_create(scr_led);
    lv_obj_set_style_bg_opa(sw_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(sw_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(sw_container, 0, 0);
    lv_obj_set_height(sw_container, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(sw_container, 0, 0);
    lv_obj_set_flex_flow(sw_container, LV_FLEX_FLOW_ROW);
    lv_obj_t* lbl_max = lv_label_create(sw_container);
    lv_label_set_text(lbl_max, "max");
    sw_led_true = lv_switch_create(sw_container);
    lv_obj_add_state(sw_led_true, LV_STATE_CHECKED);
    lv_obj_set_size(sw_led_true, 40, 20);
    lv_obj_add_event_cb(sw_led_true, scr_led_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t* lbl_true = lv_label_create(sw_container);
    lv_label_set_text(lbl_true, "true");
    lv_obj_set_style_pad_column(sw_container, 5, 0);

    // Colors for the 3 sliders
    lv_color_t color[3] {
      lv_color_make(255, 0, 0),
      lv_color_make(0, 200, 0),  // Full-on green looks too yellow on display
      lv_color_make(0, 0, 255)
    };

    // Set up the sliders
    for (int n = 0; n < 3; n++) {
      slider_led[n] = lv_slider_create(scr_led);
      lv_obj_set_width(slider_led[n], lv_pct(80));
      lv_obj_set_style_margin_top(slider_led[n], 25, LV_PART_MAIN);
      lv_obj_set_style_bg_color(slider_led[n], color[n], LV_PART_MAIN);
      lv_obj_set_style_bg_color(slider_led[n], color[n], LV_PART_INDICATOR);
      lv_obj_set_style_bg_color(slider_led[n], lv_color_lighten(color[n], LV_OPA_30), LV_PART_KNOB);
      lv_slider_set_range(slider_led[n], 0, 255);
      lv_obj_add_event_cb(slider_led[n], scr_led_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
  }

  lv_label_set_text(lbl_header, "RGB LED color");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_led);
}

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

void go_touch() {
  if (!scr_touch) {
    scr_touch = new_screen(NULL);
    lv_obj_set_layout(scr_touch, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_top(scr_touch, 0, LV_PART_MAIN);
    lv_obj_add_flag(scr_touch, LV_OBJ_FLAG_CLICKABLE);

    horizontal = lv_obj_create(scr_touch);
    lv_obj_set_style_border_width(horizontal, 0, 0);
    lv_obj_set_style_radius(horizontal, 0, LV_PART_MAIN);
    lv_obj_set_size(horizontal, lv_pct(100), 3);
    lv_obj_set_style_bg_color(horizontal, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(horizontal, LV_OBJ_FLAG_CLICKABLE);

    vertical = lv_obj_create(scr_touch);
    lv_obj_set_style_border_width(vertical, 0, 0);
    lv_obj_set_style_radius(vertical, 0, LV_PART_MAIN);
    lv_obj_set_size(vertical, 3, lv_pct(100));
    lv_obj_set_style_bg_color(vertical, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(vertical, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(scr_touch, [](lv_event_t* e) -> void {
      if (lv_event_get_code(e) == LV_EVENT_PRESSED || lv_event_get_code(e) == LV_EVENT_PRESSING) {
        lv_point_t point;
        lv_indev_get_point(lv_indev_get_act(), &point);
        lv_obj_set_y(horizontal, point.y);
        lv_obj_set_x(vertical, point.x);
        lv_obj_clear_flag(horizontal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(vertical, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
      } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        lv_obj_add_flag(horizontal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(vertical, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
      }
    }, LV_EVENT_ALL, NULL);
  }

  lv_obj_add_flag(horizontal, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(vertical, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_header, "Touch indicator");
  lv_screen_load(scr_touch);
}

void go_rotate() {
  lv_disp_t* display = lv_disp_get_default();
  lv_display_rotation_t rotation = lv_display_get_rotation(display);
  if (rotation == USB_LEFT) lv_display_set_rotation(display, USB_DOWN);
  else if (rotation == USB_DOWN) lv_display_set_rotation(display, USB_RIGHT);
  else if (rotation == USB_RIGHT) lv_display_set_rotation(display, USB_UP);
  else lv_display_set_rotation(display, USB_LEFT);
}

void go_settings() {
  if (!scr_settings) {
    scr_settings = new_screen(NULL);
    lv_obj_set_size(scr_settings, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    lv_obj_t* btn_led = lv_button_create(scr_settings);
    lv_obj_t* lbl_led = lv_label_create(btn_led);
    lv_label_set_text(lbl_led, "RGB LED color");
    lv_obj_add_event_cb(btn_led, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_SETTINGS);
      go_led();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_bl = lv_button_create(scr_settings);
    lv_obj_t* lbl_bl = lv_label_create(btn_bl);
    lv_label_set_text(lbl_bl, "Backlight brightness");
    lv_obj_add_event_cb(btn_bl, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_SETTINGS);
      go_backlight();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_touch = lv_button_create(scr_settings);
    lv_obj_t* lbl_touch = lv_label_create(btn_touch);
    lv_label_set_text(lbl_touch, "Touch indicator");
    lv_obj_add_event_cb(btn_touch, [](lv_event_t* e) -> void {
      nav_push(ScreenID::SCREEN_SETTINGS);
      go_touch();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_rotate = lv_button_create(scr_settings);
    lv_obj_t* lbl_rotate = lv_label_create(btn_rotate);
    lv_label_set_text(lbl_rotate, "Rotate display");
    lv_obj_add_event_cb(btn_rotate, [](lv_event_t* e) -> void {
      go_rotate();
    }, LV_EVENT_CLICKED, NULL);
  }

  lv_label_set_text(lbl_header, "Settings");
  lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(btn_exit, LV_OBJ_FLAG_HIDDEN);
  lv_screen_load(scr_settings);
}

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

void scan_for_jk_devices() {

}

void populate_device_list() {
  scan_for_jk_devices();
}

void go_connect_bms() {
  if(!scr_connect_jk_device) {
    scr_connect_jk_device = new_screen(NULL);
    lv_obj_set_size(scr_connect_jk_device, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));

    // Add Scan button
    lv_obj_t* scan_btn = lv_btn_create(scr_connect_jk_device);
    lv_obj_set_size(scan_btn, 120, 40);
    lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(scan_btn, [](lv_event_t* e) -> void {
      populate_device_list();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_btn_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_btn_label, LV_STR_SYMBOL_BLUETOOTH + " Scan");
    lv_obj_center(scan_btn_label);

    // Create scrollable container for device list
    lv_obj_t* scroll_container = lv_obj_create(scr_connect_jk_device);
    lv_obj_set_size(scroll_container, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(scroll_container, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_container, 0, 0);
    lv_obj_set_layout(scroll_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(scroll_container, LV_FLEX_FLOW_COLUMN);
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
    lv_label_set_text(connect_bms_btn_label, "Connect BMS");
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
    lv_obj_align(go_to_settings_btn, LV_ALIGN_TOP_LEFT, -10, 10);
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
    lv_label_set_text(go_to_settings_btn_label, "...");
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