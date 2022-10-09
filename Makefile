all: spd_dump

spd_dump: spd_dump.c spd_cmd.h
	$(CC) -s -O2 -Wall -Wextra -Wno-unused -o $@ $^
