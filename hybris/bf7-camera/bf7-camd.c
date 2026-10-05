/*
 * bf7-camd — камера телефона как обычная веб-камера.
 *
 * Обычная программа Debian — браузер, месенджер, «Камера» — умеет только
 * одно: открыть /dev/videoN и читать кадры. Демон закрывает разрыв: забирает
 * кадры у блока захвата CAMSV, разбирает мозаику Байера, ведёт выдержку,
 * баланс белого и наводку — и отдаёт готовый поток в v4l2loopback.
 *
 * Это обычная программа glibc: собирается прямо на телефоне
 *
 *     gcc -O2 -o bf7-camd bf7-camd.c -lm
 *
 * Поток к этому моменту должен идти: его поднимает и держит bf7-seninf-up —
 * единственная часть, которой нужен мир Android, потому что приёмник MIPI
 * настраивает вендорская библиотека.
 *
 * Путь кадра:
 *   CAMSV0 -> DMA в память (адрес от ION через блок MMU камеры)
 *          -> свёртка Байера вдвое -> YUYV -> write() в /dev/video0
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/videodev2.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/inotify.h>
#include <sys/eventfd.h>
#include <poll.h>

/* ------------------------------------------------------------------ */
/*  регистры ISP                                                       */
/* ------------------------------------------------------------------ */
#define IMGSYS_BASE  0x15000000u
#define IMGSYS_SIZE  0x52000u

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

#define SV_INT_SOF       (1u << 7)
#define SV_INT_PASS1_DON (1u << 10)

static volatile uint32_t *imgsys;

static inline uint32_t ispr(unsigned off) { return imgsys[off / 4]; }
static inline void ispw(unsigned off, uint32_t v) { imgsys[off / 4] = v; }

/* ------------------------------------------------------------------ */
/*  ION: память, понятная блоку MMU камеры                             */
/* ------------------------------------------------------------------ */
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

#define ION_CMD_SYSTEM     0
#define ION_CMD_MULTIMEDIA 1
#define ION_SYS_CACHE_SYNC 0
#define ION_CACHE_INVALID_BY_RANGE 1
#define ION_MM_GET_IOVA    8
#define ION_FLAG_CACHED    1
#define M4U_PORT_CAM_IMGO_S 24

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
struct ion_sys_cache_sync_param {
	union { int handle; void *kernel_handle; };
	void *va;
	unsigned int size;
	int sync_type;
	unsigned long long iova;
};
struct ion_sys_data {
	int sys_cmd;
	struct ion_sys_cache_sync_param cache_sync_param;
	char pad[64];
};

struct campbuf {
	ion_user_handle_t handle;
	int dmafd;
	uint32_t mva;
	uint8_t *va;
	size_t len;
};

static int ionfd = -1;
static int cached = 1;

static int buf_alloc(struct campbuf *b, size_t len)
{
	struct ion_allocation_data a;
	struct ion_mm_data mm;
	struct ion_custom_data cd = { ION_CMD_MULTIMEDIA, (unsigned long)&mm };
	struct ion_fd_data fdd;

	memset(&a, 0, sizeof(a));
	memset(&mm, 0, sizeof(mm));
	a.len = (len + 0xfffff) & ~(size_t)0xfffff;
	a.heap_id_mask = 1u << 10;              /* мультимедийная куча */
	a.flags = cached ? ION_FLAG_CACHED : 0;
	if (ioctl(ionfd, ION_IOC_ALLOC, &a) < 0) { perror("ION_IOC_ALLOC"); return -1; }

	mm.mm_cmd = ION_MM_GET_IOVA;
	mm.get_phys_param.handle = a.handle;
	mm.get_phys_param.module_id = M4U_PORT_CAM_IMGO_S;
	if (ioctl(ionfd, ION_IOC_CUSTOM, &cd) < 0) { perror("ION_MM_GET_IOVA"); return -1; }

	fdd.handle = a.handle; fdd.fd = -1;
	if (ioctl(ionfd, ION_IOC_MAP, &fdd) < 0) { perror("ION_IOC_MAP"); return -1; }

	b->handle = a.handle;
	b->dmafd  = fdd.fd;
	b->len    = a.len;
	b->mva    = (uint32_t)mm.get_phys_param.phy_addr;
	b->va     = mmap(NULL, a.len, PROT_READ | PROT_WRITE, MAP_SHARED, fdd.fd, 0);
	if (b->va == MAP_FAILED) { perror("mmap буфера"); return -1; }
	return 0;
}

/* выбросить из кэша то, что мог переписать DMA */
static void buf_invalidate(struct campbuf *b, size_t len)
{
	struct ion_sys_data sd;
	struct ion_custom_data cd = { ION_CMD_SYSTEM, (unsigned long)&sd };

	if (!cached) return;
	memset(&sd, 0, sizeof(sd));
	sd.sys_cmd = ION_SYS_CACHE_SYNC;
	sd.cache_sync_param.handle = b->handle;
	sd.cache_sync_param.va = b->va;
	sd.cache_sync_param.size = len;
	sd.cache_sync_param.sync_type = ION_CACHE_INVALID_BY_RANGE;
	ioctl(ionfd, ION_IOC_CUSTOM, &cd);
}

/* ------------------------------------------------------------------ */
/*  сенсор по шине: выдержка и усиление                                */
/* ------------------------------------------------------------------ */
static int i2cfd = -1;

static int sensor_w16(unsigned reg, unsigned val)
{
	unsigned char b[4] = { reg >> 8, reg & 0xff, val >> 8, val & 0xff };
	struct i2c_msg m = { .addr = 0x10, .flags = 0, .len = 4, .buf = b };
	struct i2c_rdwr_ioctl_data io = { .msgs = &m, .nmsgs = 1 };

	return i2cfd < 0 ? -1 : ioctl(i2cfd, I2C_RDWR, &io);
}

/* ------------------------------------------------------------------ */
/*  линза                                                              */
/* ------------------------------------------------------------------ */
#define AF_MAGIC 'A'
struct stAF_MotorName { char uMotorName[32]; };
#define AFIOC_T_MOVETO     _IOW(AF_MAGIC, 1, uint32_t)
#define AFIOC_S_SETDRVNAME _IOW(AF_MAGIC, 10, struct stAF_MotorName)

static int affd = -1;
static int af_direct;          /* писать в катушку самим, мимо драйвера */

/*
 * Катушка DW9714 принимает две посылки: [положение>>4, (положение&0xF)<<4]
 * на адрес 0x0C шины i2c-2 — ровно так же делает и драйвер ядра
 * (drivers/misc/mediatek/lens/mtk/main/common/dw9714af/DW9714AF.c).
 * Узел /dev/MAINAF всё равно нужно держать открытым: его открытие включает
 * питание катушки (регулятор vldo28 главного PMIC).
 */
static int af_write_i2c(unsigned pos)
{
	unsigned char b[2] = { (unsigned char)(pos >> 4),
			       (unsigned char)((pos & 0xf) << 4) };
	struct i2c_msg m = { .addr = 0x0c, .flags = 0, .len = 2, .buf = b };
	struct i2c_rdwr_ioctl_data io = { .msgs = &m, .nmsgs = 1 };

	return i2cfd < 0 ? -1 : ioctl(i2cfd, I2C_RDWR, &io);
}

static void af_moveto(unsigned pos)
{
	if (affd < 0) return;
	if (af_direct) {
		if (af_write_i2c(pos) < 0)
			fprintf(stderr, "  катушка не отозвалась на %u: %s\n",
				pos, strerror(errno));
	} else if (ioctl(affd, AFIOC_T_MOVETO, &pos) < 0) {
		fprintf(stderr, "  наводка на %u: %s\n", pos, strerror(errno));
	}
}

/* ------------------------------------------------------------------ */
static volatile sig_atomic_t stop_now;
static void on_signal(int s) { (void)s; stop_now = 1; }

/*
 * Кто-нибудь смотрит в камеру?
 *
 * Разбор кадра занимает 28 мс при кадре в 30 мс — это почти целое ядро. Если
 * держать демона постоянно занятым, телефон будет греться и сажать батарею
 * впустую. Поэтому, пока /dev/video0 никем не открыт, кроме нас самих, демон
 * спит и не трогает ни память, ни сенсор.
 *
 * Считаем по /proc: у v4l2loopback нет счётчика читателей наружу.
 */
/*
 * Сколько программ держат узел открытым.
 *
 * Быстрый путь — счётчик самого модуля: наша сборка v4l2loopback показывает
 * open_count в /sys/…/video0/openers. Он считает всех, включая нас самих,
 * поэтому зритель — каждый сверх первого.
 *
 * Медленный путь оставлен запасным, на случай непатченного модуля. Он дорог:
 * замер на телефоне — 19,7 мс на перебор всего /proc, а спрашиваем четыре раза
 * в секунду, то есть почти восемь процентов ядра уходило на один вопрос
 * «смотрит ли кто-нибудь».
 */
