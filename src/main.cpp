#include "ble/jk_ble.h"
#include "bms/bms_data.h"
#include "board/board.h"
#include "console/console.h"
#include "display/display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui/ui.h"

static const char *TAG = "main";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "CYD JK BMS Display %s on %s", esp_app_get_description()->version, BOARD_NAME);

    // NVS: the saved device, and the BLE PHY calibration
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    bmsDataInit();
    bleStart();
#ifdef NO_DISPLAY
    // Test build (env:cyd_nodisplay): BLE without the screen, backlight off
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 0);
    if(false) {
#else
    if(displayInit() == ESP_OK) {
#endif
        uiStart();
    } else {
        ESP_LOGE(TAG, "No display");  // the BLE link and console still run
    }
    consoleStart();
}
