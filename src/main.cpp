#include "board/board.h"
#include "display/display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "lvgl.h"
#include "nvs_flash.h"

static const char *TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "CYD JK BMS Display %s on %s", esp_app_get_description()->version, BOARD_NAME);

    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    if(displayInit() != ESP_OK) {
        ESP_LOGE(TAG, "No display");
        return;
    }
    LvglLock lock;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1014), 0);
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_label_set_text(l, "ESP-IDF + LVGL");
    lv_obj_center(l);
}