static int watchers_sysfs(const char *vdev)
{
	static char path[160];
	static int ready;
	char buf[32];
	int fd, n;

	if (!ready) {
		const char *base = strrchr(vdev, '/');

		snprintf(path, sizeof(path),
			 "/sys/devices/virtual/video4linux/%s/openers",
			 base ? base + 1 : vdev);
		ready = 1;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = (int)read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	n = atoi(buf);
	return n > 0 ? n - 1 : 0;	/* себя не считаем */
}
static int watchers_proc(const char *vdev)
{
	DIR *proc = opendir("/proc");
	struct dirent *pe;
	int n = 0;
	pid_t self = getpid();

	if (!proc) return 1;              /* не смогли посмотреть — считаем, что смотрят */
	while ((pe = readdir(proc))) {
		char path[128], link[256];
		DIR *fds;
		struct dirent *fe;
		pid_t pid = (pid_t)atoi(pe->d_name);

		if (pid <= 0 || pid == self) continue;
		snprintf(path, sizeof(path), "/proc/%d/fd", pid);
		fds = opendir(path);
		if (!fds) continue;
		while ((fe = readdir(fds))) {
			char f[300];
			ssize_t k;

			if (fe->d_name[0] == '.') continue;
			snprintf(f, sizeof(f), "%s/%s", path, fe->d_name);
			k = readlink(f, link, sizeof(link) - 1);
			if (k <= 0) continue;
			link[k] = 0;
			if (!strcmp(link, vdev)) { n++; break; }
		}
		closedir(fds);
	}
	closedir(proc);
	return n;
}

/*
 * Ручное управление. Программам оно не нужно — они ждут, что камера
 * настроится сама, — но человеку при настройке необходимо. Команды кладутся
 * в файл, демон его подхватывает и опустошает:
 *
 *     echo "фокус 420"      > /run/bf7-camd      (или focus)
 *     echo "фокус авто"     > /run/bf7-camd
 *     echo "выдержка 2000"  > /run/bf7-camd      (или exposure)
 *     echo "выдержка авто"  > /run/bf7-camd
 *     echo "усиление 0x100" > /run/bf7-camd      (или gain)
 *     echo "яркость 130"    > /run/bf7-camd      (к какой средней вести)
 *     echo "баланс 1.0 1.0 1.0" > /run/bf7-camd  (или wb; "авто" вернёт расчёт)
 */
#define CMDFILE "/run/bf7-camd"
#define GRABFILE  "/run/bf7-camd.grabbed"
#define DARKFILE  "/var/lib/bf7-camd-тёмный.raw"
/*
 * Где храним последнюю удачную наводку и выдержку. Без этого после каждой
 * загрузки первые секунды уходят на перебор, и кадр, снятый сразу, выходит
 * смазанным — а обычная программа именно так себя и ведёт: открыла камеру,
 * взяла кадр, закрыла.
 */
#define STATEFILE "/var/lib/bf7-camd.state"

static int cmd_word(const char *w, const char *ru, const char *en)
{
	return !strcmp(w, ru) || !strcmp(w, en);
}

/*
 * Похожа ли точка на серую. В замер баланса белого берём только такие: если
 * пускать туда насыщенные цвета, баланс поедет за ними. На снимке палитры
 * красная, зелёная и синяя полосы утягивали расчёт, и белая полоса розовела.
 * Мерка простая: самый сильный канал не больше чем вдвое сильнее самого
 * слабого.
 */

static int watchers(const char *vdev)
{
	int n = watchers_sysfs(vdev);

	return n >= 0 ? n : watchers_proc(vdev);
}
static int grayish(int r, int g, int b)
{
	int mx = r > g ? r : g, mn = r < g ? r : g;

	if (b > mx) mx = b;
	if (b < mn) mn = b;
	return mn > 8 && mx <= mn * 2;
}

static double now_s(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

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

/*
 * ---- разбор кадра по ядрам ----
 *
 * Один поток разбирал кадр за 24 мс при кадре сенсора в 26 мс. Пока мы
 * считали предыдущий кадр, следующий успевал прийти и уйти: наружу выходило
 * то 38 кадров в секунду, то 19, и картинка в guvcview и браузере дёргалась.
 * Кадр делится по строкам между ядрами — разбор укладывается в четверть
 * такта, и темп становится ровным.
 *
 * Полосы независимы: каждая ведёт свои суммы и свою пару строк зелёного.
 * Перепад по вертикали на самой верхней строке полосы не считается — на
 * четыре строки из 768 замер резкости не реагирует.
 */
struct cvparams {
	const uint16_t *src;
	uint8_t *yuyv;
	unsigned width, outw, outh, x0, y0;
	unsigned roix0, roix1, roiy0, roiy1;
	int wbR, wbG, wbB, usecm, hithr, stats;
	const int *ccm;
	const uint8_t *gam;
};

struct band {
	const struct cvparams *p;
	unsigned oy0, oy1;
	int *grow, *gprev;
	int64_t sumR, sumG, sumB, hiR, hiG, hiB;
	uint64_t sumY, sharp, roiG;
	unsigned hiN, clipN, roiN;
	int gmax;
	/*
	 * Отступ до конца кэш-линии. Без него счётчики соседних полос попадают
	 * в одну линию, и четыре ядра на каждом пикселе отнимают её друг у
	 * друга: разбор кадра стоил 60 мс процессорного времени вместо 20, а
	 * телефону не хватало сил отрисовать окно, куда эти кадры показывают.
	 */
	char pad[64];
} __attribute__((aligned(64)));

static struct cvparams cvp;
static struct band bands[8];
static int nband = 1, bgen, bleft, bquit;
static pthread_mutex_t bmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bgo = PTHREAD_COND_INITIALIZER, bfin = PTHREAD_COND_INITIALIZER;

/*
 * Цветовая цепочка одной точки: баланс белого -> обесцвечивание светов ->
 * цветовая матрица -> гамма.
 *
 * Вынесена отдельно нарочно: по ней идут оба пути — и видео, и фотография.
 * Если развести их по двум копиям, копии со временем разойдутся, и снимок
 * перестанет совпадать по цвету с тем, что видно в видоискателе.
 */
static inline void pixel_chain(const struct cvparams *p, int rawR, int rawG, int rawB,
			       int *outR, int *outG, int *outB,
			       int *balR, int *balG, int *balB)
{
	int rr = (int)(((int64_t)rawR * p->wbR) >> 16);
	int gg = (int)(((int64_t)rawG * p->wbG) >> 16);
	int bb = (int)(((int64_t)rawB * p->wbB) >> 16);
	int rawmax = rawG > rawR ? rawG : rawR;

	if (rr > 959) rr = 959;
	if (gg > 959) gg = 959;
	if (bb > 959) bb = 959;
	if (rawB > rawmax) rawmax = rawB;
	if (rawmax > 880) {
		int t = ((rawmax - 880) << 8) / 79;
		int mx = rr > gg ? rr : gg;

		if (t > 256) t = 256;
		if (bb > mx) mx = bb;
		rr += ((mx - rr) * t) >> 8;
		gg += ((mx - gg) * t) >> 8;
		bb += ((mx - bb) * t) >> 8;
	}
	*balR = rr; *balG = gg; *balB = bb;
	if (p->usecm) {
		const int *m = p->ccm;
		int cr = (m[0] * rr + m[1] * gg + m[2] * bb) >> 8;
		int cg = (m[3] * rr + m[4] * gg + m[5] * bb) >> 8;
		int cb = (m[6] * rr + m[7] * gg + m[8] * bb) >> 8;

		rr = cr < 0 ? 0 : (cr > 959 ? 959 : cr);
		gg = cg < 0 ? 0 : (cg > 959 ? 959 : cg);
		bb = cb < 0 ? 0 : (cb > 959 ? 959 : cb);
	}
	*outR = p->gam[rr]; *outG = p->gam[gg]; *outB = p->gam[bb];
}

static void convert_band(struct band *b)
{
	const struct cvparams *p = b->p;
	int *grow = b->grow, *gprev = b->gprev;

	int64_t sumR = 0, sumG = 0, sumB = 0, hiR = 0, hiG = 0, hiB = 0;
	uint64_t sumY = 0, sharp = 0, roiG = 0;
	unsigned hiN = 0, clipN = 0, roiN = 0;
	int gmax = 0;

	for (unsigned oy = b->oy0; oy < b->oy1; oy++) {
		const uint16_t *r0 = p->src + (size_t)(2 * (oy + p->y0)) * p->width + 2 * p->x0;
		const uint16_t *r1 = r0 + p->width;
		uint8_t *o = p->yuyv + (size_t)oy * p->outw * 2;
		int mid = (oy > p->roiy0 && oy < p->roiy1);
		int prevG = -1;
		int *gcur = grow;

		for (unsigned ox = 0; ox < p->outw; ox += 2) {
			int Rp[2], Gp[2], Bp[2], Y[2];

			for (int k = 0; k < 2; k++) {
				int p00 = (r0[0] >> 4) - 64;
				int p01 = (r0[1] >> 4) - 64;
				int p10 = (r1[0] >> 4) - 64;
				int p11 = (r1[1] >> 4) - 64;

				r0 += 2; r1 += 2;
				if (p00 < 0) p00 = 0;
				if (p01 < 0) p01 = 0;
				if (p10 < 0) p10 = 0;
				if (p11 < 0) p11 = 0;

				int g = (p00 + p11) >> 1;
				int br, bg, bl;

				pixel_chain(p, p01, g, p10, &Rp[k], &Gp[k], &Bp[k],
					    &br, &bg, &bl);
				if (!p->stats) goto nostats;
				sumR += p01; sumG += g; sumB += p10;
				if (g > gmax) gmax = g;
				if (g >= 900 || p01 >= 900 || p10 >= 900)
					clipN++;
				else if (g >= p->hithr && grayish(br, bg, bl)) {
					/*
					 * «Серость» проверяем по значениям ПОСЛЕ баланса, а
					 * не по сырым. У матрицы зелёных точек вдвое больше,
					 * и ровный нейтральный серый в сыром виде выглядит
					 * зелёным — отбор его отбраковывал, замер оставался
					 * пустым, и баланс не двигался вовсе.
					 */
					hiR += p01; hiG += g; hiB += p10; hiN++;
				}
nostats:
				Y[k] = (77 * Rp[k] + 150 * Gp[k] + 29 * Bp[k]) >> 8;
				if (p->stats) {
					sumY += (unsigned)Y[k];
					gcur[ox + k] = g;
				}
			}
			int U = ((-43 * Rp[0] - 85 * Gp[0] + 128 * Bp[0]) >> 8) + 128;
			int V = ((128 * Rp[0] - 107 * Gp[0] - 21 * Bp[0]) >> 8) + 128;

			if (U < 0) U = 0;
			if (U > 255) U = 255;
			if (V < 0) V = 0;
			if (V > 255) V = 255;
			o[0] = Y[0]; o[1] = U; o[2] = Y[1]; o[3] = V;
			o += 4;

			if (p->stats && mid && ox > p->roix0 && ox < p->roix1) {
				for (int k = 0; k < 2; k++) {
					int g = gcur[ox + k];

					if (prevG >= 0) {
						int dh = g - prevG;

						sharp += (uint64_t)(dh * dh);
					}
					if (oy > b->oy0) {
						int dv = g - gprev[ox + k];

						sharp += (uint64_t)(dv * dv);
					}
					prevG = g;
					roiG += (unsigned)g;
					roiN++;
				}
			}
		}
		{ int *t = gprev; gprev = grow; grow = t; }   /* строки местами */
	}
	b->grow = grow; b->gprev = gprev;
	b->sumR = sumR; b->sumG = sumG; b->sumB = sumB;
	b->hiR = hiR; b->hiG = hiG; b->hiB = hiB;
	b->sumY = sumY; b->sharp = sharp; b->roiG = roiG;
	b->hiN = hiN; b->clipN = clipN; b->roiN = roiN;
	b->gmax = gmax;
}

/*
 * Кто смотрит в /dev/video0 — узнаём в отдельном потоке.
 *
 * Перебор описателей всех процессов на телефоне занимает до четырёхсот
 * миллисекунд. Пока он шёл в главном цикле, поток кадров раз в пару секунд
 * замирал на полсекунды — именно это и выглядело как рывок. Сам по себе
 * ответ нужен раз в секунду и с точностью до «есть зритель или нет», так
 * что пусть считается в стороне.
 */
static volatile int watch_n = 1;
static const char *watch_dev = "/dev/video0";

/*
 * Будильник главного цикла: наблюдатель пишет сюда, когда число зрителей
 * изменилось, и главный цикл выходит из покоя сразу, не дожидаясь срока.
 */
static int watch_efd = -1;

/*
 * Раньше поток перечитывал счётчик открытий раз в 250 мс — четыре
 * пробуждения в секунду круглосуточно, хотя камеру открывают раз в
 * несколько часов. Теперь ждём событий открытия и закрытия самого узла
 * через inotify (проверено: для /dev/video0 OPEN и CLOSE приходят) и
 * пересчитываем только по ним. Раз в полминуты — перепроверка и без
 * события, на случай, если что-то прошло мимо.
 */
static void *wwatch(void *a)
{
	int in, wd;

	(void)a;
	in = inotify_init1(IN_CLOEXEC);
	wd = in >= 0 ? inotify_add_watch(in, watch_dev, IN_OPEN | IN_CLOSE) : -1;
	if (wd < 0) {
		fprintf(stderr, "bf7-camd: inotify недоступен, слежу по старинке\n");
		for (;;) {
			watch_n = watchers(watch_dev);
			usleep(250000);
		}
	}

	for (;;) {
		int n = watchers(watch_dev);

		if (n != watch_n) {
			uint64_t odin = 1;

			watch_n = n;
			if (watch_efd >= 0 && write(watch_efd, &odin, sizeof odin) < 0)
				perror("bf7-camd: будильник");
		}

		struct pollfd p = { .fd = in, .events = POLLIN };

		if (poll(&p, 1, 30000) > 0) {
			char buf[4096];

			if (read(in, buf, sizeof buf) < 0 && errno != EINTR)
				usleep(250000);
		}
	}
	return NULL;
}

/*
 * ---- сон матрицы в покое ----
 *
 * Покой раньше означал «пишем прошлый кадр пореже», но сама матрица и блок
 * захвата продолжали снимать 30 кадров в секунду круглосуточно — ради
 * камеры, в которую никто не смотрит. Теперь в покое матрицу переводим в
 * дежурный режим, а поддерживающий кадр для v4l2loopback отдаём из памяти.
 *
 * Регистр и запись — ровно как у вендорского драйвера этой матрицы
 * (s5k3l6mipiraw_Sensor.c, streaming_control): 16-битная запись в 0x0100,
 * 0x0100 — поток, 0x0000 — дежурный режим. После выключения вендор ждёт
 * конца текущего кадра (check_streamoff); мы просто выжидаем 50 мс.
 *
 * Страховка: если после пробуждения кадры не пошли и блок захвата
 * пришлось перевзводить подряд, сон отключается до перезапуска демона —
 * пусть лучше камера греется, чем не работает.
 */
static int have_frame;          /* в yuyv уже лежит готовый кадр */
static int matrica_spit;        /* матрица в дежурном режиме */
static int matrica_bez_sna;     /* сон отключён страховкой */
static int posle_probuzhdeniya; /* перевзводов подряд после пробуждения */

static void matrica_usypit(void)
{
	ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
	sensor_w16(0x0100, 0x0000);
	usleep(50000);
	matrica_spit = 1;
	fprintf(stderr, "bf7-camd: никто не смотрит — матрица в дежурном режиме\n");
}

static void matrica_razbudit(void)
{
	sensor_w16(0x0100, 0x0100);
	usleep(10000);
	ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
	ispw(SV_MODULE_EN, 0x4000001du);
	ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) | 1u);
	matrica_spit = 0;
	posle_probuzhdeniya = 1;
	fprintf(stderr, "bf7-camd: матрица разбужена\n");
}

/*
 * ---- ручки V4L2 ----
 *
 * Обычная программа настраивает камеру не через посторонний файл, а через
 * VIDIOC_S_CTRL на самом узле. Модуль v4l2loopback у нас пропатчен: он эти
 * ручки объявляет и хранит (см. kernel-work/v4l2loopback), а применяет их
 * тот, кто пишет кадры, то есть мы. Читаем их изредка и реагируем только на
 * изменения — иначе они затирали бы настройки, сделанные через файл команд.
 */
enum {
	C_BRIGHT, C_GAIN, C_AWB, C_WBTEMP,
	C_AEMODE, C_EXPO, C_AF, C_FOCUS, C_N
};

static const unsigned ctl_id[C_N] = {
	V4L2_CID_BRIGHTNESS,
	V4L2_CID_GAIN,
	V4L2_CID_AUTO_WHITE_BALANCE,
	V4L2_CID_WHITE_BALANCE_TEMPERATURE,
	V4L2_CID_EXPOSURE_AUTO,
	V4L2_CID_EXPOSURE_ABSOLUTE,
	V4L2_CID_FOCUS_AUTO,
	V4L2_CID_FOCUS_ABSOLUTE,
};

static int ctl_read(int fd, int v[C_N])
{
	int ok = 0;

	for (int i = 0; i < C_N; i++) {
		struct v4l2_control c = { .id = ctl_id[i] };

		if (ioctl(fd, VIDIOC_G_CTRL, &c) == 0) {
			v[i] = c.value;
			ok++;
		}
	}
	return ok == C_N;
}

