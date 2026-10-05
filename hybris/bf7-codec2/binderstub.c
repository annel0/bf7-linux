/*
 * Отключение запроса контекста безопасности при регистрации диспетчера binder.
 *
 * ProcessState::becomeContextManager() сначала пробует
 * BINDER_SET_CONTEXT_MGR_EXT с флагом FLAT_BINDER_FLAG_TXN_SECURITY_CTX, и
 * только при отказе откатывается на BINDER_SET_CONTEXT_MGR. Флаг заставляет
 * ядро прикладывать к каждой транзакции контекст SELinux:
 *
 *	security_secid_to_secctx() -> -EOPNOTSUPP   (LSM нет, security=none)
 *	-> BR_FAILED_REPLY, binder.c:3280
 *
 * То есть при включённом флаге к диспетчеру не проходит ни одна транзакция.
 * Заглушка отвечает отказом на расширенный ioctl, и libhwbinder сам уходит на
 * обычный, которому контекст не нужен.
 *
 * Альтернатива — включить SELinux в командной строке ядра; тогда заглушка не
 * нужна. Здесь она позволяет обойтись без перепрошивки.
 *
 * Подключается через LD_PRELOAD только процессам-диспетчерам.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

struct bf7_flat_binder_object {
	uint32_t type;
	uint32_t flags;
	uint64_t ptr;
	uint64_t cookie;
};

#define BF7_BINDER_SET_CONTEXT_MGR_EXT \
	_IOW('b', 13, struct bf7_flat_binder_object)

int ioctl(int fd, int request, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	if ((unsigned int)request == (unsigned int)BF7_BINDER_SET_CONTEXT_MGR_EXT) {
		errno = EINVAL;
		return -1;
	}
	return (int)syscall(__NR_ioctl, fd, request, arg);
}
