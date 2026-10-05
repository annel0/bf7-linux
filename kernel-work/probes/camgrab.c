/*
 * Снять кадр с CAMSV0 в память: буфер, выдержка, усиление, фокус.
 *
 * Собирать и запускать на телефоне обычным gcc — программа glibc, андроидные
 * заглушки ей не нужны. Поток к этому моменту уже идёт: сенсор поднят и
 * приёмник seninf настроен пробой bf7-camshot, которая держит
 * /dev/kd_camera_hw открытым.
 *
 * Порядок важен: адрес буфера кладётся в регистры ДО включения видоискателя.
 *
 * Смещения регистров CAMSV взяты из строк вендорской libcam.iopipe.so — она
 * печатает собственный дамп с именами; база CAMSV — 0x15050000.
 *
 * Вызов (всё необязательно, через равно):
 *   ./camgrab w=2104 h=1560 stride=4208 port=24 exp=2000 gain=256 af=400 \
 *             wait=700 out=/root/frame.raw
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* ---------------- ION ---------------- */
typedef int ion_user_handle_t;

struct ion_allocation_data {
	size_t len, align;
	unsigned int heap_id_mask, flags;
	ion_user_handle_t handle;
};
struct ion_fd_data { ion_user_handle_t handle; int fd; };
struct ion_handle_data { ion_user_handle_t handle; };
struct ion_custom_data { unsigned int cmd; unsigned long arg; };

#define ION_IOC_MAGIC  'I'
#define ION_IOC_ALLOC  _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE   _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP    _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_CUSTOM _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)

#define ION_CMD_MULTIMEDIA 1
#define ION_MM_GET_IOVA    8

struct ion_mm_get_iova_param {
	union { int handle; void *kernel_handle; };
	int module_id;
	unsigned int security, coherent;
	unsigned int reserve_iova_start, reserve_iova_end;
	unsigned long long phy_addr;
	unsigned long len;
};
struct ion_mm_data {
	int mm_cmd;
	struct ion_mm_get_iova_param get_phys_param;
	char pad[64];
};

/* ---------------- регистры ISP ---------------- */
#define ISP_MAGIC 'k'
struct reg { unsigned int Addr, Val; };
struct regio { struct reg *pData; unsigned int Count; };
#define ISP_READ_REGISTER  _IOWR(ISP_MAGIC, 5, struct regio)
#define ISP_WRITE_REGISTER _IOWR(ISP_MAGIC, 6, struct regio)

static int ispfd = -1;

static unsigned rd(unsigned off)
{
	struct reg r = { off, 0 };
	struct regio io = { &r, 1 };

	return ioctl(ispfd, ISP_READ_REGISTER, &io) < 0 ? 0xdeadbeef : r.Val;
}

static void wr(unsigned off, unsigned val)
{
	struct reg r = { off, val };
	struct regio io = { &r, 1 };

	if (ioctl(ispfd, ISP_WRITE_REGISTER, &io) < 0)
		printf("  запись 0x%05x не прошла: %s\n", off, strerror(errno));
}

#define SV             0x50000u
#define SV_MODULE_EN   (SV + 0x000)
#define SV_FMT_SEL     (SV + 0x004)
#define SV_INT_EN      (SV + 0x008)
#define SV_INT_ST      (SV + 0x00C)
#define SV_SW_CTL      (SV + 0x010)
#define SV_IMGO_FBC    (SV + 0x01C)
#define SV_CLK_EN      (SV + 0x020)
#define SV_PAK         (SV + 0x03C)
#define SV_IMGO_BASE   (SV + 0x208)
#define SV_IMGO_OFST   (SV + 0x20C)
#define SV_IMGO_XSIZE  (SV + 0x210)
#define SV_IMGO_YSIZE  (SV + 0x214)
#define SV_IMGO_STRIDE (SV + 0x218)
#define SV_TG_VF_CON   (SV + 0x414)
#define SV_TG_FRMSIZE  (SV + 0x448)
#define SV_TG_INTER    (SV + 0x44C)

/* ---------------- сенсор по шине ---------------- */
static int i2cfd = -1;

