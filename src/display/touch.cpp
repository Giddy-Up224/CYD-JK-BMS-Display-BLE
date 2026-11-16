#include "touch.h"
#include "board/board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

[[maybe_unused]] static const char *TAG = "Touch";

#if defined(TOUCH_XPT2046)
#include "esp_rom_sys.h"

// Bit-banged SPI, mode 0. A few microseconds per bit is plenty for the XPT2046.
static uint16_t transfer(uint8_t command) {
    for(int i = 7; i >= 0; i--) {
        gpio_set_level(TOUCH_MOSI, (command >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(TOUCH_CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(TOUCH_CLK, 0);
    }
    gpio_set_level(TOUCH_MOSI, 0);
    // 12-bit result in the next 16 clocks (busy bit, 12 bits, 3 zeros)
    uint16_t value = 0;
    for(int i = 0; i < 16; i++) {
        gpio_set_level(TOUCH_CLK, 1);
        esp_rom_delay_us(1);
        value = (value << 1) | gpio_get_level(TOUCH_MISO);
        gpio_set_level(TOUCH_CLK, 0);
        esp_rom_delay_us(1);
    }
    return (value >> 3) & 0x0FFF;
}

esp_err_t touchInit() {
    gpio_config_t out = {};
    out.pin_bit_mask = (1ULL << TOUCH_CLK) | (1ULL << TOUCH_MOSI) | (1ULL << TOUCH_CS);
    out.mode = GPIO_MODE_OUTPUT;
    gpio_config(&out);
    gpio_config_t in = {};
    in.pin_bit_mask = (1ULL << TOUCH_MISO) | (1ULL << TOUCH_IRQ);
    in.mode = GPIO_MODE_INPUT;  // input-only pins: no internal pull-ups
    gpio_config(&in);
    gpio_set_level(TOUCH_CS, 1);
    gpio_set_level(TOUCH_CLK, 0);
    return ESP_OK;
}

static int16_t scale(int raw, int rawMin, int rawMax, int size) {
    int v = (raw - rawMin) * (size - 1) / (rawMax - rawMin);
    return static_cast<int16_t>(v < 0 ? 0 : v >= size ? size - 1 : v);
}

static const int Z_THRESHOLD = 300;

static int pressure() {
    return transfer(0xB1) + 4095 - transfer(0xC1);  // Z1 + (4095 - Z2)
}

// Mean of the samples without the lowest and highest
static int trimmedMean(int *v, int n) {
    int sum = 0, lo = v[0], hi = v[0];
    for(int i = 0; i < n; i++) {
        sum += v[i];
        if(v[i] < lo) lo = v[i];
        if(v[i] > hi) hi = v[i];
    }
    return (sum - lo - hi) / (n - 2);
}

// False if the panel isn't pressed firmly the whole time
bool touchRead(int16_t &x, int16_t &y) {
    // IRQ goes low while the panel is pressed
    if(gpio_get_level(TOUCH_IRQ)) return false;
    gpio_set_level(TOUCH_CS, 0);
    int zBefore = pressure();
    const int samples = 6;
    int rx[samples], ry[samples];
    transfer(0xD1);  // the first conversion after switching is noisy
    for(int i = 0; i < samples; i++) {
        rx[i] = transfer(0xD1);  // X
        ry[i] = transfer(0x91);  // Y
    }
    int zAfter = pressure();
    transfer(0x90);  // power down, keep IRQ on
    gpio_set_level(TOUCH_CS, 1);
    // a finger landing or lifting gives readings pulled towards a corner
    if(zBefore < Z_THRESHOLD || zAfter < Z_THRESHOLD) return false;
    int rawX = trimmedMean(rx, samples);
    int rawY = trimmedMean(ry, samples);
    if(TOUCH_SWAP_XY) {
        int t = rawX;
        rawX = rawY;
        rawY = t;
    }
    x = scale(rawX, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, LCD_H_RES);
    y = scale(rawY, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, LCD_V_RES);
    return true;
}

#endif
