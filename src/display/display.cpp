#include "display.h"
#include "board/board.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include "touch.h"
#include <stdio.h>

static const char *TAG = "Display";

// Lines per draw buffer; two buffers so LVGL draws while the other is sent
static const int BUFFER_LINES = 40;

static esp_lcd_panel_handle_t panel_ = nullptr;

// Backlight: off until the first frame is on the panel. Before that the
// ILI9341 shows whatever is in its RAM, which looks white.
static volatile bool lastQueued_ = false;   // the last area of a frame is being sent
static volatile bool frameShown_ = false;   // a whole frame has reached the panel
static bool backlightOn_ = false;
static SemaphoreHandle_t lvglMutex_ = nullptr;

void lvglLock() {
    xSemaphoreTakeRecursive(lvglMutex_, portMAX_DELAY);
}

void lvglUnlock() {
    xSemaphoreGiveRecursive(lvglMutex_);
}

static uint32_t tickMs() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// SPI transfer done: LVGL may reuse the buffer
static bool flushDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *ctx) {
    if(lastQueued_) frameShown_ = true;
    lv_display_flush_ready(static_cast<lv_display_t *>(ctx));
    return false;
}

// Screenshot in progress: every area of the next frame is printed
static volatile bool shotActive_ = false;

static void printArea(const lv_area_t *area, const uint8_t *pixels) {
    int w = lv_area_get_width(area);
    printf("AREA %d %d %d %d\n", (int)area->x1, (int)area->y1, (int)area->x2, (int)area->y2);
    unsigned char out[LCD_H_RES * 2 * 4 / 3 + 8];
    for(int y = 0; y < lv_area_get_height(area); y++) {
        size_t n;
        mbedtls_base64_encode(out, sizeof(out), &n, pixels + y * w * 2, w * 2);
        out[n] = 0;
        // row number first: a line lost on the way can't shift the rows after it
        printf("R%d %s\n", (int)area->y1 + y, out);
        // a frame takes ~18 s at 115200 baud: let the idle task run, or the
        // task watchdog prints a backtrace in the middle of the rows
        if(y % 8 == 7) vTaskDelay(1);
    }
}

// The frame is printed twice: now and then a row loses characters on the
// serial link, and tools/shots.py takes each row from a copy that arrived whole
static const int SHOT_PASSES = 2;