/*
 * Цвет свечения по температуре — приближение Таннера Хелланда. Нужен не сам
 * цвет, а наклон: чем выше температура, тем синее свет, тем меньше синего
 * усиления надо и тем больше красного.
 */
static void kelvin_tilt(double T, double *kr, double *kb)
{
	double f = T / 5000.0;

	if (f < 0.4) f = 0.4;
	if (f > 2.0) f = 2.0;
	*kr = pow(f, 0.6);
	*kb = pow(f, -0.6);
}

/*
 * ---- фотография ----
 *
 * Видео и снимок — разные задачи, и требуют разного. Для видео важно успеть
 * в такт, поэтому мозаика Байера сворачивается вдвое: четыре точки матрицы
 * дают одну точку кадра. Для снимка спешить некуда, и выбрасывать три
 * четверти разрешения незачем: разбираем полностью, 2104x1560 вместо
 * 1024x768 — вчетверо больше точек.
 *
 * Второе отличие — шум. На снимке можно сложить несколько кадров подряд:
 * сигнал складывается, а шум растёт как корень, то есть на четырёх кадрах
 * шум падает вдвое. Плата — время: четыре кадра это около 130 мс, и
 * движущееся в кадре смажется. Для вида из окна или предмета на столе это
 * то, что надо.
 *
 * Третье — свет. Перед съёмкой усиление сбрасывается к единице, а
 * недостающее добирается выдержкой: по замерам выдержка даёт настоящий свет
 * (шум растёт как корень из сигнала), а усиление просто множит вместе с
 * шумом.
 *
 * Разбор идёт в отдельном потоке с пониженным приоритетом: поток кадров в
 * /dev/video0 прерывать нельзя, иначе читатели отвалятся.
 */
/*
 * ---- тёмный кадр ----
 *
 * Часть шума в каждом кадре одна и та же: собственное свечение ячеек матрицы
 * и «горячие» точки, которые светятся даже при закрытом объективе. Сложением
 * она не убирается — сколько кадров ни складывай, рисунок в них один и тот
 * же. Зато её можно снять с закрытой камерой и вычесть.
 *
 * Замер на этом телефоне (16 кадров, проверка на тех, что в калибровке не
 * участвовали): один кадр 1,092 -> 0,753, сложенные восемь 0,950 -> 0,399.
 * Самая горячая точка была 479 над фоном, стала 45. Сложение само упиралось
 * в 0,950 и дальше не шло — вместе с вычетом дошло до 0,399.
 *
 * Годится тёмный кадр только для тех условий, в которых снят: собственное
 * свечение растёт и с выдержкой, и с усилением. Поэтому рядом хранятся
 * выдержка и усиление, и вычитаем только если нынешние близки.
 */
struct dark {
	int16_t *pix;                   /* отклонение от чёрного, в единицах матрицы */
	unsigned w, h;
	unsigned expo, gain;
	int есть;
};

static struct dark dark;

static void dark_load(unsigned w, unsigned h)
{
	FILE *f = fopen(DARKFILE, "rb");
	unsigned hw, hh, he, hg;

	if (!f) return;
	if (fscanf(f, "%u %u %u %u\n", &hw, &hh, &he, &hg) != 4 || hw != w || hh != h) {
		fclose(f);
		return;
	}
	free(dark.pix);
	dark.pix = malloc((size_t)w * h * sizeof(int16_t));
	if (!dark.pix) { fclose(f); return; }
	if (fread(dark.pix, sizeof(int16_t), (size_t)w * h, f) != (size_t)w * h) {
		free(dark.pix);
		dark.pix = NULL;
		fclose(f);
		return;
	}
	fclose(f);
	dark.w = w; dark.h = h; dark.expo = he; dark.gain = hg; dark.есть = 1;
	fprintf(stderr, "bf7-camd: тёмный кадр найден (выдержка %u, усиление 0x%x)\n", he, hg);
}

/*
 * Во сколько раз пересчитать тёмный кадр под нынешние условия.
 *
 * Пересчёт идёт по произведению выдержки и усиления, но **только рядом** с
 * условиями калибровки. Измерено: два тёмных кадра, снятые при усилении 189
 * и 43, дают рисунки со схожестью всего 0,44 — то есть это наполовину разные
 * рисунки, и ни один простой закон пересчёта между ними не работает
 * (проверены пересчёты по усилению, по выдержке и по их произведению —
 * измеренный множитель 0,47 против предсказанных 4,40, 0,66 и 2,89).
 *
 * Поэтому окно узкое: калибровку надо снимать для тех условий, в которых
 * собираешься снимать. За пределами окна честнее не вычитать вовсе, чем
 * вычитать наугад. Ноль означает «не подходит».
 */
static double dark_коэф(unsigned expo, unsigned gain)
{
	double k;

	if (!dark.есть || !dark.expo || !dark.gain) return 0;
	k = ((double)expo * gain) / ((double)dark.expo * dark.gain);
	return (k > 0.7 && k < 1.4) ? k : 0;
}

struct shot {
	pthread_mutex_t mx;
	pthread_cond_t go;
	uint32_t *acc;                  /* сумма сырых кадров */
	uint16_t *burst;                /* или все кадры по отдельности */
	int raw;                        /* 1 — отдать серию сырых кадров */
	int тёмный;                     /* 1 — это съёмка тёмного кадра */
	double вычесть;                 /* во сколько раз вычесть тёмный кадр, 0 — не вычитать */
	unsigned nacc;                  /* сколько сложено */
	unsigned w, h;
	struct cvparams p;              /* цвет на момент съёмки */
	unsigned expo, gain;
	char path[256];
	int busy;                       /* разбор идёт */
	int ready;                      /* кадры собраны, можно разбирать */
};

static struct shot shot = {
	.mx = PTHREAD_MUTEX_INITIALIZER,
	.go = PTHREAD_COND_INITIALIZER,
};

/* сырое значение точки матрицы, с зажимом по краю */
static inline int rawat(const uint32_t *a, unsigned w, unsigned h, int y, int x, unsigned n)
{
	int v;

	if (y < 0) y = 0;
	if (x < 0) x = 0;
	if (y >= (int)h) y = (int)h - 1;
	if (x >= (int)w) x = (int)w - 1;
	v = (int)(a[(size_t)y * w + x] / n) - 64;
	return v < 0 ? 0 : v;
}

static void *shot_worker(void *arg)
{
	(void)arg;
	setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
	for (;;) {
		pthread_mutex_lock(&shot.mx);
		while (!shot.ready)
			pthread_cond_wait(&shot.go, &shot.mx);
		shot.ready = 0;
		pthread_mutex_unlock(&shot.mx);

		unsigned w = shot.w, h = shot.h, n = shot.nacc;
		uint32_t *a = shot.acc;
		double t0 = now_s();
		char ppm[300];
		FILE *f;

		if (shot.тёмный) {
			/*
			 * Съёмка тёмного кадра: сохраняем средний как есть,
			 * в единицах матрицы, вместе с условиями съёмки.
			 */
			FILE *t = fopen(DARKFILE, "wb");
			size_t np = (size_t)w * h;
			int16_t *d = malloc(np * sizeof(int16_t));
			double уровень = 0;

			if (d && t) {
				for (size_t i = 0; i < np; i++)
					уровень += (double)a[i] / n;
				уровень /= (double)np;
				for (size_t i = 0; i < np; i++) {
					double v = (double)a[i] / n - уровень;

					d[i] = (int16_t)(v > 3000 ? 3000 : (v < -3000 ? -3000 : v));
				}
				fprintf(t, "%u %u %u %u\n", w, h, shot.expo, shot.gain);
				fwrite(d, sizeof(int16_t), np, t);
				fclose(t);
				free(dark.pix);
				dark.pix = d;
				dark.w = w; dark.h = h;
				dark.expo = shot.expo; dark.gain = shot.gain;
				dark.есть = 1;
				fprintf(stderr,
					"bf7-camd: тёмный кадр снят: уровень %.2f, "
					"выдержка %u, усиление 0x%x\n",
					уровень, shot.expo, shot.gain);
				/*
				 * Уровень заметно выше чёрного означает, что под крышку
				 * попал свет, и калибровка испорчена: вместо рисунка
				 * матрицы в неё записался слабый снимок. Один раз так и
				 * вышло — 68,24 вместо 63,7.
				 */
				if (уровень > 66.0)
					fprintf(stderr,
						"bf7-camd: ВНИМАНИЕ: под крышку попал свет "
						"(уровень %.2f при чёрном около 63,7). "
						"Закройте объектив плотнее и снимите заново.\n",
						уровень);
			} else {
				free(d);
				if (t) fclose(t);
				fprintf(stderr, "bf7-camd: тёмный кадр сохранить не вышло\n");
			}
			free(shot.burst);
			shot.burst = NULL;
			shot.тёмный = 0;
			shot.raw = 0;
			shot.busy = 0;
			continue;
		}

		if (shot.raw) {
			/*
			 * Серия сырых кадров. Складывать их будем не здесь:
			 * когда все кадры целы, на большой машине можно
			 * перебрать любое число складываемых кадров по одному
			 * и тому же сюжету — сравнение выйдет честным, без
			 * разницы в сцене между съёмками.
			 */
			char nm[320];
			size_t fb = (size_t)w * h * sizeof(uint16_t);
			int bad = 0;

			mkdir(shot.path, 0755);
			for (unsigned i = 0; i < n; i++) {
				snprintf(nm, sizeof(nm), "%s/%02u.raw", shot.path, i);
				FILE *r = fopen(nm, "wb");

				if (!r || fwrite(shot.burst + (size_t)i * w * h, 1, fb, r) != fb)
					bad = 1;
				if (r) fclose(r);
			}
			if (dark.есть && dark.w == w && dark.h == h) {
				/*
				 * Сырую серию не трогаем — она на то и сырая. Но тёмный
				 * кадр кладём рядом: пусть разбирающий решит сам,
				 * вычитать его или нет.
				 */
				snprintf(nm, sizeof(nm), "%s/тёмный.raw", shot.path);
				FILE *t = fopen(nm, "wb");

				if (t) {
					fprintf(t, "%u %u %u %u\n", dark.w, dark.h, dark.expo, dark.gain);
					fwrite(dark.pix, sizeof(int16_t), (size_t)w * h, t);
					fclose(t);
				}
			}
			snprintf(nm, sizeof(nm), "%s/опись.txt", shot.path);
			f = fopen(nm, "w");
			if (f) {
				fprintf(f,
					"ширина %u\nвысота %u\nразрядность 10 в младших битах 16\n"
					"чёрное %d\nмозаика GRBG\nкадров %u\n"
					"выдержка %u\nусиление 0x%x\n"
					"баланс %.4f %.4f %.4f\n"
					"матрица %d %d %d %d %d %d %d %d %d\nгамма 2.2\n",
					w, h, 64, n, shot.expo, shot.gain,
					shot.p.wbR / 65536.0, shot.p.wbG / 65536.0,
					shot.p.wbB / 65536.0,
					shot.p.ccm[0], shot.p.ccm[1], shot.p.ccm[2],
					shot.p.ccm[3], shot.p.ccm[4], shot.p.ccm[5],
					shot.p.ccm[6], shot.p.ccm[7], shot.p.ccm[8]);
				fclose(f);
			}
			free(shot.burst);
			shot.burst = NULL;
			fprintf(stderr, "bf7-camd: серия готова: %s (%u кадров по %ux%u, %.1f с)%s\n",
				shot.path, n, w, h, now_s() - t0, bad ? " — часть кадров не записалась" : "");
			shot.raw = 0;
			shot.busy = 0;
			continue;
		}

		if (shot.вычесть) {
			/*
			 * Вычитаем собственное свечение матрицы. Делаем это по
			 * сумме, а не по каждому кадру: результат тот же, а
			 * работы в n раз меньше.
			 */
			size_t np = (size_t)w * h;

			double k = shot.вычесть * (double)n;

			for (size_t i = 0; i < np; i++) {
				long v = (long)a[i] - (long)(dark.pix[i] * k);

				a[i] = v < 0 ? 0 : (uint32_t)v;
			}
			fprintf(stderr, "bf7-camd: вычтен тёмный кадр, множитель %.2f\n",
				shot.вычесть);
		}
		snprintf(ppm, sizeof(ppm), "%s.ppm", shot.path);
		f = fopen(ppm, "wb");
		if (!f) {
			fprintf(stderr, "bf7-camd: снимок некуда писать: %s\n", strerror(errno));
			shot.busy = 0;
			continue;
		}
		fprintf(f, "P6\n%u %u\n255\n", w, h);

		uint8_t *row = malloc((size_t)w * 3);

		if (!row) { fclose(f); shot.busy = 0; continue; }
		/*
		 * Разбор мозаики по-честному, билинейно. Порядок GRBG:
		 * чётная строка G R G R, нечётная B G B G.
		 */
		for (unsigned y = 0; y < h; y++) {
			for (unsigned x = 0; x < w; x++) {
				int R, G, B;
				int c = (int)x, r = (int)y;
				int ye = (y & 1) == 0, xe = (x & 1) == 0;

				if (ye && xe) {              /* зелёная в строке с красными */
					G = rawat(a, w, h, r, c, n);
					R = (rawat(a, w, h, r, c - 1, n) + rawat(a, w, h, r, c + 1, n)) >> 1;
					B = (rawat(a, w, h, r - 1, c, n) + rawat(a, w, h, r + 1, c, n)) >> 1;
				} else if (ye && !xe) {      /* красная */
					R = rawat(a, w, h, r, c, n);
					G = (rawat(a, w, h, r, c - 1, n) + rawat(a, w, h, r, c + 1, n)
					     + rawat(a, w, h, r - 1, c, n) + rawat(a, w, h, r + 1, c, n)) >> 2;
					B = (rawat(a, w, h, r - 1, c - 1, n) + rawat(a, w, h, r - 1, c + 1, n)
					     + rawat(a, w, h, r + 1, c - 1, n) + rawat(a, w, h, r + 1, c + 1, n)) >> 2;
				} else if (!ye && xe) {      /* синяя */
					B = rawat(a, w, h, r, c, n);
					G = (rawat(a, w, h, r, c - 1, n) + rawat(a, w, h, r, c + 1, n)
					     + rawat(a, w, h, r - 1, c, n) + rawat(a, w, h, r + 1, c, n)) >> 2;
					R = (rawat(a, w, h, r - 1, c - 1, n) + rawat(a, w, h, r - 1, c + 1, n)
					     + rawat(a, w, h, r + 1, c - 1, n) + rawat(a, w, h, r + 1, c + 1, n)) >> 2;
				} else {                     /* зелёная в строке с синими */
					G = rawat(a, w, h, r, c, n);
					B = (rawat(a, w, h, r, c - 1, n) + rawat(a, w, h, r, c + 1, n)) >> 1;
					R = (rawat(a, w, h, r - 1, c, n) + rawat(a, w, h, r + 1, c, n)) >> 1;
				}
				int oR, oG, oB, bR, bG, bB;

				pixel_chain(&shot.p, R, G, B, &oR, &oG, &oB, &bR, &bG, &bB);
				row[x * 3 + 0] = (uint8_t)oR;
				row[x * 3 + 1] = (uint8_t)oG;
				row[x * 3 + 2] = (uint8_t)oB;
			}
			fwrite(row, 1, (size_t)w * 3, f);
		}
		free(row);
		fclose(f);

		size_t plen = strlen(shot.path);
		int keep_ppm = plen > 4 && !strcmp(shot.path + plen - 4, ".ppm");

		if (keep_ppm) {
			/*
			 * Просили без сжатия — отдаём PPM как есть. Нужно, когда
			 * снимок разглядывают по точкам: JPEG сам сглаживает
			 * зерно и прячет разницу между одним кадром и стопкой.
			 */
			rename(ppm, shot.path);
			fprintf(stderr, "bf7-camd: снимок готов: %s (%ux%u, сложено %u кадров, %.1f с)\n",
				shot.path, w, h, n, now_s() - t0);
			shot.busy = 0;
			continue;
		}

		char cmd[700];

		snprintf(cmd, sizeof(cmd),
			 "ffmpeg -loglevel error -y -i '%s' -q:v 2 '%s' >/dev/null 2>&1",
			 ppm, shot.path);
		int rc = system(cmd);

		/*
		 * PPM удаляем только если сжатие удалось. Раньше он стирался
		 * всегда, а сообщение обещало «остался PPM» — и снимок пропадал
		 * бесследно. Так и вышло, когда у ffmpeg не оказалось libblas.
		 */
		if (rc == 0)
			unlink(ppm);
		fprintf(stderr, "bf7-camd: снимок готов: %s (%ux%u, сложено %u кадров, %.1f с)%s\n",
			rc == 0 ? shot.path : ppm, w, h, n, now_s() - t0,
			rc == 0 ? "" : " — ffmpeg не справился, оставлен PPM");
		shot.busy = 0;
	}
	return NULL;
}

