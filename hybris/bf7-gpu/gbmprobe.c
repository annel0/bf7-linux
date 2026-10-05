/*
 * BF7: пробник пути GBM так, как им ходит WebKit (DMABuf-рендерер).
 *
 * WebKit в процессе отрисовки: gbm_create_device(renderD128) → gbm_bo_create
 * → дескриптор dma-buf → eglCreateImage(EGL_LINUX_DMA_BUF_EXT) → рисует в
 * него через FBO → отдаёт дескриптор окну. Здесь каждый шаг отдельно, с
 * проверкой: GPU заливает буфер цветом, а процессор читает байты прямо из
 * dma-buf. Если байтов нет — рвётся ввоз/рисование, а не передача окну.
 *
 * Сборка (на телефоне):
 *   gcc -O2 -o gbmprobe gbmprobe.c -lgbm -lEGL -lGLESv2
 * Запуск: ./gbmprobe [ширина высота [модификатор]]
 *
 * UI=1 — сторона окна: дисплей EGL на платформе Wayland (как у GTK в
 * процессе Epiphany), буфер GBM заполняется процессором, ввозится как
 * EGLImage и читается обратно через FBO. Нужны WAYLAND_DISPLAY и
 * XDG_RUNTIME_DIR сеанса.
 * Сборка тогда с -lwayland-client.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-client.h>

#define CHECK(c, ...) do { if (!(c)) { printf("ОШИБКА: " __VA_ARGS__); printf("\n"); return 1; } } while (0)

int main(int argc, char **argv)
{
	int w = argc > 2 ? atoi(argv[1]) : 720;
	int h = argc > 2 ? atoi(argv[2]) : 1400;
	int use_mod = argc > 3;
	uint64_t mod = use_mod ? strtoull(argv[3], NULL, 0) : 0;

	int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	CHECK(fd >= 0, "renderD128 не открылся");
	struct gbm_device *dev = gbm_create_device(fd);
	CHECK(dev, "gbm_create_device");
	printf("gbm: бэкенд %s\n", gbm_device_get_backend_name(dev));

	struct gbm_bo *bo;
	if (use_mod)
		bo = gbm_bo_create_with_modifiers2(dev, w, h, GBM_FORMAT_ARGB8888,
						   &mod, 1, GBM_BO_USE_RENDERING);
	else
		bo = gbm_bo_create(dev, w, h, GBM_FORMAT_ARGB8888,
				   GBM_BO_USE_RENDERING);
	CHECK(bo, "gbm_bo_create %dx%d", w, h);
	int planes = gbm_bo_get_plane_count(bo);
	int bfd = gbm_bo_get_fd_for_plane(bo, 0);
	uint32_t stride = gbm_bo_get_stride_for_plane(bo, 0);
	uint32_t off = gbm_bo_get_offset(bo, 0);
	uint64_t bmod = gbm_bo_get_modifier(bo);
	off_t sz = lseek(bfd, 0, SEEK_END);
	printf("bo: %dx%d планов %d fd %d шаг %u смещение %u модификатор 0x%llx размер fd %lld\n",
	       w, h, planes, bfd, stride, off, (unsigned long long)bmod, (long long)sz);
	CHECK(bfd >= 0, "нет дескриптора");

	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	int ui = getenv("UI") != NULL;
	EGLDisplay dpy;
	if (ui) {
		/* сторона окна: буфер заполняем процессором, GPU только читает */
		struct wl_display *wl = wl_display_connect(NULL);
		CHECK(wl, "нет соединения Wayland");
		unsigned char *wm = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, bfd, 0);
		CHECK(wm != MAP_FAILED, "mmap на запись");
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				unsigned char *q = wm + off + (size_t)y * stride + 4 * x;
				q[0] = 0; q[1] = 255; q[2] = 0; q[3] = 255;	/* зелёный */
			}
		munmap(wm, sz);
		dpy = gpd(EGL_PLATFORM_WAYLAND_KHR, wl, NULL);
	} else
		dpy = gpd ? gpd(EGL_PLATFORM_GBM_KHR, dev, NULL)
			  : eglGetDisplay((EGLNativeDisplayType)dev);
	CHECK(dpy != EGL_NO_DISPLAY, "нет дисплея EGL");
	EGLint maj, min;
	CHECK(eglInitialize(dpy, &maj, &min), "eglInitialize 0x%x", eglGetError());
	const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
	printf("EGL %d.%d; dma_buf_import %s; modifiers %s; MESA export %s; KHR_fence %s\n", maj, min,
	       strstr(ext, "EGL_EXT_image_dma_buf_import ") || strstr(ext, "EGL_EXT_image_dma_buf_import\0") ? "да" : (strstr(ext, "dma_buf_import") ? "да?" : "нет"),
	       strstr(ext, "dma_buf_import_modifiers") ? "да" : "нет",
	       strstr(ext, "EGL_MESA_image_dma_buf_export") ? "да" : "нет",
	       strstr(ext, "EGL_KHR_fence_sync") ? "да" : "нет");

	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	if (ctx == EGL_NO_CONTEXT) {
		EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
		EGLConfig cfg; EGLint n;
		eglChooseConfig(dpy, ca, &cfg, 1, &n);
		ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattr);
	}
	CHECK(ctx != EGL_NO_CONTEXT, "контекст 0x%x", eglGetError());
	CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx), "makecurrent 0x%x", eglGetError());

	EGLAttrib ia[32]; int k = 0;
	ia[k++] = EGL_WIDTH; ia[k++] = w;
	ia[k++] = EGL_HEIGHT; ia[k++] = h;
	ia[k++] = EGL_LINUX_DRM_FOURCC_EXT; ia[k++] = GBM_FORMAT_ARGB8888;
	ia[k++] = EGL_DMA_BUF_PLANE0_FD_EXT; ia[k++] = bfd;
	ia[k++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT; ia[k++] = off;
	ia[k++] = EGL_DMA_BUF_PLANE0_PITCH_EXT; ia[k++] = stride;
	if (use_mod) {
		ia[k++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT; ia[k++] = (EGLAttrib)(bmod & 0xffffffff);
		ia[k++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT; ia[k++] = (EGLAttrib)(bmod >> 32);
	}
	ia[k++] = EGL_NONE;
	PFNEGLCREATEIMAGEPROC ci = (void *)eglGetProcAddress("eglCreateImage");
	EGLImage img = EGL_NO_IMAGE;
	if (ci)
		img = ci(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ia);
	if (img == EGL_NO_IMAGE) {
		PFNEGLCREATEIMAGEKHRPROC cik = (void *)eglGetProcAddress("eglCreateImageKHR");
		EGLint ia2[32];
		for (int i = 0; i < k; i++) ia2[i] = (EGLint)ia[i];
		printf("eglCreateImage: %s (0x%x), пробую KHR\n", ci ? "отказ" : "нет функции", eglGetError());
		img = cik(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, ia2);
	}
	CHECK(img != EGL_NO_IMAGE, "образ из dma-buf 0x%x", eglGetError());
	printf("образ из dma-buf ввезён\n");

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC tex2d = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC rbs = (void *)eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
	GLuint fb, t, rb;
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	const char *via = getenv("RB") ? "renderbuffer" : "texture";
	if (getenv("RB")) {
		glGenRenderbuffers(1, &rb);
		glBindRenderbuffer(GL_RENDERBUFFER, rb);
		rbs(GL_RENDERBUFFER, img);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	} else {
		glGenTextures(1, &t);
		glBindTexture(GL_TEXTURE_2D, t);
		tex2d(GL_TEXTURE_2D, img);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
	}
	GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	printf("FBO через %s: 0x%x (%s), glGetError 0x%x\n", via, st,
	       st == GL_FRAMEBUFFER_COMPLETE ? "готов" : "НЕ готов", glGetError());

	if (!ui) {
		glViewport(0, 0, w, h);
		glClearColor(1.0f, 0.0f, 0.0f, 1.0f);	/* красный */
		glClear(GL_COLOR_BUFFER_BIT);
	}
	glFinish();
	unsigned char px[4] = { 0 };
	glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("glReadPixels из FBO: %u %u %u %u%s\n", px[0], px[1], px[2], px[3], ui ? "  (ожидаю зелёный 0 255 0 255)" : "");
	if (ui) return 0;

	unsigned char *m = mmap(NULL, sz, PROT_READ, MAP_SHARED, bfd, 0);
	CHECK(m != MAP_FAILED, "mmap dma-buf");
	unsigned char *p = m + off + stride * (h / 2) + 4 * (w / 2);
	printf("байты в dma-buf (середина): %u %u %u %u  (ожидаю ARGB8888 LE: 0 0 255 255)\n",
	       p[0], p[1], p[2], p[3]);
	printf("байты в dma-buf (начало):   %u %u %u %u\n", m[off], m[off + 1], m[off + 2], m[off + 3]);
	return 0;
}
