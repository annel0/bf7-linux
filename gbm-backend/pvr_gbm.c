// SPDX-License-Identifier: MIT
/*
 * BF7: бэкенд GBM поверх gralloc.
 *
 * Зачем он нужен. Xwayland умеет ускорять рисование только через glamor, а
 * glamor берёт буферы у GBM. Драйвера Mesa для нашего GPU нет и не будет, зато
 * у Mesa есть штатный способ подключить чужой бэкенд GBM — механизм, добавленный
 * ради NVIDIA: чужая проприетарная EGL поверх Mesa'вского фронтенда libgbm. У
 * нас ровно та же расстановка, только EGL приходит из андроидных блобов через
 * libhybris.
 *
 * Что он делает. Выделяет буферы через gralloc (тот же аллокатор, которым
 * пользуется сам GPU) и отдаёт наружу их дескрипторы dma-buf. Дальше буфер
 * живёт обычной жизнью: EGL втягивает его через EGL_EXT_image_dma_buf_import,
 * а показывает его наш узел KMS. Никакой связи с libhybris у самого бэкенда
 * нет — общаются через дескриптор, и это хорошо.
 *
 * Как его находят. Загрузчик Mesa ищет файл по имени драйвера из
 * drmGetVersion(): у нас это "pvr", значит файл зовётся pvr_gbm.so и лежит в
 * /usr/lib/aarch64-linux-gnu/gbm/. Переменные окружения не нужны.
 *
 * Сборка (на устройстве):
 *   gcc -O2 -fPIC -fvisibility=hidden -shared -o pvr_gbm.so pvr_gbm.c \
 *       -Imesa-abi -I/usr/include/libdrm -ldrm -lgralloc
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xf86drm.h>
#include <drm_fourcc.h>

#include "gbm_backend_abi.h"

/* --- то, что приходит из libhybris; заголовков у него не установлено --- */
typedef const struct native_handle *buffer_handle_t;

struct native_handle {
	int version;
	int numFds;
	int numInts;
	int data[0];
};

extern void hybris_gralloc_initialize(int framebuffer);
extern int hybris_gralloc_allocate(int width, int height, int format, int usage,
				   buffer_handle_t *handle, uint32_t *stride);
extern int hybris_gralloc_release(buffer_handle_t handle, int was_allocated);
extern int hybris_gralloc_lock(buffer_handle_t handle, int usage, int l, int t,
			       int w, int h, void **vaddr);
extern int hybris_gralloc_unlock(buffer_handle_t handle);

/* Значения Android. */
#define HAL_PIXEL_FORMAT_RGBA_8888	1
#define HAL_PIXEL_FORMAT_RGBX_8888	2
#define HAL_PIXEL_FORMAT_RGB_565	4
#define HAL_PIXEL_FORMAT_BGRA_8888	5

#define GRALLOC_USAGE_SW_READ_OFTEN	0x00000003
#define GRALLOC_USAGE_SW_WRITE_OFTEN	0x00000030
#define GRALLOC_USAGE_HW_TEXTURE	0x00000100
#define GRALLOC_USAGE_HW_RENDER		0x00000200
#define GRALLOC_USAGE_HW_COMPOSER	0x00000800
#define GRALLOC_USAGE_HW_FB		0x00001000

static const struct gbm_core *core;

static int trace_on;

