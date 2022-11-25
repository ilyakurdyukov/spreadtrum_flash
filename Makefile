
LIBUSB = 0
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic -Wno-unused
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)

ifeq ($(LIBUSB), 1)
LIBS = -lusb-1.0
endif

.PHONY: all clean
all: spd_dump

clean:
	$(RM) spd_dump

spd_dump: spd_dump.c spd_cmd.h
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)
