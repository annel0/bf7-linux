/*
 * BF7: NMEA из GPS MediaTek для обычного Linux.
 *
 * Цепочка: чип (connsys) ← gps_drv.ko (/dev/stpgps) ← mnld из /vendor ←
 * gps.default.so (legacy HAL, говорит с mnld через абстрактные сокеты
 * mtk_hal2mnl/mtk_mnl2hal) ← эта программа через libhybris.
 *
 * Раздаёт предложения NMEA всем, кто подключился к Unix-сокету
 * /run/bf7-gps/nmea.sock (geoclue: [network-nmea] nmea-socket=...).
 * Приёмник включён, только пока есть хоть один слушатель: GPS заметно
 * ест батарею, а geoclue держит сокет лишь пока кому-то нужно место.
 *
 *   bf7-gps-nmea             — служба
 *   bf7-gps-nmea --proba N   — N секунд печатать NMEA и спутники в stdout
 *
 * У HAL MediaTek структура обратных вызовов длиннее AOSP-овской на два
 * указателя: «gpshal_gpscbs_save: Bad callback, size: 96, expected: 112» —
 * с 96 байтами (test_gps из libhybris) он молча не шлёт ничего.
 */
#define _GNU_SOURCE
#include <android-config.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <hardware/gps.h>

#define KATALOG "/run/bf7-gps"
#define SOKET KATALOG "/nmea.sock"
#define MAKS_KLIENTOV 8

/* Два хвостовых обратных вызова MediaTek. Что именно HAL в них передаёт,
 * нам не важно: аргументы в регистрах, заглушка их просто не читает. */
static void mtk_zaglushka(void) { }

struct bf7_callbacks {
	GpsCallbacks base;
	void (*mtk_ext1)(void);
	void (*mtk_ext2)(void);
};

static const GpsInterface *gps;
static int proba;
static volatile sig_atomic_t stop;

static pthread_mutex_t zamok = PTHREAD_MUTEX_INITIALIZER;
static int klienty[MAKS_KLIENTOV];
static int n_klientov;

static void razdat(const char *s, int dlina)
{
	pthread_mutex_lock(&zamok);
	for (int i = 0; i < n_klientov; ) {
		if (send(klienty[i], s, dlina, MSG_NOSIGNAL | MSG_DONTWAIT) < 0 &&
		    errno != EAGAIN) {
			close(klienty[i]);
			klienty[i] = klienty[--n_klientov];
			continue;
		}
		i++;
	}
	pthread_mutex_unlock(&zamok);
}

static void nmea_cb(GpsUtcTime t, const char *nmea, int dlina)
{
	(void)t;
	if (dlina <= 0 || !nmea)
		return;
	if (proba) {
		fwrite(nmea, 1, dlina, stdout);
		if (nmea[dlina - 1] != '\n')
			fputc('\n', stdout);
		fflush(stdout);
	}
	razdat(nmea, dlina);
}

static void location_cb(GpsLocation *l)
{
	if (proba)
		printf("# место: %.6f %.6f ±%.0f м\n", l->latitude, l->longitude, l->accuracy);
}

static void status_cb(GpsStatus *s)
{
	if (proba)
		printf("# состояние: %d\n", s->status);
}

static void sv_status_cb(GpsSvStatus *s)
{
	(void)s;
}

static void gnss_sv_status_cb(GnssSvStatus *s)
{
	int vidno = 0;

	for (int i = 0; i < s->num_svs; i++)
		if (s->gnss_sv_list[i].c_n0_dbhz > 0)
			vidno++;
	if (proba)
		printf("# спутников: %d, с сигналом: %d\n", s->num_svs, vidno);
}

static void set_capabilities_cb(uint32_t c)
{
	(void)c;
}

static void acquire_wakelock_cb(void) { }
static void release_wakelock_cb(void) { }
static void request_utc_time_cb(void) { }
static void set_system_info_cb(const GnssSystemInfo *i) { (void)i; }

struct obertka {
	void (*f)(void *);
	void *arg;
};

static void *pusk_niti(void *p)
{
	struct obertka o = *(struct obertka *)p;

	free(p);
	o.f(o.arg);
	return NULL;
}

static pthread_t create_thread_cb(const char *imya, void (*f)(void *), void *arg)
{
	pthread_t t = 0;
	struct obertka *o = malloc(sizeof(*o));

	(void)imya;
	o->f = f;
	o->arg = arg;
	if (pthread_create(&t, NULL, pusk_niti, o) != 0)
		free(o);
	return t;
}

