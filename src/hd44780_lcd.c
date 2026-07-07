#include "hd44780_lcd-cdev.h"
#include "hd44780_lcd-driver.h"
#include "hd44780_lcd-dtbo.h"
#include "hd44780_lcd-common.h"
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>

int ovcs_id = -1;
static int __init lcd_init(void) {
	int error = 0;

	int ret = lcd_driver_register();
	if (ret) {
		LCD_ERROR("failed to register driver: %d\n", ret);
		error = ret;
		goto cleanup;
	}
	LCD_DEBUG("registered driver\n");

	struct device_node *base = of_find_node_by_path("/");
	if (!base) {
		LCD_ERROR("failed to find base\n");
		lcd_driver_unregister();
		return -ENOENT;
	}
	ret = of_overlay_fdt_apply(lcd_dtbo, lcd_dtbo_len, &ovcs_id, base);
	if (ret < 0) {
		LCD_ERROR("failed to apply overlay: %d\n", ret);
		error = ret;
		of_node_put(base);
		goto cleanup;
	}

	struct device_node *module_node = of_find_node_by_path("/" LCD_DRIVER_NAME);
	if (!module_node) {
		LCD_ERROR("failed to find module_node\n");
		error = -ENOENT;
		of_node_put(base);
		goto cleanup;
	}

	of_node_put(base);
	of_node_put(module_node);

	return 0;
cleanup:
	of_overlay_remove(&ovcs_id);
	return error;
}

static void __exit lcd_exit(void) {
	of_overlay_remove(&ovcs_id);
	lcd_driver_unregister();
	LCD_DEBUG("unregistered driver\n");
}

module_init(lcd_init);
module_exit(lcd_exit);

MODULE_AUTHOR("Gazy Mahomar");
MODULE_DESCRIPTION("HD44780 Linux char device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hd44780_lcd");
