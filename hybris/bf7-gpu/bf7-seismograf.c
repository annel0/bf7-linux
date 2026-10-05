/*
 * BF7: сейсмограф — телефон лежит и пишет, как дрожит то, на чём он лежит.
 *
 * Акселерометр — sc7a20 (клон LIS3DH) на i2c-1, адрес 0x18. На Android им
 * правит сопроцессор SCP (своя прошивка, драйверы sc7a20/mxc4005x), а ядру
 * сенсорный узел не нужен: шина общая, и с процессора датчик читается
 * напрямую через /dev/i2c-1. SCP его не трогает, пока датчик никто не
 * включил; при выходе возвращаем как было — CTRL_REG1 = 0x07 (выключен).
 *
 * Разрешение — только 8 бит (шаг 16 мg при ±2 g): младший байт всегда 0,
 * при любых HR/FS/ODR (проверено 2026-09-25, по даташиту должно быть 12).
 * Покой даёт ~8–12 мg скз от дрожания младшего разряда — это фон; шаги
 * рядом, стук по столу, хлопок двери видны над ним, далёкий транспорт нет.
 *
 * 50 Гц, FIFO в потоковом режиме (32 отсчёта), забираем раз в 500 мс. На
 * 100 Гц с забором раз в 200 мс служба будила процессор 110 раз в секунду
 * (каждое чтение I2C — прерывание); 50 Гц шагам и стукам хватает.
 * Сила тяжести убирается медленным средним (~2 с) по каждой оси; остаток —
 * тряска. Раз в секунду в /var/lib/bf7-seismograf/ГГГГ-ММ-ДД.csv строка:
 *   время_unix,скз_мg,пик_мg,отсчётов
 * Пока телефон спит, строк нет — на графике это пропуск.
 *
 * Заодно — ориентация для автоповорота (датчиком владеет одна служба). Раз в
 * секунду по вектору силы тяжести: normal / right-up / left-up / bottom-up,
 * как у iio-sensor-proxy; при смене пишется в /run/bf7-akselerometr/orientaciya
 * (её отдаёт phosh служба bf7-sensor-proxy). Лёжа плашмя — прежняя.
 * Ось y — вверх по портрету, как у Android, а x у этого датчика смотрит
 * ВЛЕВО: хозяин проверил руками 2026-09-25 — на левый бок экран вставал
 * как на правый. Поэтому знак x по умолчанию −1; переопределить можно в
 * /etc/bf7/akselerometr строкой «x 1» или «x -1».
 *
 * Три режима по питанию (2026-09-30: служба была главным будильником
 * процессора в покое — ~74 пробуждения в секунду при погашенном экране):
 *   на зарядке (usb или ac online) — всё как выше: 50 Гц, сейсмограмма,
 *     ориентация;
 *   на батарее с горящим экраном — только ориентация: датчик на 10 Гц, забор
 *     раз в секунду, CSV не пишется;
 *   на батарее с погашенным экраном — датчик выключен (CTRL_REG1 = 0x07),
 *     служба раз в 2 с смотрит, не сменилось ли питание или экран.
 * Экран — по /sys/class/drm/card1-DSI-1/enabled (яркость подсветки врёт).
 *
 * Сборка: gcc -O2 -o bf7-seismograf bf7-seismograf.c -lm
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define SHINA		"/dev/i2c-1"
#define ADRES		0x18
#define KATALOG		"/var/lib/bf7-seismograf"

#define WHO_AM_I	0x0f
#define CTRL_REG1	0x20
#define CTRL_REG4	0x23
#define CTRL_REG5	0x24
#define OUT_X_L		0x28
#define FIFO_CTRL	0x2e
#define FIFO_SRC	0x2f
#define AVTO		0x80	/* автоприращение адреса при чтении подряд */

static int fd = -1;
static volatile sig_atomic_t stop;

