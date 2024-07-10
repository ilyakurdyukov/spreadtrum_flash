CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic -Wno-unused
APPNAME = fphelper

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): main.c
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)
