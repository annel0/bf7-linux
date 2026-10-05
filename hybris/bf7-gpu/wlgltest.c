/*
 * Минимальный клиент Wayland с GL ES: проверяет, что приложение рисует на GPU
 * и отдаёт композитору буфер GPU, а не разделяемую память.
 *
 * Работает поверх libhybris с платформой EGL wayland: wl_egl_window
 * превращается в ANativeWindow, буфер gralloc уходит композитору протоколом
 * android_wlegl.
 *
 * Берём wl_shell, а не xdg-shell: он есть у композитора и не требует генерации
 * кода протокола через wayland-scanner.
 *
 *	EGL_PLATFORM=wayland LD_LIBRARY_PATH=/usr/local/lib ./wlgltest [кадров]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static struct wl_compositor *compositor;
static struct wl_shell *shell;

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
			    const char *iface, uint32_t ver)
{
	if (!strcmp(iface, "wl_compositor"))
		compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 1);
	else if (!strcmp(iface, "wl_shell"))
		shell = wl_registry_bind(reg, name, &wl_shell_interface, 1);
	else if (!strcmp(iface, "android_wlegl"))
		printf("композитор объявляет android_wlegl (версия %u)\n", ver);
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
	registry_global, registry_global_remove
};

int main(int argc, char **argv)
{
	const int frames = (argc > 1) ? atoi(argv[1]) : 60;
	const int w = 400, h = 300;

	struct wl_display *display = wl_display_connect(NULL);

	if (!display) {
		printf("не подключиться к композитору\n");
		return 1;
	}
	printf("подключились к композитору\n");

	struct wl_registry *registry = wl_display_get_registry(display);

	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!compositor || !shell) {
		printf("нет wl_compositor или wl_shell\n");
		return 1;
	}

	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct wl_shell_surface *shsurf = wl_shell_get_shell_surface(shell, surface);

	wl_shell_surface_set_toplevel(shsurf);

	struct wl_egl_window *win = wl_egl_window_create(surface, w, h);

	EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)display);

	if (dpy == EGL_NO_DISPLAY) {
		printf("eglGetDisplay не дал дисплея\n");
		return 1;
	}

	EGLint major = 0, minor = 0;

	if (!eglInitialize(dpy, &major, &minor)) {
		printf("eglInitialize отказал: 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d\n", major, minor);
	printf("EGL_VENDOR: %s\n", eglQueryString(dpy, EGL_VENDOR));

	{
		const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);

		printf("EGL_WL_bind_wayland_display у клиента: %s\n",
		       (ext && strstr(ext, "EGL_WL_bind_wayland_display")) ? "есть" : "нет");
	}

	eglBindAPI(EGL_OPENGL_ES_API);

	const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	EGLConfig cfg;
	EGLint ncfg = 0;

	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
		printf("eglChooseConfig не нашёл конфигурации: 0x%x\n", eglGetError());
		return 1;
	}

	const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);

	if (ctx == EGL_NO_CONTEXT) {
		printf("eglCreateContext отказал: 0x%x\n", eglGetError());
		return 1;
	}

	EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)win, NULL);

	if (surf == EGL_NO_SURFACE) {
		printf("eglCreateWindowSurface отказал: 0x%x\n", eglGetError());
		return 1;
	}

	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		printf("eglMakeCurrent отказал: 0x%x\n", eglGetError());
		return 1;
	}

	printf("GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	printf("GL_VERSION:  %s\n", glGetString(GL_VERSION));

	for (int i = 0; i < frames; i++) {
		float t = (float)i / (frames > 1 ? frames - 1 : 1);

		glViewport(0, 0, w, h);
		glClearColor(t, 0.3f, 1.0f - t, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (!eglSwapBuffers(dpy, surf)) {
			printf("eglSwapBuffers отказал на кадре %d: 0x%x\n", i, eglGetError());
			return 1;
		}
		wl_display_dispatch_pending(display);
	}
	printf("отрисовано кадров: %d\n", frames);
	printf("ИТОГ: клиент Wayland рисует на GPU\n");

	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	wl_display_disconnect(display);
	return 0;
}
