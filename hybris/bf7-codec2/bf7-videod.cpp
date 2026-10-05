/*
 * BF7: демон аппаратного декодирования видео.
 *
 * Зачем отдельный процесс. Настоящий драйвер кодека на этом устройстве
 * существует только 32-битным (/vendor/lib/libvcodecdrv.so, 64-битной версии
 * нет вовсе), поэтому декодировать обязан 32-битный процесс. А показывать
 * кадры должен 64-битный: там наш композитор, EGL и всё остальное. Дескриптор
 * dma-buf спокойно переходит между процессами через SCM_RIGHTS, так что
 * разрыв в разрядности ничего не стоит — кадр по-прежнему не копируется.
 *
 * Сам демон живёт в мире bionic: это обычный андроидный бинарник, libhybris
 * ему не нужен.
 *
 * Сборка: sh build-videod32.sh
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <dlfcn.h>

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <C2Component.h>
#include <C2Config.h>
#include <C2PlatformSupport.h>
#include <C2AllocatorGralloc.h>
#include <C2Buffer.h>
#include <C2Work.h>
#include <system/graphics.h>

#include "bf7-videod-proto.h"

namespace {

/* ------------------------------------------------------------------ */
/* Приём кадров от компонента                                          */
/* ------------------------------------------------------------------ */

struct Decoded {
	struct bf7v_frame	meta;
	int			fd;	/* наш экземпляр, закрываем сами */
	std::shared_ptr<C2Buffer> keep;	/* держит буфер живым до release */
};

class Listener : public C2Component::Listener {
public:
	void onWorkDone_nb(std::weak_ptr<C2Component>,
			   std::list<std::unique_ptr<C2Work>> works) override
	{
		std::lock_guard<std::mutex> lock(mLock);

		for (auto &w : works) {
			mDoneWorks++;
			if (!w || w->worklets.empty() || !w->worklets.front())
				continue;

			const C2FrameData &out = w->worklets.front()->output;

			if (out.flags & C2FrameData::FLAG_END_OF_STREAM)
				mEos = true;

			for (const auto &buf : out.buffers) {
				if (!buf)
					continue;
				for (const auto &b : buf->data().graphicBlocks())
					take(b, buf, out.ordinal.timestamp.peekull());
			}
		}
		mCond.notify_all();
	}

	void onTripped_nb(std::weak_ptr<C2Component>,
			  std::vector<std::shared_ptr<C2SettingResult>>) override {}

	void onError_nb(std::weak_ptr<C2Component>, uint32_t code) override
	{
		fprintf(stderr, "videod: компонент сообщил об ошибке %u\n", code);
		std::lock_guard<std::mutex> lock(mLock);
		mError = true;
		mCond.notify_all();
	}

	/* Забрать готовый кадр; 0 — пока нечего отдавать. */
	bool pop(Decoded *out, int timeout_ms)
	{
		std::unique_lock<std::mutex> lock(mLock);

		if (mReady.empty() && timeout_ms != 0)
			mCond.wait_for(lock, std::chrono::milliseconds(
					       timeout_ms < 0 ? 5000 : timeout_ms),
				       [&] { return !mReady.empty() || mEos || mError; });
		if (mReady.empty())
			return false;
		*out = std::move(mReady.front());
		mReady.pop_front();
		return true;
	}

