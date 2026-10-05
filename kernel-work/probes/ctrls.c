/* Перечислить ручки /dev/video0 так, как их видит обычная программа. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/video0";
	int fd = open(dev, O_RDWR);
	struct v4l2_queryctrl q;

	if (fd < 0) { perror(dev); return 1; }
	memset(&q, 0, sizeof(q));
	q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
	while (ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0) {
		struct v4l2_control c = { .id = q.id };
		const char *t = q.type == V4L2_CTRL_TYPE_BOOLEAN ? "да/нет" :
				q.type == V4L2_CTRL_TYPE_MENU ? "список" : "число";

		ioctl(fd, VIDIOC_G_CTRL, &c);
		printf("  %-28s %-7s %6d..%-6d сейчас %6d (по умолчанию %d)\n",
		       q.name, t, q.minimum, q.maximum, c.value, q.default_value);
		q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
	}
	close(fd);
	return 0;
}
