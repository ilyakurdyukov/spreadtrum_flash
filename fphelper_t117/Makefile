CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic
APPNAME = fphelper_t117

.PHONY: all clean
all: $(APPNAME)

clean:
	$(RM) $(APPNAME)

$(APPNAME): main.c
	$(CC) -s $(CFLAGS) -o $@ $< $(LIBS)

fwcrypto: LIBS += -lcrypto
fwcrypto: fwcrypto.c
	$(CC) -s $(CFLAGS) -o $@ $< $(LIBS)
