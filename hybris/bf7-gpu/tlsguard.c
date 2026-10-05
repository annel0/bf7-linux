/*
 * Резерв статического TLS для libhybris на glibc.
 *
 * Андроидный libEGL хранит указатели на gl_hooks прямо в слотах
 * bionic-TLS: __get_tls()[TLS_SLOT_OPENGL_API=6] и [TLS_SLOT_OPENGL=7],
 * то есть по смещениям 48 и 56 от tpidr_el0. В процессе glibc там лежит
 * начало статического TLS-блока — thread-local переменные того модуля,
 * которому достался первый слот (обычно самой libc: указатель локали и
 * прочее). Любой printf с плавающей точкой после eglInitialize падает.
 *
 * Библиотека объявляет достаточно большой __thread-массив и за счёт
 * LD_PRELOAD получает первый слот статического TLS, закрывая собой
 * смещения 16..272. Всё, что пишет туда андроид, попадает в этот массив.
 *
 * Использование: LD_PRELOAD=/usr/local/lib/libbf7tlsguard.so.
 * Работает только если сама программа не объявляет __thread-переменных
 * (тогда первый слот забирает она); проверить можно bf7-tlscheck.
 */
#include <stdio.h>
#include <stdlib.h>

__thread unsigned long bf7_tls_guard[32] __attribute__((visibility("default"), used));

unsigned long bf7_tls_guard_offset(void)
{
	return (unsigned long)((char *)bf7_tls_guard - (char *)__builtin_thread_pointer());
}

__attribute__((constructor)) static void bf7_tls_guard_init(void)
{
	unsigned long off = bf7_tls_guard_offset();

	bf7_tls_guard[0] = 1;	/* чтобы блок точно был размещён */
	if (getenv("BF7_TLS_GUARD_VERBOSE"))
		fprintf(stderr, "bf7 tls guard: смещение %lu..%lu от tp\n",
			off, off + sizeof bf7_tls_guard);
	if (off > 48 || off + sizeof bf7_tls_guard < 64)
		fprintf(stderr,
			"bf7 tls guard: ВНИМАНИЕ, блок %lu..%lu не закрывает слоты 48 и 56\n",
			off, off + sizeof bf7_tls_guard);
}
