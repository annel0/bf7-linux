/*
 * BF7: мост между /dev/stpbt и /dev/vhci.
 *
 * Зачем. Комбо-чип MediaTek отдаёт Bluetooth не отдельным HCI-устройством, а
 * символьным узлом /dev/stpbt: драйвер bt_drv кладёт туда пакеты HCI ровно в
 * том же виде, в каком их ждёт транспорт H4 — один байт типа пакета, следом
 * тело. В Android поверх этого работает libbt-vendor; у нас его нет.
 *
 * В ядре есть виртуальный транспорт hci_vhci: пишешь в /dev/vhci пакет H4 —
 * он приходит в стек Bluetooth как от настоящего устройства, читаешь — это
 * стек шлёт команду наружу. Значит, нужен не драйвер, а перекладчик.
 *
 * Что делает. Открывает оба узла, создаёт HCI-устройство (первый пакет в
 * vhci: 0xff — вендорский, затем 0x00 — тип «основное»), и дальше гоняет
 * байты в обе стороны, пока жив.
 *
 * Почему две нити, а не poll. Драйвер MediaTek не реализует .poll, и ядро для
 * такого файла отвечает DEFAULT_POLLMASK — «всегда готов». Мост на poll
 * немедленно лез читать из stpbt, застревал там в блокирующем чтении и никогда
 * не передавал чипу команду из vhci: стек слал HCI Reset, ответа не было,
 * hciconfig отвечал «Connection timed out». При этом прямая проба той же
 * командой через тот же узел отвечала «успех» — разница была только в этом.
 *
 * Поэтому каждое направление живёт в своей нити и просто читает блокирующе.
 *
 * Открытие /dev/stpbt само включает функцию BT у чипа через WMT: отдельной
 * команды питания не нужно.
 *
 * Сборка:
 *   gcc -O2 -o bf7-btbridge btbridge.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define STPBT "/dev/stpbt"
#define VHCI  "/dev/vhci"

/* Пакет HCI не превышает 1024 + заголовок; берём с запасом. */
#define BUF 2048

static volatile sig_atomic_t stop_now;

/*
 * Дескрипторы держим глобально нарочно. Нити сидят в блокирующем чтении, и
 * одного флага им мало: пока из узла ничего не пришло, read не вернётся, флаг
 * никто не посмотрит, а pthread_join будет ждать вечно.
 *
 * Именно так я и подвесил телефон: systemd послал службе SIGTERM при
 * выключении, мост не вышел, и система осталась в состоянии «stopping» на
 * тридцать пять минут.
 *
 * Первая починка — закрыть узлы в обработчике — **не работала**, и это
 * выяснилось лишь 2026-09-22: в Linux close() не будит нить, уже
 * заблокированную в read() того же файла, — вызов держит свою ссылку на
 * него. Выключение по-прежнему стояло ~25 секунд, пока systemd не сдавался.
 *
 * Теперь обработчик просто выходит: _exit() безопасен в обработчике сигнала,
 * а ядро при выходе процесса шлёт смертельный сигнал всем его нитям. Раньше
 * это не помогло бы — драйвер ждал непрерываемо, и нить застревала в
 * состоянии D. С той же даты ожидания в BT_read прерываемые
 * (kernel-work/bf7-bluetooth/stp_chrdev_bt.c), и нить честно умирает.
 * Прибирать нечего: узлы закроет ядро, hci0 исчезнет при закрытии vhci,
 * а закрытие stpbt само выключит BT у чипа через WMT.
 */
static void on_signal(int n)
{
	(void) n;
	stop_now = 1;
	_exit(0);
}


/*
 * Собственный адрес чипа — заглушка: он читается как 00:00:46:76:63:01, то
 * есть начинается с двух нулей, ровно как placeholder MediaTek у Wi-Fi
 * (00:08:22:...). Настоящий лежит в NVRAM, и вендорский слой Android достаёт
 * его оттуда; у нас этого не происходит.
 *
 * С таким адресом чужие устройства ведут себя странно, а после перезагрузки
 * телефон выглядит другим. Поэтому выдаём свой: выводим из машинного
 * идентификатора, ставим бит локального назначения и снимаем групповой. Адрес
 * получается случайным для стороннего наблюдателя, но одинаковым от загрузки к
 * загрузке — та же мысль, что у «stable» в NetworkManager.
 *
 * Пишем вендорской командой 0xFC1A: проверено на живом чипе, отвечает
 * «успех» и следом «Read BD Addr» возвращает уже новый адрес.
 */
