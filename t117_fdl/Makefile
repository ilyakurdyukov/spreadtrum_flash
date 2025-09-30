# 0 - detect
CHIP = 0
OBJDIR = obj$(CHIP)
SRCS = start entry common usbio packet channel main sdram_init
OBJS = $(SRCS:%=$(OBJDIR)/%.o)
FDL = 1
LDSCRIPT = ums9117_fdl$(FDL).ld
NAME = t117_fdl$(FDL)

ifdef TOOLCHAIN
CC = "$(TOOLCHAIN)"-gcc
OBJCOPY = "$(TOOLCHAIN)"-objcopy
endif

COMPILER = $(findstring clang,$(notdir $(CC)))
ifeq ($(COMPILER), clang)
# Clang
CFLAGS = -Oz
else
# GCC
CFLAGS = -Os
endif

CFLAGS += -Wall -funsigned-char
CFLAGS += -fno-PIE -ffreestanding -mcpu=cortex-a7 -mthumb $(EXTRA_CFLAGS) -fno-strict-aliasing
CFLAGS += -fomit-frame-pointer
CFLAGS += -ffunction-sections -fdata-sections
LFLAGS = -static -nostartfiles -nodefaultlibs -nostdlib -Wl,-T,$(LDSCRIPT) -Wl,--gc-sections -Wl,-z,notext
LFLAGS += -mcpu=cortex-a7 # required for GCC + LTO
CFLAGS += -DCHIP=$(CHIP)
ifeq ($(FDL), 2)
CFLAGS += -DFDL2=1
endif
# CFLAGS += -DSDRAM_INIT=0

# Clang's LTO doesn't work with the GCC toolchain
ifeq ($(findstring -gcc-toolchain,$(notdir $(CC))),)
CFLAGS += -flto
endif

ifdef SYSROOT
CFLAGS += --sysroot="$(SYSROOT)"
endif

.PHONY: all clean

all: $(NAME).bin

clean:
	$(RM) -r $(OBJDIR) $(NAME).bin

$(OBJDIR):
	mkdir -p $(OBJDIR)

-include $(OBJS:.o=.d)

%.asm: %.c
	$(CC) $(CFLAGS) $< -S -o $@

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -MF $(@:.o=.d) $< -c -o $@

$(OBJDIR)/%.o: %.s | $(OBJDIR)
	$(CC) $< -c -o $@

$(OBJDIR)/%.o: %.S | $(OBJDIR)
	$(CC) $< -c -o $@

$(OBJDIR)/$(NAME).elf: $(OBJS)
	$(CC) $(LFLAGS) $(OBJS) -o $@

%.bin: $(OBJDIR)/%.elf
	$(OBJCOPY) -O binary -j .text $< $@