static void trace(const char *fmt, ...)
{
	va_list ap;

	if (!trace_on)
		return;

	va_start(ap, fmt);
	fprintf(stderr, "pvr_gbm: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

struct pvr_device {
	struct gbm_device base;		/* обязан быть первым */
	int fd;
};

struct pvr_bo {
	struct gbm_bo base;		/* обязан быть первым */
	buffer_handle_t handle;		/* NULL у импортированных чужих буферов */
	int dma_fd;
	uint32_t size;
	uint64_t modifier;
	void *map_addr;
};

struct pvr_surface {
	struct gbm_surface base;
};

static inline struct pvr_device *to_dev(struct gbm_device *gbm)
{
	return (struct pvr_device *)gbm;
}

static inline struct pvr_bo *to_bo(struct gbm_bo *bo)
{
	return (struct pvr_bo *)bo;
}

/*
 * Порядок каналов.
 *
 * Имена DRM перечисляют каналы от старшего бита к младшему, имена Android — в
 * порядке байтов в памяти, поэтому выглядят зеркально: DRM_FORMAT_ARGB8888 —
 * это байты B,G,R,A, то есть HAL_PIXEL_FORMAT_BGRA_8888. Из этого же следует,
 * что тот же самый буфер можно без перестановок отдать и EGL, и слою дисплея.
 */
/*
 * Формат, который мы отдаём наружу.
 *
 * У Android нет бесальфовых 32-битных форматов: XRGB8888 всё равно выделяется
 * как BGRA_8888, а XBGR8888 — как RGBA_8888. Сообщать при этом бесальфовый
 * формат значило бы врать о том, что лежит в памяти.
 *
 * И это не придирка. PowerVR **отказывается принимать пиксели** в образ EGL,
 * сделанный из бесальфового буфера: glTexSubImage2D отвечает
 * INVALID_OPERATION и для BGRA, и для RGBA (проверено пробой fourcctest3, у
 * альфовых вариантов всё принимается). Для glamor это означает, что стирание
 * и любое рисование картинкой просто не доезжает — окна оставляют шлейф.
 *
 * Раскладка байтов у X- и A-вариантов одна и та же, так что показу это ничем
 * не грозит: тот, кому альфа не нужна, объявляет её игнорируемой сам.
 */
static uint32_t reported_format(uint32_t fourcc)
{
	switch (fourcc) {
	case DRM_FORMAT_XRGB8888:
		return DRM_FORMAT_ARGB8888;
	case DRM_FORMAT_XBGR8888:
		return DRM_FORMAT_ABGR8888;
	default:
		return fourcc;
	}
}

static int hal_format(uint32_t fourcc, unsigned *bpp)
{
	switch (fourcc) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		*bpp = 4;
		return HAL_PIXEL_FORMAT_BGRA_8888;
	case DRM_FORMAT_ABGR8888:
		*bpp = 4;
		return HAL_PIXEL_FORMAT_RGBA_8888;
	case DRM_FORMAT_XBGR8888:
		*bpp = 4;
		return HAL_PIXEL_FORMAT_RGBX_8888;
	case DRM_FORMAT_RGB565:
		*bpp = 2;
		return HAL_PIXEL_FORMAT_RGB_565;
	default:
		return -1;
	}
}

static int gralloc_usage(uint32_t usage)
{
	int out = GRALLOC_USAGE_HW_TEXTURE;

	if (usage & GBM_BO_USE_RENDERING)
		out |= GRALLOC_USAGE_HW_RENDER;
	if (usage & GBM_BO_USE_SCANOUT)
		out |= GRALLOC_USAGE_HW_COMPOSER;
	if (usage & (GBM_BO_USE_WRITE | GBM_BO_USE_LINEAR))
		out |= GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN;

	return out;
}

/*
 * Поиск dma-buf в дескрипторе gralloc.
 *
 * Постоянного номера у нужного дескриптора нет: у PowerVR их три, и только один
 * — сам буфер. Проверено на устройстве: у первого размер вообще не берётся, у
 * третьего ровно одна страница метаданных. Годится только проба по размеру,
 * ровно как в нашем же mtkfb_window.cpp.
 */
static int find_dma_fd(buffer_handle_t h, size_t need)
{
	int i;

	if (!h)
		return -1;

	for (i = 0; i < h->numFds; i++) {
		off_t len = lseek(h->data[i], 0, SEEK_END);

		if (len > 0 && (size_t)len >= need)
			return h->data[i];
	}

	return -1;
}

/* --- буферы --------------------------------------------------------- */

static void pvr_bo_destroy(struct gbm_bo *bo)
{
	struct pvr_bo *b = to_bo(bo);

	if (b->map_addr && b->handle)
		hybris_gralloc_unlock(b->handle);

	if (b->handle)
		hybris_gralloc_release(b->handle, 1);
	else if (b->dma_fd >= 0)
		close(b->dma_fd);	/* импортированный: дескриптор наш */

	free(b);
}

static struct gbm_bo *bo_alloc_common(struct gbm_device *gbm, uint32_t width,
				      uint32_t height, uint32_t fourcc,
				      uint32_t stride_bytes)
{
	struct pvr_bo *b = calloc(1, sizeof(*b));

	if (!b)
		return NULL;

	b->base.gbm = gbm;
	b->base.v0.width = width;
	b->base.v0.height = height;
	b->base.v0.format = fourcc;
	b->base.v0.stride = stride_bytes;
	b->dma_fd = -1;
	b->modifier = DRM_FORMAT_MOD_LINEAR;
	return &b->base;
}

static struct gbm_bo *pvr_bo_create(struct gbm_device *gbm,
				    uint32_t width, uint32_t height,
				    uint32_t format, uint32_t usage,
				    const uint64_t *modifiers,
				    const unsigned int count)
{
	struct pvr_device *dev = to_dev(gbm);
	uint32_t fourcc = core->v0.format_canonicalize(format);
	buffer_handle_t handle = NULL;
	uint32_t stride_px = 0;
	struct gbm_bo *bo;
	struct pvr_bo *b;
	unsigned bpp = 4;
	int hal, fd;
	unsigned i;

	hal = hal_format(fourcc, &bpp);
	if (hal < 0) {
		trace("формат %.4s не поддержан", (const char *)&fourcc);
		errno = EINVAL;
		return NULL;
	}

	/*
	 * Модификаторов у нас ровно один — линейный. Проверено показом на
	 * панели: gralloc отдаёт обычный линейный буфер. Если просят что-то
	 * ещё, честно отказываем: молча подсунуть линейный нельзя, приняв
	 * его за тайловый, вызывающий покажет мусор.
	 */
	if (modifiers && count) {
		int ok = 0;

		for (i = 0; i < count; i++)
			if (modifiers[i] == DRM_FORMAT_MOD_LINEAR ||
			    modifiers[i] == DRM_FORMAT_MOD_INVALID)
				ok = 1;
		if (!ok) {
			trace("ни один из %u модификаторов не линейный", count);
			errno = EINVAL;
			return NULL;
		}
	}

	if (hybris_gralloc_allocate(width, height, hal, gralloc_usage(usage),
				    &handle, &stride_px) != 0 || !handle) {
		trace("gralloc отказал: %ux%u формат %d", width, height, hal);
		errno = ENOMEM;
		return NULL;
	}

	fd = find_dma_fd(handle, (size_t)stride_px * height * bpp);
	if (fd < 0) {
		trace("dma-buf не найден среди %d дескрипторов", handle->numFds);
		hybris_gralloc_release(handle, 1);
		errno = EINVAL;
		return NULL;
	}

	bo = bo_alloc_common(gbm, width, height, reported_format(fourcc),
			     stride_px * bpp);
	if (!bo) {
		hybris_gralloc_release(handle, 1);
		errno = ENOMEM;
		return NULL;
	}

	b = to_bo(bo);
	b->handle = handle;
	b->dma_fd = fd;
	b->size = stride_px * height * bpp;

	/*
	 * Номер GEM нужен не всем, и отказ здесь не смертелен: путь с
	 * дескрипторами плоскостей его не спрашивает.
	 */
	if (drmPrimeFDToHandle(dev->fd, fd, &b->base.v0.handle.u32))
		b->base.v0.handle.u32 = 0;

	trace("создан %ux%u просили %.4s, отдаём %.4s, шаг %u байт, dma-buf %d",
	      width, height, (const char *)&fourcc,
	      (const char *)&b->base.v0.format, b->base.v0.stride, fd);
	return bo;
}

static struct gbm_bo *pvr_bo_import(struct gbm_device *gbm, uint32_t type,
				    void *buffer, uint32_t usage)
{
	struct pvr_device *dev = to_dev(gbm);
	struct gbm_bo *bo;
	struct pvr_bo *b;
	uint32_t fourcc, stride;
	uint32_t width, height;
	uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
	int fd;

	/*
	 * Импорт делаем только по дескриптору. Чужой буфер мы в gralloc не
	 * заворачиваем: всё, что нужно и EGL, и слою дисплея, — это сам
	 * дескриптор, а выдумывать под него handle gralloc значило бы врать
	 * о происхождении памяти.
	 */
	switch (type) {
	case GBM_BO_IMPORT_FD: {
		struct gbm_import_fd_data *d = buffer;

		fd = d->fd;
		width = d->width;
		height = d->height;
		stride = d->stride;
		fourcc = core->v0.format_canonicalize(d->format);
		break;
	}
	case GBM_BO_IMPORT_FD_MODIFIER: {
		struct gbm_import_fd_modifier_data *d = buffer;

		if (d->num_fds != 1) {
			trace("импорт %u плоскостей не поддержан", d->num_fds);
			errno = ENOSYS;
			return NULL;
		}
		fd = d->fds[0];
		width = d->width;
		height = d->height;
		stride = d->strides[0];
		fourcc = core->v0.format_canonicalize(d->format);
		modifier = d->modifier;
		break;
	}
	default:
		trace("импорт вида 0x%x не поддержан", type);
		errno = ENOSYS;
		return NULL;
	}

	if (modifier != DRM_FORMAT_MOD_LINEAR &&
	    modifier != DRM_FORMAT_MOD_INVALID) {
		trace("импорт с модификатором 0x%llx не поддержан",
		      (unsigned long long)modifier);
		errno = EINVAL;
		return NULL;
	}

	bo = bo_alloc_common(gbm, width, height, fourcc, stride);
	if (!bo) {
		errno = ENOMEM;
		return NULL;
	}

	b = to_bo(bo);
	b->dma_fd = dup(fd);		/* свой экземпляр: чужой закроют */
	if (b->dma_fd < 0) {
		free(b);
		errno = errno ? errno : EINVAL;
		return NULL;
	}
	b->size = stride * height;
	b->modifier = modifier;

	if (drmPrimeFDToHandle(dev->fd, b->dma_fd, &b->base.v0.handle.u32))
		b->base.v0.handle.u32 = 0;

	trace("импортирован %ux%u %.4s шаг %u", width, height,
	      (const char *)&fourcc, stride);
	return bo;
}

static int pvr_bo_get_fd(struct gbm_bo *bo)
{
	struct pvr_bo *b = to_bo(bo);

	/* Отдаём копию: закрывать возвращённый дескриптор будет вызывающий. */
	return b->dma_fd < 0 ? -1 : dup(b->dma_fd);
}

static int pvr_bo_get_plane_fd(struct gbm_bo *bo, int plane)
{
	if (plane != 0) {
		errno = EINVAL;
		return -1;
	}

	return pvr_bo_get_fd(bo);
}

static int pvr_bo_get_planes(struct gbm_bo *bo)
{
	(void)bo;
	return 1;
}

static union gbm_bo_handle pvr_bo_get_handle(struct gbm_bo *bo, int plane)
{
	union gbm_bo_handle none = { .s32 = -1 };

	if (plane != 0)
		return none;

	return bo->v0.handle;
}

static uint32_t pvr_bo_get_stride(struct gbm_bo *bo, int plane)
{
	if (plane != 0) {
		errno = EINVAL;
		return 0;
	}

	return bo->v0.stride;
}

static uint32_t pvr_bo_get_offset(struct gbm_bo *bo, int plane)
{
	(void)bo;
	(void)plane;
	return 0;
}

static uint64_t pvr_bo_get_modifier(struct gbm_bo *bo)
{
	return to_bo(bo)->modifier;
}

static void *pvr_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y,
			uint32_t width, uint32_t height, uint32_t flags,
			uint32_t *stride, void **map_data)
{
	struct pvr_bo *b = to_bo(bo);
	int usage = 0;
	void *addr = NULL;

	if (!b->handle) {
		/* Чужой буфер отображать нечем: ручки gralloc у него нет. */
		errno = ENOSYS;
		return NULL;
	}

	if (flags & GBM_BO_TRANSFER_READ)
		usage |= GRALLOC_USAGE_SW_READ_OFTEN;
	if (flags & GBM_BO_TRANSFER_WRITE)
		usage |= GRALLOC_USAGE_SW_WRITE_OFTEN;

	if (hybris_gralloc_lock(b->handle, usage, x, y, width, height, &addr) != 0
	    || !addr) {
		errno = ENOSYS;
		return NULL;
	}

	b->map_addr = addr;
	*stride = bo->v0.stride;
	*map_data = b;
	return addr;
}

