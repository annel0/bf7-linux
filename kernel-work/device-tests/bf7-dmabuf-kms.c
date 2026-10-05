// SPDX-License-Identifier: GPL-2.0
/*
 * BF7: показ чужого dma-buf через наш узел KMS, без единого копирования.
 *
 * Буфер берём из ION — той же кучи, из которой его берут gralloc и декодер,
 * так что путь проверяется ровно тот, которым потом пойдут кадры GPU и видео.
 * Заполняем полосами: по снимку экрана сразу видно, дошло ли изображение и не
 * перепутан ли порядок байтов.
 *
 * Сборка на устройстве:
 *   gcc -O2 -o bf7-dmabuf-kms bf7-dmabuf-kms.c -ldrm -I/usr/include/libdrm
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

/* Старый ABI ION: тот, что у MediaTek. */
typedef int ion_user_handle_t;

struct ion_allocation_data {
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	ion_user_handle_t handle;
};

struct ion_fd_data {
	ion_user_handle_t handle;
	int fd;
};

#define ION_IOC_MAGIC	'I'
#define ION_IOC_ALLOC	_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_SHARE	_IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)

#define ALIGN_UP(x, a)	(((x) + (a) - 1) & ~((a) - 1))

/*
 * Кучи пробуем в осмысленном порядке: сперва мультимедийная (из неё берёт
 * буферы gralloc, туда же пишет декодер), затем системная. Перебор вслепую
 * может наткнуться на защищённую кучу, чью память M4U нам не отобразит.
 */
static int ion_alloc_dmabuf(int ion_fd, size_t len, unsigned int *heap_out)
{
	static const unsigned int order[] = { 10, 0, 1, 2, 3, 4 };
	unsigned int n;

	for (n = 0; n < sizeof(order) / sizeof(order[0]); n++) {
		unsigned int bit = order[n];
		struct ion_allocation_data alloc = {
			.len = len,
			.align = 4096,
			.heap_id_mask = 1u << bit,
			.flags = 0,
		};
		struct ion_fd_data share;

		if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0)
			continue;

		share.handle = alloc.handle;
		share.fd = -1;
		if (ioctl(ion_fd, ION_IOC_SHARE, &share) < 0)
			continue;

		*heap_out = bit;
		return share.fd;
	}

	return -1;
}

int main(void)
{
	static const uint32_t bands[] = {
		0x00ff0000,	/* красный */
		0x0000ff00,	/* зелёный */
		0x000000ff,	/* синий */
		0x00ffffff,	/* белый */
	};
	const int nbands = sizeof(bands) / sizeof(bands[0]);
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc;
	drmModeModeInfo mode;
	uint32_t handle = 0, fb_id = 0;
	uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
	unsigned int heap = 0, pitch_px, y;
	size_t size;
	uint8_t *map;
	int drm_fd, ion_fd, dmabuf_fd, i, crtc_id = -1;

	drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		perror("open /dev/dri/card1");
		return 1;
	}

	res = drmModeGetResources(drm_fd);
	if (!res) {
		perror("drmModeGetResources");
		return 1;
	}

	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(drm_fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0)
			break;
		if (conn)
			drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn) {
		fprintf(stderr, "нет подключённого соединителя\n");
		return 1;
	}

	mode = conn->modes[0];
	enc = drmModeGetEncoder(drm_fd, conn->encoder_id ? conn->encoder_id :
						conn->encoders[0]);
	if (enc) {
		crtc_id = enc->crtc_id ? (int)enc->crtc_id :
					 (int)res->crtcs[0];
		drmModeFreeEncoder(enc);
	} else {
		crtc_id = res->crtcs[0];
	}

	printf("режим %ux%u, соединитель %u, crtc %d\n",
	       mode.hdisplay, mode.vdisplay, conn->connector_id, crtc_id);

	/* Шаг строки как у вендорского кода: выравнивание на 32 пикселя. */
	pitch_px = ALIGN_UP(mode.hdisplay, 32);
	size = (size_t)pitch_px * mode.vdisplay * 4;

	ion_fd = open("/dev/ion", O_RDWR | O_CLOEXEC);
	if (ion_fd < 0) {
		perror("open /dev/ion");
		return 1;
	}

	dmabuf_fd = ion_alloc_dmabuf(ion_fd, size, &heap);
	if (dmabuf_fd < 0) {
		fprintf(stderr, "ни одна куча ION не отдала %zu байт\n", size);
		return 1;
	}
	printf("ION: куча %u, fd %d, %zu байт, шаг %u пикселей\n",
	       heap, dmabuf_fd, size, pitch_px);

	map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   dmabuf_fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap dma-buf");
		return 1;
	}

	for (y = 0; y < mode.vdisplay; y++) {
		uint32_t color = bands[y * nbands / mode.vdisplay];
		uint32_t *row = (uint32_t *)(map + (size_t)y * pitch_px * 4);
		unsigned int x;

		for (x = 0; x < pitch_px; x++)
			row[x] = color;
	}
	msync(map, size, MS_SYNC);

	if (drmPrimeFDToHandle(drm_fd, dmabuf_fd, &handle)) {
		perror("drmPrimeFDToHandle");
		return 1;
	}
	printf("импорт удался: handle %u\n", handle);

	handles[0] = handle;
	pitches[0] = pitch_px * 4;
	if (drmModeAddFB2(drm_fd, mode.hdisplay, mode.vdisplay,
			  DRM_FORMAT_XRGB8888, handles, pitches, offsets,
			  &fb_id, 0)) {
		perror("drmModeAddFB2");
		return 1;
	}
	printf("кадр создан: fb %u\n", fb_id);

	if (drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0,
			   &conn->connector_id, 1, &mode)) {
		perror("drmModeSetCrtc");
		return 1;
	}
	printf("режим установлен, кадр показан\n");

	/*
	 * Держим устройство открытым: как только последняя ссылка на кадр
	 * пропадёт, конвейер выключится и панель уйдёт на чёрное.
	 */
	printf("держу кадр, Ctrl-C чтобы отпустить\n");
	pause();
	return 0;
}