static int sensor_w16(unsigned reg, unsigned val)
{
	unsigned char b[4] = { reg >> 8, reg & 0xff, val >> 8, val & 0xff };
	struct i2c_msg m = { .addr = 0x10, .flags = 0, .len = 4, .buf = b };
	struct i2c_rdwr_ioctl_data io = { .msgs = &m, .nmsgs = 1 };

	return ioctl(i2cfd, I2C_RDWR, &io);
}

static int sensor_r16(unsigned reg, unsigned *val)
{
	unsigned char a[2] = { reg >> 8, reg & 0xff }, d[2] = { 0, 0 };
	struct i2c_msg m[2] = {
		{ .addr = 0x10, .flags = 0, .len = 2, .buf = a },
		{ .addr = 0x10, .flags = I2C_M_RD, .len = 2, .buf = d },
	};
	struct i2c_rdwr_ioctl_data io = { .msgs = m, .nmsgs = 2 };
	int r = ioctl(i2cfd, I2C_RDWR, &io);

	if (r >= 0) *val = (d[0] << 8) | d[1];
	return r;
}

/* ---------------- линза ---------------- */
#define AF_MAGIC 'A'
struct stAF_MotorName { char uMotorName[32]; };
#define AFIOC_T_MOVETO     _IOW(AF_MAGIC, 1, uint32_t)
#define AFIOC_S_SETDRVNAME _IOW(AF_MAGIC, 10, struct stAF_MotorName)

static int af_open(const char *drv, unsigned pos)
{
	int fd = open("/dev/MAINAF", O_RDWR);
	struct stAF_MotorName n;

	if (fd < 0) { printf("  /dev/MAINAF: %s\n", strerror(errno)); return -1; }
	memset(&n, 0, sizeof(n));
	strncpy(n.uMotorName, drv, sizeof(n.uMotorName) - 1);
	if (ioctl(fd, AFIOC_S_SETDRVNAME, &n) < 0)
		printf("  выбор мотора %s: %s\n", drv, strerror(errno));
	if (ioctl(fd, AFIOC_T_MOVETO, &pos) < 0)
		printf("  наводка на %u: %s\n", pos, strerror(errno));
	else
		printf("  линза: мотор %s, положение %u\n", drv, pos);
	return fd;
}

/* ---------------- разбор «имя=значение» ---------------- */
static unsigned long opt(int argc, char **argv, const char *name, unsigned long dflt)
{
	size_t n = strlen(name);

	for (int i = 1; i < argc; i++)
		if (!strncmp(argv[i], name, n) && argv[i][n] == '=')
			return strtoul(argv[i] + n + 1, NULL, 0);
	return dflt;
}

static const char *opts(int argc, char **argv, const char *name, const char *dflt)
{
	size_t n = strlen(name);

	for (int i = 1; i < argc; i++)
		if (!strncmp(argv[i], name, n) && argv[i][n] == '=')
			return argv[i] + n + 1;
	return dflt;
}

