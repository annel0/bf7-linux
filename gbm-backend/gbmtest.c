/*
 * BF7: проверка бэкенда GBM целиком.
 *
 * Идём тем же путём, каким пойдёт Xwayland: создаём устройство GBM на узле
 * рендера, просим буфер с модификаторами, забираем дескриптор плоскости —
 * и показываем его на панели через наш узел KMS. Если полосы окажутся на
 * месте, значит бэкенд отдаёт настоящую, годную к показу память.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	static const uint64_t mods[] = { DRM_FORMAT_MOD_LINEAR };
	struct gbm_device *gbm;
	struct gbm_bo *bo;
	int fd, drm_fd, plane_fd, i, crtc_id;
	uint32_t stride = 0, handle = 0, fb_id = 0;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	void *map_data = NULL, *map = NULL;
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeModeInfo mode;
	unsigned W, H, y;

	drm_fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) { perror("card1"); return 1; }
	res = drmModeGetResources(drm_fd);
	for (i = 0; res && i < res->count_connectors; i++) {
		conn = drmModeGetConnector(drm_fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes)
			break;
		if (conn) drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn) { fprintf(stderr, "нет соединителя\n"); return 1; }
	mode = conn->modes[0];
	W = mode.hdisplay; H = mode.vdisplay;
	crtc_id = res->crtcs[0];

	fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror(node); return 1; }

	gbm = gbm_create_device(fd);
	if (!gbm) { fprintf(stderr, "gbm_create_device отказал\n"); return 1; }
	printf("устройство GBM: бэкенд %s\n", gbm_device_get_backend_name(gbm));

	printf("формат ARGB8888 поддержан: %d\n",
	       gbm_device_is_format_supported(gbm, GBM_FORMAT_ARGB8888,
					      GBM_BO_USE_RENDERING));

	bo = gbm_bo_create_with_modifiers(gbm, W, H, GBM_FORMAT_ARGB8888,
					  mods, 1);
	if (!bo) {
		printf("с модификаторами не вышло (%s), пробую без них\n",
		       strerror(errno));
		bo = gbm_bo_create(gbm, W, H, GBM_FORMAT_ARGB8888,
				   GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
	}
	if (!bo) { fprintf(stderr, "gbm_bo_create отказал: %s\n", strerror(errno)); return 1; }

	printf("буфер %ux%u: шаг %u, плоскостей %d, модификатор 0x%llx\n",
	       gbm_bo_get_width(bo), gbm_bo_get_height(bo), gbm_bo_get_stride(bo),
	       gbm_bo_get_plane_count(bo),
	       (unsigned long long)gbm_bo_get_modifier(bo));
	printf("смещение плоскости 0: %u, шаг плоскости 0: %u\n",
	       gbm_bo_get_offset(bo, 0), gbm_bo_get_stride_for_plane(bo, 0));

	plane_fd = gbm_bo_get_fd_for_plane(bo, 0);
	printf("дескриптор плоскости 0: %d\n", plane_fd);
	if (plane_fd < 0) { fprintf(stderr, "дескриптора нет\n"); return 1; }

	/* Заполняем через gbm_bo_map — заодно проверяем и его. */
	map = gbm_bo_map(bo, 0, 0, W, H, GBM_BO_TRANSFER_WRITE, &stride, &map_data);
	if (!map) {
		fprintf(stderr, "gbm_bo_map отказал: %s\n", strerror(errno));
		return 1;
	}
	printf("отображён, шаг при отображении %u\n", stride);
	for (y = 0; y < H; y++) {
		static const uint32_t bands[] = {
			0x00ff00ff,	/* пурпурный */
			0x0000ffff,	/* голубой */
			0x00ffff00,	/* жёлтый */
		};
		uint32_t c = bands[y * 3 / H];
		uint32_t *row = (uint32_t *)((char *)map + (size_t)y * stride);
		unsigned x;

		for (x = 0; x < W; x++)
			row[x] = c;
	}
	gbm_bo_unmap(bo, map_data);

	if (drmPrimeFDToHandle(drm_fd, plane_fd, &handle)) { perror("PrimeFDToHandle"); return 1; }
	handles[0] = handle;
	pitches[0] = gbm_bo_get_stride(bo);
	if (drmModeAddFB2(drm_fd, W, H, DRM_FORMAT_XRGB8888, handles, pitches,
			  offsets, &fb_id, 0)) { perror("AddFB2"); return 1; }
	if (drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &conn->connector_id, 1, &mode)) {
		perror("SetCrtc"); return 1;
	}
	printf("показано через бэкенд GBM, держу\n");
	pause();
	return 0;
}
