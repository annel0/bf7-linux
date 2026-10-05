/*
 * BF7: проигрыватель с аппаратным декодированием.
 *
 * Кадр не копируется ни разу: декодер MediaTek кладёт его в буфер gralloc,
 * демон отдаёт нам дескриптор dma-buf, а мы передаём этот же дескриптор
 * композитору через zwp_linux_dmabuf_v1. Ни EGL, ни шейдера здесь нет вовсе —
 * преобразование цвета и вывод делает композитор, который YV12 уже объявляет.
 *
 * Почему не окно EGL. Композитор переключён на клиентскую интеграцию буферов
 * linux-dmabuf, и глобального объекта android_wlegl — того, которым окна EGL
 * из libhybris отдают кадры, — у него больше нет. Прямая передача и проще, и
 * честнее: лишнего преобразования в промежуточную поверхность не возникает.
 *
 * Сборка на устройстве: см. build-vplay.sh
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>

#include <wayland-client.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "linux-dmabuf-unstable-v1-client-protocol.h"

#include "bf7-videod-proto.h"

#define FOURCC_ARGB8888	0x34325241
#define FOURCC_YV12	0x32315659	/* Y, затем Cr, затем Cb */
#define FOURCC_NV12	0x3231564e
#define HAL_NV12	0x3231564e

static struct wl_compositor *compositor;
static struct wl_shell *shell;
static struct zwp_linux_dmabuf_v1 *dmabuf;
static int sock = -1;

/* --- реестр ----------------------------------------------------------- */

static void registry_global(void *d, struct wl_registry *reg, uint32_t name,
			    const char *iface, uint32_t ver)
{
	if (!strcmp(iface, "wl_compositor"))
		compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
	else if (!strcmp(iface, "wl_shell"))
		shell = wl_registry_bind(reg, name, &wl_shell_interface, 1);
	else if (!strcmp(iface, "zwp_linux_dmabuf_v1"))
		dmabuf = wl_registry_bind(reg, name, &zwp_linux_dmabuf_v1_interface,
					  ver < 3 ? ver : 3);
}

static void registry_global_remove(void *d, struct wl_registry *r, uint32_t n) {}

static const struct wl_registry_listener registry_listener = {
	registry_global, registry_global_remove
};

static void shell_ping(void *d, struct wl_shell_surface *s, uint32_t serial)
{
	wl_shell_surface_pong(s, serial);
}
static void shell_configure(void *d, struct wl_shell_surface *s, uint32_t e,
			    int32_t w, int32_t h) {}
static void shell_popup_done(void *d, struct wl_shell_surface *s) {}

static const struct wl_shell_surface_listener shell_listener = {
	shell_ping, shell_configure, shell_popup_done
};

/* ------------------------------------------------------------------ */
/* Преобразование цвета на GPU                                         */
/*                                                                     */
/* Композитор принимает YV12, но нарисовать его не может: у Qt нет     */
/* шейдера для многоплоскостного YUV, и он отвечает "Empty shader      */
/* passed to graphics pipeline". Поэтому переводим кадр в RGB сами —   */
/* одной выборкой из внешней текстуры, где преобразование делает сам   */
/* драйвер. Копирований по-прежнему нет: и вход, и выход — dma-buf.    */
/* ------------------------------------------------------------------ */

struct rgb_target {
	struct gbm_bo	*bo;
	int		fd;
	uint32_t	stride;
	EGLImageKHR	image;
	GLuint		tex, fbo;
	int		busy;		/* держит композитор */
};

#define RGB_BUFFERS 3

static struct gbm_device *gbm;
static EGLDisplay egl_dpy;
static EGLContext egl_ctx;
static GLuint prog, ext_tex;
static struct rgb_target targets[RGB_BUFFERS];
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target;

static const char *vs_src =
	"attribute vec2 pos;\n"
	"varying vec2 uv;\n"
	"void main() {\n"
	"  uv = vec2((pos.x + 1.0) * 0.5, (1.0 - pos.y) * 0.5);\n"
	"  gl_Position = vec4(pos, 0.0, 1.0);\n"
	"}\n";

