/*
 * Реализация прослойки к аппаратному декодеру MediaTek. См. bf7c2dec.h.
 *
 * Собирается NDK как андроидная библиотека, потому что весь Codec2 — это C++
 * поверх bionic-овской libc++. Наружу отдаётся только C.
 */
#include "bf7c2dec.h"

#include <dlfcn.h>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <C2Component.h>
#include <C2ComponentFactory.h>
#include <C2Buffer.h>
#include <C2Work.h>
#include <C2PlatformSupport.h>
#include <C2AllocatorGralloc.h>

namespace {

/* Последняя ошибка: одна на поток, чтобы не мешать вызывающим. */
thread_local std::string g_error;

/* Трассировка шагов открытия: BF7C2_TRACE=1 */
int trace_on()
{
	static int on = -1;

	if (on < 0)
		on = getenv("BF7C2_TRACE") ? 1 : 0;
	return on;
}

#define TRACE(...) do { if (trace_on()) { \
	fprintf(stderr, "bf7c2dec: " __VA_ARGS__); fputc('\n', stderr); \
	fflush(stderr); } } while (0)

void set_error(const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	g_error = buf;
}

struct CodecInfo {
	const char *factory;	/* имя функции в libcodec2_mtk_vdec.so */
	const char *name;	/* как компонент называется у MediaTek */
};

const CodecInfo kCodecs[BF7C2_CODEC_COUNT] = {
	{ "CreateCodec2AvcFactory",   "c2.mtk.avc.decoder" },
	{ "CreateCodec2HevcFactory",  "c2.mtk.hevc.decoder" },
	{ "CreateCodec2Vp9Factory",   "c2.mtk.vp9.decoder" },
	{ "CreateCodec2VpxFactory",   "c2.mtk.vp8.decoder" },
	{ "CreateCodec2Av1Factory",   "c2.mtk.av1.decoder" },
	{ "CreateCodec2Mpeg4Factory", "c2.mtk.mpeg4.decoder" },
	{ "CreateCodec2Mpeg2Factory", "c2.mtk.mpeg2.decoder" },
};

/* Готовый кадр, ожидающий выдачи наружу.
 *
 * C2ConstGraphicBlock не имеет конструктора по умолчанию, поэтому держим его
 * в optional: очередь заполняется по месту.
 */
struct PendingFrame {
	std::optional<C2ConstGraphicBlock>	view;
	std::shared_ptr<C2Buffer>		keepalive;
	bf7c2_frame				info{};
};

} /* namespace */

struct bf7c2_decoder {
	void					*lib = nullptr;
	C2ComponentFactory			*factory = nullptr;
	C2ComponentFactory::DestroyCodec2FactoryFunc destroy = nullptr;
	std::shared_ptr<C2Component>		component;
	std::shared_ptr<C2BlockPool>		inputPool;
	std::shared_ptr<C2Component::Listener>	listener;

	std::mutex				lock;
	std::condition_variable			ready;
	std::deque<PendingFrame>		frames;
	std::map<uint64_t, std::shared_ptr<C2Buffer>> held;
	uint64_t				nextHandle = 1;
	uint64_t				frameIndex = 0;
	bool					error = false;
};

namespace {

/* Слушатель, который складывает готовые кадры в очередь декодера. */
class Listener : public C2Component::Listener {
public:
	explicit Listener(bf7c2_decoder *dec) : mDec(dec) {}

	void onWorkDone_nb(std::weak_ptr<C2Component>,
			   std::list<std::unique_ptr<C2Work>> workItems) override
	{
		std::lock_guard<std::mutex> guard(mDec->lock);

		for (auto &work : workItems) {
			if (!work || work->worklets.empty())
				continue;
			const auto &worklet = work->worklets.front();

			for (const auto &buffer : worklet->output.buffers) {
				if (!buffer)
					continue;
				for (const auto &block : buffer->data().graphicBlocks()) {
					PendingFrame frame;

					frame.view = block;
					frame.keepalive = buffer;
					frame.info.width = block.width();
					frame.info.height = block.height();
					frame.info.timestamp_us =
						(int64_t)worklet->output.ordinal.timestamp.peeku();
					frame.info.handle = mDec->nextHandle++;
					frame.info.dma_fd = -1;
					frame.info.stride = 0;
					frame.info.format = 0;
					mDec->frames.push_back(std::move(frame));
				}
			}
		}
		mDec->ready.notify_all();
	}

