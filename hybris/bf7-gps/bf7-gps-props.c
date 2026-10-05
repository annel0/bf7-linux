/*
 * BF7: подложка свойств Android для mnld (демон GPS MediaTek из /vendor).
 *
 * mnld зовёт property_get из libcutils, а службы свойств Android у нас нет.
 * Без persist.vendor.connsys.chipid его chip_detector двадцать раз пишет
 * «get chip id fail», конфигурация выходит без пути к устройству (dev_gps
 * пустой), и /dev/stpgps он не открывает вовсе. На Android это свойство
 * кладёт wmt_loader (формат 0x%04x); у нас он пишет его в пустоту —
 * значения взяты из его же журнала: consys 0x6761, A-die 0x6631.
 * Каждое новое свойство здесь найдено по журналу BF7_GPS_PROPS_LOG.
 *
 * Отдельно от fake-android-props.so (там только vendor.connsys.driver.ready
 * для WMT): Wi-Fi работает, его подложку не трогаем.
 *
 * Подгружается в процесс на bionic через LD_PRELOAD, собирается NDK.
 * BF7_GPS_PROPS_LOG=файл — дописывать туда каждый запрошенный ключ и ответ.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void zapisat(const char *key, const char *val)
{
	const char *put_ = getenv("BF7_GPS_PROPS_LOG");
	int fd;

	if (!put_)
		return;
	fd = open(put_, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd >= 0) {
		dprintf(fd, "%s=%s\n", key, val);
		close(fd);
	}
}
static int same(const char *a, const char *b)
{
	while (*a && *b && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int put(char *dst, const char *src)
{
	int n = 0;

	while (src[n]) {
		dst[n] = src[n];
		n++;
	}
	dst[n] = 0;
	return n;
}

static const char *const svojstva[][2] = {
	{ "persist.vendor.connsys.chipid", "0x6761" },
	{ "vendor.connsys.adie.chipid", "0x6631" },
	{ "vendor.connsys.driver.ready", "yes" },
	/*
	 * Признак готовности NVRAM (на Android ставит nvram_daemon). Без него
	 * mnld 20 с ждёт в mtk_gps_sys_init («nvram_init_val not Ready») и
	 * работает без калибровки из /mnt/vendor/nvdata/APCFG/APRDEB/GPS.
	 */
	{ "vendor.service.nvram_init", "Ready" },
};

int property_get(const char *key, char *value, const char *def)
{
	int n;

	for (unsigned i = 0; i < sizeof(svojstva) / sizeof(svojstva[0]); i++)
		if (same(key, svojstva[i][0])) {
			n = put(value, svojstva[i][1]);
			zapisat(key, value);
			return n;
		}
	n = put(value, def ? def : "");
	zapisat(key, value);
	return n;
}

int property_set(const char *key, const char *value)
{
	(void)key;
	(void)value;
	return 0;
}
