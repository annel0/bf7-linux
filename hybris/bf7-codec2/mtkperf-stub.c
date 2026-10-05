/*
 * Заглушка вместо libmtkperf_client_vendor.so.
 *
 * Настоящий клиент обращается к службе vendor.mediatek.hardware.mtkpower
 * через HIDL, то есть через hwbinder и диспетчер служб. Диспетчера у нас нет:
 * hwservicemanager падает в AccessControl, потому что ядро загружено с
 * security=none и SELinux отключён. В результате perf_lock_acq не
 * возвращается, поток-обработчик декодера застревает, и
 * C2MtkComponent::start() ждёт ответа вечно.
 *
 * Эти блокировки всего лишь просят систему подержать частоты повыше на время
 * декодирования — без них декодер работает, просто без подсказки планировщику.
 *
 * Собирается с -nostdlib, чтобы в библиотеке не было ни одной записи
 * DT_NEEDED: её грузит андроидный линковщик как замену вендорской.
 */

/* int perf_lock_acq(int handle, int duration, int *list, int num) */
int perf_lock_acq(int handle, int duration, int *list, int num)
{
	(void)duration;
	(void)list;
	(void)num;
	/* Ненулевой описатель: вызывающий проверяет его перед perf_lock_rel. */
	return handle ? handle : 1;
}

int perf_lock_rel(int handle)
{
	(void)handle;
	return 0;
}

int perf_cus_lock_hint(int hint, int duration)
{
	(void)hint;
	(void)duration;
	return 0;
}

int perf_lock_rel_async(int handle)
{
	(void)handle;
	return 0;
}