static void pvr_bo_unmap(struct gbm_bo *bo, void *map_data)
{
	struct pvr_bo *b = to_bo(bo);

	(void)map_data;
	if (b->handle && b->map_addr) {
		hybris_gralloc_unlock(b->handle);
		b->map_addr = NULL;
	}
}

static int pvr_bo_write(struct gbm_bo *bo, const void *buf, size_t count)
{
	struct pvr_bo *b = to_bo(bo);
	void *addr = NULL;

	if (!b->handle) {
		errno = ENOSYS;
		return -1;
	}

	if (hybris_gralloc_lock(b->handle,
				GRALLOC_USAGE_SW_READ_OFTEN |
				GRALLOC_USAGE_SW_WRITE_OFTEN,
				0, 0, bo->v0.width, bo->v0.height, &addr) != 0
	    || !addr) {
		errno = ENOSYS;
		return -1;
	}

	if (count > b->size)
		count = b->size;
	memcpy(addr, buf, count);
	hybris_gralloc_unlock(b->handle);
	return 0;
}

/* --- поверхности ----------------------------------------------------- */

/*
 * Поверхности GBM нужны тем, кто рисует через eglCreateWindowSurface поверх
 * gbm_surface — kmscube и подобные. Xwayland так не делает: он работает
 * отдельными буферами и безоконным контекстом. Пока честно отказываем, чтобы
 * не изображать работоспособность, которой нет.
 */
