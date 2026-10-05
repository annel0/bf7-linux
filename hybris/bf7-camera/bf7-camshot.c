/*
 * BF7: попытка снять кадр с главной камеры без Android.
 *
 * Расклад такой. Сенсор мы поднимаем сами, через /dev/kd_camera_hw — это
 * умеет ядро. А приёмник MIPI (seninf) ядро только читает: настраивает его у
 * MediaTek пользовательская часть. Она лежит на разделе, и — редкая удача —
 * её символы не спрятаны:
 *
 *   SeninfDrv::createInstance()
 *   SeninfDrvImp::init()
 *   SeninfDrvImp::configSensorInput(cfg, dynInfo, sensorInfo, enable)
 *
 * Последняя и делает всю работу: проверяет, что источник CSI-2, добирает
 * параметры полос через getCSIpara() и зовёт configInputForCSI(), а тот уже
 * настраивает приёмник, окно захвата и мультиплексоры.
 *
 * Это обычный андроидный бинарник (NDK, bionic), как и bf7-videod: libhybris
 * не нужен, границы между libc++ нет.
 *
 * Раскладка SENINF_CONFIG_STRUCT в заголовках отсутствует. Поля восстановлены
 * по коду configInputForCSI: читаются смещения 8, 16, 20, 24, 28, 32, 56, 60,
 * 80 и 252. Поля 16..28 уходят в setTgGrabRange(), 80 обязано быть 8 (это
 * источник «CSI-2»), 252 — номер гнезда. Остальные пока под сомнением,
 * поэтому их можно задать из командной строки и перебрать.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define IMGSENSORMAGIC 'i'
#define KDIMGSENSORIOC_X_FEATURECONCTROL _IOWR(IMGSENSORMAGIC, 15, struct featurectrl)
#define KDIMGSENSORIOC_X_CONTROL         _IOWR(IMGSENSORMAGIC, 20, struct sensorctrl)

#define SENSOR_FEATURE_OPEN       3105
#define SENSOR_FEATURE_SET_DRIVER 3107

#define SENINF_BASE 0x15040000u
#define SENINF_SIZE 0x8000u
#define IMGSYS_BASE 0x15000000u
#define IMGSYS_SIZE 0x52000u
#define MIPIANA_BASE 0x11c10000u
#define MIPIANA_SIZE 0x6000u

struct featurectrl {
	uint32_t InvokeCamera;
	uint32_t FeatureId;
	uint8_t *pFeaturePara;
	uint32_t *pFeatureParaLen;
};

struct sensorctrl {
	uint32_t InvokeCamera;
	uint32_t ScenarioId;
	void *pImageWindow;
	void *pSensorConfigData;
};

static int feature(int fd, uint32_t cam, uint32_t id, void *para, uint32_t len)
{
	uint32_t l = len;
	struct featurectrl f = { cam, id, (uint8_t *)para, &l };

	return ioctl(fd, KDIMGSENSORIOC_X_FEATURECONCTROL, &f);
}

/* снимок куска регистров, чтобы потом показать разницу */
static void snap(volatile uint32_t *base, uint32_t *out, unsigned words)
{
	for (unsigned i = 0; i < words; i++)
		out[i] = base[i];
}

static unsigned diff(const uint32_t *a, const uint32_t *b, unsigned words,
		     const char *what, unsigned show)
{
	unsigned n = 0;

	for (unsigned i = 0; i < words; i++) {
		if (a[i] == b[i])
			continue;
		if (n < show)
			printf("    +0x%04x: %08x -> %08x\n", i * 4, a[i], b[i]);
		n++;
	}
	printf("  %s: изменилось регистров %u\n", what, n);
	return n;
}

