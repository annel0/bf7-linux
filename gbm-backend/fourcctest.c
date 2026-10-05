/*
 * BF7: в каком порядке драйвер кладёт байты.
 *
 * Вопрос ровно один. Мы создаём буфер как ARGB8888 (байты B,G,R,A), втягиваем
 * его в EGL тем же fourcc и рисуем в него КРАСНЫЙ через GLES. Если драйвер
 * честно уважает fourcc, в памяти окажется 00 00 FF xx — красный в третьем
 * байте. Если он пишет как придётся, будет FF 00 00 xx.
 *
 * От ответа зависит, где чинить перестановку каналов: в объявлении формата
 * или в самом драйвере.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>

static void probe(struct gbm_device *gbm, EGLDisplay dpy, uint32_t fourcc,
		  const char *name)
{
	PFNEGLCREATEIMAGEKHRPROC create_image =
		(void *)eglGetProcAddress("eglCreateImageKHR");
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target =
		(void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	struct gbm_bo *bo;
	EGLImageKHR img;
	GLuint tex, fbo;
	int fd;
	uint32_t stride;
	void *map_data = NULL, *map;
	const uint8_t *px;
	EGLint attrs[16];
	int n = 0;

	bo = gbm_bo_create(gbm, 64, 64, fourcc,
			   GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
	if (!bo) { printf("%s: буфер не создать\n", name); return; }

	fd = gbm_bo_get_fd(bo);
	attrs[n++] = EGL_WIDTH;			attrs[n++] = 64;
	attrs[n++] = EGL_HEIGHT;		attrs[n++] = 64;
	attrs[n++] = EGL_LINUX_DRM_FOURCC_EXT;	attrs[n++] = (EGLint)fourcc;
	attrs[n++] = EGL_DMA_BUF_PLANE0_FD_EXT;	attrs[n++] = fd;
	attrs[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;	attrs[n++] = 0;
	attrs[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
	attrs[n++] = (EGLint)gbm_bo_get_stride(bo);
	attrs[n++] = EGL_NONE;

	img = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
	if (img == EGL_NO_IMAGE_KHR) {
		printf("%s: eglCreateImageKHR отказал 0x%x\n", name, eglGetError());
		return;
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	image_target(GL_TEXTURE_2D, img);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("%s: кадровый буфер неполон\n", name);
		return;
	}

	glViewport(0, 0, 64, 64);
	glClearColor(1.f, 0.f, 0.f, 1.f);	/* чистый красный */
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();

	map = gbm_bo_map(bo, 0, 0, 64, 64, GBM_BO_TRANSFER_READ, &stride, &map_data);
	if (!map) { printf("%s: не отобразить\n", name); return; }

	px = map;
	printf("%s: байты %02x %02x %02x %02x  -> красный в байте %d  %s\n",
	       name, px[0], px[1], px[2], px[3],
	       px[0] == 0xff ? 0 : (px[2] == 0xff ? 2 : -1),
	       (fourcc == DRM_FORMAT_ARGB8888 || fourcc == DRM_FORMAT_XRGB8888)
		       ? (px[2] == 0xff ? "ВЕРНО (fourcc уважён)" : "ПЕРЕСТАВЛЕНО")
		       : (px[0] == 0xff ? "ВЕРНО (fourcc уважён)" : "ПЕРЕСТАВЛЕНО"));
	gbm_bo_unmap(bo, map_data);
	gbm_bo_destroy(bo);
}

int main(void)
{
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint pb_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC getdpy;
	struct gbm_device *gbm;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLSurface pb;
	EGLint n = 0, maj, min;
	int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);

	if (fd < 0) { perror("renderD128"); return 1; }
	gbm = gbm_create_device(fd);
	if (!gbm) { printf("нет устройства GBM\n"); return 1; }
	printf("бэкенд: %s\n", gbm_device_get_backend_name(gbm));

	getdpy = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = getdpy(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize отказал\n"); return 1; }
	printf("EGL: %s\n", eglQueryString(dpy, EGL_VENDOR));

	eglBindAPI(EGL_OPENGL_ES_API);
	eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	pb = eglCreatePbufferSurface(dpy, cfg, pb_attr);
	if (!eglMakeCurrent(dpy, pb, pb, ctx)) { printf("makeCurrent отказал\n"); return 1; }
	printf("GL: %s\n\n", glGetString(GL_RENDERER));

	probe(gbm, dpy, DRM_FORMAT_ARGB8888, "ARGB8888 (ждём B,G,R,A)");
	probe(gbm, dpy, DRM_FORMAT_ABGR8888, "ABGR8888 (ждём R,G,B,A)");
	return 0;
}