static struct gbm_surface *pvr_surface_create(struct gbm_device *gbm,
					      uint32_t width, uint32_t height,
					      uint32_t format, uint32_t flags,
					      const uint64_t *modifiers,
					      const unsigned count)
{
	(void)gbm; (void)width; (void)height; (void)format; (void)flags;
	(void)modifiers; (void)count;
	trace("поверхности пока не поддержаны");
	errno = ENOSYS;
	return NULL;
}

static struct gbm_bo *pvr_surface_lock_front_buffer(struct gbm_surface *s)
{
	(void)s;
	errno = ENOSYS;
	return NULL;
}

static void pvr_surface_release_buffer(struct gbm_surface *s, struct gbm_bo *bo)
{
	(void)s; (void)bo;
}

static int pvr_surface_has_free_buffers(struct gbm_surface *s)
{
	(void)s;
	return 0;
}

static void pvr_surface_destroy(struct gbm_surface *s)
{
	free(s);
}

/* --- устройство ------------------------------------------------------ */

static int pvr_is_format_supported(struct gbm_device *gbm, uint32_t format,
				   uint32_t usage)
{
	unsigned bpp;

	(void)gbm;
	if (usage & GBM_BO_USE_CURSOR && usage & GBM_BO_USE_RENDERING)
		return 0;

	return hal_format(core->v0.format_canonicalize(format), &bpp) >= 0;
}

