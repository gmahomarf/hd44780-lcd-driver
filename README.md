# HD44780 LCD Driver for BeagleBone Black (BBB) Rev. C

> **⚠️DISCLAIMER: THIS CODE IS IN NO WAY PRODUCTION READY! DO NOT USE THIS IN PRODUCTION⚠️**
> 
> I wrote this to learn how to build Linux modules/drivers and how to create new character devices.
> If you do decide to use this, please don't reach out to me for support/complaints/suggestions/requests/insults.
> While I always accept compliments and other forms of flattery, these won't change my mind about the previous
> sentence. That being said, if you want to try out what I built, read on
> 
> **⚠️DO NOT USE THIS IN PRODUCTION!⚠️**

## Intro
This is a kernel module/driver that configures a HD44780-based LCD (Specifically [this one from Adafruit][ada])
and sets up a single character device (`/dev/lcd0`). You can write to and read from the LCD via the character device. 

## Building
### Requirements
- [CMake]
- [xxd] (usually provided by your distro's `vim` package)
- [The ARM GNU Toolchain][armgnu] (I suggest the `gnueabihf` variant)
- The BBB kernel headers for your board's kernel version (`uname -r`). You can either:
  + copy these from your board. Install them by running ```sudo apt install linux-headers-`uname -r` ``` 
    The headers will be installed to ```/lib/modules/`uname -r`/build```; or 
  + cross-compile and build the headers using the scripts provided in [this BBB repo][build_repo]
    (Make sure to use the branch that matches your kernel version)

## Build process
1. Create a copy the sample device tree file.  
    ```.shell
    cp src/lcd-gpio.dts.example src/lcd-gpio.dts
    ```
   Update the pin mappings or keep the default values. Read the comments in the file for more information.

2. Configure your build:
    ```shell
    cmake -S . \
         -B build \
         -DKERNEL_DIR=/home/me/bbb-linux-headers
    ```
    If you're using a different variant of the ARM GNU Toolchain, make sure to add `-DCROSS_COMPILE=your-variant-`
   (don't forget the hyphen  `-` at the end)

3. Build the module: 
   ```shell 
   cmake --build build --target driver
   ```
   If everything goes as planned, you should see your compiled module at `build/module/hd44780_lcd.ko` 

## Installation
### Optional steps
By default, the character device `/dev/lcd0` is only readable and writeable by the root user. To change that, follow
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




[ada]: https://adafru.it/499
[CMake]: https://cmake.org/
[xxd]: https://www.linux.org/docs/man1/xxd.html
[armgnu]: https://gitlab.arm.com/tooling/gnu-toolchains-for-arm
[build_repo]: https://github.com/RobertCNelson/bb-kernel/tree/6.19.13-bone16
https://debian.beagle.cc/debian-trixie-armhf/pool/main/l/linux-upstream/