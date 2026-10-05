/*
 * BF7: проба аппаратного декодирования из 64-битного мира.
 *
 * Подключается к демону, скармливает ему поток H.264 в виде Annex-B и
 * забирает кадры. Кадр приходит дескриптором dma-buf — проба отображает
 * первый и проверяет, что в нём не пусто: иначе «декодировано N кадров»
 * ничего не доказывает.
 *
 * Файл IVF (заголовок «DKIF») — поток VP9 или VP8: кадры идут целиком, по
 * одному на посылку, кодек берётся из заголовка.
 *
 * Показа здесь нет намеренно: сначала надо убедиться, что кадры доходят и
 * содержат картинку, и только потом выводить их на панель.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <sys/ioctl.h>

#include "bf7-videod-proto.h"

/*
 * Синхронизация кэша перед чтением dma-buf.
 *
 * Буфер пишет видеодекодер, а читает процессор. Без этого вызова процессор
 * вправе отдать то, что лежит у него в кэше, — то есть нули, если он этих
 * строк ещё не видел. Заголовка linux/dma-buf.h в sysroot может не быть,
 * поэтому определения свои.
 */
#define BF7_DMA_BUF_SYNC_READ	(1 << 0)
#define BF7_DMA_BUF_SYNC_START	(0 << 2)
#define BF7_DMA_BUF_SYNC_END	(1 << 2)

struct bf7_dma_buf_sync { uint64_t flags; };

#define BF7_DMA_BUF_IOCTL_SYNC _IOW('b', 0, struct bf7_dma_buf_sync)

static void dmabuf_sync(int fd, int start)
{
	struct bf7_dma_buf_sync s;

	s.flags = BF7_DMA_BUF_SYNC_READ |
		  (start ? BF7_DMA_BUF_SYNC_START : BF7_DMA_BUF_SYNC_END);
	if (ioctl(fd, BF7_DMA_BUF_IOCTL_SYNC, &s) < 0 && start)
		printf("  (синхронизация кэша недоступна: %s)\n", strerror(errno));
}

