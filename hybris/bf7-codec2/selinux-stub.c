/*
 * Разрешающая заглушка libselinux для hwservicemanager.
 *
 * Ядро загружено с security=none, selinuxfs нет, и настоящая libselinux не
 * может открыть состояние: hwservicemanager падает в
 * AccessControl::AccessControl(). Без него нет диспетчера служб HIDL, а
 * значит нет ни gralloc4, ни службы выделения буферов — и декодер виснет на
 * выделении выходного кадра.
 *
 * Заглушка отвечает «разрешено» на любую проверку. Это осознанный размен:
 * разграничения доступа Android у нас всё равно нет, а весь стек работает от
 * root в одном доверенном окружении.
 *
 * Подставляется только конкретному процессу через LD_PRELOAD, глобально
 * libselinux не подменяется.
 */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static char *dup_context(const char *s)
{
	char *p = (char *)malloc(strlen(s) + 1);

	if (p)
		strcpy(p, s);
	return p;
}

int selinux_status_open(int fallback)
{
	(void)fallback;
	return 0;
}

int getcon(char **con)
{
	if (!con)
		return -1;
	*con = dup_context("u:r:hwservicemanager:s0");
	return *con ? 0 : -1;
}

int getpidcon(pid_t pid, char **con)
{
	(void)pid;
	if (!con)
		return -1;
	*con = dup_context("u:r:vendor_init:s0");
	return *con ? 0 : -1;
}

void freecon(char *con)
{
	free(con);
}

int selinux_check_access(const char *scon, const char *tcon, const char *cls,
			 const char *perm, void *aux)
{
	(void)scon; (void)tcon; (void)cls; (void)perm; (void)aux;
	return 0;	/* разрешено */
}

void *selinux_android_hw_service_context_handle(void)
{
	/* Непрозрачный описатель; наш selabel_lookup его не разыменовывает. */
	static int handle;

	return &handle;
}

int selabel_lookup(void *handle, char **con, const char *key, int type)
{
	(void)handle; (void)key; (void)type;
	if (!con)
		return -1;
	*con = dup_context("u:object_r:default_android_hwservice:s0");
	return *con ? 0 : -1;
}

void selinux_set_callback(int type, void *cb)
{
	(void)type; (void)cb;
}

/* hwservicemanager импортирует это как функцию (FUNC UND), не как объект. */
int selinux_log_callback(int type, const char *fmt, ...)
{
	(void)type; (void)fmt;
	return 0;
}