static int pvr_get_format_modifier_plane_count(struct gbm_device *gbm,
					       uint32_t format,
					       uint64_t modifier)
{
	unsigned bpp;

	(void)gbm;
	if (hal_format(core->v0.format_canonicalize(format), &bpp) < 0)
		return -1;

	return modifier == DRM_FORMAT_MOD_LINEAR ? 1 : -1;
}

static void pvr_destroy(struct gbm_device *gbm)
{
	free(to_dev(gbm));
}

static struct gbm_device *pvr_create_device(int fd, uint32_t backend_version)
{
	struct pvr_device *dev;
	drmVersionPtr ver;

	trace_on = getenv("PVR_GBM_TRACE") != NULL;

	/*
	 * Проверяем, что нам дали именно наш GPU. Загрузчик Mesa зовёт бэкенд
	 * по имени драйвера, но нас могут выбрать и переменной GBM_BACKEND —
	 * тогда лучше отказаться, чем выдавать буферы поверх чужого устройства.
	 */
	ver = drmGetVersion(fd);
	if (!ver) {
		trace("drmGetVersion отказал");
		return NULL;
	}
	if (strcmp(ver->name, "pvr") != 0) {
		trace("устройство не наше: драйвер %s", ver->name);
		drmFreeVersion(ver);
		return NULL;
	}
	drmFreeVersion(ver);

