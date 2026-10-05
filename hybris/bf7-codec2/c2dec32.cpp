/*
 * Аппаратное декодирование H.264 через Codec2 MediaTek.
 *
 * Помощник обязан быть 32-битным: настоящий драйвер кодека на этом устройстве
 * существует только как ELF32 (/vendor/lib/libvcodecdrv.so).
 *
 * Запуск требует поднятых служб Android — см. /usr/local/bin/hal-start.sh и
 * записи в docs/bf7-kernel/research-log.md за 2026-09-16.
 *
 *	c2dec32 поток.h264 [ширина высота]
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include <condition_variable>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <vector>

#include <C2Component.h>
#include <C2ComponentFactory.h>
#include <C2Config.h>
#include <C2AllocatorGralloc.h>
#include <C2PlatformSupport.h>
#include <system/graphics.h>

namespace {

/* Накопитель готовых кадров: слушатель вызывается из потока компонента. */
class Listener : public C2Component::Listener {
public:
	void onWorkDone_nb(std::weak_ptr<C2Component>,
			   std::list<std::unique_ptr<C2Work>> works) override
	{
		std::lock_guard<std::mutex> lock(mLock);

		for (auto &w : works) {
			if (!w)
				continue;
			mDone++;
			if (w->worklets.empty() || !w->worklets.front())
				continue;

			const C2FrameData &out = w->worklets.front()->output;

			if (out.flags & C2FrameData::FLAG_END_OF_STREAM)
				mEos = true;

			for (const auto &buf : out.buffers) {
				if (!buf)
					continue;
				const auto &blocks = buf->data().graphicBlocks();

				for (const auto &b : blocks)
					report(b, out.ordinal.timestamp.peekull());
			}
		}
		mCond.notify_all();
	}

	void onTripped_nb(std::weak_ptr<C2Component>,
			  std::vector<std::shared_ptr<C2SettingResult>>) override {}

	void onError_nb(std::weak_ptr<C2Component>, uint32_t code) override
	{
		printf("  слушатель: ошибка %u\n", code);
		std::lock_guard<std::mutex> lock(mLock);
		mError = true;
		mCond.notify_all();
	}

	/* Ждём, пока не наберётся столько кадров, либо не придёт конец потока. */
	bool wait(unsigned frames, unsigned seconds)
	{
		std::unique_lock<std::mutex> lock(mLock);

		return mCond.wait_for(lock, std::chrono::seconds(seconds),
				      [&] { return mFrames >= frames || mEos || mError; });
	}

	unsigned frames() const { return mFrames; }
	unsigned done() const { return mDone; }
	bool eos() const { return mEos; }

private:
	void report(const C2ConstGraphicBlock &b, unsigned long long pts)
	{
		const C2Handle *h = b.handle();
		native_handle_t *nh = h ?
			android::UnwrapNativeCodec2GrallocHandle(h) : nullptr;
		int fd = (nh && nh->numFds > 0) ? nh->data[0] : -1;

		mFrames++;
		printf("  кадр %u: %ux%u, смещение %u,%u, pts %llu, dma-buf fd %d\n",
		       mFrames, b.width(), b.height(),
		       b.crop().left, b.crop().top, pts, fd);
		if (nh)
			native_handle_delete(nh);
	}

	std::mutex mLock;
	std::condition_variable mCond;
	unsigned mFrames = 0;
	unsigned mDone = 0;
	bool mEos = false;
	bool mError = false;
};

/* Разбор Annex-B: возвращает границы NAL без стартовых кодов. */
struct Nal {
	const uint8_t *data;
	size_t size;
	uint8_t type;
};

std::vector<Nal> split_nals(const std::vector<uint8_t> &buf)
{
	std::vector<Nal> nals;
	size_t i = 0;
	size_t start = 0;
	bool have = false;

	auto is_sc = [&](size_t p, size_t *len) {
		if (p + 3 <= buf.size() && buf[p] == 0 && buf[p + 1] == 0 && buf[p + 2] == 1) {
			*len = 3;
			return true;
		}
		if (p + 4 <= buf.size() && buf[p] == 0 && buf[p + 1] == 0 &&
		    buf[p + 2] == 0 && buf[p + 3] == 1) {
			*len = 4;
			return true;
		}
		return false;
	};

	while (i < buf.size()) {
		size_t sc = 0;

		if (is_sc(i, &sc)) {
			if (have)
				nals.push_back({ &buf[start], i - start,
						 (uint8_t)(buf[start] & 0x1f) });
			i += sc;
			start = i;
			have = true;
			continue;
		}
		i++;
	}
	if (have && start < buf.size())
		nals.push_back({ &buf[start], buf.size() - start,
				 (uint8_t)(buf[start] & 0x1f) });
	return nals;
}

