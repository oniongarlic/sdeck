CC=gcc
CFLAGS=-O2 -g
CFLAGS+=$(shell pkg-config --cflags libusb-1.0 hidapi-libusb libevdev)
LIBS=$(shell pkg-config --libs libusb-1.0 hidapi-libusb libevdev)

all: sdeck

sdeck.o: sdeck.c

sdeck: sdeck.o
	$(CC) -o sdeck sdeck.o $(LIBS)
