#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <hidapi.h>

#define MAX_STR 255

#define IMG_SIZE 72

#define POS_SE 2 // PACKET SEQUENCE NUMBER
#define POS_PR 4 // PREVOUS PACKET SEQUENCE NUMBER
#define POS_KI 5 // KEY INDEX

#define IMAGE_1 7749
#define IMAGE_2 7803

int res;
hid_device *handle;

//                       SE    PR KI
char img_header[]={ 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
char img_extra_v1[]={ 0x42, 0x4d, 0xf6, 0x3c, 0, 0, 0, 0, 0, 0, 0x36, 0, 0, 0, 0x28, 0, 0, 0, 0x48, 0, 0, 0, 0x48, 0, 0, 0, 0x01, 0, 0x18, 0, 0, 0, 0, 0, 0xc0, 0x3c, 0, 0, 0x13, 0x0e, 0, 0, 0x13, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
char img_buffer[8191];

char img_data[65535];

ssize_t load_img(char *file)
{
int f;
ssize_t r;

f=open(file, O_RDONLY);
r=read(f, img_data, 3528);
close(f);

return r;
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

printf("IS=%d\n", remain);

while (remain>0) {
 len=remain<1016 ? remain : 1016;
 sent=pn*1016;

 printf("%d %d %d %d\n", len, remain, sent, pn);

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
 printf("Ir=%d\n", r);

 remain=remain-len;
 pn++;
}

#if 0
       while bytes_remaining > 0:
            this_length = min(bytes_remaining, self.IMAGE_REPORT_PAYLOAD_LENGTH)
            bytes_sent = page_number * self.IMAGE_REPORT_PAYLOAD_LENGTH

            header = [
                0x02,
                0x07,
                key,
                1 if this_length == bytes_remaining else 0,
                this_length & 0xFF,
                this_length >> 8,
                page_number & 0xFF,
                page_number >> 8
            ]

            payload = bytes(header) + image[bytes_sent:bytes_sent + this_length]
            padding = bytearray(self.IMAGE_REPORT_LENGTH - len(payload))
            self.device.write(payload + padding)

            bytes_remaining = bytes_remaining - this_length
            page_number = page_number + 1
#endif

return r;
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

	if (!handle)
		return 255;

	printf("%d %d %d\n", sizeof(img_header), sizeof(img_extra_v1), sizeof(img_buffer));

	res=reset_deck();
	printf("RESET=%d\n", res);

	sleep(2);

	deck_brightness(100);

	sleep(1);

	deck_brightness(60);

	il=load_img("button.jpg");
	printf("ILs=%d\n", il);

	deck_set_image(2, img_data, il);

//	res=reset_img();
//	printf("RES=%d\n", res);

#if 0
	res=set_img(3, 1);
	printf("RES=%d\n", res);

	res=set_img(3, 2);
	printf("RES=%d\n", res);
#endif

	// Read requested state
	while (1) {
		memset(buf, 0, 64);
		res = hid_read_timeout(handle, buf, 64, 1500);
		printf("RES=%d\n", res);

		// Print out the returned buffer.
		for (i = 0; i < 64; i++) {
			printf("%02x ", buf[i]);
		}
		printf("\n");
	}

	// Close the device
	hid_close(handle);

	// Finalize the hidapi library
	res = hid_exit();

	return 0;
}
