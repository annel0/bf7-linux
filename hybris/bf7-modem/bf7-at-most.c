/*
 * BF7: мост AT-порта модема к настоящему терминалу для ModemManager.
 *
 * AT-канал модема MediaTek — /dev/ttyC0 (CCCI_UART2, очередь
 * DATA_AT_CMD_Q). Несмотря на имя, это простое символьное устройство, а не
 * tty: tcgetattr отвечает ENOTTY. ModemManager работает только с настоящими
 * терминалами, которые видны в sysfs (по ним udev вешает ID_MM_*). Обычный
 * pty из /dev/pts в sysfs не виден, поэтому берётся «старый» BSD-pty
 * (CONFIG_LEGACY_PTYS): /dev/ttyp0 — настоящий tty с узлом в sysfs, а его
 * вторая половина /dev/ptyp0 — у нас.
 *
 * /dev/ttyC0 открывается только когда модем готов (см. zhdat_modem).
 *
 * Байты гоняются как есть в обе стороны, кроме одного: незапрошенные
 * сообщения MediaTek «+E…» вырезаются (см. vyrezat_musor) — MM принимал их
 * за часть ответа на команду. Самое частое из них, +EDMFAPP, приходит
 * каждые 3–30 с и командой не выключается. Если терминал никто не открыл, а модем шлёт незапрошенные
 * сообщения (+EIND, +CREG…), они выбрасываются — иначе буфер pty заполнится
 * и мост встанет. Если модем перезапускается и /dev/ttyC0 отвечает ошибкой,
 * канал переоткрывается раз в секунду.
 *
 * Сборка на телефоне: gcc -O2 -Wall -o bf7-at-most bf7-at-most.c
 * Вызов: bf7-at-most [/dev/ttyC0] [/dev/ptyp0]
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/*
 * Ждать, пока модем готов (md1:4 в /sys/kernel/ccci/boot). Открывать
 * /dev/ttyC0 раньше нельзя: ядро считает его держателя «критическим
 * пользователем» (CRIT_USR_MUXD) и перед запуском модема ждёт, пока тот
 * уйдёт, — 30 секунд BOOT_TIMEOUT на каждой загрузке. На Android
 * gsm0710muxd тоже открывает порт только после готовности модема.
 */
static void zhdat_modem(void)
{
	int skazal = 0;

	for (;;) {
		char s[16] = "";
		FILE *f = fopen("/sys/kernel/ccci/boot", "r");

		if (f) {
			if (fscanf(f, "%15s", s) != 1)
				s[0] = 0;
			fclose(f);
		}
		if (!strcmp(s, "md1:4"))
			return;
		if (!skazal++)
			fprintf(stderr, "bf7-at-most: жду готовности модема (%s)\n", s);
		sleep(1);
	}
}

/*
 * Собственные сообщения MediaTek — строки «+E…» (+EDMFAPP, +ECPI, +EPDN,
 * +EDRETRY, +EIND, +EIF…). ModemManager их не знает; пока они приходят
 * сами по себе, он их молча отбрасывает, но модем часто кладёт такое
 * сообщение в один пакет с ответом на команду, и тогда MM считает его
 * частью ответа: USSD «+CUSD» отброшен как неверный из-за двух +EDMFAPP
 * перед ним, исходящий вызов закрыт как «Unhandled response» из-за
 * «+ECPI: 1,130,…» сразу за OK на ATD. Стандартные сообщения 3GPP,
 * которые разбирает MM, начинаются с «+C», поэтому режем все «+E», кроме
 * ответа на запрос, который MM сам послал (AT+ECPI? и т.п., см. zhdu), и
 * +EIF, +EAIC и +ECPI — их наш MM разбирает сам.
 *
 * Модем отдаёт каждое сообщение одним пакетом CCCI, строка целиком внутри
 * одного read(); недописанную строку (без <CR><LF> в конце куска) не
 * трогаем — пропустим как есть.
 */
static char zhdu[16];	/* «+ECPI» из последней AT+E-команды, до OK/ERROR */

