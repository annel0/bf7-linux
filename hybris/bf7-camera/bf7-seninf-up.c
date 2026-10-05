/*
 * bf7-seninf-up — поднять камеру и держать поток.
 *
 * Единственная часть камеры, которой нужен мир Android: приёмник MIPI
 * (seninf) у MediaTek настраивает не ядро, а вендорская libcam.halsensor.so,
 * и она собрана под bionic. Поэтому это андроидный бинарник (NDK), как
 * bf7-videod. Запускать через `env`, не через `export`: андроидные заглушки
 * ломают обычные программы glibc.
 *
 * Делает ровно три вещи: открывает сенсор, даёт вендорской библиотеке
 * настроить приёмник и мультиплексор, запускает сценарий предпросмотра — и
 * дальше просто спит, держа узлы открытыми. Закрытие /dev/kd_camera_hw
 * гасит такты и сбрасывает ножки, поэтому выходить нельзя.
 *
 * Кадры из блока захвата CAMSV забирает bf7-camd — обычная программа glibc,
 * которая собирается прямо на телефоне.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <dlfcn.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* ------------------------------------------------------------------ */
/*  сенсор через ядро                                                  */
/* ------------------------------------------------------------------ */
#define IMGSENSORMAGIC 'i'
#define KDIMGSENSORIOC_X_FEATURECONCTROL _IOWR(IMGSENSORMAGIC, 15, struct featurectrl)
#define KDIMGSENSORIOC_X_CONTROL         _IOWR(IMGSENSORMAGIC, 20, struct sensorctrl)
#define SENSOR_FEATURE_OPEN       3105
#define SENSOR_FEATURE_CLOSE      3106
#define SENSOR_FEATURE_SET_DRIVER 3107

struct featurectrl {
	uint32_t InvokeCamera, FeatureId;
	uint8_t *pFeaturePara;
	uint32_t *pFeatureParaLen;
};
struct sensorctrl {
	uint32_t InvokeCamera, ScenarioId;
	void *pImageWindow, *pSensorConfigData;
};

static int feature(int fd, uint32_t cam, uint32_t id, void *para, uint32_t len)
{
	uint32_t l = len;
	struct featurectrl f = { cam, id, (uint8_t *)para, &l };

	return ioctl(fd, KDIMGSENSORIOC_X_FEATURECONCTROL, &f);
}

/* ------------------------------------------------------------------ */
/*  регистры ISP                                                       */
/* ------------------------------------------------------------------ */
#define SENINF_BASE  0x15040000u
#define SENINF_SIZE  0x8000u
#define IMGSYS_BASE  0x15000000u
#define IMGSYS_SIZE  0x52000u
#define MIPIANA_BASE 0x11c10000u
#define MIPIANA_SIZE 0x6000u

#define SV             0x50000u
#define SV_MODULE_EN   (SV + 0x000)
#define SV_FMT_SEL     (SV + 0x004)
#define SV_INT_EN      (SV + 0x008)
#define SV_INT_ST      (SV + 0x00C)
#define SV_SW_CTL      (SV + 0x010)
#define SV_IMGO_FBC    (SV + 0x01C)
#define SV_CLK_EN      (SV + 0x020)
#define SV_IMGO_BASE   (SV + 0x208)
#define SV_IMGO_OFST   (SV + 0x20C)
#define SV_IMGO_XSIZE  (SV + 0x210)
#define SV_IMGO_YSIZE  (SV + 0x214)
#define SV_IMGO_STRIDE (SV + 0x218)
#define SV_TG_VF_CON   (SV + 0x414)
#define SV_TG_FRMSIZE  (SV + 0x448)
#define SV_TG_INTER    (SV + 0x44C)

#define SV_INT_PASS1_DON (1u << 10)

static volatile uint32_t *imgsys;          /* окно ISP, слова */

static inline uint32_t ispr(unsigned off) { return imgsys[off / 4]; }
static inline void ispw(unsigned off, uint32_t v) { imgsys[off / 4] = v; }

/* ------------------------------------------------------------------ */
/*  ION: память, которую понимает блок MMU камеры                      */
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

#define ION_CMD_MULTIMEDIA 1
#define ION_MM_GET_IOVA    8
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

struct campbuf {
	ion_user_handle_t handle;
	int dmafd;
	uint32_t mva;
	uint8_t *va;
	size_t len;
};

static int ionfd = -1;

