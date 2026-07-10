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

struct file_private_data {
	bool truncate;
	struct lcd_data *lcd_data;
};

static ssize_t handle_read(struct lcd_data *lcd_data, char *data, size_t count, loff_t *off);
static ssize_t handle_write(struct lcd_data *lcd_data, const char *data, size_t count, loff_t *off);
static ssize_t handle_control_code(struct lcd_data *lcd_data, const char *data, size_t count);

static DEFINE_MUTEX(lcd_lock);

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
		dev_warn(dev, "Could not add character device: %d\n", res);
		error = res;
		goto cleanup;
	}

	dev_dbg(dev, "character device added\n");

	lcd_class = class_create(LCD_CLASS_NAME);
	if (IS_ERR(lcd_class)) {
		error = PTR_ERR(lcd_class);
		dev_err(dev, "Could not create class: %d\n", error);
		cdev_del(&lcd_data->cdev);
		goto cleanup;
	}

	dev_dbg(dev, "lcd class created: %s\n", lcd_class->name);

	lcd_dev = device_create(lcd_class, NULL, dev_no, lcd_data, LCD_DEVICE_NAME);
	if (!lcd_data) {
		dev_warn(dev, "lcd_data is null\n");
	}
	if (IS_ERR(lcd_dev)) {
		error = PTR_ERR(lcd_dev);
		dev_err(dev, "Could not create device: %d\n", error);
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
	if (!mutex_trylock(&lcd_lock)) {
		return -EBUSY;
	};

	struct file_private_data *private_data = kmalloc(sizeof(struct file_private_data), GFP_KERNEL);
	if (!private_data) {
		mutex_unlock(&lcd_lock);
		return -ENOMEM;
	}

	struct lcd_data *lcd_data = container_of(inode->i_cdev, struct lcd_data, cdev);
	lcd_data->pos = 0;

	if (file->f_flags & O_TRUNC) {
		LCD_DEBUG("Truncating LCD...\n");
		private_data->truncate = true;
	} else if (file->f_flags & O_WRONLY && file->f_flags & O_APPEND) {
		lcd_data->pos = file->f_pos = hd44780_lcd_get_position(lcd_data);
		LCD_DEBUG("Appending to LCD at pos %u...\n", lcd_data->pos);
	} else if (file->f_flags == O_RDONLY) {
		LCD_DEBUG("Reading from LCD...\n");
		hd44780_lcd_set_position(lcd_data, 0);
	} else if (file->f_flags & O_RDWR) {
		LCD_DEBUG("R/W LCD...\n");
		hd44780_lcd_set_position(lcd_data, 0);
	} else {
		LCD_DEBUG("?? LCD... 0x%08X (%o)\n", file->f_flags, file->f_flags);
		hd44780_lcd_set_position(lcd_data, 0);
	}

	private_data->truncate = false;
	private_data->lcd_data = lcd_data;
	file->private_data = private_data;
	return 0;
}

static int hd44780_lcd_release(struct inode *inode, struct file *file) {
	struct file_private_data *private_data = file->private_data;
	kfree(private_data);
	file->private_data = NULL;
	mutex_unlock(&lcd_lock);
	return 0;
}

static ssize_t hd44780_lcd_write(struct file *file, const char __user *buf, const size_t count, loff_t *off) {
	struct file_private_data *private_data = file->private_data;
	struct lcd_data *lcd_data = private_data->lcd_data;
	ssize_t written;

	if (*off >= LCD_BUFFER_LENGTH) {
		return (ssize_t)count;
	}

	LCD_DEBUG("Writing %d bytes at pos %llu\n", count, *off);

	char *data = kmalloc(count + 1, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, count + 1);
	if (copy_from_user(data, buf, count)) {
		return -EFAULT;
	}

	if (*off == 0 && data[0] == 0x88) {
		written = handle_control_code(lcd_data, data, count);
	} else {
		if (private_data->truncate) {
			private_data->truncate = false;
			hd44780_lcd_clear(lcd_data);
			*off = lcd_data->pos = 0;
		}
		written = handle_write(lcd_data, data, count, off);
	}

	kfree(data);

	return written;
}

