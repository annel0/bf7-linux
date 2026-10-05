/* Печатает все EGLConfig, которые отдаёт драйвер. */
#include <stdio.h>
#include <EGL/egl.h>

static const struct { EGLint a; const char *n; } A[] = {
	{ EGL_CONFIG_ID, "id" }, { EGL_BUFFER_SIZE, "buf" },
	{ EGL_RED_SIZE, "r" }, { EGL_GREEN_SIZE, "g" }, { EGL_BLUE_SIZE, "b" },
	{ EGL_ALPHA_SIZE, "a" }, { EGL_DEPTH_SIZE, "depth" }, { EGL_STENCIL_SIZE, "stencil" },
	{ EGL_SAMPLES, "samples" }, { EGL_SURFACE_TYPE, "surf" },
	{ EGL_RENDERABLE_TYPE, "rend" }, { EGL_NATIVE_VISUAL_ID, "visual" },
	{ EGL_NATIVE_RENDERABLE, "natrend" }, { EGL_CONFORMANT, "conf" },
};

int main(void)
{
	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint maj, min, n = 0;
	EGLConfig cfgs[128];

	setvbuf(stdout, NULL, _IONBF, 0);
	eglInitialize(dpy, &maj, &min);
	eglGetConfigs(dpy, cfgs, 128, &n);
	printf("всего конфигов: %d\n", n);
	for (int i = 0; i < n; i++) {
		printf(" ");
		for (unsigned k = 0; k < sizeof A / sizeof A[0]; k++) {
			EGLint v = 0;
			eglGetConfigAttrib(dpy, cfgs[i], A[k].a, &v);
			if (A[k].a == EGL_SURFACE_TYPE || A[k].a == EGL_RENDERABLE_TYPE ||
			    A[k].a == EGL_CONFORMANT)
				printf(" %s=0x%x", A[k].n, v);
			else
				printf(" %s=%d", A[k].n, v);
		}
		printf("\n");
	}
	return 0;
}