static int buf_alloc(struct campbuf *b, size_t len)
{
	struct ion_allocation_data a = { 0 };
	struct ion_mm_data mm = { 0 };
	struct ion_custom_data cd = { ION_CMD_MULTIMEDIA, (unsigned long)&mm };
	struct ion_fd_data fdd;

	a.len = (len + 0xfffff) & ~(size_t)0xfffff;
	a.heap_id_mask = 1u << 10;
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

/* ------------------------------------------------------------------ */
/*  сенсор по шине: выдержка и усиление                                */
/* ------------------------------------------------------------------ */
static int i2cfd = -1;

static int sensor_w16(unsigned reg, unsigned val)
{
	unsigned char b[4] = { reg >> 8, reg & 0xff, val >> 8, val & 0xff };
	struct i2c_msg m = { .addr = 0x10, .flags = 0, .len = 4, .buf = b };
	struct i2c_rdwr_ioctl_data io = { .msgs = &m, .nmsgs = 1 };

	return ioctl(i2cfd, I2C_RDWR, &io);
}

/* ------------------------------------------------------------------ */
/*  линза                                                              */
/* ------------------------------------------------------------------ */
#define AF_MAGIC 'A'
struct stAF_MotorName { char uMotorName[32]; };
#define AFIOC_T_MOVETO     _IOW(AF_MAGIC, 1, uint32_t)
#define AFIOC_S_SETDRVNAME _IOW(AF_MAGIC, 10, struct stAF_MotorName)

static int affd = -1;

static void af_moveto(unsigned pos)
{
	if (affd >= 0)
		ioctl(affd, AFIOC_T_MOVETO, &pos);
}

/* ------------------------------------------------------------------ */
/*  уборка                                                             */
/* ------------------------------------------------------------------ */
static int sensorfd = -1, ispfd = -1, vfd = -1;
static uint32_t sensor_cam;
static volatile sig_atomic_t stop_now;

static void on_signal(int s) { (void)s; stop_now = 1; }

static void cleanup(void)
{
	if (imgsys) {
		ispw(SV_TG_VF_CON, ispr(SV_TG_VF_CON) & ~1u);
		ispw(SV_MODULE_EN, 0x40000000u);
	}
	if (sensorfd >= 0) {
		uint8_t nothing[4] = { 0 };

		feature(sensorfd, sensor_cam, SENSOR_FEATURE_CLOSE, nothing, 4);
		close(sensorfd);
		sensorfd = -1;
	}
	if (affd >= 0) { close(affd); affd = -1; }
	if (vfd >= 0)  { close(vfd);  vfd = -1; }
	if (i2cfd >= 0) { close(i2cfd); i2cfd = -1; }
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
	unsigned cam    = 0;
	unsigned width  = 2104, height = 1560;
	uint64_t rate   = 227200000ULL;
	unsigned csiport = 1, lanes = 4, settle = 85;
	unsigned outw = 1024, outh = 768;
	const char *vdev = "/dev/video0";
	unsigned target = 110;          /* к какой средней яркости вести */
	int verbose = 0;

	for (int i = 1; i < argc; i++) {
		char *eq = strchr(argv[i], '=');

		if (!strcmp(argv[i], "-v")) { verbose = 1; continue; }
		if (!eq) continue;
		if (!strncmp(argv[i], "dev=", 4))    vdev = eq + 1;
		else if (!strncmp(argv[i], "outw=", 5))  outw = atoi(eq + 1);
		else if (!strncmp(argv[i], "outh=", 5))  outh = atoi(eq + 1);
		else if (!strncmp(argv[i], "target=", 7)) target = atoi(eq + 1);
	}

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	/* ---- окно регистров: обязательно ДО открытия узла сенсора ---- */
	ispfd = open("/dev/camera-isp", O_RDWR);
	if (ispfd < 0) { perror("/dev/camera-isp"); return 1; }

	volatile uint32_t *seninf = mmap(NULL, SENINF_SIZE, PROT_READ | PROT_WRITE,
					 MAP_SHARED, ispfd, SENINF_BASE);
	imgsys = mmap(NULL, IMGSYS_SIZE, PROT_READ | PROT_WRITE,
		      MAP_SHARED, ispfd, IMGSYS_BASE);
	volatile uint32_t *ana = mmap(NULL, MIPIANA_SIZE, PROT_READ | PROT_WRITE,
				      MAP_SHARED, ispfd, MIPIANA_BASE);

	if (seninf == MAP_FAILED || imgsys == MAP_FAILED || ana == MAP_FAILED) {
		fprintf(stderr, "регистры ISP не отображаются\n");
		return 1;
	}

	/* ---- сенсор ---- */
	sensorfd = open("/dev/kd_camera_hw", O_RDWR);
	if (sensorfd < 0) { perror("/dev/kd_camera_hw"); return 1; }
	sensor_cam = cam;

	int32_t drv = 0;
	uint8_t nothing[4] = { 0 };

	if (feature(sensorfd, cam, SENSOR_FEATURE_SET_DRIVER, &drv, 4) < 0 || drv < 0) {
		fprintf(stderr, "драйвер сенсора не подобран\n"); return 1;
	}
	if (feature(sensorfd, cam, SENSOR_FEATURE_OPEN, nothing, 4) < 0) {
		fprintf(stderr, "сенсор не открылся: %s\n", strerror(errno)); return 1;
	}

	/* ---- вендорская настройка приёмника ---- */
	void *lib = dlopen("/vendor/lib64/libcam.halsensor.so", RTLD_NOW);

	if (!lib) { fprintf(stderr, "libcam.halsensor.so: %s\n", dlerror()); return 1; }

	typedef void *(*fn_create)(void);
	typedef int (*fn_init)(void *);
	typedef int (*fn_cfg)(void *, void *, void *, void *, bool);
	typedef int (*fn_mux)(void *, unsigned, unsigned);
	typedef int (*fn_svcfg)(void *, unsigned, unsigned, unsigned, unsigned);
	typedef int (*fn_svgrab)(void *, unsigned, uint64_t, uint64_t, uint64_t, uint64_t);
	typedef int (*fn_svvf)(void *, unsigned, uint64_t);

	fn_create create = (fn_create)dlsym(lib, "_ZN9SeninfDrv14createInstanceEv");
	fn_init   init   = (fn_init)dlsym(lib, "_ZN12SeninfDrvImp4initEv");
	fn_cfg    cfgsi  = (fn_cfg)dlsym(lib,
		"_ZN12SeninfDrvImp17configSensorInputEP20SENINF_CONFIG_STRUCTPN5NSCam17SensorDynamicInfoEP23ACDK_SENSOR_INFO_STRUCTb");
	fn_mux    camMux = (fn_mux)dlsym(lib,
		"_ZN12SeninfDrvImp16setTopCamMuxCtrlE18SENINF_TOP_P1_ENUM15SENINF_MUX_ENUM");
	fn_svcfg  svCfg  = (fn_svcfg)dlsym(lib,
		"_ZN12SeninfDrvImp8setSVCfgEj14TG_FORMAT_ENUM21SENSOR_DATA_BITS_ENUMj");
	fn_svgrab svGrab = (fn_svgrab)dlsym(lib, "_ZN12SeninfDrvImp14setSVGrabRangeEjmmmm");
	fn_svvf   svVf   = (fn_svvf)dlsym(lib, "_ZN12SeninfDrvImp19setSVViewFinderModeEjm");

	if (!create || !init || !cfgsi) {
		fprintf(stderr, "в libcam.halsensor.so нет нужных символов\n"); return 1;
	}

	void *drvobj = create();

	if (!drvobj) { fprintf(stderr, "SeninfDrv::createInstance() дал ноль\n"); return 1; }
	init(drvobj);

	static uint8_t cfg[1024], dyn[256], sinfo[4096];
	uint32_t *c = (uint32_t *)cfg;
	uint64_t *q = (uint64_t *)cfg;

	memset(cfg, 0, sizeof(cfg));
	memset(dyn, 0, sizeof(dyn));
	memset(sinfo, 0, sizeof(sinfo));
	c[20 / 4]  = width;          /* окно захвата: правый край */
	c[28 / 4]  = height;         /* нижний край */
	c[80 / 4]  = 8;              /* источник — CSI-2 */
	c[88 / 4]  = csiport;        /* вход CSI: у главной камеры первый */
	c[104 / 4] = settle;         /* задержка успокоения линий */
	c[116 / 4] = lanes - 1;      /* число полос минус одна */
	c[252 / 4] = cam;
	q[72 / 8]  = rate;           /* частота пикселей MIPI, 64 бита */
	q[144 / 8] = rate;

	if (cfgsi(drvobj, cfg, dyn, sinfo, true) < 0) {
		fprintf(stderr, "configSensorInput() отказал\n"); return 1;
	}
	if (camMux) camMux(drvobj, 2, 0);       /* ниббл 2 в маршруте -> мультиплексор 0 */

	/* ---- поток ---- */
	struct sensorctrl sc = { cam, 0, NULL, NULL };

	if (ioctl(sensorfd, KDIMGSENSORIOC_X_CONTROL, &sc) < 0) {
		fprintf(stderr, "сценарий предпросмотра не пошёл: %s\n", strerror(errno));
		return 1;
	}
	usleep(400000);

	/*
	 * Генератор кадров CAMSV настраивается только теперь. Вендорские
	 * setSVCfg/setSVGrabRange читают состояние уже идущего потока: если
	 * позвать их до запуска сенсора, окно захвата остаётся пустым и DMA
	 * не пишет ни байта.
	 */
	if (svCfg)  svCfg(drvobj, 0, 0, 0, 0);
	if (svGrab) svGrab(drvobj, 0, 0, width, 0, height);
	if (svVf)   svVf(drvobj, 0, 1);
	usleep(100000);

	/*
	 * Дальше ничего не делаем: наша задача — держать открытыми узлы
	 * сенсора, чтобы ядро не сняло питание и такты, и не мешать. Кадры
	 * забирает отдельная программа bf7-camd, обычная, собранная на самом
	 * телефоне: ей вендорские библиотеки не нужны, только регистры CAMSV
	 * и память от ION.
	 */
	fprintf(stderr,
		"bf7-seninf-up: приёмник настроен, кадр мультиплексора 0x%08x. Держу поток.\n",
		ispr(0x40d14u));
	fflush(stderr);

	while (!stop_now)
		pause();

	fprintf(stderr, "bf7-seninf-up: останов\n");
	cleanup();
	return 0;
}
