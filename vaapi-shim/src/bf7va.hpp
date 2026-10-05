/*
 * BF7: драйвер VA-API поверх аппаратного декодера MediaTek.
 *
 * Зачем он есть. Аппаратный декодер на этом телефоне достижим только через
 * Codec2 в 32-битном процессе (драйвер libvcodecdrv.so существует лишь в 32
 * битах). Всё, что умеет показывать видео в Linux — браузеры, mpv, GStreamer,
 * FFmpeg — просит железо через VA-API. Этот драйвер и есть переводчик между
 * ними: снаружи он выглядит обычным драйвером видеокарты, внутри разговаривает
 * с нашим демоном bf7-videod по сокету.
 *
 * Главная трудность перевода. VA-API отдаёт кадр уже разобранным: отдельно
 * параметры картинки, отдельно слайсы. Наш декодер, наоборот, принимает целую
 * единицу доступа в Annex-B. Поэтому слайсы склеиваются обратно, а заголовки
 * SPS и PPS приходится писать заново по полям VAPictureParameterBufferH264 —
 * оригинальных байтов VA-API не передаёт вовсе.
 *
 * Готовый кадр возвращается дескриптором dma-buf: от декодера до экрана он не
 * копируется ни разу.
 */
#ifndef BF7VA_HPP
#define BF7VA_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <va/va.h>
#include <va/va_backend.h>

namespace bf7va {

/* Буфер, созданный клиентом: параметры картинки, слайс, его данные и прочее. */
struct Buffer {
	VABufferType		type = VABufferTypeMax;
	unsigned		size = 0;	/* размер элемента */
	unsigned		count = 0;	/* сколько элементов */
	std::vector<uint8_t>	data;
	bool			mapped = false;

	/*
	 * Буфер образа поверхности памятью не владеет: он смотрит прямо в
	 * dma-buf декодера. Отображение живёт, пока образ не уничтожат.
	 */
	void			*map = nullptr;
	size_t			map_size = 0;
	int			sync_fd = -1;	/* чужой, не закрываем */
	int			exported_fd = -1;	/* выдан наружу, наш */
	uint32_t		fourcc_hint = 0;	/* формат образа */
	VASurfaceID		from_surface = VA_INVALID_ID;	/* чей кадр */
};

/* Поверхность: у нас это не своя память, а место, куда ляжет кадр декодера. */
struct Surface {
	unsigned	width = 0, height = 0;
	int		dma_fd = -1;
	unsigned	stride = 0;
	uint32_t	fourcc = 0;
	uint64_t	frame_handle = 0;	/* вернуть демону при освобождении */
	int64_t		pts = -1;		/* по нему кадр и опознаётся */
	bool		pending = false;	/* отдан декодеру, ждём */
	bool		ready = false;

	/*
	 * Своя память поверхности. Нужна до первого кадра: проигрыватели
	 * (mpv, а за ним и браузеры) сразу после создания поверхности просят
	 * вывезти её дескриптором, чтобы понять, годится ли формат. Пустую
	 * поверхность вывезти нечем, и они решают, что аппаратный декодер не
	 * поддерживает ничего. Кадр декодера, когда придёт, показывается
	 * вместо неё — эта память так и остаётся запасной.
	 */
	void		*own_handle = nullptr;	/* buffer_handle_t от gralloc */
	int		 own_fd = -1;		/* внутри own_handle, не наш */
	unsigned	 own_stride = 0;

