/*
 * BF7: нажать кнопку программно — для проверок без человека у телефона и
 * для будильника.
 *
 * Заводит устройство uinput с одной клавишей KEY_POWER, ждёт, пока
 * композитор его заметит, и делает короткое нажатие. Событие идёт тем же
 * путём, что и от настоящей кнопки (mtk-pmic-keys): через libinput в phoc,
 * оттуда в Phosh. Демоны, читающие /dev/input/event1 напрямую, его не
 * видят — для них это другое устройство.
 *
 * Композитор принимает новое устройство не сразу (как и у bf7-touchinj):
 * без паузы первое нажатие теряется.
 *
 * Вторая клавиша — wakeup (KEY_WAKEUP): безвредное «действие пользователя».
 * Им будильник зажигает экран так, чтобы gsd-power сам вышел из простоя.
 * Пробовал протоколом org_kde_kwin_idle (simulate_user_activity) — в wlroots
 * он сбрасывает таймер лишь самого приславшего клиента, gsd его не видит.
 *
 * Escape (escape) сворачивает сетку приложений phosh — для проверок.
 *
 * Вызов:   bf7-knopka [power|wakeup|escape] [мс удержания=100]
 * Сборка:  gcc -O2 -o bf7-knopka bf7-knopka.c
 */
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void sobytie(int fd, int tip, int kod, int znach)
{
	struct input_event e;

	memset(&e, 0, sizeof e);
	e.type = tip;
	e.code = kod;
	e.value = znach;
	if (write(fd, &e, sizeof e) != sizeof e)
		perror("bf7-knopka: write");
}

int main(int argc, char **argv)
{
	int klavisha = KEY_POWER;
	int uderzhanie = 100;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "wakeup") == 0)
			klavisha = KEY_WAKEUP;
		else if (strcmp(argv[i], "escape") == 0)
			klavisha = KEY_ESC;	/* сворачивает сетку приложений phosh */
		else if (strcmp(argv[i], "power") != 0)
			uderzhanie = atoi(argv[i]);
	}
	struct uinput_setup nastr;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	if (fd < 0) {
		perror("bf7-knopka: /dev/uinput");
		return 1;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, klavisha);
	memset(&nastr, 0, sizeof nastr);
	nastr.id.bustype = BUS_VIRTUAL;
	strcpy(nastr.name, "bf7-knopka");
	if (ioctl(fd, UI_DEV_SETUP, &nastr) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("bf7-knopka: uinput");
		return 1;
	}

	usleep(1500 * 1000);	/* пусть композитор заметит устройство */
	sobytie(fd, EV_KEY, klavisha, 1);
	sobytie(fd, EV_SYN, SYN_REPORT, 0);
	usleep(uderzhanie * 1000);
	sobytie(fd, EV_KEY, klavisha, 0);
	sobytie(fd, EV_SYN, SYN_REPORT, 0);
	usleep(500 * 1000);	/* отпускание должно дойти до уничтожения */

	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}
