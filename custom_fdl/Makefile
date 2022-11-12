# 0 - detect, 1 - SC6531E, 2 - SC6531DA
CHIP = 0
OBJDIR = obj$(CHIP)
SRCS = start entry common usbio packet channel main
OBJS = $(SRCS:%=$(OBJDIR)/%.o)
LDSCRIPT = sc6531e_fdl1.ld
NAME = nor_fdl1

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

CFLAGS += -Wall -fno-PIE -ffreestanding -march=armv5te -mthumb $(EXTRA_CFLAGS) -fno-strict-aliasing
LFLAGS = -nostartfiles -nodefaultlibs -nostdlib -Wl,-T,$(LDSCRIPT) -Wl,--gc-sections -Wl,-z,notext
CFLAGS += -DCHIP=$(CHIP)

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
	$(OBJCOPY) -O binary -R .bss $< $@

