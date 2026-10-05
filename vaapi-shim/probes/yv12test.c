/*
 * BF7: принимает ли PowerVR кадр YV12 из dma-buf — целым слоем и по
 * плоскостям. Дескриптор собирается ровно такой, какой отдаёт наш драйвер.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>

#define W 640
#define H 480

static PFNEGLCREATEIMAGEKHRPROC pCreate;
static PFNEGLDESTROYIMAGEKHRPROC pDestroy;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pBind;

static void attempt(EGLDisplay dpy, int fd, const char *what,
		    uint32_t fourcc, int planes,
		    const unsigned *offs, const unsigned *pitch)
{
	EGLint a[40];
	int n = 0, i;
	static const EGLint fdk[3] = { EGL_DMA_BUF_PLANE0_FD_EXT,
		EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE2_FD_EXT };
	static const EGLint ofk[3] = { EGL_DMA_BUF_PLANE0_OFFSET_EXT,
		EGL_DMA_BUF_PLANE1_OFFSET_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT };
	static const EGLint pik[3] = { EGL_DMA_BUF_PLANE0_PITCH_EXT,
		EGL_DMA_BUF_PLANE1_PITCH_EXT, EGL_DMA_BUF_PLANE2_PITCH_EXT };
	EGLImageKHR img;
	GLuint tex;
	EGLint err;

	a[n++] = EGL_WIDTH;  a[n++] = (fourcc == DRM_FORMAT_R8) ? W : W;
	a[n++] = EGL_HEIGHT; a[n++] = (fourcc == DRM_FORMAT_R8) ? H : H;
	a[n++] = EGL_LINUX_DRM_FOURCC_EXT; a[n++] = (EGLint)fourcc;
	for (i = 0; i < planes; i++) {
		a[n++] = fdk[i]; a[n++] = fd;
		a[n++] = ofk[i]; a[n++] = (EGLint)offs[i];
		a[n++] = pik[i]; a[n++] = (EGLint)pitch[i];
	}
	a[n++] = EGL_NONE;

	while (eglGetError() != EGL_SUCCESS) ;	/* снять чужое */
	img = pCreate(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
	if (img == EGL_NO_IMAGE_KHR) {
		printf("  %-28s образ НЕ создан (0x%x)\n", what, eglGetError());
		return;
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	pBind(GL_TEXTURE_2D, img);
	err = eglGetError();
	printf("  %-28s образ создан, привязка: %s (0x%x)\n", what,
	       err == EGL_SUCCESS ? "принята" : "ОТКАЗ", err);
	glDeleteTextures(1, &tex);
	pDestroy(dpy, img);
}

int main(void)
{
	int drm = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gbm = gbm_create_device(drm);
	EGLDisplay dpy;
	EGLint major, minor, ncfg;
	EGLConfig cfg;
	EGLContext ctx;
	struct gbm_bo *bo;
	int fd;
	unsigned ysz, cst, csz;
	unsigned offs[3], pitch[3];
	static const EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");

	dpy = gpd(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	eglInitialize(dpy, &major, &minor);
	eglBindAPI(EGL_OPENGL_ES_API);
	eglChooseConfig(dpy, cattr, &cfg, 1, &ncfg);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	printf("EGL поставщик: %s\n", eglQueryString(dpy, EGL_VENDOR));
	printf("GL: %s\n", (const char *)glGetString(GL_RENDERER));

	pCreate = (void *)eglGetProcAddress("eglCreateImageKHR");
	pDestroy = (void *)eglGetProcAddress("eglDestroyImageKHR");
	pBind = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");

	/* Буфер с запасом: важен только размер и то, что это dma-buf. */
	bo = gbm_bo_create(gbm, W, H, DRM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING);
	if (!bo) { fprintf(stderr, "буфер не выделен\n"); return 1; }
	fd = gbm_bo_get_fd(bo);
	printf("буфер: шаг %u, dma-buf %d\n\n", gbm_bo_get_stride(bo), fd);

	/* Раскладка ровно как у драйвера: шаг яркости = ширине. */
	ysz = W * H;
	cst = W / 2;
	csz = cst * (H / 2);

	offs[0] = 0;       pitch[0] = W;
	offs[1] = ysz;     pitch[1] = cst;
	offs[2] = ysz+csz; pitch[2] = cst;
	attempt(dpy, fd, "YV12 одним слоем, 3 плоскости", DRM_FORMAT_YVU420, 3, offs, pitch);
	attempt(dpy, fd, "I420 одним слоем, 3 плоскости", DRM_FORMAT_YUV420, 3, offs, pitch);

	offs[0] = 0; pitch[0] = W;
	attempt(dpy, fd, "R8 одной плоскостью (яркость)", DRM_FORMAT_R8, 1, offs, pitch);

	offs[0] = 0; pitch[0] = W * 4;
	attempt(dpy, fd, "ARGB для сравнения", DRM_FORMAT_ARGB8888, 1, offs, pitch);

	/* Как это делает Firefox: формат YV12, а атрибуты — только нулевой
	 * плоскости. Остальных двух он не передаёт вовсе. */
	offs[0] = 0; pitch[0] = W;
	attempt(dpy, fd, "YV12, но только плоскость 0", DRM_FORMAT_YVU420, 1, offs, pitch);
	attempt(dpy, fd, "NV12, только плоскость 0", DRM_FORMAT_NV12, 1, offs, pitch);

	/* И NV12 целиком, двумя плоскостями. */
	offs[0] = 0;   pitch[0] = W;
	offs[1] = ysz; pitch[1] = W;
	attempt(dpy, fd, "NV12 целиком, 2 плоскости", DRM_FORMAT_NV12, 2, offs, pitch);
	return 0;
}