void displayScreenshot(const char *name) {
    for(int pass = 0; pass < SHOT_PASSES; pass++) {
        {
            LvglLock lock;
            // other tasks' log lines would land between the pixel rows
            esp_log_level_set("*", ESP_LOG_NONE);
            printf("SHOT %s %d %d\n", name, LCD_H_RES, LCD_V_RES);
            shotActive_ = true;
            lv_obj_invalidate(lv_screen_active());
            lv_obj_invalidate(lv_layer_top());
        }
        // the LVGL task prints the frame on its next refresh
        for(int i = 0; i < 2000 && shotActive_; i++) vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
    if(!backlightOn_ && lv_display_flush_is_last(disp)) lastQueued_ = true;
    if(shotActive_) {
        printArea(area, pixels);
        if(lv_display_flush_is_last(disp)) {
            printf("END\n");
            shotActive_ = false;
            esp_log_level_set("*", ESP_LOG_INFO);
        }
    }
    // the ILI9341 takes RGB565 big-endian
    lv_draw_sw_rgb565_swap(pixels, lv_area_get_size(area));
    esp_lcd_panel_draw_bitmap(panel_, area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels);
}

// A press from the console (displayTap), read instead of the panel
static volatile int64_t tapFromUs_ = 0, tapUntilUs_ = 0;
static volatile int16_t tapX1_, tapY1_, tapX2_, tapY2_;

void displaySwipe(int x1, int y1, int x2, int y2, int ms) {
    tapX1_ = static_cast<int16_t>(x1);
    tapY1_ = static_cast<int16_t>(y1);
    tapX2_ = static_cast<int16_t>(x2);
    tapY2_ = static_cast<int16_t>(y2);
    tapFromUs_ = esp_timer_get_time();
    tapUntilUs_ = tapFromUs_ + (ms > 0 ? ms : 1) * 1000LL;
}

void displayTap(int x, int y, int ms) {
    displaySwipe(x, y, x, y, ms);
}

static void readTouch(lv_indev_t *, lv_indev_data_t *data) {
    int16_t x, y;
    int64_t now = esp_timer_get_time();
    if(now < tapUntilUs_) {
        // where along the swipe we are; a tap stays put
        int64_t t = now - tapFromUs_, len = tapUntilUs_ - tapFromUs_;
        data->point.x = static_cast<int32_t>(tapX1_ + (tapX2_ - tapX1_) * t / len);
        data->point.y = static_cast<int32_t>(tapY1_ + (tapY2_ - tapY1_) * t / len);
        data->state = LV_INDEV_STATE_PRESSED;
    } else if(touchRead(x, y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void lvglTask(void *) {
    for(;;) {
        lvglLock();
        uint32_t wait = lv_timer_handler();
        lvglUnlock();
        if(frameShown_ && !backlightOn_) {
            backlightOn_ = true;
            gpio_set_level(LCD_BL, 1);
        }
        if(wait > 30) wait = 30;
        if(wait < 2) wait = 2;
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

esp_err_t displayInit() {
    lvglMutex_ = xSemaphoreCreateRecursiveMutex();

    // Backlight off first; the LVGL task turns it on once a frame is showing
    gpio_set_level(LCD_BL, 0);
    gpio_config_t bl = {};
    bl.pin_bit_mask = 1ULL << LCD_BL;
    bl.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bl);
    gpio_set_level(LCD_BL, 0);

    spi_bus_config_t bus = {};
    bus.sclk_io_num = LCD_SCK;
    bus.mosi_io_num = LCD_MOSI;
    bus.miso_io_num = LCD_MISO;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = LCD_H_RES * BUFFER_LINES * sizeof(uint16_t);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "LCD SPI bus");

    lv_init();
    lv_tick_set_cb(tickMs);
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t ioConfig = {};
    ioConfig.cs_gpio_num = LCD_CS;
    ioConfig.dc_gpio_num = LCD_DC;
    ioConfig.spi_mode = 0;
    ioConfig.pclk_hz = LCD_PCLK_HZ;
    ioConfig.trans_queue_depth = 10;
    ioConfig.lcd_cmd_bits = 8;
    ioConfig.lcd_param_bits = 8;
    ioConfig.on_color_trans_done = flushDone;
    ioConfig.user_ctx = disp;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_HOST), &ioConfig, &io), TAG, "LCD IO");

    esp_lcd_panel_dev_config_t panelConfig = {};
    panelConfig.reset_gpio_num = LCD_RST;
    panelConfig.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panelConfig.bits_per_pixel = 16;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panelConfig, &panel_), TAG, "ILI9341");
    esp_lcd_panel_reset(panel_);
    esp_lcd_panel_init(panel_);
    esp_lcd_panel_invert_color(panel_, LCD_INVERT);
    // landscape
    esp_lcd_panel_swap_xy(panel_, true);
    esp_lcd_panel_mirror(panel_, LCD_MIRROR_X, LCD_MIRROR_Y);
    esp_lcd_panel_disp_on_off(panel_, true);

    size_t bufSize = LCD_H_RES * BUFFER_LINES * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(bufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(bufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if(!buf1 || !buf2) {
        ESP_LOGE(TAG, "No memory for the draw buffers");
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(disp, buf1, buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush);

    if(touchInit() == ESP_OK) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, readTouch);
    } else {
        ESP_LOGW(TAG, "No touch controller");
    }

    // LVGL's initial screen is white, and its first frame may be drawn
    // before the UI (splash) loads
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

    xTaskCreatePinnedToCore(lvglTask, "lvgl", 8192, nullptr, 2, nullptr, 1);
    ESP_LOGI(TAG, "Display ready");
    return ESP_OK;
}
