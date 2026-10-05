/*
 * Прочитать несколько байт у устройства на шине I2C, начиная с заданного
 * регистра. Отличие от i2cread.c: там чтение всегда с нулевого регистра, а
 * коды опознания сенсоров лежат в глубине (у GalaxyCore — 0x03f0, у Hynix —
 * 0x0716, у Samsung — 0x0000).
 *
 * надо: ./i2creg /dev/i2c-N адрес ширина_регистра регистр [сколько_байт]
 *   ширина_регистра: 0 — адрес регистра одним байтом, 1 — двумя (старший первым)
 *
 * Сенсор отвечает только под питанием. Подать и удержать:
 *   echo "0 on" > /proc/driver/camsensor_power   (0 — основная, 1 — фронтальная)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int rd(int fd, int addr, int wide, int reg, unsigned char *out, int n)
{
	unsigned char a[2];
	struct i2c_msg m[2];
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

	if (wide) {
		a[0] = (unsigned char)(reg >> 8);
		a[1] = (unsigned char)(reg & 0xff);
	} else {
		a[0] = (unsigned char)(reg & 0xff);
	}

	m[0].addr = (unsigned short)addr;
	m[0].flags = 0;
	m[0].len = (unsigned short)(wide ? 2 : 1);
	m[0].buf = a;

	m[1].addr = (unsigned short)addr;
	m[1].flags = I2C_M_RD;
	m[1].len = (unsigned short)n;
	m[1].buf = out;

	return ioctl(fd, I2C_RDWR, &d);
}

int main(int argc, char **argv)
{
	unsigned char buf[256];

	if (argc < 5) {
		printf("надо: %s /dev/i2c-N адрес ширина_регистра регистр [сколько_байт]\n",
		       argv[0]);
		return 2;
	}

	int addr = (int)strtol(argv[2], NULL, 0);
	int wide = atoi(argv[3]);
	int reg  = (int)strtol(argv[4], NULL, 0);
	int n    = argc > 5 ? atoi(argv[5]) : 4;

	if (n > (int)sizeof(buf))
		n = sizeof(buf);

	int fd = open(argv[1], O_RDWR);

	if (fd < 0) {
		perror(argv[1]);
		return 1;
	}

	for (int try = 0; try < 200; try++) {
		memset(buf, 0, n);
		if (rd(fd, addr, wide, reg, buf, n) >= 0) {
			printf("0x%02x рег 0x%04x (%d-бит): ", addr, reg, wide ? 16 : 8);
			for (int i = 0; i < n; i++)
				printf("%02x", buf[i]);
			printf("\n");
			return 0;
		}
		usleep(500);
	}

	printf("0x%02x рег 0x%04x: не ответил (%s)\n", addr, reg, strerror(errno));
	return 1;
}