int main(int argc, char **argv)
{
	unsigned cam      = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
	unsigned width    = argc > 2 ? (unsigned)atoi(argv[2]) : 2104;
	unsigned height   = argc > 3 ? (unsigned)atoi(argv[3]) : 1560;
	unsigned f8       = argc > 4 ? (unsigned)atoi(argv[4]) : 0;
	unsigned f32      = argc > 5 ? (unsigned)atoi(argv[5]) : 0;
	unsigned f56      = argc > 6 ? (unsigned)atoi(argv[6]) : 0;
	unsigned f60      = argc > 7 ? (unsigned)atoi(argv[7]) : 0;
	uint64_t rate     = argc > 8 ? strtoull(argv[8], NULL, 0) : 227200000ULL;
	unsigned p1       = argc > 9 ? (unsigned)atoi(argv[9]) : 0;
	unsigned mux      = argc > 10 ? (unsigned)atoi(argv[10]) : 0;
	unsigned vf       = argc > 11 ? (unsigned)atoi(argv[11]) : 3;
	unsigned csiport  = argc > 12 ? (unsigned)atoi(argv[12]) : 0;
	unsigned sv       = 0, svidx = 0, svfmt = 0, svbits = 0;
	unsigned tm       = 0, tmidx = 0, tmpat = 0, tmsrc = 0;

	/* дальше идут пары «смещение=значение» для полей настройки */
	unsigned setoff[16], setval[16], rawoff[32], rawval[32];
	int nset = 0, nraw = 0;

	for (int i = 13; i < argc; i++) {
		char *eq = strchr(argv[i], '=');

		if (!eq)
			continue;
		if (argv[i][0] == 'W' || argv[i][0] == 'w') {
			if (nraw < 32) {
				rawoff[nraw] = (unsigned)strtoul(argv[i] + 1, NULL, 0);
				rawval[nraw] = (unsigned)strtoul(eq + 1, NULL, 0);
				nraw++;
			}
			continue;
		}
		if (!strncmp(argv[i], "tm", 2)) {
			sscanf(eq + 1, "%u,%u,%u", &tmidx, &tmpat, &tmsrc);
			tm = 1;
			continue;
		}
		if (!strncmp(argv[i], "sv", 2)) {
			sscanf(eq + 1, "%u,%u,%u", &svidx, &svfmt, &svbits);
			sv = 1;
			continue;
		}
		if (nset < 16) {
			setoff[nset] = (unsigned)strtoul(argv[i], NULL, 0);
			setval[nset] = (unsigned)strtoul(eq + 1, NULL, 0);
			nset++;
		}
	}

	/* ---- 2. окно регистров, чтобы видеть, что происходит ---- */
	int ispfd = open("/dev/camera-isp", O_RDWR);

	if (ispfd < 0) { perror("/dev/camera-isp"); return 1; }

	volatile uint32_t *seninf = (volatile uint32_t *)mmap(
		NULL, SENINF_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		ispfd, SENINF_BASE);

	if (seninf == MAP_FAILED) { perror("mmap seninf"); return 1; }

	volatile uint32_t *imgsys = (volatile uint32_t *)mmap(
		NULL, IMGSYS_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		ispfd, IMGSYS_BASE);
	volatile uint32_t *ana = (volatile uint32_t *)mmap(
		NULL, MIPIANA_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
		ispfd, MIPIANA_BASE);

	const unsigned iwords = IMGSYS_SIZE / 4;
	const unsigned awords = MIPIANA_SIZE / 4;
	uint32_t *ib = (uint32_t *)malloc(iwords * 4), *ia = (uint32_t *)malloc(iwords * 4);
	uint32_t *ab = (uint32_t *)malloc(awords * 4), *aa = (uint32_t *)malloc(awords * 4);

	printf("   ISP: %s, аналог MIPI: %s\n",
	       imgsys == MAP_FAILED ? "не отображён" : "есть",
	       ana == MAP_FAILED ? "не отображён" : "есть");

	const unsigned words = SENINF_SIZE / 4;
	uint32_t *before = (uint32_t *)malloc(words * 4);
	uint32_t *after  = (uint32_t *)malloc(words * 4);

	snap(seninf, before, words);
	printf("2. регистры приёмника отображены (0x%08x, %u слов)\n",
	       SENINF_BASE, words);
	/*
	 * Сверка, что смотрим в те же регистры, что и ядро: по смещению 0x10
	 * лежит SENINF_TOP_CAM_MUX_CTRL, в покое там 0x76543210.
	 */
	printf("   сверка: +0x00=%08x +0x08=%08x +0x10=%08x +0x14=%08x\n",
	       seninf[0], seninf[2], seninf[4], seninf[5]);

	/* ---- 1. сенсор ---- */
	int fd = open("/dev/kd_camera_hw", O_RDWR);

	if (fd < 0) { perror("/dev/kd_camera_hw"); return 1; }

	int32_t drv = 0;
	uint8_t nothing[4] = { 0 };

	if (feature(fd, cam, SENSOR_FEATURE_SET_DRIVER, &drv, 4) < 0 || drv < 0) {
		printf("драйвер сенсора не подобран (%d)\n", drv);
		return 1;
	}
	if (feature(fd, cam, SENSOR_FEATURE_OPEN, nothing, 4) < 0) {
		printf("открытие сенсора: %s\n", strerror(errno));
		return 1;
	}

	printf("1. сенсор открыт (питание и такты поданы)\n");

	/* ---- 3. вендорская настройка приёмника ---- */
	/*
	 * Развязать язык вендорскому коду. Подробные сообщения он печатает
	 * только когда взведён младший бит в NSCam::Utils::ULog::ULogger::
	 * sDetailsLevel — это обычная переменная в libmtkcam_ulog.so, и её
	 * видно по имени. Без неё в журнал попадают только ошибки, а нам нужны
	 * строки вида "CSI2-%d TermDelay:%d SettleDelay:%d ... LaneNum:%d".
	 */
	{
		void *ulog = dlopen("/vendor/lib64/libmtkcam_ulog.so", RTLD_NOW);

		if (ulog) {
			uint32_t *lvl = (uint32_t *)dlsym(ulog,
				"_ZN5NSCam5Utils4ULog7ULogger13sDetailsLevelE");
			uint32_t *msk = (uint32_t *)dlsym(ulog,
				"_ZN5NSCam5Utils4ULog7ULogger18sNormalDetailsMaskE");

			if (lvl) *lvl = 0xffffffffu;
			if (msk) *msk = 0xffffffffu;
			printf("   подробные сообщения: %s\n", lvl ? "включены" : "переменной нет");
		}
	}

	void *lib = dlopen("/vendor/lib64/libcam.halsensor.so", RTLD_NOW);

	if (!lib) { printf("не загрузилась: %s\n", dlerror()); return 1; }
	printf("3. libcam.halsensor.so загружена\n");

	typedef void *(*fn_create)(void);
	typedef int (*fn_init)(void *);
	typedef int (*fn_cfg)(void *, void *, void *, void *, bool);

	fn_create create = (fn_create)dlsym(lib, "_ZN9SeninfDrv14createInstanceEv");
	fn_init   init   = (fn_init)dlsym(lib, "_ZN12SeninfDrvImp4initEv");
	typedef int (*fn_tg)(void *, unsigned, void *, void *);
	typedef int (*fn_mux)(void *, unsigned, unsigned);

	fn_cfg    cfgsi  = (fn_cfg)dlsym(lib,
		"_ZN12SeninfDrvImp17configSensorInputEP20SENINF_CONFIG_STRUCTPN5NSCam17SensorDynamicInfoEP23ACDK_SENSOR_INFO_STRUCTb");

	fn_tg  configTg = (fn_tg)dlsym(lib,
		"_ZN12SeninfDrvImp8configTgE18SENINF_TOP_P1_ENUMP20SENINF_CONFIG_STRUCTPN5NSCam17SensorDynamicInfoE");
	typedef int (*fn_svcfg)(void *, unsigned, unsigned, unsigned, unsigned);
	typedef int (*fn_svgrab)(void *, unsigned, uint64_t, uint64_t, uint64_t, uint64_t);
	typedef int (*fn_svvf)(void *, unsigned, uint64_t);

	fn_svcfg  svCfg  = (fn_svcfg)dlsym(lib,
		"_ZN12SeninfDrvImp8setSVCfgEj14TG_FORMAT_ENUM21SENSOR_DATA_BITS_ENUMj");
	fn_svgrab svGrab = (fn_svgrab)dlsym(lib, "_ZN12SeninfDrvImp14setSVGrabRangeEjmmmm");
	fn_svvf   svVf   = (fn_svvf)dlsym(lib, "_ZN12SeninfDrvImp19setSVViewFinderModeEjm");

	typedef int (*fn_tm)(void *, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
	typedef int (*fn_smux)(void *, uint64_t, uint64_t, uint64_t, unsigned, unsigned, unsigned);

	fn_tm   tmCtl  = (fn_tm)dlsym(lib,
		"_ZN12SeninfDrvImp12setTG_TM_CtlEjjjjjj");
	fn_smux sMux   = (fn_smux)dlsym(lib,
		"_ZN12SeninfDrvImp16setSeninfMuxCtrlEmmm18SENINF_SOURCE_ENUM14TG_FORMAT_ENUMj");

	fn_mux camMux = (fn_mux)dlsym(lib,
		"_ZN12SeninfDrvImp16setTopCamMuxCtrlE18SENINF_TOP_P1_ENUM15SENINF_MUX_ENUM");

	if (!create || !init || !cfgsi) {
		printf("нет символов: create=%p init=%p cfg=%p\n",
		       (void *)create, (void *)init, (void *)cfgsi);
		return 1;
	}

	void *drvobj = create();

	printf("4. SeninfDrv::createInstance() -> %p\n", drvobj);
	if (!drvobj) return 1;

	int r = init(drvobj);

	printf("5. SeninfDrvImp::init() -> %d\n", r);

	/*
	 * Внутри объекта драйвера лежат адреса блоков регистров. Смещения
	 * известны из кода: 104 — ISP, 112 — приёмник, 120 — аналог MIPI, а с
	 * 168 идёт таблица мультиплексоров (setSeninfMuxCtrl берёт
	 * this->[168 + номер*8]). Переведём их в смещения от базы приёмника —
	 * тогда мультиплексоры можно будет трогать и напрямую.
	 */
	{
		void **o = (void **)drvobj;
		unsigned long sbase = (unsigned long)o[112 / 8];

		printf("   базы: ISP=%p приёмник=%p аналог=%p\n",
		       o[104 / 8], o[112 / 8], o[120 / 8]);
		printf("   мультиплексоры (смещение от приёмника):");
		for (int i = 0; i < 6; i++) {
			unsigned long m = (unsigned long)o[168 / 8 + i];

			if (m > sbase && m - sbase < SENINF_SIZE)
				printf(" [%d]=+0x%04lx", i, m - sbase);
			else
				printf(" [%d]=%p", i, (void *)m);
		}
		printf("\n");
	}

	/* SENINF_CONFIG_STRUCT: раскладки нет, берём с запасом и обнуляем */
	uint8_t cfg[1024];
	uint8_t dyn[256];
	uint8_t sinfo[4096];

	memset(cfg, 0, sizeof(cfg));
	memset(dyn, 0, sizeof(dyn));
	memset(sinfo, 0, sizeof(sinfo));

	uint32_t *c = (uint32_t *)cfg;

	c[8 / 4]   = f8;
	c[16 / 4]  = 0;		/* начало окна по горизонтали */
	c[20 / 4]  = width;
	c[24 / 4]  = 0;		/* начало окна по вертикали */
	c[28 / 4]  = height;
	c[32 / 4]  = f32;
	c[56 / 4]  = f56;
	c[60 / 4]  = f60;
	c[80 / 4]  = 8;		/* источник: CSI-2, проверяется явно */
	c[88 / 4]  = csiport;	/* номер входа CSI: с него начинается SENINF_CSI_PARA */

	/*
	 * Остальные поля параметров полос вызывающая сторона обязана заполнить
	 * сама: getCSIpara() ставит только два слова по таблице входа CSI, а
	 * число полос, задержки и прочее ждёт готовыми. У штатного HAL их
	 * берёт HalSensor::configure из сведений о сенсоре; у нас заголовков
	 * нет, поэтому поля задаются парами «смещение=значение» и находятся по
	 * тому, что печатает сама библиотека (строка «CSI2-… LaneNum:…»).
	 */
	for (int i = 0; i < nset; i++) {
		c[setoff[i] / 4] = setval[i];
		printf("   поле +%u = %u\n", setoff[i], setval[i]);
	}
	c[252 / 4] = cam;	/* номер гнезда */

	/*
	 * Частота пикселей MIPI — 64-битное поле. Какое из двух, зависит от
	 * того, какой мультиплексор приёмника достался: при номере меньше 2
	 * берётся смещение 72, иначе 144 (в коде это
	 * `cmp w26,#2; csel x8, #72, #144, lo`). Заполняем оба: без этого
	 * configInputForCSI отвечает
	 *   "sensor does not provide mipi_pixel_rate 0 or pixel_rate 0".
	 * Для предпросмотра S5K3L6 это 227 200 000 (imgsensor_info.pre).
	 */
	uint64_t *q = (uint64_t *)cfg;

	q[72 / 8]  = rate;
	q[144 / 8] = rate;

	snap(seninf, before, words);
	if (imgsys != MAP_FAILED) snap(imgsys, ib, iwords);
	if (ana != MAP_FAILED) snap(ana, ab, awords);
	r = cfgsi(drvobj, cfg, dyn, sinfo, true);
	printf("6. configSensorInput() -> %d\n", r);
	snap(seninf, after, words);
	diff(before, after, words, "приёмник после настройки", 6);
	if (imgsys != MAP_FAILED) { snap(imgsys, ia, iwords); diff(ib, ia, iwords, "ISP после настройки", 32); }
	if (ana != MAP_FAILED) { snap(ana, aa, awords); diff(ab, aa, awords, "аналог MIPI после настройки", 6); }

	/*
	 * Приёмник настроен, но поток ещё некуда девать: надо настроить
	 * генератор кадров (TG) у приёмника кадров P1 и проложить к нему
	 * маршрут от мультиплексора. Какой номер P1 у какого блока — в
	 * заголовках не описано, поэтому номер задаётся снаружи и перебирается.
	 */
	if (configTg && p1) {
		snap(seninf, before, words);
		if (imgsys != MAP_FAILED) snap(imgsys, ib, iwords);
		r = configTg(drvobj, p1, cfg, dyn);
		printf("6a. configTg(%u) -> %d\n", p1, r);
		snap(seninf, after, words);
		diff(before, after, words, "приёмник после configTg", 6);
		if (imgsys != MAP_FAILED) { snap(imgsys, ia, iwords); diff(ib, ia, iwords, "ISP после configTg", 8); }
	}
	if (camMux && p1) {
		snap(seninf, before, words);
		r = camMux(drvobj, p1, mux);
		printf("6b. setTopCamMuxCtrl(%u, %u) -> %d\n", p1, mux, r);
		snap(seninf, after, words);
		diff(before, after, words, "приёмник после маршрута", 6);
	}

	printf("   сведения о потоке (dyn): ");
	for (int i = 0; i < 24; i++)
		printf("%08x ", ((uint32_t *)dyn)[i]);
	printf("\n");

	/*
	 * Поток включаем только теперь. Приёмник должен быть настроен до того,
	 * как сенсор начнёт передавать: иначе он не поймает начало кадра.
	 */
	struct sensorctrl sc = { cam, 0, NULL, NULL };

	snap(seninf, before, words);
	if (ioctl(fd, KDIMGSENSORIOC_X_CONTROL, &sc) < 0) {
		printf("запуск сценария: %s\n", strerror(errno));
		return 1;
	}
	printf("7. сценарий предпросмотра запущен (%ux%u)\n", width, height);
	usleep(200000);
	snap(seninf, after, words);
	diff(before, after, words, "приёмник после запуска потока", 16);

	/* ---- 4. идут ли данные: что шевелится само ---- */
	/*
	 * Последний недостающий шаг. configSensorInput() прописал окно захвата
	 * в генератор кадров (видно по регистрам: 0x838 = 2104 и 0x618 = 1560),
	 * но сам генератор оставил выключенным: в TG_VF_CON младший бит нулевой.
	 * Его взводит драйвер конвейера P1, которого у нас нет, — взводим сами.
	 *
	 * Раскладка ISP 5.0, от базы блока:
	 *   +0x410 TG_SEN_MODE   +0x414 TG_VF_CON
	 *   +0x418 TG_SEN_GRAB_PXL  +0x41c TG_SEN_GRAB_LIN  +0x44c TG_INTER_ST
	 * CAM_A сидит по 0x15004000, CAMSV — по 0x1500d000.
	 */
	if (imgsys != MAP_FAILED && vf) {
		const unsigned cam_vf = 0x4414 / 4, sv_vf = 0xd414 / 4;
		const unsigned cam_st = 0x444c / 4, sv_st = 0xd44c / 4;

		printf("7a. включаю генератор кадров: CAM_A %08x, CAMSV %08x\n",
		       imgsys[cam_vf], imgsys[sv_vf]);
		if (vf & 1)
			imgsys[cam_vf] = imgsys[cam_vf] | 1u;
		if (vf & 2)
			imgsys[sv_vf] = imgsys[sv_vf] | 1u;
		usleep(100000);
		printf("    стало: CAM_A %08x, CAMSV %08x\n",
		       imgsys[cam_vf], imgsys[sv_vf]);
		printf("    счётчик строк CAM_A: ");
		for (int i = 0; i < 6; i++) {
			printf("%08x ", imgsys[cam_st]);
			usleep(20000);
		}
		printf("\n    счётчик строк CAMSV: ");
		for (int i = 0; i < 6; i++) {
			printf("%08x ", imgsys[sv_st]);
			usleep(20000);
		}
		printf("\n");
	}

	/*
	 * Идёт ли поток у самого сенсора. Проверка не лишняя: вендорская
	 * библиотека тоже открывает /dev/kd_camera_hw, а закрытие этого узла
	 * гасит такты и сбрасывает ножки — то есть выключает камеру.
	 */
	{
		int bus = open("/dev/i2c-2", O_RDWR);

		if (bus >= 0) {
			unsigned char a[2] = { 0x00, 0x05 }, v = 0;
			struct i2c_msg m[2] = {
				{ 0x10, 0, 2, a },
				{ 0x10, I2C_M_RD, 1, &v },
			};
			struct i2c_rdwr_ioctl_data d = { m, 2 };

			printf("7b. счётчик кадров сенсора: ");
			for (int i = 0; i < 8; i++) {
				if (ioctl(bus, I2C_RDWR, &d) >= 0)
					printf("%02x ", v);
				else
					printf("-- ");
				usleep(40000);
			}
			printf("\n");
			close(bus);
		}
	}

	/*
	 * Путь через CAMSV — он проще, чем полный конвейер P1: у него нет
	 * обработки, только приём строк и выгрузка. Настраиваем его
	 * вендорскими же функциями, чтобы не угадывать биты.
	 * Формат кадра у нас RAW, десять бит на точку.
	 */
	if (sv && svCfg && svGrab && svVf) {
		if (imgsys != MAP_FAILED) snap(imgsys, ib, iwords);
		printf("7e. CAMSV %u: setSVCfg(%u,%u) setSVGrabRange(0,%u,0,%u) setSVViewFinderMode(1)\n",
		       svidx, svfmt, svbits, width, height);
		svCfg(drvobj, svidx, svfmt, svbits, 0);
		svGrab(drvobj, svidx, 0, width, 0, height);
		svVf(drvobj, svidx, 1);
		usleep(150000);
		if (imgsys != MAP_FAILED) {
			snap(imgsys, ia, iwords);
			diff(ib, ia, iwords, "ISP после настройки CAMSV", 12);
			printf("    счётчик CAMSV0 (0x5044c) = %08x, CAMSV1 (0x5144c) = %08x\n",
			       imgsys[0x5044c / 4], imgsys[0x5144c / 4]);
		}
	}

	/*
	 * Развилка: включить собственный генератор тестовой картинки в приёмнике.
	 * Если после этого блок захвата начнёт считать кадры — значит он жив и
	 * беда в звене «приёмник → блок захвата». Если не начнёт — беда в самом
	 * блоке захвата.
	 */
	if (tm && tmCtl) {
		if (imgsys != MAP_FAILED) snap(imgsys, ib, iwords);
		printf("7f. тестовая картинка: setTG_TM_Ctl(%u,1,%u,%u,%u,0)\n",
		       tmidx, width, height, tmpat);
		tmCtl(drvobj, tmidx, 1, width, height, tmpat, 0);
		if (sMux && tmsrc)
			printf("    источник мультиплексора -> %u: %d\n", tmsrc,
			       sMux(drvobj, 0, 0, 0, tmsrc, 0, 0));
		usleep(200000);
		if (imgsys != MAP_FAILED) {
			snap(imgsys, ia, iwords);
			diff(ib, ia, iwords, "ISP после тестовой картинки", 14);
			printf("    счётчики: CAM_A %08x CAM_B %08x CAMSV %08x\n",
			       imgsys[0x444c / 4], imgsys[0x644c / 4], imgsys[0x5044c / 4]);
		}
	}

	/*
	 * Прямые записи в регистры ISP: аргументы вида W<смещение>=<значение>.
	 * Нужны, пока не восстановлена вся последовательность вендорского
	 * конвейера P1 — ей и включать блоки захвата.
	 */
	for (int i = 0; i < nraw; i++) {
		if (imgsys == MAP_FAILED)
			break;
		printf("7d. ISP +0x%05x: %08x -> %08x\n", rawoff[i],
		       imgsys[rawoff[i] / 4], rawval[i]);
		imgsys[rawoff[i] / 4] = rawval[i];
	}
	if (nraw) {
		usleep(150000);
		printf("    после записей — счётчики: CAM_A %08x CAM_B %08x CAMSV %08x\n",
		       imgsys[0x444c / 4], imgsys[0x644c / 4], imgsys[0x5044c / 4]);
		snap(imgsys, ib, iwords);
		usleep(300000);
		snap(imgsys, ia, iwords);
		diff(ib, ia, iwords, "живое в ISP после записей", 16);
	}

	/* содержимое приёмника и аналоговой части — глазами, а не разницей */
	{
		printf("7c. приёмник CSI-2 (0x0a00):");
		for (int i = 0; i < 12; i++)
			printf(" %08x", seninf[(0x0a00 / 4) + i]);
		printf("\n    прерывания/ошибки (0x0a40):");
		for (int i = 0; i < 8; i++)
			printf(" %08x", seninf[(0x0a40 / 4) + i]);
		printf("\n    мультиплексор (0x0d00):");
		for (int i = 0; i < 8; i++)
			printf(" %08x", seninf[(0x0d00 / 4) + i]);
		printf("\n    верх (0x0000):");
		for (int i = 0; i < 12; i++)
			printf(" %08x", seninf[i]);
		if (ana != MAP_FAILED) {
			printf("\n    аналог MIPI 0x11c10000:");
			for (int i = 0; i < 12; i++)
				printf(" %08x", ana[i]);
		}
		if (imgsys != MAP_FAILED) {
			printf("\n    настройка MIPI (0x1500c000):");
			for (int i = 0; i < 12; i++)
				printf(" %08x", imgsys[(0xc000 / 4) + i]);
		}
		printf("\n");
	}

	printf("8. смотрю, что меняется само за 200 мс:\n");
	snap(seninf, before, words);
	usleep(200000);
	snap(seninf, after, words);
	diff(before, after, words, "живые счётчики в приёмнике", 24);

	if (imgsys != MAP_FAILED) {
		snap(imgsys, ib, iwords);
		usleep(300000);
		snap(imgsys, ia, iwords);
		diff(ib, ia, iwords, "живые счётчики в ISP/CAMSV", 16);
	}

	printf("\nсенсор остаётся включённым, жду сигнала\n");
	fflush(stdout);
	pause();
	return 0;
}
