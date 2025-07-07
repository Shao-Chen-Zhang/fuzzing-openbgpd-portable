# Simple Makefile for the test program

CC = cc
CFLAGS = -Wall -g
LDFLAGS = "-L/usr/local/lib"
CPPFLAG = "-I/usr/local/include"

OBJS = simple_bgp.o ../compat/imsg.o ../compat/imsg-buffer.o

all: simple_bgp

simple_bgp: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -I include -c $< -o $@

clean:
	rm -f simple_bgp $(OBJS)
	
