/*
 * Слежение за вызовами EGL: печатает аргументы и коды возврата в stderr.
 *
 * Нужно, чтобы понять, чем именно Firefox не угодил нашему EGL: сам он
 * сообщает только «Failed to create EGLContext!: 0x300c», не показывая
 * запрошенных атрибутов.
 *
 * Подключается через LD_PRELOAD перед libEGL; настоящие функции берутся
 * через RTLD_NEXT.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>

static void log_attribs(const char *what, const EGLint *a)
{
	fprintf(stderr, "  %s:", what);
	if (!a) {
		fprintf(stderr, " (нет)\n");
		return;
	}
	for (int i = 0; a[i] != EGL_NONE; i += 2)
		fprintf(stderr, " 0x%04x=0x%04x", a[i], a[i + 1]);
	fprintf(stderr, "\n");
}

EGLBoolean eglBindAPI(EGLenum api)
{
	static EGLBoolean (*real)(EGLenum);

	if (!real)
		real = dlsym(RTLD_NEXT, "eglBindAPI");

	EGLBoolean r = real(api);

	fprintf(stderr, "[eglspy] eglBindAPI(0x%04x) -> %d%s\n", api, r,
		api == 0x30A2 ? "  (EGL_OPENGL_API — настольный GL)" :
		api == 0x30A0 ? "  (EGL_OPENGL_ES_API)" : "");
	return r;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share,
			    const EGLint *attrib_list)
{
	static EGLContext (*real)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);

	if (!real)
		real = dlsym(RTLD_NEXT, "eglCreateContext");

	fprintf(stderr, "[eglspy] eglCreateContext(config=%p, share=%p)\n",
		(void *)cfg, (void *)share);
	log_attribs("атрибуты", attrib_list);

	EGLContext c = real(dpy, cfg, share, attrib_list);

	fprintf(stderr, "[eglspy]   -> %p, ошибка 0x%04x\n", (void *)c, eglGetError());
	return c;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
			   EGLConfig *configs, EGLint size, EGLint *num)
{
	static EGLBoolean (*real)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);

	if (!real)
		real = dlsym(RTLD_NEXT, "eglChooseConfig");

	EGLBoolean r = real(dpy, attrib_list, configs, size, num);

	fprintf(stderr, "[eglspy] eglChooseConfig -> %d, найдено %d\n", r, num ? *num : -1);
	log_attribs("запрошено", attrib_list);
	return r;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, const EGLint *attrib_list)
{
	static EGLSurface (*real)(EGLDisplay, EGLConfig, const EGLint *);

	if (!real)
		real = dlsym(RTLD_NEXT, "eglCreatePbufferSurface");

	EGLSurface s = real(dpy, cfg, attrib_list);

	fprintf(stderr, "[eglspy] eglCreatePbufferSurface -> %p, ошибка 0x%04x\n",
		(void *)s, eglGetError());
	log_attribs("атрибуты", attrib_list);
	return s;
}
