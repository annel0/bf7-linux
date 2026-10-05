/*
 * BF7: таблица драйвера VA-API.
 *
 * libva не примет драйвер, у которого хоть один из обязательных указателей
 * нулевой, — проверка идёт при загрузке и молча отвергает библиотеку целиком.
 * Поэтому здесь заполнено всё, а то, что нам не нужно (подкартинки, палитры,
 * атрибуты экрана), отвечает "не умею" уже при вызове.
 *
 * Настоящей работы требует немногое: создание конфигурации и контекста, сбор
 * буферов между vaBeginPicture и vaEndPicture, ожидание кадра и выдача его
 * наружу дескриптором dma-buf.
 */
#include "bf7va.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <va/va_drmcommon.h>

using namespace bf7va;

/* Объявлено в decode.cpp; нужно раньше, чем уничтожаются поверхности. */
namespace bf7va {
void release_backing(Surface &s);
bool rgb_mode(void);
}

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
/* Возможности                                                         */
/* ------------------------------------------------------------------ */

static const VAProfile kProfiles[] = {
	VAProfileH264ConstrainedBaseline,
	VAProfileH264Main,
	VAProfileH264High,
	/*
	 * H.265 Main (8 бит 4:2:0). Main10 не объявляем: компонент MediaTek его
	 * не открывает («Fail to open Vdec Drv H265 instance», 2026-09-25) —
	 * клиент, выбрав железо, получил бы отказ вместо программного пути.
	 */
	VAProfileHEVCMain,
};

static VAStatus bf7_QueryConfigProfiles(VADriverContextP ctx,
					VAProfile *list, int *num)
{
	UNUSED(ctx);
	*num = (int)(sizeof kProfiles / sizeof kProfiles[0]);
	memcpy(list, kProfiles, sizeof kProfiles);
	return VA_STATUS_SUCCESS;
}

static bool profile_known(VAProfile p)
{
	for (VAProfile k : kProfiles)
		if (k == p)
			return true;
	return false;
}

static VAStatus bf7_QueryConfigEntrypoints(VADriverContextP ctx, VAProfile p,
					   VAEntrypoint *list, int *num)
{
	UNUSED(ctx);
	if (!profile_known(p))
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	list[0] = VAEntrypointVLD;	/* только разбор потока, кодирования нет */
	*num = 1;
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_GetConfigAttributes(VADriverContextP ctx, VAProfile p,
					VAEntrypoint e, VAConfigAttrib *attrs,
					int num)
{
	UNUSED(ctx);
	if (!profile_known(p))
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	if (e != VAEntrypointVLD)
		return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;

	for (int i = 0; i < num; i++) {
		switch (attrs[i].type) {
		case VAConfigAttribRTFormat:
			/*
			 * В режиме без копий кадр приходит уже в RGBA, и
			 * клиент (через ffmpeg) не станет заводить такие
			 * поверхности, пока мы не признаем, что умеем RGB32.
			 */
			attrs[i].value = VA_RT_FORMAT_YUV420;
			if (bf7va::rgb_mode())
				attrs[i].value |= VA_RT_FORMAT_RGB32;
			break;
		case VAConfigAttribMaxPictureWidth:
			attrs[i].value = 1920;
			break;
		case VAConfigAttribMaxPictureHeight:
			attrs[i].value = 1088;
			break;
		default:
			attrs[i].value = VA_ATTRIB_NOT_SUPPORTED;
			break;
		}
	}
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_CreateConfig(VADriverContextP ctx, VAProfile p,
				 VAEntrypoint e, VAConfigAttrib *attrs,
				 int num, VAConfigID *out)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	Config c;

	if (!profile_known(p))
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
	if (e != VAEntrypointVLD)
		return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;

	c.profile = p;
	c.entrypoint = e;
	for (int i = 0; i < num; i++)
		if (attrs[i].type == VAConfigAttribRTFormat)
			c.rt_format = attrs[i].value;

	*out = (VAConfigID)d->next_id++;
	d->configs[*out] = c;
	TRACE("конфигурация %u: профиль %d", *out, (int)p);
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_DestroyConfig(VADriverContextP ctx, VAConfigID id)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);

	return d->configs.erase(id) ? VA_STATUS_SUCCESS
				    : VA_STATUS_ERROR_INVALID_CONFIG;
}

