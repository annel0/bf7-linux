/*
 * Двигается ли линза.
 *
 * Открываем /dev/MAINAF (это само включает питание катушки), выбираем мотор
 * и ходим по положениям, каждый раз спрашивая у драйвера, где он думает, что
 * находится. Заодно смотрим на шину: у DW9714 положение пишется двумя
 * байтами по адресу 0x0c.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define AF_MAGIC 'A'
struct stAF_MotorInfo {
	uint32_t u4CurrentPosition, u4MacroPosition, u4InfPosition;
	unsigned char bIsMotorMoving, bIsMotorOpen, bIsSupportSR;
};
struct stAF_MotorName { char uMotorName[32]; };
#define AFIOC_G_MOTORINFO  _IOR(AF_MAGIC, 0, struct stAF_MotorInfo)
#define AFIOC_T_MOVETO     _IOW(AF_MAGIC, 1, uint32_t)
#define AFIOC_S_SETDRVNAME _IOW(AF_MAGIC, 10, struct stAF_MotorName)
#define AFIOC_G_GETDRVNAME _IOWR(AF_MAGIC, 17, struct stAF_MotorName)

static int i2c_read_raw(int fd, unsigned char *buf, int n)
{
	struct i2c_msg m = { .addr = 0x0c, .flags = I2C_M_RD, .len = n, .buf = buf };
	struct i2c_rdwr_ioctl_data io = { .msgs = &m, .nmsgs = 1 };

	return ioctl(fd, I2C_RDWR, &io);
}

int main(int argc, char **argv)
{
	const char *drv = argc > 1 ? argv[1] : "DW9714AF";
	int fd = open("/dev/MAINAF", O_RDWR);

	if (fd < 0) { perror("/dev/MAINAF"); return 1; }

	struct stAF_MotorName n;

	memset(&n, 0, sizeof(n));
	strncpy(n.uMotorName, drv, sizeof(n.uMotorName) - 1);
	printf("выбор мотора %s: %s\n", drv,
	       ioctl(fd, AFIOC_S_SETDRVNAME, &n) < 0 ? strerror(errno) : "принят");

	memset(&n, 0, sizeof(n));
	if (ioctl(fd, AFIOC_G_GETDRVNAME, &n) == 0)
		printf("драйвер отвечает, что он: %.31s\n", n.uMotorName);

	int bus = open("/dev/i2c-2", O_RDWR);

	for (int i = 0; i <= 8; i++) {
		uint32_t pos = (uint32_t)(i * 128);
		struct stAF_MotorInfo mi;
		unsigned char raw[2] = { 0, 0 };
		int r = ioctl(fd, AFIOC_T_MOVETO, &pos);

		usleep(120000);
		memset(&mi, 0, sizeof(mi));
		printf("положение %4u: ioctl %s", pos,
		       r < 0 ? strerror(errno) : "принят");
		if (ioctl(fd, AFIOC_G_MOTORINFO, &mi) == 0)
			printf(", драйвер считает %u (едет %d, открыт %d)",
			       mi.u4CurrentPosition, mi.bIsMotorMoving, mi.bIsMotorOpen);
		if (bus >= 0 && i2c_read_raw(bus, raw, 2) >= 0)
			printf(", на шине %02x %02x -> код %u",
			       raw[0], raw[1], ((raw[0] & 0x3f) << 4) | (raw[1] >> 4));
		printf("\n");
		fflush(stdout);
	}
	if (bus >= 0) close(bus);
	close(fd);
	return 0;
}
