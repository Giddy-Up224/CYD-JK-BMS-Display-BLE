#pragma once

#include <lvgl.h>

// Forward declaration to avoid circular dependency
class JKBMS;

// Global LVGL elements
extern lv_obj_t* soc_gauge;
extern lv_obj_t* soc_gauge_label;
extern lv_obj_t* battery_current_label;
extern lv_obj_t* battery_voltage_and_current_label;
extern lv_obj_t* scr_cell_resistances;
extern lv_obj_t* scr_cell_voltages;
extern lv_obj_t* cell_voltage_table;
extern lv_obj_t* delta_voltages_table;
extern lv_obj_t* wire_res_table;
extern lv_obj_t* res_high_low_avg_table;

// Screen objects
extern lv_obj_t* scr_main;
extern lv_obj_t* scr_more;
extern lv_obj_t* scr_connect_jk_device;
extern lv_obj_t* scr_settings;
extern lv_obj_t* scr_misc_settings;
extern lv_obj_t* btn_back;
extern lv_obj_t* btn_exit;
extern lv_obj_t* lbl_header;
extern lv_obj_t* scr_led;
extern lv_obj_t* slider_led[3];
extern lv_obj_t* sw_led_true;
extern lv_obj_t* scr_backlight;
extern lv_obj_t* slider_bl;
extern lv_obj_t* scr_touch;
extern lv_obj_t* horizontal;
extern lv_obj_t* vertical;

// Global variables for LVGL components
extern float battery_voltage;
extern float battery_current;

// Screen creation and navigation functions
lv_obj_t* new_screen(lv_obj_t* parent);
void ui_navigation_init();
void go_main();
void go_more();
void go_connect_bms();
void go_settings();
void go_misc_settings();
void go_led();
void go_backlight();
void go_touch();
void go_rotate();
void go_cell_voltages();
void go_wire_resistances();

// element creation functions
static lv_obj_t* create_device_list_button(const char* name, const char* mac_address);

// Update functions
void update_bms_display();

// Callbacks
void scr_led_cb(lv_event_t* e);