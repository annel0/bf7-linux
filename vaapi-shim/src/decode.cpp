/*
 * BF7: путь декодирования драйвера VA-API.
 *
 * Клиент отдаёт картинку по частям: между vaBeginPicture и vaEndPicture он
 * присылает параметры, таблицы квантования и слайсы. Мы их накапливаем, а в
 * vaEndPicture собираем из них единицу доступа в Annex-B и отправляем демону.
 *
 * Опознание кадра. VA-API просит декодировать картинку в конкретную
 * поверхность и потом ждёт именно её. Наш декодер отдаёт кадры когда готов и
 * в своём порядке, поэтому каждая единица доступа помечается меткой времени,
 * равной номеру поверхности по порядку; Codec2 метку сохраняет, и пришедший
 * кадр мы кладём туда, откуда метка взялась.
 */
#include "bf7va.hpp"
#include "bf7-videod-proto.h"

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <va/va_drmcommon.h>

namespace bf7va {

#define UNUSED(x) (void)(x)

static Driver *drv(VADriverContextP ctx)
{
	return static_cast<Driver *>(ctx->pDriverData);
}

static bool trace_on(void)
{
	static int on = -1;

	if (on < 0)
		on = getenv("BF7VA_TRACE") ? 1 : 0;
	return on != 0;
}

#define TRACE(...) do { if (trace_on()) { \
	fprintf(stderr, "bf7va: "); fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); } } while (0)

/* ------------------------------------------------------------------ */

VAStatus CreateContext(VADriverContextP ctx, VAConfigID cfg, int w, int h,
		       int flag, VASurfaceID *targets, int num,
		       VAContextID *out)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto ci = d->configs.find(cfg);
	Context c;

	UNUSED(flag); UNUSED(targets); UNUSED(num);
	if (ci == d->configs.end())
		return VA_STATUS_ERROR_INVALID_CONFIG;

	c.config = cfg;
	c.profile = ci->second.profile;
	c.width = (unsigned)w;
	c.height = (unsigned)h;
	c.sock = daemon_connect();
	if (c.sock < 0) {
		TRACE("демон декодирования недоступен");
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}
	if (!daemon_open(c.sock, is_hevc(c.profile) ? BF7V_CODEC_H265 : BF7V_CODEC_H264,
			 c.width, c.height)) {
		TRACE("демон отказался открыть декодер %dx%d", w, h);
		close(c.sock);
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}
	c.opened = true;

	*out = (VAContextID)d->next_id++;
	d->contexts[*out] = std::move(c);
	TRACE("контекст %u: %dx%d", *out, w, h);
	return VA_STATUS_SUCCESS;
}

VAStatus DestroyContext(VADriverContextP ctx, VAContextID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->contexts.find(id);

	if (it == d->contexts.end())
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	if (it->second.sock >= 0) {
		struct bf7v_request req;

		memset(&req, 0, sizeof req);
		req.type = BF7V_MSG_CLOSE;
		(void)!write(it->second.sock, &req, sizeof req);
		close(it->second.sock);
	}
	d->contexts.erase(it);
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Буферы                                                              */
/* ------------------------------------------------------------------ */

VAStatus CreateBuffer(VADriverContextP ctx, VAContextID c, VABufferType type,
		      unsigned int size, unsigned int count, void *data,
		      VABufferID *out)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	Buffer b;

	UNUSED(c);
	b.type = type;
	b.size = size;
	b.count = count;
	b.data.resize((size_t)size * count);
	if (data)
		memcpy(b.data.data(), data, b.data.size());

	*out = (VABufferID)d->next_id++;
	d->buffers[*out] = std::move(b);
	return VA_STATUS_SUCCESS;
}

VAStatus BufferSetNumElements(VADriverContextP ctx, VABufferID id,
			      unsigned int count)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->buffers.find(id);

	if (it == d->buffers.end())
		return VA_STATUS_ERROR_INVALID_BUFFER;
	it->second.count = count;
	it->second.data.resize((size_t)it->second.size * count);
	return VA_STATUS_SUCCESS;
}

static void dmabuf_sync(int fd, bool start);

VAStatus MapBuffer(VADriverContextP ctx, VABufferID id, void **out)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->buffers.find(id);

	if (it == d->buffers.end())
		return VA_STATUS_ERROR_INVALID_BUFFER;
	Buffer &b = it->second;

	/* Отложенное отображение кадра: см. DeriveImage. */
	if (!b.map && b.sync_fd >= 0 && b.map_size) {
		void *base = mmap(nullptr, b.map_size, PROT_READ, MAP_SHARED,
				  b.sync_fd, 0);

		if (base == MAP_FAILED) {
			TRACE("буфер %u: отображение не вышло: %s", id,
			      strerror(errno));
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}
		b.map = base;
		dmabuf_sync(b.sync_fd, true);
		TRACE("буфер %u отображён в память: кадр читает процессор", id);
	}
	b.mapped = true;
	*out = b.map ? b.map : b.data.data();
	return VA_STATUS_SUCCESS;
}

VAStatus UnmapBuffer(VADriverContextP ctx, VABufferID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->buffers.find(id);

	if (it == d->buffers.end())
		return VA_STATUS_ERROR_INVALID_BUFFER;
	it->second.mapped = false;
	return VA_STATUS_SUCCESS;
}

VAStatus DestroyBuffer(VADriverContextP ctx, VABufferID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);

	return d->buffers.erase(id) ? VA_STATUS_SUCCESS
				    : VA_STATUS_ERROR_INVALID_BUFFER;
}

/*
 * Кадр целиком, а не по плоскостям.
 *
 * Драйвер PowerVR умеет втягивать дескриптором только целые кадры: в списке
 * его форматов есть NV12, YV12, AB24 и подобные, но нет однобайтового R8. А
 * проигрыватели (mpv, Firefox) разбирают YUV именно по плоскостям — яркость
 * как R8, цветность как GR88, — и получают EGL_BAD_MATCH.
 *
 * Выход: просить у декодера сразу RGBA. Преобразование делает MDP, то есть
 * железо, и кадр остаётся одним слоем, который EGL принимает. Тогда показ
 * идёт вовсе без копий процессором.
 *
 * Согласуется это парой переменных: BF7V_RGB у демона и BF7VA_RGB здесь.
 */
bool rgb_mode(void)
{
	static int on = -1;

	if (on < 0)
		on = getenv("BF7VA_RGB") ? 1 : 0;
	return on != 0;
}

/* Формат кадра приходит от gralloc; в fourcc его переводим здесь. */
#define BF7_HAL_RGBA_8888	1

static uint32_t fourcc_of_frame(uint32_t hal_format)
{
	if (hal_format == BF7_HAL_RGBA_8888)
		return VA_FOURCC_RGBA;
	return hal_format;		/* YV12 и NV12 совпадают с fourcc */
}

/* ------------------------------------------------------------------ */
/* Своя память поверхности                                             */
/* ------------------------------------------------------------------ */

/*
 * Выделение буфера через gralloc, подгружаемый по необходимости.
 *
 * Связываться с ним на этапе сборки не хочется: тогда libhybris въезжает в
 * каждый процесс, который всего лишь открыл libva, — а ffmpeg, например,
 * прекрасно обходится без него. Поэтому библиотека открывается только когда
 * действительно понадобился буфер.
 */
struct GrallocApi {
	void (*initialize)(int framebuffer) = nullptr;
	int (*allocate)(int w, int h, int format, int usage,
			void **handle, uint32_t *stride) = nullptr;
	int (*release)(const void *handle, int was_allocated) = nullptr;
	bool ok = false;
};