	void onTripped_nb(std::weak_ptr<C2Component>,
			  std::vector<std::shared_ptr<C2SettingResult>>) override {}

	void onError_nb(std::weak_ptr<C2Component>, uint32_t errorCode) override
	{
		std::lock_guard<std::mutex> guard(mDec->lock);

		mDec->error = true;
		mDec->ready.notify_all();
		fprintf(stderr, "bf7c2dec: компонент сообщил об ошибке %u\n", errorCode);
	}

private:
	bf7c2_decoder *mDec;
};

} /* namespace */

extern "C" const char *bf7c2_component_name(bf7c2_codec codec)
{
	if (codec < 0 || codec >= BF7C2_CODEC_COUNT)
		return "?";
	return kCodecs[codec].name;
}

extern "C" const char *bf7c2_last_error(void)
{
	return g_error.empty() ? "" : g_error.c_str();
}

extern "C" bf7c2_decoder *bf7c2_open(bf7c2_codec codec, uint32_t width, uint32_t height)
{
	if (codec < 0 || codec >= BF7C2_CODEC_COUNT) {
		set_error("неизвестный кодек %d", (int)codec);
		return nullptr;
	}

	std::unique_ptr<bf7c2_decoder> dec(new bf7c2_decoder);

	TRACE("открываю libcodec2_mtk_vdec.so");
	dec->lib = dlopen("libcodec2_mtk_vdec.so", RTLD_NOW);
	if (!dec->lib) {
		set_error("libcodec2_mtk_vdec.so не загрузилась: %s", dlerror());
		return nullptr;
	}

	auto create = (C2ComponentFactory::CreateCodec2FactoryFunc)
		dlsym(dec->lib, kCodecs[codec].factory);
	dec->destroy = (C2ComponentFactory::DestroyCodec2FactoryFunc)
		dlsym(dec->lib, kCodecs[codec].factory[0] ? "DestroyCodec2Factory" : nullptr);
	if (!create) {
		set_error("нет символа %s", kCodecs[codec].factory);
		return nullptr;
	}

	TRACE("зову %s", kCodecs[codec].factory);
	dec->factory = create();
	if (!dec->factory) {
		set_error("%s вернула пустую фабрику", kCodecs[codec].factory);
		return nullptr;
	}

	TRACE("фабрика %p, создаю компонент", (void *)dec->factory);
	c2_status_t st = dec->factory->createComponent(0, &dec->component);
	if (st != C2_OK || !dec->component) {
		set_error("createComponent: %d", (int)st);
		return nullptr;
	}

	TRACE("компонент %p, ставлю слушателя", (void *)dec->component.get());
	dec->listener = std::make_shared<Listener>(dec.get());
	st = dec->component->setListener_vb(dec->listener, C2_DONT_BLOCK);
	if (st != C2_OK) {
		set_error("setListener_vb: %d", (int)st);
		return nullptr;
	}

	TRACE("слушатель поставлен, беру пул входных буферов");
	st = android::GetCodec2BlockPool(C2BlockPool::BASIC_LINEAR,
					 dec->component, &dec->inputPool);
	if (st != C2_OK || !dec->inputPool) {
		set_error("не получен пул входных буферов: %d", (int)st);
		return nullptr;
	}

	TRACE("пул %p, запускаю компонент", (void *)dec->inputPool.get());
	st = dec->component->start();
	if (st != C2_OK) {
		set_error("start: %d", (int)st);
		return nullptr;
	}

	TRACE("декодер готов");
	(void)width;
	(void)height;
	return dec.release();
}