static VAStatus bf7_QueryConfigAttributes(VADriverContextP ctx, VAConfigID id,
					  VAProfile *p, VAEntrypoint *e,
					  VAConfigAttrib *attrs, int *num)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);
	auto it = d->configs.find(id);

	if (it == d->configs.end())
		return VA_STATUS_ERROR_INVALID_CONFIG;
	*p = it->second.profile;
	*e = it->second.entrypoint;
	if (attrs && num) {
		attrs[0].type = VAConfigAttribRTFormat;
		attrs[0].value = it->second.rt_format;
		*num = 1;
	}
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Поверхности                                                         */
/* ------------------------------------------------------------------ */

static VAStatus create_surfaces(Driver *d, unsigned w, unsigned h,
				VASurfaceID *out, unsigned num)
{
	std::lock_guard<std::mutex> g(d->lock);

	for (unsigned i = 0; i < num; i++) {
		Surface s;

		s.width = w;
		s.height = h;
		out[i] = (VASurfaceID)d->next_id++;
		d->surfaces[out[i]] = s;
	}
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_CreateSurfaces(VADriverContextP ctx, int w, int h,
				   int format, int num, VASurfaceID *out)
{
	UNUSED(format);
	return create_surfaces(drv(ctx), (unsigned)w, (unsigned)h, out,
			       (unsigned)num);
}

static VAStatus bf7_CreateSurfaces2(VADriverContextP ctx, unsigned format,
				    unsigned w, unsigned h, VASurfaceID *out,
				    unsigned num, VASurfaceAttrib *attrs,
				    unsigned num_attrs)
{
	UNUSED(format); UNUSED(attrs); UNUSED(num_attrs);
	return create_surfaces(drv(ctx), w, h, out, num);
}

static VAStatus bf7_DestroySurfaces(VADriverContextP ctx, VASurfaceID *list,
				    int num)
{
	Driver *d = drv(ctx);
	std::lock_guard<std::mutex> g(d->lock);

	for (int i = 0; i < num; i++) {
		auto it = d->surfaces.find(list[i]);

		if (it == d->surfaces.end())
			continue;
		if (it->second.dma_fd >= 0)
			close(it->second.dma_fd);
		bf7va::release_backing(it->second);
		d->surfaces.erase(it);
	}
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_QuerySurfaceAttributes(VADriverContextP ctx, VAConfigID cfg,
					   VASurfaceAttrib *attrs,
					   unsigned int *num)
{
	UNUSED(ctx); UNUSED(cfg);
	if (!attrs) {
		*num = 2;
		return VA_STATUS_SUCCESS;
	}
	if (*num < 2)
		return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;

	attrs[0].type = VASurfaceAttribPixelFormat;
	attrs[0].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
	attrs[0].value.type = VAGenericValueTypeInteger;
	/*
	 * В режиме без копий декодер отдаёт кадр уже в RGBA: преобразование
	 * делает MDP, то есть железо, зато кадр остаётся одним слоем, и
	 * драйвер PowerVR может втянуть его дескриптором. Отдельные плоскости
	 * YUV он не принимает — в списке форматов EGL нет R8.
	 */
	/*
	 * Объявляем то, что декодер отдаёт на самом деле: YV12. Раньше тут
	 * значился NV12, и GStreamer, поверив, разбирал кадр по чужой
	 * раскладке — картинка выходила сдвоенной и в неверных цветах.
	 */
	attrs[0].value.value.i = bf7va::rgb_mode() ? VA_FOURCC_RGBA
						   : VA_FOURCC_YV12;

	attrs[1].type = VASurfaceAttribMemoryType;
	attrs[1].flags = VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE;
	attrs[1].value.type = VAGenericValueTypeInteger;
	attrs[1].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA |
				 VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
	*num = 2;
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Заглушки: libva требует, чтобы указатели были заполнены             */
/* ------------------------------------------------------------------ */

#define STUB(name, args) static VAStatus name args { \
	return VA_STATUS_ERROR_UNIMPLEMENTED; }

STUB(bf7_QuerySurfaceError, (VADriverContextP, VASurfaceID, VAStatus, void **))
STUB(bf7_PutSurface, (VADriverContextP, VASurfaceID, void *, short, short,
		      unsigned short, unsigned short, short, short,
		      unsigned short, unsigned short, VARectangle *,
		      unsigned int, unsigned int))
STUB(bf7_SetImagePalette, (VADriverContextP, VAImageID, unsigned char *))
STUB(bf7_PutImage, (VADriverContextP, VASurfaceID, VAImageID, int, int,
		    unsigned int, unsigned int, int, int, unsigned int,
		    unsigned int))
STUB(bf7_CreateSubpicture, (VADriverContextP, VAImageID, VASubpictureID *))
STUB(bf7_DestroySubpicture, (VADriverContextP, VASubpictureID))
STUB(bf7_SetSubpictureImage, (VADriverContextP, VASubpictureID, VAImageID))
STUB(bf7_SetSubpictureChromakey, (VADriverContextP, VASubpictureID,
				  unsigned int, unsigned int, unsigned int))
STUB(bf7_SetSubpictureGlobalAlpha, (VADriverContextP, VASubpictureID, float))
STUB(bf7_AssociateSubpicture, (VADriverContextP, VASubpictureID, VASurfaceID *,
			       int, short, short, unsigned short, unsigned short,
			       short, short, unsigned short, unsigned short,
			       unsigned int))
STUB(bf7_DeassociateSubpicture, (VADriverContextP, VASubpictureID,
				 VASurfaceID *, int))
STUB(bf7_GetDisplayAttributes, (VADriverContextP, VADisplayAttribute *, int))
STUB(bf7_SetDisplayAttributes, (VADriverContextP, VADisplayAttribute *, int))
STUB(bf7_BufferInfo, (VADriverContextP, VABufferID, VABufferType *,
		      unsigned int *, unsigned int *))
STUB(bf7_LockSurface, (VADriverContextP, VASurfaceID, unsigned int *,
		       unsigned int *, unsigned int *, unsigned int *,
		       unsigned int *, unsigned int *, unsigned int *,
		       unsigned int *, void **))
STUB(bf7_UnlockSurface, (VADriverContextP, VASurfaceID))
STUB(bf7_GetSurfaceAttributes, (VADriverContextP, VAConfigID,
				VASurfaceAttrib *, unsigned int))
STUB(bf7_CreateMFContext, (VADriverContextP, VAMFContextID *))
STUB(bf7_MFAddContext, (VADriverContextP, VAMFContextID, VAContextID))
STUB(bf7_MFReleaseContext, (VADriverContextP, VAMFContextID, VAContextID))
STUB(bf7_MFSubmit, (VADriverContextP, VAMFContextID, VAContextID *, int))
STUB(bf7_QueryProcessingRate, (VADriverContextP, VAConfigID,
			       VAProcessingRateParameter *, unsigned int *))
STUB(bf7_SyncBuffer, (VADriverContextP, VABufferID, uint64_t))
STUB(bf7_Copy, (VADriverContextP, VACopyObject *, VACopyObject *, VACopyOption))

static VAStatus bf7_QueryImageFormats(VADriverContextP ctx, VAImageFormat *list,
				      int *num)
{
	UNUSED(ctx);
	/*
	 * Объявляем те же форматы, в которых кадр и приходит от декодера.
	 * Читать их процессором мы не даём: наружу отдаётся dma-buf.
	 */
	memset(&list[0], 0, sizeof list[0]);
	memset(&list[1], 0, sizeof list[1]);
	if (bf7va::rgb_mode()) {
		list[0].fourcc = VA_FOURCC_RGBA;
		list[0].byte_order = VA_LSB_FIRST;
		list[0].bits_per_pixel = 32;
		list[0].depth = 24;
		list[0].red_mask = 0x000000ff;
		list[0].green_mask = 0x0000ff00;
		list[0].blue_mask = 0x00ff0000;
		list[0].alpha_mask = 0xff000000;
		*num = 1;
		return VA_STATUS_SUCCESS;
	}
	/* Первым — тот, в котором кадр приходит; его берут по умолчанию. */
	list[0].fourcc = VA_FOURCC_YV12;
	list[0].byte_order = VA_LSB_FIRST;
	list[0].bits_per_pixel = 12;
	list[1].fourcc = VA_FOURCC_NV12;
	list[1].byte_order = VA_LSB_FIRST;
	list[1].bits_per_pixel = 12;
	*num = 2;
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_QuerySubpictureFormats(VADriverContextP ctx,
					   VAImageFormat *list,
					   unsigned int *flags,
					   unsigned int *num)
{
	UNUSED(ctx); UNUSED(list); UNUSED(flags);
	*num = 0;
	return VA_STATUS_SUCCESS;
}

static VAStatus bf7_QueryDisplayAttributes(VADriverContextP ctx,
					   VADisplayAttribute *list, int *num)
{
	UNUSED(ctx); UNUSED(list);
	*num = 0;
	return VA_STATUS_SUCCESS;
}

/* Объявлены в decode.cpp */
namespace bf7va {
VAStatus CreateContext(VADriverContextP, VAConfigID, int, int, int,
		       VASurfaceID *, int, VAContextID *);
VAStatus DestroyContext(VADriverContextP, VAContextID);
VAStatus CreateBuffer(VADriverContextP, VAContextID, VABufferType,
		      unsigned int, unsigned int, void *, VABufferID *);
VAStatus BufferSetNumElements(VADriverContextP, VABufferID, unsigned int);
VAStatus CreateImage(VADriverContextP, VAImageFormat *, int, int, VAImage *);
VAStatus GetImage(VADriverContextP, VASurfaceID, int, int, unsigned int,
		  unsigned int, VAImageID);
VAStatus AcquireBufferHandle(VADriverContextP, VABufferID, VABufferInfo *);
VAStatus ReleaseBufferHandle(VADriverContextP, VABufferID);
VAStatus DeriveImage(VADriverContextP, VASurfaceID, VAImage *);
VAStatus DestroyImage(VADriverContextP, VAImageID);
VAStatus MapBuffer(VADriverContextP, VABufferID, void **);
VAStatus UnmapBuffer(VADriverContextP, VABufferID);
VAStatus DestroyBuffer(VADriverContextP, VABufferID);
VAStatus BeginPicture(VADriverContextP, VAContextID, VASurfaceID);
VAStatus RenderPicture(VADriverContextP, VAContextID, VABufferID *, int);
VAStatus EndPicture(VADriverContextP, VAContextID);
VAStatus SyncSurface(VADriverContextP, VASurfaceID);
VAStatus SyncSurface2(VADriverContextP, VASurfaceID, uint64_t);
VAStatus QuerySurfaceStatus(VADriverContextP, VASurfaceID, VASurfaceStatus *);
VAStatus ExportSurfaceHandle(VADriverContextP, VASurfaceID, uint32_t,
			     uint32_t, void *);
}

static VAStatus bf7_Terminate(VADriverContextP ctx)
{
	Driver *d = drv(ctx);

	TRACE("завершение");
	delete d;
	ctx->pDriverData = nullptr;
	return VA_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */

extern "C" VAStatus __vaDriverInit_1_17(VADriverContextP ctx)
{
	Driver *d = new Driver();
	struct VADriverVTable *v = ctx->vtable;

	ctx->version_major = VA_MAJOR_VERSION;
	ctx->version_minor = VA_MINOR_VERSION;
	ctx->max_profiles = (int)(sizeof kProfiles / sizeof kProfiles[0]);
	ctx->max_entrypoints = 1;
	ctx->max_attributes = 4;
	/*
	 * Счётчики обязаны быть ненулевыми: libva иначе отказывается
	 * принимать драйвер ("Failed to define max_image_formats in init").
	 * Сами форматы мы объявляем, но доступа к пикселям процессором не
	 * даём — кадр уходит наружу дескриптором dma-buf.
	 */
	ctx->max_image_formats = 2;
	ctx->max_subpic_formats = 1;
	ctx->max_display_attributes = 1;
	ctx->str_vendor = "BF7 hardware decoder (MediaTek Codec2)";
	ctx->pDriverData = d;

	v->vaTerminate = bf7_Terminate;
	v->vaQueryConfigProfiles = bf7_QueryConfigProfiles;
	v->vaQueryConfigEntrypoints = bf7_QueryConfigEntrypoints;
	v->vaGetConfigAttributes = bf7_GetConfigAttributes;
	v->vaCreateConfig = bf7_CreateConfig;
	v->vaDestroyConfig = bf7_DestroyConfig;
	v->vaQueryConfigAttributes = bf7_QueryConfigAttributes;
	v->vaCreateSurfaces = bf7_CreateSurfaces;
	v->vaCreateSurfaces2 = bf7_CreateSurfaces2;
	v->vaDestroySurfaces = bf7_DestroySurfaces;
	v->vaQuerySurfaceAttributes = bf7_QuerySurfaceAttributes;
	v->vaCreateContext = bf7va::CreateContext;
	v->vaDestroyContext = bf7va::DestroyContext;
	v->vaCreateBuffer = bf7va::CreateBuffer;
	v->vaBufferSetNumElements = bf7va::BufferSetNumElements;
	v->vaMapBuffer = bf7va::MapBuffer;
	v->vaUnmapBuffer = bf7va::UnmapBuffer;
	v->vaDestroyBuffer = bf7va::DestroyBuffer;
	v->vaBeginPicture = bf7va::BeginPicture;
	v->vaRenderPicture = bf7va::RenderPicture;
	v->vaEndPicture = bf7va::EndPicture;
	v->vaSyncSurface = bf7va::SyncSurface;
	v->vaSyncSurface2 = bf7va::SyncSurface2;
	v->vaQuerySurfaceStatus = bf7va::QuerySurfaceStatus;
	v->vaExportSurfaceHandle = bf7va::ExportSurfaceHandle;

	v->vaQuerySurfaceError = bf7_QuerySurfaceError;
	v->vaPutSurface = bf7_PutSurface;
	v->vaQueryImageFormats = bf7_QueryImageFormats;
	v->vaCreateImage = bf7va::CreateImage;
	v->vaDeriveImage = bf7va::DeriveImage;
	v->vaDestroyImage = bf7va::DestroyImage;
	v->vaSetImagePalette = bf7_SetImagePalette;
	v->vaGetImage = bf7va::GetImage;
	v->vaPutImage = bf7_PutImage;
	v->vaQuerySubpictureFormats = bf7_QuerySubpictureFormats;
	v->vaCreateSubpicture = bf7_CreateSubpicture;
	v->vaDestroySubpicture = bf7_DestroySubpicture;
	v->vaSetSubpictureImage = bf7_SetSubpictureImage;
	v->vaSetSubpictureChromakey = bf7_SetSubpictureChromakey;
	v->vaSetSubpictureGlobalAlpha = bf7_SetSubpictureGlobalAlpha;
	v->vaAssociateSubpicture = bf7_AssociateSubpicture;
	v->vaDeassociateSubpicture = bf7_DeassociateSubpicture;
	v->vaQueryDisplayAttributes = bf7_QueryDisplayAttributes;
	v->vaGetDisplayAttributes = bf7_GetDisplayAttributes;
	v->vaSetDisplayAttributes = bf7_SetDisplayAttributes;
	v->vaBufferInfo = bf7_BufferInfo;
	v->vaLockSurface = bf7_LockSurface;
	v->vaUnlockSurface = bf7_UnlockSurface;
	v->vaGetSurfaceAttributes = bf7_GetSurfaceAttributes;
	v->vaAcquireBufferHandle = bf7va::AcquireBufferHandle;
	v->vaReleaseBufferHandle = bf7va::ReleaseBufferHandle;
	v->vaCreateMFContext = bf7_CreateMFContext;
	v->vaMFAddContext = bf7_MFAddContext;
	v->vaMFReleaseContext = bf7_MFReleaseContext;
	v->vaMFSubmit = bf7_MFSubmit;
	v->vaQueryProcessingRate = bf7_QueryProcessingRate;
	v->vaSyncBuffer = bf7_SyncBuffer;
	v->vaCopy = bf7_Copy;
	v->vaCreateBuffer2 = nullptr;	/* необязательный, libva проверит сам */

	TRACE("драйвер поднят, профилей %d", ctx->max_profiles);
	return VA_STATUS_SUCCESS;
}
