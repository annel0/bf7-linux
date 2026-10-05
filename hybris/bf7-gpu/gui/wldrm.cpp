/*
 * BF7: серверная сторона wl_drm для нашего композитора.
 *
 * Зачем. Xwayland включает glamor только если композитор объявляет глобальный
 * объект wl_drm: проверка в xwl_glamor_gbm_has_wl_interfaces() смотрит ровно
 * на него и больше ни на что. Одного zwp_linux_dmabuf_v1 недостаточно, хотя
 * буферы потом ходят именно через него.
 *
 * Что здесь есть. Объявление узла рендера, список форматов и признак PRIME.
 * Аутентификация не нужна: Xwayland смотрит тип узла, а /dev/dri/renderD128 —
 * настоящий узел рендера, и тогда он пропускает весь обмен с drmAuthMagic.
 * Обработчик всё же отвечает событием, чтобы не молчать, если кто-то спросит.
 *
 * Чего здесь нет. Создания буферов по старым запросам. Буферы приходят через
 * zwp_linux_dmabuf_v1 — композитор объявляет ARGB8888 и XRGB8888 с линейным
 * модификатором, а наши буферы именно линейные, так что Xwayland выбирает этот
 * путь и до create_prime_buffer не доходит. Если всё же дойдёт, мы не роняем
 * клиента протокольной ошибкой, а громко пишем в журнал и отдаём пустой буфер:
 * пропавшее окно диагностировать куда приятнее, чем умерший сеанс.
 */
#include <unistd.h>

#include <QByteArray>
#include <QDebug>
#include <QWaylandCompositor>

extern "C" {
#include <wayland-server-core.h>
#include "wayland-drm-server-protocol.h"
}

namespace {

const char *renderNodePath()
{
	/*
	 * Путь можно переопределить: узел рендера у другого ядра может быть
	 * иным. Значение держим в статической строке — отдавать указатель на
	 * временный QByteArray значило бы вернуть висячий.
	 */
	static const QByteArray path = qEnvironmentVariableIsSet("BF7_WL_DRM_DEVICE")
		? qgetenv("BF7_WL_DRM_DEVICE")
		: QByteArray("/dev/dri/renderD128");

	return path.constData();
}

void drmAuthenticate(wl_client *, wl_resource *resource, uint32_t)
{
	/* Узел рендера аутентификации не требует, но промолчать было бы грубо. */
	wl_drm_send_authenticated(resource);
}

wl_resource *makeInertBuffer(wl_client *client, wl_resource *resource, uint32_t id)
{
	wl_resource *buffer = wl_resource_create(client, &wl_buffer_interface, 1, id);

	if (buffer)
		wl_resource_set_implementation(buffer, nullptr, nullptr, nullptr);
	else
		wl_resource_post_no_memory(resource);

	return buffer;
}

void drmCreateBuffer(wl_client *client, wl_resource *resource, uint32_t id,
		     uint32_t, int32_t, int32_t, uint32_t, uint32_t)
{
	qWarning("wl_drm: create_buffer (обмен через flink) не поддержан");
	makeInertBuffer(client, resource, id);
}

void drmCreatePlanarBuffer(wl_client *client, wl_resource *resource, uint32_t id,
			   uint32_t, int32_t, int32_t, uint32_t,
			   int32_t, int32_t, int32_t, int32_t, int32_t, int32_t)
{
	qWarning("wl_drm: create_planar_buffer не поддержан");
	makeInertBuffer(client, resource, id);
}

void drmCreatePrimeBuffer(wl_client *client, wl_resource *resource, uint32_t id,
			  int32_t fd, int32_t width, int32_t height, uint32_t format,
			  int32_t, int32_t stride0, int32_t, int32_t, int32_t, int32_t)
{
	/*
	 * Сюда попадать не должно: буферы идут через zwp_linux_dmabuf_v1. Если
	 * всё же попали — значит клиент не нашёл нужный формат с линейным
	 * модификатором, и смотреть надо туда.
	 */
	qWarning("wl_drm: запрошен create_prime_buffer %dx%d формат 0x%x шаг %d — "
		 "клиент не выбрал путь dmabuf, окно будет пустым",
		 width, height, format, stride0);
	close(fd);
	makeInertBuffer(client, resource, id);
}

const struct wl_drm_interface drmImplementation = {
	drmAuthenticate,
	drmCreateBuffer,
	drmCreatePlanarBuffer,
	drmCreatePrimeBuffer,
};

void drmBind(wl_client *client, void *, uint32_t version, uint32_t id)
{
	wl_resource *resource = wl_resource_create(client, &wl_drm_interface,
						   int(qMin(version, 2u)), id);

	if (!resource)
		return;

	wl_resource_set_implementation(resource, &drmImplementation, nullptr, nullptr);

	wl_drm_send_device(resource, renderNodePath());
	wl_drm_send_format(resource, WL_DRM_FORMAT_ARGB8888);
	wl_drm_send_format(resource, WL_DRM_FORMAT_XRGB8888);
	wl_drm_send_format(resource, WL_DRM_FORMAT_ABGR8888);
	wl_drm_send_format(resource, WL_DRM_FORMAT_XBGR8888);
	wl_drm_send_format(resource, WL_DRM_FORMAT_RGB565);

	if (version >= 2)
		wl_drm_send_capabilities(resource, WL_DRM_CAPABILITY_PRIME);
}

} // namespace

void bf7InstallWlDrm(QWaylandCompositor *compositor)
{
	if (!compositor || !compositor->display()) {
		qWarning("wl_drm: композитор ещё не готов, глобальный объект не создан");
		return;
	}

	if (!wl_global_create(compositor->display(), &wl_drm_interface, 2,
			      nullptr, drmBind)) {
		qWarning("wl_drm: не удалось создать глобальный объект");
		return;
	}

	qInfo("wl_drm: объявлен, узел %s", renderNodePath());
}