extern "C" int bf7c2_queue(bf7c2_decoder *dec, const void *data, size_t size,
			   int64_t timestamp_us, uint32_t flags)
{
	if (!dec || !dec->component) {
		set_error("декодер не открыт");
		return -1;
	}

	std::shared_ptr<C2LinearBlock> block;
	c2_status_t st = dec->inputPool->fetchLinearBlock(
		size ? size : 1,
		{ C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE },
		&block);

	if (st != C2_OK || !block) {
		set_error("fetchLinearBlock(%zu): %d", size, (int)st);
		return -1;
	}
	if (size) {
		C2WriteView view = block->map().get();

		if (view.error() != C2_OK) {
			set_error("map входного буфера: %d", (int)view.error());
			return -1;
		}
		memcpy(view.data(), data, size);
	}

	std::unique_ptr<C2Work> work(new C2Work);

	work->input.flags = (C2FrameData::flags_t)0;
	if (flags & BF7C2_FLAG_END_OF_STREAM)
		work->input.flags = (C2FrameData::flags_t)
			(work->input.flags | C2FrameData::FLAG_END_OF_STREAM);
	if (flags & BF7C2_FLAG_CODEC_CONFIG)
		work->input.flags = (C2FrameData::flags_t)
			(work->input.flags | C2FrameData::FLAG_CODEC_CONFIG);
	work->input.ordinal.timestamp = (uint64_t)timestamp_us;
	work->input.ordinal.frameIndex = dec->frameIndex++;
	work->input.ordinal.customOrdinal = 0;
	work->input.buffers.clear();
	if (size)
		work->input.buffers.push_back(
			C2Buffer::CreateLinearBuffer(block->share(0, size, C2Fence())));
	work->worklets.clear();
	work->worklets.emplace_back(new C2Worklet);

	std::list<std::unique_ptr<C2Work>> items;
	items.push_back(std::move(work));

	st = dec->component->queue_nb(&items);
	if (st != C2_OK) {
		set_error("queue_nb: %d", (int)st);
		return -1;
	}
	return 0;
}

extern "C" int bf7c2_dequeue(bf7c2_decoder *dec, bf7c2_frame *out, int timeout_ms)
{
	if (!dec || !out) {
		set_error("неверные аргументы");
		return -1;
	}

	std::unique_lock<std::mutex> guard(dec->lock);

	if (dec->frames.empty()) {
		if (timeout_ms == 0)
			return 0;
		auto pred = [dec] { return !dec->frames.empty() || dec->error; };

		if (timeout_ms < 0)
			dec->ready.wait(guard, pred);
		else if (!dec->ready.wait_for(guard, std::chrono::milliseconds(timeout_ms), pred))
			return 0;
	}
	if (dec->frames.empty())
		return dec->error ? -1 : 0;

	PendingFrame frame = std::move(dec->frames.front());
	dec->frames.pop_front();

	/* Вытаскиваем дескриптор dma-buf: у кадра Codec2 это гралловый хендл,
	 * завёрнутый в C2Handle. */
	const C2Handle *handle = frame.view ? frame.view->handle() : nullptr;
	native_handle_t *nh = handle ?
		android::UnwrapNativeCodec2GrallocHandle(handle) : nullptr;

	if (nh && nh->numFds > 0)
		frame.info.dma_fd = nh->data[0];

	/* Держим буфер, пока вызывающий не вернёт кадр: иначе декодер заберёт
	 * его обратно в пул и dma-buf станет чужим. */
	dec->held[frame.info.handle] = frame.keepalive;
	*out = frame.info;
	return 1;
}

extern "C" void bf7c2_release_frame(bf7c2_decoder *dec, uint64_t handle)
{
	if (!dec)
		return;

	std::lock_guard<std::mutex> guard(dec->lock);
	dec->held.erase(handle);
}

extern "C" void bf7c2_close(bf7c2_decoder *dec)
{
	if (!dec)
		return;
	if (dec->component) {
		dec->component->stop();
		dec->component->release();
		dec->component.reset();
	}
	if (dec->factory && dec->destroy)
		dec->destroy(dec->factory);
	if (dec->lib)
		dlclose(dec->lib);
	delete dec;
}