static void zapomnit_komandu(const char *b, size_t n)
{
	size_t i, k;

	for (i = 0; i + 4 < n; i++) {
		if (memcmp(b + i, "AT+E", 4))
			continue;
		zhdu[0] = '+';
		for (k = 1; k < sizeof(zhdu) - 1 && i + 2 + k < n; k++) {
			char c = b[i + 2 + k];

			if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9'))
				break;
			zhdu[k] = c;
		}
		zhdu[k] = 0;
		return;
	}
}

/* Кусок начался с начала строки: прошлый кончился на <LF>. Модем иногда
 * рвёт «<CR><LF>+EDMFAPP: …» между двумя пакетами, и тогда «+E» стоит в
 * самом начале куска без <CR><LF> перед ним. */
static int s_nachala = 1;

static size_t vyrezat_musor(char *b, size_t n)
{
	size_t i = 0, o = 0, dz = strlen(zhdu);
	int konec = 0;

	while (i < n) {
		size_t pr = 0;	/* длина <CR><LF> перед «+E» */

		if (n - i >= 4 && !memcmp(b + i, "\r\n+E", 4))
			pr = 2;
		else if (i == 0 && s_nachala && n >= 2 && !memcmp(b, "+E", 2))
			pr = 0;
		else
			goto kopirovat;
		if (dz && n - i - pr >= dz && !memcmp(b + i + pr, zhdu, dz))
			goto kopirovat;
		/* +EIF — какой интерфейс ccmni модем отдал контексту; его ждёт
		 * наш ModemManager (заплатка mtk-cgact-data). */
		if (n - i - pr >= 5 && !memcmp(b + i + pr, "+EIF:", 5))
			goto kopirovat;
		/* +EAIC — входящий ждёт одобрения; одобряет наш MM (заплатка
		 * mtk-no-clcc), без этого на звонок не ответить. */
		if (n - i - pr >= 6 && !memcmp(b + i + pr, "+EAIC:", 6))
			goto kopirovat;
		/* +ECPI — ход вызова (2 — у абонента звонит, 6 — ответил); по нему
		 * наш MM (заплатка mtk-ecpi) ведёт исходящий: без этого вызов был
		 * «active» сразу после ATD, ещё до гудков. Ставить только вместе с
		 * этим MM — старый не знает +ECPI, и сообщение посреди ответа на
		 * команду сбило бы разбор. В журнал — по-прежнему: по строке
		 * «+ECPI: N,6» bf7-pozvonit ждёт ответа абонента. */
		if (n - i - pr >= 6 && !memcmp(b + i + pr, "+ECPI:", 6)) {
			size_t k = i + pr;

			while (k + 1 < n && !(b[k] == '\r' && b[k + 1] == '\n'))
				k++;
			fprintf(stderr, "bf7-at-most: передано %.*s\n",
				(int)(k - i - pr), b + i + pr);
			goto kopirovat;
		}
		{
			size_t k = i + pr + 2;

			while (k + 1 < n && !(b[k] == '\r' && b[k + 1] == '\n'))
				k++;
			if (k + 1 < n) {
				/* Редкие сообщения — в журнал: по ним видно, что модем
				 * делает с данными (+EIF, +EPDN) и вызовами (+ECPI). */
				if (memcmp(b + i + pr, "+EDMFAPP", 8))
					fprintf(stderr, "bf7-at-most: убрано %.*s\n",
						(int)(k - i - pr), b + i + pr);
				/* <CR><LF> в конце строки оставляем: он же открывает
				 * следующее сообщение, если оно идёт вплотную. */
				i = k;
				continue;
			}
		}
kopirovat:
		if (n - i >= 6 && (!memcmp(b + i, "\r\nOK\r\n", 6) ||
				   !memcmp(b + i, "ERROR", 5)))
			konec = 1;
		b[o++] = b[i++];
	}
	if (konec)
		zhdu[0] = 0;
	if (n)
		s_nachala = b[n - 1] == '\n';
	return o;
}

/*
 * Модем иногда рвёт длинную строку «+EDMFAPP: …» между двумя пакетами. Кусок
 * без <CR><LF> в конце фильтр вырезать не может, и он уходил в MM — прилипал
 * к ответу на ATA, и MM бросал только что принятый вызов («Unhandled
 * response '+EDMFAPP…'»). Поэтому недописанный хвост «<CR><LF>+E…» держим в
 * запасе до следующего пакета; если за 200 мс продолжения нет — отдаём как
 * есть.
 */
