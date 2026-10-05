/*
 * Воспроизведение падения драйвера VA-API в песочнице.
 *
 * Firefox запирает процесс декодирования (RDD) в пустой каталог: андроидных
 * библиотек оттуда не видно. Здесь то же самое делается пространством
 * монтирования — поверх /system и /vendor кладётся пустая tmpfs.
 *
 * Ожидание до заплаты: SIGSEGV с переходом по нулевому адресу.
 * Ожидание после: честный отказ VA-API, процесс жив.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>

int main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);

	if (fd < 0) {
		perror("renderD128");
		return 2;
	}

	VADisplay d = vaGetDisplayDRM(fd);
	int major, minor;
	VAStatus st = vaInitialize(d, &major, &minor);

	if (st != VA_STATUS_SUCCESS) {
		printf("vaInitialize -> %d (%s)\n", st, vaErrorStr(st));
		return 1;
	}
	printf("VA-API %d.%d, поставщик: %s\n", major, minor, vaQueryVendorString(d));

	VAConfigID cfg;

	st = vaCreateConfig(d, VAProfileH264ConstrainedBaseline,
			    VAEntrypointVLD, NULL, 0, &cfg);
	printf("vaCreateConfig -> %d (%s)\n", st, vaErrorStr(st));
	if (st != VA_STATUS_SUCCESS)
		return 1;

	VASurfaceID s;

	st = vaCreateSurfaces(d, VA_RT_FORMAT_YUV420, 640, 480, &s, 1, NULL, 0);
	printf("vaCreateSurfaces -> %d (%s)\n", st, vaErrorStr(st));
	if (st != VA_STATUS_SUCCESS)
		return 1;

	VADRMPRIMESurfaceDescriptor dsc;

	st = vaExportSurfaceHandle(d, s, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
				   VA_EXPORT_SURFACE_READ_ONLY |
				   VA_EXPORT_SURFACE_SEPARATE_LAYERS, &dsc);
	printf("vaExportSurfaceHandle -> %d (%s)\n", st, vaErrorStr(st));
	if (st == VA_STATUS_SUCCESS)
		printf("  объектов %u, слоёв %u, fourcc %.4s\n",
		       dsc.num_objects, dsc.num_layers, (char *)&dsc.fourcc);

	printf("процесс дожил до конца\n");
	return 0;
}
