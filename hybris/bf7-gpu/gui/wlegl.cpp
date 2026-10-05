/*
 * BF7: объявление android_wlegl в нашем композиторе.
 *
 * Зачем. Браузеры просят у EGL платформу Wayland. Наш вендор — libhybris, и
 * его модуль для этой платформы обменивается буферами с композитором по
 * андроидному протоколу android_wlegl: клиент выделяет буфер через gralloc и
 * передаёт композитору его дескриптор. Если композитор такого глобального
 * объекта не объявляет, модуль прекращает работу вызовом abort() с
 * сообщением «the server doesn't advertise the android_wlegl global!», и
 * браузер остаётся либо без ускорения, либо вовсе без графики.
 *
 * Без этого Firefox молча съезжает на программный EGL от Mesa, а за ним
 * отключается dmabuf и вместе с ним аппаратное декодирование видео: до
 * нашего драйвера VA-API дело просто не доходит. Chromium же доходит до
 * нашего модуля и падает на упомянутом abort().
 *
 * Что здесь есть. Ровно одно: регистрация глобального объекта. Вся работа —
 * разбор дескрипторов, обращение к gralloc, создание wl_buffer — уже написана
 * в самом libhybris (hybris/platforms/common/server_wlegl*) и собрана в
 * libhybris-platformcommon. Мы её только включаем.
 *
 * Почему объявления здесь, а не через заголовок. Заголовок server_wlegl.h в
 * систему не ставится (установлены лишь nativewindowbase.h и support.h), а
 * тянуть в композитор весь набор андроидных заголовков ради двух строк
 * незачем. Функция собрана компилятором C++, поэтому и объявлена как C++:
 * искажённое имя обязано совпасть с _Z19server_wlegl_createP10wl_display.
 */
#include <QDebug>
#include <QWaylandCompositor>

extern "C" {
#include <wayland-server-core.h>

/* Из libgralloc: заголовок hybris/gralloc/gralloc.h тоже не установлен. */
void hybris_gralloc_initialize(int framebuffer);
}

struct server_wlegl;
server_wlegl *server_wlegl_create(struct wl_display *display);

void bf7InstallWlEgl(QWaylandCompositor *compositor)
{
	if (!compositor || !compositor->display()) {
		qWarning() << "bf7: android_wlegl не объявлен — нет дисплея";
		return;
	}

	/*
	 * Буферы клиентов приходят дескрипторами gralloc, и разбирать их
	 * будет именно gralloc. К этому времени он, скорее всего, уже поднят
	 * модулем mtkfb, через который рисует сам композитор, но полагаться на
	 * чужой порядок запуска не стоит: повторный вызов безвреден.
	 */
	hybris_gralloc_initialize(0);

	if (!server_wlegl_create(compositor->display())) {
		qWarning() << "bf7: не удалось объявить android_wlegl";
		return;
	}
	qInfo() << "bf7: android_wlegl объявлен";
}
