/*
 * BF7: впрыск касаний через uinput.
 *
 * Нужен, чтобы отлаживать оболочки на экране телефона без живого пальца:
 * нажать в заданную точку столько раз, сколько попросили, с заданной паузой.
 *
 * Устройство объявляется прямым (INPUT_PROP_DIRECT) — иначе libinput считает
 * его тачпадом и координаты становятся относительными. Протокол MT типа B:
 * слот, tracking id, позиция; BTN_TOUCH обязателен, без него libinput касания
 * не замечает.
 *
 * Правило udev 99-bf7-touch.rules разворачивает настоящий mtk-tpd2 на 180°,
 * но нас оно не касается — имя другое, поэтому координаты задаются сразу
 * экранные.
 *
 * Сборка:  gcc -O2 -o touchinj touchinj.c
 * Запуск:  ./touchinj <x> <y> [сколько] [пауза_мс] [длительность_мс]
 *          ./touchinj свайп <x1> <y1> <x2> <y2> [шагов] [пауза_мс]
 */
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ШИРИНА   720
#define ВЫСОТА  1612

static int узел;
static int метка = 1;

static void событие(int тип, int код, int знач)
{
	struct input_event e;
	memset(&e, 0, sizeof e);
	e.type = тип;
	e.code = код;
	e.value = знач;
	if (write(узел, &e, sizeof e) != sizeof e)
		perror("write");
}

static void кадр(void) { событие(EV_SYN, SYN_REPORT, 0); }

static void вниз(int x, int y)
{
	событие(EV_ABS, ABS_MT_SLOT, 0);
	событие(EV_ABS, ABS_MT_TRACKING_ID, метка++);
	событие(EV_ABS, ABS_MT_POSITION_X, x);
	событие(EV_ABS, ABS_MT_POSITION_Y, y);
	событие(EV_KEY, BTN_TOUCH, 1);
	событие(EV_ABS, ABS_X, x);
	событие(EV_ABS, ABS_Y, y);
	кадр();
}

static void ведём(int x, int y)
{
	событие(EV_ABS, ABS_MT_SLOT, 0);
	событие(EV_ABS, ABS_MT_POSITION_X, x);
	событие(EV_ABS, ABS_MT_POSITION_Y, y);
	событие(EV_ABS, ABS_X, x);
	событие(EV_ABS, ABS_Y, y);
	кадр();
}

static void вверх(void)
{
	событие(EV_ABS, ABS_MT_SLOT, 0);
	событие(EV_ABS, ABS_MT_TRACKING_ID, -1);
	событие(EV_KEY, BTN_TOUCH, 0);
	кадр();
}