/*
 * Сборка единиц доступа. Новая начинается на срезе (тип 1 или 5), если срез в
 * текущей уже был. Параметры последовательности и картинки (7, 8) уходят
 * отдельной посылкой с признаком настройки — так требует Codec2.
 */
struct AccessUnit {
	std::vector<uint8_t> data;
	bool config;
};

std::vector<AccessUnit> build_aus(const std::vector<Nal> &nals)
{
	static const uint8_t sc[4] = { 0, 0, 0, 1 };
	std::vector<AccessUnit> aus;
	AccessUnit cur{ {}, false };
	bool cur_has_slice = false;

	auto flush = [&] {
		if (!cur.data.empty())
			aus.push_back(std::move(cur));
		cur = AccessUnit{ {}, false };
		cur_has_slice = false;
	};

	for (const auto &n : nals) {
		bool slice = (n.type == 1 || n.type == 5);
		bool param = (n.type == 7 || n.type == 8);

		if (param) {
			if (cur_has_slice || (!cur.data.empty() && !cur.config))
				flush();
			cur.config = true;
		} else if (slice && cur_has_slice) {
			flush();
		} else if (!cur.data.empty() && cur.config && !param) {
			flush();
		}

		cur.data.insert(cur.data.end(), sc, sc + 4);
		cur.data.insert(cur.data.end(), n.data, n.data + n.size);
		if (slice)
			cur_has_slice = true;
	}
	flush();
	return aus;
}

} /* namespace */

