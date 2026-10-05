/*
 * BF7: читает ли композитор недорисованный кадр, если клиент не ждёт забор?
 *
 * libhybris в queueBuffer ждёт GPU (sync_wait) перед фиксацией кадра — это
 * держит GTK4-GLES на 45 к/с. Без ожидания 60 к/с, но буфер уходит phoc
 * раньше, чем GPU его дописал. Здесь GPU нарочно рисует кадр долго: шейдер
 * крутит цикл из SLOI шагов, но итог — ровный серый одного из пяти уровней
 * (по номеру кадра). Если phoc читает буфер недописанным, на снимке окна
 * будет чужой уровень или шов по плиткам. (Первая версия складывала слои
 * сложением — в 8-битном буфере прибавка меньше 1/255 пропадала.)
 *
 * Сборка (на телефоне):
 *   wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
 *   wayland-scanner private-code  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c
 *   gcc -O2 -o fencetest fencetest.c xdg-shell-protocol.c -lwayland-client -lwayland-egl -lEGL -lGLESv2
 * Запуск: SLOI=60 ./fencetest [секунд]   (ZAMER=1 — печатать время GPU на кадр)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"

static struct wl_compositor *comp;
static struct xdg_wm_base *wm;
static int shir = 720, vys = 1400, nastroeno;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
	if (!strcmp(iface, "wl_compositor"))
		comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, "xdg_wm_base"))
		wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t s) { xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };

static void surf_conf(void *d, struct xdg_surface *s, uint32_t serial)
{
	xdg_surface_ack_configure(s, serial);
	nastroeno = 1;
}
static const struct xdg_surface_listener surf_l = { surf_conf };

static void top_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	if (w > 0 && h > 0) { shir = w; vys = h; }
}
static void top_close(void *d, struct xdg_toplevel *t) { exit(0); }
static const struct xdg_toplevel_listener top_l = { top_conf, top_close };

static double seychas(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static const char *vs =
	"attribute vec2 p;\n"
	"void main(){ gl_Position = vec4(p, 0.0, 1.0); }\n";
static const char *fs =
	"precision mediump float;\n"
	"uniform float v;\n"
	"uniform float n;\n"
	"void main(){\n"
	"  float a = gl_FragCoord.x * 0.001;\n"
	"  for (int i = 0; i < 4000; i++) {\n"
	"    if (float(i) >= n) break;\n"
	"    a = fract(sin(a * 12.9898 + 78.233) * 43758.5453);\n"
	"  }\n"
	"  gl_FragColor = vec4(vec3(v + step(2.0, a)), 1.0);\n"
	"}\n";

static GLuint shader(GLenum t, const char *src)
{
	GLuint s = glCreateShader(t);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	return s;
}

int main(int argc, char **argv)
{
	int sekund = argc > 1 ? atoi(argv[1]) : 20;
	int sloi = getenv("SLOI") ? atoi(getenv("SLOI")) : 40;	/* шагов цикла */
	int zamer = getenv("ZAMER") != NULL;

	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy) { fprintf(stderr, "нет Wayland\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_l, NULL);
	wl_display_roundtrip(dpy);
	if (!comp || !wm) { fprintf(stderr, "нет wl_compositor/xdg_wm_base\n"); return 1; }
	xdg_wm_base_add_listener(wm, &wm_l, NULL);

	struct wl_surface *surf = wl_compositor_create_surface(comp);
	struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surf);
	xdg_surface_add_listener(xs, &surf_l, NULL);
	struct xdg_toplevel *top = xdg_surface_get_toplevel(xs);
	xdg_toplevel_add_listener(top, &top_l, NULL);
	xdg_toplevel_set_title(top, "fencetest");
	wl_surface_commit(surf);
	while (!nastroeno)
		wl_display_dispatch(dpy);

	EGLDisplay ed = eglGetDisplay((EGLNativeDisplayType)dpy);
	eglInitialize(ed, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE };
	EGLConfig cfg; EGLint n;
	eglChooseConfig(ed, ca, &cfg, 1, &n);
	EGLint cx[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(ed, cfg, EGL_NO_CONTEXT, cx);
	struct wl_egl_window *ew = wl_egl_window_create(surf, shir, vys);
	EGLSurface es = eglCreateWindowSurface(ed, cfg, (EGLNativeWindowType)ew, NULL);
	eglMakeCurrent(ed, es, es, ctx);
	eglSwapInterval(ed, 1);
	printf("окно %dx%d, шагов %d\n", shir, vys, sloi);

	GLuint pr = glCreateProgram();
	glAttachShader(pr, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(pr, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(pr, 0, "p");
	glLinkProgram(pr);
	glUseProgram(pr);
	GLint uv = glGetUniformLocation(pr, "v");
	glUniform1f(glGetUniformLocation(pr, "n"), (float)sloi);
	static const float kvad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kvad);
	glEnableVertexAttribArray(0);

	/*
	 * Пять уровней при трёх буферах: устаревшее содержимое буфера (кадр
	 * k-3) даёт другой уровень, чем кадр k. На снимках уровни должны идти
	 * только вперёд: 51 → 89 → 128 → 166 → 204 → 51 …
	 */
	static const float urov[5] = { 0.2f, 0.35f, 0.5f, 0.65f, 0.8f };
	double t0 = seychas(), gpu = 0;
	long kadr = 0;
	while (seychas() - t0 < sekund) {
		glViewport(0, 0, shir, vys);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(uv, urov[kadr % 5]);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (zamer && kadr % 30 == 5) {
			double a = seychas();
			glFinish();
			gpu = seychas() - a;
			printf("кадр %ld: glFinish %.1f мс\n", kadr, gpu * 1000);
		}
		eglSwapBuffers(ed, es);
		wl_display_dispatch_pending(dpy);
		kadr++;
	}
	printf("кадров %ld за %d с (%.1f к/с)\n", kadr, sekund, kadr / (seychas() - t0));
	return 0;
}
