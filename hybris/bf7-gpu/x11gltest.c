/*
 * Проба платформы EGL «x11»: аппаратный GL в обычном окне X11.
 *
 * Делает ровно то же, что сделал бы Firefox: открывает окно, просит у EGL
 * дисплей без указания платформы (eglGetDisplay(EGL_DEFAULT_DISPLAY)) и
 * создаёт оконную поверхность по идентификатору окна X11. Если платформа
 * работает, на экране будет меняться цвет, а GL_RENDERER назовёт PowerVR.
 *
 * Запускать так:
 *   DISPLAY=:1 HYBRIS_EGLPLATFORM=x11 ./x11gltest [секунд]
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 320
#define H 240

static double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	double seconds = argc > 1 ? atof(argv[1]) : 5.0;
	Display *xdpy = XOpenDisplay(NULL);

	if (!xdpy) {
		fprintf(stderr, "нет подключения к X (DISPLAY=%s)\n",
			getenv("DISPLAY") ? getenv("DISPLAY") : "не задан");
		return 1;
	}

	int screen = DefaultScreen(xdpy);
	Window win = XCreateSimpleWindow(xdpy, RootWindow(xdpy, screen),
					 40, 40, W, H, 0,
					 BlackPixel(xdpy, screen),
					 BlackPixel(xdpy, screen));

	/* XStoreName берёт Latin-1; для кириллицы нужно свойство UTF8_STRING. */
	{
		const char *title = "проба GL на X11";

		XChangeProperty(xdpy, win, XInternAtom(xdpy, "_NET_WM_NAME", False),
				XInternAtom(xdpy, "UTF8_STRING", False), 8,
				PropModeReplace,
				(const unsigned char *)title, strlen(title));
	}
	XMapWindow(xdpy, win);
	XSync(xdpy, False);

	/*
	 * Два способа спросить дисплей. Без переменной — как Firefox, платформу
	 * не называя. С BF7_PLATFORM_X11=1 — как Qt и SDL, называя её явно:
	 * это другая ветка в диспетчере, и проверять её надо отдельно.
	 */
	EGLDisplay dpy;

	if (getenv("BF7_PLATFORM_X11")) {
		PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
			(PFNEGLGETPLATFORMDISPLAYEXTPROC)
			eglGetProcAddress("eglGetPlatformDisplayEXT");

		if (!get_platform_display) {
			fprintf(stderr, "eglGetPlatformDisplayEXT недоступна\n");
			return 1;
		}
		printf("спрашиваю платформу X11 явно\n");
		dpy = get_platform_display(EGL_PLATFORM_X11_KHR, xdpy, NULL);
	} else {
		printf("платформу не называю (как Firefox)\n");
		dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	}

	if (dpy == EGL_NO_DISPLAY) {
		fprintf(stderr, "eglGetDisplay отказал\n");
		return 1;
	}

	EGLint major = 0, minor = 0;

	if (!eglInitialize(dpy, &major, &minor)) {
		fprintf(stderr, "eglInitialize -> 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d, поставщик: %s\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR));

	static const EGLint cfg_attrs[] = {
		EGL_SURFACE_TYPE,	EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE,		8,
		EGL_GREEN_SIZE,		8,
		EGL_BLUE_SIZE,		8,
		EGL_NONE
	};
	EGLConfig cfg;
	EGLint n = 0;

	if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1) {
		fprintf(stderr, "подходящей конфигурации нет: 0x%x\n",
			eglGetError());
		return 1;
	}

	EGLSurface surf = eglCreateWindowSurface(dpy, cfg,
						 (EGLNativeWindowType)win, NULL);

	if (surf == EGL_NO_SURFACE) {
		fprintf(stderr, "eglCreateWindowSurface -> 0x%x\n",
			eglGetError());
		return 1;
	}

	static const EGLint ctx_attrs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
	};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);

	if (ctx == EGL_NO_CONTEXT) {
		fprintf(stderr, "eglCreateContext -> 0x%x\n", eglGetError());
		return 1;
	}
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		fprintf(stderr, "eglMakeCurrent -> 0x%x\n", eglGetError());
		return 1;
	}

	/*
	 * Список расширений дисплея. Для видео без копий нужны два:
	 * EGL_EXT_image_dma_buf_import (втянуть кадр декодера дескриптором) и
	 * GL_OES_EGL_image_external (прочитать его как текстуру YUV).
	 */
	{
		const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
		const char *glext = (const char *)glGetString(GL_EXTENSIONS);

		printf("ввоз dma-buf:   %s\n",
		       ext && strstr(ext, "EGL_EXT_image_dma_buf_import") ?
		       "есть" : "НЕТ");
		printf("внешняя текстура: %s\n",
		       glext && strstr(glext, "GL_OES_EGL_image_external") ?
		       "есть" : "НЕТ");
	}

	/*
	 * Какие форматы EGL готов втянуть дескриптором. Без этого списка
	 * непонятно, почему проигрыватель получает EGL_BAD_MATCH на плоскости
	 * кадра: то ли формат не тот, то ли дело в чём-то ещё.
	 */
	{
		PFNEGLQUERYDMABUFFORMATSEXTPROC q =
			(PFNEGLQUERYDMABUFFORMATSEXTPROC)
			eglGetProcAddress("eglQueryDmaBufFormatsEXT");

		if (!q) {
			printf("список форматов dma-buf: функции нет\n");
		} else {
			EGLint total = 0;

			if (q(dpy, 0, NULL, &total) && total > 0) {
				EGLint *fmts = calloc(total, sizeof *fmts);

				q(dpy, total, fmts, &total);
				printf("форматов dma-buf: %d\n", total);
				for (int i = 0; i < total; i++) {
					char t[5] = {
						(char)(fmts[i] & 0xff),
						(char)((fmts[i] >> 8) & 0xff),
						(char)((fmts[i] >> 16) & 0xff),
						(char)((fmts[i] >> 24) & 0xff), 0
					};
					printf("  %s (0x%08x)\n", t, fmts[i]);
				}
				free(fmts);
			} else {
				printf("список форматов dma-buf: пуст\n");
			}
		}
	}

	printf("GL_VENDOR:   %s\n", (const char *)glGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));
	printf("GL_VERSION:  %s\n", (const char *)glGetString(GL_VERSION));

	double start = now_s();
	unsigned frames = 0;

	while (now_s() - start < seconds) {
		double t = now_s() - start;

		glViewport(0, 0, W, H);
		glClearColor((float)(0.5 + 0.5 * __builtin_sin(t * 2.0)),
			     (float)(0.5 + 0.5 * __builtin_sin(t * 2.7)),
			     (float)(0.5 + 0.5 * __builtin_sin(t * 3.3)),
			     1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (!eglSwapBuffers(dpy, surf)) {
			fprintf(stderr, "eglSwapBuffers -> 0x%x на кадре %u\n",
				eglGetError(), frames);
			return 1;
		}
		frames++;
	}

	printf("ИТОГ: кадров %u за %.1f с (%.1f в секунду)\n",
	       frames, seconds, frames / seconds);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	XDestroyWindow(xdpy, win);
	XCloseDisplay(xdpy);
	return 0;
}
