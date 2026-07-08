#ifndef HD44780_LCD_UTIL_H
#define HD44780_LCD_UTIL_H

#include <linux/cdev.h>
#include <linux/pwm.h>

#define LCD_DRIVER_NAME "hd44780_lcd"
#define LCD_LINE_LENGTH 20
#define LCD_LINE_COUNT 4
#define LCD_BUFFER_LENGTH LCD_LINE_LENGTH * LCD_LINE_COUNT
#define LCD_BACKLIGHT_PERIOD 100000 // Nanoseconds, ~10kHz

#define LCD_LOG_PREFIX LCD_DRIVER_NAME " " LCD_DRIVER_NAME ": "
#define LCD_ERROR(fmt, ...) printk(KERN_ERR LCD_LOG_PREFIX fmt, ##__VA_ARGS__)
#define LCD_INFO(fmt, ...) printk(KERN_INFO LCD_LOG_PREFIX fmt, ##__VA_ARGS__)
#define LCD_DEBUG(fmt, ...) printk(KERN_DEBUG LCD_LOG_PREFIX fmt, ##__VA_ARGS__)

struct lcd_data {
	// Internal data
	struct cdev cdev;
	bool initialized;
	bool rgb_enabled;
	uint8_t pos;

	// LCD Operations pins
	struct gpio_desc *rw;
	struct gpio_desc *rs;
	struct gpio_desc *en;
	union {
		struct {
			struct gpio_desc *d4;
			struct gpio_desc *d5;
			struct gpio_desc *d6;
			struct gpio_desc *d7;
		};
		struct gpio_desc *data[4];
	};

	// LCD Backlight
	struct pwm_device *bl_r;
	struct pwm_device *bl_g;
	struct pwm_device *bl_b;
};

#endif // HD44780_LCD_UTIL_H
