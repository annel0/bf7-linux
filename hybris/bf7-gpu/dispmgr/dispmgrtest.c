/*
 * Проба вывода кадра на панель через /dev/mtk_disp_mgr, минуя фреймбуфер.
 *
 * Буфер выделяется обычным gralloc (то есть это ION dma-buf), заполняется
 * градиентом через mmap и отдаётся дисплейному контроллеру как слой
 * наложения. Если на экране появится градиент — путь без копирования
 * работает, и его можно встраивать в платформу EGL.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <cutils/native_handle.h>
#include <hardware/gralloc.h>
#include <hybris/gralloc/gralloc.h>

#include "disp_session.h"

static int fbsize(unsigned *w, unsigned *h)
{
	struct { unsigned int v[40]; } var;
	int fd = open("/dev/fb0", O_RDONLY);

	if (fd < 0)
		return -1;
	if (ioctl(fd, 0x4600 /* FBIOGET_VSCREENINFO */, &var) < 0) {
		close(fd);
		return -1;
	}
	*w = var.v[0];
	*h = var.v[1];
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	unsigned W = 720, H = 1612;
	buffer_handle_t handle = NULL;
	uint32_t stride = 0;
	int disp, err, seconds = argc > 1 ? atoi(argv[1]) : 6;
	struct disp_session_config cfg;
	struct disp_buffer_info buf;
	struct disp_frame_cfg_t *frame;
	const native_handle_t *nh;
	void *va = MAP_FAILED;
	int dmafd = -1;
	size_t maplen;

	setvbuf(stdout, NULL, _IONBF, 0);
	fbsize(&W, &H);
	printf("панель %ux%u\n", W, H);

	hybris_gralloc_initialize(0);
	err = hybris_gralloc_allocate(W, H, HAL_PIXEL_FORMAT_RGBA_8888,
				      GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE,
				      &handle, &stride);
	if (err || !handle) {
		printf("gralloc не выделил буфер: %d\n", err);
		return 1;
	}
	nh = (const native_handle_t *)handle;
	printf("буфер: stride=%u numFds=%d numInts=%d\n", stride, nh->numFds, nh->numInts);

	maplen = (size_t)stride * H * 4;
	for (int i = 0; i < nh->numFds; i++) {
		off_t len = lseek(nh->data[i], 0, SEEK_END);

		if (len > 0 && (size_t)len < maplen)
			continue;
		va = mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, nh->data[i], 0);
		if (va != MAP_FAILED) {
			dmafd = nh->data[i];
			printf("dma-buf: fd[%d]=%d, %zu байт\n", i, dmafd, maplen);
			break;
		}
	}
	if (va == MAP_FAILED) {
		printf("не удалось отобразить буфер: %s\n", strerror(errno));
		return 2;
	}

	/* Градиент: слева направо красный, сверху вниз зелёный. */
	for (unsigned y = 0; y < H; y++) {
		unsigned char *row = (unsigned char *)va + (size_t)y * stride * 4;

		for (unsigned x = 0; x < W; x++) {
			row[x * 4 + 0] = (unsigned char)(255u * x / W);
			row[x * 4 + 1] = (unsigned char)(255u * y / H);
			row[x * 4 + 2] = 64;
			row[x * 4 + 3] = 255;
		}
	}
	printf("градиент записан\n");

	disp = open("/dev/mtk_disp_mgr", O_RDONLY);
	if (disp < 0) {
		printf("не открылся /dev/mtk_disp_mgr: %s\n", strerror(errno));
		return 3;
	}

	memset(&cfg, 0, sizeof cfg);
	cfg.type = DISP_SESSION_PRIMARY;
	cfg.device_id = 0;
	cfg.mode = DISP_SESSION_DIRECT_LINK_MODE;
	cfg.user = SESSION_USER_HWC;
	if (ioctl(disp, DISP_IOCTL_CREATE_SESSION, &cfg) < 0) {
		printf("CREATE_SESSION: %s\n", strerror(errno));
		return 4;
	}
	printf("сессия 0x%x\n", cfg.session_id);

	memset(&buf, 0, sizeof buf);
	buf.session_id = cfg.session_id;
	buf.layer_id = 0;		/* FB_LAYER, тот же слой, что у mtkfb */
	buf.layer_en = 1;
	buf.ion_fd = dmafd;
	buf.cache_sync = 1;
	if (ioctl(disp, DISP_IOCTL_PREPARE_INPUT_BUFFER, &buf) < 0) {
		printf("PREPARE_INPUT_BUFFER: %s\n", strerror(errno));
		return 5;
	}
	printf("буфер зарегистрирован: индекс %u, fence %d\n", buf.index, buf.fence_fd);

	frame = calloc(1, sizeof *frame);
	frame->setter = SESSION_USER_HWC;
	frame->session_id = cfg.session_id;
	frame->input_layer_num = 1;
	frame->mode = DISP_SESSION_DIRECT_LINK_MODE;
	frame->user = SESSION_USER_HWC;
	frame->present_fence_idx = (unsigned)-1;
	frame->prev_present_fence_fd = -1;

	struct disp_input_config *in = &frame->input_cfg[0];

	in->layer_id = 0;
	in->layer_enable = 1;
	in->buffer_source = DISP_BUFFER_ION;
	in->security = DISP_NORMAL_BUFFER;
	in->src_fmt = DISP_FORMAT_RGBA8888;
	in->src_pitch = (unsigned short)stride;
	in->src_offset_x = 0;
	in->src_offset_y = 0;
	in->src_width = (unsigned short)W;
	in->src_height = (unsigned short)H;
	in->tgt_offset_x = 0;
	in->tgt_offset_y = 0;
	in->tgt_width = (unsigned short)W;
	in->tgt_height = (unsigned short)H;
	in->next_buff_idx = buf.index;
	in->src_phy_addr = NULL;	/* пусть ядро возьмёт адрес из ION */
	in->src_fence_fd = -1;
	in->alpha_enable = 0;
	in->alpha = 0xFF;
	in->ext_sel_layer = -1;
	in->layer_type = 0;

	if (ioctl(disp, DISP_IOCTL_FRAME_CONFIG, frame) < 0) {
		printf("FRAME_CONFIG: %s\n", strerror(errno));
		return 6;
	}
	printf("кадр отправлен, смотрим %d с\n", seconds);
	sleep(seconds);

	if (buf.fence_fd >= 0)
		close(buf.fence_fd);
	close(disp);
	return 0;
}
