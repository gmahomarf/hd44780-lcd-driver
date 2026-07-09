//
// Created by gazy on 01-07-2026.
//

#include "hd44780_lcd-cdev.h"
#include "hd44780_lcd-driver.h"
#include "hd44780_lcd-common.h"
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define OPS_PINS_CON_ID "ops"
#define BACKLIGHT_RED_CON_ID "backlight_r"
#define BACKLIGHT_GREEN_CON_ID "backlight_g"
#define BACKLIGHT_BLUE_CON_ID "backlight_b"

static const char *const ops_pin_names[] = {
	LCD_DRIVER_NAME "_RS",
	LCD_DRIVER_NAME "_RW",
	LCD_DRIVER_NAME "_Enable",
	LCD_DRIVER_NAME "_D4",
	LCD_DRIVER_NAME "_D5",
	LCD_DRIVER_NAME "_D6",
	LCD_DRIVER_NAME "_D7",
};

static const char *const backlight_pin_names[] = {
	LCD_DRIVER_NAME "_BL_R",
	LCD_DRIVER_NAME "_BL_G",
	LCD_DRIVER_NAME "_BL_B",
};

static struct platform_driver lcd_driver;
static struct lcd_data *lcd_data;

int lcd_driver_register() {
	return platform_driver_register(&lcd_driver);
}

void lcd_driver_unregister() {
	return platform_driver_unregister(&lcd_driver);
}

static void reset_pwm(struct pwm_device *pwm) {
	struct pwm_state state;
	pwm_init_state(pwm, &state);
	state.polarity = PWM_POLARITY_INVERSED;
	state.period = LCD_BACKLIGHT_PERIOD;
	state.duty_cycle = LCD_BACKLIGHT_PERIOD >> 1;
	pwm_apply_might_sleep(pwm, &state);
}

static int hd44780_lcd_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	int error;

	struct gpio_descs *lcd_ops_pins = devm_gpiod_get_array(dev, OPS_PINS_CON_ID, GPIOD_OUT_LOW);

	if (IS_ERR(lcd_ops_pins)) {
		dev_err(dev, "failed to get lcd_pins: %ld\n", PTR_ERR(lcd_ops_pins));
		error = PTR_ERR(lcd_ops_pins);
		goto cleanup;
	}

	lcd_data = kzalloc(sizeof(struct lcd_data), GFP_KERNEL);
	if (!lcd_data) {
		dev_err(dev, "failed to allocate lcd_data\n");
		error = -ENOMEM;
		goto cleanup;
	}

	lcd_data->initialized = false;

	for (int i = 0; i < lcd_ops_pins->ndescs; i++) {
		gpiod_set_consumer_name(lcd_ops_pins->desc[i], ops_pin_names[i]);
		gpiod_set_value(lcd_ops_pins->desc[i], 0);
	}

	// See lcd-gpios.dts for order
	lcd_data->rs = lcd_ops_pins->desc[0];
	lcd_data->rw = lcd_ops_pins->desc[1];
	lcd_data->en = lcd_ops_pins->desc[2];
	lcd_data->d4 = lcd_ops_pins->desc[3];
	lcd_data->d5 = lcd_ops_pins->desc[4];
	lcd_data->d6 = lcd_ops_pins->desc[5];
	lcd_data->d7 = lcd_ops_pins->desc[6];

	dev_dbg(dev, "Registered ops GPIOs\n");

	if (of_property_present(dev->of_node, "pwms") && of_property_present(dev->of_node, "pwm-names")) {
		lcd_data->rgb_enabled = true;
		struct pwm_device *g;
		struct pwm_device *b;
		struct pwm_device *r = devm_pwm_get(dev, BACKLIGHT_RED_CON_ID);
		if (IS_ERR(r)) {
			lcd_data->rgb_enabled = false;
			dev_warn(dev, "Failed to get backlight pin R: %ld. Disabling RGB.\n", PTR_ERR(r));
		}

		if (lcd_data->rgb_enabled) {
			g = devm_pwm_get(dev, BACKLIGHT_GREEN_CON_ID);
			if (IS_ERR(g)) {
				lcd_data->rgb_enabled = false;
				dev_warn(dev, "Failed to get backlight pin G: %ld. Disabling RGB.\n", PTR_ERR(g));
			}
		}

		if (lcd_data->rgb_enabled) {
			b = devm_pwm_get(dev, BACKLIGHT_BLUE_CON_ID);
			if (IS_ERR(b)) {
				lcd_data->rgb_enabled = false;
				dev_warn(dev, "Failed to get backlight pin B: %ld. Disabling RGB.\n", PTR_ERR(b));
			}
		}

		if (lcd_data->rgb_enabled) {
			lcd_data->bl_r = r;
			lcd_data->bl_g = g;
			lcd_data->bl_b = b;

			reset_pwm(r);
			reset_pwm(g);
			reset_pwm(b);

			dev_dbg(dev, "Registered backlight PWM devices\n");
		}
	} else {
		dev_warn(dev, "Backlight configuration missing in Device Tree. Disabling RGB.");
		lcd_data->rgb_enabled = false;
	}

	int ret = hd44780_lcd_create_cdev(dev, lcd_data);
	if (ret < 0) {
		dev_err(dev, "failed to create cdev: %d\n", ret);
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