//
// Created by gazy on 02-07-2026.
//

#ifndef HD44780_LCD_HD44780_LCD_CTRL_H
#define HD44780_LCD_HD44780_LCD_CTRL_H

#include <linux/types.h>
#include "hd44780_lcd-common.h"

enum rs_values {
	rs_instruction,
	rs_data,
};

enum rw_values {
	rw_write,
	rw_read,
};

#define LINE_WIDTH 20
static const uint8_t line_addresses[4] = {
	0x00,
	0x40,
	0x14,
	0x54,
};

// Instructions
enum instructions {
	INSTRUCTION_CLEAR_DISPLAY = 0x01,
	INSTRUCTION_RETURN_HOME = 0x02,
	INSTRUCTION_SET_ENTRY_MODE = 0x04,
	INSTRUCTION_DISPLAY_STATE = 0x08,
	INSTRUCTION_CURSOR_DISPLAY_SHIFT = 0x10,
	INSTRUCTION_SET_FUNCTION = 0x20,
	INSTRUCTION_SET_CG_RAM_ADDRESS = 0x40,
	INSTRUCTION_SET_DD_RAM_ADDRESS = 0x80,
};

/**
 * @brief Entry modes
 *
 * Bits: 0000 01XS
 * Where:
 *   - X is increment (1) or decrement (0)
 *   - S is display shift (on/off)
 */
enum entry_modes { ENTRY_MODE_DECREMENT = 0x00, ENTRY_MODE_INCREMENT = 0x02, ENTRY_MODE_SHIFT = 0x01 };

/**
 * @brief Display states.
 *
 * Bits: 0000 1DCB
 * Where:
 *   - D is display state (on/off)
 *   - C is cursor state (on/off)
 *   - B is cursor blink (on/off)
 *
 */
enum display_states {
	DISPLAY_STATE_OFF = 0x00,
	DISPLAY_STATE_ON = 0x04,
	DISPLAY_STATE_CURSOR_OFF = 0x00,
	DISPLAY_STATE_CURSOR_ON = 0x02,
	DISPLAY_STATE_CURSOR_BLINK_OFF = 0x00,
	DISPLAY_STATE_CURSOR_BLINK_ON = 0x01,
};

/**
 * @brief Cursor/Display shifts.
 *
 * Bits: 0001 XD--
 * Where:
 *   - X is display (1) or cursor (0)
 *   - D is direction: right (1) or left (0)
 *
 */
enum cursor_display_shifts {
	CURSOR_DISPLAY_SHIFT_CURSOR = 0x00,
	CURSOR_DISPLAY_SHIFT_DISPLAY = 0x08,
	CURSOR_DISPLAY_SHIFT_LEFT = 0x00,
	CURSOR_DISPLAY_SHIFT_RIGHT = 0x04,
};

/**
 * @brief Cursor/Display shifts.
 *
 * Bits: 001L NF--
 * Where:
 *   - L is data length: 4 bits (0) or 8 bits (1)
 *   - N is number of lines: 1 line (0) or 2 lines (1)
 *   - F is font type:  5 × 10 dots (1), 5 × 8 dots (0)
 *
 */
enum function {
	FUNCTION_DATA_LENGTH_4 = 0x00,
	FUNCTION_DATA_LENGTH_8 = 0x10,
	FUNCTION_LINES_1 = 0x00,
	FUNCTION_LINES_2 = 0x08,
	FUNCTION_FONT_5X8 = 0x00,
	FUNCTION_FONT_5X10 = 0x04,
};

void hd44780_lcd_init(struct lcd_data *lcd_data);

/**
 * @brief Turn the HD44780_LCD display on
 *
 * @param lcd_data The HD44780_LCD data struct to control
 */
void hd44780_lcd_turn_on(const struct lcd_data *lcd_data);

/**
 * @brief Turn the HD44780_LCD display off
 *
 * @param lcd_data The HD44780_LCD data struct to control
 */
void hd44780_lcd_turn_off(const struct lcd_data *lcd_data);

/**
 * @brief Show a string on the HD44780_LCD display
 *
 * @param lcd_data The HD44780_LCD data struct
 * @param data The data to write
 * @param data_length the length of the data to write
 */
void hd44780_lcd_write_data(const struct lcd_data *lcd_data, const char *data, size_t data_length);

/**
 * @brief Show a string on the HD44780_LCD display
 *
 * @param lcd_data The HD44780_LCD data struct
 * @param buf The buffer in which to save the data
 * @param buf_length The length of data to read
 */
void hd44780_lcd_read_data(const struct lcd_data *lcd_data, char *buf, size_t buf_length);

/**
 * @brief Update a single character in the display
 *
 * @param lcd_data The HD44780_LCD data struct
 * @param pos The position to set
 */
void hd44780_lcd_set_position(struct lcd_data *lcd_data, uint8_t pos);

void hd44780_lcd_set_coords(const struct lcd_data *lcd_data, uint8_t row, uint8_t column);

void hd44780_lcd_clear(const struct lcd_data *lcd_data);
void hd44780_set_backlight_color(const struct lcd_data *lcd_data, uint8_t red, uint8_t green, uint8_t blue);
void hd44780_lcd_set_lines(struct lcd_data *lcd_data, uint8_t lines);

#endif //HD44780_LCD_HD44780_LCD_CTRL_H
