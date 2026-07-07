//
// Created by gazy on 01-07-2026.
//

#ifndef HD44780_LCD_HD44780_LCD_PROBE_H
#define HD44780_LCD_HD44780_LCD_PROBE_H

#include <linux/platform_device.h>

int lcd_driver_register(void);
void lcd_driver_unregister(void);

#endif // HD44780_LCD_HD44780_LCD_PROBE_H