/*
 * Есть ли вокруг андроидная часть.
 *
 * Спрашивать обязательно, и вот почему. В libhybris символы Android
 * связываются по требованию: HYBRIS_DLSYSM берёт адрес через android_dlsym
 * и тут же зовёт по нему, а неудачу ловит assert — то есть в сборке с
 * NDEBUG не ловит вовсе. Если андроидных библиотек не видно, указатель
 * остаётся нулевым и вызов уходит по адресу 0. Проверено отладчиком:
 *
 *   #0  0x0 in ?? ()
 *   #1  hybris_gralloc_initialize (framebuffer=0) at gralloc.c:118
 *   #2  bf7va::ensure_backing(bf7va::Surface&)
 *
 * (gralloc.c:118 — это вызов hw_get_module из libhardware.so.)
 *
 * Так падал процесс декодирования Firefox: он запирает RDD в пустой
 * каталог, оттуда /system и /vendor не видно, и наш драйвер ронял чужой
 * процесс. Библиотека, лежащая в общем каталоге libva, такого права не
 * имеет: не можем — обязаны отказать, а не обрушить.
 */
static bool android_side_reachable(void)
{
	static const char *const need[] = {
		"/system/lib64/libhardware.so",	/* в ней hw_get_module */
		"/vendor/lib64/hw",		/* там сам модуль gralloc */
	};

	for (const char *p : need) {
		if (access(p, R_OK) != 0) {
			TRACE("андроидной части не видно (%s): gralloc не трогаем", p);
			return false;
		}
	}
	return true;
}

static GrallocApi &gralloc(void)
{
	static GrallocApi api;
	static bool tried = false;

	if (tried)
		return api;
	tried = true;

	if (!android_side_reachable())
		return api;

	void *lib = dlopen("libgralloc.so.1", RTLD_NOW);

	if (!lib)
		lib = dlopen("/usr/local/lib/libgralloc.so.1", RTLD_NOW);
	if (!lib) {
		TRACE("gralloc недоступен: %s", dlerror());
		return api;
	}
	api.initialize = (void (*)(int))dlsym(lib, "hybris_gralloc_initialize");
	api.allocate = (int (*)(int, int, int, int, void **, uint32_t *))
		dlsym(lib, "hybris_gralloc_allocate");
	api.release = (int (*)(const void *, int))
		dlsym(lib, "hybris_gralloc_release");
	api.ok = api.initialize && api.allocate && api.release;
	if (api.ok)
		api.initialize(0);
	else
		TRACE("в gralloc нет нужных функций");
	return api;
}

/* Раскладка native_handle: версия, число дескрипторов, число чисел, данные. */
struct Bf7NativeHandle {
	int version;
	int num_fds;
	int num_ints;
	int data[0];
};

#define BF7_HAL_PIXEL_FORMAT_YV12	0x32315659
#define BF7_GRALLOC_USAGE_HW_TEXTURE	0x00000100

/*
 * Завести поверхности собственный буфер, если его ещё нет.
 * Возвращает false, если не вышло — тогда вывезти поверхность нечем.
 */
static bool ensure_backing(Surface &s)
{
	if (s.own_fd >= 0)
		return true;
	if (!s.width || !s.height)
		return false;

	GrallocApi &g = gralloc();

	if (!g.ok)
		return false;

	void *handle = nullptr;
	uint32_t stride = 0;
	int r = g.allocate((int)s.width, (int)s.height,
			   rgb_mode() ? BF7_HAL_RGBA_8888
				      : BF7_HAL_PIXEL_FORMAT_YV12,
			   BF7_GRALLOC_USAGE_HW_TEXTURE, &handle, &stride);

	if (r != 0 || !handle) {
		TRACE("gralloc не дал буфер %ux%u: %d", s.width, s.height, r);
		return false;
	}

	const Bf7NativeHandle *h = (const Bf7NativeHandle *)handle;
	size_t need = (size_t)stride * s.height * (rgb_mode() ? 4 : 1);
	int fd = -1;

	for (int i = 0; i < h->num_fds; i++) {
		off_t size = lseek(h->data[i], 0, SEEK_END);

		if (size > 0 && (size_t)size >= need) {
			fd = h->data[i];
			break;
		}
	}
	if (fd < 0 && h->num_fds > 0)
		fd = h->data[0];
	if (fd < 0) {
		g.release(handle, 1);
		return false;
	}

	s.own_handle = handle;
	s.own_fd = fd;
	s.own_stride = stride;
	TRACE("поверхности заведён свой буфер %ux%u шаг %u", s.width, s.height,
	      stride);
	return true;
}

void release_backing(Surface &s)
{
	if (!s.own_handle)
		return;

	GrallocApi &g = gralloc();

	if (s.own_map)
		munmap(s.own_map, s.own_map_size);
	s.own_map = nullptr;
	s.own_map_size = 0;
	s.own_exported = false;
	if (g.ok)
		g.release(s.own_handle, 1);
	s.own_handle = nullptr;
	s.own_fd = -1;
}

/* ------------------------------------------------------------------ */
/* Образ поверхности                                                   */
/* ------------------------------------------------------------------ */

/*
 * Ядро объявляет это в linux/dma-buf.h, но заголовок есть не во всяком
 * наборе для сборки, а нужна из него одна команда.
 */
struct bf7_dma_buf_sync { uint64_t flags; };
#define BF7_DMA_BUF_SYNC_READ	(1ull << 0)
#define BF7_DMA_BUF_SYNC_START	(0ull << 2)
#define BF7_DMA_BUF_SYNC_END	(1ull << 2)
#define BF7_DMA_BUF_IOCTL_SYNC	_IOW('b', 0, struct bf7_dma_buf_sync)

static void dmabuf_sync(int fd, bool start)
{
	struct bf7_dma_buf_sync s;

	s.flags = BF7_DMA_BUF_SYNC_READ |
		  (start ? BF7_DMA_BUF_SYNC_START : BF7_DMA_BUF_SYNC_END);
	(void)ioctl(fd, BF7_DMA_BUF_IOCTL_SYNC, &s);
}

/*
 * Раскладка кадра в памяти: где какая плоскость и с каким шагом.
 * Общая для нашего образа и для кадра, пришедшего от декодера.
 */
struct Layout {
	unsigned planes = 0;
	unsigned pitch[3] = { 0, 0, 0 };
	unsigned off[3] = { 0, 0, 0 };
	unsigned size = 0;
};

static bool layout_of(uint32_t fourcc, unsigned stride, unsigned w,
		      unsigned h, Layout *l)
{
	unsigned luma = stride * h;

	switch (fourcc) {
	case VA_FOURCC_NV12:
		l->planes = 2;
		l->pitch[0] = stride;
		l->pitch[1] = stride;
		l->off[0] = 0;
		l->off[1] = luma;
		l->size = luma + stride * (h / 2);
		return true;
	case VA_FOURCC_YV12:
	case VA_FOURCC_I420: {
		/* У gralloc шаг цветности выравнивается до 16 байт. */
		unsigned cs = ((stride / 2) + 15) & ~15u;
		unsigned chroma = cs * (h / 2);

		l->planes = 3;
		l->pitch[0] = stride;
		l->pitch[1] = cs;
		l->pitch[2] = cs;
		l->off[0] = 0;
		l->off[1] = luma;
		l->off[2] = luma + chroma;
		l->size = luma + 2 * chroma;
		return true;
	}
	default:
		UNUSED(w);
		return false;
	}
}

/* Плоскость цветности в YV12 — V, в I420 — U; различаются они только этим. */
static bool swapped_chroma(uint32_t a, uint32_t b)
{
	bool a_yv = a == VA_FOURCC_YV12, b_yv = b == VA_FOURCC_YV12;

	return (a == VA_FOURCC_YV12 || a == VA_FOURCC_I420) &&
	       (b == VA_FOURCC_YV12 || b == VA_FOURCC_I420) && a_yv != b_yv;
}

