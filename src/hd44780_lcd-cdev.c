#include "hd44780_lcd-cdev.h"
#include "hd44780_lcd-ctrl.h"
#include "hd44780_lcd-common.h"
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>

int major;
const int minor = 0;
const int no_devs = 1;
static struct device *lcd_dev;
static struct class *lcd_class;
static struct file_operations fops;

int hd44780_lcd_create_cdev(const struct device *dev, struct lcd_data *lcd_data) {
	dev_t dev_no;
	int res = alloc_chrdev_region(&dev_no, minor, no_devs, LCD_DRIVER_NAME);
	if (res < 0) {
		dev_err(dev, "can't get major\n");
		return res;
	}
	major = MAJOR(dev_no);

	dev_dbg(dev, "allocated cdev region. major: %d\n", major);

	cdev_init(&lcd_data->cdev, &fops);
	lcd_data->cdev.owner = THIS_MODULE;

	int error;
	res = cdev_add(&lcd_data->cdev, dev_no, 1);
	if (res < 0) {
		dev_warn(dev, "Could not add character device: %d", res);
		error = res;
		goto cleanup;
	}

	dev_dbg(dev, "character device added\n");

	lcd_class = class_create("lcd");
	if (IS_ERR(lcd_class)) {
		error = PTR_ERR(lcd_class);
		dev_err(dev, "Could not create class: %d", error);
		cdev_del(&lcd_data->cdev);
		goto cleanup;
	}

	dev_dbg(dev, "lcd class created: %s\n", lcd_class->name);

	lcd_dev = device_create(lcd_class, NULL, dev_no, lcd_data, "lcd0");
	if (!lcd_data) {
		dev_warn(dev, "lcd_data is null");
	}
	if (IS_ERR(lcd_dev)) {
		error = PTR_ERR(lcd_dev);
		dev_err(dev, "Could not create device: %d", error);
		cdev_del(&lcd_data->cdev);
		class_destroy(lcd_class);
		goto cleanup;
	}
	dev_dbg(dev, "lcd device created: %s (%u)\n", lcd_dev->kobj.name, lcd_dev->id);

	return 0;
cleanup:
	unregister_chrdev_region(dev_no, no_devs);
	return error;
}

void hd44780_lcd_remove_cdev() {
	dev_t dev_no = MKDEV(major, minor);
	device_destroy(lcd_class, dev_no);
	class_destroy(lcd_class);
	cdev_del(&((struct lcd_data *)lcd_dev->driver_data)->cdev);
	unregister_chrdev_region(dev_no, no_devs);
}

static ssize_t hd44780_lcd_write(struct file *file, const char __user *buf, const size_t count, loff_t *off) {
	const struct lcd_data *lcd_data = file->private_data;
	char *data = kmalloc(count + 1, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, count + 1);
	if (copy_from_user(data, buf, count)) {
		return -EFAULT;
	}

	switch (data[0]) {
	case 0x5: {
		LCD_INFO("Clearing lcd...\n");
		hd44780_lcd_clear(lcd_data);
		break;
	}
	case 0x7: {
		LCD_DEBUG("Turning lcd on...\n");
		hd44780_lcd_turn_on(lcd_data);
		break;
	}
	case 0x9: {
		LCD_DEBUG("Turning lcd off...\n");
		hd44780_lcd_turn_off(lcd_data);
		break;
	}
	default: {
		LCD_DEBUG("Wrote to lcd: %s\n", data);
		hd44780_lcd_write_data(lcd_data, data, count);
	}
	}

	kfree(data);

	return (ssize_t)count;
}

static ssize_t hd44780_lcd_read(struct file *file, char __user *buf, size_t count, loff_t *off) {
	const struct lcd_data *lcd_data = file->private_data;
	size_t n = count <= LCD_MAX_BUFFER_LENGTH ? count : LCD_MAX_BUFFER_LENGTH;
	char *data = kmalloc(n + 1, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, n + 1);

	hd44780_lcd_read_data(lcd_data, data, n);

	if (copy_to_user(buf, data, n)) {
		return -EFAULT;
	}

	kfree(data);

	return (ssize_t)n;
}

static int hd44780_lcd_open(struct inode *inode, struct file *file) {
	struct lcd_data *lcd_data = container_of(inode->i_cdev, struct lcd_data, cdev);
	file->private_data = lcd_data;
	if (!lcd_data->initialized) {
		hd44780_lcd_init(lcd_data);
		lcd_data->initialized = true;
	}
	return 0;
}

static struct file_operations fops = {
	.write = &hd44780_lcd_write,
	.open = &hd44780_lcd_open,
	.read = &hd44780_lcd_read,
};
