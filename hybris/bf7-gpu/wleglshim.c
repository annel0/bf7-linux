/*
 * BF7: объявление android_wlegl в чужом композиторе, без правки его исходников.
 *
 * Зачем. Наш EGL — это libhybris, и её модуль для платформы Wayland
 * обменивается буферами с композитором по андроидному протоколу
 * android_wlegl: клиент выделяет буфер через gralloc и передаёт композитору
 * его дескриптор. Не найдя такого глобального объекта, модуль прекращает
 * работу вызовом abort():
 *
 *     Fatal: the server doesn't advertise the android_wlegl global!
 *
 * Именно на этом падают под phoc все программы с GL — Cheese, guvcview,
 * браузеры. Наш собственный композитор объявляет протокол сам
 * (hybris/bf7-gpu/gui/wlegl.cpp), а phoc про libhybris ничего не знает.
 *
 * Как. Вся работа — разбор дескрипторов, обращение к gralloc, создание
 * wl_buffer — уже написана в libhybris и собрана в libhybris-platformcommon.
 * Нужно лишь позвать server_wlegl_create() на готовом дисплее. Перехватываем
 * для этого wl_display_add_socket_auto(): к моменту её вызова композитор уже
 * создал дисплей и поднял рендерер, а оболочку (-E) ещё не запустил. На
 * случай, если композитор заводит сокет иначе, тем же способом подстрахованы
 * wl_display_add_socket() и wl_display_run().
 *
 * server_wlegl_create собрана компилятором C++, поэтому берётся по
 * искажённому имени _Z19server_wlegl_createP10wl_display: заголовок
 * server_wlegl.h в систему не ставится.
 *
 * Сборка:
 *   gcc -shared -fPIC -O2 -o libbf7wlegl.so wleglshim.c -ldl
 * Применение:
 *   LD_PRELOAD="/usr/local/lib/libbf7eglfilter.so /usr/local/lib/libbf7wlegl.so"
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stddef.h>

/* Заголовки libwayland тянуть незачем — нужен только указатель. */
struct wl_display;

#define ИМЯ_СОЗДАНИЯ "_Z19server_wlegl_createP10wl_display"

static int уже;

static void объявить(struct wl_display *дисплей)
{
	if (уже || !дисплей)
		return;
	уже = 1;

	/*
	 * Буферы клиентов приходят дескрипторами gralloc, и разбирать их будет
	 * именно gralloc. Повторный вызов безвреден, поэтому не выясняем, успел
	 * ли его поднять кто-то ещё.
	 */
	void *гралок = dlopen("libgralloc.so", RTLD_NOW | RTLD_GLOBAL);
	if (гралок) {
		void (*поднять)(int) = dlsym(гралок, "hybris_gralloc_initialize");
		if (поднять)
			поднять(0);
		else
			fprintf(stderr, "bf7-wlegl: в libgralloc нет hybris_gralloc_initialize\n");
	} else {
		fprintf(stderr, "bf7-wlegl: не открылась libgralloc: %s\n", dlerror());
	}

	void *общая = dlopen("libhybris-platformcommon.so.1", RTLD_NOW | RTLD_GLOBAL);
	if (!общая) {
		fprintf(stderr, "bf7-wlegl: не открылась libhybris-platformcommon: %s\n",
			dlerror());
		return;
	}

	void *(*создать)(struct wl_display *) = dlsym(общая, ИМЯ_СОЗДАНИЯ);
	if (!создать) {
		fprintf(stderr, "bf7-wlegl: нет символа %s\n", ИМЯ_СОЗДАНИЯ);
		return;
	}

	if (создать(дисплей))
		fprintf(stderr, "bf7-wlegl: android_wlegl объявлен\n");
	else
		fprintf(stderr, "bf7-wlegl: server_wlegl_create вернула пусто\n");
}

const char *wl_display_add_socket_auto(struct wl_display *дисплей)
{
	static const char *(*настоящая)(struct wl_display *);
	if (!настоящая)
		настоящая = dlsym(RTLD_NEXT, "wl_display_add_socket_auto");
	const char *имя = настоящая ? настоящая(дисплей) : NULL;
	объявить(дисплей);
	return имя;
}

int wl_display_add_socket(struct wl_display *дисплей, const char *имя)
{
	static int (*настоящая)(struct wl_display *, const char *);
	if (!настоящая)
		настоящая = dlsym(RTLD_NEXT, "wl_display_add_socket");
	int r = настоящая ? настоящая(дисплей, имя) : -1;
	объявить(дисплей);
	return r;
}

void wl_display_run(struct wl_display *дисплей)
{
	static void (*настоящая)(struct wl_display *);
	if (!настоящая)
		настоящая = dlsym(RTLD_NEXT, "wl_display_run");
	объявить(дисплей);
	if (настоящая)
		настоящая(дисплей);
}