VAStatus CreateImage(VADriverContextP ctx, VAImageFormat *fmt, int width,
		     int height, VAImage *image)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	Layout l;

	if (!fmt || width <= 0 || height <= 0)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	/* Шаг свой, поэтому берём выровненную ширину без оглядки на кадр. */
	if (!layout_of(fmt->fourcc, ((unsigned)width + 15) & ~15u,
		       (unsigned)width, (unsigned)height, &l))
		return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;

	Buffer b;

	b.type = VAImageBufferType;
	b.size = l.size;
	b.count = 1;
	b.data.resize(l.size);
	b.fourcc_hint = fmt->fourcc;

	VABufferID bid = (VABufferID)d->next_id++;

	d->buffers[bid] = std::move(b);

	memset(image, 0, sizeof *image);
	image->image_id = (VAImageID)d->next_id++;
	image->buf = bid;
	image->format = *fmt;
	image->width = (unsigned short)width;
	image->height = (unsigned short)height;
	image->data_size = l.size;
	image->num_planes = l.planes;
	for (unsigned i = 0; i < l.planes; i++) {
		image->pitches[i] = l.pitch[i];
		image->offsets[i] = l.off[i];
	}
	d->images[image->image_id] = bid;
	return VA_STATUS_SUCCESS;
}

/*
 * Переложить кадр из dma-buf декодера в образ клиента.
 *
 * Это единственное место во всём пути, где картинка копируется процессором, и
 * нужно оно лишь тем, кто хочет смотреть на пиксели сам. Ускоренный показ
 * идёт мимо — через vaExportSurfaceHandle, дескриптором.
 */
VAStatus GetImage(VADriverContextP ctx, VASurfaceID id, int x, int y,
		  unsigned int w, unsigned int h, VAImageID image)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto si = d->surfaces.find(id);
	auto ii = d->images.find(image);

	UNUSED(x); UNUSED(y);
	if (si == d->surfaces.end())
		return VA_STATUS_ERROR_INVALID_SURFACE;
	if (ii == d->images.end())
		return VA_STATUS_ERROR_INVALID_IMAGE;

	auto bi = d->buffers.find(ii->second);

	if (bi == d->buffers.end())
		return VA_STATUS_ERROR_INVALID_IMAGE;

	Surface &s = si->second;
	Buffer &b = bi->second;

	/*
	 * Кадр обычно лежит в буфере декодера; но если его пришлось спасти в
	 * свой буфер поверхности (переоткрытие декодера H.265, preserve_frames),
	 * читаем оттуда.
	 */
	int sfd = s.dma_fd;
	unsigned sstride = s.stride;
	uint32_t sfourcc = s.fourcc;

	if (sfd < 0 && s.ready && s.own_fd >= 0) {
		sfd = s.own_fd;
		sstride = s.own_stride;
		sfourcc = rgb_mode() ? VA_FOURCC_RGBA : VA_FOURCC_YV12;
	}
	if (!s.ready || sfd < 0)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	Layout src, dst;

	if (!layout_of(sfourcc, sstride, s.width, s.height, &src) ||
	    !layout_of(b.fourcc_hint, ((unsigned)s.width + 15) & ~15u,
		       s.width, s.height, &dst)) {
		TRACE("раскладка неизвестна: кадр %.4s, образ %.4s",
		      (const char *)&sfourcc, (const char *)&b.fourcc_hint);
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}
	if (b.data.size() < dst.size) {
		TRACE("образ мал: %zu < %u", b.data.size(), dst.size);
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	off_t whole = lseek(sfd, 0, SEEK_END);

	if (whole <= 0 || (unsigned)whole < src.size)
		return VA_STATUS_ERROR_OPERATION_FAILED;

	uint8_t *base = (uint8_t *)mmap(nullptr, (size_t)whole, PROT_READ,
					MAP_SHARED, sfd, 0);

	if (base == MAP_FAILED)
		return VA_STATUS_ERROR_OPERATION_FAILED;
	dmabuf_sync(sfd, true);

	unsigned copy_w = w && w < s.width ? w : s.width;
	unsigned copy_h = h && h < s.height ? h : s.height;
	unsigned cw = copy_w / 2, ch = copy_h / 2;

	/* Яркость одинакова во всех наших раскладках. */
	for (unsigned r = 0; r < copy_h; r++)
		memcpy(b.data.data() + dst.off[0] + (size_t)r * dst.pitch[0],
		       base + src.off[0] + (size_t)r * src.pitch[0], copy_w);

	if (src.planes == dst.planes) {
		/* YV12 и I420 различаются лишь порядком плоскостей. */
		bool swap = swapped_chroma(sfourcc, b.fourcc_hint);

		for (unsigned pl = 1; pl < src.planes; pl++) {
			unsigned sp = swap ? (pl == 1 ? 2 : 1) : pl;
			unsigned bytes = src.planes == 3 ? cw : copy_w;

			for (unsigned r = 0; r < ch; r++)
				memcpy(b.data.data() + dst.off[pl] +
				       (size_t)r * dst.pitch[pl],
				       base + src.off[sp] +
				       (size_t)r * src.pitch[sp], bytes);
		}
	} else if (src.planes == 3 && dst.planes == 2) {
		/*
		 * Разделённая цветность -> чересстрочная. В NV12 первым идёт
		 * U, а в YV12 от декодера порядок плоскостей Y, V, U — то
		 * есть U лежит последним.
		 */
		unsigned up = sfourcc == VA_FOURCC_YV12 ? 2 : 1;
		unsigned vp = sfourcc == VA_FOURCC_YV12 ? 1 : 2;

		for (unsigned r = 0; r < ch; r++) {
			const uint8_t *su = base + src.off[up] +
					    (size_t)r * src.pitch[up];
			const uint8_t *sv = base + src.off[vp] +
					    (size_t)r * src.pitch[vp];
			uint8_t *to = b.data.data() + dst.off[1] +
				      (size_t)r * dst.pitch[1];

			for (unsigned i = 0; i < cw; i++) {
				to[2 * i] = su[i];
				to[2 * i + 1] = sv[i];
			}
		}
	} else if (src.planes == 2 && dst.planes == 3) {
		unsigned up = b.fourcc_hint == VA_FOURCC_YV12 ? 2 : 1;
		unsigned vp = b.fourcc_hint == VA_FOURCC_YV12 ? 1 : 2;

		for (unsigned r = 0; r < ch; r++) {
			const uint8_t *from = base + src.off[1] +
					      (size_t)r * src.pitch[1];
			uint8_t *du = b.data.data() + dst.off[up] +
				      (size_t)r * dst.pitch[up];
			uint8_t *dv = b.data.data() + dst.off[vp] +
				      (size_t)r * dst.pitch[vp];

			for (unsigned i = 0; i < cw; i++) {
				du[i] = from[2 * i];
				dv[i] = from[2 * i + 1];
			}
		}
	}

	dmabuf_sync(sfd, false);
	munmap(base, (size_t)whole);
	return VA_STATUS_SUCCESS;
}

static void collect(Driver *d, Context &c, bool ask);
static bool copy_to_own(Surface &s);

/*
 * Дождаться, пока декодер отдаст кадр этой поверхности.
 *
 * Строго по VA-API клиент обязан позвать vaSyncSurface, прежде чем брать
 * содержимое. GStreamer этого не делает: у настоящих драйверов поверхность
 * готова уже к моменту vaDeriveImage, потому что декодирование там идёт в
 * ядре. У нас кадр приходит от демона, и ждать приходится нам самим —
 * иначе клиент получает пустой запасной буфер и рисует зелёный экран.
 */
/*
 * H.265: выпустить кадр, который декодер держит.
 *
 * Декодер HEVC у MediaTek всегда держит один кадр до следующей картинки (у
 * H.264 такого нет). Не помогают ни sps_max_num_reorder_pics = 0 (и с одним
 * подслоем), ни сброс без конца потока (drain NO_EOS), ни режим видеозвонка
 * mtk.ext.vdec.vilte, ни разделитель AUD после картинки (проверено
 * 2026-09-25). В
 * середине потока это не мешает: клиент просит кадры в порядке показа и к
 * тому времени уже подал следующие. Но в конце файла следующей картинки нет, и
 * последний кадр не вышел бы никогда. Если просьбы о выдаче не помогают, шлём
 * конец потока (пустой разделитель AUD с флагом EOS) — тогда декодер отдаёт
 * всё. Следующая картинка переоткроет декодер (EndPicture, eos_sent).
 */
