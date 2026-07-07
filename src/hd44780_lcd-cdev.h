#ifndef HD44780_LCD_HD44780_LCD_CDEV_H
#define HD44780_LCD_HD44780_LCD_CDEV_H

#include "hd44780_lcd-common.h"
#include <linux/fs.h>

int hd44780_lcd_create_cdev(const struct device *dev, struct lcd_data *lcd_data);
void hd44780_lcd_remove_cdev(void);

#endif // HD44780_LCD_HD44780_LCD_CDEV_H