	dev = calloc(1, sizeof(*dev));
	if (!dev)
		return NULL;

	dev->fd = fd;

	/* 0 — обычные буферы, не фреймбуфер. */
	hybris_gralloc_initialize(0);

	dev->base.v0.fd = fd;
	dev->base.v0.name = "pvr";
	/*
	 * Версию копируем ту, что дали, слово в слово: загрузчик сверяет её
	 * на точное равенство и при несовпадении молча выбрасывает устройство,
	 * не сказав ни слова.
	 */
	dev->base.v0.backend_version = backend_version;

	dev->base.v0.destroy = pvr_destroy;
	dev->base.v0.is_format_supported = pvr_is_format_supported;
	dev->base.v0.get_format_modifier_plane_count = pvr_get_format_modifier_plane_count;
	dev->base.v0.bo_create = pvr_bo_create;
	dev->base.v0.bo_import = pvr_bo_import;
	dev->base.v0.bo_map = pvr_bo_map;
	dev->base.v0.bo_unmap = pvr_bo_unmap;
	dev->base.v0.bo_write = pvr_bo_write;
	dev->base.v0.bo_get_fd = pvr_bo_get_fd;
	dev->base.v0.bo_get_planes = pvr_bo_get_planes;
	dev->base.v0.bo_get_handle = pvr_bo_get_handle;
	dev->base.v0.bo_get_plane_fd = pvr_bo_get_plane_fd;
	dev->base.v0.bo_get_stride = pvr_bo_get_stride;
	dev->base.v0.bo_get_offset = pvr_bo_get_offset;
	dev->base.v0.bo_get_modifier = pvr_bo_get_modifier;
	dev->base.v0.bo_destroy = pvr_bo_destroy;
	dev->base.v0.surface_create = pvr_surface_create;
	dev->base.v0.surface_lock_front_buffer = pvr_surface_lock_front_buffer;
	dev->base.v0.surface_release_buffer = pvr_surface_release_buffer;
	dev->base.v0.surface_has_free_buffers = pvr_surface_has_free_buffers;
	dev->base.v0.surface_destroy = pvr_surface_destroy;

	trace("устройство готово, fd %d, ABI %u", fd, backend_version);
	return &dev->base;
}

static struct gbm_backend pvr_backend = {
	.v0.backend_version = GBM_BACKEND_ABI_VERSION,
	.v0.backend_name = "pvr",
	.v0.create_device = pvr_create_device,
};

__attribute__((visibility("default")))
const struct gbm_backend *GBM_GET_BACKEND_PROC(const struct gbm_core *gbm_core)
{
	core = gbm_core;
	return &pvr_backend;
}