static int connect_daemon(const char *path)
{
	struct sockaddr_un addr;
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

static int write_all(int fd, const void *buf, size_t n)
{
	const uint8_t *p = buf;

	while (n) {
		ssize_t k = write(fd, p, n);

		if (k <= 0) {
			if (k < 0 && errno == EINTR)
				continue;
			return -1;
		}
		p += k; n -= (size_t)k;
	}
	return 0;
}

static int read_all(int fd, void *buf, size_t n)
{
	uint8_t *p = buf;

	while (n) {
		ssize_t k = read(fd, p, n);

		if (k <= 0) {
			if (k < 0 && errno == EINTR)
				continue;
			return -1;
		}
		p += k; n -= (size_t)k;
	}
	return 0;
}

/* Принять кадр вместе с дескриптором. */
static int recv_frame(int sock, struct bf7v_frame *f, int *dmafd)
{
	struct msghdr msg;
	struct iovec io;
	char control[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *c;
	ssize_t n;

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
	return (n == (ssize_t)sizeof *f) ? 0 : -1;
}

/* --- разбор Annex-B --------------------------------------------------- */

static size_t next_start(const uint8_t *b, size_t n, size_t from, size_t *sclen)
{
	size_t i;

	for (i = from; i + 3 <= n; i++) {
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
			*sclen = 3;
			return i;
		}
		if (i + 4 <= n && b[i] == 0 && b[i + 1] == 0 &&
		    b[i + 2] == 0 && b[i + 3] == 1) {
			*sclen = 4;
			return i;
		}
	}
	return n;
}

int main(int argc, char **argv)
{
	const char *file = argc > 1 ? argv[1] : "/root/codec2/test640.h264";
	unsigned w = argc > 3 ? (unsigned)atoi(argv[2]) : 640;
	unsigned h = argc > 3 ? (unsigned)atoi(argv[3]) : 480;
	uint8_t *buf;
	size_t size, pos;
	int sock, frames = 0, checked = 0;
	struct bf7v_request req;
	struct bf7v_status st;
	int64_t pts = 0;

	{
		FILE *f = fopen(file, "rb");
		long n;

		if (!f) { perror(file); return 1; }
		fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
		buf = malloc((size_t)n);
		size = fread(buf, 1, (size_t)n, f);
		fclose(f);
	}
	printf("поток %s: %zu байт, подсказка %ux%u\n", file, size, w, h);

	sock = connect_daemon(BF7V_SOCKET_PATH);
	if (sock < 0) { perror("подключение к демону"); return 1; }

	int ivf = size >= 32 && memcmp(buf, "DKIF", 4) == 0;
	uint32_t codec = BF7V_CODEC_H264;

	int hevc = strstr(file, ".hevc") || strstr(file, ".h265") || strstr(file, ".265");

	if (hevc)
		codec = BF7V_CODEC_H265;
	if (ivf) {
		codec = memcmp(buf + 8, "VP80", 4) == 0 ? BF7V_CODEC_VP8 : BF7V_CODEC_VP9;
		w = buf[12] | (unsigned)buf[13] << 8;
		h = buf[14] | (unsigned)buf[15] << 8;
		printf("IVF %.4s %ux%u\n", (const char *)buf + 8, w, h);
	}

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_OPEN;
	req.codec = codec;
	req.width = w; req.height = h;
	if (write_all(sock, &req, sizeof req) || read_all(sock, &st, sizeof st)) {
		printf("демон не ответил\n"); return 1;
	}
	if (st.code != 0) { printf("демон отказался открыть декодер\n"); return 1; }
	printf("декодер открыт\n");

	/*
	 * Каждый NAL отдаём отдельной единицей. Заголовки (SPS 7, PPS 8)
	 * помечаем как настройку кодека — иначе компонент не узнает размер
	 * кадра и откажется работать.
	 */
	pos = 0;
	if (ivf) {
		/* кадр IVF: 4 байта размера, 8 байт метки, затем сам кадр */
		pos = buf[6] | (size_t)buf[7] << 8;	/* длина заголовка файла */
		while (pos + 12 <= size) {
			size_t fl = buf[pos] | (size_t)buf[pos + 1] << 8 |
				    (size_t)buf[pos + 2] << 16 | (size_t)buf[pos + 3] << 24;

			pos += 12;
			if (pos + fl > size)
				break;
			memset(&req, 0, sizeof req);
			req.type = BF7V_MSG_QUEUE;
			req.size = (uint32_t)fl;
			req.pts_us = pts;
			pts += 33333;
			if (write_all(sock, &req, sizeof req) ||
			    write_all(sock, buf + pos, fl)) {
				printf("демон закрыл связь на подаче\n");
				return 1;
			}
			pos += fl;
		}
		pos = size;
	}
	while (pos < size) {
		size_t sclen = 0, start = next_start(buf, size, pos, &sclen);
		size_t end, dummy;

		if (start >= size)
			break;
		end = next_start(buf, size, start + sclen, &dummy);
		{
			const uint8_t *nal = buf + start + sclen;
			size_t nlen = end - start - sclen;
			unsigned type = nlen ? (nal[0] & 0x1f) : 0;

			/* H.265: тип в битах 1..6; VPS 32, SPS 33, PPS 34 — настройка */
			if (hevc) {
				unsigned t5 = nlen ? ((nal[0] >> 1) & 0x3f) : 0;

				type = (t5 >= 32 && t5 <= 34) ? 7 : 1;
			}

			/*
			 * H.265: VPS, SPS и PPS — одной посылкой настройки
			 * (HEVC_CFG=separate — по одной, как у H.264).
			 */
			if (hevc && type == 7 && !getenv("HEVC_CFG")) {
				size_t cend = end, d2;

				for (;;) {
					size_t sc = 0, s2 = next_start(buf, size, cend, &sc);
					unsigned t6;

					if (s2 >= size)
						break;
					t6 = (buf[s2 + sc] >> 1) & 0x3f;
					if (t6 < 32 || t6 > 34)
						break;
					cend = next_start(buf, size, s2 + sc, &d2);
				}
				end = cend;
			}
			memset(&req, 0, sizeof req);
			req.type = BF7V_MSG_QUEUE;
			req.size = (uint32_t)(end - start);
			req.pts_us = pts;
			if (type == 7 || type == 8)
				req.flags = BF7V_FLAG_CODEC_CONFIG;
			else
				pts += 33333;
			if (write_all(sock, &req, sizeof req) ||
			    write_all(sock, buf + start, end - start)) {
				printf("демон закрыл связь на подаче\n");
				return 1;
			}
		}
		pos = end;
	}
	printf("поток подан целиком\n");

	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_DRAIN;
	if (write_all(sock, &req, sizeof req)) { printf("не отправить DRAIN\n"); return 1; }

	for (;;) {
		struct bf7v_frame f;
		int dmafd = -1;
		uint32_t type;

		if (recv_frame(sock, &f, &dmafd) < 0) { printf("связь прервалась\n"); break; }
		type = f.type;
		if (type == BF7V_MSG_STATUS) {
			struct bf7v_status *s = (struct bf7v_status *)&f;

			printf("готово: код %d\n", s->code);
			break;
		}
		if (type != BF7V_MSG_FRAME || dmafd < 0) {
			printf("неожиданное сообщение %u\n", type);
			break;
		}
		frames++;

		/* Смотрим не первый кадр, а десятый: в начале ролика картинка
		 * может быть и правда чёрной. */
		if (!checked && frames >= 10) {
			off_t len = lseek(dmafd, 0, SEEK_END);
			void *m = len > 0 ? mmap(NULL, (size_t)len, PROT_READ,
						 MAP_SHARED, dmafd, 0) : MAP_FAILED;

			printf("кадр %d: буфер %ux%u шаг %u формат 0x%x, "
			       "видимая часть %u,%u %ux%u, pts %lld, "
			       "dma-buf %zd байт\n",
			       frames, f.width, f.height, f.stride, f.format,
			       f.crop_x, f.crop_y, f.crop_w, f.crop_h,
			       (long long)f.pts_us, (ssize_t)len);
			if (m != MAP_FAILED) {
				const uint8_t *p = m;

				dmabuf_sync(dmafd, 1);
				size_t i, nonzero = 0, n = (size_t)len;
				unsigned lo = 255, hi = 0;

				for (i = 0; i < n; i += 97) {
					if (p[i]) nonzero++;
					if (p[i] < lo) lo = p[i];
					if (p[i] > hi) hi = p[i];
				}
				printf("  содержимое: ненулевых %zu из %zu проб, "
				       "яркость от %u до %u -> %s\n",
				       nonzero, n / 97 + 1, lo, hi,
				       nonzero > 10 && hi > lo ? "КАРТИНКА ЕСТЬ"
							       : "пусто");
				/* Сохраняем кадр, чтобы посмотреть на него глазами. */
				{
					const char *out = getenv("BF7V_DUMP");
					FILE *o = out ? fopen(out, "wb") : NULL;

					if (o) {
						fwrite(m, 1, (size_t)len, o);
						fclose(o);
						printf("  кадр сохранён в %s\n", out);
					}
				}
				dmabuf_sync(dmafd, 0);
				munmap(m, (size_t)len);
			} else {
				printf("  буфер не отобразить: %s\n", strerror(errno));
			}
			checked = 1;
		}

		close(dmafd);
		memset(&req, 0, sizeof req);
		req.type = BF7V_MSG_RELEASE;
		req.handle = f.handle;
		if (write_all(sock, &req, sizeof req)) break;
	}

	printf("ИТОГ: получено кадров %d\n", frames);
	memset(&req, 0, sizeof req);
	req.type = BF7V_MSG_CLOSE;
	write_all(sock, &req, sizeof req);
	close(sock);
	return frames > 0 ? 0 : 1;
}