static void *bworker(void *arg)
{
	int idx = (int)(intptr_t)arg, seen = 0;

	for (;;) {
		pthread_mutex_lock(&bmx);
		while (!bquit && bgen == seen)
			pthread_cond_wait(&bgo, &bmx);
		if (bquit) { pthread_mutex_unlock(&bmx); return NULL; }
		seen = bgen;
		pthread_mutex_unlock(&bmx);

		convert_band(&bands[idx]);

		pthread_mutex_lock(&bmx);
		if (--bleft == 0)
			pthread_cond_signal(&bfin);
		pthread_mutex_unlock(&bmx);
	}
}

static void convert_frame(void)
{
	if (nband > 1) {
		pthread_mutex_lock(&bmx);
		bleft = nband - 1;
		bgen++;
		pthread_cond_broadcast(&bgo);
		pthread_mutex_unlock(&bmx);
	}
	convert_band(&bands[0]);
	if (nband > 1) {
		pthread_mutex_lock(&bmx);
		while (bleft)
			pthread_cond_wait(&bfin, &bmx);
		pthread_mutex_unlock(&bmx);
	}
}

int main(int argc, char **argv)
{
	unsigned width  = opt(argc, argv, "w", 2104);
	unsigned height = opt(argc, argv, "h", 1560);
	unsigned outw   = opt(argc, argv, "outw", 1024);
	unsigned outh   = opt(argc, argv, "outh", 768);
	unsigned target = opt(argc, argv, "target", 110);
		/*
	 * Два буфера по очереди. Само по себе переписывание адреса на лету
	 * железом не подхватывается — но после сброса выгрузки (см. rearm ниже)
	 * новый адрес вступает в силу, и чередование работает: 420 кадров
	 * подряд без единого пустого и без повторов.
	 *
	 * Чередование нужно не ради скорости, а против разрыва кадра: разбор
	 * занимает 24 мс при кадре в 27 мс, и на одном буфере пишущий догонял
	 * читающего — поперёк кадра шёл шов, особенно заметный, когда в этот
	 * миг менялась выдержка.
	 */
	/*
	 * 2026-09-25: чередование сломано — после первого переключения железо
	 * пишет только во второй буфер, новый адрес выгрузки больше не
	 * подхватывается (контрольная сумма буфера 0 всё время нулевая). Каждый
	 * второй кадр в видоискателе был чёрным или старым: мигание и полосы при
	 * движении (заметил хозяин). Теперь по умолчанию один буфер и копия
	 * кадра до перевзвода — см. «копия кадра» в главном цикле.
	 */
	unsigned swapmode = opt(argc, argv, "swap", 0);
		/*
	 * Перевзвод выгрузки после каждого кадра. Без него блок пишет в память
	 * ровно один кадр и замолкает: в состоянии прерываний загорается
	 * IMGO_DROP, покадровый учёт считает буфер занятым, а отдавать его
	 * некому — ядро для CAMSV адрес буфера не трогает вовсе, о чём в нём и
	 * написано прямым текстом («addr should write by MVHDR»). Способ 3 —
	 * штатный сброс выгрузки из драйвера ISP: взвести IMGO_RST_TRIG,
	 * дождаться IMGO_RST_ST, дать SW_RST и снять.
	 */
	unsigned rearm = opt(argc, argv, "rearm", 3);
	int verbose     = (int)opt(argc, argv, "v", 0);
	const char *vdev = opts(argc, argv, "dev", "/dev/video0");

	cached = (int)opt(argc, argv, "cached", 1);
		/*
	 * Двигаем катушку сами, по шине. Через /dev/MAINAF не работает:
	 * AFIOC_T_MOVETO принимается без ошибки, но до чипа ничего не доходит —
	 * проверено полным проходом, резкость не менялась ни на йоту, а те же
	 * положения, записанные по шине, дают настоящую кривую фокуса.
	 * Узел всё равно держим открытым: он включает питание катушки.
	 */
	af_direct = (int)opt(argc, argv, "afdirect", 1);

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);

	/*
	 * Только один демон на телефон. Иначе второй экземпляр не только зря
	 * жжёт ядро, но и ломает логику: держа /dev/video0 открытым, он
	 * выглядит зрителем для первого, и оба крутятся вхолостую; вдобавок
	 * /dev/MAINAF пускает только одного, и второму фокус не достаётся.
	 */
	int lockfd = open("/run/bf7-camd.lock", O_RDWR | O_CREAT, 0644);

	if (lockfd >= 0 && flock(lockfd, LOCK_EX | LOCK_NB) < 0) {
		fprintf(stderr, "bf7-camd: демон уже запущен\n");
		return 1;
	}

	int ispfd = open("/dev/camera-isp", O_RDWR);

	if (ispfd < 0) { perror("/dev/camera-isp"); return 1; }
	imgsys = mmap(NULL, IMGSYS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		      ispfd, IMGSYS_BASE);
	if (imgsys == MAP_FAILED) { perror("mmap регистров ISP"); return 1; }

	/*
	 * Готов ли приёмник, видно не по TG (его включает сам демон, до этого
	 * там ноль), а по размеру кадра, который намерил мультиплексор seninf:
	 * 0x40d14 от базы ISP. Там появляется 0x0838 x 0x0618, как только
	 * сенсор начал передавать.
	 */
