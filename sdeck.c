/*
 *  Stream Deck testing
 *
 *  Copyright (C) 2022 Kaj-Michael Lang
 *  Parts peeked from python libraries
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <hidapi.h>
#include <linux/input.h>
#include <libevdev/libevdev-uinput.h>

#define SETSIG(sa, sig, fun, flags) \
	do { \
		sa.sa_handler = fun; \
		sa.sa_flags = flags; \
		sigemptyset(&sa.sa_mask); \
		sigaction(sig, &sa, NULL); \
	} while(0)

#define MAX_STR 255

#define IMG_SIZE 72

#define POS_SE 2 // PACKET SEQUENCE NUMBER
#define POS_PR 4 // PREVOUS PACKET SEQUENCE NUMBER
#define POS_KI 5 // KEY INDEX

#define IMAGE_1 7749
#define IMAGE_2 7803

static struct sigaction sa_int;
static int sigint_c=0;

static int res;
static hid_device *handle;

static int err;
static struct libevdev *dev;
static struct libevdev_uinput *uidev;

//                       SE    PR KI
char img_header[]={ 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char img_extra_v1[]={ 0x42, 0x4d, 0xf6, 0x3c, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0, 0x28, 0, 0, 0, 0x48, 0, 0, 0, 0x48, 0, 0, 0, 0x01, 0, 0x18, 0, 0, 0, 0, 0, 0xc0, 0x3c, 0, 0, 0x13, 0x0e, 0, 0, 0x13, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char img_buffer[8191];
char img_data[65535];

#define KEY_OFFSET 4
static int keymap[15]={	KEY_VOLUMEDOWN, KEY_VOLUMEUP, KEY_MUTE, KEY_PLAYPAUSE, KEY_NEXTSONG,
			KEY_CUT, KEY_COPY, KEY_PASTE, KEY_UNDO, KEY_PROPS,
			KEY_PAGEUP, KEY_PAGEDOWN, KEY_HOME, KEY_END, KEY_DELETE };

static int keysmax=sizeof(keymap)/sizeof(int);

void sig_handler_sigint(int i)
{
sigint_c++;
}

ssize_t load_img(char *file)
{
int f;
ssize_t r,rl=0;

f=open(file, O_RDONLY);
if (f==-1) {
	perror("Failed to open image!");
	return -1;
}
do {
	r=read(f, img_data+rl, 1024);
	printf("R=%ld %ld\n", r, rl);
	if (r==-1) {
		perror("Failed to read image!");
		break;
	}
	rl+=r;
	if (rl+1024>sizeof(img_data)) {
		r=-1;
		break;
	}
} while (r>0);

close(f);

return r<0 ? r : rl;
}

int set_img(char key, char part)
{
memset(img_buffer, 0, 8191);
img_header[POS_SE]=part;
img_header[POS_PR]=part==1 ? 0 : 1;
img_header[POS_KI]=key+1;
memcpy(img_buffer, img_header, sizeof(img_header));
if (part==1) {
	memcpy(img_buffer+sizeof(img_header), img_extra_v1, sizeof(img_extra_v1));
} else {
	// memcpy(img_buffer+sizeof(img_header), img_extra, sizeof(img_extra));
}

return hid_write(handle, img_buffer, 8191);
}

int reset_img()
{
memset(img_buffer, 0, 8191);
img_buffer[0]=0x02;

return hid_write(handle, img_buffer, 1024);
}

int reset_deck()
{
memset(img_buffer, 0, 32);
img_buffer[0]=0x03;
img_buffer[1]=0x02;

return hid_send_feature_report(handle, img_buffer, 32);
}

int deck_brightness(char percent)
{
memset(img_buffer, 0, 32);
img_buffer[0]=0x03;
img_buffer[1]=0x08;
img_buffer[2]=percent;

return hid_send_feature_report(handle, img_buffer, 32);
}

int deck_set_image(char key, char *img, ssize_t imgsize)
{
int pn=0,len,sent,r;
ssize_t remain;

pn=0;
remain=imgsize;

while (remain>0) {
	len=remain<1016 ? remain : 1016;
	sent=pn*1016;

	img_header[0]=0x02;
	img_header[1]=0x07;
	img_header[2]=key;

	img_header[3]=len==remain ? 1 : 0;

	img_header[4]=len & 0xff;
	img_header[5]=len >> 8;

	img_header[6]=pn & 0xff;
	img_header[7]=pn >> 8;

	memset(img_buffer, 0, 1024);
	memcpy(img_buffer, img_header, 8);
	memcpy(img_buffer+8, img+sent, len);

	r=hid_write(handle, img_buffer, 1024);

	remain=remain-len;
	pn++;
}

return r;
}

void emit_key(unsigned int type, unsigned int code)
{
printf("KEY=%02x\n", code);
libevdev_uinput_write_event(uidev, type, code, 1);
libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
libevdev_uinput_write_event(uidev, type, code, 0);
libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
}

void emit_keys(unsigned int type, ...)
{
va_list ap;
unsigned int code;

va_start(ap, type);

do {
	code=va_arg (ap, int);
	printf("KEYs=%02x\n", code);
	libevdev_uinput_write_event(uidev, type, code, 1);
} while (code>0);

libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);

do {
	code=va_arg (ap, int);
	libevdev_uinput_write_event(uidev, type, code, 0);
} while (code>0);

libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);

va_end(ap);
}

int main(int argc, char* argv[])
{
unsigned char buf[255];
int i, il;

// Initialize the hidapi library
res = hid_init();

// Open the device using the VID, PID,
// and optionally the Serial number.
handle = hid_open(0x0fd9, 0x006d, NULL);

if (!handle) {
	printf("Stream Deck not found\n");
	return 255;
}

res=reset_deck();

sleep(1);

for (i=0;i<15;i++) {
	char btn[16];

	sprintf(btn, "button-%d.jpg", i);
	il=load_img(btn);
	printf("Image loaded: %d\n", il);
	if (il!=-1) {
		deck_set_image(14-i, img_data, il);
	}
	deck_brightness((100/15)*i);
	usleep(150*1000); // demo
}

dev = libevdev_new();
libevdev_set_name(dev, "Stream Deck Uinput");
libevdev_enable_event_type(dev, EV_KEY);

for (i = 0; i < keysmax; i++) {
	libevdev_enable_event_code(dev, EV_KEY, keymap[i], NULL);
}

err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
if (err != 0) {
	printf("uinput fail\n");
}

SETSIG(sa_int, SIGINT, sig_handler_sigint, SA_RESTART);

// Read requested state
printf("Ready\n");
while (sigint_c==0) {
	res=hid_read_timeout(handle, buf, 64, 1500);

	if (res==0)
		continue;

	// Print out the returned buffer.
	for (i = 0; i < 24; i++) {
		printf("%d=%02x, ", i, buf[i]);
	}
	printf("\n");


	for (i = 0; i < keysmax; i++) {
    	if (buf[KEY_OFFSET+i]==1) {
    		emit_key(EV_KEY, keymap[i]);
    	}
    }
}

libevdev_uinput_destroy(uidev);
libevdev_free(dev);

hid_close(handle);
hid_exit();

return 0;
}
