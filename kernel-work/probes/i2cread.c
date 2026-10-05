/*
 * Прочитать несколько байт у устройства на шине I2C.
 *
 * Два способа адресации регистра: однобайтовый (обычные EEPROM) и
 * двухбайтовый (сенсоры камер и EEPROM большого объёма). Пробуем оба.
 *
 * Устройство отвечает только под питанием, поэтому повторяем, пока не
 * выйдет: рядом крутится проба, которая питание периодически подаёт.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int rd(int fd, int addr, int wide, unsigned reg, unsigned char *buf, int n)
{
	unsigned char a[2];
	struct i2c_msg msg[2];
	struct i2c_rdwr_ioctl_data io = { .msgs = msg, .nmsgs = 2 };

	if (wide) {
		a[0] = (reg >> 8) & 0xff; a[1] = reg & 0xff;
	} else {
		a[0] = reg & 0xff;
	}
	msg[0].addr = addr; msg[0].flags = 0; msg[0].len = wide ? 2 : 1; msg[0].buf = a;
	msg[1].addr = addr; msg[1].flags = I2C_M_RD; msg[1].len = n; msg[1].buf = buf;
	return ioctl(fd, I2C_RDWR, &io);
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		printf("надо: %s /dev/i2c-N адрес ширина_регистра [сколько_байт]\n", argv[0]);
		return 2;
	}
	int addr = (int)strtol(argv[2], NULL, 0);
	int wide = atoi(argv[3]);
	int n = argc > 4 ? atoi(argv[4]) : 32;
	unsigned char buf[256];
	int fd = open(argv[1], O_RDWR);

	if (fd < 0) { perror(argv[1]); return 1; }
	if (n > (int)sizeof(buf)) n = sizeof(buf);

	for (int try = 0; try < 4000; try++) {
		memset(buf, 0, n);
		if (rd(fd, addr, wide, 0, buf, n) >= 0) {
			printf("адрес 0x%02x, регистр %s: ", addr, wide ? "16-бит" : "8-бит");
			for (int i = 0; i < n; i++)
				printf("%02x", buf[i]);
			printf("\n  как текст: ");
			for (int i = 0; i < n; i++)
				putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
			printf("\n");
			return 0;
		}
		usleep(500);
	}
	printf("адрес 0x%02x, регистр %s: не ответил\n", addr, wide ? "16-бит" : "8-бит");
	return 1;
}
