## Spreadtrum firmware dumper for Linux

Currently only for feature phones based on the SC6531E chipset. You can edit the code to work with other Spreadtrum chipsets.

### Instructions

1. Find any firmware using this search request: `site:androiddatahost.com SC6531E FSPD`.
2. You need to extract two small files from the found firmware: `nor_fdl1.bin` and `nor_fdl.bin`.
3. Find the **boot key** for your phone, there can be many different combinations (center, call key, '*', '0', '9'... and even two-key combinations).  
Other instructions say that before connecting you must remove the battery and put it back, but it works for me even without a battery.  
If you plug your phone into USB, you should see it connect as `1782:4d00` for a very short time (you can find it in `syslog`), then it will go into charging mode, disconnecting from USB. If you hold the correct boot key (or keys), the wait time before going into charge mode will be much longer and will be visible in the `lsusb` output.
4. Initialize the USB serial driver:
```
$ sudo modprobe ftdi_sio
$ echo 1782 4d00 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```
5. Plug your phone to USB while holding the boot key and run `sudo ./spd_dump`.

* Using a boot cable may work the same as the boot key, but I haven't tested this.
* If you want to run the tool again then you need to reconnect the phone to the USB.

### Useful links

1. [SPD Flash Tool source code](https://spflashtools.com/category/source)
2. [RDA memory dumper](https://github.com/ihewitt/ivrtrack/blob/main/util/dump.c)
3. [sharkalaka - FDL1/FDL2 loader in Python](https://github.com/fxsheep/sharkalaka)
4. [uwpflash - Unisoc flasher for Linux](https://github.com/Mani-Sadhasivam/uwpflash)
5. [Opus Spreadtrum (protocol explanation)](https://chronovir.us/2021/12/18/Opus-Spreadtrum/)
6. [uniflash - from the author of Opus Spreadtrum (in Python)](https://gitlab.com/suborg/uniflash)
7. [sprdproto - another tool (in C)](https://github.com/kagaimiq/sprdproto)

* I only found "Opus Spreadtrum" after I wrote this tool using information from other source code. So now there is another dump tool, but written in C.

