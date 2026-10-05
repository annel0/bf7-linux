/*
 * BF7: связь драйвера с демоном аппаратного декодирования.
 *
 * Демон живёт отдельным 32-битным процессом — иначе нельзя, драйвер кодека
 * существует только в 32 битах. Сюда он отдаёт готовые кадры дескрипторами
 * dma-buf, и дальше они идут до самого экрана без копирования.
 */
#include "bf7va.hpp"
#include "bf7-videod-proto.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace bf7va {

static bool write_all(int fd, const void *buf, size_t n)
{
	const uint8_t *p = (const uint8_t *)buf;

	while (n) {
		/*
		 * send с MSG_NOSIGNAL, а не write: если демон исчез, write
		 * присылает SIGPIPE, и хозяин процесса умирает целиком. Мы
		 * библиотека внутри чужой программы — убивать её нельзя ни
		 * при каких обстоятельствах, отказ надо вернуть кодом.
		 */
		ssize_t k = send(fd, p, n, MSG_NOSIGNAL);

		if (k <= 0) {
			if (k < 0 && errno == EINTR)
				continue;
			return false;
		}
		p += k;
		n -= (size_t)k;
	}
	return true;
}

int daemon_connect(void)
{
	struct sockaddr_un addr;
	const char *path = getenv("BF7V_SOCKET") ?: BF7V_SOCKET_PATH;
	int s = socket(AF_UNIX, SOCK_STREAM, 0);

	if (s < 0)
		return -1;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
	if (connect(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
		close(s);
		return -1;
	}
	return s;
}

/* Принять сообщение демона вместе с дескриптором, если он есть. */
static int recv_msg(int sock, struct bf7v_frame *f, int *dmafd, int timeout_ms)
{
	struct msghdr msg;
	struct iovec io;
	char control[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *c;
	ssize_t n;

	if (timeout_ms >= 0) {
		struct pollfd p = { sock, POLLIN, 0 };
		int r = poll(&p, 1, timeout_ms);

		if (r == 0)
			return 0;
		if (r < 0)
			return -1;
	}

	memset(&msg, 0, sizeof msg);
	io.iov_base = f;
	io.iov_len = sizeof *f;
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof control;

	do {
		n = recvmsg(sock, &msg, 0);
	} while (n < 0 && errno == EINTR);
	if (n <= 0)
		return -1;

	*dmafd = -1;
	for (c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
			memcpy(dmafd, CMSG_DATA(c), sizeof(int));
	return 1;
}

bool daemon_open(int sock, unsigned codec, unsigned w, unsigned h)
{
	struct bf7v_request req;
	struct bf7v_frame reply;
	int fd = -1;

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_OPEN;
	req.codec = codec;
	req.width = w;
	req.height = h;
	if (!write_all(sock, &req, sizeof req))
		return false;
	if (recv_msg(sock, &reply, &fd, -1) != 1)
		return false;
	if (fd >= 0)
		close(fd);
	return ((struct bf7v_status *)&reply)->code == 0;
}

bool daemon_queue(int sock, const uint8_t *data, size_t size, int64_t pts,
		  uint32_t flags)
{
	struct bf7v_request req;

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_QUEUE;
	req.size = (uint32_t)size;
	req.pts_us = pts;
	req.flags = flags;
	return write_all(sock, &req, sizeof req) && write_all(sock, data, size);
}

int daemon_poll_frame(int sock, int timeout_ms, int64_t *pts, int *dma_fd,
		      unsigned *width, unsigned *height, unsigned *stride,
		      uint32_t *format, uint64_t *handle)
{
	struct bf7v_frame f;
	int fd = -1, r = recv_msg(sock, &f, &fd, timeout_ms);

	if (r <= 0)
		return r;
	if (f.type != BF7V_MSG_FRAME || fd < 0) {
		if (fd >= 0)
			close(fd);
		return 2;	/* это ответ на запрос, не кадр */
	}
	*pts = f.pts_us;
	*dma_fd = fd;
	*width = f.crop_w ? f.crop_w : f.width;
	*height = f.crop_h ? f.crop_h : f.height;
	*stride = f.stride;
	*format = f.format;
	*handle = f.handle;
	return 1;
}

void daemon_release(int sock, uint64_t handle)
{
	struct bf7v_request req;

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_RELEASE;
	req.handle = handle;
	write_all(sock, &req, sizeof req);
}

void daemon_drain(int sock)
{
	struct bf7v_request req;

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_DRAIN;
	write_all(sock, &req, sizeof req);
}

} /* namespace bf7va */
