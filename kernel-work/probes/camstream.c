/*
 * Поднять сенсор и запустить поток кадров, потом убедиться, что он правда
 * идёт: читать счётчик кадров сенсора прямо по шине.
 *
 * Порядок обязателен и выстрадан:
 *   SET_DRIVER (3107) -> OPEN (3105) -> ioctl X_CONTROL со сценарием.
 * Узел /dev/kd_camera_hw нельзя закрывать: на закрытии драйвер гасит такты и
 * сбрасывает все ножки, а вместе с ними и питание сенсора.
 *
 * Команды _IO(...) (T_OPEN, T_CLOSE) этим драйвером не принимаются вовсе:
 * обработчик отвечает -EFAULT всему, у чего _IOC_DIR == _IOC_NONE. Открытие
 * делается «свойством» 3105, а не своей командой.
 *
 * Сборка на телефоне: gcc -O1 -o camstream camstream.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define IMGSENSORMAGIC 'i'

#define KDIMGSENSORIOC_X_FEATURECONCTROL	_IOWR(IMGSENSORMAGIC, 15, struct featurectrl)
#define KDIMGSENSORIOC_X_CONTROL		_IOWR(IMGSENSORMAGIC, 20, struct sensorctrl)

#define SENSOR_FEATURE_OPEN			3105
#define SENSOR_FEATURE_CLOSE			3106
#define SENSOR_FEATURE_SET_DRIVER		3107

struct featurectrl {
	uint32_t InvokeCamera;
	uint32_t FeatureId;
	uint8_t *pFeaturePara;
	uint32_t *pFeatureParaLen;
};

struct sensorctrl {
	uint32_t InvokeCamera;
	uint32_t ScenarioId;
	void *pImageWindow;
	void *pSensorConfigData;
};

static int feature(int fd, uint32_t cam, uint32_t id, void *para, uint32_t len)
{
	uint32_t l = len;
	struct featurectrl f = {
		.InvokeCamera = cam,
		.FeatureId = id,
		.pFeaturePara = (uint8_t *)para,
		.pFeatureParaLen = &l,
	};

	return ioctl(fd, KDIMGSENSORIOC_X_FEATURECONCTROL, &f);
}

/* чтение регистра сенсора по шине, адрес регистра двухбайтовый */
static int reg16(int bus, int addr, int reg, unsigned char *out, int n)
{
	unsigned char a[2] = { (unsigned char)(reg >> 8), (unsigned char)reg };
	struct i2c_msg m[2] = {
		{ .addr = (unsigned short)addr, .flags = 0, .len = 2, .buf = a },
		{ .addr = (unsigned short)addr, .flags = I2C_M_RD,
		  .len = (unsigned short)n, .buf = out },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

	return ioctl(bus, I2C_RDWR, &d);
}

int main(int argc, char **argv)
{
	uint32_t cam = argc > 1 ? (uint32_t)atoi(argv[1]) : 0;
	uint32_t scenario = argc > 2 ? (uint32_t)atoi(argv[2]) : 0;
	const char *busname = argc > 3 ? argv[3] : "/dev/i2c-2";
	int addr = argc > 4 ? (int)strtol(argv[4], NULL, 0) : 0x10;
	int32_t drv = 0;
	unsigned char b[2];
	int fd, bus, i;

	fd = open("/dev/kd_camera_hw", O_RDWR);
	if (fd < 0) {
		perror("/dev/kd_camera_hw");
		return 1;
	}

	if (feature(fd, cam, SENSOR_FEATURE_SET_DRIVER, &drv, 4) < 0 || drv < 0) {
		printf("драйвер не подобран (%d): сенсор не отозвался\n", drv);
		return 1;
	}
	printf("драйвер подобран, номер %d\n", drv);

	/*
	 * Пустой указатель здесь не проходит: adopt_CAMERA_HW_FeatureControl
	 * отвечает -EFAULT на любой вызов с pFeaturePara == NULL, даже там, где
	 * параметров нет по смыслу. Даём буфер-пустышку.
	 */
	uint8_t nothing[4] = { 0 };

	if (feature(fd, cam, SENSOR_FEATURE_OPEN, nothing, 4) < 0) {
		printf("открытие сенсора: отказ (%s)\n", strerror(errno));
		return 1;
	}
	printf("сенсор открыт\n");

	struct sensorctrl c = {
		.InvokeCamera = cam,
		.ScenarioId = scenario,
		.pImageWindow = NULL,
		.pSensorConfigData = NULL,
	};

	if (ioctl(fd, KDIMGSENSORIOC_X_CONTROL, &c) < 0) {
		printf("запуск сценария %u: отказ (%s)\n",
		       scenario, strerror(errno));
		return 1;
	}
	printf("сценарий %u запущен\n", scenario);

	bus = open(busname, O_RDWR);
	if (bus < 0) {
		perror(busname);
		return 1;
	}

	if (reg16(bus, addr, 0x0100, b, 1) >= 0)
		printf("регистр 0x0100 (поток): 0x%02x — %s\n",
		       b[0], b[0] ? "включён" : "выключен");

	printf("счётчик кадров (регистр 0x0005), десять замеров:\n  ");
	for (i = 0; i < 10; i++) {
		if (reg16(bus, addr, 0x0005, b, 1) >= 0)
			printf("%02x ", b[0]);
		else
			printf("-- ");
		fflush(stdout);
		usleep(60000);
	}
	printf("\n");

	printf("узел остаётся открытым, жду сигнала (Ctrl+C или kill)\n");
	fflush(stdout);
	pause();
	return 0;
}
