/*
 * BF7: повторяем путь Xwayland целиком — буфер gbm, образ EGL из dma-buf
 * и привязка образа к текстуре. Ищем, от чего зависит отказ 0x300C.
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

static PFNEGLCREATEIMAGEKHRPROC pCreateImage;
static PFNEGLDESTROYIMAGEKHRPROC pDestroyImage;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC pBind;

static void try_one(EGLDisplay dpy, struct gbm_device *gbm, unsigned w, unsigned h)
{
	struct gbm_bo *bo = gbm_bo_create(gbm, w, h, DRM_FORMAT_XRGB8888,
					  GBM_BO_USE_RENDERING);
	int fd;
	EGLint a[20]; int n = 0;
	EGLImageKHR img;
	GLuint tex;
	EGLint err;

	if (!bo) { printf("%4ux%-4u выделение отказано\n", w, h); return; }
	fd = gbm_bo_get_fd(bo);
	a[n++] = EGL_WIDTH;  a[n++] = w;
	a[n++] = EGL_HEIGHT; a[n++] = h;
	a[n++] = EGL_LINUX_DRM_FOURCC_EXT; a[n++] = gbm_bo_get_format(bo);
	a[n++] = EGL_DMA_BUF_PLANE0_FD_EXT; a[n++] = fd;
	a[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT; a[n++] = 0;
	a[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT; a[n++] = gbm_bo_get_stride(bo);
	a[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT; a[n++] = 0;
	a[n++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT; a[n++] = 0;
	a[n++] = EGL_NONE;

	img = pCreateImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
	if (img == EGL_NO_IMAGE_KHR) {
		printf("%4ux%-4u шаг %5u  образ НЕ создан (0x%x)\n",
		       w, h, gbm_bo_get_stride(bo), eglGetError());
		close(fd); gbm_bo_destroy(bo); return;
	}
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	pBind(GL_TEXTURE_2D, img);
	err = eglGetError();
	printf("%4ux%-4u шаг %5u (%4u пикс)  привязка: %s (0x%x)\n",
	       w, h, gbm_bo_get_stride(bo), gbm_bo_get_stride(bo) / 4,
	       err == EGL_SUCCESS ? "принята" : "ОТКАЗ", err);
	glDeleteTextures(1, &tex);
	pDestroyImage(dpy, img);
	close(fd);
	gbm_bo_destroy(bo);
}

int main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy;
	EGLint major, minor, ncfg;
	EGLConfig cfg;
	EGLContext ctx;
	static const EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	unsigned widths[] = { 1075, 1074, 1073, 1072, 1080, 720, 481, 255 };
	unsigned i;

	dpy = gpd ? gpd(EGL_PLATFORM_GBM_KHR, gbm, NULL)
		  : eglGetDisplay((EGLNativeDisplayType)gbm);
	eglInitialize(dpy, &major, &minor);
	eglBindAPI(EGL_OPENGL_ES_API);
	eglChooseConfig(dpy, cattr, &cfg, 1, &ncfg);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "контекст не сделался текущим: 0x%x\n", eglGetError());
		return 1;
	}
	printf("GL: %s\n", (const char *)glGetString(GL_RENDERER));

	pCreateImage = (void *)eglGetProcAddress("eglCreateImageKHR");
	pDestroyImage = (void *)eglGetProcAddress("eglDestroyImageKHR");
	pBind = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	printf("привязка через eglGetProcAddress: %p\n", (void *)pBind);

	for (i = 0; i < sizeof(widths)/sizeof(widths[0]); i++)
		try_one(dpy, gbm, widths[i], 480);
	return 0;
}