/*
 * Спасти кадры, лежащие в буферах декодера, в свои буферы поверхностей.
 *
 * Нужно перед переоткрытием декодера (EndPicture, eos_sent): при закрытии
 * компонента его пул выходных буферов переходит новому, и тот пишет поверх
 * кадров, которые клиент ещё не забрал (проверено: -stream_loop — последний
 * кадр прохода испорчен). Буфер декодера после копии возвращаем демону.
 */
static void preserve_frames(Driver *d, Context &c)
{
	for (auto &kv : d->surfaces) {
		Surface &s = kv.second;

		if (!s.ready || s.dma_fd < 0 || !s.frame_handle)
			continue;
		if (!ensure_backing(s))
			continue;

		bool was = s.own_exported;

		s.own_exported = true;	/* copy_to_own копирует только так */
		bool ok = copy_to_own(s);

		s.own_exported = was;
		if (!ok)
			continue;
		close(s.dma_fd);
		s.dma_fd = -1;
		daemon_release(c.sock, s.frame_handle);
		s.frame_handle = 0;
	}
}

static void flush_held(Context &c)
{
	static const uint8_t aud[] = { 0, 0, 0, 1, 0x46, 0x01, 0x50 };

	/*
	 * Только когда в декодере осталась одна картинка — ровно тот
	 * удержанный кадр в конце файла. После (пере)открытия первые кадры
	 * идут долго, а поданных картинок много: EOS там был бы ложным и
	 * оборвал бы поток (проверено: -stream_loop).
	 */
	if (!is_hevc(c.profile) || c.eos_sent || c.inflight.size() != 1)
		return;
	if (daemon_queue(c.sock, aud, sizeof aud, ++c.next_pts * 33333,
			 BF7V_FLAG_END_OF_STREAM)) {
		c.eos_sent = true;
		TRACE("H.265: кадр задержан декодером — конец потока, чтобы выпустить");
	}
}

static void ensure_decoded(Driver *d, VASurfaceID id)
{
	auto si = d->surfaces.find(id);

	if (si == d->surfaces.end() || !si->second.pending)
		return;

	/*
	 * Сначала просто забираем то, что демон уже подтолкнул: он сбрасывает
	 * готовые кадры перед чтением каждой очередной просьбы, так что после
	 * подачи картинки в сокете обычно уже что-то лежит. Просить выдачу
	 * отдельно дорого — декодер MediaTek на этом тратит три четверти
	 * процессора, — поэтому делаем это только когда кадра правда нет.
	 */
	for (auto &kv : d->contexts)
		collect(d, kv.second, false);

	for (int tries = 0; tries < 20 && si->second.pending; tries++) {
		if (tries == 3)
			for (auto &kv : d->contexts)
				flush_held(kv.second);
		for (auto &kv : d->contexts)
			collect(d, kv.second, true);
	}
}

/*
 * Вернуть демону кадр, лежащий в поверхности.
 *
 * После этого поверхность снова считается пустой: показывать из неё нечего,
 * а если клиент снова о ней спросит, ей заведут свой буфер.
 */
static void release_frame(Driver *d, VASurfaceID id)
{
	if (id == VA_INVALID_ID)
		return;

	auto si = d->surfaces.find(id);

	if (si == d->surfaces.end())
		return;

	Surface &s = si->second;

	if (!s.frame_handle)
		return;

	for (auto &kv : d->contexts) {
		daemon_release(kv.second.sock, s.frame_handle);
		break;
	}
	s.frame_handle = 0;
	if (s.dma_fd >= 0) {
		close(s.dma_fd);
		s.dma_fd = -1;
	}
	s.ready = false;
}

/*
 * Показать готовый кадр как образ VA-API.
 *
 * Своей памяти у поверхности нет: кадр лежит в dma-buf декодера. Мы его
 * отображаем и отдаём клиенту прямо оттуда — копии не возникает даже здесь,
 * где клиент собирается читать картинку процессором. Без синхронизации
 * dma-buf чтение возвращает нули: кадр ещё в кэше устройства.
 */
