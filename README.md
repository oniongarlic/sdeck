# Stream Deck thingy in C

Just a quick and dirty proof-of-concept right now.

Requires: libusb-1.0 hidapi-libusb libevdev

A 360x216 grid of 72x72 images can be used as base image for buttons.
 convert base.png -crop 72x72 -scene 0 button-%d.jpg
