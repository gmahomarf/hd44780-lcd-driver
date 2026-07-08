#include "hd44780_lcd-cdev.h"
#include "hd44780_lcd-ctrl.h"
#include "hd44780_lcd-common.h"
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/semaphore.h>

int major;
const int minor = 0;
const int no_devs = 1;
static struct device *lcd_dev;
static struct class *lcd_class;
static struct file_operations fops;

static ssize_t handle_write(struct lcd_data *lcd_data, const char *data, size_t count);
static ssize_t handle_control_code(struct lcd_data *lcd_data, const char *data, size_t count);

DEFINE_SEMAPHORE(sem_read, 0);
DEFINE_MUTEX(mut_read);

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

// File Operations

static int hd44780_lcd_open(struct inode *inode, struct file *file) {
	struct lcd_data *lcd_data = container_of(inode->i_cdev, struct lcd_data, cdev);
	file->private_data = lcd_data;
	if (!lcd_data->initialized) {
		hd44780_lcd_init(lcd_data);
		lcd_data->initialized = true;
	}
	return 0;
}

static ssize_t hd44780_lcd_write(struct file *file, const char __user *buf, const size_t count, loff_t *off) {
	struct lcd_data *lcd_data = file->private_data;
	ssize_t written;
	char *data = kmalloc(count + 1, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, count + 1);
	if (copy_from_user(data, buf, count)) {
		return -EFAULT;
	}

	LCD_DEBUG("Want to write %d bytes at offset %lld\n", count, *off);

	if (*off == 0) {
		switch (data[0]) {
		case 0x88:
			written = handle_control_code(lcd_data, data, count);
			break;
		default: {
			written = handle_write(lcd_data, data, count);
		}
		}
	} else {
		written = handle_write(lcd_data, data, count);
	}

	kfree(data);

	return written;
}

static ssize_t hd44780_lcd_read(struct file *file, char __user *buf, size_t count, loff_t *off) {
	const struct lcd_data *lcd_data = file->private_data;
	size_t n = count <= LCD_BUFFER_LENGTH ? count : LCD_BUFFER_LENGTH;
	char *data = kmalloc(n + 1, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, n + 1);

	LCD_DEBUG("Want to read %d bytes at offset %lld\n", count, *off);

	hd44780_lcd_read_data(lcd_data, data, n);

	if (copy_to_user(buf, data, n)) {
		return -EFAULT;
	}

	kfree(data);

	return (ssize_t)n;
}

static loff_t hd44780_lcd_seek(struct file *file, const loff_t offset, const int whence) {
	mutex_lock(&file->f_pos_lock);
	struct lcd_data *lcd_data = file->private_data;

	LCD_DEBUG("Seeking to %lld (whence %d)\n", offset, whence);

	if (whence == SEEK_SET) {
		lcd_data->pos = offset > LCD_BUFFER_LENGTH ? LCD_BUFFER_LENGTH : offset;
	} else if (whence == SEEK_CUR) {
		lcd_data->pos = lcd_data->pos + offset > LCD_BUFFER_LENGTH ? LCD_BUFFER_LENGTH : offset + lcd_data->pos;
	}

	hd44780_lcd_set_position(lcd_data, lcd_data->pos);

	mutex_unlock(&file->f_pos_lock);

	return lcd_data->pos;
}

static struct file_operations fops = {
	.write = &hd44780_lcd_write,
	.open = &hd44780_lcd_open,
	.read = &hd44780_lcd_read,
	.llseek = &hd44780_lcd_seek,
};

// Private functions
static ssize_t handle_control_code(struct lcd_data *lcd_data, const char *data, const size_t count) {
	if (count < 2) {
		return -EINVAL;
	}
	char control_code = data[1];
	switch (control_code) {
	case 'c': {
		// Color
		// 0x88, c, R, G, B
		if (count != 5) {
			return -EINVAL;
		}
		LCD_DEBUG("Setting color to #%02X%02X%02X\n", data[2], data[3], data[4]);
		hd44780_set_backlight_color(lcd_data, data[2], data[3], data[4]);
		break;
	}
	case 'C': {
		// Clear
		if (count != 2) {
			return -EINVAL;
		}
		LCD_INFO("Clearing lcd...\n");
		hd44780_lcd_clear(lcd_data);
		lcd_data->pos = 0;
		break;
	}
	case 'O': {
		// On
		if (count != 2) {
			return -EINVAL;
		}
		LCD_INFO("Turning lcd on...\n");
		hd44780_lcd_turn_on(lcd_data);
		break;
	}
	case 'o': {
		// Off
		if (count != 2) {
			return -EINVAL;
		}
		LCD_INFO("Turning lcd off...\n");
		hd44780_lcd_turn_off(lcd_data);
		break;
	}
	default:
		return -EINVAL;
	}

	return (ssize_t)count;
}

static ssize_t handle_write(struct lcd_data *lcd_data, const char *data, const size_t count) {
	uint pos = lcd_data->pos;
	uint written = 0;

	uint line = pos / LCD_LINE_LENGTH;
	uint col = pos % LCD_LINE_LENGTH;
	while (written < count) {
		const int remaining = LCD_LINE_LENGTH - col;
		const uint to_write = count - written;
		const uint c = remaining > to_write ? to_write : remaining;

		if (!to_write) {
			break;
		}
		if (remaining) {
			hd44780_lcd_write_data(lcd_data, data + written, c);
			pos += c;
			written += c;
			col += c;
		} else if (line < LCD_LINE_COUNT - 1) {
			hd44780_lcd_set_coords(lcd_data, ++line, col = 0);
		}
		if (col >= LCD_LINE_LENGTH) {
			if (line < LCD_LINE_COUNT - 1) {
				hd44780_lcd_set_coords(lcd_data, ++line, col = 0);
			} else {
				hd44780_lcd_set_coords(lcd_data, line = 0, col = pos = 0);
			}
		}
	}

	lcd_data->pos = pos >= LCD_BUFFER_LENGTH ? 0 : pos;
	LCD_DEBUG("Total bytes written: %u bytes. Final pos: %u\n", written, lcd_data->pos);

	return (ssize_t)count;
}