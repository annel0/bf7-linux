/*
 * BF7: правка строк расширений EGL и GL для чужих композиторов.
 *
 * Делает две вещи.
 *
 * 1. Прячет «устройственные» расширения EGL.
 *
 * wlroots 0.15 при создании рендерера сначала пробует платформу
 * EGL_PLATFORM_DEVICE_EXT и только не найдя устройства откатывается на GBM.
 * Устройства ему перечисляет мешовский поставщик glvnd, а создать на них
 * дисплей умеет только он же — наш hybris-EGL работает исключительно через
 * GBM. Получается тупик: wlroots уходит в «устройственную» ветку и падает,
 * а если убрать Mesa из списка поставщиков, то вместе с ней пропадает
 * EGL_EXT_platform_base, без которого wlroots отказывается работать вовсе.
 *
 * Поэтому Mesa оставляем, а из строки клиентских расширений вычёркиваем
 * ровно четыре имени, по которым wlroots решает лезть в устройства.
 * Строка отдаётся при eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS).
 *
 * 2. Выдаёт узел рендера для bf7-disp.
 *
 * Распределитель wlroots переоткрывает устройство показа, чтобы выделять на
 * нём буферы обмена. Он сначала спрашивает drmGetRenderDeviceNameFromFd(), а у
 * нашего card1 узла рендера нет — и тогда буферы выделяются «тупыми», из CMA,
 * которого на телефоне всего 16 МБ. Подставляем renderD128: буферы даёт
 * gralloc, а в панель они входят импортом PRIME — тем самым путём, которым уже
 * работает показ кадра без копирования.
 *
 * 3. Объявляет экспорт PRIME за pvrsrvkm.
 *
 * Распределитель wlroots перед тем, как брать буферы через GBM, спрашивает
 * drmGetCap(fd, DRM_CAP_PRIME). Наш узел рендера отвечает 0x0 — ни импорта,
 * ни экспорта, — и wlroots отступает на «тупые» буферы из CMA
 * (`render/allocator/gbm.c:183: PRIME export not supported`). Но способность
 * есть: наш бэкенд gbm-backend/pvr_gbm.c достаёт дескриптор dma-buf прямо из
 * ручки gralloc (find_dma_fd) и отдаёт его из bo_get_fd. Драйвер просто не
 * объявляет того, что умеет прослойка поверх него. Отвечаем за него 0x3
 * (импорт и экспорт) — ровно то, что отдаёт соседний bf7disp.
 *
 * 4. Дописывает GL_EXT_unpack_subimage.
 *
 * Рендерер wlroots требует это расширение, а драйвер PowerVR его не
 * объявляет — потому что в OpenGL ES 3.0 оно вошло в ядро стандарта и
 * перестало быть расширением. Мы получаем ES 3.2, то есть GL_UNPACK_ROW_LENGTH
 * и соседи работают по-настоящему; недостаёт только имени в строке. Это та же
 * болезнь, из-за которой glamor падал на первой текстуре.
 *
 * Сборка:
 *   gcc -shared -fPIC -O2 -o libbf7eglfilter.so eglfilter.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <xf86drm.h>

/*
 * Прячем ровно одно имя. Сначала пряталось четыре, и это выходило боком:
 * wlroots узнаёт свой узел DRM через eglQueryDisplayAttribEXT, которому нужен
 * EGL_EXT_device_query, а без узла она отключает wl_drm и не поднимает
 * linux-dmabuf — то самое, чем клиенты отдают буферы. Для ухода из
 * устройственной ветки достаточно спрятать EGL_EXT_platform_device: именно по
 * нему wlroots решает, пробовать ли EGL_PLATFORM_DEVICE_EXT.
 */
static const char *const прячем[] = {
	"EGL_EXT_platform_device",
	NULL
};

static int скрыто(const char *имя, size_t длина)
{
	for (int i = 0; прячем[i]; i++)
		if (strlen(прячем[i]) == длина && !memcmp(прячем[i], имя, длина))
			return 1;
	return 0;
}

