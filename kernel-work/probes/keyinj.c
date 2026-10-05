/*
 * BF7: ввод с клавиатуры через uinput.
 *
 * Экран блокировки Phosh берёт клавиатурный ввод
 * (zwlr_layer_surface_v1.set_keyboard_interactivity(1)), а вот касания её
 * обработчик жестов проглатывает. Это средство позволяет набрать пароль
 * мимо экрана: строка читается со своего входа с погашенным отображением,
 * так что она не видна ни в терминале, ни в журнале, ни в списке процессов.
 *
 * Сборка:  gcc -O2 -o keyinj keyinj.c
 * Запуск:  ./keyinj            — спросит строку молча и наберёт её + Enter
 *          ./keyinj --показать — то же, но с отображением (для отладки)
 */
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int узел;

static void событие(int тип, int код, int знач)
{
	struct input_event e;
	memset(&e, 0, sizeof e);
	e.type = тип; e.code = код; e.value = знач;
	if (write(узел, &e, sizeof e) != sizeof e)
		perror("write");
}

static void кадр(void) { событие(EV_SYN, SYN_REPORT, 0); }

/* Раскладка US: код клавиши и нужен ли Shift. */
static int код_для(char c, int *шифт)
{
	static const char цифры[] = ")!@#$%^&*(";
	static const char ряд1[] = "qwertyuiop";
	static const char ряд2[] = "asdfghjkl";
	static const char ряд3[] = "zxcvbnm";
	const char *п;

	*шифт = 0;
	if (c >= '1' && c <= '9') return KEY_1 + (c - '1');
	if (c == '0') return KEY_0;
	if ((п = strchr(цифры, c)) && c) { *шифт = 1;
		int i = (int)(п - цифры);
		return i == 0 ? KEY_0 : KEY_1 + i - 1; }
	if (c >= 'A' && c <= 'Z') { *шифт = 1; c = (char)(c - 'A' + 'a'); }
	if ((п = strchr(ряд1, c))) return KEY_Q + (int)(п - ряд1);
	if ((п = strchr(ряд2, c))) return KEY_A + (int)(п - ряд2);
	if ((п = strchr(ряд3, c))) return KEY_Z + (int)(п - ряд3);
	switch (c) {
	case ' ':  return KEY_SPACE;
	case '-':  return KEY_MINUS;
	case '_':  *шифт = 1; return KEY_MINUS;
	case '=':  return KEY_EQUAL;
	case '+':  *шифт = 1; return KEY_EQUAL;
	case '.':  return KEY_DOT;
	case ',':  return KEY_COMMA;
	case '/':  return KEY_SLASH;
	case ';':  return KEY_SEMICOLON;
	case '\'': return KEY_APOSTROPHE;
	}
	return -1;
}

static void нажать(int код, int шифт)
{
	if (шифт) { событие(EV_KEY, KEY_LEFTSHIFT, 1); кадр(); }
	событие(EV_KEY, код, 1); кадр();
	usleep(25000);
	событие(EV_KEY, код, 0); кадр();
	if (шифт) { событие(EV_KEY, KEY_LEFTSHIFT, 0); кадр(); }
	usleep(45000);
}

int main(int argc, char **argv)
{
	int показать = argc > 1 && (!strcmp(argv[1], "--показать") ||
				    !strcmp(argv[1], "--show"));

	узел = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (узел < 0) { perror("/dev/uinput"); return 1; }
	ioctl(узел, UI_SET_EVBIT, EV_KEY);
	ioctl(узел, UI_SET_EVBIT, EV_SYN);
	for (int k = KEY_ESC; k <= KEY_COMPOSE; k++)
		ioctl(узел, UI_SET_KEYBIT, k);

	struct uinput_setup s;
	memset(&s, 0, sizeof s);
	s.id.bustype = BUS_VIRTUAL;
	s.id.vendor = 0xbf07; s.id.product = 0x0002;
	snprintf(s.name, sizeof s.name, "bf7-virt-keyboard");
	ioctl(узел, UI_DEV_SETUP, &s);
	if (ioctl(узел, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }

	fprintf(stderr, "строка (не отображается), затем Enter: ");
	struct termios было, стало;
	int тихо = !показать && tcgetattr(STDIN_FILENO, &было) == 0;
	if (тихо) { стало = было; стало.c_lflag &= ~(tcflag_t)ECHO;
		    tcsetattr(STDIN_FILENO, TCSAFLUSH, &стало); }

	char строка[256];
	if (!fgets(строка, sizeof строка, stdin)) строка[0] = 0;
	if (тихо) { tcsetattr(STDIN_FILENO, TCSAFLUSH, &было); fprintf(stderr, "\n"); }
	строка[strcspn(строка, "\r\n")] = 0;

	/* Композитору нужно время заметить новое устройство. */
	fprintf(stderr, "жду, пока композитор заметит клавиатуру...\n");
	sleep(2);

	int набрано = 0, пропущено = 0;
	for (char *p = строка; *p; p++) {
		int шифт, код = код_для(*p, &шифт);
		if (код < 0) { пропущено++; continue; }
		нажать(код, шифт);
		набрано++;
	}
	нажать(KEY_ENTER, 0);
	fprintf(stderr, "набрано знаков: %d%s, затем Enter\n", набрано,
		пропущено ? " (часть знаков раскладка не знает)" : "");

	memset(строка, 0, sizeof строка);
	usleep(300000);
	ioctl(узел, UI_DEV_DESTROY);
	close(узел);
	return 0;
}