/*
 * Адрес чипа. Сперва заводской, если он есть.
 *
 * Он лежит в разделе nvdata (APCFG/APRDEB/BT_Addr, первые шесть байт) в том
 * же порядке, в каком его показывают: e4 f8 be 91 af 26. Доставать его при
 * каждой загрузке незачем — bf7-bt-prepare кладёт его в /etc/bf7-bt-addr, а
 * мы читаем оттуда. В команде HCI байты идут наоборот, поэтому переворачиваем.
 *
 * Если кэша нет (например, nvdata не прочитался), выводим адрес из
 * machine-id, как раньше: он хотя бы постоянный для этого телефона и помечен
 * локально назначенным.
 */
static void bf7_svoy_adres(unsigned char out[6])
{
	uint64_t h = 1469598103934665603ULL;	/* FNV-1a */
	FILE *f;
	char id[64] = "bf7";
	int i;

	f = fopen("/etc/bf7-bt-addr", "rb");
	if (f) {
		unsigned char zavodskoj[6];
		size_t got = fread(zavodskoj, 1, 6, f);

		fclose(f);
		if (got == 6) {
			int pustoj = 1;

			for (i = 0; i < 6; i++)
				if (zavodskoj[i])
					pustoj = 0;
			if (!pustoj) {
				for (i = 0; i < 6; i++)
					out[i] = zavodskoj[5 - i];
				return;
			}
		}
	}

	f = fopen("/etc/machine-id", "r");

	if (f) {
		if (!fgets(id, sizeof id, f))
			snprintf(id, sizeof id, "bf7");
		fclose(f);
	}

	for (i = 0; id[i]; i++) {
		h ^= (unsigned char) id[i];
		h *= 1099511628211ULL;
	}

	for (i = 0; i < 6; i++)
		out[i] = (unsigned char) (h >> (8 * i));

	/* Старший байт адреса: локально назначенный, не групповой. */
	out[5] = (unsigned char) ((out[5] & 0xfe) | 0x02);
}

/* Отправить команду и дождаться события. 0 — дождались. */
static int bf7_komanda(int fd, const unsigned char *pkt, size_t len,
                       unsigned char *otvet, size_t otvet_len)
{
	fd_set set;
	struct timeval tv = { .tv_sec = 4, .tv_usec = 0 };
	ssize_t got;

	if (write(fd, pkt, len) != (ssize_t) len)
		return -1;

	FD_ZERO(&set);
	FD_SET(fd, &set);
	if (select(fd + 1, &set, NULL, NULL, &tv) <= 0)
		return -1;

	got = read(fd, otvet, otvet_len);
	return got > 0 ? 0 : -1;
}