int main(int argc, char **argv)
{
	unsigned width  = opt(argc, argv, "w", 2104);
	unsigned height = opt(argc, argv, "h", 1560);
	unsigned stride = opt(argc, argv, "stride", 0);
	int      module = (int)opt(argc, argv, "port", 24);   /* CAM_IMGO_S = CAMSV0 */
	unsigned moden  = opt(argc, argv, "moden", 0x4000001du);
	unsigned expo   = opt(argc, argv, "exp", 0);
	unsigned gain   = opt(argc, argv, "gain", 0);
	unsigned flen   = opt(argc, argv, "flen", 0);
	long     afpos  = (long)opt(argc, argv, "af", 0xffffffffUL);
	unsigned waitms = opt(argc, argv, "wait", 600);
	const char *afdrv = opts(argc, argv, "afdrv", "DW9714AF");
	const char *out   = opts(argc, argv, "out", "/root/frame.raw");

	if (!stride) stride = width * 2;

	size_t len = ((size_t)stride * height + 0xfffff) & ~(size_t)0xfffff;

	/* ---- выдержка, усиление, фокус ---- */
	i2cfd = open("/dev/i2c-2", O_RDWR);
	if (i2cfd >= 0) {
		unsigned v;

		if (flen) sensor_w16(0x0340, flen);
		if (expo) sensor_w16(0x0202, expo);
		if (gain) sensor_w16(0x0204, gain);
		if (!sensor_r16(0x0340, &v)) printf("  сенсор: длина кадра %u", v);
		if (!sensor_r16(0x0202, &v)) printf(", выдержка %u", v);
		if (!sensor_r16(0x0204, &v)) printf(", усиление 0x%04x", v);
		printf("\n");
	}

	int affd = afpos == (long)0xffffffffUL ? -1 : af_open(afdrv, (unsigned)afpos);

	/* ---- буфер ---- */
	int ionfd = open("/dev/ion", O_RDONLY);

	if (ionfd < 0) { perror("/dev/ion"); return 1; }

	struct ion_allocation_data alloc = { 0 };

	alloc.len = len;
	alloc.heap_id_mask = 1u << 10;
	if (ioctl(ionfd, ION_IOC_ALLOC, &alloc) < 0) { perror("ION_IOC_ALLOC"); return 1; }

	struct ion_mm_data mm = { 0 };
	struct ion_custom_data cd = { ION_CMD_MULTIMEDIA, (unsigned long)&mm };

	mm.mm_cmd = ION_MM_GET_IOVA;
	mm.get_phys_param.handle = alloc.handle;
	mm.get_phys_param.module_id = module;
	if (ioctl(ionfd, ION_IOC_CUSTOM, &cd) < 0) { perror("ION_MM_GET_IOVA"); return 1; }

	unsigned mva = (unsigned)mm.get_phys_param.phy_addr;
	struct ion_fd_data fdd = { alloc.handle, -1 };

	if (ioctl(ionfd, ION_IOC_MAP, &fdd) < 0) { perror("ION_IOC_MAP"); return 1; }

	unsigned char *mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fdd.fd, 0);

	if (mem == MAP_FAILED) { perror("mmap буфера"); return 1; }
	memset(mem, 0xA5, len);

	printf("  буфер %zu байт, адрес для камеры 0x%08x (порт %d)\n", len, mva, module);

	/* ---- блок захвата ---- */
	ispfd = open("/dev/camera-isp", O_RDWR);
	if (ispfd < 0) { perror("/dev/camera-isp"); return 1; }

	wr(SV_TG_VF_CON, rd(SV_TG_VF_CON) & ~1u);
	wr(SV_CLK_EN, 0x00008005);
	wr(SV_SW_CTL, 0);
	wr(SV_IMGO_BASE,   mva);
	wr(SV_IMGO_OFST,   0);
	wr(SV_IMGO_XSIZE,  stride - 1);
	wr(SV_IMGO_YSIZE,  height - 1);
	wr(SV_IMGO_STRIDE, stride);
	wr(SV_IMGO_FBC,    0);
	wr(SV_INT_EN,      0xffffffff);
	wr(SV_MODULE_EN,   moden);
	wr(SV_TG_VF_CON,   rd(SV_TG_VF_CON) | 1u);

	usleep(waitms * 1000);
	printf("  TG: INTER_ST=%08x FRMSIZE=%08x INT=%08x\n",
	       rd(SV_TG_INTER), rd(SV_TG_FRMSIZE), rd(SV_INT_ST));

	size_t expect = (size_t)stride * height, changed = 0;

	for (size_t i = 0; i < expect; i++)
		if (mem[i] != 0xA5) changed++;

	if (changed) {
		FILE *f = fopen(out, "wb");

		if (f) { fwrite(mem, 1, expect, f); fclose(f); }
		printf("  снято: %u x %u, шаг %u -> %s (заполнено %zu из %zu)\n",
		       width, height, stride, out, changed, expect);
	} else {
		printf("  в буфер ничего не записалось\n");
	}

	wr(SV_TG_VF_CON, rd(SV_TG_VF_CON) & ~1u);
	munmap(mem, len);
	close(fdd.fd);
	{ struct ion_handle_data h = { alloc.handle }; ioctl(ionfd, ION_IOC_FREE, &h); }
	close(ionfd);
	close(ispfd);
	if (affd >= 0) close(affd);
	if (i2cfd >= 0) close(i2cfd);
	return changed ? 0 : 2;
}
