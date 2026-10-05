/*
 * BF7: заливка экрана сплошным цветом — окно во весь экран, без GPU.
 *
 * Зачем. Во-первых, для настройки камеры, которая смотрит на панель: чтобы
 * найти на снимке границы экрана, нужно показать заведомо белое поле.
 * Во-вторых, это самая простая проверка пути показа: если сплошной цвет
 * дошёл до матрицы без пятен и полос, значит слой показа исправен.
 *
 * Почему не GL. Здесь нарочно только wl_shm: буфер в разделяемой памяти,
 * никакого EGL, никакого hybris. Если что-то не так с ускорением, эта
 * программа всё равно покажет цвет — и разделит «сломан показ» и «сломано
 * ускорение». Ради этого стоит лишних полсотни строк.
 *
 * Вызов: bf7-zalivka RRGGBB [секунд]
 *        bf7-zalivka ffffff 6   — белое поле на шесть секунд
 *
 * Сборка (на телефоне):
 *   wayland-scanner client-header \
 *       /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
 *       xdg-shell-client-protocol.h
 *   wayland-scanner private-code \
 *       /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
 *       xdg-shell-protocol.c
 *   cc -O2 -o bf7-zalivka bf7-zalivka.c xdg-shell-protocol.c \
 *       $(pkg-config --cflags --libs wayland-client) -lrt
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

static struct wl_compositor *kompozitor;
static struct wl_shm *pamyat;
static struct xdg_wm_base *osnova;

static int shirina = 360;
static int vysota = 806;
static int razmer_prishel = 0;
static int nastroeno = 0;

static uint32_t tsvet = 0xffffffffu;

static void osnova_ping(void *dannye, struct xdg_wm_base *o, uint32_t metka)
{
	(void)dannye;
	xdg_wm_base_pong(o, metka);
}

static const struct xdg_wm_base_listener osnova_sluh = { osnova_ping };

static void reestr_dobavil(void *dannye, struct wl_registry *reestr,
			   uint32_t imya, const char *interfejs, uint32_t versiya)
{
	(void)dannye;
	(void)versiya;

	if (!strcmp(interfejs, wl_compositor_interface.name))
		kompozitor = wl_registry_bind(reestr, imya,
					      &wl_compositor_interface, 4);
	else if (!strcmp(interfejs, wl_shm_interface.name))
		pamyat = wl_registry_bind(reestr, imya, &wl_shm_interface, 1);
	else if (!strcmp(interfejs, xdg_wm_base_interface.name)) {
		osnova = wl_registry_bind(reestr, imya,
					  &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(osnova, &osnova_sluh, NULL);
	}
}

static void reestr_ubral(void *dannye, struct wl_registry *reestr, uint32_t imya)
{
	(void)dannye; (void)reestr; (void)imya;
}

static const struct wl_registry_listener reestr_sluh = {
	reestr_dobavil, reestr_ubral
};

/*
 * Размер приходит в настройке поверхности. Нулевая ширина означает «решай
 * сам» — тогда остаёмся при своих числах; композитор во весь экран обычно
 * всё-таки называет размер.
 */
static void poverhnost_nastroj(void *dannye, struct xdg_toplevel *verh,
			       int32_t sh, int32_t v, struct wl_array *sost)
{
	(void)dannye; (void)verh; (void)sost;

	if (sh > 0 && v > 0) {
		shirina = sh;
		vysota = v;
		razmer_prishel = 1;
	}
}

static void poverhnost_zakroj(void *dannye, struct xdg_toplevel *verh)
{
	(void)dannye; (void)verh;
	exit(0);
}

static const struct xdg_toplevel_listener verh_sluh = {
	poverhnost_nastroj, poverhnost_zakroj
};

static void okno_nastroeno(void *dannye, struct xdg_surface *okno,
			   uint32_t serial)
{
	(void)dannye;
	xdg_surface_ack_configure(okno, serial);
	nastroeno = 1;
}

static const struct xdg_surface_listener okno_sluh = { okno_nastroeno };

/* Безымянный файл в разделяемой памяти под буфер кадра. */
static int vremennyj_fajl(size_t bajt)
{
	char obrazec[] = "/bf7-zalivka-XXXXXX";
	int fd;

	/*
	 * memfd_create был бы чище, но в 4.19 с нашей libc он местами
	 * капризничает, а shm_open есть везде и ведёт себя одинаково.
	 */
	for (int popytka = 0; popytka < 100; popytka++) {
		snprintf(obrazec, sizeof(obrazec), "/bf7-zalivka-%d",
			 (int)(random() % 100000));
		fd = shm_open(obrazec, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(obrazec);
			if (ftruncate(fd, (off_t)bajt) < 0) {
				close(fd);
				return -1;
			}
			return fd;
		}
	}
	return -1;
}

