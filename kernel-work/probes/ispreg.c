/*
 * Чтение и запись регистров ISP через /dev/camera-isp.
 *
 * Смещения считаются от 0x15000000 — это третий диапазон reg у узла
 * ispsys в дереве устройств, и именно от него отмеряет проверку сам
 * драйвер (ISP_ReadReg / ISP_WriteRegToHw в mt6761/camera_isp.c).
 * Полезные куски внутри окна:
 *   0x04000  CAM_A         0x06000  CAM_B
 *   0x40000  seninf        0x50000  CAMSV
 *
 * Заводское ядро отдавало наружу только первые 0x10000 — то есть ни seninf,
 * ни CAMSV. У нас окно расширено до 0x52000 (правка в camera_isp.c).
 *
 *   ./ispreg r <смещение> [сколько слов]      прочитать
 *   ./ispreg w <смещение> <значение>          записать
 *   ./ispreg diff <от> <до> <мс>              что меняется само
 *
 * «diff» — главное: он дважды снимает кусок регистров с паузой и печатает
 * только то, что изменилось. Живые счётчики так видно без всякой
 * документации.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#define ISP_MAGIC 'k'
#define ISP_CMD_READ_REG  5
#define ISP_CMD_WRITE_REG 6

struct reg {
	unsigned int Addr;
	unsigned int Val;
};

struct regio {
	struct reg *pData;
	unsigned int Count;
};

#define ISP_READ_REGISTER  _IOWR(ISP_MAGIC, ISP_CMD_READ_REG,  struct regio)
#define ISP_WRITE_REGISTER _IOWR(ISP_MAGIC, ISP_CMD_WRITE_REG, struct regio)

#define MAXN 512

static int rd(int fd, unsigned int off, unsigned int n, struct reg *out)
{
	struct regio io = { .pData = out, .Count = n };
	unsigned int i;

	for (i = 0; i < n; i++) {
		out[i].Addr = off + i * 4;
		out[i].Val = 0;
	}
	return ioctl(fd, ISP_READ_REGISTER, &io);
}

int main(int argc, char **argv)
{
	struct reg a[MAXN], b[MAXN];
	int fd;

	if (argc < 3) {
		printf("надо: %s r|w|diff ...\n", argv[0]);
		return 2;
	}

	fd = open("/dev/camera-isp", O_RDWR);
	if (fd < 0) {
		perror("/dev/camera-isp");
		return 1;
	}

	if (argv[1][0] == 'r') {
		unsigned int off = (unsigned int)strtoul(argv[2], NULL, 0);
		unsigned int n = argc > 3 ? (unsigned int)atoi(argv[3]) : 8;
		unsigned int i;

		if (n > MAXN)
			n = MAXN;
		if (rd(fd, off, n, a) < 0) {
			perror("чтение");
			return 1;
		}
		for (i = 0; i < n; i++) {
			if (i % 4 == 0)
				printf("\n0x%05x:", off + i * 4);
			printf(" %08x", a[i].Val);
		}
		printf("\n");
		return 0;
	}

	if (argv[1][0] == 'w') {
		struct reg one = {
			.Addr = (unsigned int)strtoul(argv[2], NULL, 0),
			.Val = (unsigned int)strtoul(argv[3], NULL, 0),
		};
		struct regio io = { .pData = &one, .Count = 1 };

		if (ioctl(fd, ISP_WRITE_REGISTER, &io) < 0) {
			perror("запись");
			return 1;
		}
		printf("0x%05x <- %08x\n", one.Addr, one.Val);
		return 0;
	}

	/* diff */
	{
		unsigned int from = (unsigned int)strtoul(argv[2], NULL, 0);
		unsigned int to = argc > 3 ?
			(unsigned int)strtoul(argv[3], NULL, 0) : from + 0x200;
		unsigned int ms = argc > 4 ? (unsigned int)atoi(argv[4]) : 120;
		unsigned int off, changed = 0;

		for (off = from; off < to; off += MAXN * 4) {
			unsigned int n = (to - off) / 4;
			unsigned int i;

			if (n > MAXN)
				n = MAXN;
			if (rd(fd, off, n, a) < 0)
				continue;
			usleep(ms * 1000);
			if (rd(fd, off, n, b) < 0)
				continue;
			for (i = 0; i < n; i++) {
				if (a[i].Val != b[i].Val) {
					printf("0x%05x: %08x -> %08x\n",
					       off + i * 4, a[i].Val, b[i].Val);
					changed++;
				}
			}
		}
		if (!changed)
			printf("в этом куске ничего не шевелится\n");
		return 0;
	}
}