	/*
	 * Свой буфер отдан наружу дескриптором и так и не заменён кадром
	 * декодера. GStreamer (vah264dec) вывозит поверхность один раз, при
	 * заведении пула, и дальше показывает этот самый dma-buf — кадры
	 * декодера до него иначе не доходят (пустой YUV = зелёный экран).
	 * Пока флаг стоит, каждый пришедший кадр копируется в свой буфер.
	 * Снимается, когда клиент вывозит уже готовый кадр (mpv, Firefox):
	 * им копия не нужна.
	 */
	bool		 own_exported = false;
	void		*own_map = nullptr;	/* отображение own_fd для копий */
	size_t		 own_map_size = 0;
};

struct Config {
	VAProfile	profile = VAProfileNone;
	VAEntrypoint	entrypoint = VAEntrypointVLD;
	unsigned	rt_format = VA_RT_FORMAT_YUV420;
};

/* Заголовки, которые мы уже выдали декодеру: переизлучаем только при смене. */
struct H264State {
	std::vector<uint8_t>			sps;
	std::map<unsigned, std::vector<uint8_t>> pps;
};

/* H.265: срез — его описание и байты NAL (без стартового кода). */
struct HevcSlice {
	VASliceParameterBufferHEVC	params{};
	std::vector<uint8_t>		bytes;
};

struct HevcPicture {
	VAPictureParameterBufferHEVC		params{};
	VAIQMatrixBufferHEVC			iq{};
	bool					has_params = false, has_iq = false;
	std::vector<VASliceParameterBufferHEVC>	pending_slices;	/* описания без байтов */
	std::vector<HevcSlice>			slices;
};

/* Выданные декодеру VPS/SPS/PPS и точка входа в поток (см. h265.cpp). */
struct HevcState {
	bool					started = false;
	bool					no_rasl_output = false;
	std::vector<uint8_t>			vps, sps;
	std::map<unsigned, std::vector<uint8_t>> pps;
};

static inline bool is_hevc(VAProfile p)
{
	return p == VAProfileHEVCMain;
}

/* Собранная картинка: то, что клиент отдал между vaBeginPicture и vaEndPicture. */
struct Picture {
	VASurfaceID				target = VA_INVALID_ID;
	bool					have_pic_param = false;
	VAPictureParameterBufferH264		pic;
	bool					have_iq = false;
	VAIQMatrixBufferH264			iq;
	/* слайсы: описание и его же байты */
	std::vector<std::pair<VASliceParameterBufferH264, std::vector<uint8_t>>> slices;
	HevcPicture				hevc;	/* для профилей H.265 */
};

struct Context {
	VAConfigID	config = VA_INVALID_ID;
	VAProfile	profile = VAProfileNone;
	unsigned	width = 0, height = 0;
	int		sock = -1;	/* связь с демоном */
	bool		opened = false;	/* декодер открыт */
	int64_t		next_pts = 1;
	H264State	h264;
	HevcState	h265;
	bool		eos_sent = false;	/* H.265: послан конец потока, см. flush_held */
	Picture		current;
	std::map<int64_t, VASurfaceID> inflight;	/* метка времени -> поверхность */
};

struct Driver {
	std::mutex			lock;
	std::map<VAConfigID, Config>	configs;
	std::map<VASurfaceID, Surface>	surfaces;
	std::map<VAContextID, Context>	contexts;
	std::map<VABufferID, Buffer>	buffers;
	std::map<VAImageID, VABufferID>	images;
	unsigned			next_id = 1;
};

/*
 * Сборка потока для декодера: см. h264.cpp.
 *
 * Возвращает две части. Наборы параметров (SPS и PPS) декодеру надо отдавать
 * отдельной посылкой с пометкой «настройка кодека» — если сложить их вместе с
 * картинкой, компонент MediaTek сбивается со счёта рабочих единиц и жалуется
 * на «Unknown output workId». Проверено: тот же поток, поданный раздельно,
 * декодируется.
 */
struct H264Stream {
	std::vector<uint8_t> config;	/* пусто, если ничего не поменялось */
	std::vector<uint8_t> picture;
};

H264Stream h264_access_unit(VAProfile profile, const Picture &pic,
			    H264State &state);

/*
 * Сборка потока H.265: см. h265.cpp. config — изменившиеся VPS/SPS/PPS (одной
 * посылкой настройки), picture — срезы. false — картинку не собрать, status —
 * код VA для клиента (он уйдёт на программное декодирование).
 */
struct H265Stream {
	std::vector<uint8_t> config;
	std::vector<uint8_t> picture;
};

bool h265_access_unit(VAProfile profile, const HevcPicture &pic, HevcState &state,
		      H265Stream &out, VAStatus &status);

/* Связь с демоном: см. daemon.cpp. */
int daemon_connect(void);
bool daemon_open(int sock, unsigned codec, unsigned w, unsigned h);
bool daemon_queue(int sock, const uint8_t *data, size_t size, int64_t pts,
		  uint32_t flags);
/* Забрать кадр; 0 — пока нет, 1 — есть, -1 — беда. */
/*
 * Забрать сообщение: 1 — кадр, 2 — ответ на просьбу (кадров больше нет),
 * 0 — истекло время, отрицательное — связь оборвалась.
 */
int daemon_poll_frame(int sock, int timeout_ms, int64_t *pts, int *dma_fd,
		      unsigned *width, unsigned *height, unsigned *stride,
		      uint32_t *format, uint64_t *handle);
void daemon_release(int sock, uint64_t handle);
void daemon_drain(int sock);

} /* namespace bf7va */

#endif /* BF7VA_HPP */
