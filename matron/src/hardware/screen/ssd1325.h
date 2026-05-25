#pragma once

#include <arm_neon.h>
#include <cairo.h>
#include <fcntl.h>
#include <gpiod.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <linux/types.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "platform.h"

#define SPIDEV_0_0_PATH "/dev/spidev0.0"
#define SPI0_BUS_WIDTH 8

// Pinout for Fates with SSD1325
// DC (Data/Command) = GPIO17, RESET = GPIO4
#define SSD1325_DC_AND_RESET_GPIO_CHIP "gpiochip0"
#define SSD1325_DC_GPIO_LINE 17
#define SSD1325_RESET_GPIO_LINE 4

// SSD1325 Command Set (from datasheet Section 10)
#define SSD1325_SET_COLUMN_ADDRESS 0x15
#define SSD1325_SET_ROW_ADDRESS 0x75
#define SSD1325_WRITE_RAM_COMMAND 0x5C
#define SSD1325_READ_RAM_COMMAND 0x5D
#define SSD1325_SET_REMAP_DUAL_COM 0xA0
#define SSD1325_SET_DISPLAY_START_LINE 0xA1
#define SSD1325_SET_DISPLAY_OFFSET 0xA2
#define SSD1325_SET_DISPLAY_MODE_ALL_OFF 0xA4
#define SSD1325_SET_DISPLAY_MODE_ALL_ON 0xA5
#define SSD1325_SET_DISPLAY_MODE_NORMAL 0xA6
#define SSD1325_SET_DISPLAY_MODE_INVERSE 0xA7
#define SSD1325_ENABLE_PARTIAL_DISPLAY 0xA8
#define SSD1325_EXIT_PARTIAL_DISPLAY 0xA9
#define SSD1325_SET_MASTER_CONFIG 0xAD
#define SSD1325_SET_DISPLAY_OFF 0xAE
#define SSD1325_SET_DISPLAY_ON 0xAF
#define SSD1325_SET_OSCILLATOR_FREQUENCY 0xB3
#define SSD1325_SET_PRECHARGE_COMPARATOR 0xB4
#define SSD1325_ENABLE_PRECHARGE_COMPARATOR 0xB0
#define SSD1325_SET_ROW_PERIOD 0xB2
#define SSD1325_SET_PHASE_LENGTH 0xB1
#define SSD1325_SET_GRAYSCALE_TABLE 0xB8
#define SSD1325_SET_DEFAULT_LINEAR_GRAY_SCALE 0xB9
#define SSD1325_SET_PRECHARGE_VOLTAGE 0xBB
#define SSD1325_SET_VCOMH_VOLTAGE 0xBE
#define SSD1325_SET_CONTRAST_CURRENT 0x81
#define SSD1325_SET_MULTIPLEX_RATIO 0xCA
#define SSD1325_SET_GPIO 0xB5
#define SSD1325_SET_FULL_CURRENT_RANGE 0x86

// Resolution: 128x64 pixels, 4-bit grayscale (2 pixels per byte)
// See datasheet Section 9.9 - GDDRAM Data Structure:
// "Each gray-scale dot is defined by 4 bits, and each byte defines two dots"
// "The higher nibble corresponds to the left (even) column, 
//  and the lower nibble to the right (odd) column"
#define SSD1325_PIXEL_WIDTH 128
#define SSD1325_PIXEL_HEIGHT 64
#define SSD1325_GRAYSCALE_MAX_VALUE 15.0

typedef enum {
    SSD1325_DISPLAY_MODE_ALL_OFF = 0,
    SSD1325_DISPLAY_MODE_ALL_ON,
    SSD1325_DISPLAY_MODE_NORMAL,
    SSD1325_DISPLAY_MODE_INVERT,
} ssd1325_display_mode_t;

void ssd1325_init();
void ssd1325_deinit();
void ssd1325_refresh();
void ssd1325_update(cairo_surface_t *surface, bool should_translate_color);
void ssd1325_set_brightness(uint8_t b);
void ssd1325_set_contrast(uint8_t c);
void ssd1325_set_display_mode(ssd1325_display_mode_t);
void ssd1325_set_gamma(double g);
void ssd1325_set_refresh_rate(uint8_t hz);
uint8_t *ssd1325_resize_buffer(size_t);