/*
 * Простой C-интерфейс к аппаратному видеодекодеру MediaTek.
 *
 * Зачем он нужен. Декодер доступен только через Codec2, а это C++ API поверх
 * bionic-овской libc++: std::shared_ptr, std::vector, C2Param. Вызывать такое
 * из обычной glibc-программы нельзя — раскладки и аллокаторы разные. Поэтому
 * здесь стоит прослойка, собранная NDK (bionic + libc++), которая наружу
 * отдаёт только простой C.
 *
 * Со стороны Linux библиотека грузится линковщиком libhybris, ровно как
 * андроидные libEGL и gralloc.
 *
 * Кадр отдаётся дескриптором dma-buf, то есть тем же, что уже умеет наш вывод
 * слоем наложения и EGLImage: декодирование и показ складываются без единого
 * копирования.
 */
#ifndef BF7C2DEC_H
#define BF7C2DEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bf7c2_decoder bf7c2_decoder;

typedef enum {
	BF7C2_CODEC_H264 = 0,
	BF7C2_CODEC_H265,
	BF7C2_CODEC_VP9,
	BF7C2_CODEC_VP8,
	BF7C2_CODEC_AV1,
	BF7C2_CODEC_MPEG4,
	BF7C2_CODEC_MPEG2,
	BF7C2_CODEC_COUNT
} bf7c2_codec;

/* Флаги пакета. */
#define BF7C2_FLAG_END_OF_STREAM	(1u << 0)
#define BF7C2_FLAG_CODEC_CONFIG		(1u << 1)	/* SPS/PPS и подобное */

typedef struct {
	int		dma_fd;		/* дескриптор dma-buf кадра, -1 если нет */
	uint32_t	width;
	uint32_t	height;
	uint32_t	stride;
	uint32_t	format;		/* HAL_PIXEL_FORMAT_* */
	int64_t		timestamp_us;
	uint64_t	handle;		/* вернуть в bf7c2_release_frame */
} bf7c2_frame;

/* Название компонента, который будет создан для кодека (для диагностики). */
const char *bf7c2_component_name(bf7c2_codec codec);

/* Открыть декодер. width и height — подсказка; поток может их переопределить. */
bf7c2_decoder *bf7c2_open(bf7c2_codec codec, uint32_t width, uint32_t height);

/* Отдать декодеру пакет. Возвращает 0 при успехе. */
int bf7c2_queue(bf7c2_decoder *dec, const void *data, size_t size,
		int64_t timestamp_us, uint32_t flags);

/* Забрать готовый кадр. timeout_ms < 0 — ждать бесконечно.
 * Возвращает 1 если кадр получен, 0 если вышло время, отрицательное при ошибке. */
int bf7c2_dequeue(bf7c2_decoder *dec, bf7c2_frame *frame, int timeout_ms);

/* Вернуть кадр декодеру. Обязательно: иначе пул буферов кончится. */
void bf7c2_release_frame(bf7c2_decoder *dec, uint64_t handle);

void bf7c2_close(bf7c2_decoder *dec);

/* Текст последней ошибки; никогда не NULL. */
const char *bf7c2_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* BF7C2DEC_H */
