
LIBUSB = 1
CFLAGS = -std=c99
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)
APPNAME = spd_dump

ifeq ($(LIBUSB), 1)
LIBS = -lusb-1.0
endif

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): $(APPNAME).c spd_cmd.h
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)