int main(int argc, char **argv)
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	if (argc < 2) {
		printf("использование: %s поток.h264 [ширина высота]\n", argv[0]);
		return 2;
	}
	const uint32_t width = (argc > 3) ? (uint32_t)atoi(argv[2]) : 1280;
	const uint32_t height = (argc > 3) ? (uint32_t)atoi(argv[3]) : 720;

	std::vector<uint8_t> stream;
	{
		FILE *f = fopen(argv[1], "rb");

		if (!f) {
			printf("не открыть %s\n", argv[1]);
			return 1;
		}
		fseek(f, 0, SEEK_END);
		long n = ftell(f);
		fseek(f, 0, SEEK_SET);
		stream.resize((size_t)n);
		if (fread(stream.data(), 1, stream.size(), f) != stream.size()) {
			printf("не дочитать %s\n", argv[1]);
			fclose(f);
			return 1;
		}
		fclose(f);
	}

	auto nals = split_nals(stream);
	auto aus = build_aus(nals);

	printf("поток %s: %zu байт, %zu NAL, %zu единиц доступа, кадр %ux%u\n",
	       argv[1], stream.size(), nals.size(), aus.size(), width, height);

	void *lib = dlopen("libcodec2_mtk_vdec.so", RTLD_NOW);

	if (!lib) {
		printf("libcodec2_mtk_vdec.so: %s\n", dlerror());
		return 1;
	}

	auto create = (C2ComponentFactory::CreateCodec2FactoryFunc)
		dlsym(lib, "CreateCodec2AvcFactory");
	if (!create) {
		printf("нет CreateCodec2AvcFactory\n");
		return 1;
	}

	C2ComponentFactory *factory = create();
	std::shared_ptr<C2Component> component;
	c2_status_t st = factory->createComponent(0, &component);

	printf("createComponent -> %d\n", (int)st);
	if (st != C2_OK || !component)
		return 1;

	auto listener = std::make_shared<Listener>();

	st = component->setListener_vb(listener, C2_DONT_BLOCK);
	printf("setListener_vb -> %d\n", (int)st);

	std::shared_ptr<C2BlockPool> inPool;
	st = android::GetCodec2BlockPool(C2BlockPool::BASIC_LINEAR, component, &inPool);
	printf("пул входных буферов -> %d\n", (int)st);

	/* Компонент сам не заводит выходные буферы: пул создаёт клиент и
	 * сообщает его идентификатор. */
	std::shared_ptr<C2BlockPool> outPool;
	st = android::CreateCodec2BlockPool(android::C2PlatformAllocatorStore::GRALLOC,
					    component, &outPool);
	printf("пул выходных кадров -> %d\n", (int)st);
	if (!inPool || !outPool)
		return 1;

	std::vector<std::unique_ptr<C2SettingResult>> failures;
	{
		std::unique_ptr<C2PortBlockPoolsTuning::output> pools =
			C2PortBlockPoolsTuning::output::AllocUnique({ outPool->getLocalId() });
		std::vector<C2Param *> cfg{ pools.get() };

		st = component->intf()->config_vb(cfg, C2_MAY_BLOCK, &failures);
		printf("привязка пула кадров -> %d (отказов %zu)\n", (int)st, failures.size());
	}

	failures.clear();
	{
		C2StreamPictureSizeInfo::output size(0u, width, height);
		C2StreamMaxPictureSizeTuning::output maxSize(0u, width, height);
		C2StreamPixelFormatInfo::output pixelFormat(0u, HAL_PIXEL_FORMAT_YCBCR_420_888);
		C2StreamUsageTuning::output usage(
			0u, (uint64_t)(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE));
		std::vector<C2Param *> cfg{ &size, &maxSize, &pixelFormat, &usage };

		st = component->intf()->config_vb(cfg, C2_MAY_BLOCK, &failures);
		printf("параметры потока -> %d (отказов %zu)\n", (int)st, failures.size());
	}

	st = component->start();
	printf("start -> %d\n", (int)st);
	if (st != C2_OK)
		return 1;

	uint64_t index = 0;

	for (size_t i = 0; i < aus.size(); i++) {
		const AccessUnit &au = aus[i];
		std::shared_ptr<C2LinearBlock> block;
		C2MemoryUsage usage{ C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };

		st = inPool->fetchLinearBlock(au.data.size(), usage, &block);
		if (st != C2_OK || !block) {
			printf("не выделить входной буфер на %zu байт -> %d\n",
			       au.data.size(), (int)st);
			break;
		}
		{
			C2WriteView view = block->map().get();

			if (view.error() != C2_OK) {
				printf("не отобразить входной буфер -> %d\n", (int)view.error());
				break;
			}
			memcpy(view.data(), au.data.data(), au.data.size());
		}

		auto work = std::make_unique<C2Work>();

		work->input.flags = au.config ?
			C2FrameData::FLAG_CODEC_CONFIG : (C2FrameData::flags_t)0;
		if (i + 1 == aus.size())
			work->input.flags = (C2FrameData::flags_t)
				(work->input.flags | C2FrameData::FLAG_END_OF_STREAM);
		work->input.ordinal.timestamp = index * 33333;
		work->input.ordinal.frameIndex = index;
		work->input.buffers.push_back(
			C2Buffer::CreateLinearBuffer(block->share(0, au.data.size(), C2Fence())));
		work->worklets.clear();
		work->worklets.emplace_back(new C2Worklet);

		std::list<std::unique_ptr<C2Work>> items;

		items.push_back(std::move(work));
		st = component->queue_nb(&items);
		if (st != C2_OK) {
			printf("queue_nb на единице %zu -> %d\n", i, (int)st);
			break;
		}
		index++;
	}
	printf("подано единиц доступа: %" PRIu64 "\n", index);

	const unsigned expect = (unsigned)(aus.size() > 1 ? aus.size() - 1 : 1);
	bool ok = listener->wait(expect, 20);

	printf("ожидание: %s, завершено работ %u, кадров %u, конец потока %s\n",
	       ok ? "готово" : "истекло время",
	       listener->done(), listener->frames(), listener->eos() ? "да" : "нет");

	component->stop();
	component->release();

	printf("ИТОГ: декодировано кадров %u\n", listener->frames());
	return listener->frames() > 0 ? 0 : 1;
}
