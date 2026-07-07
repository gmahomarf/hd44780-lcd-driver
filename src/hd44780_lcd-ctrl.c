#include "hd44780_lcd-ctrl.h"

#include <linux/gpio/consumer.h>
#include <linux/types.h>
#include <linux/delay.h>

// Most instructions complete in 39 us, but a couple do so in 43 us. This covers both
#define SLEEP_SHORT_MICROSECONDS 50
#define SLEEP_LONG_MICROSECONDS 1800

uint8_t hd44780_lcd_get_address(const struct lcd_data *lcd_data);
uint8_t hd44780_lcd_read_byte(const struct lcd_data *lcd_data);
void hd44780_lcd_wait_busy(const struct lcd_data *lcd_data);
void hd44780_lcd_enable(const struct lcd_data *lcd_data);
void hd44780_lcd_disable(const struct lcd_data *lcd_data);
void hd44780_lcd_write_instruction(const struct lcd_data *lcd_data, uint8_t instruction);
void hd44780_lcd_write_byte(const struct lcd_data *lcd_data, uint8_t data);
void hd44780_lcd_set_raw_address(const struct lcd_data *lcd_data, uint8_t address);
void hd44780_lcd_set_pos(const struct lcd_data *lcd_data, uint8_t row, uint8_t column);
void hd44780_lcd_wait_short(void);
void hd44780_lcd_wait_long(void);

void hd44780_lcd_turn_on(const struct lcd_data *lcd_data) {
	hd44780_lcd_write_instruction(lcd_data,
		INSTRUCTION_DISPLAY_STATE | DISPLAY_STATE_ON | DISPLAY_STATE_CURSOR_ON | DISPLAY_STATE_CURSOR_BLINK_ON);
	hd44780_lcd_wait_short();
}

void hd44780_lcd_turn_off(const struct lcd_data *lcd_data) {
	hd44780_lcd_write_instruction(lcd_data,
		INSTRUCTION_DISPLAY_STATE | DISPLAY_STATE_OFF | DISPLAY_STATE_CURSOR_OFF | DISPLAY_STATE_CURSOR_BLINK_OFF);
	hd44780_lcd_wait_short();
}

void hd44780_lcd_clear(const struct lcd_data *lcd_data) {
	hd44780_lcd_write_instruction(lcd_data, INSTRUCTION_CLEAR_DISPLAY);
	hd44780_lcd_wait_long();
}

void hd44780_lcd_write_data(const struct lcd_data *lcd_data, const char *data, const size_t data_length) {
	gpiod_set_value(lcd_data->rs, rs_data);
	gpiod_set_value(lcd_data->rw, rw_write);
	for (int i = 0; i < data_length; i++) {
		hd44780_lcd_write_byte(lcd_data, data[i]);
		hd44780_lcd_wait_short();
	}
}

void hd44780_lcd_read_data(const struct lcd_data *lcd_data, char *buf, const size_t buf_length) {
	gpiod_set_value(lcd_data->rs, rs_data);
	gpiod_set_value(lcd_data->rw, rw_read);

	const size_t limit = buf_length <= LCD_MAX_BUFFER_LENGTH ? buf_length : LCD_MAX_BUFFER_LENGTH;
	for (int i = 0; i < limit; i++) {
		buf[i] = hd44780_lcd_read_byte(lcd_data);
		// LCD_DEBUG("Char: %02x (%c)\n", buf[i], buf[i]);
	}
}

void hd44780_lcd_init(struct lcd_data *lcd_data) {
	gpiod_set_value(lcd_data->rs, rs_data);
	gpiod_set_value(lcd_data->rw, rw_read);

	// Reading without init seems to return 0xF2 then 0x22 (i.e., `"`). Not sure why...
	// Either way, let's check it 4 times in a row. If all 4 reads return 0xF2/0x22
	// we can safely(?) assume the LCD hasn't been initialized
	uint8_t c = 0x20;
	int n;
	for (n = 0; n < 4; n++) {
		c = hd44780_lcd_read_byte(lcd_data);
		if (c != 0x22 && c != 0xF2) {
			LCD_DEBUG("Not running init. c: %X", c);
			lcd_data->initialized = true;
			return;
		}
	}
	LCD_DEBUG("Running init. c was 0x%02X", c);

	// Move cursor back n spaces because reading moved it forward n spaces
	const uint8_t address = hd44780_lcd_get_address(lcd_data);
	if (address == 0x00) {
		hd44780_lcd_set_raw_address(lcd_data, 0x67);
	} else if (address == 0x40) {
		hd44780_lcd_set_raw_address(lcd_data, 0x27);
	} else {
		hd44780_lcd_set_raw_address(lcd_data, address - n);
	}

	// To enable 4-bit mode, this needs to be sent as a single write. All other instructions are sent as two writes
	const int instruction = INSTRUCTION_SET_FUNCTION | FUNCTION_DATA_LENGTH_4;
	hd44780_lcd_enable(lcd_data);
	for (int i = 4; i < 8; i++) {
		gpiod_direction_output(lcd_data->data[i - 4], GPIOD_OUT_LOW);
		gpiod_set_value(lcd_data->data[i - 4], (instruction >> i) & 1);
	}
	hd44780_lcd_disable(lcd_data);
	hd44780_lcd_wait_short();

	hd44780_lcd_write_instruction(
		lcd_data, INSTRUCTION_SET_FUNCTION | FUNCTION_DATA_LENGTH_4 | FUNCTION_FONT_5X8 | FUNCTION_LINES_2);
	hd44780_lcd_wait_short();

	hd44780_lcd_turn_on(lcd_data);

	hd44780_lcd_write_instruction(lcd_data, INSTRUCTION_SET_ENTRY_MODE | ENTRY_MODE_INCREMENT);
	hd44780_lcd_wait_short();

	hd44780_lcd_clear(lcd_data);
	hd44780_lcd_wait_long();
}