static struct bf7_callbacks cb = {
	.base = {
		.size = sizeof(struct bf7_callbacks),
		.location_cb = location_cb,
		.status_cb = status_cb,
		.sv_status_cb = sv_status_cb,
		.nmea_cb = nmea_cb,
		.set_capabilities_cb = set_capabilities_cb,
		.acquire_wakelock_cb = acquire_wakelock_cb,
		.release_wakelock_cb = release_wakelock_cb,
		.create_thread_cb = create_thread_cb,
		.request_utc_time_cb = request_utc_time_cb,
		.set_system_info_cb = set_system_info_cb,
		.gnss_sv_status_cb = gnss_sv_status_cb,
	},
	.mtk_ext1 = mtk_zaglushka,
	.mtk_ext2 = mtk_zaglushka,
};

static const GpsInterface *otkryt_hal(void)
{
	const hw_module_t *m;
	struct gps_device_t *d;

	if (hw_get_module(GPS_HARDWARE_MODULE_ID, &m) != 0 ||
	    m->methods->open(m, GPS_HARDWARE_MODULE_ID, (struct hw_device_t **)&d) != 0) {
		fprintf(stderr, "bf7-gps-nmea: нет HAL gps\n");
		return NULL;
	}
	return d->get_gps_interface(d);
}

static int vklyuchen;

static void vklyuchit(int da)
{
	if (da == vklyuchen)
		return;
	if (da) {
		gps->set_position_mode(GPS_POSITION_MODE_STANDALONE,
				       GPS_POSITION_RECURRENCE_PERIODIC, 1000, 0, 0);
		gps->start();
	} else {
		gps->stop();
	}
	vklyuchen = da;
	fprintf(stderr, "bf7-gps-nmea: приёмник %s\n", da ? "включён" : "выключен");
}

static void na_vyhod(int s)
{
	(void)s;
	stop = 1;
}

int main(int argc, char **argv)
{
	int sekund = 0, ls;
	struct sockaddr_un adr = { .sun_family = AF_UNIX };

	if (argc > 2 && !strcmp(argv[1], "--proba")) {
		proba = 1;
		sekund = atoi(argv[2]);
	}
	signal(SIGTERM, na_vyhod);
	signal(SIGINT, na_vyhod);
	signal(SIGPIPE, SIG_IGN);

	gps = otkryt_hal();
	if (!gps || gps->init(&cb.base) != 0) {
		fprintf(stderr, "bf7-gps-nmea: init HAL не прошёл\n");
		return 1;
	}

	if (proba) {
		time_t konec = time(NULL) + sekund;

		vklyuchit(1);
		while (!stop && time(NULL) < konec)
			sleep(1);
		vklyuchit(0);
		gps->cleanup();
		return 0;
	}

	mkdir(KATALOG, 0755);
	unlink(SOKET);
	strncpy(adr.sun_path, SOKET, sizeof(adr.sun_path) - 1);
	ls = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (ls < 0 || bind(ls, (struct sockaddr *)&adr, sizeof(adr)) < 0 || listen(ls, 4) < 0) {
		perror("bf7-gps-nmea: " SOKET);
		return 1;
	}
	chmod(SOKET, 0666);

	/*
	 * Ждём подключений; раз в 5 с, пока приёмник включён, проверяем, не
	 * ушли ли все слушатели (отвалившихся убирает razdat при записи).
	 * В покое — один poll без таймаута, пробуждений нет.
	 */
	while (!stop) {
		struct pollfd p = { ls, POLLIN, 0 };
		int r = poll(&p, 1, vklyuchen ? 5000 : -1);

		if (r > 0 && (p.revents & POLLIN)) {
			int k = accept4(ls, NULL, NULL, SOCK_CLOEXEC);

			if (k >= 0) {
				pthread_mutex_lock(&zamok);
				if (n_klientov < MAKS_KLIENTOV)
					klienty[n_klientov++] = k;
				else
					close(k);
				pthread_mutex_unlock(&zamok);
			}
		}
		/* слушатель, закрывший сокет, виден как читаемый конец файла */
		pthread_mutex_lock(&zamok);
		for (int i = 0; i < n_klientov; ) {
			char b;
			struct pollfd q = { klienty[i], POLLIN, 0 };

			if (poll(&q, 1, 0) > 0 && recv(klienty[i], &b, 1, MSG_DONTWAIT) == 0) {
				close(klienty[i]);
				klienty[i] = klienty[--n_klientov];
				continue;
			}
			i++;
		}
		r = n_klientov;
		pthread_mutex_unlock(&zamok);
		vklyuchit(r > 0);
	}
	vklyuchit(0);
	gps->cleanup();
	unlink(SOKET);
	return 0;
}