	bool eos() { std::lock_guard<std::mutex> l(mLock); return mEos; }
	uint64_t doneWorks() { std::lock_guard<std::mutex> l(mLock); return mDoneWorks; }
	bool error() { std::lock_guard<std::mutex> l(mLock); return mError; }

private:
	void take(const C2ConstGraphicBlock &b,
		  const std::shared_ptr<C2Buffer> &buf, unsigned long long pts)
	{
		const C2Handle *h = b.handle();
		native_handle_t *nh = h ?
			android::UnwrapNativeCodec2GrallocHandle(h) : nullptr;
		Decoded d;
		uint32_t gw = 0, gh = 0, gfmt = 0, gstride = 0, gen = 0;
		uint64_t usage = 0, igbp = 0;
		uint32_t slot = 0;
		int fd = -1;

		if (h)
			android::_UnwrapNativeCodec2GrallocMetadata(
				h, &gw, &gh, &gfmt, &usage, &gstride,
				&gen, &igbp, &slot);

		/*
		 * Нужный дескриптор ищем пробой по размеру, а не берём первый.
		 * У буфера gralloc их несколько, и постоянного номера у
		 * настоящего нет: у первого размер вообще не берётся, у другого
		 * лежит страница метаданных. Тот же приём, что и у буферов GPU.
		 */
		if (nh) {
			size_t need = (size_t)(gstride ? gstride : b.width()) *
				      b.height();
			off_t best = 0;
			int i;

			for (i = 0; i < nh->numFds; i++) {
				off_t len = lseek(nh->data[i], 0, SEEK_END);

				if (len > best && (size_t)len >= need) {
					best = len;
					fd = nh->data[i];
				}
			}
			if (fd < 0 && nh->numFds > 0)
				fd = nh->data[0];	/* хоть что-то */
		}

		/*
		 * Свой экземпляр дескриптора: исходный принадлежит буферу
		 * компонента, а мы отправим его наружу и закроем у себя.
		 * native_handle_delete дескрипторы не закрывает, только
		 * освобождает обёртку.
		 */
		d.fd = fd >= 0 ? dup(fd) : -1;
		if (nh)
			native_handle_delete(nh);

		memset(&d.meta, 0, sizeof d.meta);
		d.meta.type = BF7V_MSG_FRAME;
		d.meta.width = b.width();
		d.meta.height = b.height();
		d.meta.crop_x = b.crop().left;
		d.meta.crop_y = b.crop().top;
		d.meta.crop_w = b.crop().width;
		d.meta.crop_h = b.crop().height;
		d.meta.stride = gstride ? gstride : b.width();
		d.meta.format = gfmt;
		d.meta.pts_us = (int64_t)pts;
		d.meta.handle = ++mSeq;
		d.keep = buf;

		if (d.fd < 0) {
			fprintf(stderr, "videod: у кадра нет дескриптора\n");
			return;
		}
		mReady.push_back(std::move(d));

		/*
		 * Готовые кадры уходят клиенту только между его просьбами. Клиент,
		 * который кадры не забирает («ffmpeg -f null» с кадрами в
		 * видеопамяти не просит выдачи вовсе), копил их здесь без предела:
		 * у каждого своя копия dma-buf и буфер gralloc с клиентом ION —
		 * 1024 дескриптора, «Too many open files», падение демона и
		 * «Failed to end picture decode» у всех. Старые лишние кадры
		 * выбрасываем.
		 */
		while (mReady.size() > kMaxReady) {
			close(mReady.front().fd);
			mReady.pop_front();
			if (!mSkazal) {
				mSkazal = true;
				fprintf(stderr, "videod: клиент не забирает кадры — "
					"держу не больше %zu готовых\n", kMaxReady);
			}
		}
	}

	std::mutex mLock;
	std::condition_variable mCond;
	std::deque<Decoded> mReady;
	static const size_t kMaxReady = 12;
	uint64_t mDoneWorks = 0;
	bool mSkazal = false;
	uint64_t mSeq = 0;
	bool mEos = false;
	bool mError = false;
};

/* ------------------------------------------------------------------ */
/* Приём и отправка по сокету                                          */
/* ------------------------------------------------------------------ */

bool read_all(int fd, void *buf, size_t n)
{
	uint8_t *p = (uint8_t *)buf;

	while (n) {
		ssize_t k = read(fd, p, n);

		if (k == 0)
			return false;
		if (k < 0) {
			if (errno == EINTR)
				continue;
			return false;
		}
		p += k;
		n -= (size_t)k;
	}
	return true;
}

