/*
 * Поднять сенсор камеры и прочитать его номер по I2C — с правильным
 * порядком вызовов.
 *
 * Прошлая проба звала CHECK_IS_ALIVE сразу и уронила ядро: имя драйвера
 * сенсора было ещё не заполнено, а imgsensor_hw_power зовёт с ним strstr.
 * Сперва обязателен SET_DRIVER — он выбирает драйвер и заполняет имя.
 *
 * Длины буфера проверяет check_length_of_para: для SET_DRIVER ровно 4.
 *
 * Ждём: S5K3L6 = 0x30C6 (основная), HI556 = 0x0556 (фронтальная).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define SENSOR_FEATURE_SET_DRIVER	3107
#define SENSOR_FEATURE_CHECK_IS_ALIVE	3108

struct feature_control {
	uint32_t InvokeCamera;
	uint32_t FeatureId;
	uint8_t *pFeaturePara;
	uint32_t *pFeatureParaLen;
};

#define KDIMGSENSORIOC_X_FEATURECONCTROL \
	_IOWR('i', 15, struct feature_control)

static int feature(int fd, uint32_t cam, uint32_t id, void *para, uint32_t len)
{
	uint32_t l = len;
	struct feature_control fc = {
		.InvokeCamera = cam,
		.FeatureId = id,
		.pFeaturePara = (uint8_t *)para,
		.pFeatureParaLen = &l,
	};

	return ioctl(fd, KDIMGSENSORIOC_X_FEATURECONCTROL, &fc);
}

int main(void)
{
	int fd = open("/dev/kd_camera_hw", O_RDWR);

	if (fd < 0) {
		perror("/dev/kd_camera_hw");
		return 1;
	}

	for (uint32_t cam = 0; cam < 2; cam++) {
		int32_t drv = 0;
		int r;

		printf("\nкамера %u (%s):\n", cam,
		       cam ? "фронтальная, ждём HI556 = 0x0556"
			   : "основная, ждём S5K3L6 = 0x30C6");

		r = feature(fd, cam, SENSOR_FEATURE_SET_DRIVER, &drv, 4);
		if (r < 0) {
			printf("  выбор драйвера: отказ (%s)\n", strerror(errno));
			continue;
		}
		printf("  выбор драйвера: принят, номер драйвера %d\n", drv);
		fflush(stdout);

		if (drv < 0) {
			printf("  драйвер не подобран — сенсор не отозвался, питание не подаём\n");
			continue;
		}

		uint8_t para[4] = { 0 };

		r = feature(fd, cam, SENSOR_FEATURE_CHECK_IS_ALIVE, para, 4);
		printf("  проверка живости: %s\n",
		       r < 0 ? strerror(errno) : "принята");
		fflush(stdout);
	}
	close(fd);
	printf("\nпроба дожила до конца\n");
	return 0;
}
