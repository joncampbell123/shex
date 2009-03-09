all: shex

shex: shex.c
	gcc -D_FILE_OFFSET_BITS=64 -o shex shex.c

install: shex
	cp shex /usr/local/bin/shex