#define SENINF_MUX0_FRMSIZE 0x40d14u
	if (ispr(SENINF_MUX0_FRMSIZE) == 0) {
		fprintf(stderr,
			"bf7-camd: приёмник молчит. Сперва запустите bf7-seninf-up.\n");
		return 1;
	}

	unsigned stride = width * 2;
	size_t fsize = (size_t)stride * height;
	struct campbuf buf[2];
	uint16_t *kopiya = NULL;	/* копия кадра при одном буфере */

	ionfd = open("/dev/ion", O_RDONLY);
	if (ionfd < 0) { perror("/dev/ion"); return 1; }
	if (buf_alloc(&buf[0], fsize) || buf_alloc(&buf[1], fsize)) return 1;
	memset(buf[0].va, 0, fsize);
	memset(buf[1].va, 0, fsize);
	if (!swapmode) {
		kopiya = malloc(fsize);
		if (!kopiya) { perror("копия кадра"); return 1; }
	}

	/* ---- блок захвата ---- */
	ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
	ispw(SV_CLK_EN, 0x00008005);       /* такты подблоков CAMSV: без них всё мертво */
	ispw(SV_SW_CTL, 0);
	ispw(SV_IMGO_BASE,   buf[0].mva);
	ispw(SV_IMGO_OFST,   0);
	ispw(SV_IMGO_XSIZE,  stride - 1);
	ispw(SV_IMGO_YSIZE,  height - 1);
	ispw(SV_IMGO_STRIDE, stride);
	ispw(SV_IMGO_FBC,    0);
	/*
	 * Прерывания блока захвата держим выключенными. Нам они не нужны —
	 * состояние мы читаем сами из INT_ST, а биты там взводятся независимо
	 * от маски. Зато включённые прерывания будят обработчик в ядре, и тот
	 * на каждом кадре пишет в журнал по две строки («SV0:Lost done_1» и
	 * «SV0:Err IRQ»), потому что перевзвод выгрузки он считает потерей
	 * кадра. Шестьдесят строк в секунду заливали журнал так, что systemd
	 * не успевал их разбирать и крутился на целом ядре.
	 */
	ispw(SV_INT_EN,      0x00000000);
	ispw(SV_MODULE_EN,   0x4000001du);
	ispw(SV_TG_VF_CON,   ispr(SV_TG_VF_CON) | 1u);

	/* ---- выход ---- */
	int vfd = open(vdev, O_RDWR);

	if (vfd < 0) { perror(vdev); return 1; }

	struct v4l2_format vf;

	memset(&vf, 0, sizeof(vf));
	vf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vf.fmt.pix.width  = outw;
	vf.fmt.pix.height = outh;
	vf.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	vf.fmt.pix.field = V4L2_FIELD_NONE;
	vf.fmt.pix.bytesperline = outw * 2;
	vf.fmt.pix.sizeimage = outw * outh * 2;
	vf.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	if (ioctl(vfd, VIDIOC_S_FMT, &vf) < 0) { perror("VIDIOC_S_FMT"); return 1; }

	uint8_t *yuyv = malloc((size_t)outw * outh * 2);
	unsigned hw = width / 2, hh = height / 2;

	if (outw > hw) outw = hw;
	if (outh > hh) outh = hh;
	if (!yuyv) { fprintf(stderr, "не хватило памяти\n"); return 1; }

	unsigned x0 = (hw - outw) / 2, y0 = (hh - outh) / 2;
	/*
	 * Область, по которой судим о резкости. Центральная треть оказалась
	 * слишком узкой: мишень для наводки лежала ниже неё, и в замер попадал
	 * ровный кусок стола — то есть нечего было мерить, и число не менялось
	 * при любом положении линзы. Берём середину в 60% кадра.
	 */
	unsigned roix0 = outw / 5, roix1 = outw - outw / 5;
	unsigned roiy0 = outh / 5, roiy1 = outh - outh / 5;

	/*
	 * Цветовая матрица. У матрицы камеры светофильтры перекрываются: чистый
	 * зелёный засвечивает и красные, и синие точки, поэтому без поправки
	 * цвета выходят блёклыми, а красный отдаёт в оранжевый. Числа посчитаны
	 * по снимку палитры (чистые красный, зелёный, синий и белый на экране
	 * другого телефона, 2026-09-17): решена задача наименьших квадратов с
	 * условием, что сумма каждой строки равна единице — тогда серое
	 * остаётся серым и баланс белого матрица не трогает.
	 *
	 * Множители в 1/256. Применять к линейным значениям, ДО гаммы.
	 */
	static const int ccm[9] = {
		 486, -236,    6,
		 -39,  348,  -53,
		  19, -159,  396,
	};
	int usecm = (int)opt(argc, argv, "ccm", 1);

	/* гамма: без неё кадр выглядит почти чёрным */
	static uint8_t gam[1024];

	for (int i = 0; i < 1024; i++) {
		double v = i / 959.0;

		if (v > 1) v = 1;
		gam[i] = (uint8_t)(255.0 * pow(v, 1.0 / 2.2) + 0.5);
	}

	/* ---- разбор кадра по ядрам ---- */
	nband = (int)opt(argc, argv, "threads", 3);
	{
		long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

		if (nband < 1) nband = 1;
		if (ncpu > 0 && nband > (int)ncpu) nband = (int)ncpu;
		if (nband > (int)(sizeof(bands) / sizeof(bands[0])))
			nband = (int)(sizeof(bands) / sizeof(bands[0]));
	}
	watch_dev = vdev;
	{
		pthread_t t;

		watch_efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
		if (!pthread_create(&t, NULL, wwatch, NULL))
			pthread_detach(t);
	}
	for (int i = 1; i < nband; i++) {
		pthread_t t;

		if (pthread_create(&t, NULL, bworker, (void *)(intptr_t)i)) {
			fprintf(stderr, "bf7-camd: поток %d не завёлся, считаю в %d\n", i, i);
			nband = i;
			break;
		}
		pthread_detach(t);
	}
	cvp.width = width; cvp.outw = outw; cvp.outh = outh;
	cvp.x0 = x0; cvp.y0 = y0;
	cvp.roix0 = roix0; cvp.roix1 = roix1;
	cvp.roiy0 = roiy0; cvp.roiy1 = roiy1;
	cvp.ccm = ccm; cvp.gam = gam; cvp.yuyv = yuyv;
	dark_load(width, height);
	shot.acc = malloc((size_t)width * height * sizeof(uint32_t));
	if (!shot.acc) { fprintf(stderr, "не хватило памяти под снимок\n"); return 1; }
	{
		pthread_t t;

		if (!pthread_create(&t, NULL, shot_worker, NULL))
			pthread_detach(t);
	}

	for (int i = 0; i < nband; i++) {
		bands[i].p = &cvp;
		bands[i].oy0 = outh * (unsigned)i / (unsigned)nband;
		bands[i].oy1 = outh * (unsigned)(i + 1) / (unsigned)nband;
		bands[i].grow = calloc(outw + 2, sizeof(int));
		bands[i].gprev = calloc(outw + 2, sizeof(int));
		if (!bands[i].grow || !bands[i].gprev) {
			fprintf(stderr, "не хватило памяти\n");
			return 1;
		}
	}

	/* ---- автоматика ---- */
	unsigned expo = 1200, gain = 0x0060, flen = 3260;
	const unsigned gain_max = 0x0300, gain_min = 0x0020;
	/*
	 * Потолок длины кадра. 19560 проверено замером: выдержка 19548 даёт
	 * ровно вдвое больше сигнала при шуме в 1,35 раза — то есть это
	 * настоящий свет (дробовой шум растёт как корень), а не домножение
	 * внутри сенсора. Частота кадров при этом не меняется: у сенсора с
	 * бегущим затвором накопление может быть длиннее кадра, волн
	 * накопления просто несколько сразу. Выше не ходить: 40000 вешает
	 * сенсор намертво, спасает только перезагрузка.
	 */
	const unsigned flen_min = 3260, flen_max = 19560;
	/*
	 * Ручной потолок выше автоматического. 19560 в замерах работало, 40000
	 * вешало сенсор намертво — поэтому автоматике оставлен прежний предел,
	 * а руками можно дойти до проверенной границы и посмотреть, даёт ли
	 * выдержка длиннее кадра настоящий свет или сенсор домножает сам.
	 */
	const unsigned flen_hard = 19560;   /* руками — не выше проверенного */   /* до 10 кадров в секунду */
	/*
	 * Потолок длины кадра для видоискателя. Длина кадра делит такт матрицы
	 * (замер 2026-09-25 после починки буферов: 3260 — свежий почти каждый
	 * кадр, 6520 — 72%, 9780 — 45%, 19560 — 22%, то есть ~5 кадров/с;
	 * прежний вывод «такт не меняется» был снят со сломанным чередованием
	 * буферов, где старые кадры считались новыми). В темноте видоискатель
	 * тормозил. Теперь он не растягивает кадр дальше flen_view, а недобор
	 * света копит в ae_extra — «виртуальную» длину кадра, которую получит
	 * снимок: там кадр и так поднимается до flen_max.
	 */
	const unsigned flen_view = opt(argc, argv, "vflen", 6520);
	double ae_extra = 1.0;
	unsigned expo_max = flen - 12;
	/*
	 * Баланс белого в 16.16. Считаем «в среднем серое», но затем делим все
	 * три множителя на наибольший — чтобы ни один не был больше единицы.
	 * Если вместо этого задирать слабые каналы, светлые места упираются в
	 * потолок и белая бумага уходит в цвет: у нас так получался зелёный.
	 * Недостачу яркости добирает выдержка, ей это по силам.
	 */
	int wbR = 65536, wbG = 65536, wbB = 65536;

	unsigned sweep = opt(argc, argv, "sweep", 0);

	/*
	 * AF_WAIT — первая ступень: ждём, пока выдержка наберётся. Наводка
	 * сравнивает кадры между собой, а в темноте сравнивать нечего: перепады
	 * тонут в шуме, и любой ход линзы даёт одно и то же число.
	 */
	enum { AF_WAIT, AF_COARSE, AF_FINE, AF_LOCK, AF_SWEEP } afst = AF_WAIT;
	static const int af_coarse[] = { 0, 96, 192, 288, 384, 480, 576, 672, 768, 864, 960 };
	int af_i = 0, af_best = 240, af_fine_base = 0, af_fine_i = 0;
	/*
	 * Пауза после шага линзы отсчитывается по времени, а не по кадрам.
	 * Раньше ждали три кадра; когда поток поднялся с 30 до 60 кадров в
	 * секунду, эти три кадра превратились в 50 мс — мотор DW9714 за такое
	 * время до места не доходит, и резкость мерилась на летящей линзе.
	 * Перебор давал случайный максимум, и наводка принималась рыскать.
	 */
	double af_hold = 0, af_cool = 0, af_low = 0;
	const double af_wait = opt(argc, argv, "settle", 130) / 1000.0;
	double af_bestval = -1, af_lockval = 0;
	int af_lost = 0;

	/* что было в прошлый раз */
	int saved_focus = -1;

	{
		FILE *f = fopen(STATEFILE, "r");

		if (f) {
			unsigned sf, se, sg, sl;

			unsigned sr = 0, sgn = 0, sb = 0;
			int nread = fscanf(f, "%u %u %u %u %u %u %u",
					   &sf, &se, &sg, &sl, &sr, &sgn, &sb);

			if (nread >= 7) {
				if (sr > 6553 && sr <= 65536) wbR = (int)sr;
				if (sgn > 6553 && sgn <= 65536) wbG = (int)sgn;
				if (sb > 6553 && sb <= 65536) wbB = (int)sb;
			}
			if (nread >= 4) {
				if (sf <= 1023) saved_focus = (int)sf;
				if (se >= 8 && se < 10000) expo = se;
				if (sg >= gain_min && sg <= gain_max) gain = sg;
				if (sl >= flen_min && sl <= flen_max) flen = sl;
				if (flen > flen_view) {
					ae_extra = (double)flen / flen_view;
					flen = flen_view;
				}
				expo_max = flen - 12;
				if (expo > expo_max) expo = expo_max;
			}
			fclose(f);
		}
	}

	i2cfd = open("/dev/i2c-2", O_RDWR);
	sensor_w16(0x0340, flen);
	sensor_w16(0x0202, expo);
	sensor_w16(0x0204, gain);

	affd = open("/dev/MAINAF", O_RDWR);
	if (affd >= 0) {
		struct stAF_MotorName n;

		memset(&n, 0, sizeof(n));
		strncpy(n.uMotorName, "DW9714AF", sizeof(n.uMotorName) - 1);
		ioctl(affd, AFIOC_S_SETDRVNAME, &n);
		af_moveto(saved_focus >= 0 ? (unsigned)saved_focus : (unsigned)af_coarse[0]);
		if (saved_focus >= 0)
			fprintf(stderr, "bf7-camd: начинаю с прошлой наводки %d\n", saved_focus);
	} else {
		afst = AF_LOCK;
		sweep = 0;
		fprintf(stderr, "bf7-camd: линза недоступна, фокус наводиться не будет\n");
	}

	fprintf(stderr, "bf7-camd: %ux%u -> %s, кадр приёмника 0x%08x, захват 0x%08x, буферы %s\n",
		outw, outh, vdev, ispr(SENINF_MUX0_FRMSIZE), ispr(SV_TG_FRMSIZE),
		cached ? "кэшируемые" : "без кэша");

	int cur = 0;
	unsigned long frames = 0, shown = 0, empty = 0, same = 0;
	double t0 = now_s(), tconv = 0;

	int idle = 0;
	double t_check = 0;
	/*
	 * Пауза после команды сенсору. Выдержка и усиление вступают в силу через
	 * два-три кадра: сенсор уже начал копить свет, когда команда приходит.
	 * Пока поток шёл 22 кадра в секунду, замер раз в три кадра успевал
	 * захватить новое состояние. На 49 кадрах три кадра — это 60 мс, меньше
	 * задержки сенсора: автоматика мерила ещё старый кадр, считала, что её
	 * не послушались, и добавляла ещё. Усиление металось от 0x20 до 0x300 и
	 * обратно — картинка мигала. Теперь после каждой команды ждём, пока она
	 * доедет.
	 */
	double ae_hold = 0;
	int man_flen = 0;
	enum { SH_IDLE, SH_PREP, SH_GRAB } shst = SH_IDLE;
	int sh_wait = 0, sh_left = 0, sh_total = 4;
	unsigned sh_expo = 0, sh_gain = 0, sh_flen = 0;
	char sh_path[256] = "";
	int ctl[C_N], ctl_prev[C_N];
	int ctl_ok = 0;
	int wbR_ref = 65536, wbG_ref = 65536, wbB_ref = 65536;
	double mx_wait = 0, mx_conv = 0, mx_wr = 0, mx_rest = 0, t_seg = 0;
	double tnoise = 0;
	/*
	 * Ровный шаг выдачи.
	 *
	 * Ожидание кадра в замерах всегда ноль: железо снимает быстрее, чем мы
	 * разбираем, и готовый кадр лежит наготове всегда. Значит темп задаём
	 * мы, а он гуляет вместе с временем разбора — от 17 до 28 мс. Наружу
	 * это и есть неровное движение. Раз кадр всегда есть, выдаём его по
	 * часам: ровно тридцать раз в секунду. Заодно освобождается половина
	 * ядер — телефону это не лишнее.
	 */
	double outfps = opt(argc, argv, "fps", 30), next_slot = 0;
	int man_focus = -1, man_expo = 0, man_gain = 0, man_wb = 0;

	while (!stop_now) {
		int waited = 0;

		/* пока никто не смотрит — спим и не жжём ядро */
		/* ---- команды от человека ---- */
		/*
		 * Перебор описателей всех процессов стоит десятки миллисекунд — на такт кадра это
		 * заметно. Пока кто-то смотрит, спрашиваем редко; в покое можно чаще:
		 * там всё равно два кадра в секунду.
		 */
		if (now_s() - t_check > 0.1) {
			FILE *cf = fopen(CMDFILE, "r");

			if (cf) {
				char line[512];

				while (fgets(line, sizeof(line), cf)) {
					/*
					 * Первый разбор был на 31 байт, и путь к снимку в него
					 * не влезал: кириллица в имени папки занимает по два
					 * байта на букву, и «/root/фото/...» обрезалось молча.
					 */
					char w[32], a[256], b[32], c[32];
					int n = sscanf(line, "%31s %255s %31s %31s", w, a, b, c);

					if (n < 2) continue;
					if (cmd_word(w, "фокус", "focus")) {
						if (cmd_word(a, "авто", "auto")) {
							man_focus = -1;
							afst = AF_COARSE;
							af_i = 0; af_bestval = -1; af_hold = now_s() + af_wait;
							af_moveto((unsigned)af_coarse[0]);
						} else {
							man_focus = atoi(a);
							if (man_focus < 0) man_focus = 0;
							if (man_focus > 1023) man_focus = 1023;
							afst = AF_LOCK;
							af_lockval = 0;
							af_moveto((unsigned)man_focus);
						}
						fprintf(stderr, "bf7-camd: фокус -> %s\n", a);
					} else if (cmd_word(w, "выдержка", "exposure")) {
						if (cmd_word(a, "авто", "auto")) {
							man_expo = 0;
						} else {
							man_expo = atoi(a);
							if (man_expo < 8) man_expo = 8;
							if ((unsigned)man_expo > expo_max) man_expo = (int)expo_max;
							expo = (unsigned)man_expo;
							sensor_w16(0x0202, expo);
						}
						fprintf(stderr, "bf7-camd: выдержка -> %s\n", a);
					} else if (cmd_word(w, "усиление", "gain")) {
						if (cmd_word(a, "авто", "auto")) {
							man_gain = 0;
						} else {
							man_gain = (int)strtoul(a, NULL, 0);
							gain = (unsigned)man_gain;
							sensor_w16(0x0204, gain);
						}
						fprintf(stderr, "bf7-camd: усиление -> %s\n", a);
					} else if (cmd_word(w, "темнота", "dark")) {
						if (shot.busy || shst != SH_IDLE) {
							fprintf(stderr, "bf7-camd: съёмка уже идёт\n");
						} else {
							sh_total = atoi(a) > 0 ? atoi(a) : 16;
							if (sh_total > 32) sh_total = 32;
							sh_expo = expo; sh_gain = gain; sh_flen = flen;
							/*
							 * Тёмный кадр надо снимать ровно там, куда
							 * приходит съёмка фотографии в этой же сцене.
							 *
							 * Сначала я заставлял его сниматься при усилении
							 * в единицу — и калибровка не подходила ни к
							 * чему: в темноте съёмка упирается в предел
							 * выдержки и оставляет усиление большим. Рисунок
							 * же от усиления зависит сильно и не по простому
							 * закону (измерено: при усилении 189 и 43
							 * рисунки схожи всего на 0,44). Поэтому здесь
							 * тот же перенос света, что и при съёмке фото.
							 */
							{
								double light = (double)expo * (double)gain;

								flen = flen_max;
								sensor_w16(0x0340, flen);
								expo_max = flen - 12;
								gain = gain_min;
								double e = light / (double)gain;

								if (e > (double)expo_max) {
									e = expo_max;
									double g2 = light / e;

									gain = g2 > (double)gain_max
										? gain_max : (unsigned)g2;
								}
								if (e < 8) e = 8;
								expo = (unsigned)e;
								sensor_w16(0x0202, expo);
								sensor_w16(0x0204, gain);
							}
							shot.тёмный = 1;
							shot.raw = 0;
							unlink(GRABFILE);
							shst = SH_PREP;
							sh_wait = 5;
							fprintf(stderr,
								"bf7-camd: снимаю тёмный кадр, %d кадров, выдержка %u, усиление 0x%x\n",
								sh_total, expo, gain);
						}
					} else if (cmd_word(w, "серия", "burst")) {
						if (shot.busy || shst != SH_IDLE) {
							fprintf(stderr, "bf7-camd: съёмка уже идёт\n");
						} else {
							sh_total = n >= 3 ? atoi(b) : 16;
							if (sh_total < 1) sh_total = 1;
							if (sh_total > 32) sh_total = 32;
							free(shot.burst);
							shot.burst = malloc((size_t)sh_total * width * height
									    * sizeof(uint16_t));
							if (!shot.burst) {
								fprintf(stderr, "bf7-camd: не хватило памяти на серию\n");
							} else {
								snprintf(sh_path, sizeof(sh_path), "%s", a);
								/*
								 * Выдержку и усиление не трогаем: серия
								 * снимается ради замера шума, и мерить
								 * надо то, что автоматика выбрала сама.
								 */
								sh_expo = expo;
								sh_gain = gain;
								sh_flen = flen;
								shot.raw = 1;
								unlink(GRABFILE);
								shst = SH_PREP;
								sh_wait = 5;
								fprintf(stderr,
									"bf7-camd: серия %s, кадров %d, выдержка %u, усиление 0x%x\n",
									sh_path, sh_total, expo, gain);
							}
						}
					} else if (cmd_word(w, "фото", "photo")) {
						if (shot.busy || shst != SH_IDLE) {
							fprintf(stderr, "bf7-camd: снимок уже делается\n");
						} else {
							snprintf(sh_path, sizeof(sh_path), "%s", a);
							sh_total = n >= 3 ? atoi(b) : 4;
							if (sh_total < 1) sh_total = 1;
							if (sh_total > 16) sh_total = 16;
							/*
							 * Свет перекладываем с усиления на выдержку:
							 * общее количество света то же, но шум меньше.
							 * Длину кадра поднимаем до предела режима,
							 * чтобы выдержке было куда расти.
							 */
							sh_expo = expo; sh_gain = gain; sh_flen = flen;
							double light = (double)expo * (double)gain * ae_extra;

							flen = flen_max;
							sensor_w16(0x0340, flen);
							expo_max = flen - 12;
							gain = gain_min;
							double e = light / (double)gain;

							if (e > (double)expo_max) {
								e = expo_max;
								double g2 = light / e;

								gain = g2 > (double)gain_max ? gain_max : (unsigned)g2;
							}
							if (e < 8) e = 8;
							expo = (unsigned)e;
							sensor_w16(0x0202, expo);
							sensor_w16(0x0204, gain);
							unlink(GRABFILE);
							shst = SH_PREP;
							sh_wait = 5;
							fprintf(stderr,
								"bf7-camd: снимаю %s, кадров %d, выдержка %u, усиление 0x%x\n",
								sh_path, sh_total, expo, gain);
						}
					} else if (cmd_word(w, "длина", "flen")) {
						if (cmd_word(a, "авто", "auto")) {
							man_flen = 0;
						} else {
							/*
							 * Держать в пределах режима обязательно.
							 * Запись 40000 в 0x0340 вешает сенсор
							 * намертво: поток идёт, кадры приходят, но
							 * все чёрные, и ни перезапуск андроидной
							 * части, ни возврат прежней длины не
							 * помогают — спасает только перезагрузка
							 * телефона (проверено 2026-09-18).
							 */
							long v = atol(a);

							if (v < (long)flen_min) v = flen_min;
							if (v > (long)flen_hard) v = flen_hard;
							man_flen = 1;
							flen = (unsigned)v;
							sensor_w16(0x0340, flen);
							expo_max = flen - 12;
							if (expo > expo_max) {
								expo = expo_max;
								sensor_w16(0x0202, expo);
							}
						}
						fprintf(stderr, "bf7-camd: длина кадра -> %s\n", a);
					} else if (cmd_word(w, "кадры", "rate")) {
						outfps = atof(a);
						next_slot = 0;
						fprintf(stderr, "bf7-camd: выдача %.0f кадров в секунду\n", outfps);
					} else if (cmd_word(w, "яркость", "target")) {
						target = (unsigned)atoi(a);
						fprintf(stderr, "bf7-camd: целевая яркость -> %u\n", target);
					} else if (cmd_word(w, "баланс", "wb")) {
						if (cmd_word(a, "авто", "auto")) {
							man_wb = 0;
						} else if (n >= 4) {
							man_wb = 1;
							wbR = (int)(atof(a) * 65536);
							wbG = (int)(atof(b) * 65536);
							wbB = (int)(atof(c) * 65536);
						}
						fprintf(stderr, "bf7-camd: баланс -> %s\n", a);
					}
				}
				fclose(cf);
				unlink(CMDFILE);
			}

			/* ---- ручки V4L2 ---- */
			if (ctl_read(vfd, ctl)) {
				if (!ctl_ok) {
					/*
					 * За точку отсчёта берём значения по умолчанию, а
					 * не текущие. Если демон перезапустился, когда
					 * программа уже выключила самонаводку, «текущее»
					 * совпало бы с прошлым и мы бы ничего не применили:
					 * ручка говорит «вручную», а демон наводится сам.
					 */
					static const int def[C_N] = {
						110, 32, 1, 5000, V4L2_EXPOSURE_AUTO, 33, 1, 384
					};

					memcpy(ctl_prev, def, sizeof(ctl_prev));
					ctl_ok = 1;
				}
				if (ctl[C_BRIGHT] != ctl_prev[C_BRIGHT]) {
					target = (unsigned)ctl[C_BRIGHT];
					fprintf(stderr, "bf7-camd: ручка яркости -> %u\n", target);
				}
				if (ctl[C_AEMODE] != ctl_prev[C_AEMODE]
				    || ctl[C_EXPO] != ctl_prev[C_EXPO]
				    || ctl[C_GAIN] != ctl_prev[C_GAIN]) {
					if (ctl[C_AEMODE] == V4L2_EXPOSURE_MANUAL) {
						/*
						 * V4L2 задаёт выдержку в сотнях микросекунд,
						 * сенсор — в своих единицах. Единицу берём из
						 * длины кадра и настоящего такта: кадр в 3260
						 * единиц идёт 1/54 с, то есть единица — 5,7 мкс.
						 */
						double unit_us = (1e6 / 54.0) / (double)flen_min;
						long e = (long)(ctl[C_EXPO] * 100.0 / unit_us);

						if (e < 8) e = 8;
						if ((unsigned)e > expo_max) e = expo_max;
						man_expo = (int)e;
						expo = (unsigned)e;
						man_gain = ctl[C_GAIN];
						gain = (unsigned)ctl[C_GAIN];
						sensor_w16(0x0202, expo);
						sensor_w16(0x0204, gain);
						fprintf(stderr, "bf7-camd: ручка выдержки -> %u, усиление 0x%x\n",
							expo, gain);
					} else if (man_expo || man_gain) {
						man_expo = man_gain = 0;
						fprintf(stderr, "bf7-camd: выдержка снова сама\n");
					}
				}
				if (ctl[C_AF] != ctl_prev[C_AF] || ctl[C_FOCUS] != ctl_prev[C_FOCUS]) {
					if (ctl[C_AF]) {
						if (man_focus >= 0) {
							man_focus = -1;
							afst = AF_COARSE;
							af_i = 0; af_bestval = -1;
							af_hold = now_s() + af_wait;
							af_moveto((unsigned)af_coarse[0]);
							fprintf(stderr, "bf7-camd: наводка снова сама\n");
						}
					} else {
						man_focus = ctl[C_FOCUS];
						if (man_focus < 0) man_focus = 0;
						if (man_focus > 1023) man_focus = 1023;
						afst = AF_LOCK;
						af_lockval = 0;
						af_moveto((unsigned)man_focus);
						fprintf(stderr, "bf7-camd: ручка наводки -> %d\n", man_focus);
					}
				}
				if (ctl[C_AWB] != ctl_prev[C_AWB] || ctl[C_WBTEMP] != ctl_prev[C_WBTEMP]) {
					if (ctl[C_AWB]) {
						man_wb = 0;
						fprintf(stderr, "bf7-camd: баланс снова сам\n");
					} else {
						/*
						 * За точку отсчёта берём то, к чему сошёлся
						 * самостоятельный баланс: у матрицы свои
						 * чувствительности по каналам, и чистая формула
						 * по температуре без них промахнулась бы сильно.
						 */
						double kr, kb, mx;

						kelvin_tilt((double)ctl[C_WBTEMP], &kr, &kb);
						double r = wbR_ref * kr, g = wbG_ref, b = wbB_ref * kb;

						mx = r > g ? r : g;
						if (b > mx) mx = b;
						man_wb = 1;
						wbR = (int)(r * 65536 / mx);
						wbG = (int)(g * 65536 / mx);
						wbB = (int)(b * 65536 / mx);
						fprintf(stderr, "bf7-camd: ручка баланса -> %d K\n", ctl[C_WBTEMP]);
					}
				}
				memcpy(ctl_prev, ctl, sizeof(ctl));
			}

			int w = watch_n;

			t_check = now_s();
			if (w == 0 && !idle) {
				idle = 1;
				if (verbose) fprintf(stderr, "bf7-camd: никто не смотрит, сбавляю ход\n");
			} else if (w > 0 && idle) {
				idle = 0;
				if (man_focus < 0) {
					afst = AF_COARSE;
					af_i = 0; af_bestval = -1; af_hold = now_s() + af_wait;
					af_moveto((unsigned)af_coarse[0]);
				}
				if (verbose) fprintf(stderr, "bf7-camd: камеру открыли, полный ход\n");
			}
		}
		/*
		 * В покое поток не останавливаем совсем, а лишь сбавляем до пары
		 * кадров в секунду. Причина не в лени: v4l2loopback объявляет узел
		 * камерой только пока в него кто-то пишет. Если замолчать
		 * полностью, следующая программа получит «Not a video capture
		 * device» и уйдёт, так и не дав нам повода проснуться.
		 */
		/*
		 * Холостой ход дробим на кусочки и выходим из него сразу, как
		 * появился зритель. Сплошные 400 мс означали, что первая секунда
		 * после открытия камеры шла рывками: программа уже читает, а мы
		 * ещё спим. Ровно эти 433 мс и торчали в замерах как наибольший
		 * провал.
		 */
		/*
		 * В покое ждём будильника наблюдателя (появился зритель) или
		 * секунды — тогда отдаём поддерживающий кадр. Прежде тут было
		 * восемь снов по 50 мс ради быстрой реакции: двадцать пробуждений
		 * в секунду. Будильник даёт реакцию быстрее, а спим целиком.
		 * Поддерживающий кадр раз в секунду вместо двух с половиной:
		 * v4l2loopback держит узел камерой, пока писатель в потоке, а
		 * частота записи ему не важна.
		 */
		if (idle && watch_n == 0 && !stop_now) {
			if (watch_efd >= 0) {
				struct pollfd p = { .fd = watch_efd, .events = POLLIN };

				if (poll(&p, 1, 1000) > 0) {
					uint64_t v;

					if (read(watch_efd, &v, sizeof v) < 0)
						perror("bf7-camd: будильник");
				}
			} else {
				for (int i = 0; i < 8 && watch_n == 0 && !stop_now; i++)
					usleep(50000);
			}
		}

		/*
		 * Сон матрицы. Усыпляем, только когда в памяти уже есть кадр
		 * (иначе нечего отдавать) и не идёт съёмка фото. Будим, как только
		 * появился зритель или попросили снимок.
		 */
		if (idle && have_frame && shst == SH_IDLE && !matrica_spit
		    && !matrica_bez_sna)
			matrica_usypit();
		if (matrica_spit && (!idle || shst != SH_IDLE))
			matrica_razbudit();
		if (matrica_spit) {
			if (write(vfd, yuyv, (size_t)outw * outh * 2) > 0)
				shown++;
			continue;
		}

		/*
		 * Готовность кадра ловим по покадровому учёту, а не по
		 * прерываниям: в поле 20..23 регистра FBC железо считает, сколько
		 * буферов занято. Стало не ноль — кадр в памяти. Прерывания при
		 * этом выключены нарочно (см. INT_EN выше).
		 */
		if (outfps > 0 && !idle) {
			double now = now_s();

			if (next_slot == 0) next_slot = now;
			if (now < next_slot)
				usleep((useconds_t)((next_slot - now) * 1e6));
			next_slot += 1.0 / outfps;
			if (next_slot < now_s() - 0.3)   /* отстали — не догонять рывком */
				next_slot = now_s();
		} else {
			next_slot = 0;
		}

		if (t_seg > 0) {
			double d = now_s() - t_seg;

			if (d > mx_rest) mx_rest = d;
		}
		t_seg = now_s();
		while (!stop_now) {
			if (((ispr(SV_IMGO_FBC) >> 20) & 0xf) != 0) break;
			usleep(1000);
			if (++waited > 2000) break;
		}
		if (stop_now) break;
		if (waited > 2000) {
			fprintf(stderr, "bf7-camd: кадры пропали, перевзвожу блок захвата\n");
			if (posle_probuzhdeniya && ++posle_probuzhdeniya > 3) {
				fprintf(stderr, "bf7-camd: после сна матрицы кадры не идут — "
					"сон отключён до перезапуска демона\n");
				matrica_bez_sna = 1;
				posle_probuzhdeniya = 0;
				sensor_w16(0x0100, 0x0100);
			}
			ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
			usleep(50000);
			ispw(SV_MODULE_EN, 0x4000001du);
			ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) | 1u);
			continue;
		}

		{
			double d = now_s() - t_seg;

			if (d > mx_wait) mx_wait = d;
		}
		posle_probuzhdeniya = 0;    /* кадр пришёл — матрица проснулась как надо */
		int done = cur;

		/*
		 * Стрелку на свободный буфер переводим ДО взвода выгрузки.
		 *
		 * Раньше было наоборот: перевзвод включал выгрузку, пока адрес ещё
		 * указывал на тот самый буфер, который мы сейчас будем разбирать, и
		 * только потом адрес менялся. В это окно железо успевало начать
		 * кадр — и писало поверх разбираемого.
		 */
		if (swapmode)
			cur ^= 1;

		/*
		 * Копия кадра — до перевзвода. Дописав кадр, блок захвата стоит,
		 * пока его не взведут; значит, скопировав буфер сейчас (~5 мс
		 * против ~25 мс разбора), мы держим целый кадр, а железо пишет
		 * следующий уже в тот же буфер, никому не мешая: ни разрыва, ни
		 * старых кадров, как при двух буферах.
		 */
		if (!swapmode) {
			buf_invalidate(&buf[done], fsize);
			memcpy(kopiya, buf[done].va, fsize);
		}

		/*
		 * Блок захвата пишет один кадр и замолкает: в состоянии прерываний
		 * загорается IMGO_DROP, а покадровый учёт (FBC) показывает, что
		 * буфер занят и отдавать его некому. Ядро для CAMSV адрес буфера не
		 * трогает вовсе («addr should write by MVHDR»), значит взводить
		 * выгрузку заново приходится самим. Что именно помогает — перебираем.
		 */
		switch (rearm) {
		case 1:
			ispw(SV_MODULE_EN, 0x40000000u);
			ispw(SV_IMGO_BASE, buf[cur].mva);
			ispw(SV_MODULE_EN, 0x4000001du);
			break;
		case 2:
			ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
			ispw(SV_MODULE_EN, 0x40000000u);
			ispw(SV_IMGO_BASE, buf[cur].mva);
			ispw(SV_MODULE_EN, 0x4000001du);
			ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) | 1u);
			break;
		case 3:
			ispw(SV_SW_CTL, 1);                 /* сброс выгрузки */
			for (int k = 0; k < 100 && !(ispr(SV_SW_CTL) & 2); k++)
				;
			ispw(SV_SW_CTL, 4);
			ispw(SV_SW_CTL, 0);
			ispw(SV_IMGO_BASE, buf[cur].mva);
			ispw(SV_MODULE_EN, 0x4000001du);
			break;
		case 4:
			ispw(SV_IMGO_BASE, buf[cur].mva);
			ispw(SV_IMGO_FBC, 0);
			break;
		case 5:
			ispw(SV_SW_CTL, 1);
			ispw(SV_SW_CTL, 0);
			break;
		}

		if (swapmode)
			buf_invalidate(&buf[done], fsize);

		const uint16_t *src = swapmode ? (const uint16_t *)buf[done].va : kopiya;

		/* ---- сбор кадров для снимка ---- */
		if (shst == SH_PREP) {
			if (--sh_wait <= 0) {
				shst = SH_GRAB;
				sh_left = sh_total;
				shot.nacc = 0;
				memset(shot.acc, 0,
				       (size_t)width * height * sizeof(uint32_t));
			}
		} else if (shst == SH_GRAB) {
			size_t npx = (size_t)width * height;

			if (shot.raw)
				memcpy(shot.burst + (size_t)shot.nacc * npx, src,
				       npx * sizeof(uint16_t));
			else
				for (size_t i = 0; i < npx; i++)
					shot.acc[i] += (uint32_t)(src[i] >> 4);
			shot.nacc++;
			if (--sh_left == 0) {
				/*
				 * Запоминаем настройки, при которых кадры СНЯТЫ, а не те,
				 * что были до съёмки: для фотографии свет перекладывается
				 * с усиления на выдержку, и тёмный кадр надо подбирать
				 * под съёмочные, иначе он не подойдёт никогда.
				 */
				unsigned было_expo = expo, было_gain = gain;

				/* съёмочные настройки обратно, автоматика подхватит сама */
				flen = sh_flen;
				sensor_w16(0x0340, flen);
				expo_max = flen - 12;
				expo = sh_expo;
				gain = sh_gain;
				sensor_w16(0x0202, expo);
				sensor_w16(0x0204, gain);
				shot.w = width;
				shot.h = height;
				shot.p = cvp;
				shot.expo = было_expo;
				shot.gain = было_gain;
				shot.вычесть = (!shot.тёмный && !shot.raw
						&& dark.w == width && dark.h == height)
					? dark_коэф(было_expo, было_gain) : 0;
				snprintf(shot.path, sizeof(shot.path), "%s", sh_path);
				shst = SH_IDLE;
				/*
				 * Метка «кадры собраны». Разбор длится ещё несколько
				 * секунд, но держать телефон ровно к этому моменту уже
				 * не нужно — и человек должен это узнать сразу, а не
				 * стоять столбом до конца.
				 */
				{
					int fd = open(GRABFILE, O_CREAT | O_WRONLY | O_TRUNC, 0644);

					if (fd >= 0) close(fd);
				}
				pthread_mutex_lock(&shot.mx);
				shot.busy = 1;
				shot.ready = 1;
				pthread_cond_signal(&shot.go);
				pthread_mutex_unlock(&shot.mx);
			}
		}
		double tc0 = now_s();

		frames++;

		int64_t sumR = 0, sumG = 0, sumB = 0;
		int64_t hiR = 0, hiG = 0, hiB = 0;
		unsigned hiN = 0, clipN = 0, roiN = 0;
		uint64_t sumY = 0, sharp = 0, roiG = 0;
		int gmax = 0;
		/*
		 * Для баланса белого считаем не весь кадр, а только светлые места.
		 * «В среднем серое» по всему кадру врёт, когда полкадра занимает
		 * тёплый деревянный стол: белая бумага уходила в розовое. Светлое
		 * же почти всегда и есть настоящий белый.
		 */
		static int gmax_prev = 500;
		int hithr = gmax_prev * 3 / 4;

		if (hithr > 700) hithr = 700;

		/*
		 * Замеры (суммы для баланса, пересветы, резкость) считаем не на
		 * каждом кадре. Автоматика всё равно смотрит на них раз в три
		 * кадра, а на перебор по резкости при наводке — каждый. Кадру это
		 * экономит заметную долю работы: телефон на четырёх слабых ядрах, и
		 * освобождённое ядро достаётся тому, кто эти кадры показывает.
		 */
		int want_stats = (frames % 3) == 0 || afst == AF_COARSE
			|| afst == AF_FINE || afst == AF_SWEEP;

		/*
		 * В покое кадр пересчитываем не каждый раз. Замер: холостой ход
		 * идёт 2,2 кадра в секунду, а стоит 31% ядра — по 140 мс процессора
		 * на кадр, и всё это на картинку, которую никто не видит. Отдавать
		 * что-то в узел обязательно (иначе v4l2loopback перестанет считаться
		 * камерой), но отдавать можно прошлый кадр: он уже лежит в yuyv.
		 *
		 * Каждый восьмой всё же пересчитываем, чтобы выдержка не отстала от
		 * освещения совсем: при 2,2 кадрах в секунду это раз в три с
		 * половиной секунды, чего хватает, чтобы первый кадр после открытия
		 * камеры был уже примерно верным.
		 */
		static unsigned idle_tick;
		int reuse = idle && have_frame && (++idle_tick % 8) != 0;

		if (!reuse) {
			cvp.src = src;
			cvp.wbR = wbR; cvp.wbG = wbG; cvp.wbB = wbB;
			cvp.usecm = usecm;
			cvp.hithr = hithr;
			cvp.stats = want_stats;
			convert_frame();
			have_frame = 1;
		}

		for (int i = 0; i < nband && !reuse; i++) {
			const struct band *bd = &bands[i];

			sumR += bd->sumR; sumG += bd->sumG; sumB += bd->sumB;
			hiR += bd->hiR; hiG += bd->hiG; hiB += bd->hiB;
			hiN += bd->hiN; clipN += bd->clipN; roiN += bd->roiN;
			sumY += bd->sumY; sharp += bd->sharp; roiG += bd->roiG;
			if (bd->gmax > gmax) gmax = bd->gmax;
		}
		/*
		 * Временной шум: одна и та же точка в двух соседних кадрах. Нужен,
		 * чтобы отличить настоящее накопление света от внутреннего домножения
		 * в сенсоре. У настоящего света шум растёт как корень из сигнала —
		 * значит отношение сигнал/шум улучшается; у домножения шум растёт
		 * вместе с сигналом и отношение не меняется. Берём диагональ из 256
		 * зелёных точек, до гаммы и баланса.
		 */
		{
			static int prevpx[256];
			static int havepx;
			double d = 0;
			int nn = 0;

			for (int i = 0; i < 256; i++) {
				size_t r = (size_t)(2 * (y0 + 100 + i)) * width;
				size_t c = (size_t)(2 * (x0 + 100 + i));
				int v = (int)(src[r + c] >> 4) - 64;

				if (havepx) { d += fabs((double)(v - prevpx[i])); nn++; }
				prevpx[i] = v;
			}
			havepx = 1;
			if (nn) tnoise = tnoise * 0.9 + (d / nn) * 0.1;
		}
		tconv += now_s() - tc0;
		if (now_s() - tc0 > mx_conv) mx_conv = now_s() - tc0;
		t_seg = now_s();

		static double meanY_last, sharp_last;

		if (want_stats) {
			double meanRoi = roiN ? (double)roiG / roiN : 1.0;

			meanY_last = (double)sumY / ((double)outw * outh);
			gmax_prev = gmax > 64 ? gmax : 64;
			sharp_last = (double)sharp
				/ ((meanRoi * meanRoi + 1.0) * (roiN ? roiN : 1));
		}
		double meanY = meanY_last, sharpn = sharp_last;

		/* меняется ли вообще картинка: дешёвая контрольная сумма кадра */
		{
			static unsigned long prevsum;
			unsigned long ck = 0;

			for (size_t i = 0; i < (size_t)outw * outh * 2; i += 997)
				ck = ck * 31 + yuyv[i];
			if (ck == prevsum) same++;
			prevsum = ck;
		}
		if (meanY < 0.5) empty++;
		if (write(vfd, yuyv, (size_t)outw * outh * 2) > 0)
			shown++;
		{
			double d = now_s() - t_seg;

			if (d > mx_wr) mx_wr = d;
			t_seg = now_s();
		}

		/* ---- выдержка и баланс белого ---- */
		/*
		 * Пока идёт наводка, выдержку и усиление не трогаем: при смене
		 * выдержки меняется и шум, и контраст, а перебор по резкости
		 * сравнивает кадры между собой — сравнивать надо равное с равным.
		 */
		if (want_stats && (afst == AF_LOCK || afst == AF_WAIT) && now_s() >= af_hold
		    && now_s() >= ae_hold) {
			double err = (double)target - meanY;
			double clipf = (double)clipN / ((double)outw * outh);

			/*
			 * Защита светов: если пересвечено больше двух сотых кадра,
			 * убавляем выдержку, что бы там ни говорила средняя яркость.
			 * Иначе автоматика «подтягивает» тёмный стол и сжигает белую
			 * бумагу вместе с мишенью.
			 */
			/*
			 * Раньше защита срабатывала рывком: чуть больше двух сотых
			 * пересвета — и поправка сразу -40. Автоматика уводила яркость
			 * вниз, пересвет пропадал, защита отпускала, автоматика гнала
			 * обратно — и так без конца. На тёмной комнате со светящимся
			 * окном в кадре усиление металось от 0x60 до 0x300 несколько раз
			 * в секунду, и картинка мигала. Теперь поправка растёт плавно,
			 * и у пары «яркость — пересвет» появляется общая точка покоя.
			 */
			if (clipf > 0.06) {
				double ce = -40.0 * (clipf - 0.06) / 0.10;

				if (ce < -40) ce = -40;
				if (err > ce) err = ce;
			}

			if (man_expo || man_gain) err = 0;
			if (fabs(err) > 10) {
				double k = 1.0 + err / 200.0;

				/*
			 * Шаг ограничен нарочно. Отклик у сенсора вялый: учетверение
			 * усиления поднимает среднюю яркость всего на пятую часть
			 * (яркость считается после гаммы, а света упираются в потолок).
			 * При широком шаге автоматика гонит усиление до упора, потом
			 * отпускает — и зерно на картинке пульсирует. Медленный шаг
			 * доходит до места за пару секунд и дальше не мельтешит.
				 */
				if (k < 0.88) k = 0.88;
				if (k > 1.12) k = 1.12;
				if (err > 0) {
					if (expo < expo_max) expo = (unsigned)(expo * k);
					else if (gain < gain_max) gain = (unsigned)(gain * k);
					else if (flen >= flen_view && !man_flen) {
						/* дальше видоискатель не тормозим — копим для снимка */
						ae_extra *= 1.5;
						if (ae_extra > (double)flen_max / flen_view)
							ae_extra = (double)flen_max / flen_view;
					}
					else if (flen < flen_max && !man_flen) {
						/*
						 * Света всё равно мало. Удлиняем кадр.
						 *
						 * Важно понимать, что именно это даёт: такт
						 * сенсора длина кадра НЕ меняет — проверено
						 * прогоном 3260..19560, выдача держится 54 кадра
						 * в секунду при нуле повторов. Она поднимает
						 * потолок выдержки (expo_max = flen - 12), и
						 * потолок — это и есть свет в темноте. Каким
						 * образом выдержка длиннее кадра вообще
						 * отрабатывает, не выяснено.
						 */
						flen = flen * 3 / 2;
						if (flen > flen_view) flen = flen_view;
						sensor_w16(0x0340, flen);
						expo_max = flen - 12;
					}
				} else {
					if (ae_extra > 1.0) {
						ae_extra *= k;
						if (ae_extra < 1.0) ae_extra = 1.0;
					} else if (gain > gain_min) gain = (unsigned)(gain * k);
					/*
					 * Убавлять выдержку можно до самого низа. Раньше
					 * здесь стояло `expo > flen_min / 2`, то есть ниже
					 * 1630 автоматика не опускалась — а усиление к тому
					 * времени уже лежало на минимуме. На дневном свете
					 * это значило, что убавить нечего: вид в окно
					 * выгорал в малиновое (зелёный упирается в потолок
					 * первым, красный с синим остаются).
					 */
					else if (expo > 8) expo = (unsigned)(expo * k);
					else if (flen > flen_min && !man_flen) {
						flen = flen * 2 / 3;
						if (flen < flen_min) flen = flen_min;
						sensor_w16(0x0340, flen);
						expo_max = flen - 12;
					}
				}
				if (expo < 8) expo = 8;
				if (expo > expo_max) expo = expo_max;
				if (gain < gain_min) gain = gain_min;
				if (gain > gain_max) gain = gain_max;
				sensor_w16(0x0202, expo);
				sensor_w16(0x0204, gain);
				ae_hold = now_s() + 0.12;
			}
			/*
			 * Баланс подстраиваем раз в три кадра. Ворота этого блока раньше
			 * стояли на «каждом третьем кадре», а теперь на паузе после
			 * команды сенсору — выдержке так правильнее, но балансу вместе с
			 * ней достались бы тридцать поправок в секунду вместо десяти, и
			 * цвет начал бы плыть на ровном месте.
			 */
			if (man_wb || (frames % 3) != 0) goto wb_done;

			int64_t wR = hiN > 2000 ? hiR : sumR;
			int64_t wG = hiN > 2000 ? hiG : sumG;
			int64_t wB = hiN > 2000 ? hiB : sumB;

			if (wR > 100 && wB > 100 && wG > 100) {
				double gr = (double)wG / (double)wR;
				double gb = (double)wG / (double)wB;
				double gg = 1.0;
				double mx = gr > gb ? gr : gb;

				if (gg > mx) mx = gg;
				gr /= mx; gb /= mx; gg /= mx;
				if (gr < 0.25) gr = 0.25;
				if (gb < 0.25) gb = 0.25;
				if (gg < 0.25) gg = 0.25;
				wbR += ((int)(gr * 65536) - wbR) / 16;
				wbB += ((int)(gb * 65536) - wbB) / 16;
				wbG += ((int)(gg * 65536) - wbG) / 16;
				wbR_ref = wbR; wbG_ref = wbG; wbB_ref = wbB;
			}
wb_done:		;
		}

		/* ---- наводка по контрасту ---- */
		double sh = sharpn;

		if (!want_stats) {
			;                            /* замеров нет — судить не о чем */
		} else if (now_s() < af_hold) {
			;                            /* линза ещё едет — не мерить */
		} else if (afst == AF_WAIT) {
			static int steady;

			if (fabs(meanY - (double)target) < 18 || frames > 150) {
				if (++steady > 5) {
					steady = 0;
					afst = sweep ? AF_SWEEP : AF_COARSE;
					af_i = 0; af_bestval = -1; af_hold = now_s() + af_wait * 1.5;
					af_moveto(sweep ? 0 : (unsigned)af_coarse[0]);
					fprintf(stderr,
						"bf7-camd: выдержка набрана (яркость %.0f, выдержка %u, усиление 0x%x), навожусь\n",
						meanY, expo, gain);
				}
			} else {
				steady = 0;
			}
		} else if (afst == AF_COARSE) {
			if (sh > af_bestval) { af_bestval = sh; af_best = af_coarse[af_i]; }
			af_i++;
			if (af_i < (int)(sizeof(af_coarse) / sizeof(af_coarse[0]))) {
				af_moveto((unsigned)af_coarse[af_i]);
				af_hold = now_s() + af_wait;
			} else {
				af_fine_base = af_best > 96 ? af_best - 96 : 0;
				af_fine_i = 0;
				af_bestval = -1;
				afst = AF_FINE;
				af_moveto((unsigned)af_fine_base);
				af_hold = now_s() + af_wait;
			}
		} else if (afst == AF_FINE) {
			int pos = af_fine_base + af_fine_i * 24;

			if (sh > af_bestval) { af_bestval = sh; af_best = pos; }
			af_fine_i++;
			if (af_fine_i <= 8) {
				af_moveto((unsigned)(af_fine_base + af_fine_i * 24));
				af_hold = now_s() + af_wait;
			} else {
				af_moveto(af_best);
				af_lockval = af_bestval;
				afst = AF_LOCK;
				af_lost = 0;
				af_cool = now_s() + 5.0;
				af_low = 0;
				fprintf(stderr, "bf7-camd: наведено, положение линзы %d (резкость %.4f)\n",
					af_best, af_bestval);
				{
					FILE *f = fopen(STATEFILE, "w");

					if (f) {
						fprintf(f, "%d %u %u %u %d %d %d\n",
							af_best, expo, gain, (unsigned)(flen * ae_extra), wbR, wbG, wbB);
						fclose(f);
					}
				}
			}
		} else if (afst == AF_SWEEP) {
			/*
			 * Полный проход по всему ходу линзы с печатью таблицы —
			 * чтобы видеть кривую резкости целиком, а не верить
			 * найденному максимуму на слово.
			 */
			int pos = af_i * (int)sweep;

			fprintf(stderr, "  проход: положение %4d  резкость %12.4f  яркость %5.1f\n",
				pos, sh, meanY);
			if (sh > af_bestval) { af_bestval = sh; af_best = pos; }
			af_i++;
			if (af_i * (int)sweep <= 1023) {
				af_moveto((unsigned)(af_i * (int)sweep));
				af_hold = now_s() + af_wait * 1.5;
			} else {
				af_moveto(af_best);
				af_lockval = af_bestval;
				afst = AF_LOCK;
				fprintf(stderr, "bf7-camd: проход закончен, лучшее положение %d (резкость %.4f)\n",
					af_best, af_bestval);
			}
		} else {
			/*
			 * Перенаводка — дело дорогое: перебор занимает около двух
			 * секунд, и всё это время картинка «дышит». Поэтому, во-первых,
			 * после наводки пять секунд не дёргаемся вовсе; во-вторых,
			 * падение резкости должно держаться почти секунду, а не
			 * мелькнуть; в-третьих, планка удержания больше не ползёт вверх
			 * рывком от случайно резкого кадра — иначе она задиралась, и
			 * дальше любой обычный кадр выглядел «нерезким».
			 */
			if (man_focus < 0 && af_lockval > 0 && sh < af_lockval * 0.5
			    && now_s() > af_cool) {
				if (af_low == 0) af_low = now_s();
				if (now_s() - af_low > 0.8) {
					afst = AF_COARSE;
					af_i = 0; af_bestval = -1;
					af_moveto(af_coarse[0]);
					af_hold = now_s() + af_wait;
					af_lost = 0;
				}
			} else {
				af_lost = 0;
				af_low = 0;
				if (sh > af_lockval) af_lockval += (sh - af_lockval) * 0.02;
			}
		}

		if ((frames % 300) == 0) {
			FILE *f = fopen(STATEFILE, "w");

			if (f) {
				fprintf(f, "%d %u %u %u %d %d %d\n",
					man_focus >= 0 ? man_focus : af_best,
					expo, gain, (unsigned)(flen * ae_extra), wbR, wbG, wbB);
				fclose(f);
			}
		}

		if (verbose && (frames % 30) == 0) {
			static double t_prev;
			static unsigned long f_prev;
			double tnow = now_s();
			double dt = t_prev > 0 ? tnow - t_prev : tnow - t0;
			unsigned long df = frames - f_prev;

			t_prev = tnow;
			f_prev = frames;
			/*
			 * Сырой зелёный — единственный честный светомер. Яркость
			 * считается после гаммы, баланса и матрицы, и по ней нельзя
			 * судить, вдвое ли прибавилось света. Здесь же — среднее
			 * значение зелёных точек прямо с матрицы, за вычетом чёрного:
			 * оно прямо пропорционально свету.
			 */
			double npix = (double)outw * outh;
			double rawR = (double)sumR / npix, rawG = (double)sumG / npix;
			double rawB = (double)sumB / npix;

			fprintf(stderr,
				"кадров %lu (пустых %lu, повторов %lu), отдано %lu, %.1f/с сейчас, сырые R %.1f G %.1f B %.1f (отобрано %u), шум %.2f, разбор %.0f мс, яркость %.0f, выдержка %u, усиление 0x%x, ббR %.2f ббG %.2f ббB %.2f, резкость %.4f, длина кадра %u\n"
				"   задержки, наибольшие за отрезок: ожидание %.0f, разбор %.0f, отдача %.0f, прочее %.0f мс\n",
				frames, empty, same, shown, df / (dt > 0 ? dt : 1),
				rawR, rawG, rawB, hiN, tnoise, 1000 * tconv / frames, meanY, expo, gain, wbR / 65536.0, wbG / 65536.0, wbB / 65536.0, sh, flen,
				mx_wait * 1000, mx_conv * 1000, mx_wr * 1000, mx_rest * 1000);
			mx_wait = mx_conv = mx_wr = mx_rest = 0;
		}
	}

	ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
	fprintf(stderr, "bf7-camd: останов, кадров %lu, пустых %lu\n", frames, empty);
	return 0;
}