int main(int argc, char **argv)
{
	struct wl_display *displej;
	struct wl_registry *reestr;
	struct wl_surface *poverhnost;
	struct xdg_surface *okno;
	struct xdg_toplevel *verh;
	struct wl_shm_pool *zapas;
	struct wl_buffer *bufer;
	uint32_t *piksely;
	size_t bajt;
	int fd;
	double sekund = 5.0;

	srandom((unsigned)time(NULL) ^ (unsigned)getpid());

	if (argc > 1) {
		unsigned long v = strtoul(argv[1], NULL, 16);
		tsvet = 0xff000000u | (uint32_t)(v & 0xffffffu);
	}
	if (argc > 2)
		sekund = atof(argv[2]);

	displej = wl_display_connect(NULL);
	if (!displej) {
		fprintf(stderr, "bf7-zalivka: нет связи с композитором\n");
		return 1;
	}

	reestr = wl_display_get_registry(displej);
	wl_registry_add_listener(reestr, &reestr_sluh, NULL);
	wl_display_roundtrip(displej);

	if (!kompozitor || !pamyat || !osnova) {
		fprintf(stderr, "bf7-zalivka: композитор не дал нужных частей\n");
		return 1;
	}

	poverhnost = wl_compositor_create_surface(kompozitor);
	okno = xdg_wm_base_get_xdg_surface(osnova, poverhnost);
	xdg_surface_add_listener(okno, &okno_sluh, NULL);
	verh = xdg_surface_get_toplevel(okno);
	xdg_toplevel_add_listener(verh, &verh_sluh, NULL);
	xdg_toplevel_set_title(verh, "bf7-zalivka");
	xdg_toplevel_set_app_id(verh, "bf7-zalivka");
	xdg_toplevel_set_fullscreen(verh, NULL);
	wl_surface_commit(poverhnost);

	/*
	 * Ждём настройку: без неё неизвестен размер во весь экран, и первый
	 * кадр придётся показывать наугад.
	 */
	while (!nastroeno && wl_display_dispatch(displej) != -1)
		;

	if (!razmer_prishel)
		fprintf(stderr, "bf7-zalivka: размер не назван, беру %dx%d\n",
			shirina, vysota);

	bajt = (size_t)shirina * (size_t)vysota * 4u;
	fd = vremennyj_fajl(bajt);
	if (fd < 0) {
		fprintf(stderr, "bf7-zalivka: нет памяти под кадр\n");
		return 1;
	}

	piksely = mmap(NULL, bajt, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (piksely == MAP_FAILED) {
		fprintf(stderr, "bf7-zalivka: кадр не лёг в память\n");
		return 1;
	}

	for (size_t i = 0; i < bajt / 4u; i++)
		piksely[i] = tsvet;

	zapas = wl_shm_create_pool(pamyat, fd, (int32_t)bajt);
	bufer = wl_shm_pool_create_buffer(zapas, 0, shirina, vysota,
					  shirina * 4, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(zapas);
	close(fd);

	wl_surface_attach(poverhnost, bufer, 0, 0);
	wl_surface_damage_buffer(poverhnost, 0, 0, shirina, vysota);
	wl_surface_commit(poverhnost);
	wl_display_flush(displej);

	printf("bf7-zalivka: %dx%d цвет %06x, держу %.1f с\n",
	       shirina, vysota, tsvet & 0xffffffu, sekund);
	fflush(stdout);

	{
		struct timespec skolko;

		skolko.tv_sec = (time_t)sekund;
		skolko.tv_nsec = (long)((sekund - (double)(time_t)sekund) * 1e9);
		/*
		 * Спим с обработкой очереди: композитор шлёт ping, и без
		 * ответа на него окно через несколько секунд объявят мёртвым.
		 */
		{
			struct timespec do_kontsa, teper;

			clock_gettime(CLOCK_MONOTONIC, &do_kontsa);
			do_kontsa.tv_sec += skolko.tv_sec;
			do_kontsa.tv_nsec += skolko.tv_nsec;
			if (do_kontsa.tv_nsec >= 1000000000L) {
				do_kontsa.tv_sec++;
				do_kontsa.tv_nsec -= 1000000000L;
			}
			for (;;) {
				clock_gettime(CLOCK_MONOTONIC, &teper);
				if (teper.tv_sec > do_kontsa.tv_sec ||
				    (teper.tv_sec == do_kontsa.tv_sec &&
				     teper.tv_nsec >= do_kontsa.tv_nsec))
					break;
				if (wl_display_dispatch_pending(displej) < 0)
					break;
				wl_display_flush(displej);
				usleep(50000);
			}
		}
	}

	wl_buffer_destroy(bufer);
	wl_surface_destroy(poverhnost);
	wl_display_disconnect(displej);
	return 0;
}
