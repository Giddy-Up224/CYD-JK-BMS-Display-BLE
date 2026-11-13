#pragma once
// The board's touch controller: XPT2046 resistive touch (CYD)

#include <stdint.h>
#include "esp_err.h"

esp_err_t touchInit();

// True while touched; x/y in screen pixels (landscape, 0..LCD_H_RES-1, 0..LCD_V_RES-1)
bool touchRead(int16_t &x, int16_t &y);
