## Custom FDL1/FDL2 for UMS9117

### Usage

It can't read or write to NAND flash yet.

However, it can dump the boot ROM and read RAM, which the original FDLs can't do.

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

