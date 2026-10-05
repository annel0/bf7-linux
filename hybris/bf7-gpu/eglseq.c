/*
 * Повторяет последовательность Firefox: сначала контекст с устойчивостью в
 * варианте KHR (драйвер её отвергает), затем в варианте EXT.
 *
 * Проверяем догадку: сбрасывает ли драйвер код ошибки после успешного вызова.
 * Если нет, Firefox видит залипшее EGL_BAD_PARAMETER от первой попытки и
 * считает неудачной вторую, хотя контекст получен.
 */
#include <stdio.h>
#include <string.h>

#include <wayland-client.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define EGL_CONTEXT_FLAGS_KHR				0x30FC
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR	0x00000004
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR 0x31BD
#define EGL_LOSE_CONTEXT_ON_RESET_KHR			0x31BF
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT 0x3138
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT		0x30BF

static struct wl_compositor *comp;
static void rg(void *d, struct wl_registry *r, uint32_t n, const char *i, uint32_t v)
{
	if (!strcmp(i, "wl_compositor"))
		comp = wl_registry_bind(r, n, &wl_compositor_interface, 1);
}
static void rr(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener rl = { rg, rr };

int main(void)
{
	struct wl_display *wl = wl_display_connect(NULL);
	struct wl_registry *reg;
	EGLDisplay dpy;
	EGLint maj, min, n = 0;
	EGLConfig cfg;

	if (!wl) { printf("нет композитора\n"); return 1; }
	reg = wl_display_get_registry(wl);
	wl_registry_add_listener(reg, &rl, NULL);
	wl_display_roundtrip(wl);

	dpy = eglGetDisplay((EGLNativeDisplayType)wl);
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_ES_API);

	EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };

	eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
	printf("конфигураций: %d\n", n);
	if (n < 1) return 1;

	printf("ошибка до начала: 0x%04x\n", eglGetError());

	/* Первая попытка — как у Firefox, вариант KHR. */
	const EGLint khr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR, EGL_LOSE_CONTEXT_ON_RESET_KHR,
		EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR,
		EGL_NONE
	};
	EGLContext c1 = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, khr);

	printf("попытка KHR: контекст %p\n", (void *)c1);

	/* Вторая попытка — вариант EXT. Код ошибки НЕ читаем между вызовами,
	 * ровно как Firefox. */
	const EGLint ext[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_EXT, EGL_LOSE_CONTEXT_ON_RESET_KHR,
		EGL_CONTEXT_OPENGL_ROBUST_ACCESS_EXT, EGL_TRUE,
		EGL_NONE
	};
	EGLContext c2 = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ext);

	printf("попытка EXT: контекст %p\n", (void *)c2);
	printf("ошибка после успешной попытки: 0x%04x  <<< если не 0x3000, драйвер её не сбрасывает\n",
	       eglGetError());

	if (c2 != EGL_NO_CONTEXT) {
		printf("makeCurrent без поверхности: %d\n",
		       eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, c2));
		printf("ошибка после makeCurrent: 0x%04x\n", eglGetError());
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(dpy, c2);
	}
	return 0;
}
