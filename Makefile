CC := gcc
CFLAGS := -std=gnu99 -Os -Wall
LDFLAGS := -lxcb -s
SRC := xselq.c

xselq: ${SRC}
	${CC} -o $@ ${SRC} ${CFLAGS} ${LDFLAGS}
	
