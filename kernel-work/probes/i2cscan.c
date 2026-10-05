/*
 * Кто отзывается на шине I2C.
 *
 * Нужно, чтобы опознать модули камер: драйверы, собранные в ядро, стучатся
 * в 0x10 (S5K3L6) и 0x20/0x28 (HI556), а на плате объявлены 0x1A (i2c-2) и
 * 0x2D (i2c-4). Похоже, стоят другие модули; выяснить это можно только
 * спросив саму шину.
 *
 * Тонкость: сенсор отвечает только под питанием, а драйвер снимает его
 * через ~25 мс после проверки. Поэтому сканируем в цикле, пока рядом
 * крутится проба, которая это питание периодически подаёт.
 *
 * Пропускаем адреса, уже занятые драйверами ядра (их брать нельзя).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#define I2C_SLAVE_FORCE_CMD 0x0706

static int seen[128];

static void scan(const char *dev)
{
	int fd = open(dev, O_RDWR);

	if (fd < 0)
		return;

	for (int a = 0x08; a <= 0x77; a++) {
		unsigned char b;

		if (ioctl(fd, I2C_SLAVE_FORCE_CMD, a) < 0)
			continue;
		if (read(fd, &b, 1) >= 0)
			seen[a]++;
	}
	close(fd);
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/i2c-2";
	int passes = argc > 2 ? atoi(argv[2]) : 40;

	printf("шина %s, проходов %d\n", dev, passes);
	for (int i = 0; i < passes; i++) {
		scan(dev);
		usleep(3000);
	}

	int found = 0;

	for (int a = 0x08; a <= 0x77; a++)
		if (seen[a]) {
			printf("  отозвался 0x%02x (в %d проходах из %d)\n",
			       a, seen[a], passes);
			found++;
		}
	if (!found)
		printf("  никто не отозвался\n");
	return 0;
}
