<h1>HD44780 LCD Driver for BeagleBone Black (BBB) Rev. C</h1>

> **⚠️DISCLAIMER: THIS CODE IS IN NO WAY PRODUCTION READY! DO NOT USE THIS IN PRODUCTION⚠️**
> 
> I wrote this to learn how to build Linux modules/drivers and how to create new character devices.
> If you do decide to use this, please don't reach out to me for support/complaints/suggestions/requests/insults.
> While I always accept compliments and other forms of flattery, these won't change my mind about the previous
> sentence. That being said, if you want to try out what I built, read on
> 
> **⚠️DO NOT USE THIS IN PRODUCTION!⚠️**

<h2>Table of Contents</h2>
<!-- TOC -->
  * [Intro](#intro)
  * [Building](#building)
    * [Build Requirements](#build-requirements)
    * [Setup](#setup)
      * [RGB Backlight (Optional)](#rgb-backlight-optional)
    * [Build process](#build-process)
  * [Installation](#installation)
    * [Optional steps](#optional-steps)
    * [Required steps](#required-steps)
  * [Usage](#usage)
    * [Caveats (i.e. things to keep in mind)](#caveats-ie-things-to-keep-in-mind)
    * [Writing](#writing)
    * [Reading](#reading)
<!-- TOC -->

## Intro
This is a kernel module/driver that configures a HD44780-based LCD (Specifically [this one from Adafruit][adafruit])
and sets up a single character device (`/dev/lcd0`). You can write to and read from the LCD via the character device. 

## Building
### Build Requirements
- [CMake]
- [xxd] (usually provided by your distro's `vim` package)
- [sed]
- [The ARM GNU Toolchain][armgnu] (I suggest the `gnueabihf` variant)
- The BBB kernel headers for your board's kernel version (`uname -r`). You can either:
  + copy these from your board. Install them by running ```sudo apt install linux-headers-`uname -r` ``` 
    The headers will be installed to ```/lib/modules/`uname -r`/build```; or 
  + cross-compile and build the headers using the scripts provided in [this BBB repo][build_repo]
    (Make sure to use the branch that matches your kernel version)

### Setup
1. Create a copy the sample device tree file.  
    ```.shell
    cp src/lcd-gpio.dts.example src/lcd-gpio.dts
    ```
2. Update the pin mappings or keep the default values. Read the comments in the file for more information.

#### RGB Backlight (Optional)
If your LCD supports RGB backlight coloring, this module supports it. It relies on the BBB's PWM controllers. You can 
enable that by doing the following:
1. Configure your BBB bootloader to load overlays `BB-EHRPWM0-P9_29-P9_31` and `BB-EHRPWM1-P9_14-P9_16`.
   To this, open `/boot/uEnv.txt` in whatever editor you prefer. Find the Overlays section and modify it to load
   the two overlays mentioned (make sure uboot overlays are enabled). e.g.,
   ```ini
   ###U-Boot Overlays###
   ###Master Enable
   enable_uboot_overlays=1
   ###
   ###Overide capes with eeprom
   uboot_overlay_addr0=BB-EHRPWM0-P9_29-P9_31.dtbo
   uboot_overlay_addr1=BB-EHRPWM1-P9_14-P9_16.dtbo
   #uboot_overlay_addr2=<file2>.dtbo
   #uboot_overlay_addr3=<file3>.dtbo
   ```
   BBB provides a third overlay (`BB-EHRPWM2-P8_13-P8_19`) supporting two additional PWM-enabled pins. Since the module
   needs three PWM-enabled pins and since each overlay provides two such pins, you can choose any two overlays to
   enable.
2. Uncomment the relevant block in your device tree file.
3. If you want to update the pins used to control the backlight, update the `pwms` property based on the comments
   provided

### Build process
1. Configure your build. There is one required variable and two optional variables that you can set:
   - `KERNEL_DIR` **(Required)**: The linux headers build directory (e.g., /lib/modules/$(uname -r)/build)
   - `LCD_LINE_COUNT`_(Optional)_: The amount of lines that the LCD can display (Default: 4)
   - `LCD_LINE_LENGTH`_(Optional)_: The number of characters per line that the LCD can display (Default: 20)
   
   To configure your build, run:
   ```shell
   cmake -S . \
         -B build \
         -DKERNEL_DIR=/home/me/bbb-linux-headers
   ```
   If you're using a different variant of the ARM GNU Toolchain, you must add `-DCROSS_COMPILE=your-variant-` to
   your configure command (don't forget the hyphen  `-` at the end)

2. Build the module: 
   ```shell 
   cmake --build build --target driver
   ```
   If everything goes well, you should see your compiled module at `build/module/hd44780_lcd.ko` 

## Installation
### Optional steps
By default, the created character device is only readable and writeable by the root user. To change that, follow
these steps first:

1. Copy `udev-rules/99-hd44780-lcd.rules` to your BBB
2. Navigate to the directory where you copied the rules file and run the following:
    ```shell
    sudo cp 99-hd44780-lcd.rules /etc/udev/rules.d
    sudo udevadm control --reload-rules && sudo udevadm trigger
    ```

If you installed your module prior to these steps, you'll need to remove and reinstall the module after step 2.

### Required steps
1. Copy the compiled module file (`build/module/hd44780_lcd.ko`) to your BBB
2. Navigate to the directory where you copied the module file and run the following:
   ```shell
   sudo modprobe ./hd44780_lcd.ko
   ```
   If everything worked fine, you should now see your new character device at `/dev/lcd0`

## Usage
### Caveats (i.e. things to keep in mind)
- I chose to allow only one process to access the character device at a time. Attempts to open the device while it's
already open will result in `EBUSY`.
- When writing, anything beyond the LCD's max buffer length is silently discarded.
- When reading, the file is always the size of the LCD's max buffer length.

### Writing
To write to the LCD, you can open `/dev/lc0` like a regular file and write to it. e.g.,
```shell
echo -n 'Hello, LCD World!' > /dev/lcd0
dd if=somefile of=/dev/lcd0
```

You can *technically* append data to the device, but it's best-effort. It assumes that the cursor was last left
at the end of the file:
```shell
echo -n 'Append me' >> /dev/lcd0
dd if=somefile of=/dev/lcd0
```

### Reading
To read from the LCD, you can open `/dev/lc0` like a regular file and read from it. e.g.,
```shell
cat /dev/lcd0
head -c10 /dev/lcd0
```

### Special functions
You can control a few aspects of the LCD device by writing a string starting with special control code and a function.
The general syntax is `"<CC><FN>[<arg0><arg1>...<argN>]"`. The control code is `0x88`, and the available functions are:
- `c<RR><GG><BB>`: If RGB is enabled, set the backlight color using RRGGBB hex syntax. For example, to set the backlight
  color to 12AB3C, you would write the string `"\x88c\x12\xAB\x3C"`:
  ```shell
  echo -ne '\x88c\x12\xAB\x3C' > /dev/lcd0
  ```
- `C`: (no arguments) Clears the screen
- `o`: (no arguments) Turns the screen off, preserving both data and color
- `O`: (no arguments) Turns the screen on, preserving both data and color
- `1`: (no arguments) Sets LCD to 1-line display mode. See LCD docs for more info
- `2`: (no arguments) Sets LCD to 2-line display mode. See LCD docs for more info
- `R`: (no arguments) Resets the LCD config. This is useful if you need to disconnect and reconnect your display for
  whatever reason.

[adafruit]: https://adafru.it/499
[CMake]: https://cmake.org/
[xxd]: https://www.linux.org/docs/man1/xxd.html
[sed]: https://www.linux.org/docs/man1/sed.html
[armgnu]: https://gitlab.arm.com/tooling/gnu-toolchains-for-arm
[build_repo]: https://github.com/RobertCNelson/bb-kernel/tree/6.19.13-bone16