/* Задать чипу постоянный адрес до того, как поднимется стек. */
static void bf7_postavit_adres(int fd)
{
	unsigned char addr[6], otvet[64];
	unsigned char reset[4] = { 0x01, 0x03, 0x0c, 0x00 };
	unsigned char cmd[10] = { 0x01, 0x1a, 0xfc, 0x06 };

	bf7_svoy_adres(addr);
	memcpy(cmd + 4, addr, 6);

	if (bf7_komanda(fd, reset, sizeof reset, otvet, sizeof otvet) != 0) {
		fprintf(stderr, "bf7-btbridge: чип не ответил на сброс, адрес не меняю\n");
		return;
	}
	if (bf7_komanda(fd, cmd, sizeof cmd, otvet, sizeof otvet) != 0 ||
	    otvet[6] != 0x00) {
		fprintf(stderr, "bf7-btbridge: адрес задать не вышло\n");
		return;
	}

	fprintf(stderr, "bf7-btbridge: адрес задан %02X:%02X:%02X:%02X:%02X:%02X\n",
		addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

	/*
	 * После смены адреса чип пересобирает связь, и чтение из stpbt какое-то
	 * время отвечает EIO. Даём ему сброс и выдержку, иначе нити моста
	 * стартуют в этот промежуток и сразу умирают с «Input/output error».
	 */
	sleep(3);
	if (bf7_komanda(fd, reset, sizeof reset, otvet, sizeof otvet) != 0)
		fprintf(stderr, "bf7-btbridge: сброс после смены адреса без ответа\n");
	sleep(5);
}

/* Одно направление: читаем блокирующе и перекладываем, пока живы. */
struct storona {
	int from;
	int to;
	const char *label;
	int vendor_drop;	/* отбрасывать служебные пакеты vhci */
	int terpet_eio;		/* пережидать ложный EIO (только сторона stpbt) */
};

/*
 * Сколько ложных EIO подряд терпим, прежде чем признать связь мёртвой.
 *
 * Откуда ложный EIO. BT_read в bt_drv блокирующе ждёт данных, и если его
 * будят, а очередь пуста и сброса чипа нет (rstflag=0), он не ждёт снова, а
 * ругается WARN «Blocking read is waken up with no data» и отдаёт EIO. Связь
 * при этом цела — следующее чтение забирает данные как ни в чём не бывало.
 * Поймано 2026-09-22 после прошивки: через 20 мс после создания hci0 первый
 * же ответ чипа на HCI Reset разбудил читателя вхолостую, и мост упал, унеся
 * Bluetooth до перезагрузки.
 *
 * Корень чинится в драйвере; это страховка на случай, если на телефоне
 * окажется старый модуль. Считаем подряд: успешное чтение сбрасывает счёт,
 * так что редкие ложные пробуждения за долгий сеанс не копятся.
 */
#define BF7_EIO_PODRYAD 5

static void *pump(void *arg)
{
	struct storona *s = arg;
	unsigned char buf[BUF];
	int eio_podryad = 0;

	while (!stop_now) {
		ssize_t got = read(s->from, buf, sizeof buf);

		if (got < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EIO && s->terpet_eio &&
			    eio_podryad < BF7_EIO_PODRYAD) {
				eio_podryad++;
				fprintf(stderr, "bf7-btbridge: ложный EIO из %s, "
					"жду и читаю снова (%d из %d)\n",
					s->label, eio_podryad, BF7_EIO_PODRYAD);
				usleep(50000);
				continue;
			}
			fprintf(stderr, "bf7-btbridge: чтение %s: %s\n",
				s->label, strerror(errno));
			break;
		}
		eio_podryad = 0;
		if (got == 0)
			continue;

		/*
		 * Служебный пакет vhci чипу не нужен. Тот самый «ff 00», которым мы
		 * создаём HCI-устройство, возвращается обратно при чтении из vhci —
		 * и, уйдя в чип, сбивает его: следом отправленный HCI Reset остаётся
		 * без ответа. Отсюда и «Connection timed out», хотя прямая проба тем
		 * же узлом отвечала «успех». Вендорские пакеты (тип 0xff) не наше
		 * дело — отбрасываем.
		 */
		if (s->vendor_drop && buf[0] == 0xff)
			continue;

		/*
		 * Подпорка под чип: он объявляет умение, которого у него нет.
		 *
		 * В ответе на «Read Local Extended Features» для страницы 2 стоят
		 * биты широковещания без соединения и синхронизирующего поезда. Ядро
		 * верит и шлёт «Read Synchronization Train Parameters» — а чип
		 * отвечает «неизвестная команда», и весь подъём адаптера срывается:
		 *
		 *   < Read Synchronization Train Parameters (0x03|0x0077)
		 *   > Command Status: Unknown HCI Command (0x01)
		 *   = Close Index
		 *
		 * Гасим четыре нижних бита ПЕРВОГО байта возможностей страницы 2
		 * (LMP_CSB_MASTER, LMP_CSB_SLAVE, LMP_SYNC_TRAIN, LMP_SYNC_SCAN):
		 * ядро смотрит именно features[2][0], а не следующий байт — на этом
		 * я сперва и ошибся, погасив не тот. После правки команд не шлёт.
		 *
		 * Раскладка события: 04 0e plen ncmd op_lo op_hi status page max features[8].
		 */
		if (!s->vendor_drop && got >= 17 &&
		    buf[0] == 0x04 && buf[1] == 0x0e &&
		    buf[4] == 0x04 && buf[5] == 0x10 &&	/* opcode 0x1004 */
		    buf[6] == 0x00 && buf[7] == 0x02) {	/* успех, страница 2 */
			if (buf[9] & 0x0f) {
				fprintf(stderr, "bf7-btbridge: снимаю заявку на "
					"синхронизирующий поезд (было 0x%02x)\n", buf[9]);
				buf[9] &= (unsigned char) ~0x0f;
			}
		}

		if (write(s->to, buf, (size_t) got) != got) {
			fprintf(stderr, "bf7-btbridge: запись после %s: %s\n",
				s->label, strerror(errno));
			break;
		}
	}

	stop_now = 1;
	return NULL;
}

