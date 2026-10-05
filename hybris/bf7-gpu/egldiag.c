/*
 * Что умеет наш EGL через libhybris: расширения, конфигурации pbuffer,
 * закадровый контекст.
 *
 * Firefox и Chromium создают контекст без окна — либо безповерхностный
 * (EGL_KHR_surfaceless_context), либо через pbuffer. Наш оконный путь уже
 * доказан (wlgltest), поэтому смотрим именно закадровый.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <wayland-client.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static struct wl_compositor *comp;

static void reg_global(void *d, struct wl_registry *r, uint32_t n,
		       const char *i, uint32_t v)
{
	if (!strcmp(i, "wl_compositor"))
		comp = wl_registry_bind(r, n, &wl_compositor_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static void print_wrapped(const char *title, const char *s)
{
	printf("%s:\n", title);
	if (!s) {
		printf("    (пусто)\n");
		return;
	}
	char *copy = strdup(s);
	for (char *t = strtok(copy, " "); t; t = strtok(NULL, " "))
		if (*t)
			printf("    %s\n", t);
	free(copy);
}

int main(void)
{
	print_wrapped("клиентские расширения EGL (без дисплея)",
		      eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));

	struct wl_display *wl = wl_display_connect(NULL);

	if (!wl) {
		printf("нет подключения к композитору\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(wl);

	wl_registry_add_listener(reg, &reg_l, NULL);
	wl_display_roundtrip(wl);

	EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)wl);
	EGLint maj, min;

	if (!eglInitialize(dpy, &maj, &min)) {
		printf("eglInitialize отказал: 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d\n\n", maj, min);
	print_wrapped("расширения дисплея", eglQueryString(dpy, EGL_EXTENSIONS));

	const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
	int surfaceless = ext && strstr(ext, "EGL_KHR_surfaceless_context") != NULL;
	int noconfig = ext && strstr(ext, "EGL_KHR_no_config_context") != NULL;

	printf("\nEGL_KHR_surfaceless_context: %s\n", surfaceless ? "есть" : "НЕТ");
	printf("EGL_KHR_no_config_context:   %s\n", noconfig ? "есть" : "НЕТ");

	eglBindAPI(EGL_OPENGL_ES_API);

	/* Конфигурации, пригодные для pbuffer. */
	EGLint pb_attr[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	EGLConfig cfgs[8];
	EGLint n = 0;

	eglChooseConfig(dpy, pb_attr, cfgs, 8, &n);
	printf("конфигураций с EGL_PBUFFER_BIT: %d\n", n);

	const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	if (n > 0) {
		EGLContext c = eglCreateContext(dpy, cfgs[0], EGL_NO_CONTEXT, ctx_attr);

		printf("  контекст под pbuffer: %s (0x%x)\n",
		       c != EGL_NO_CONTEXT ? "создан" : "отказ", eglGetError());
		if (c != EGL_NO_CONTEXT) {
			const EGLint surf_attr[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
			EGLSurface s = eglCreatePbufferSurface(dpy, cfgs[0], surf_attr);

			printf("  поверхность pbuffer: %s (0x%x)\n",
			       s != EGL_NO_SURFACE ? "создана" : "отказ", eglGetError());
			if (s != EGL_NO_SURFACE) {
				printf("  makeCurrent на pbuffer: %s\n",
				       eglMakeCurrent(dpy, s, s, c) ? "да" : "нет");
				printf("  GL_RENDERER: %s\n", glGetString(GL_RENDERER));
				eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
				eglDestroySurface(dpy, s);
			}
			eglDestroyContext(dpy, c);
		}
	}

	/* Безповерхностный путь. */
	{
		EGLint any_attr[] = {
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
			EGL_NONE
		};
		EGLConfig c0;
		EGLint m = 0;

		eglChooseConfig(dpy, any_attr, &c0, 1, &m);
		if (m > 0) {
			EGLContext c = eglCreateContext(dpy, c0, EGL_NO_CONTEXT, ctx_attr);

			printf("  контекст без поверхности: %s (0x%x)\n",
			       c != EGL_NO_CONTEXT ? "создан" : "отказ", eglGetError());
			if (c != EGL_NO_CONTEXT) {
				printf("  makeCurrent без поверхности: %s (0x%x)\n",
				       eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, c) ?
				       "да" : "нет", eglGetError());
				eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
				eglDestroyContext(dpy, c);
			}
		}
	}

	/* Контекст GL ES 3 — Firefox просит именно его. */
	{
		const EGLint c3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
		EGLint any_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
		EGLConfig c0;
		EGLint m = 0;

		eglChooseConfig(dpy, any_attr, &c0, 1, &m);
		printf("конфигураций под GL ES 3: %d\n", m);
		if (m > 0) {
			EGLContext c = eglCreateContext(dpy, c0, EGL_NO_CONTEXT, c3);

			printf("  контекст GL ES 3: %s (0x%x)\n",
			       c != EGL_NO_CONTEXT ? "создан" : "отказ", eglGetError());
			if (c != EGL_NO_CONTEXT)
				eglDestroyContext(dpy, c);
		}
	}

	eglTerminate(dpy);
	wl_display_disconnect(wl);
	return 0;
}
