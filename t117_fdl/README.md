## Custom FDL1/FDL2 for UMS9117

### Usage

It can read raw SPI-NAND flash (2K pages, on-die ECC), but can't write to it yet.

It can also dump the boot ROM and read RAM, which the original FDLs can't do.

Example of using FDL1:
```
sudo ./spd_dump \
	fdl t117_fdl1.bin 0x6200 \
	read_mem 0xffff0000 0x8000 brom.bin
```

Example of using FDL2:
```
sudo ./spd_dump \
	keep_charge 1  fdl orig_fdl1.bin 0x6200 \
	fdl t117_fdl2.bin 0x80100000 \
	read_mem 0xffff0000 0x8000 brom.bin
```

NAND reading is tested **only** on Nokia 105 4G 2nd Edition (2024) with Foresee F35UQA001G (128M SPI-NAND, 2K pages), other flash chips may need different settings or may not work at all.

Reading raw NAND flash (FDL1 or FDL2):
```
sudo ./spd_dump \
	fdl t117_fdl1.bin 0x6200 \
	blk_size 0x800 read_flash 0xf0000000 0 128M nand.bin
```

Reading raw NAND flash with spare data:
```
sudo ./spd_dump \
	fdl t117_fdl1.bin 0x6200 \
	blk_size 0x840 read_flash 0xf0000001 0 0x8400000 nand_oob.bin
```

* `0xf0000000` - raw main area, the offset is `page * 2048`.
* `0xf0000001` - raw pages with the spare area appended, 2112 bytes per page, the offset is `page * 2112`.
* `blk_size` of one page (0x800 or 0x840) gives one request per page, any other size up to 0xff7 also works.
* Each page with an uncorrectable ECC error is reported as a `BSL_REP_LOG` message, the data is still dumped.
* `read_mem` at these two addresses also reads NAND, not memory.

### Build

* To build FDL2, use `FDL=2` option to `make`.

#### with GCC from the old NDK

* GCC has been removed since r18, and hasn't updated since r13. But sometimes it makes the smallest code.

```
NDK=$HOME/android-ndk-r15c
SYSROOT=$NDK/platforms/android-21/arch-arm
TOOLCHAIN=$NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT"
```

#### with Clang from the old NDK

* NDK, SYSROOT, TOOLCHAIN as before.

```
CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang -target armv7-none-linux-androideabi -gcc-toolchain $NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64"

make all TOOLCHAIN="$TOOLCHAIN" SYSROOT="$SYSROOT" CC="$CLANG"
```

#### with Clang from the newer NDK

```
NDK=$HOME/android-ndk-r25b
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm
CLANG=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi21-clang

make all TOOLCHAIN=$TOOLCHAIN CC=$CLANG
```

#### with cross GCC 14.2.0

```
TOOLCHAIN=/usr/bin/arm-linux-gnueabi

make all TOOLCHAIN=$TOOLCHAIN CC=$TOOLCHAIN-gcc-14
```