VAStatus DeriveImage(VADriverContextP ctx, VASurfaceID id, VAImage *image)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto si = d->surfaces.find(id);

	if (si == d->surfaces.end()) {
		TRACE("образ просят у незнакомой поверхности %u", id);
		return VA_STATUS_ERROR_INVALID_SURFACE;
	}

	ensure_decoded(d, id);

	Surface &s = si->second;
	int src_fd = s.ready ? s.dma_fd : -1;
	uint32_t src_fourcc = s.ready ? s.fourcc : VA_FOURCC_YV12;
	unsigned src_stride = s.stride;

	if (src_fd < 0) {
		/*
		 * Поверхность цела, не вышло именно выделить под неё буфер
		 * (например, андроидной части не видно). Код должен говорить
		 * об этом прямо, иначе получатель решит, что мы подсунули ему
		 * несуществующую поверхность, и будет искать поломку у себя.
		 */
		if (!ensure_backing(s)) {
			TRACE("поверхности %u нечем подпереть образ", id);
			return VA_STATUS_ERROR_ALLOCATION_FAILED;
		}
		src_fd = s.own_fd;
		src_stride = s.own_stride;
	}
	if (src_fourcc != VA_FOURCC_YV12 && src_fourcc != VA_FOURCC_NV12 &&
	    src_fourcc != VA_FOURCC_RGBA) {
		TRACE("образ: формат %.4s не наш", (const char *)&src_fourcc);
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	off_t whole = lseek(src_fd, 0, SEEK_END);

	if (whole <= 0) {
		TRACE("образ: размер буфера не узнать");
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	/*
	 * Отображать буфер сразу не нужно.
	 *
	 * Тому, кто берёт кадр дескриптором (GStreamer, браузеры), память
	 * процессора не нужна вовсе, а отображение полумегабайта со сбросом
	 * кэша на каждый кадр обходится дорого. Поэтому откладываем до
	 * vaMapBuffer: кто вправду собрался читать пиксели, тот и заплатит.
	 */
	Buffer b;

	b.type = VAImageBufferType;
	b.size = (unsigned)whole;
	b.count = 1;
	b.map = nullptr;
	b.map_size = (size_t)whole;
	b.sync_fd = src_fd;
	b.fourcc_hint = src_fourcc;
	b.from_surface = id;

	VABufferID bid = (VABufferID)d->next_id++;

	d->buffers[bid] = std::move(b);

	memset(image, 0, sizeof *image);
	image->image_id = (VAImageID)d->next_id++;
	image->buf = bid;
	image->width = (unsigned short)s.width;
	image->height = (unsigned short)s.height;
	image->data_size = (unsigned)whole;
	image->format.byte_order = VA_LSB_FIRST;
	image->format.fourcc = src_fourcc;
	image->format.bits_per_pixel = 12;

	unsigned luma = src_stride * s.height;

	if (src_fourcc == VA_FOURCC_NV12) {
		image->num_planes = 2;
		image->pitches[0] = src_stride;
		image->pitches[1] = src_stride;
		image->offsets[0] = 0;
		image->offsets[1] = luma;
	} else {
		/*
		 * YV12 от gralloc: за яркостью идёт V, потом U, и шаг
		 * цветности выравнивается до 16 байт — не просто «половина».
		 */
		unsigned cs = ((src_stride / 2) + 15) & ~15u;
		unsigned chroma = cs * (s.height / 2);

		image->num_planes = 3;
		image->pitches[0] = src_stride;
		image->pitches[1] = cs;
		image->pitches[2] = cs;
		image->offsets[0] = 0;
		image->offsets[1] = luma;
		image->offsets[2] = luma + chroma;
	}

	d->images[image->image_id] = bid;
	TRACE("образ %u: поверхность %u, %ux%u, %u байт",
	      image->image_id, id, s.width, s.height, (unsigned)whole);
	return VA_STATUS_SUCCESS;
}

/*
 * Старый способ отдать кадр дескриптором: vaAcquireBufferHandle.
 *
 * Им пользуется GStreamer (gstvaapisurface_drm.c): сперва vaDeriveImage,
 * потом просит у полученного буфера дескриптор dma-buf. Это предшественник
 * vaExportSurfaceHandle, и для нас он даже удобнее — кадр уходит одним
 * куском, а раскладку плоскостей клиент берёт из VAImage. Именно так его
 * втягивает glimagesink, целой внешней текстурой, а не по плоскостям.
 */
#define BF7_VA_MEM_TYPE_DRM_PRIME	0x20000000
#define BF7_VA_MEM_TYPE_DRM_PRIME_2	0x40000000

VAStatus AcquireBufferHandle(VADriverContextP ctx, VABufferID id,
			     VABufferInfo *info)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->buffers.find(id);

	if (it == d->buffers.end())
		return VA_STATUS_ERROR_INVALID_BUFFER;
	if (!info)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	Buffer &b = it->second;

	if (b.sync_fd < 0) {
		TRACE("буфер %u не из кадра, дескриптора нет", id);
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	}
	/* Просят конкретный вид памяти; кроме dma-buf мы ничего не умеем. */
	uint32_t both = BF7_VA_MEM_TYPE_DRM_PRIME | BF7_VA_MEM_TYPE_DRM_PRIME_2;

	if (info->mem_type && !(info->mem_type & both)) {
		TRACE("буфер %u: просят вид памяти 0x%x, а у нас dma-buf",
		      id, info->mem_type);
		return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
	}

	if (b.exported_fd < 0) {
		b.exported_fd = dup(b.sync_fd);
		if (b.exported_fd < 0)
			return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	off_t whole = lseek(b.sync_fd, 0, SEEK_END);

	info->handle = (uintptr_t)b.exported_fd;
	info->type = VAImageBufferType;
	info->mem_type = (info->mem_type & BF7_VA_MEM_TYPE_DRM_PRIME) ?
			 BF7_VA_MEM_TYPE_DRM_PRIME : BF7_VA_MEM_TYPE_DRM_PRIME_2;
	info->mem_size = whole > 0 ? (size_t)whole : b.map_size;
	TRACE("буфер %u выдан дескриптором, %zu байт", id, info->mem_size);
	return VA_STATUS_SUCCESS;
}

VAStatus ReleaseBufferHandle(VADriverContextP ctx, VABufferID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->buffers.find(id);

	if (it == d->buffers.end())
		return VA_STATUS_ERROR_INVALID_BUFFER;
	if (it->second.exported_fd >= 0) {
		close(it->second.exported_fd);
		it->second.exported_fd = -1;
	}
	/*
	 * Вот теперь клиент с кадром закончил: буфер возвращаем демону. Иначе
	 * у декодера кончаются буферы — их меньше, чем поверхностей заводит
	 * проигрыватель, — и он упирается в предел дескрипторов.
	 */
	release_frame(d, it->second.from_surface);
	return VA_STATUS_SUCCESS;
}

VAStatus DestroyImage(VADriverContextP ctx, VAImageID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto ii = d->images.find(id);

	if (ii == d->images.end())
		return VA_STATUS_ERROR_INVALID_IMAGE;

	auto bi = d->buffers.find(ii->second);

	if (bi != d->buffers.end()) {
		/*
		 * Если дескриптор наружу не отдавали, клиент читал кадр через
		 * отображение и уже закончил — буфер можно вернуть демону.
		 * А если отдавали, кадр ещё нужен: GStreamer уничтожает образ
		 * сразу, а рисует из дескриптора потом. Там возврат делает
		 * vaReleaseBufferHandle.
		 */
		if (bi->second.exported_fd >= 0)
			close(bi->second.exported_fd);
		else
			release_frame(d, bi->second.from_surface);
		if (bi->second.map) {
			if (bi->second.sync_fd >= 0)
				dmabuf_sync(bi->second.sync_fd, false);
			munmap(bi->second.map, bi->second.map_size);
		}
		d->buffers.erase(bi);
	}
	d->images.erase(ii);
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Картинка                                                            */
/* ------------------------------------------------------------------ */

VAStatus BeginPicture(VADriverContextP ctx, VAContextID id, VASurfaceID target)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->contexts.find(id);

	if (it == d->contexts.end())
		return VA_STATUS_ERROR_INVALID_CONTEXT;
	if (!d->surfaces.count(target))
		return VA_STATUS_ERROR_INVALID_SURFACE;

	it->second.current = Picture();
	it->second.current.target = target;
	return VA_STATUS_SUCCESS;
}

/*
 * Буферы картинки H.265. Описания срезов приходят раньше их байтов, поэтому
 * копятся в pending_slices и получают байты из ближайшего буфера данных.
 * Стартовый код, если клиент его приписал, снимаем: h265.cpp ждёт голый NAL.
 */
static VAStatus render_hevc(Driver *d, HevcPicture &p, VABufferID *list, int num)
{
	for (int i = 0; i < num; i++) {
		auto bi = d->buffers.find(list[i]);

		if (bi == d->buffers.end())
			return VA_STATUS_ERROR_INVALID_BUFFER;

		const Buffer &b = bi->second;

		switch (b.type) {
		case VAPictureParameterBufferType:
			if (b.data.size() < sizeof p.params)
				return VA_STATUS_ERROR_INVALID_BUFFER;
			memcpy(&p.params, b.data.data(), sizeof p.params);
			p.has_params = true;
			break;
		case VAIQMatrixBufferType:
			if (b.data.size() < sizeof p.iq)
				return VA_STATUS_ERROR_INVALID_BUFFER;
			memcpy(&p.iq, b.data.data(), sizeof p.iq);
			p.has_iq = true;
			break;
		case VASliceParameterBufferType:
			/* элемент может быть больше (…HEVCExtension у GStreamer) */
			if (b.size < sizeof(VASliceParameterBufferHEVC))
				return VA_STATUS_ERROR_INVALID_BUFFER;
			for (unsigned k = 0; k < b.count; k++) {
				VASliceParameterBufferHEVC sp;
				size_t off = (size_t)k * b.size;

				if (off + sizeof sp > b.data.size())
					break;
				memcpy(&sp, b.data.data() + off, sizeof sp);
				p.pending_slices.push_back(sp);
			}
			break;
		case VASliceDataBufferType:
			for (const auto &sp : p.pending_slices) {
				size_t off = sp.slice_data_offset, len = sp.slice_data_size;

				if (sp.slice_data_flag != VA_SLICE_DATA_FLAG_ALL)
					return VA_STATUS_ERROR_UNIMPLEMENTED;
				if (off > b.data.size() || len > b.data.size() - off)
					return VA_STATUS_ERROR_INVALID_BUFFER;

				HevcSlice s;

				s.params = sp;
				s.bytes.assign(b.data.begin() + off, b.data.begin() + off + len);
				if (s.bytes.size() >= 4 && !s.bytes[0] && !s.bytes[1]) {
					unsigned prefix = s.bytes[2] == 1 ? 3 :
						(!s.bytes[2] && s.bytes[3] == 1) ? 4 : 0;

					s.bytes.erase(s.bytes.begin(), s.bytes.begin() + prefix);
				}
				p.slices.push_back(std::move(s));
			}
			p.pending_slices.clear();
			break;
		default:
			break;
		}
	}
	return VA_STATUS_SUCCESS;
}

VAStatus RenderPicture(VADriverContextP ctx, VAContextID id, VABufferID *list,
		       int num)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto ci = d->contexts.find(id);

	if (ci == d->contexts.end())
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	Picture &p = ci->second.current;

	if (is_hevc(ci->second.profile))
		return render_hevc(d, p.hevc, list, num);

	for (int i = 0; i < num; i++) {
		auto bi = d->buffers.find(list[i]);

		if (bi == d->buffers.end())
			return VA_STATUS_ERROR_INVALID_BUFFER;

		const Buffer &b = bi->second;

		switch (b.type) {
		case VAPictureParameterBufferType:
			if (b.data.size() >= sizeof p.pic) {
				memcpy(&p.pic, b.data.data(), sizeof p.pic);
				p.have_pic_param = true;
			}
			break;
		case VAIQMatrixBufferType:
			if (b.data.size() >= sizeof p.iq) {
				memcpy(&p.iq, b.data.data(), sizeof p.iq);
				p.have_iq = true;
			}
			break;
		case VASliceParameterBufferType:
			/*
			 * Описание слайса приходит раньше его байтов, поэтому
			 * заводим запись заранее, а данные допишем следом.
			 */
			for (unsigned k = 0; k < b.count; k++) {
				VASliceParameterBufferH264 sp;
				size_t off = (size_t)k * b.size;

				if (off + sizeof sp > b.data.size())
					break;
				memcpy(&sp, b.data.data() + off, sizeof sp);
				p.slices.emplace_back(sp, std::vector<uint8_t>());
			}
			break;
		case VASliceDataBufferType:
			/*
			 * Байты всех слайсов картинки лежат одним куском;
			 * где чей — сказано в описании (slice_data_offset).
			 */
			for (auto &s : p.slices) {
				size_t off = s.first.slice_data_offset;
				size_t len = s.first.slice_data_size;

				if (!s.second.empty())
					continue;
				if (off + len > b.data.size())
					continue;
				s.second.assign(b.data.begin() + off,
						b.data.begin() + off + len);
			}
			break;
		default:
			break;	/* прочее нам не нужно */
		}
	}
	return VA_STATUS_SUCCESS;
}

