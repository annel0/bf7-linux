/* Печатает, что лежит в bionic-слотах TLS 6 и 7 до и после инициализации EGL. */
#include <stdio.h>
#include <EGL/egl.h>

int main(void)
{
	void **tp = (void **)__builtin_thread_pointer();
	EGLDisplay dpy;
	EGLint a, b;

	printf("до  EGL: tp=%p tp[6]=%p tp[7]=%p\n", (void *)tp, tp[6], tp[7]);
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(dpy, &a, &b);
	printf("после EGL: tp[6]=%p tp[7]=%p\n", tp[6], tp[7]);
	return 0;
}