static const char *fs_src =
	"#extension GL_OES_EGL_image_external : require\n"
	"precision mediump float;\n"
	"uniform samplerExternalOES tex;\n"
	"varying vec2 uv;\n"
	"void main() { gl_FragColor = texture2D(tex, uv); }\n";

static GLuint make_shader(GLenum type, const char *src)
{
	GLuint sh = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(sh, 1, &src, NULL);
	glCompileShader(sh);
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];

		glGetShaderInfoLog(sh, sizeof log, NULL, log);
		printf("шейдер не собрался: %s\n", log);
		return 0;
	}
	return sh;
}

static int gpu_init(unsigned w, unsigned h)
{
	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC getdpy;
	EGLConfig cfg;
	EGLint n = 0, maj, min;
	int i, fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);

	if (fd < 0) { perror("/dev/dri/renderD128"); return -1; }
	gbm = gbm_create_device(fd);
	if (!gbm) { printf("нет устройства GBM\n"); return -1; }

	getdpy = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (!getdpy) { printf("нет eglGetPlatformDisplayEXT\n"); return -1; }
	egl_dpy = getdpy(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, &maj, &min)) {
		printf("EGL на GBM не поднялся\n"); return -1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &n) || n < 1) {
		printf("нет подходящей конфигурации EGL\n"); return -1;
	}
	egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	/* Рисуем только в кадровые буферы, окно не нужно. */
	if (!eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_ctx)) {
		printf("безоконный контекст не сделать текущим: 0x%x\n", eglGetError());
		return -1;
	}
	printf("GPU: %s\n", glGetString(GL_RENDERER));

	create_image = (void *)eglGetProcAddress("eglCreateImageKHR");
	destroy_image = (void *)eglGetProcAddress("eglDestroyImageKHR");
	image_target = (void *)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	if (!create_image || !destroy_image || !image_target) {
		printf("нет точек входа для образов EGL\n"); return -1;
	}

	{
		GLuint vs = make_shader(GL_VERTEX_SHADER, vs_src);
		GLuint fs = make_shader(GL_FRAGMENT_SHADER, fs_src);
		GLint ok = 0;

		if (!vs || !fs) return -1;
		prog = glCreateProgram();
		glAttachShader(prog, vs);
		glAttachShader(prog, fs);
		glBindAttribLocation(prog, 0, "pos");
		glLinkProgram(prog);
		glGetProgramiv(prog, GL_LINK_STATUS, &ok);
		if (!ok) { printf("программа не слинковалась\n"); return -1; }
		glUseProgram(prog);
		glUniform1i(glGetUniformLocation(prog, "tex"), 0);
	}
	glGenTextures(1, &ext_tex);

	for (i = 0; i < RGB_BUFFERS; i++) {
		struct rgb_target *t = &targets[i];
		EGLint attrs[16];
		int a = 0;

		t->bo = gbm_bo_create(gbm, w, h, GBM_FORMAT_ARGB8888,
				      GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
		if (!t->bo) { printf("не выделить буфер RGB\n"); return -1; }
		t->fd = gbm_bo_get_fd(t->bo);
		t->stride = gbm_bo_get_stride(t->bo);

		attrs[a++] = EGL_WIDTH;			attrs[a++] = (EGLint)w;
		attrs[a++] = EGL_HEIGHT;		attrs[a++] = (EGLint)h;
		attrs[a++] = EGL_LINUX_DRM_FOURCC_EXT;	attrs[a++] = FOURCC_ARGB8888;
		attrs[a++] = EGL_DMA_BUF_PLANE0_FD_EXT;	attrs[a++] = t->fd;
		attrs[a++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;	attrs[a++] = 0;
		attrs[a++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;	attrs[a++] = (EGLint)t->stride;
		attrs[a++] = EGL_NONE;

		t->image = create_image(egl_dpy, EGL_NO_CONTEXT,
					EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
		if (t->image == EGL_NO_IMAGE_KHR) {
			printf("буфер RGB не втянуть в EGL: 0x%x\n", eglGetError());
			return -1;
		}
		glGenTextures(1, &t->tex);
		glBindTexture(GL_TEXTURE_2D, t->tex);
		image_target(GL_TEXTURE_2D, t->image);
		glGenFramebuffers(1, &t->fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				       GL_TEXTURE_2D, t->tex, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			printf("кадровый буфер RGB неполон\n"); return -1;
		}
	}
	printf("буферов вывода RGB: %d, шаг %u\n", RGB_BUFFERS, targets[0].stride);
	return 0;
}

/* Перевести кадр YUV в свободный буфер RGB; вернуть его номер или -1. */
static int gpu_convert(int dmafd, uint32_t fourcc, uint32_t w, uint32_t h,
		       uint32_t stride)
{
	static const GLfloat quad[] = {
		-1.f, -1.f,  1.f, -1.f, -1.f, 1.f,
		 1.f, -1.f,  1.f,  1.f, -1.f, 1.f,
	};
	EGLint attrs[24];
	EGLImageKHR img;
	size_t ysz = (size_t)stride * h, csz = (size_t)(stride / 2) * (h / 2);
	int a = 0, i, slot = -1;

	for (i = 0; i < RGB_BUFFERS; i++)
		if (!targets[i].busy) { slot = i; break; }
	if (slot < 0)
		return -1;

	attrs[a++] = EGL_WIDTH;			attrs[a++] = (EGLint)w;
	attrs[a++] = EGL_HEIGHT;		attrs[a++] = (EGLint)h;
	attrs[a++] = EGL_LINUX_DRM_FOURCC_EXT;	attrs[a++] = (EGLint)fourcc;
	attrs[a++] = EGL_DMA_BUF_PLANE0_FD_EXT;		attrs[a++] = dmafd;
	attrs[a++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;	attrs[a++] = 0;
	attrs[a++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;	attrs[a++] = (EGLint)stride;
	attrs[a++] = EGL_DMA_BUF_PLANE1_FD_EXT;		attrs[a++] = dmafd;
	attrs[a++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;	attrs[a++] = (EGLint)ysz;
	attrs[a++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;	attrs[a++] = (EGLint)(stride / 2);
	attrs[a++] = EGL_DMA_BUF_PLANE2_FD_EXT;		attrs[a++] = dmafd;
	attrs[a++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;	attrs[a++] = (EGLint)(ysz + csz);
	attrs[a++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;	attrs[a++] = (EGLint)(stride / 2);
	attrs[a++] = EGL_NONE;

	img = create_image(egl_dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
	if (img == EGL_NO_IMAGE_KHR) {
		printf("кадр YUV не втянуть в EGL: 0x%x\n", eglGetError());
		return -1;
	}

	glBindTexture(GL_TEXTURE_EXTERNAL_OES, ext_tex);
	image_target(GL_TEXTURE_EXTERNAL_OES, img);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glBindFramebuffer(GL_FRAMEBUFFER, targets[slot].fbo);
	glViewport(0, 0, (GLsizei)w, (GLsizei)h);
	glUseProgram(prog);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glFinish();		/* композитор возьмёт буфер без fence */

	destroy_image(egl_dpy, img);
	return slot;
}

/* --- кадр: живёт, пока композитор его не отпустит ---------------------- */

struct held {
	int		slot;		/* какой буфер RGB занят */
	struct wl_buffer *buffer;
};

static int write_all(int fd, const void *buf, size_t n)
{
	const uint8_t *p = buf;

	while (n) {
		ssize_t k = write(fd, p, n);

		if (k <= 0) {
			if (k < 0 && errno == EINTR) continue;
			return -1;
		}
		p += k; n -= (size_t)k;
	}
	return 0;
}

static void release_to_daemon(uint64_t handle)
{
	struct bf7v_request req;

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_RELEASE;
	req.handle = handle;
	write_all(sock, &req, sizeof req);
}

/* Композитор отпустил буфер — значит кадр можно вернуть декодеру. */
static int released;

static void buffer_release(void *data, struct wl_buffer *b)
{
	struct held *h = data;

	if (!released++)
		printf("композитор отпустил первый буфер — значит он его показал\n");

	wl_buffer_destroy(h->buffer);
	targets[h->slot].busy = 0;
	free(h);
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

/* --- связь с демоном -------------------------------------------------- */

static int recv_msg(struct bf7v_frame *f, int *dmafd)
{
	struct msghdr msg;
	struct iovec io;
	char control[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *c;
	ssize_t n;

	memset(&msg, 0, sizeof msg);
	io.iov_base = f;
	io.iov_len = sizeof *f;
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof control;

	do { n = recvmsg(sock, &msg, 0); } while (n < 0 && errno == EINTR);
	if (n <= 0) return -1;
	*dmafd = -1;
	for (c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
			memcpy(dmafd, CMSG_DATA(c), sizeof(int));
	return 0;
}

/* --- разбор Annex-B --------------------------------------------------- */

static size_t next_start(const uint8_t *b, size_t n, size_t from, size_t *sclen)
{
	size_t i;

	for (i = from; i + 3 <= n; i++) {
		if (!b[i] && !b[i + 1] && b[i + 2] == 1) { *sclen = 3; return i; }
		if (i + 4 <= n && !b[i] && !b[i + 1] && !b[i + 2] && b[i + 3] == 1) {
			*sclen = 4; return i;
		}
	}
	return n;
}

int main(int argc, char **argv)
{
	const char *file = argc > 1 ? argv[1] : "/root/codec2/test640.h264";
	unsigned vw = argc > 3 ? (unsigned)atoi(argv[2]) : 640;
	unsigned vh = argc > 3 ? (unsigned)atoi(argv[3]) : 480;
	struct wl_display *display;
	struct wl_surface *surface;
	struct wl_shell_surface *shsurf;
	struct bf7v_request req;
	struct bf7v_frame reply;
	uint8_t *buf;
	size_t size, pos;
	int shown = 0, dummy, gpu_ready = 0;
	int64_t pts = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	{
		FILE *f = fopen(file, "rb");
		long len;

		if (!f) { perror(file); return 1; }
		fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
		buf = malloc((size_t)len);
		size = fread(buf, 1, (size_t)len, f);
		fclose(f);
	}

	display = wl_display_connect(NULL);
	if (!display) { printf("нет связи с композитором\n"); return 1; }
	{
		struct wl_registry *reg = wl_display_get_registry(display);

		wl_registry_add_listener(reg, &registry_listener, NULL);
		wl_display_roundtrip(display);
	}
	if (!compositor || !shell || !dmabuf) {
		printf("композитор не даёт нужного: compositor=%p shell=%p dmabuf=%p\n",
		       (void *)compositor, (void *)shell, (void *)dmabuf);
		return 1;
	}
	printf("композитор: есть wl_compositor, wl_shell и zwp_linux_dmabuf_v1\n");

	surface = wl_compositor_create_surface(compositor);
	shsurf = wl_shell_get_shell_surface(shell, surface);
	wl_shell_surface_add_listener(shsurf, &shell_listener, NULL);
	wl_shell_surface_set_toplevel(shsurf);

	/* --- демон --- */
	{
		struct sockaddr_un addr;

		sock = socket(AF_UNIX, SOCK_STREAM, 0);
		memset(&addr, 0, sizeof addr);
		addr.sun_family = AF_UNIX;
		snprintf(addr.sun_path, sizeof addr.sun_path, "%s", BF7V_SOCKET_PATH);
		if (connect(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
			perror("демон декодирования"); return 1;
		}
	}
	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_OPEN;
	req.codec = BF7V_CODEC_H264;
	req.width = vw; req.height = vh;
	write_all(sock, &req, sizeof req);
	if (recv_msg(&reply, &dummy) < 0 ||
	    ((struct bf7v_status *)&reply)->code != 0) {
		printf("декодер не открылся\n"); return 1;
	}
	printf("декодер открыт\n");

	/* --- подача --- */
	pos = 0;
	while (pos < size) {
		size_t sclen = 0, start = next_start(buf, size, pos, &sclen), end, d2;

		if (start >= size) break;
		end = next_start(buf, size, start + sclen, &d2);
		{
			const uint8_t *nal = buf + start + sclen;
			unsigned type = (end > start + sclen) ? (nal[0] & 0x1f) : 0;

			memset(&req, 0, sizeof req);
			req.type = BF7V_MSG_QUEUE;
			req.size = (uint32_t)(end - start);
			req.pts_us = pts;
			if (type == 7 || type == 8)
				req.flags = BF7V_FLAG_CODEC_CONFIG;
			else
				pts += 33333;
			write_all(sock, &req, sizeof req);
			write_all(sock, buf + start, end - start);
		}
		pos = end;
	}
	printf("поток подан\n");

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_DRAIN;
	write_all(sock, &req, sizeof req);

	/* --- показ --- */
	for (;;) {
		struct bf7v_frame f;
		int dmafd = -1, slot;
		struct zwp_linux_buffer_params_v1 *params;
		struct held *h;
		uint32_t fourcc;

		if (recv_msg(&f, &dmafd) < 0) { printf("связь с демоном прервалась\n"); break; }
		if (f.type == BF7V_MSG_STATUS) { printf("поток кончился\n"); break; }
		if (f.type != BF7V_MSG_FRAME || dmafd < 0) break;

		if (!gpu_ready) {
			if (gpu_init(f.crop_w ? f.crop_w : f.width,
				     f.crop_h ? f.crop_h : f.height) < 0) {
				close(dmafd);
				break;
			}
			gpu_ready = 1;
		}

		fourcc = (f.format == HAL_NV12) ? FOURCC_NV12 : FOURCC_YV12;

		/* Ждём, пока композитор отпустит хоть один буфер вывода. */
		while ((slot = gpu_convert(dmafd, fourcc,
					   f.crop_w ? f.crop_w : f.width,
					   f.crop_h ? f.crop_h : f.height,
					   f.stride)) < 0) {
			wl_display_roundtrip(display);
			if (wl_display_get_error(display)) break;
		}

		/* Кадр YUV больше не нужен: он уже переведён в RGB. */
		close(dmafd);
		release_to_daemon(f.handle);

		if (slot < 0) break;
		targets[slot].busy = 1;

		params = zwp_linux_dmabuf_v1_create_params(dmabuf);
		zwp_linux_buffer_params_v1_add(params, targets[slot].fd, 0, 0,
					       targets[slot].stride, 0, 0);
		h = calloc(1, sizeof *h);
		h->slot = slot;
		h->buffer = zwp_linux_buffer_params_v1_create_immed(
			params,
			(int32_t)(f.crop_w ? f.crop_w : f.width),
			(int32_t)(f.crop_h ? f.crop_h : f.height),
			FOURCC_ARGB8888, 0);
		zwp_linux_buffer_params_v1_destroy(params);
		if (!h->buffer) {
			printf("композитор не принял буфер RGB\n");
			targets[slot].busy = 0;
			free(h);
			break;
		}
		wl_buffer_add_listener(h->buffer, &buffer_listener, h);

		wl_surface_attach(surface, h->buffer, 0, 0);
		wl_surface_damage(surface, 0, 0, 1 << 24, 1 << 24);
		wl_surface_commit(surface);
		wl_display_flush(display);
		shown++;
		if (shown == 1) {
			wl_display_roundtrip(display);
			if (wl_display_get_error(display)) {
				printf("ОШИБКА ПРОТОКОЛА после первого кадра\n");
				break;
			}
			printf("первый кадр показан, протокол чист\n");
		}

		{
			struct timespec ts = { 0, 33000000 };

			nanosleep(&ts, NULL);
		}
		wl_display_dispatch_pending(display);
	}

	printf("ИТОГ: показано кадров %d, отпущено композитором %d\n",
	       shown, released);
	wl_display_roundtrip(display);
	sleep(3);	/* дать разглядеть последний кадр */

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_CLOSE;
	write_all(sock, &req, sizeof req);
	return shown > 0 ? 0 : 1;
}
