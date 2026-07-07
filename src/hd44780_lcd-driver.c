//
// Created by gazy on 01-07-2026.
//

#include "hd44780_lcd-cdev.h"
#include "hd44780_lcd-driver.h"
#include "hd44780_lcd-common.h"
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/slab.h>

static const char *const pin_names[] = {
	LCD_DRIVER_NAME "_RS",
	LCD_DRIVER_NAME "_RW",
	LCD_DRIVER_NAME "_Enable",
	LCD_DRIVER_NAME "_D4",
	LCD_DRIVER_NAME "_D5",
	LCD_DRIVER_NAME "_D6",
	LCD_DRIVER_NAME "_D7",
};

static struct platform_driver lcd_driver;
static struct lcd_data *lcd_data;

int lcd_driver_register() {
	return platform_driver_register(&lcd_driver);
}

void lcd_driver_unregister() {
	return platform_driver_unregister(&lcd_driver);
}

static int hd44780_lcd_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	int error;

	struct gpio_descs *lcd_pins = devm_gpiod_get_array(dev, "lcd", GPIOD_OUT_LOW);

	if (IS_ERR(lcd_pins)) {
		dev_warn(dev, "failed to get lcd_pins: %ld\n", PTR_ERR(lcd_pins));
		error = PTR_ERR(lcd_pins);
		goto cleanup;
	}

	lcd_data = kzalloc(sizeof(struct lcd_data), GFP_KERNEL);
	if (!lcd_data) {
		dev_warn(dev, "failed to allocate lcd_data\n");
		error = -ENOMEM;
		goto cleanup;
	}

	lcd_data->initialized = false;

	for (int i = 0; i < lcd_pins->ndescs; i++) {
		gpiod_set_consumer_name(lcd_pins->desc[i], pin_names[i]);
		gpiod_set_value(lcd_pins->desc[i], 0);
	}

	// See lcd-gpios.dts for order
	lcd_data->rs = lcd_pins->desc[0];
	lcd_data->rw = lcd_pins->desc[1];
	lcd_data->en = lcd_pins->desc[2];
	lcd_data->d4 = lcd_pins->desc[3];
	lcd_data->d5 = lcd_pins->desc[4];
	lcd_data->d6 = lcd_pins->desc[5];
	lcd_data->d7 = lcd_pins->desc[6];

	dev_info(dev, "registered gpios\n");

	int ret = hd44780_lcd_create_cdev(dev, lcd_data);
	if (ret < 0) {
		dev_warn(dev, "failed to create cdev: %d\n", ret);
		error = ret;
		goto cleanup;
	}

	dev_info(dev, "cdev created\n");

	return 0;
cleanup:
	return error;
};

static void hd44780_lcd_shutdown(struct platform_device *pdev) {
	hd44780_lcd_remove_cdev();
	kfree(lcd_data);
	dev_info(&pdev->dev, "shutting down\n");
}

static struct of_device_id lcd_device_table[] = {
	{
		.compatible = LCD_DRIVER_NAME,
	 },
	{}
};

MODULE_DEVICE_TABLE(of, lcd_device_table);

// clang-format off
// this is formatted so freaking weird...
static struct platform_driver lcd_driver = {
	.probe = &hd44780_lcd_probe,
	.remove = &hd44780_lcd_shutdown,
	.shutdown = &hd44780_lcd_shutdown,
	.driver ={
		.name = LCD_DRIVER_NAME,
		.of_match_table = lcd_device_table,
	},
};
// clang-format on