const char *eglQueryString(EGLDisplay dpy, EGLint name)
{
	static const char *(*настоящий)(EGLDisplay, EGLint);
	static char *очищенная;

	if (!настоящий)
		настоящий = dlsym(RTLD_NEXT, "eglQueryString");
	if (!настоящий)
		return NULL;

	const char *исходная = настоящий(dpy, name);
	if (dpy != EGL_NO_DISPLAY || name != EGL_EXTENSIONS || !исходная)
		return исходная;

	if (очищенная)
		return очищенная;

	/* Собираем заново, пропуская вычёркнутые имена. */
	char *выход = malloc(strlen(исходная) + 1);
	if (!выход)
		return исходная;
	выход[0] = 0;

	size_t длина_выхода = 0;
	const char *p = исходная;
	while (*p) {
		while (*p == ' ')
			p++;
		const char *начало = p;
		while (*p && *p != ' ')
			p++;
		size_t длина = (size_t)(p - начало);
		if (!длина || скрыто(начало, длина))
			continue;
		if (длина_выхода)
			выход[длина_выхода++] = ' ';
		memcpy(выход + длина_выхода, начало, длина);
		длина_выхода += длина;
		выход[длина_выхода] = 0;
	}

	очищенная = выход;
	return очищенная;
}

/* Имена, которых в строке драйвера нет, хотя возможность есть. */
static const char *const дописываем[] = {
	"GL_EXT_unpack_subimage",
	NULL
};

const GLubyte *glGetString(GLenum name)
{
	static const GLubyte *(*настоящий)(GLenum);
	static char *дополненная;

	if (!настоящий)
		настоящий = dlsym(RTLD_NEXT, "glGetString");
	if (!настоящий)
		return NULL;

	const GLubyte *исходная = настоящий(name);
	if (name != GL_EXTENSIONS || !исходная)
		return исходная;

	if (дополненная)
		return (const GLubyte *)дополненная;

	const char *s = (const char *)исходная;
	size_t нужно = strlen(s) + 2;
	for (int i = 0; дописываем[i]; i++)
		нужно += strlen(дописываем[i]) + 1;

	char *выход = malloc(нужно);
	if (!выход)
		return исходная;

	size_t длина = (size_t)snprintf(выход, нужно, "%s", s);
	for (int i = 0; дописываем[i]; i++) {
		/* Проверяем как отдельное слово, чтобы не спутать с приставкой. */
		const char *п = strstr(s, дописываем[i]);
		int уже_есть = 0;
		while (п) {
			size_t дл = strlen(дописываем[i]);
			int слева = (п == s) || п[-1] == ' ';
			int справа = (п[дл] == 0) || п[дл] == ' ';
			if (слева && справа) { уже_есть = 1; break; }
			п = strstr(п + 1, дописываем[i]);
		}
		if (уже_есть)
			continue;
		длина += (size_t)snprintf(выход + длина, нужно - длина, " %s", дописываем[i]);
	}

	дополненная = выход;
	return (const GLubyte *)дополненная;
}

char *drmGetRenderDeviceNameFromFd(int fd)
{
	static char *(*настоящий)(int);

	if (!настоящий)
		настоящий = dlsym(RTLD_NEXT, "drmGetRenderDeviceNameFromFd");
	if (!настоящий)
		return NULL;

	char *имя = настоящий(fd);
	if (имя)
		return имя;

	/* Подставляем только для нашего дисплейного драйвера, чужие не трогаем. */
	drmVersionPtr в = drmGetVersion(fd);
	int наш = в && в->name && !strcmp(в->name, "bf7disp");
	if (в)
		drmFreeVersion(в);
	if (!наш)
		return NULL;

	return strdup("/dev/dri/renderD128");
}

int drmGetCap(int fd, uint64_t возможность, uint64_t *значение)
{
	static int (*настоящий)(int, uint64_t, uint64_t *);

	if (!настоящий)
		настоящий = dlsym(RTLD_NEXT, "drmGetCap");
	if (!настоящий)
		return -1;

	int r = настоящий(fd, возможность, значение);

	/* DRM_CAP_PRIME = 0x5; ответ 0 означает «ничего не умею». */
	if (возможность != 0x5 || !значение || *значение)
		return r;

	drmVersionPtr в = drmGetVersion(fd);
	int наш = в && в->name && !strcmp(в->name, "pvr");
	if (в)
		drmFreeVersion(в);
	if (!наш)
		return r;

	*значение = 0x3;	/* DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT */
	return 0;
}
