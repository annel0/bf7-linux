/*
 * BF7: acquire_wake_lock / release_wake_lock без службы ISystemSuspend.
 *
 * В Android 10+ libhardware_legacy берёт wakelock не у ядра, а у HIDL-службы
 * android.system.suspend@1.0::ISystemSuspend. У нас её нет, и fsd внутри
 * ccci_mdinit на первом же запросе модема ждал её вечно: раз в секунду
 * «getService: Trying again», а модем тем временем не проходил второе
 * рукопожатие (MD_BOOT_HS2_FAIL).
 *
 * Здесь — прежний, до-десятый путь: имя замка пишется в /sys/power/wake_lock
 * ядра и снимается через wake_unlock. Смысл тот же: пока fsd отвечает
 * модему, телефон не уснёт. Подключается через LD_PRELOAD поштучно.
 */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int zapisat(const char *put, const char *imya)
{
	int fd = open(put, O_WRONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = write(fd, imya, strlen(imya));
	close(fd);
	return n < 0 ? -1 : 0;
}

int acquire_wake_lock(int lock, const char *id)
{
	(void)lock;
	return zapisat("/sys/power/wake_lock", id);
}

int release_wake_lock(const char *id)
{
	return zapisat("/sys/power/wake_unlock", id);
}
