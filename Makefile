all: shex

ifdef CC
CC=gcc
endif

shex: shex.c
	$(CC) -D_FILE_OFFSET_BITS=64 -o shex shex.c

clean:
	rm -f shex

install: shex
	cp shex /usr/local/bin/shex

