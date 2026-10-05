/* Подделать нажатие кнопки питания через uinput — чтобы проверить обработку. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/uinput.h>

static void ev(int fd, int t, int c, int v)
{
	struct input_event e = { .type = t, .code = c, .value = v };

	write(fd, &e, sizeof(e));
}

int main(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	struct uinput_setup us;

	if (fd < 0) { perror("/dev/uinput"); return 1; }
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_POWER);
	memset(&us, 0, sizeof(us));
	us.id.bustype = BUS_VIRTUAL;
	strcpy(us.name, "bf7-проба-кнопки");
	ioctl(fd, UI_DEV_SETUP, &us);
	ioctl(fd, UI_DEV_CREATE);
	sleep(2);                       /* дать Qt заметить устройство */
	ev(fd, EV_KEY, KEY_POWER, 1); ev(fd, EV_SYN, SYN_REPORT, 0);
	usleep(120000);
	ev(fd, EV_KEY, KEY_POWER, 0); ev(fd, EV_SYN, SYN_REPORT, 0);
	usleep(300000);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	printf("нажатие отправлено\n");
	return 0;
}