static void создать(void)
{
	узел = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (узел < 0) { perror("/dev/uinput"); exit(1); }

	ioctl(узел, UI_SET_EVBIT, EV_ABS);
	ioctl(узел, UI_SET_EVBIT, EV_KEY);
	ioctl(узел, UI_SET_EVBIT, EV_SYN);
	ioctl(узел, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(узел, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
	int оси[] = { ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y,
		      ABS_MT_SLOT, ABS_MT_TRACKING_ID };
	for (unsigned i = 0; i < sizeof оси / sizeof *оси; i++)
		ioctl(узел, UI_SET_ABSBIT, оси[i]);

	struct uinput_abs_setup a;
	int пределы[][3] = {
		{ ABS_X,             0, ШИРИНА - 1 },
		{ ABS_Y,             0, ВЫСОТА - 1 },
		{ ABS_MT_POSITION_X, 0, ШИРИНА - 1 },
		{ ABS_MT_POSITION_Y, 0, ВЫСОТА - 1 },
		{ ABS_MT_SLOT,       0, 4 },
		{ ABS_MT_TRACKING_ID, 0, 65535 },
	};
	for (unsigned i = 0; i < sizeof пределы / sizeof *пределы; i++) {
		memset(&a, 0, sizeof a);
		a.code = пределы[i][0];
		a.absinfo.minimum = пределы[i][1];
		a.absinfo.maximum = пределы[i][2];
		ioctl(узел, UI_ABS_SETUP, &a);
	}

	struct uinput_setup s;
	memset(&s, 0, sizeof s);
	s.id.bustype = BUS_VIRTUAL;
	s.id.vendor = 0xbf07;
	s.id.product = 0x0001;
	snprintf(s.name, sizeof s.name, "bf7-virt-touch");
	ioctl(узел, UI_DEV_SETUP, &s);
	if (ioctl(узел, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); exit(1); }

	/* Композитору нужно время заметить новое устройство. */
	usleep(1200000);
}

int main(int argc, char **argv)
{
	/* Поток. Зачем отдельный режим: каждый запуск заводит новый узел uinput,
	 * а это для композитора появление и исчезновение устройства. Пока он его
	 * добавляет, первые события теряются, а удаление узла способно оборвать
	 * ещё не обработанное касание. Палец у хозяина — одно постоянное
	 * устройство, и проверять надо так же, иначе меряешь свой пробник. */
	if (argc > 1 && (!strcmp(argv[1], "поток") || !strcmp(argv[1], "stream"))) {
		char строка[128];
		создать();
		while (fgets(строка, sizeof строка, stdin)) {
			int x = 0, y = 0, держать = 110;
			int x1, y1, x2, y2, шагов = 40, полей;

			/* Свайп прямо в потоке. Без него приходилось запускать
			 * пробник второй раз, а это новый узел uinput: экран
			 * блокировки успевал закрыться между запусками, и
			 * казалось, что он не принимает код. */
			if (!strncmp(строка, "свайп", 10) || строка[0] == 's') {
				const char *хвост = строка;
				while (*хвост && !(*хвост >= '0' && *хвост <= '9') && *хвост != '-')
					хвост++;
				полей = sscanf(хвост, "%d %d %d %d %d",
					       &x1, &y1, &x2, &y2, &шагов);
				if (полей < 4)
					continue;
				вниз(x1, y1);
				for (int i = 1; i <= шагов; i++) {
					ведём(x1 + (x2 - x1) * i / шагов,
					      y1 + (y2 - y1) * i / шагов);
					usleep(8000);
				}
				вверх();
				printf("свайп (%d,%d) -> (%d,%d)\n", x1, y1, x2, y2);
				fflush(stdout);
				usleep(400000);
				continue;
			}

			полей = sscanf(строка, "%d %d %d", &x, &y, &держать);
			if (полей < 2)
				continue;
			вниз(x, y);
			usleep(держать * 1000);
			вверх();
			printf("нажатие в (%d,%d)\n", x, y);
			fflush(stdout);
			usleep(400000);
		}
		usleep(1500000);
		ioctl(узел, UI_DEV_DESTROY);
		close(узел);
		return 0;
	}

	if (argc < 3) {
		fprintf(stderr,
			"нажатие: %s <x> <y> [сколько=1] [пауза_мс=400] [длительность_мс=80]\n"
			"свайп:   %s свайп <x1> <y1> <x2> <y2> [шагов=40] [пауза_мс=8]\n"
			"поток:   %s поток   — строки «x y [длительность_мс]» со входа,\n"
			"                     либо «свайп x1 y1 x2 y2 [шагов]»,\n"
			"                     одно устройство на всю череду\n",
			argv[0], argv[0], argv[0]);
		return 1;
	}


	if (!strcmp(argv[1], "свайп") || !strcmp(argv[1], "swipe")) {
		if (argc < 6) { fprintf(stderr, "нужны x1 y1 x2 y2\n"); return 1; }
		int x1 = atoi(argv[2]), y1 = atoi(argv[3]);
		int x2 = atoi(argv[4]), y2 = atoi(argv[5]);
		int шагов = argc > 6 ? atoi(argv[6]) : 40;
		int пауза = argc > 7 ? atoi(argv[7]) : 8;
		создать();
		вниз(x1, y1);
		for (int i = 1; i <= шагов; i++) {
			ведём(x1 + (x2 - x1) * i / шагов, y1 + (y2 - y1) * i / шагов);
			usleep(пауза * 1000);
		}
		вверх();
		printf("свайп (%d,%d) -> (%d,%d), %d шагов\n", x1, y1, x2, y2, шагов);
	} else {
		int x = atoi(argv[1]), y = atoi(argv[2]);
		int сколько = argc > 3 ? atoi(argv[3]) : 1;
		int пауза = argc > 4 ? atoi(argv[4]) : 400;
		int держать = argc > 5 ? atoi(argv[5]) : 80;
		создать();
		for (int i = 0; i < сколько; i++) {
			вниз(x, y);
			usleep(держать * 1000);
			вверх();
			printf("нажатие %d в (%d,%d)\n", i + 1, x, y);
			fflush(stdout);
			if (i + 1 < сколько) usleep(пауза * 1000);
		}
	}

	/* Держим устройство живым после последнего касания. Уничтожение узла
	 * uinput — это событие удаления устройства для libinput, и если оно
	 * приходит раньше, чем клиент успел обработать «отпускание», нажатие
	 * пропадает. На нашем железе 300 мс не хватало: экранная клавиатура
	 * меняла раскладку (это её внутреннее дело), но ни одной буквы не
	 * печатала — и это выглядело как поломка клавиатуры, а не пробника. */
	usleep(1500000);
	ioctl(узел, UI_DEV_DESTROY);
	close(узел);
	return 0;
}
