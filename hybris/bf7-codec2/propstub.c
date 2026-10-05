/*
 * Подстановка системных свойств Android для нативных блобов.
 *
 * Init у нас не запускается, поэтому /dev/__properties__ не создаётся и вся
 * подсистема свойств bionic мертва. Для большинства вызовов это безобидно, но
 * libhidlbase перед обращением к любой HIDL-службе крутит цикл
 *
 *	while (GetProperty("hwservicemanager.ready", "") != "true")
 *		WaitForProperty("hwservicemanager.ready", "true", 1s);
 *
 * а WaitForProperty без области свойств возвращается немедленно — выходит
 * плотный цикл, который съедает ядро и никогда не завершается.
 *
 * Заглушка подменяет нижний слой bionic (__system_property_*) собственной
 * таблицей, набранной из настоящих build.prop плюс файл наших переопределений.
 * Настоящую область свойств строить не получится: для неё нужен
 * property_info, который собирает init из *_property_contexts.
 *
 * Подключается через LD_PRELOAD поштучно, глобально ничего не меняется.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* В bionic эти пределы включают завершающий ноль — буфер вызывающего ровно
 * такого размера, поэтому и наши поля должны быть такими же. */
#define PROP_NAME_MAX	32
#define PROP_VALUE_MAX	92
#define MAX_PROPS	4096

struct bf7_prop {
	char name[PROP_NAME_MAX];
	char value[PROP_VALUE_MAX];
	uint32_t serial;
};

static struct bf7_prop props[MAX_PROPS];
static int nprops;
static int loaded;

static struct bf7_prop *lookup(const char *name)
{
	int i;

	for (i = 0; i < nprops; i++)
		if (!strcmp(props[i].name, name))
			return &props[i];
	return NULL;
}

static void put(const char *name, const char *value)
{
	struct bf7_prop *p = lookup(name);

	if (!p) {
		if (nprops >= MAX_PROPS)
			return;
		p = &props[nprops++];
		snprintf(p->name, sizeof(p->name), "%s", name);
	}
	snprintf(p->value, sizeof(p->value), "%s", value);
	p->serial += 2;		/* младший бит bionic держит под «идёт запись» */
}

static void load_file(const char *path)
{
	char line[512];
	FILE *f = fopen(path, "r");

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *eq, *end;
		char *s = line;

		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == '#' || *s == '\n' || !*s)
			continue;
		eq = strchr(s, '=');
		if (!eq)
			continue;
		*eq = '\0';
		end = s + strlen(s);
		while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';
		{
			char *v = eq + 1;

			end = v + strlen(v);
			while (end > v && (end[-1] == '\n' || end[-1] == '\r' ||
					   end[-1] == ' ' || end[-1] == '\t'))
				*--end = '\0';
			put(s, v);
		}
	}
	fclose(f);
}

static void init_once(void)
{
	if (loaded)
		return;
	loaded = 1;

	/* BF7_PROP_MINIMAL=1 — не подхватывать build.prop, только наши
	 * переопределения. Нужно для блобов, которые от настоящих значений
	 * уходят на неработающую у нас ветку. */
	if (!getenv("BF7_PROP_MINIMAL")) {
		load_file("/system/build.prop");
		load_file("/system/etc/prop.default");
		load_file("/vendor/build.prop");
		load_file("/vendor/default.prop");
		load_file("/vendor/odm/etc/build.prop");
	}

	/* Наши переопределения идут последними и побеждают. */
	load_file("/vendor.local/etc/bf7.prop");

	/* Без этого libhidlbase уходит в вечный цикл ожидания диспетчера. */
	if (!lookup("hwservicemanager.ready"))
		put("hwservicemanager.ready", "true");
}

/* --- нижний слой bionic --- */

int __system_properties_init(void)
{
	init_once();
	return 0;
}

int __system_property_area_init(void)
{
	init_once();
	return 0;
}

const void *__system_property_find(const char *name)
{
	init_once();
	return lookup(name);
}

int __system_property_get(const char *name, char *value)
{
	struct bf7_prop *p;

	init_once();
	p = lookup(name);
	if (!p) {
		value[0] = '\0';
		return 0;
	}
	strcpy(value, p->value);
	return (int)strlen(value);
}

int __system_property_set(const char *name, const char *value)
{
	init_once();
	put(name, value);
	return 0;
}

void __system_property_read_callback(const void *pi,
	void (*cb)(void *cookie, const char *name, const char *value,
		   uint32_t serial), void *cookie)
{
	const struct bf7_prop *p = (const struct bf7_prop *)pi;

	if (!p || !cb)
		return;
	cb(cookie, p->name, p->value, p->serial);
}

int __system_property_read(const void *pi, char *name, char *value)
{
	const struct bf7_prop *p = (const struct bf7_prop *)pi;

	if (!p)
		return 0;
	if (name)
		strcpy(name, p->name);
	if (value)
		strcpy(value, p->value);
	return (int)strlen(p->value);
}

uint32_t __system_property_serial(const void *pi)
{
	const struct bf7_prop *p = (const struct bf7_prop *)pi;

	return p ? p->serial : 0;
}

/*
 * Ждать нечего: таблица неизменна. Возвращаем «истёк таймаут», иначе вызывающий
 * снова уйдёт в цикл — ровно в ту поломку, ради которой всё это написано.
 */
int __system_property_wait(const void *pi, uint32_t old_serial,
			   uint32_t *new_serial, const struct timespec *ts)
{
	const struct bf7_prop *p = (const struct bf7_prop *)pi;

	(void)ts;
	if (new_serial)
		*new_serial = p ? p->serial : old_serial;
	return 0;
}

uint32_t __system_property_wait_any(uint32_t old_serial)
{
	return old_serial;
}

uint32_t __system_property_area_serial(void)
{
	init_once();
	return 1;
}

int __system_property_foreach(void (*cb)(const void *pi, void *cookie),
			      void *cookie)
{
	int i;

	init_once();
	for (i = 0; i < nprops; i++)
		cb(&props[i], cookie);
	return 0;
}

const void *__system_property_find_nth(unsigned n)
{
	init_once();
	return (int)n < nprops ? &props[n] : NULL;
}

int __system_property_update(void *pi, const char *value, unsigned len)
{
	struct bf7_prop *p = (struct bf7_prop *)pi;

	(void)len;
	if (!p)
		return -1;
	snprintf(p->value, sizeof(p->value), "%s", value);
	p->serial += 2;
	return 0;
}

int __system_property_add(const char *name, unsigned namelen,
			  const char *value, unsigned valuelen)
{
	char n[PROP_NAME_MAX], v[PROP_VALUE_MAX];

	init_once();
	if (namelen > PROP_NAME_MAX - 1)
		namelen = PROP_NAME_MAX - 1;
	if (valuelen > PROP_VALUE_MAX - 1)
		valuelen = PROP_VALUE_MAX - 1;
	memcpy(n, name, namelen);
	n[namelen] = '\0';
	memcpy(v, value, valuelen);
	v[valuelen] = '\0';
	put(n, v);
	return 0;
}