// private functions

uint8_t hd44780_lcd_get_address(const struct lcd_data *lcd_data) {
	gpiod_set_value(lcd_data->rs, rs_instruction);
	gpiod_set_value(lcd_data->rw, rw_read);

	// Addresses are only 7 bits long. The MSB is the busy flag, so we & it out
	return hd44780_lcd_read_byte(lcd_data) & 0x7F;
}

uint8_t hd44780_lcd_read_byte(const struct lcd_data *lcd_data) {
	gpiod_direction_input(lcd_data->d4);
	gpiod_direction_input(lcd_data->d5);
	gpiod_direction_input(lcd_data->d6);
	gpiod_direction_input(lcd_data->d7);

	uint8_t c = 0;

	hd44780_lcd_enable(lcd_data);

	c |= gpiod_get_value(lcd_data->d7) << 7;
	c |= gpiod_get_value(lcd_data->d6) << 6;
	c |= gpiod_get_value(lcd_data->d5) << 5;
	c |= gpiod_get_value(lcd_data->d4) << 4;

	hd44780_lcd_disable(lcd_data);
	udelay(50);
	hd44780_lcd_enable(lcd_data);

	c |= gpiod_get_value(lcd_data->d7) << 3;
	c |= gpiod_get_value(lcd_data->d6) << 2;
	c |= gpiod_get_value(lcd_data->d5) << 1;
	c |= gpiod_get_value(lcd_data->d4);

	hd44780_lcd_disable(lcd_data);
	udelay(50);

	return c;
}

void hd44780_lcd_wait_busy(const struct lcd_data *lcd_data) {
	const int rs_value = gpiod_get_value(lcd_data->rs);
	const int rw_value = gpiod_get_value(lcd_data->rw);
	gpiod_set_value(lcd_data->rs, rs_instruction);
	gpiod_set_value(lcd_data->rw, rw_read);
	gpiod_direction_input(lcd_data->d7);
	const int max = 200;
	int c = 0;
	bool flag = false;
	while (c < max && !flag) {
		hd44780_lcd_enable(lcd_data);
		const int d7 = gpiod_get_value(lcd_data->d7);
		if (d7 < 0) {
			LCD_ERROR("Error getting d7: %d", d7);
			return;
		} else if (!d7) {
			flag = true;
		}
		hd44780_lcd_disable(lcd_data);

		// 4-bit operation requires us to read all 8 bits, even if we only care about the top 4
		hd44780_lcd_enable(lcd_data);
		udelay(5);
		hd44780_lcd_disable(lcd_data);

		c++;
	}
	if (c >= max) {
		LCD_DEBUG("Busy after %d loops", max);
	}
	gpiod_direction_output(lcd_data->d7, GPIOD_OUT_LOW);
	gpiod_set_value(lcd_data->rw, rw_value);
	gpiod_set_value(lcd_data->rs, rs_value);
	udelay(5);
}

void hd44780_lcd_write_instruction(const struct lcd_data *lcd_data, const uint8_t instruction) {
	gpiod_set_value(lcd_data->rs, rs_instruction);
	gpiod_set_value(lcd_data->rw, rw_write);

	hd44780_lcd_write_byte(lcd_data, instruction);
}

void hd44780_lcd_write_byte(const struct lcd_data *lcd_data, const uint8_t data) {
	gpiod_direction_output(lcd_data->d4, GPIOD_OUT_LOW);
	gpiod_direction_output(lcd_data->d5, GPIOD_OUT_LOW);
	gpiod_direction_output(lcd_data->d6, GPIOD_OUT_LOW);
	gpiod_direction_output(lcd_data->d7, GPIOD_OUT_LOW);

	hd44780_lcd_enable(lcd_data);
	for (int i = 4; i < 8; i++) {
		const int dat = (data >> i) & 1;
		gpiod_set_value(lcd_data->data[i - 4], dat);
	}
	hd44780_lcd_disable(lcd_data);

	udelay(5);

	hd44780_lcd_enable(lcd_data);
	for (int i = 0; i < 4; i++) {
		const int dat = (data >> i) & 1;
		gpiod_set_value(lcd_data->data[i], dat);
	}
	hd44780_lcd_disable(lcd_data);
}

void hd44780_lcd_enable(const struct lcd_data *lcd_data) {
	gpiod_set_value(lcd_data->en, 1);
	// Enable requires a minimum pulse width of 230ns. Let's give it a few extra just in case
	ndelay(250);
}

void hd44780_lcd_disable(const struct lcd_data *lcd_data) {
	gpiod_set_value(lcd_data->en, 0);
}

void hd44780_lcd_set_raw_address(const struct lcd_data *lcd_data, const uint8_t address) {
	hd44780_lcd_write_instruction(lcd_data, INSTRUCTION_SET_DD_RAM_ADDRESS | address);
	hd44780_lcd_wait_short();
}

void hd44780_lcd_set_pos(const struct lcd_data *lcd_data, uint8_t row, uint8_t column) {
	if (row > 3) {
		row = 3;
	}
	if (column > 19) {
		column = 19;
	}

	hd44780_lcd_set_raw_address(lcd_data, line_addresses[row] + column);
}

void hd44780_lcd_wait_short() {
	udelay(SLEEP_SHORT_MICROSECONDS);
}

/**
 * This long wait should only be used after clearing the display or
 * returning home
 */
void hd44780_lcd_wait_long() {
	udelay(SLEEP_LONG_MICROSECONDS);
}