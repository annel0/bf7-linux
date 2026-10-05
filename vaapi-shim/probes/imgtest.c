/*
 * BF7: повторяем путь Xwayland — буфер gbm -> образ EGL из dma-buf.
 * Нужно, чтобы понять, на каком формате и размере ввоз перестаёт работать.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm_fourcc.h>

static PFNEGLCREATEIMAGEKHRPROC pCreateImage;
static PFNEGLDESTROYIMAGEKHRPROC pDestroyImage;
static PFNEGLGETPLATFORMDISPLAYEXTPROC pGetPlatformDisplay;

static void try_one(EGLDisplay dpy, struct gbm_device *gbm,
		    unsigned w, unsigned h, uint32_t fourcc, const char *what)
{
	struct gbm_bo *bo;
	int fd;
	EGLint attrs[16];
	int n = 0;
	EGLImageKHR img;
	uint32_t real;

	bo = gbm_bo_create(gbm, w, h, fourcc,
			   GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
	if (!bo) { printf("%-28s ВЫДЕЛЕНИЕ ОТКАЗАНО\n", what); return; }

	real = gbm_bo_get_format(bo);
	fd = gbm_bo_get_fd(bo);
	if (fd < 0) { printf("%-28s ДЕСКРИПТОРА НЕТ\n", what); gbm_bo_destroy(bo); return; }

	attrs[n++] = EGL_WIDTH;			attrs[n++] = w;
	attrs[n++] = EGL_HEIGHT;		attrs[n++] = h;
	attrs[n++] = EGL_LINUX_DRM_FOURCC_EXT;	attrs[n++] = real;
	attrs[n++] = EGL_DMA_BUF_PLANE0_FD_EXT;	attrs[n++] = fd;
	attrs[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT; attrs[n++] = 0;
	attrs[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;  attrs[n++] = gbm_bo_get_stride(bo);
	attrs[n++] = EGL_NONE;

	img = pCreateImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
	printf("%-28s %ux%u просили %.4s, выделен %.4s, шаг %u -> %s (0x%x)\n",
	       what, w, h, (char *)&fourcc, (char *)&real, gbm_bo_get_stride(bo),
	       img == EGL_NO_IMAGE_KHR ? "ОБРАЗ НЕ СОЗДАН" : "образ создан",
	       eglGetError());
	if (img != EGL_NO_IMAGE_KHR) pDestroyImage(dpy, img);
	close(fd);
	gbm_bo_destroy(bo);
}

int main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gbm;
	EGLDisplay dpy;
	EGLint major, minor;

	if (fd < 0) { perror("renderD128"); return 1; }
	gbm = gbm_create_device(fd);
	if (!gbm) { fprintf(stderr, "gbm_create_device отказал\n"); return 1; }

	pGetPlatformDisplay = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = pGetPlatformDisplay ?
		pGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL) :
		eglGetDisplay((EGLNativeDisplayType)gbm);
	if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "дисплея нет\n"); return 1; }
	if (!eglInitialize(dpy, &major, &minor)) {
		fprintf(stderr, "eglInitialize отказал: 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d, поставщик: %s\n", major, minor, eglQueryString(dpy, EGL_VENDOR));

	pCreateImage = (void *)eglGetProcAddress("eglCreateImageKHR");
	pDestroyImage = (void *)eglGetProcAddress("eglDestroyImageKHR");
	if (!pCreateImage) { fprintf(stderr, "eglCreateImageKHR не найден\n"); return 1; }

	try_one(dpy, gbm, 1075, 480, DRM_FORMAT_XRGB8888, "стол XR24");
	try_one(dpy, gbm, 1075, 480, DRM_FORMAT_ARGB8888, "стол AR24");
	try_one(dpy, gbm, 1072,  27, DRM_FORMAT_ARGB8888, "панель AR24");
	try_one(dpy, gbm,  256, 256, DRM_FORMAT_ARGB8888, "мелкий AR24");
	return 0;
}
