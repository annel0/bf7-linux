/*
 * Повторяет шаг в шаг то, что делает GDK Wayland при заведении GL, чтобы
 * понять, на чём именно оно спотыкается. Браузер жалуется:
 *   «GTK failed to initialize GL: No available configurations for the given
 *    pixel format»
 * а проба eglconfigs при этом показывает 67 подходящих конфигов. Значит,
 * дисплей у GDK другой — оно берёт его через платформу Wayland.
 *
 * Сборка:  cc -O2 -o gdkeglprobe gdkeglprobe.c -lEGL -lwayland-client
 */
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-client.h>

int main(void)
{
	struct wl_display *wl;
	EGLDisplay dpy = EGL_NO_DISPLAY;
	EGLint major = 0, minor = 0, count = 0;
	const char *rasshireniya;
	PFNEGLGETPLATFORMDISPLAYEXTPROC cherez_platformu;

	wl = wl_display_connect(NULL);
	printf("wl_display: %p\n", (void *)wl);
	if (!wl)
		return 1;

	rasshireniya = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	printf("расширения без дисплея: %s\n", rasshireniya ? rasshireniya : "(нет)");

	cherez_platformu = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");
	printf("eglGetPlatformDisplayEXT: %s\n", cherez_platformu ? "есть" : "нет");

	if (cherez_platformu && rasshireniya &&
	    strstr(rasshireniya, "EGL_EXT_platform_wayland")) {
		dpy = cherez_platformu(EGL_PLATFORM_WAYLAND_EXT, wl, NULL);
		printf("через платформу Wayland -> %p\n", (void *)dpy);
	}
	if (dpy == EGL_NO_DISPLAY) {
		dpy = eglGetDisplay((EGLNativeDisplayType)wl);
		printf("eglGetDisplay(wl_display) -> %p\n", (void *)dpy);
	}
	if (dpy == EGL_NO_DISPLAY) {
		printf("дисплея нет вовсе\n");
		return 1;
	}

	if (!eglInitialize(dpy, &major, &minor)) {
		printf("eglInitialize отказал: 0x%x\n", eglGetError());
		return 1;
	}
	printf("EGL %d.%d, поставщик: %s\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR));
	printf("клиентские API: %s\n", eglQueryString(dpy, EGL_CLIENT_APIS));

	printf("eglBindAPI(OPENGL): %s (0x%x)\n",
	       eglBindAPI(EGL_OPENGL_API) ? "да" : "нет", eglGetError());
	printf("eglBindAPI(OPENGL_ES): %s\n",
	       eglBindAPI(EGL_OPENGL_ES_API) ? "да" : "нет");

	if (!eglGetConfigs(dpy, NULL, 0, &count))
		printf("eglGetConfigs отказал: 0x%x\n", eglGetError());
	else
		printf("всего конфигов на этом дисплее: %d\n", count);

	{
		/* Ровно те признаки, что просит GDK (gdkglcontext-wayland.c). */
		EGLint prosim[] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 0,
			EGL_NONE
		};
		count = 0;
		if (!eglChooseConfig(dpy, prosim, NULL, 0, &count))
			printf("eglChooseConfig отказал: 0x%x\n", eglGetError());
		printf("подошло по признакам GDK: %d\n", count);
	}
	return 0;
}
