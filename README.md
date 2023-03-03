## Spreadtrum firmware dumper

Supported:

1. Feature phones based on the SC6530/SC6531DA/SC6531E chipset. So far, only a flash dump.
2. Smartphones with Spreadtrum/Unisoc chipsets, but only tested on Tiger T310. You can read, write, erase partitions and repartition flash memory.

* You can edit the code to work with other Spreadtrum chipsets.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

### Build

There are two options:

1. Using `libusb` for Linux and Windows (MSYS2):  
Use `make`, `libusb/libusb-dev` packages must be installed.

* For Windows users - please read how to install a [driver](https://github.com/libusb/libusb/wiki/Windows#driver-installation) for `libusb`. Prebuilt `spd_dump` binary is available in [Releases](https://github.com/ilyakurdyukov/spreadtrum_flash/releases).

2. Using the USB serial, **Linux only and doesn't work with smartphones**:  
Use `make LIBUSB=0`.
If you're using this mode, you must initialize the USB serial driver before using the tool (every boot):
```
$ sudo modprobe ftdi_sio
$ echo 1782 4d00 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```

* On Linux you must run the tool with `sudo`, unless you are using special udev rules (see below).
* For both options, you need a custom `nor_fdl1.bin`, see the [custom_fdl](custom_fdl) directory for build instructions. Or download prebuilt one from [Releases](https://github.com/ilyakurdyukov/spreadtrum_flash/releases).

### Instructions (feature phone chipsets)

1. Find the **boot key** for your phone, there can be many different combinations (center, call key, '*', '0', '9'... and even two-key combinations).  
Remove the battery, wait 3-10 seconds (to turn it off completely) and put it back. SC6530 and SC6531E devices can boot from USB without a battery.  
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

SC6530, SC6531DA: `./spd_dump fdl nor_fdl.bin 0x34000000 read_flash 0x80000003 0 0x400000 flash.bin`.

### Instructions (smartphones)

1. You need to find FDL (Firmware Downloader) from original firmware, this is the code that can read/write flash memory. Usually divided into two stages FDL1 and FDL2, a small first stage is needed to initialize external RAM and then loads FDL2 into RAM.  
If you do not have the original firmware for a specific smartphone model, you can take FDL from the firmware of another model, but on the same chipset. Which is safe for feature phones, but not so safe for smartphones, because FDL2 uses `pinmap` init which can vary on different models. Which theoretically can damage the hardware of the smartphone. So there is some risk in this.  
The easiest way to extract the FDL from the .pac firmware is to open it in the "SPD Research Tool" (aka "ResearchDownload"), then look in the temporary directory in ImageFiles, there will be files extracted from the .pac archive. The directory is deleted when you exit the "SPD Research Tool". You need files with FDL in the name, for example `fdl1-sign.bin` and `fdl2-sign.bin`, also take the .xml file (there should be only one).
Find the "FDL" in the XML file, write down the "Base" of the FDLs. For example, for the Tiger T310, the FDL1 base is 0x5500 and the FDL2 base is 0x9EFFFE00.  
It looks like the `uboot` partition (may be named `uboot_a`) is the same binary as FDL2, but with a different signature at the end. So if your phone doesn't verify the signature and you can backup the partition somehow, you can use the `uboot` binary as FDL2.

2. Disconnect your smartphone from the USB cable and use "power off" from Android. Run `spd_dump` (example described below), it will wait for the phone to connect. Then connect your phone to the USB cable while holding the volume down key (could be volume up as boot key).

* If you connect the cable with the phone turned off and without holding the volume button used as the boot key, it will quickly enter charging mode, from which it can't be connected to a computer. Disconnect the cable to exit this mode.

* If you did something wrong and want to turn off your phone, then unplug the cable, remove the battery, and put the battery back in.

* If you see the message "FDL2: incompatible partition" - this is an error code from FDL2 after it has been initialized. This error code is hardcoded and returned unconditionally. When official flashing tools see this message, they force you to do repartition to use any partition write feature.

#### Example for Android smartphones

Example for the Tiger T310 chipset:

* You must use the correct FDL base addresses taken from the chipset specific .xml file.

```
./spd_dump --verbose 0 --wait 300 \
	keep_charge 1 \
	fdl fdl1-sign.bin 0x5500 \
	fdl fdl2-sign.bin 0x9efffe00 \
	partition_list partition.xml \
	blk_size 0x3000 \
	read_part logo 0 8M logo.bmp \
	power_off
```

Here's the explanation:

`keep_charge 1` - to keep charging while FDL is active.  
`fdl <fdl-image> <base>` - loads the FDL into the phone's memory at the specified address and executes the code.  
`partition_list <partition.xml>` - saves the current partition list in .xml format. Also prints as text.  
`blk_size <size>` - changes the default block size for read/write commands, may speed up the process (but some FDL may not support too large sizes).  
`read_part <partition_name> <offset> <size> <output_file>` - dumps the specified partition at the selected offset.
`power_off` - will turn off the phone after you unplug the cable, after which you can run `spd_dump` again. If it didn't end with a power off, then you need to remove the battery and put it back.  

Other commands:

`write_part <partition_name> <input_file>` - rewrites the specified partition (if it's larger than the file, then the rest is undefined, usually unchanged), specifying an offset is not available in the protocol (destructive operation - asks for confirmation).  
`erase_part <partition_name>` - erases (clears) the specified partition (destructive operation - asks for confirmation).  
`repartition <partition.xml>` - changes flash partitioning (destructive operation - asks for confirmation).  

* Partitions with the same name plus `_a` or `_b` are the same, `_b` is the backup copy.

#### Special partition names

There are some special names not listed in the partition list:

`user_partition` - raw access to the whole flash memory, ignoring partitions.  
`splloader`, `spl_loader_bak` - bootloader similar to FDL1.  
`uboot` - just an alias for the `uboot` partition, but if it's missing (because named `uboot_a`), then `splloader` is read instead.  

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

1. [SPD Flash Tool source code](https://spdflashtool.com/source/spd-tool-source-code)
2. [RDA memory dumper](https://github.com/ihewitt/ivrtrack/blob/main/util/dump.c)
3. [sharkalaka - FDL1/FDL2 loader (in Python)](https://github.com/fxsheep/sharkalaka)
4. [uwpflash (in C)](https://github.com/Mani-Sadhasivam/uwpflash)
5. [Opus Spreadtrum (protocol explanation)](https://chronovir.us/2021/12/18/Opus-Spreadtrum/)
6. [uniflash - from the author of Opus Spreadtrum (in Python)](https://gitlab.com/suborg/uniflash)
7. [sprdproto - another tool (in C)](https://github.com/kagaimiq/sprdproto)
8. [bzpwork - tool for packing/unpacking Spreadtrum firmware (in C)](https://github.com/ilyazx/bzpwork)
9. [unisoc_dloader - another tool (in C)](https://github.com/amitv87/unisoc_dloader)

* I only found "Opus Spreadtrum" after I wrote this tool using information from other source code. So now there is another dump tool, but written in C.
* Also I have the [tool](https://github.com/ilyakurdyukov/mediatek_flash) for MediaTek chipsets.