static int pishi(uint8_t reg, uint8_t v)
{
	uint8_t b[2] = { reg, v };
	struct i2c_msg m = { ADRES, 0, 2, b };
	struct i2c_rdwr_ioctl_data d = { &m, 1 };

	return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

static int chitay(uint8_t reg, uint8_t *buf, uint16_t n)
{
	struct i2c_msg m[2] = {
		{ ADRES, 0, 1, &reg },
		{ ADRES, I2C_M_RD, n, buf },
	};
	struct i2c_rdwr_ioctl_data d = { m, 2 };

	return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

/*
 * k отсчётов из FIFO. Каждый — отдельное чтение 6 байт с OUT_X_L: sc7a20, в
 * отличие от LIS3DH, после 0x2d не заворачивает адрес на 0x28, и длинное
 * чтение подряд отдаёт соседние регистры, а FIFO не опустошается (замер:
 * 160 «отсчётов» в секунду и 1 g тряски на неподвижном телефоне). До 16
 * отсчётов за один вызов I2C_RDWR — пара сообщений на отсчёт.
 */
static int chitay_fifo(uint8_t *raw, int k)
{
	static uint8_t reg = OUT_X_L | AVTO;

	for (int i = 0; i < k; i += 16) {
		struct i2c_msg m[32];
		int c = k - i < 16 ? k - i : 16;
		struct i2c_rdwr_ioctl_data d = { m, (uint32_t)(2 * c) };

		for (int j = 0; j < c; j++) {
			m[2 * j] = (struct i2c_msg){ ADRES, 0, 1, &reg };
			m[2 * j + 1] = (struct i2c_msg){ ADRES, I2C_M_RD, 6, raw + 6 * (i + j) };
		}
		if (ioctl(fd, I2C_RDWR, &d) < 0)
			return -1;
	}
	return 0;
}

static void na_vyhod(int s)
{
	(void)s;
	stop = 1;
}

/* как было до нас: датчик выключен, FIFO в обходе */
static void vernut(void)
{
	pishi(CTRL_REG5, 0x00);
	pishi(FIFO_CTRL, 0x00);
	pishi(CTRL_REG1, 0x07);
}

enum { POLNYJ, ORIENTACIYA, VYKLYUCHEN };

static int proverit(void)
{
	uint8_t id = 0;

	if (chitay(WHO_AM_I, &id, 1) < 0 || id != 0x11) {
		fprintf(stderr, "bf7-seismograf: нет sc7a20 на %s:0x%02x (WHO_AM_I=0x%02x)\n",
			SHINA, ADRES, id);
		return -1;
	}
	return 0;
}

/*
 * POLNYJ — 50 Гц (0x47), ORIENTACIYA — 10 Гц (0x27); оси X/Y/Z, BDU +
 * высокое разрешение, ±2 g, FIFO потоком. Через обход FIFO очищается:
 * иначе после простоя в нём лежат старые отсчёты.
 */
static int vklyuchit(int rezhim)
{
	if (rezhim == VYKLYUCHEN) {
		vernut();
		return 0;
	}
	if (pishi(CTRL_REG4, 0x88) < 0 || pishi(CTRL_REG5, 0x40) < 0 ||
	    pishi(FIFO_CTRL, 0x00) < 0 || pishi(FIFO_CTRL, 0x80) < 0 ||
	    pishi(CTRL_REG1, rezhim == POLNYJ ? 0x47 : 0x27) < 0)
		return -1;
	return 0;
}

static int prochest_1(const char *put)
{
	char c = 0;
	int f = open(put, O_RDONLY | O_CLOEXEC);

	if (f < 0)
		return -1;
	if (read(f, &c, 1) != 1)
		c = 0;
	close(f);
	return c;
}

/* BF7_SEISMO_BATAREYA=1 — считать, что кабеля нет (проверка режимов батареи) */
static int bez_kabelya;

static int kakoj_rezhim(void)
{
	if (!bez_kabelya && (prochest_1("/sys/class/power_supply/usb/online") == '1' ||
	    prochest_1("/sys/class/power_supply/ac/online") == '1'))
		return POLNYJ;
	/* «enabled» / «disabled»; нет файла — считаем, что горит */
	if (prochest_1("/sys/class/drm/card1-DSI-1/enabled") == 'd')
		return VYKLYUCHEN;
	return ORIENTACIYA;
}

#define ORIENT_KAT	"/run/bf7-akselerometr"

static const char *orient = "";
static int znak_x = -1;

static void chitat_nastroyku(void)
{
	FILE *f = fopen("/etc/bf7/akselerometr", "r");
	char os[8];
	int zn;

	if (!f)
		return;
	while (fscanf(f, "%7s %d", os, &zn) == 2)
		if (!strcmp(os, "x") && (zn == 1 || zn == -1))
			znak_x = zn;
	fclose(f);
}

/*
 * Сектор по углу a = atan2(x, y): 0 — портрет, 90 — правый край вверх,
 * -90 — левый, 180 — вверх ногами. Сменить можно, только зайдя в новый
 * сектор глубже чем на 10° (гистерезис), и не плашмя: проекция силы тяжести
 * на плоскость экрана должна быть больше половины её самой.
 */
static void obnovit_orientaciyu(const double g[3])
{
	static const char *imena[4] = { "normal", "right-up", "bottom-up", "left-up" };
	double x = g[0] * znak_x, y = g[1], z = g[2];
	double v_ploskosti = sqrt(x * x + y * y);
	double a, cel;
	const char *novoe;
	int sektor;
	FILE *f;

	if (v_ploskosti < 0.5 * sqrt(x * x + y * y + z * z) || v_ploskosti < 300)
		return;
	a = atan2(x, y) * 180 / M_PI;			/* −180…180 */
	sektor = (int)floor((a + 45 + 360) / 90) % 4;	/* 0 норм, 1 справа… */
	cel = sektor * 90.0;
	if (cel > 180)
		cel -= 360;
	{
		double d = fabs(a - cel);

		if (d > 180)
			d = 360 - d;
		if (d > 35 && *orient)
			return;			/* у края сектора — держим прежнюю */
	}
	novoe = imena[sektor];
	if (!strcmp(novoe, orient))
		return;
	orient = novoe;
	mkdir(ORIENT_KAT, 0755);
	f = fopen(ORIENT_KAT "/orientaciya.new", "w");
	if (f) {
		fprintf(f, "%s\n", orient);
		fclose(f);
		rename(ORIENT_KAT "/orientaciya.new", ORIENT_KAT "/orientaciya");
	}
}

static FILE *zhurnal;
static int zhurnal_den = -1;

static FILE *zhurnal_na(time_t t)
{
	struct tm tm;
	char put[128];

	localtime_r(&t, &tm);
	if (zhurnal && tm.tm_yday == zhurnal_den)
		return zhurnal;
	if (zhurnal)
		fclose(zhurnal);
	snprintf(put, sizeof(put), KATALOG "/%04d-%02d-%02d.csv",
		 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
	zhurnal = fopen(put, "a");
	zhurnal_den = tm.tm_yday;
	return zhurnal;
}

int main(void)
{
	double sred[3] = { 0, 0, 0 };
	int nachato = 0;
	double summa2 = 0, pik = 0;
	long n = 0;
	time_t sekunda = 0;
	time_t sbros = 0;
	time_t ne_pisat_do = 0;
	time_t proverka = 0;
	int rezhim;

	/* BF7_SEISMO_SYROJ=1 — печатать сырые отсчёты (мg по осям) для отладки */
	int syroj = getenv("BF7_SEISMO_SYROJ") != NULL;

	bez_kabelya = getenv("BF7_SEISMO_BATAREYA") != NULL;

	chitat_nastroyku();
	mkdir(KATALOG, 0755);
	fd = open(SHINA, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("bf7-seismograf: " SHINA);
		return 1;
	}
	signal(SIGTERM, na_vyhod);
	signal(SIGINT, na_vyhod);
	if (proverit() < 0) {
		vernut();
		return 1;
	}

	rezhim = -1;
	while (!stop) {
		uint8_t src, raw[32 * 6];
		struct timespec pauza = { 0, 500 * 1000 * 1000 };
		time_t t;
		int k, novyj;

		/* питание и экран — не чаще раза в 2 с: это тоже чтение файлов */
		t = time(NULL);
		if (rezhim < 0 || t - proverka >= 2 || t < proverka) {
			proverka = t;
			novyj = kakoj_rezhim();
			if (novyj != rezhim) {
				if (vklyuchit(novyj) < 0) {
					nanosleep(&(struct timespec){ 2, 0 }, NULL);
					continue;
				}
				rezhim = novyj;
				nachato = 0;
				ne_pisat_do = t + 3;
				sekunda = 0;
				summa2 = 0;
				pik = 0;
				n = 0;
				if (zhurnal)
					fflush(zhurnal);
			}
		}
		if (rezhim == VYKLYUCHEN) {
			nanosleep(&(struct timespec){ 2, 0 }, NULL);
			continue;
		}
		if (rezhim == ORIENTACIYA)
			pauza = (struct timespec){ 1, 0 };

		nanosleep(&pauza, NULL);
		if (chitay(FIFO_SRC, &src, 1) < 0)
			continue;
		k = src & 0x1f;
		if (src & 0x40)		/* переполнение: проспали (сон) — старое не нужно */
			k = 32;
		if (k == 0)
			continue;
		if (chitay_fifo(raw, k) < 0)
			continue;

		t = time(NULL);
		/*
		 * После сна (пропуск больше 5 с) среднее заводим заново. Первые 3 с
		 * после включения и после сна не пишем: датчик выходит на режим, в
		 * FIFO старые отсчёты — выходил «толчок» в 300+ мg на неподвижном
		 * телефоне.
		 */
		if (sekunda && t - sekunda > 5) {
			nachato = 0;
			ne_pisat_do = t + 3;
		}
		/* ориентации пропуск не нужен: в FIFO только свежее (очищен) */
		if (t < ne_pisat_do && rezhim == POLNYJ) {
			sekunda = t;
			summa2 = 0;
			pik = 0;
			n = 0;
			continue;
		}

		for (int i = 0; i < k; i++) {
			double a[3], d2 = 0;

			for (int o = 0; o < 3; o++) {
				int16_t v = (int16_t)(raw[i * 6 + 2 * o] | raw[i * 6 + 2 * o + 1] << 8);
				a[o] = (v >> 4);	/* в мg; у нашего датчика шаг 16 */
			}
			if (syroj)
				printf("%.0f %.0f %.0f\n", a[0], a[1], a[2]);
			if (!nachato) {
				memcpy(sred, a, sizeof(sred));
				nachato = 1;
			}
			for (int o = 0; o < 3; o++) {
				double r = a[o] - sred[o];

				/* постоянная ~2 с: 0.01 при 50 Гц, 0.05 при 10 Гц */
				sred[o] += r * (rezhim == POLNYJ ? 0.01 : 0.05);
				d2 += r * r;
			}
			summa2 += d2;
			if (sqrt(d2) > pik)
				pik = sqrt(d2);
			n++;
		}

		if (t != sekunda) {
			if (sekunda && n > 0) {
				FILE *f = rezhim == POLNYJ ? zhurnal_na(sekunda) : NULL;

				obnovit_orientaciyu(sred);
				if (f)
					fprintf(f, "%ld,%.2f,%.1f,%ld\n", (long)sekunda,
						sqrt(summa2 / n), pik, n);
			}
			if (zhurnal && t - sbros >= 10) {
				fflush(zhurnal);
				sbros = t;
			}
			sekunda = t;
			summa2 = 0;
			pik = 0;
			n = 0;
		}
	}

	if (zhurnal)
		fclose(zhurnal);
	vernut();
	close(fd);
	return 0;
}