VAStatus EndPicture(VADriverContextP ctx, VAContextID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto ci = d->contexts.find(id);

	if (ci == d->contexts.end())
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	Context &c = ci->second;
	Picture &p = c.current;

	H264Stream au;
	bool hevc = is_hevc(c.profile);

	if (hevc && c.eos_sent) {
		/*
		 * После конца потока (flush_held) декодер новых картинок не
		 * берёт — открываем заново. Сначала забираем всё, что он
		 * выпустил: иначе ответ на OPEN смешается с кадрами.
		 */
		collect(d, c, true);
		for (auto &kv : c.inflight) {
			auto si = d->surfaces.find(kv.second);

			if (si != d->surfaces.end())
				si->second.pending = false;
		}
		c.inflight.clear();
		preserve_frames(d, c);
		if (!daemon_open(c.sock, BF7V_CODEC_H265, c.width, c.height)) {
			TRACE("H.265: декодер не открылся заново");
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}
		c.h265 = HevcState();	/* наборы параметров — заново, с IRAP */
		c.eos_sent = false;
		TRACE("H.265: декодер открыт заново после конца потока");
	}
	if (hevc) {
		H265Stream h5;
		VAStatus st;

		if (!h265_access_unit(c.profile, p.hevc, c.h265, h5, st)) {
			TRACE("картинка H.265 не собралась: срезов %zu", p.hevc.slices.size());
			return st;
		}
		au.config = std::move(h5.config);
		au.picture = std::move(h5.picture);
	} else {
		if (!p.have_pic_param || p.slices.empty()) {
			TRACE("картинка без параметров или без слайсов — пропускаю");
			return VA_STATUS_ERROR_INVALID_PARAMETER;
		}
		au = h264_access_unit(c.profile, p, c.h264);
	}

	if (au.picture.empty()) {
		TRACE("картинка не собралась: слайсов %zu", p.slices.size());
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	/*
	 * Метка времени. Внутри драйвера это просто ярлык: по нему пришедший
	 * кадр возвращается в ту поверхность, которую просил клиент. Но
	 * компонент MediaTek по этой же метке опознаёт свои рабочие единицы,
	 * и двух одинаковых меток он не переживает: на столкновении он
	 * молча сдвигает метку на единицу, и кадр возвращается под чужим
	 * номером — «Unknown output workId». Поэтому метка своя у каждой
	 * посылки, включая наборы параметров, и растёт она обычным кадровым
	 * интервалом, чтобы выглядеть правдоподобным временем показа.
	 */

	/*
	 * Наборы параметров уходят порознь, по одному NAL на посылку: компонент
	 * разбирает такую посылку целиком как одну единицу настройки и второй
	 * набор в ней попросту теряет.
	 */
	/*
	 * H.265 — наоборот: VPS, SPS и PPS одной посылкой. Порознь компонент
	 * пытается открыть декодер по первому же набору (одному VPS), падает
	 * и оправляется лишь со следующими (проверено bf7-vtest, 2026-09-25).
	 */
	if (hevc && !au.config.empty()) {
		if (!daemon_queue(c.sock, au.config.data(), au.config.size(),
				  ++c.next_pts * 33333, BF7V_FLAG_CODEC_CONFIG)) {
			TRACE("демон не принял наборы параметров H.265");
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}
	}
	for (size_t i = 0; !hevc && i < au.config.size(); ) {
		size_t j = i + 4;

		while (j + 3 < au.config.size() &&
		       !(au.config[j] == 0 && au.config[j + 1] == 0 &&
			 au.config[j + 2] == 0 && au.config[j + 3] == 1))
			j++;
		if (j + 3 >= au.config.size())
			j = au.config.size();
		if (!daemon_queue(c.sock, au.config.data() + i, j - i,
				  ++c.next_pts * 33333,
				  BF7V_FLAG_CODEC_CONFIG)) {
			TRACE("демон не принял набор параметров");
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}
		i = j;
	}

	int64_t pts = ++c.next_pts * 33333;

	/*
	 * Отладка: сохранить собранный поток, чтобы проверить его отдельно
	 * уже проверенной пробой декодера.
	 */
	if (const char *path = getenv("BF7VA_DUMP")) {
		FILE *f = fopen(path, "ab");

		if (f) {
			fwrite(au.config.data(), 1, au.config.size(), f);
			fwrite(au.picture.data(), 1, au.picture.size(), f);
			fclose(f);
		}
	}

	/*
	 * Просить декодер сбрасывать конвейер после каждой картинки заманчиво
	 * (кадр отдаётся раньше), но дорого: MediaTek на это тратит заметную
	 * долю процессора и ощутимо замедляет проигрывание. Кадры и так
	 * забираются просьбой о выдаче. Переменная оставлена на случай, когда
	 * задержка важнее скорости.
	 */
	static const uint32_t drain_each =
		getenv("BF7VA_DRAIN_EACH") ? BF7V_FLAG_DRAIN : 0;

	if (!daemon_queue(c.sock, au.picture.data(), au.picture.size(), pts,
			  drain_each)) {
		TRACE("демон не принял единицу доступа");
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}
	c.inflight[pts] = p.target;

	auto si = d->surfaces.find(p.target);

	if (si != d->surfaces.end()) {
		si->second.pending = true;
		si->second.ready = false;
		si->second.pts = pts;
	}
	TRACE("картинка -> поверхность %u, метка %lld, настройка %zu, картинка %zu байт, слайсов %zu",
	      p.target, (long long)pts, au.config.size(), au.picture.size(),
	      p.slices.size());

	/*
	 * Поверхность вывезена своим буфером (GStreamer, vah264dec): клиент
	 * отдаст её на показ сразу после vaEndPicture и ни vaSyncSurface, ни
	 * vaDeriveImage звать не станет — кадр некому забрать у демона.
	 * Ждём его здесь, и copy_to_own кладёт его в вывезенный буфер.
	 */
	/*
	 * Для H.265 не ждём: декодер держит кадр до следующей картинки
	 * (flush_held), и ожидание здесь уводило бы поток в конец на каждом
	 * кадре. Поэтому ранг vah265dec в сеансе и не поднят.
	 */
	if (si != d->surfaces.end() && si->second.own_exported && !hevc)
		ensure_decoded(d, p.target);
	return VA_STATUS_SUCCESS;
}

/*
 * Переложить пришедший кадр в свой буфер поверхности (см. own_exported).
 *
 * Раскладки у обоих одна — gralloc, тот же формат и размер, — но шаг
 * берём у каждого свой. Кадр 720p в YV12 — 1,4 МБ, это ~1 мс процессора:
 * несравнимо дешевле программного videoconvert, который иначе был бы нужен.
 */
static void dmabuf_sync_rw(int fd, bool start)
{
	struct bf7_dma_buf_sync s;

	s.flags = 3ull /* READ | WRITE */ |
		  (start ? BF7_DMA_BUF_SYNC_START : BF7_DMA_BUF_SYNC_END);
	(void)ioctl(fd, BF7_DMA_BUF_IOCTL_SYNC, &s);
}

static bool copy_to_own(Surface &s)
{
	if (!s.own_exported || s.own_fd < 0 || !s.ready || s.dma_fd < 0)
		return false;

	bool rgb = s.fourcc == VA_FOURCC_RGBA;
	uint32_t own_fourcc = rgb_mode() ? VA_FOURCC_RGBA : VA_FOURCC_YV12;

	if (s.fourcc != own_fourcc) {
		TRACE("поверхность: кадр %.4s, свой буфер %.4s — не копирую",
		      (const char *)&s.fourcc, (const char *)&own_fourcc);
		return false;
	}

	Layout src, dst;

	if (rgb) {
		src.planes = dst.planes = 1;
		src.pitch[0] = s.stride * 4;
		dst.pitch[0] = s.own_stride * 4;
		src.size = src.pitch[0] * s.height;
		dst.size = dst.pitch[0] * s.height;
	} else if (!layout_of(s.fourcc, s.stride, s.width, s.height, &src) ||
		   !layout_of(own_fourcc, s.own_stride, s.width, s.height, &dst)) {
		return false;
	}

	if (!s.own_map) {
		off_t whole = lseek(s.own_fd, 0, SEEK_END);

		if (whole <= 0 || (size_t)whole < dst.size)
			return false;
		void *m = mmap(nullptr, (size_t)whole, PROT_READ | PROT_WRITE,
			       MAP_SHARED, s.own_fd, 0);
		if (m == MAP_FAILED) {
			TRACE("свой буфер не отображается: %s", strerror(errno));
			return false;
		}
		s.own_map = m;
		s.own_map_size = (size_t)whole;
	}

	off_t whole = lseek(s.dma_fd, 0, SEEK_END);

	if (whole <= 0 || (size_t)whole < src.size)
		return false;
	uint8_t *from = (uint8_t *)mmap(nullptr, (size_t)whole, PROT_READ,
					MAP_SHARED, s.dma_fd, 0);
	if (from == MAP_FAILED)
		return false;

	uint8_t *to = (uint8_t *)s.own_map;

	dmabuf_sync(s.dma_fd, true);
	dmabuf_sync_rw(s.own_fd, true);
	for (unsigned pl = 0; pl < src.planes; pl++) {
		unsigned rows = pl ? s.height / 2 : s.height;
		unsigned bytes = rgb ? s.width * 4 : (pl ? s.width / 2 : s.width);

		if (src.pitch[pl] == dst.pitch[pl]) {
			memcpy(to + dst.off[pl], from + src.off[pl],
			       (size_t)src.pitch[pl] * rows);
			continue;
		}
		for (unsigned r = 0; r < rows; r++)
			memcpy(to + dst.off[pl] + (size_t)r * dst.pitch[pl],
			       from + src.off[pl] + (size_t)r * src.pitch[pl],
			       bytes);
	}
	dmabuf_sync_rw(s.own_fd, false);
	dmabuf_sync(s.dma_fd, false);
	munmap(from, (size_t)whole);
	return true;
}

/* ------------------------------------------------------------------ */
/* Ожидание кадра                                                      */
/* ------------------------------------------------------------------ */

/*
 * Забрать у демона всё, что готово, и разложить по поверхностям.
 *
 * Демон отдаёт накопленное только перед тем, как прочитать очередную просьбу,
 * а всё остальное время спит на сокете. Поэтому просто ждать кадра бесполезно:
 * надо попросить выдачу, и тогда он сначала пришлёт кадры, а в конце — ответ,
 * по которому и видно, что больше пока ничего нет.
 */
static void collect(Driver *d, Context &c, bool ask)
{
	int timeout_ms = 0;

	if (ask) {
		daemon_drain(c.sock);
		timeout_ms = 2000;
	}

	for (;;) {
		int64_t pts = 0;
		int fd = -1;
		unsigned w = 0, h = 0, stride = 0;
		uint32_t fmt = 0;
		uint64_t handle = 0;
		int r = daemon_poll_frame(c.sock, timeout_ms, &pts, &fd, &w, &h,
					  &stride, &fmt, &handle);

		if (r < 0) {
			TRACE("опрос демона -> связь оборвалась");
			return;
		}
		if (r == 0)	/* не дождались */
			return;
		if (r == 2)	/* демон отчитался: готового больше нет */
			return;

		auto ii = c.inflight.find(pts);

		if (ii == c.inflight.end()) {
			TRACE("кадр с меткой %lld ничей", (long long)pts);
			close(fd);
			daemon_release(c.sock, handle);
			continue;
		}

		auto si = d->surfaces.find(ii->second);

		if (si == d->surfaces.end()) {
			close(fd);
			daemon_release(c.sock, handle);
		} else {
			Surface &s = si->second;

			/*
			 * Прежний кадр этой поверхности возвращаем демону.
			 * Без этого он держит буфер декодера у себя навсегда:
			 * пул кончается, декодер перестаёт выдавать кадры, и
			 * проигрыватель видит это как «кадр не пришёл» и уходит
			 * на процессор — примерно после сотни кадров.
			 */
			if (s.dma_fd >= 0)
				close(s.dma_fd);
			if (s.frame_handle)
				daemon_release(c.sock, s.frame_handle);
			s.dma_fd = fd;
			s.width = w;
			s.height = h;
			s.stride = stride;
			s.fourcc = fourcc_of_frame(fmt);
			s.frame_handle = handle;
			s.pending = false;
			s.ready = true;
			if (copy_to_own(s)) {
				/*
				 * Кадр уже в своём буфере — буфер декодера
				 * сразу обратно демону, иначе пул в 12
				 * кадров кончится на поверхностях GStreamer.
				 */
				close(s.dma_fd);
				s.dma_fd = -1;
				daemon_release(c.sock, s.frame_handle);
				s.frame_handle = 0;
			}
			TRACE("поверхность %u готова: %ux%u шаг %u",
			      ii->second, w, h, stride);
		}
		c.inflight.erase(ii);
	}
}

VAStatus SyncSurface(VADriverContextP ctx, VASurfaceID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto si = d->surfaces.find(id);

	if (si == d->surfaces.end())
		return VA_STATUS_ERROR_INVALID_SURFACE;
	if (si->second.ready || !si->second.pending)
		return VA_STATUS_SUCCESS;

	/*
	 * Ждём именно этот кадр. Первый обходится дорого: декодер на этом
	 * месте заводит пул буферов, поднимает gralloc и преобразователь
		 * цвета — секунды. Дальше кадры идут быстро, но потолок ожидания всё
	 * равно нужен: лучше честно вернуть отказ, чем повиснуть навсегда.
	 */
	TRACE("жду поверхность %u", id);
	for (auto &kv : d->contexts)
		collect(d, kv.second, false);
	for (int tries = 0; tries < 20 && si->second.pending; tries++) {
		if (tries == 3)
			for (auto &kv : d->contexts)
				flush_held(kv.second);
		for (auto &kv : d->contexts)
			collect(d, kv.second, true);
	}

	TRACE("ожидание поверхности %u закончено: %s", id,
	      si->second.ready ? "готова" : "не дождались");
	return si->second.ready ? VA_STATUS_SUCCESS
				: VA_STATUS_ERROR_OPERATION_FAILED;
}

VAStatus SyncSurface2(VADriverContextP ctx, VASurfaceID id, uint64_t timeout_ns)
{
	UNUSED(timeout_ns);
	return SyncSurface(ctx, id);
}

VAStatus QuerySurfaceStatus(VADriverContextP ctx, VASurfaceID id,
			    VASurfaceStatus *status)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto si = d->surfaces.find(id);

	if (si == d->surfaces.end())
		return VA_STATUS_ERROR_INVALID_SURFACE;

	for (auto &kv : d->contexts)
		collect(d, kv.second, true);

	*status = si->second.ready ? VASurfaceReady :
		  si->second.pending ? VASurfaceRendering : VASurfaceReady;
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Выдача кадра наружу                                                 */
/* ------------------------------------------------------------------ */

/*
 * Одним слоем или по плоскостям.
 *
 * Просящий обычно говорит это флагом. Но ffmpeg — а через него и Firefox —
 * всегда просит SEPARATE_LAYERS, то есть каждую плоскость отдельно, и
 * получает отказ: PowerVR однобайтовые плоскости (R8) ввозить не умеет, в
 * списке eglQueryDmaBufFormatsEXT их нет. Целый кадр YV12 он ввозит
 * прекрасно. Переменной BF7VA_COMPOSED можно заставить отдавать одним слоем
 * даже тому, кто просил иначе.
 */
static bool composed_layers(uint32_t flags)
{
	if (flags & VA_EXPORT_SURFACE_COMPOSED_LAYERS)
		return true;
	return getenv("BF7VA_COMPOSED") != nullptr;
}

VAStatus ExportSurfaceHandle(VADriverContextP ctx, VASurfaceID id,
			     uint32_t mem_type, uint32_t flags, void *descriptor)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto si = d->surfaces.find(id);

	if (si == d->surfaces.end())
		return VA_STATUS_ERROR_INVALID_SURFACE;
	if (mem_type != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
		return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
	Surface &s = si->second;

	/*
	 * До первого кадра отдаём свой буфер. Так проигрыватель может
	 * проверить формат сразу после создания поверхности — иначе он решит,
	 * что аппаратный декодер ничего не умеет, и уйдёт на процессор.
	 */
	int fd = s.ready ? s.dma_fd : -1;
	unsigned stride = s.stride;

	if (fd < 0) {
		/*
		 * Буфера нет и завести нечем — например, клиента заперли в
		 * песочницу и андроидной части оттуда не видно. Сказать надо
		 * именно «не вышло выделить»: получатель по этому коду уходит
		 * на программное декодирование, а не считает поверхность
		 * временно занятой и не долбится в неё снова.
		 */
		if (!ensure_backing(s)) {
			TRACE("поверхность %u вывезти нечем: буфера нет", id);
			return VA_STATUS_ERROR_ALLOCATION_FAILED;
		}
		fd = s.own_fd;
		stride = s.own_stride;
		s.own_exported = true;
	} else {
		s.own_exported = false;	/* вывозит каждый кадр — копия не нужна */
	}

	VADRMPRIMESurfaceDescriptor *dsc =
		(VADRMPRIMESurfaceDescriptor *)descriptor;

	bool rgb = s.ready ? (s.fourcc == VA_FOURCC_RGBA) : rgb_mode();

	if (rgb) {
		/*
		 * Один слой, одна плоскость. AB24 — это DRM_FORMAT_ABGR8888,
		 * то есть байты R, G, B, A по порядку; именно так лежит
		 * HAL_PIXEL_FORMAT_RGBA_8888 у gralloc.
		 */
		memset(dsc, 0, sizeof *dsc);
		dsc->fourcc = VA_FOURCC_RGBA;
		dsc->width = s.width;
		dsc->height = s.height;
		dsc->num_objects = 1;
		dsc->objects[0].fd = dup(fd);
		dsc->objects[0].size = (uint32_t)((size_t)stride * 4 * s.height);
		dsc->objects[0].drm_format_modifier = 0;
		dsc->num_layers = 1;
		dsc->layers[0].drm_format = 0x34324241;	/* AB24 */
		dsc->layers[0].num_planes = 1;
		dsc->layers[0].object_index[0] = 0;
		dsc->layers[0].offset[0] = 0;
		dsc->layers[0].pitch[0] = stride * 4;
		TRACE("выдана поверхность %u: %ux%u RGBA шаг %u, %s", id,
		      s.width, s.height, stride * 4,
		      s.ready ? "кадр декодера" : "свой буфер");
		return VA_STATUS_SUCCESS;
	}

	/* У gralloc шаг цветности выравнивается до 16 байт, а не просто вдвое. */
	unsigned cstride = ((stride / 2) + 15) & ~15u;
	size_t ysz = (size_t)stride * s.height;
	size_t csz = (size_t)cstride * (s.height / 2);

	memset(dsc, 0, sizeof *dsc);
	dsc->width = s.width;
	dsc->height = s.height;
	dsc->num_objects = 1;
	dsc->objects[0].fd = dup(fd);	/* закрывать будет получатель */
	dsc->objects[0].size = (uint32_t)(ysz + 2 * csz);
	dsc->objects[0].drm_format_modifier = 0;	/* линейный */

	/*
	 * Три плоскости — тремя отдельными слоями, каждый однобайтовый (R8).
	 *
	 * Соблазнительно описать всё одним слоем с форматом YV12, но получатели
	 * так не умеют: mpv на такое отвечает «Cannot map unknown multi-plane
	 * format», потому что каждую плоскость он втягивает в EGL отдельной
	 * картинкой. Так же поступают и драйверы Mesa.
	 *
	 * Объявляем YV12 — яркость, Cr, Cb, — то есть ровно то, что лежит в
	 * буфере. Раньше тут стоял I420 с переставленными смещениями: данные
	 * были верны, но Firefox такой код просто не принимает и молча уходит
	 * на процессор («Unsupported VA-API surface format 808596553» в его
	 * журнале Dmabuf, 808596553 — это «I420»). Заодно теперь совпадает со
	 * всем остальным, что мы объявляем: и QuerySurfaceAttributes, и
	 * QueryImageFormats отвечают YV12.
	 */
	dsc->fourcc = VA_FOURCC_YV12;

	if (composed_layers(flags)) {
		/*
		 * Всё одним слоем. Так просит тот, кто ввозит кадр целиком —
		 * и только так умеет PowerVR: формата R8 в его списке
		 * eglQueryDmaBufFormatsEXT нет вовсе, а YV12 есть.
		 */
		dsc->num_layers = 1;
		dsc->layers[0].drm_format = 0x32315659;	/* YV12 */
		dsc->layers[0].num_planes = 3;
		for (unsigned i = 0; i < 3; i++)
			dsc->layers[0].object_index[i] = 0;
		dsc->layers[0].offset[0] = 0;
		dsc->layers[0].pitch[0] = stride;
		dsc->layers[0].offset[1] = (uint32_t)ysz;		/* Cr */
		dsc->layers[0].pitch[1] = cstride;
		dsc->layers[0].offset[2] = (uint32_t)(ysz + csz);	/* Cb */
		dsc->layers[0].pitch[2] = cstride;
	} else {
		dsc->num_layers = 3;
		for (unsigned i = 0; i < 3; i++) {
			dsc->layers[i].drm_format = 0x20203852;	/* R8 */
			dsc->layers[i].num_planes = 1;
			dsc->layers[i].object_index[0] = 0;
		}
		dsc->layers[0].offset[0] = 0;
		dsc->layers[0].pitch[0] = stride;
		dsc->layers[1].offset[0] = (uint32_t)ysz;		/* Cr */
		dsc->layers[1].pitch[0] = cstride;
		dsc->layers[2].offset[0] = (uint32_t)(ysz + csz);	/* Cb */
		dsc->layers[2].pitch[0] = cstride;
	}

	TRACE("выдана поверхность %u: %ux%u шаг %u, %s", id, s.width, s.height,
	      stride, s.ready ? "кадр декодера" : "свой буфер");
	return VA_STATUS_SUCCESS;
}

} /* namespace bf7va */
