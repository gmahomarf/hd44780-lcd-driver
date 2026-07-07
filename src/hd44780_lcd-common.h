#ifndef HD44780_LCD_UTIL_H
#define HD44780_LCD_UTIL_H

#include <linux/cdev.h>

#define LCD_DRIVER_NAME "hd44780_lcd"
#define LCD_MAX_BUFFER_LENGTH 80

#define LCD_ERROR(fmt, ...) printk(KERN_ERR LCD_DRIVER_NAME LCD_DRIVER_NAME ": " fmt, ##__VA_ARGS__)
#define LCD_INFO(fmt, ...) printk(KERN_INFO LCD_DRIVER_NAME LCD_DRIVER_NAME ": " fmt, ##__VA_ARGS__)
#define LCD_DEBUG(fmt, ...) printk(KERN_DEBUG LCD_DRIVER_NAME LCD_DRIVER_NAME ": " fmt, ##__VA_ARGS__)

struct lcd_data {
	struct gpio_desc *rw;
	struct gpio_desc *rs;
	struct gpio_desc *en;
	struct cdev cdev;
	bool initialized;
	union {
		struct {
			struct gpio_desc *d4;
			struct gpio_desc *d5;
			struct gpio_desc *d6;
			struct gpio_desc *d7;
		};
		struct gpio_desc *data[4];
	};
};

#endif // HD44780_LCD_UTIL_H
