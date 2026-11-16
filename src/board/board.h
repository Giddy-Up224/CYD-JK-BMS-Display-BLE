#pragma once
// The Cheap Yellow Display (ESP32-2432S028R): pins of the display, touch and
// backlight. See
// https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md
// Display and touch values come from D:\repo2\genesis, where they were
// measured on a CYD.

#include "driver/gpio.h"
#include "driver/spi_common.h"

#define BOARD_NAME "Cheap Yellow Display"

// ILI9341, 240 x 320, BGR. Some newer CYDs have an ST7789 that needs
// LCD_INVERT true.
#define LCD_HOST       SPI2_HOST
#define LCD_SCK        GPIO_NUM_14
#define LCD_MOSI       GPIO_NUM_13
#define LCD_MISO       GPIO_NUM_12
#define LCD_CS         GPIO_NUM_15
#define LCD_DC         GPIO_NUM_2
#define LCD_RST        GPIO_NUM_NC
#define LCD_BL         GPIO_NUM_21
#define LCD_INVERT     false
#define LCD_PCLK_HZ    (40 * 1000 * 1000)
// Landscape: rows and columns swapped, then mirrored as set here. Both true
// turns the picture by 180 degrees (USB on the other side).
#define LCD_MIRROR_X   false
#define LCD_MIRROR_Y   false

// XPT2046 resistive touch on its own pins, read by bit-banging
#define TOUCH_XPT2046
#define TOUCH_CLK      GPIO_NUM_25
#define TOUCH_MOSI     GPIO_NUM_32
#define TOUCH_MISO     GPIO_NUM_39
#define TOUCH_CS       GPIO_NUM_33
#define TOUCH_IRQ      GPIO_NUM_36
// The XPT2046's X channel runs along the screen's short side in landscape, so
// screen x comes from its Y channel and screen y from its X channel.
#define TOUCH_SWAP_XY  true
// Raw readings at the screen's edges, after the swap: left/right, top/bottom.
// Swap a pair if touches come out mirrored.
#define TOUCH_RAW_X_MIN 350
#define TOUCH_RAW_X_MAX 3650
#define TOUCH_RAW_Y_MIN 330
#define TOUCH_RAW_Y_MAX 3820

#define LCD_H_RES 320  // landscape
#define LCD_V_RES 240
