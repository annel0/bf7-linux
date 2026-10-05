/*
 * BF7: протокол между демоном аппаратного декодирования и показывающей
 * стороной.
 *
 * Стороны разной разрядности: декодировать умеет только 32-битный процесс
 * (драйвер кодека существует лишь в 32 битах), показывать — только 64-битный
 * (там композитор и EGL). Поэтому все поля здесь фиксированной ширины и
 * выровнены так, чтобы раскладка совпадала у обеих сторон.
 *
 * Кадр передаётся дескриптором dma-buf через SCM_RIGHTS: содержимое остаётся
 * на месте, копирования нет.
 */
#ifndef BF7_VIDEOD_PROTO_H
#define BF7_VIDEOD_PROTO_H

#include <stdint.h>

#define BF7V_SOCKET_PATH	"/run/bf7-video.sock"
#define BF7V_MAX_PACKET		(8u * 1024u * 1024u)

enum {
	BF7V_MSG_OPEN		= 1,	/* открыть декодер */
	BF7V_MSG_QUEUE		= 2,	/* отдать пакет, дальше идут данные */
	BF7V_MSG_RELEASE	= 3,	/* кадр больше не нужен */
	BF7V_MSG_DRAIN		= 4,	/* забрать всё, что готово */
	BF7V_MSG_CLOSE		= 5,

	BF7V_MSG_FRAME		= 10,	/* от демона: кадр + дескриптор */
	BF7V_MSG_STATUS		= 11,	/* от демона: ответ на запрос */
};

enum {
	BF7V_CODEC_H264 = 0,
	BF7V_CODEC_H265,
	BF7V_CODEC_VP9,
	BF7V_CODEC_VP8,
	BF7V_CODEC_AV1,
	BF7V_CODEC_MPEG4,
	BF7V_CODEC_MPEG2,
	BF7V_CODEC_COUNT
};

#define BF7V_FLAG_END_OF_STREAM	(1u << 0)
#define BF7V_FLAG_CODEC_CONFIG	(1u << 1)	/* SPS/PPS и подобное */
/*
 * Выдать всё, что уже готово, не дожидаясь следующих картинок. Декодер
 * MediaTek по своей воле держит кадры в конвейере, а клиент VA-API ждёт
 * именно поданную картинку, прежде чем прислать следующую, — без этой
 * просьбы они запираются друг на друге. Проигрывателю флаг не нужен: он
 * подаёт поток непрерывно и на пропускной способности не экономит.
 */
#define BF7V_FLAG_DRAIN		(1u << 2)

struct bf7v_request {
	uint32_t	type;
	uint32_t	codec;		/* для OPEN */
	uint32_t	width, height;	/* для OPEN: подсказка */
	uint32_t	flags;		/* для QUEUE */
	uint32_t	size;		/* для QUEUE: сколько байт следом */
	uint32_t	pad;
	int64_t		pts_us;		/* для QUEUE */
	uint64_t	handle;		/* для RELEASE */
};

struct bf7v_frame {
	uint32_t	type;		/* BF7V_MSG_FRAME */
	uint32_t	width, height;	/* размер буфера */
	uint32_t	crop_x, crop_y;	/* видимая часть */
	uint32_t	crop_w, crop_h;
	uint32_t	stride;		/* шаг строки в пикселях */
	uint32_t	format;		/* HAL_PIXEL_FORMAT_* */
	uint32_t	pad;
	int64_t		pts_us;
	uint64_t	handle;		/* вернуть в BF7V_MSG_RELEASE */
};

/*
 * Ответ намеренно той же длины, что и кадр: получатель читает сообщения одним
 * размером и различает их по первому полю. Иначе короткий ответ съедает
 * начало следующего сообщения, и связь рассыпается на ровном месте.
 */
struct bf7v_status {
	uint32_t	type;		/* BF7V_MSG_STATUS */
	int32_t		code;		/* 0 — успех, 1 — конец потока, <0 — отказ */
	uint8_t		pad[sizeof(struct bf7v_frame) - 8];
};

#endif /* BF7_VIDEOD_PROTO_H */
