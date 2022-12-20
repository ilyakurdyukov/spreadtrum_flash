## Spreadtrum firmware dumper

Currently only for feature phones based on the SC6531E/SC6531DA chipset. You can edit the code to work with other Spreadtrum chipsets.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

### Build

There are two options:

1. Using `libusb` for Linux and Windows (MSYS2):  
Use `make`, `libusb/libusb-dev` packages must be installed.

* For Windows users - please read how to install a [driver](https://github.com/libusb/libusb/wiki/Windows#driver-installation) for `libusb`. Prebuilt `spd_dump` binary is available in [Releases](https://github.com/ilyakurdyukov/spreadtrum_flash/releases).

2. Using the USB serial, Linux only:  
Use `make LIBUSB=0`.
If you're using this mode, you must initialize the USB serial driver before using the tool (every boot):
```
$ sudo modprobe ftdi_sio
$ echo 1782 4d00 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```

* On Linux you must run the tool with `sudo`, unless you are using special udev rules (see below).
* For both options, you need a custom `nor_fdl1.bin`, see the [custom_fdl](custom_fdl) directory for build instructions. Or download prebuilt one from [Releases](https://github.com/ilyakurdyukov/spreadtrum_flash/releases).

### Instructions

1. Find the **boot key** for your phone, there can be many different combinations (center, call key, '*', '0', '9'... and even two-key combinations).  
Remove the battery, wait 3-10 seconds (to turn it off completely) and put it back. SC6531E devices can boot from USB without a battery.  
If you plug your phone into USB, you should see it connect as `1782:4d00` for a very short time (you can find it in `syslog`), then it will go into charging mode, disconnecting from USB. If you hold the correct boot key (or keys), the wait time before going into charge mode will be much longer and will be visible in the `lsusb` output.

* For SC6531DA you must hold the **boot key** while inserting the battery. You can connect the USB cable before or after that, no need to hold the **boot key** while connecting the cable.

2. Run the tool on your PC:
`./spd_dump fdl nor_fdl1.bin 0x40004000 read_flash 0x80000003 0 0x400000 flash.bin`  
Then plug your phone to USB while holding the **boot key**.
This will save the first 4 MB of the firmware (the most common size).

* You can increase the timeout using the `--wait` option, eg. `spd_dump --wait 300 <commands>`
* Instead of finding the boot key (sometimes there's no boot key, as on smart watches with only the power key), it's more convenient to use a boot cable with shorted 4th and 5th pins. This is the same as for OTG adapters, so you can combine an OTG adapter with an AM to AM USB cable.
* If you want to run the tool again then you need to reconnect (also includes battery removal) the phone to the USB.

#### Use with FDL from original firmware

SC6531E: `./spd_dump fdl nor_fdl1.bin 0x40004000 fdl nor_fdl.bin 0x14000000 read_flash 0x80000003 0 0x400000 flash.bin`.

SC6531DA: `./spd_dump fdl nor_fdl.bin 0x34000000 read_flash 0x80000003 0 0x400000 flash.bin`.

### Using the tool on Linux without sudo

If you create `/etc/udev/rules.d/80-spd-mtk.rules` with these lines:
```
# Spreadtrum
SUBSYSTEMS=="usb", ATTRS{idVendor}=="1782", ATTRS{idProduct}=="4d00", MODE="0666", TAG+="uaccess"
# MediaTek
SUBSYSTEMS=="usb", ATTRS{idVendor}=="0e8d", ATTRS{idProduct}=="0003", MODE="0666", TAG+="uaccess"
```
...then you can run `spd_dump` without root privileges.

* As you can see this file for both Spreadtrum and MediaTek chipsets.

### Useful links

1. [SPD Flash Tool source code](https://spflashtools.com/category/source)
2. [RDA memory dumper](https://github.com/ihewitt/ivrtrack/blob/main/util/dump.c)
3. [sharkalaka - FDL1/FDL2 loader in Python](https://github.com/fxsheep/sharkalaka)
4. [uwpflash - Unisoc flasher for Linux](https://github.com/Mani-Sadhasivam/uwpflash)
5. [Opus Spreadtrum (protocol explanation)](https://chronovir.us/2021/12/18/Opus-Spreadtrum/)
6. [uniflash - from the author of Opus Spreadtrum (in Python)](https://gitlab.com/suborg/uniflash)
7. [sprdproto - another tool (in C)](https://github.com/kagaimiq/sprdproto)
8. [bzpwork - tool for packing/unpacking Spreadtrum firmware (in C)](https://github.com/ilyazx/bzpwork)
9. [unisoc_dloader - another tool (in C)](https://github.com/amitv87/unisoc_dloader)

* I only found "Opus Spreadtrum" after I wrote this tool using information from other source code. So now there is another dump tool, but written in C.
* Also I have the [tool](https://github.com/ilyakurdyukov/mediatek_flash) for MediaTek chipsets.