bool write_all(int fd, const void *buf, size_t n)
{
	const uint8_t *p = (const uint8_t *)buf;

	while (n) {
		ssize_t k = write(fd, p, n);

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

/* Отправка заголовка кадра вместе с дескриптором. */
bool send_frame(int sock, const struct bf7v_frame *meta, int dmafd)
{
	struct msghdr msg;
	struct iovec io;
	char control[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *cmsg;

	memset(&msg, 0, sizeof msg);
	memset(control, 0, sizeof control);
	io.iov_base = (void *)meta;
	io.iov_len = sizeof *meta;
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof control;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cmsg), &dmafd, sizeof(int));

	while (sendmsg(sock, &msg, 0) < 0) {
		if (errno == EINTR)
			continue;
		return false;
	}
	return true;
}

/* ------------------------------------------------------------------ */
/* Декодер                                                             */
/* ------------------------------------------------------------------ */

struct FactoryName { const char *lib, *sym; };

const FactoryName kFactories[BF7V_CODEC_COUNT] = {
	{ "libcodec2_mtk_vdec.so", "CreateCodec2AvcFactory"   },
	{ "libcodec2_mtk_vdec.so", "CreateCodec2HevcFactory"  },
	{ "libcodec2_mtk_vdec.so", "CreateCodec2Vp9Factory"   },
	{ "libcodec2_mtk_vdec.so", "CreateCodec2Vp8Factory"   },
	{ "libcodec2_mtk_vdec.so", "CreateCodec2Av1Factory"   },
	{ "libcodec2_mtk_vdec.so", "CreateCodec2Mpeg4Factory" },
	{ "libcodec2_mtk_vdec.so", "CreateCodec2Mpeg2Factory" },
};

class Decoder {
public:
	bool open(uint32_t codec, uint32_t w, uint32_t h)
	{
		if (codec >= BF7V_CODEC_COUNT) {
			fprintf(stderr, "videod: неизвестный кодек %u\n", codec);
			return false;
		}

		void *lib = dlopen(kFactories[codec].lib, RTLD_NOW);

		if (!lib) {
			fprintf(stderr, "videod: %s: %s\n",
				kFactories[codec].lib, dlerror());
			return false;
		}

		auto create = (C2ComponentFactory::CreateCodec2FactoryFunc)
			dlsym(lib, kFactories[codec].sym);
		if (!create) {
			fprintf(stderr, "videod: в библиотеке нет %s\n",
				kFactories[codec].sym);
			return false;
		}

		C2ComponentFactory *factory = create();
		c2_status_t st = factory->createComponent(0, &mComponent);

		if (st != C2_OK || !mComponent) {
			fprintf(stderr, "videod: createComponent -> %d\n", (int)st);
			return false;
		}

		mListener = std::make_shared<Listener>();
		mComponent->setListener_vb(mListener, C2_DONT_BLOCK);

		st = android::GetCodec2BlockPool(C2BlockPool::BASIC_LINEAR,
						 mComponent, &mInPool);
		if (st != C2_OK || !mInPool) {
			fprintf(stderr, "videod: пул входных буферов -> %d\n", (int)st);
			return false;
		}

		/*
		 * Выходные буферы компонент сам не заводит: пул создаёт клиент
		 * и сообщает компоненту его идентификатор. Кадры при этом
		 * выделяются через gralloc, то есть приходят готовыми dma-buf.
		 */
		st = android::CreateCodec2BlockPool(
			android::C2PlatformAllocatorStore::GRALLOC,
			mComponent, &mOutPool);
		if (st != C2_OK || !mOutPool) {
			fprintf(stderr, "videod: пул кадров -> %d\n", (int)st);
			return false;
		}

		std::vector<std::unique_ptr<C2SettingResult>> failures;
		{
			auto pools = C2PortBlockPoolsTuning::output::AllocUnique(
				{ mOutPool->getLocalId() });
			std::vector<C2Param *> cfg{ pools.get() };

			mComponent->intf()->config_vb(cfg, C2_MAY_BLOCK, &failures);
		}
		failures.clear();
		{
			/*
			 * Формат кадра на выходе.
			 *
			 * Обычно просим гибкий YUV — gralloc отдаёт YV12, и
			 * преобразователь MDP делает его из внутреннего
			 * NV12_BLK аппаратно.
			 *
			 * С BF7V_RGB=1 просим сразу RGBA. Это нужно для показа
			 * без единой копии: драйвер PowerVR умеет втягивать
			 * дескриптором целый кадр, но не отдельные плоскости
			 * (в списке форматов EGL нет R8), а проигрыватели
			 * разбирают YUV именно по плоскостям. Кадр в RGBA —
			 * один слой, и он проходит. Преобразование при этом
			 * по-прежнему делает MDP, процессор не участвует.
			 */
			bool want_rgb = getenv("BF7V_RGB") != nullptr;
			C2StreamPictureSizeInfo::output size(0u, w, h);
			C2StreamMaxPictureSizeTuning::output maxSize(0u, w, h);
			C2StreamPixelFormatInfo::output fmt(
				0u, want_rgb ? HAL_PIXEL_FORMAT_RGBA_8888
					     : HAL_PIXEL_FORMAT_YCBCR_420_888);
			C2StreamUsageTuning::output usage(
				0u, want_rgb ?
				(uint64_t)C2MemoryUsage::CPU_READ :
				(uint64_t)(C2MemoryUsage::CPU_READ |
					   C2MemoryUsage::CPU_WRITE));
			std::vector<C2Param *> cfg{ &size, &maxSize, &fmt, &usage };

			mComponent->intf()->config_vb(cfg, C2_MAY_BLOCK, &failures);
		}

		st = mComponent->start();
		if (st != C2_OK) {
			fprintf(stderr, "videod: start -> %d\n", (int)st);
			return false;
		}

		fprintf(stderr, "videod: декодер открыт (%s, %ux%u)\n",
			kFactories[codec].sym, w, h);
		return true;
	}

	bool queue(const uint8_t *data, size_t size, int64_t pts, uint32_t flags)
	{
		std::shared_ptr<C2LinearBlock> block;
		C2MemoryUsage usage{ C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
		c2_status_t st = mInPool->fetchLinearBlock(size, usage, &block);

		if (st != C2_OK || !block) {
			fprintf(stderr, "videod: входной буфер на %zu байт -> %d\n",
				size, (int)st);
			return false;
		}
		{
			C2WriteView view = block->map().get();

			if (view.error() != C2_OK)
				return false;
			memcpy(view.data(), data, size);
		}

		auto work = std::make_unique<C2Work>();
		uint32_t f = 0;

		if (flags & BF7V_FLAG_CODEC_CONFIG)
			f |= C2FrameData::FLAG_CODEC_CONFIG;
		if (flags & BF7V_FLAG_END_OF_STREAM)
			f |= C2FrameData::FLAG_END_OF_STREAM;

		work->input.flags = (C2FrameData::flags_t)f;
		work->input.ordinal.timestamp = (uint64_t)pts;
		work->input.ordinal.frameIndex = mIndex;
		work->input.buffers.push_back(
			C2Buffer::CreateLinearBuffer(block->share(0, size, C2Fence())));
		work->worklets.clear();
		work->worklets.emplace_back(new C2Worklet);

		std::list<std::unique_ptr<C2Work>> items;

		items.push_back(std::move(work));
		st = mComponent->queue_nb(&items);
		if (st != C2_OK) {
			fprintf(stderr, "videod: queue_nb -> %d\n", (int)st);
			return false;
		}
		mIndex++;

		/* Просьба отдать готовое немедленно; состояние не сбрасывается. */
		if (flags & BF7V_FLAG_DRAIN) {
			st = mComponent->drain_nb(C2Component::DRAIN_COMPONENT_NO_EOS);
			if (st != C2_OK)
				fprintf(stderr, "videod: drain_nb -> %d\n", (int)st);
		}
		return true;
	}

	bool pop(Decoded *d, int timeout_ms)
	{
		return mListener && mListener->pop(d, timeout_ms);
	}

	/* Поданные, но ещё не завершённые декодером работы. */
	uint64_t inflight()
	{
		uint64_t done = mListener ? mListener->doneWorks() : 0;

		return mIndex > done ? mIndex - done : 0;
	}

	bool eos() { return mListener && mListener->eos(); }
	bool error() { return mListener && mListener->error(); }

	void close()
	{
		if (mComponent) {
			mComponent->stop();
			mComponent->release();
			mComponent.reset();
		}
		mListener.reset();
		mInPool.reset();
		mOutPool.reset();
		mIndex = 0;
	}

private:
	std::shared_ptr<C2Component> mComponent;
	std::shared_ptr<Listener> mListener;
	std::shared_ptr<C2BlockPool> mInPool, mOutPool;
	uint64_t mIndex = 0;
};

/* ------------------------------------------------------------------ */

/*
 * Сколько выданных кадров держать, пока клиент их не отпустит (12: опорные
 * кадры декодер MediaTek держит у себя, проигрывателю нужны единицы для
 * показа; вместе с очередью готовых и опорными кадрами это меньше пула в
 * ~50 буферов — иначе декодер ждёт свободный буфер, а демон ждёт кадр).
 * Кадр держит
 * буфер gralloc (dma-buf и клиент ION), и если клиент кадры не забирает —
 * «ffmpeg -f null» с кадрами в видеопамяти не ждёт их вовсе, — демон копил
 * их до конца сеанса: 1024 дескриптора за 500 кадров, «Too many open files»
 * в ION, падение и «Failed to end picture decode» у всех. Больше, чем
 * бывает поверхностей у проигрывателя, держать незачем: самый старый кадр
 * возвращается в пул (клиент, если он его ещё показывает, увидит поверх
 * новый кадр — но демон останется жив).
 */
static const size_t kMaxHeld = 12;
static const uint64_t kMaxInflight = 16;

static void hold(std::map<uint64_t, Decoded> &held, Decoded &&d)
{
	static bool skazal;

	held[d.meta.handle] = std::move(d);
	while (held.size() > kMaxHeld) {
		held.erase(held.begin());	/* ручки растут: begin — самый старый */
		if (!skazal) {
			skazal = true;
			fprintf(stderr, "videod: клиент не отпускает кадры — "
				"держу не больше %zu, старые возвращаю в пул\n",
				kMaxHeld);
		}
	}
}

int serve(int sock)
{
	Decoder dec;
	std::map<uint64_t, Decoded> held;
	std::vector<uint8_t> payload;
	bool opened = false;

	for (;;) {
		struct bf7v_request req;

		/* Сперва отдаём всё готовое, чтобы не копить кадры у себя. */
		for (;;) {
			Decoded d;

			if (!opened || !dec.pop(&d, 0))
				break;
			if (!send_frame(sock, &d.meta, d.fd)) {
				close(d.fd);
				return 0;
			}
			close(d.fd);	/* у получателя теперь свой */
			hold(held, std::move(d));
		}

		if (!read_all(sock, &req, sizeof req))
			break;

		/* След для разбора падений: включается переменной BF7V_TRACE. */
		if (getenv("BF7V_TRACE"))
			fprintf(stderr, "videod: просьба %u (размер %u, метка %lld, "
				"флаги 0x%x, ручка %llu)\n", req.type, req.size,
				(long long)req.pts_us, req.flags,
				(unsigned long long)req.handle);

		switch (req.type) {
		case BF7V_MSG_OPEN:
			if (opened)
				dec.close();
			opened = dec.open(req.codec, req.width, req.height);
			{
				struct bf7v_status s = { BF7V_MSG_STATUS,
							 opened ? 0 : -1 };

				if (!write_all(sock, &s, sizeof s))
					return 0;
			}
			break;

		case BF7V_MSG_QUEUE: {
			if (req.size > BF7V_MAX_PACKET) {
				fprintf(stderr, "videod: пакет %u байт слишком велик\n",
					req.size);
				return 1;
			}
			payload.resize(req.size);
			if (req.size && !read_all(sock, payload.data(), req.size))
				return 0;
			/*
			 * Обратное давление. Клиент, который не ждёт кадров,
			 * закидывает декодер всем потоком разом, и декодер
			 * MediaTek заводит выходной буфер gralloc под каждую
			 * ждущую работу: 1800 картинок — 1024 дескриптора, «Too
			 * many open files», падение. Больше kMaxInflight работ
			 * в декодер не подаём: сперва ждём готовые кадры и
			 * отдаём их (не дольше 0,5 с, чтобы не зависнуть).
			 */
			for (int zhdali = 0; opened && dec.inflight() >= kMaxInflight &&
			     zhdali < 500; ) {
				Decoded d;

				if (dec.pop(&d, 50)) {
					if (!send_frame(sock, &d.meta, d.fd)) {
						close(d.fd);
						return 0;
					}
					close(d.fd);
					hold(held, std::move(d));
				} else {
					zhdali += 50;
				}
			}
			if (opened)
				dec.queue(payload.data(), payload.size(),
					  req.pts_us, req.flags);
			break;
		}

		case BF7V_MSG_RELEASE:
			held.erase(req.handle);
			break;

		case BF7V_MSG_DRAIN: {
			/*
			 * Отдаём накопленное. Ждать долго тут нельзя: клиент
			 * VA-API просит выдачу на каждый кадр, и триста
			 * миллисекунд ожидания превращаются в полтора кадра в
			 * секунду. Ждём ровно столько, чтобы не прозевать
			 * кадр, который уже в пути.
			 */
			for (;;) {
				Decoded d;

				if (!dec.pop(&d, 20))
					break;
				if (!send_frame(sock, &d.meta, d.fd)) {
					close(d.fd);
					return 0;
				}
				close(d.fd);
				hold(held, std::move(d));
			}
			struct bf7v_status s = { BF7V_MSG_STATUS,
						 dec.eos() ? 1 : 0 };

			if (!write_all(sock, &s, sizeof s))
				return 0;
			break;
		}

		case BF7V_MSG_CLOSE:
			dec.close();
			opened = false;
			held.clear();
			break;

		default:
			fprintf(stderr, "videod: неизвестное сообщение %u\n", req.type);
			return 1;
		}
	}

	dec.close();
	return 0;
}

} /* namespace */

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : BF7V_SOCKET_PATH;
	struct sockaddr_un addr;
	int srv;

	setvbuf(stderr, nullptr, _IONBF, 0);

	srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srv < 0) {
		perror("socket");
		return 1;
	}
	unlink(path);
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", path);
	if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) {
		perror("bind");
		return 1;
	}
	chmod(path, 0666);
	if (listen(srv, 4) < 0) {
		perror("listen");
		return 1;
	}
	fprintf(stderr, "videod: жду подключений на %s\n", path);

	for (;;) {
		int c = accept(srv, nullptr, nullptr);

		if (c < 0) {
			if (errno == EINTR)
				continue;
			perror("accept");
			break;
		}
		fprintf(stderr, "videod: клиент подключился\n");
		serve(c);
		close(c);
		fprintf(stderr, "videod: клиент отключился\n");
	}
	return 0;
}
