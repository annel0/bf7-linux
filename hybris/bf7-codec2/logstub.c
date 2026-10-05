/*
 * Перенаправление журнала Android в stderr.
 *
 * logd без init не получает свои сокеты от системы, поэтому весь вывод ALOGE
 * и подобных просто теряется: блоб падает или отказывает молча, и причину не
 * видно. Заглушка перехватывает нижний слой liblog и печатает всё в stderr.
 *
 * Подключается через LD_PRELOAD поштучно.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static char prio_char(int prio)
{
	static const char map[] = "??VDIWEF?";

	return (prio >= 0 && prio <= 8) ? map[prio] : '?';
}

static void emit(int prio, const char *tag, const char *msg)
{
	char buf[2048];
	int n = snprintf(buf, sizeof(buf), "[%c/%s] %s\n",
			 prio_char(prio), tag ? tag : "-", msg ? msg : "");

	if (n > 0)
		write(STDERR_FILENO, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1));
}

int __android_log_write(int prio, const char *tag, const char *text)
{
	emit(prio, tag, text);
	return 1;
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
	char msg[1536];

	vsnprintf(msg, sizeof(msg), fmt, ap);
	emit(prio, tag, msg);
	return 1;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__android_log_vprint(prio, tag, fmt, ap);
	va_end(ap);
	return 1;
}

int __android_log_buf_write(int bufID, int prio, const char *tag, const char *text)
{
	(void)bufID;
	return __android_log_write(prio, tag, text);
}

int __android_log_buf_print(int bufID, int prio, const char *tag, const char *fmt, ...)
{
	va_list ap;

	(void)bufID;
	va_start(ap, fmt);
	__android_log_vprint(prio, tag, fmt, ap);
	va_end(ap);
	return 1;
}

void __android_log_assert(const char *cond, const char *tag, const char *fmt, ...)
{
	char msg[1536];

	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(msg, sizeof(msg), fmt, ap);
		va_end(ap);
	} else {
		snprintf(msg, sizeof(msg), "assertion failed: %s",
			 cond ? cond : "(неизвестно)");
	}
	emit(7 /* FATAL */, tag, msg);
	abort();
}

/* Современный liblog сводит всё сюда. */
struct android_log_message {
	size_t struct_size;
	int32_t buffer_id;
	int32_t priority;
	const char *tag;
	const char *file;
	uint32_t line;
	const char *message;
};

void __android_log_write_log_message(struct android_log_message *m)
{
	if (m)
		emit(m->priority, m->tag, m->message);
}

void __android_log_logd_logger(const struct android_log_message *m)
{
	if (m)
		emit(m->priority, m->tag, m->message);
}

void __android_log_stderr_logger(const struct android_log_message *m)
{
	if (m)
		emit(m->priority, m->tag, m->message);
}

void __android_log_set_logger(void (*logger)(const struct android_log_message *))
{
	(void)logger;	/* свой оставляем */
}

void __android_log_set_aborter(void (*aborter)(const char *))
{
	(void)aborter;
}

void __android_log_call_aborter(const char *abort_message)
{
	emit(7, "ABORT", abort_message);
	abort();
}

void __android_log_default_aborter(const char *abort_message)
{
	__android_log_call_aborter(abort_message);
}

int __android_log_is_loggable(int prio, const char *tag, int default_prio)
{
	(void)prio; (void)tag; (void)default_prio;
	return 1;	/* печатаем всё */
}

int __android_log_is_loggable_len(int prio, const char *tag, size_t len,
				  int default_prio)
{
	(void)prio; (void)tag; (void)len; (void)default_prio;
	return 1;
}

int __android_log_set_minimum_priority(int priority)
{
	(void)priority;
	return 2;
}

int __android_log_get_minimum_priority(void)
{
	return 2;	/* VERBOSE */
}