static char zapas[1024];
static size_t nz;

static size_t otlozhit_hvost(char *b, size_t n)
{
	size_t k = n, start;

	/* Последний «<CR><LF>» в куске — начало последней строки. */
	while (k >= 2 && !(b[k - 2] == '\r' && b[k - 1] == '\n'))
		k--;
	if (k >= 2) {
		if (k == n)
			return n;	/* кусок кончается целой строкой */
		start = k - 2;	/* хвост вместе с <CR><LF> перед ним */
		if (n - k < 2 || memcmp(b + k, "+E", 2))
			return n;
	} else {
		if (!s_nachala || n < 2 || memcmp(b, "+E", 2))
			return n;
		start = 0;
	}
	if (n - start > sizeof(zapas))
		return n;
	memcpy(zapas, b + start, n - start);
	nz = n - start;
	return start;
}

static int otkryt(const char *put)
{
	for (;;) {
		int fd = open(put, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
		if (fd >= 0)
			return fd;
		fprintf(stderr, "bf7-at-most: %s: %s, жду\n", put, strerror(errno));
		sleep(1);
	}
}

int main(int argc, char **argv)
{
	const char *modem = argc > 1 ? argv[1] : "/dev/ttyC0";
	const char *pty = argc > 2 ? argv[2] : "/dev/ptyp0";
	char slave[64];
	char buf[4096];
	int m = otkryt(pty);
	int c;

	zhdat_modem();
	c = otkryt(modem);

	/* Сторону tty держим открытой сами: иначе, пока ModemManager её не
	 * открыл, master отвечает POLLHUP без конца и мост крутится впустую. */
	snprintf(slave, sizeof(slave), "%s", pty);
	if (!strncmp(slave, "/dev/pty", 8))
		slave[5] = 't';
	if (open(slave, O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK) < 0)
		fprintf(stderr, "bf7-at-most: %s: %s\n", slave, strerror(errno));
	struct termios t;

	/* Сторону ModemManager настраивает он сам; здесь — только сырой режим
	 * на случай, если терминал откроет кто-то без настройки. */
	if (tcgetattr(m, &t) == 0) {
		cfmakeraw(&t);
		tcsetattr(m, TCSANOW, &t);
	}
	fprintf(stderr, "bf7-at-most: %s <-> %s\n", modem, pty);

	for (;;) {
		struct pollfd p[2] = {
			{ .fd = c, .events = POLLIN },
			{ .fd = m, .events = POLLIN },
		};
		ssize_t n;

		int gotovo = poll(p, 2, nz ? 200 : -1);

		if (gotovo < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
		if (gotovo == 0) {
			/* Продолжения недописанной строки нет — отдать как есть. */
			if (write(m, zapas, nz) < 0 && errno != EAGAIN && errno != EIO)
				fprintf(stderr, "bf7-at-most: в pty: %s\n", strerror(errno));
			nz = 0;
			continue;
		}
		if (p[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fprintf(stderr, "bf7-at-most: модем пропал, переоткрываю\n");
			close(c);
			sleep(1);
			zhdat_modem();
			c = otkryt(modem);
			continue;
		}
		if (p[0].revents & POLLIN) {
			size_t z = nz;

			memcpy(buf, zapas, z);
			nz = 0;
			n = read(c, buf + z, sizeof(buf) - z);
			if (n > 0) {
				n = otlozhit_hvost(buf, n + z);
				n = vyrezat_musor(buf, n);
			} else if (z) {
				memcpy(zapas, buf, z);	/* ошибка чтения — запас не терять */
				nz = z;
			}
			if (n > 0 && write(m, buf, n) < 0 && errno != EAGAIN && errno != EIO)
				fprintf(stderr, "bf7-at-most: в pty: %s\n", strerror(errno));
		}
		/* Страховка: если сторона tty всё же закрылась, не крутиться. */
		if (p[1].revents & POLLHUP) {
			sleep(1);
			continue;
		}
		if (p[1].revents & POLLIN) {
			n = read(m, buf, sizeof(buf));
			if (n > 0)
				zapomnit_komandu(buf, n);
			if (n > 0 && write(c, buf, n) < 0)
				fprintf(stderr, "bf7-at-most: в модем: %s\n", strerror(errno));
		}
	}
}