static ssize_t hd44780_lcd_read(struct file *file, char __user *buf, size_t count, loff_t *off) {
	struct file_private_data *private_data = file->private_data;

	LCD_DEBUG("Want to read %u bytes at pos %llu\n", count, *off);

	if (*off >= LCD_BUFFER_LENGTH) {
		// EOF
		return 0;
	}

	struct lcd_data *lcd_data = private_data->lcd_data;

	// No point in allocating more than `LCD_BUFFER_LENGTH` bytes, since we can't read more than that
	const size_t buf_size = count <= LCD_BUFFER_LENGTH ? count : LCD_BUFFER_LENGTH;
	char *data = kmalloc(buf_size, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	memset(data, 0, buf_size);

	const ssize_t written = handle_read(lcd_data, data, buf_size, off);

	if (copy_to_user(buf, data, buf_size)) {
		return -EFAULT;
	}

	kfree(data);

	return written;
}

static loff_t hd44780_lcd_seek(struct file *file, const loff_t offset, const int whence) {
	loff_t pos = file->f_pos;
	struct file_private_data *private_data = file->private_data;
	struct lcd_data *lcd_data = private_data->lcd_data;

	if (whence == SEEK_SET) {
		pos = offset;
	} else if (whence == SEEK_CUR) {
		pos = file->f_pos + offset;
	} else if (whence == SEEK_END) {
		pos = hd44780_lcd_get_position(lcd_data) + offset;
	}

	hd44780_lcd_set_position(lcd_data, pos);

	LCD_DEBUG("Sought to %llu (%s)\n", pos,
		whence == SEEK_SET ? "SEEK_SET" :
		whence == SEEK_CUR ? "SEEK_CUR" :
							 "SEEK_END");

	return lcd_data->pos = file->f_pos = pos;
}

static struct file_operations fops = {
	.write = &hd44780_lcd_write,
	.open = &hd44780_lcd_open,
	.release = &hd44780_lcd_release,
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
		lcd_data->color = data[2] << 16 | data[3] << 8 | data[4];
		break;
	}
	case 'C': {
		// Clear
		if (count != 2) {
			return -EINVAL;
		}
		LCD_DEBUG("Clearing lcd...\n");
		hd44780_lcd_clear(lcd_data);
		// lcd_data->pos = 0;
		break;
	}
	case 'O': {
		// On
		if (count != 2) {
			return -EINVAL;
		}
		LCD_DEBUG("Turning lcd on...\n");
		hd44780_lcd_turn_on(lcd_data);
		hd44780_set_backlight_color(
			lcd_data, (lcd_data->color & 0xFF0000) >> 16, (lcd_data->color & 0xFF00) >> 8, lcd_data->color & 0xFF);
		break;
	}
	case 'o': {
		// Off
		if (count != 2) {
			return -EINVAL;
		}
		LCD_DEBUG("Turning lcd off...\n");
		hd44780_lcd_turn_off(lcd_data);
		hd44780_set_backlight_color(lcd_data, 0, 0, 0);
		break;
	}
	case '1': {
		// One line
		if (count != 2) {
			return -EINVAL;
		}
		LCD_DEBUG("Setting 1 line...\n");
		hd44780_lcd_set_lines(lcd_data, 1);
		break;
	}
	case '2': {
		// Two lines
		if (count != 2) {
			return -EINVAL;
		}
		LCD_DEBUG("Setting 2 lines...\n");
		hd44780_lcd_set_lines(lcd_data, 2);
		break;
	}
	case 'R': {
		// Reset
		if (count != 2) {
			return -EINVAL;
		}
		LCD_DEBUG("Resetting LCD...\n");
		hd44780_lcd_init(lcd_data);
		break;
	}
	default:
		return -EINVAL;
	}

	// We want control codes to be the only thing written
	return (ssize_t)count;
}

static ssize_t handle_write(struct lcd_data *lcd_data, const char *data, const size_t count, loff_t *off) {
	if (*off != lcd_data->pos) {
		LCD_DEBUG("Offset and position are not the same. pos: %d; off: %llu\n", lcd_data->pos, *off);
		hd44780_lcd_set_position(lcd_data, *off);
	}

	uint pos = *off;
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
			written += c;
			col += c;
		} else if (line < LCD_LINE_COUNT - 1) {
			hd44780_lcd_set_coords(lcd_data, ++line, col = 0);
		}
		if (col >= LCD_LINE_LENGTH) {
			if (line < LCD_LINE_COUNT - 1) {
				hd44780_lcd_set_coords(lcd_data, ++line, col = 0);
			} else {
				// Don't write beyond the end of the buffer, but move the cursor to the beginning
				hd44780_lcd_set_coords(lcd_data, 0, 0);
				break;
			}
		}
	}

	*off += written;
	lcd_data->pos += written;

	return (ssize_t)written;
}

static ssize_t handle_read(struct lcd_data *lcd_data, char *data, const size_t count, loff_t *off) {
	uint pos = *off;
	uint read = 0;

	uint line = pos / LCD_LINE_LENGTH;
	uint col = pos % LCD_LINE_LENGTH;
	while (read < count) {
		const int remaining = LCD_LINE_LENGTH - col;
		const uint to_read = count - read;
		const uint c = remaining > to_read ? to_read : remaining;

		if (!to_read) {
			break;
		}
		if (remaining) {
			hd44780_lcd_read_data(lcd_data, data + read, c);
			read += c;
			col += c;
		} else if (line < LCD_LINE_COUNT - 1) {
			hd44780_lcd_set_coords(lcd_data, ++line, col = 0);
		}
		if (col >= LCD_LINE_LENGTH) {
			if (line < LCD_LINE_COUNT - 1) {
				hd44780_lcd_set_coords(lcd_data, ++line, col = 0);
			} else {
				// Don't read beyond the end of the buffer, but move the cursor to the beginning
				hd44780_lcd_set_coords(lcd_data, 0, 0);
				break;
			}
		}
	}

	*off += read;
	lcd_data->pos += read;

	return (ssize_t)read;
}