int main(void)
{
	int bt, vh;
	pthread_t t1, t2;
	struct storona tuda, obratno;
	/* 0xff — вендорский пакет управления vhci, 0x00 — устройство типа
	 * «основное» (BR/EDR + LE). Без него hci-устройство не появится. */
	const unsigned char create[2] = { 0xff, 0x00 };

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	setvbuf(stderr, NULL, _IOLBF, 0);

	bt = open(STPBT, O_RDWR);
	if (bt < 0) {
		fprintf(stderr, "bf7-btbridge: не открыть %s: %s\n", STPBT, strerror(errno));
		return 1;
	}

	/*
	 * Выдержка после открытия stpbt обязательна. Открытие включает функцию BT
	 * через WMT, и чипу нужно время: если создать HCI-устройство сразу, стек
	 * тут же шлёт HCI Reset, а чип ещё не готов.
	 */
	sleep(3);

	/*
	 * Заводской адрес задаём. Работает с 2026-09-22.
	 *
	 * bf7-bt-prepare достаёт его из раздела nvdata (APCFG/APRDEB/BT_Addr) и
	 * кладёт в /etc/bf7-bt-addr; у этого телефона это E4:F8:BE:xx:xx:xx.
	 * Прошивка сама отдаёт заглушку 00:00:46:76:63:01.
	 *
	 * Долго считалось, что связь STP смены адреса не переживает: следующее
	 * же чтение из stpbt отвечало EIO, и мост падал. Выдержки 1+1 и 3+5
	 * секунд не помогали. Причина оказалась не в адресе, а в драйвере:
	 * BT_read считал «разбудили, а данных нет» невозможным и сдавался —
	 * а смена адреса как раз даёт такое пробуждение. После правки в
	 * stp_chrdev_bt.c (ждать снова при rstflag == 0) адрес встаёт и
	 * держится: hci0 UP RUNNING, поиск устройств работает.
	 */
	bf7_postavit_adres(bt);

	vh = open(VHCI, O_RDWR);
	if (vh < 0) {
		fprintf(stderr, "bf7-btbridge: не открыть %s: %s\n", VHCI, strerror(errno));
		close(bt);
		return 1;
	}

	if (write(vh, create, sizeof create) != (ssize_t) sizeof create) {
		fprintf(stderr, "bf7-btbridge: не создать HCI-устройство: %s\n",
			strerror(errno));
		close(vh);
		close(bt);
		return 1;
	}

	fprintf(stderr, "bf7-btbridge: мост поднят, %s <-> %s\n", STPBT, VHCI);

	tuda.from = bt; tuda.to = vh; tuda.label = "stpbt"; tuda.vendor_drop = 0;
	tuda.terpet_eio = 1;
	obratno.from = vh; obratno.to = bt; obratno.label = "vhci"; obratno.vendor_drop = 1;
	obratno.terpet_eio = 0;

	if (pthread_create(&t1, NULL, pump, &tuda) != 0 ||
	    pthread_create(&t2, NULL, pump, &obratno) != 0) {
		fprintf(stderr, "bf7-btbridge: не завести нити\n");
		close(vh);
		close(bt);
		return 1;
	}

	pthread_join(t1, NULL);
	pthread_join(t2, NULL);

	fprintf(stderr, "bf7-btbridge: мост опущен\n");
	close(vh);
	close(bt);
	return 0;
}
