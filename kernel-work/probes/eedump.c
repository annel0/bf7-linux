/* Снять дамп EEPROM модуля камеры: там обычно записано имя сенсора. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int rd(int fd, int addr, unsigned reg, unsigned char *buf, int n)
{
	unsigned char a[2] = { (reg >> 8) & 0xff, reg & 0xff };
	struct i2c_msg m[2];
	struct i2c_rdwr_ioctl_data io = { .msgs = m, .nmsgs = 2 };

	m[0].addr = addr; m[0].flags = 0;          m[0].len = 2; m[0].buf = a;
	m[1].addr = addr; m[1].flags = I2C_M_RD;   m[1].len = n; m[1].buf = buf;
	return ioctl(fd, I2C_RDWR, &io);
}

int main(int argc, char **argv)
{
	int addr = argc > 2 ? (int)strtol(argv[2], NULL, 0) : 0x50;
	int total = argc > 3 ? atoi(argv[3]) : 512;
	int fd = open(argc > 1 ? argv[1] : "/dev/i2c-2", O_RDWR);
	unsigned char buf[32];

	if (fd < 0) { perror("шина"); return 1; }

	for (int off = 0; off < total; off += 32) {
		int ok = 0;

		for (int try = 0; try < 2000 && !ok; try++) {
			memset(buf, 0, sizeof(buf));
			if (rd(fd, addr, off, buf, 32) >= 0) ok = 1; else usleep(300);
		}
		if (!ok) { printf("%04x: не дочитал\n", off); continue; }
		printf("%04x: ", off);
		for (int i = 0; i < 32; i++) printf("%02x", buf[i]);
		printf("  ");
		for (int i = 0; i < 32; i++)
			putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
		printf("\n");
	}
	return 0;
}
