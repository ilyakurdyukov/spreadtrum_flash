## Spreadtrum firmware dumper for Linux

Currently only for feature phones based on the SC6531E/SC6531DA chipset. You can edit the code to work with other Spreadtrum chipsets.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

### Instructions

1. Find any firmware using this search request: `site:androiddatahost.com <chipset> FSPD`.
2. You need to take the firmware loader from the found firmware: `nor_fdl1.bin` and `nor_fdl.bin` for SC6531E, and only `nor_fdl.bin` for SC6531DA.
3. Find the **boot key** for your phone, there can be many different combinations (center, call key, '*', '0', '9'... and even two-key combinations).  
Remove the battery, wait 3-10 seconds (to turn it off completely) and put it back. Two of my SC6531E devices can boot from USB even without a battery.  
If you plug your phone into USB, you should see it connect as `1782:4d00` for a very short time (you can find it in `syslog`), then it will go into charging mode, disconnecting from USB. If you hold the correct boot key (or keys), the wait time before going into charge mode will be much longer and will be visible in the `lsusb` output.
4. Initialize the USB serial driver:
```
$ sudo modprobe ftdi_sio
$ echo 1782 4d00 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```
5. Plug your phone to USB while holding the boot key and run:

SC6531E: `sudo ./spd_dump fdl nor_fdl1.bin 0x40004000 fdl nor_fdl.bin 0x14000000 read_flash 0x80000003 0 0x400000 flash.bin`.

SC6531DA: `sudo ./spd_dump fdl nor_fdl.bin 0x34000000 read_flash 0x80000003 0 0x400000 flash.bin`.

* Instead of finding the boot key (sometimes there's no boot key, as on smart watches with only the power key), it's more convenient to use a boot cable with shorted 4th and 5th pins. This is the same as for OTG adapters, so you can combine an OTG adapter with an AM to AM USB cable.
* If you want to run the tool again then you need to reconnect (also includes battery removal) the phone to the USB.

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


