WITH_LZMADEC = 1
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic -Wno-unused
APPNAME = fphelper

.PHONY: all clean
all: $(APPNAME)

ifeq ($(WITH_LZMADEC), 1)
CFLAGS += -DWITH_LZMADEC=1
$(APPNAME): lzma/LzmaTypes.h lzma/LzmaDecode.h lzma/LzmaDecode.c
endif

clean:
	$(RM) $(APPNAME)

$(APPNAME): main.c
	$(CC) -s $(CFLAGS) -o $@ $< $(LIBS)
