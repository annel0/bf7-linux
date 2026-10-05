/*
 * 32-битная проба аппаратного декодера.
 *
 * Собирается NDK под armeabi-v7a и запускается как обычный андроидный
 * бинарник — ровно как /vendor/bin/fuelgauged. Никакого libhybris: здесь мы
 * сами в мире bionic, поэтому границы между libc++ нет вовсе.
 *
 * Смысл пробы: на этом устройстве настоящий драйвер кодека существует только
 * 32-битным (/vendor/lib/libvcodecdrv.so), а в lib64 лежит лишь заглушка.
 * Проверяем, что в 32 битах компонент не только создаётся, но и стартует.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <memory>

#include <C2Component.h>
#include <C2ComponentFactory.h>
#include <C2Config.h>
#include <C2AllocatorGralloc.h>
#include <C2PlatformSupport.h>
#include <system/graphics.h>

namespace {

constexpr uint32_t kWidth = 1280;
constexpr uint32_t kHeight = 720;

struct Probe {
	const char *factory;
	const char *name;
};

const Probe kProbes[] = {
	{ "CreateCodec2AvcFactory",  "c2.mtk.avc.decoder" },
	{ "CreateCodec2HevcFactory", "c2.mtk.hevc.decoder" },
	{ "CreateCodec2Vp9Factory",  "c2.mtk.vp9.decoder" },
};

class Listener : public C2Component::Listener {
public:
	void onWorkDone_nb(std::weak_ptr<C2Component>,
			   std::list<std::unique_ptr<C2Work>>) override {}
	void onTripped_nb(std::weak_ptr<C2Component>,
			  std::vector<std::shared_ptr<C2SettingResult>>) override {}
	void onError_nb(std::weak_ptr<C2Component>, uint32_t code) override
	{
		printf("    слушатель: ошибка %u\n", code);
	}
};

} /* namespace */

int main(void)
{
	setvbuf(stdout, nullptr, _IONBF, 0);

	void *lib = dlopen("libcodec2_mtk_vdec.so", RTLD_NOW);

	printf("libcodec2_mtk_vdec.so -> %p\n", lib);
	if (!lib) {
		printf("  %s\n", dlerror());
		return 1;
	}

	for (const auto &probe : kProbes) {
		printf("--- %s ---\n", probe.name);

		auto create = (C2ComponentFactory::CreateCodec2FactoryFunc)
			dlsym(lib, probe.factory);
		if (!create) {
			printf("    нет символа %s\n", probe.factory);
			continue;
		}

		C2ComponentFactory *factory = create();
		printf("    фабрика %p\n", (void *)factory);
		if (!factory)
			continue;

		std::shared_ptr<C2Component> component;
		c2_status_t st = factory->createComponent(0, &component);

		printf("    createComponent -> %d, компонент %p\n",
		       (int)st, (void *)component.get());
		if (st != C2_OK || !component)
			continue;

		auto listener = std::make_shared<Listener>();
		st = component->setListener_vb(listener, C2_DONT_BLOCK);
		printf("    setListener_vb -> %d\n", (int)st);

		std::shared_ptr<C2BlockPool> pool;
		st = android::GetCodec2BlockPool(C2BlockPool::BASIC_LINEAR, component, &pool);
		printf("    пул входных буферов -> %d, %p\n", (int)st, (void *)pool.get());

		/*
		 * Компонент сам не заводит выходные буферы: пул для них создаёт
		 * клиент и сообщает его идентификатор. Без этого драйвер стартует
		 * с пустыми параметрами и ругается
		 * "[Err, h264_dec_setParameter] Invalid input argument".
		 */
		std::shared_ptr<C2BlockPool> outPool;
		st = android::CreateCodec2BlockPool(android::C2PlatformAllocatorStore::GRALLOC,
						    component, &outPool);
		printf("    пул выходных кадров -> %d, %p\n", (int)st, (void *)outPool.get());

		std::vector<std::unique_ptr<C2SettingResult>> failures;

		if (outPool) {
			std::unique_ptr<C2PortBlockPoolsTuning::output> pools =
				C2PortBlockPoolsTuning::output::AllocUnique({ outPool->getLocalId() });
			std::vector<C2Param *> cfg{ pools.get() };

			st = component->intf()->config_vb(cfg, C2_MAY_BLOCK, &failures);
			printf("    привязка пула кадров -> %d (отказов %zu)\n",
			       (int)st, failures.size());
		}

		/*
		 * Полный набор параметров выходного порта. Одного размера мало:
		 * драйверу нужно знать ещё формат пикселей и флаги использования,
		 * иначе h264_dec_setParameter отвергает аргумент и поток-обработчик
		 * застревает, не отвечая на сообщение о старте.
		 */
		failures.clear();
		C2StreamPictureSizeInfo::output size(0u, kWidth, kHeight);
		C2StreamMaxPictureSizeTuning::output maxSize(0u, kWidth, kHeight);
		C2StreamPixelFormatInfo::output pixelFormat(0u, HAL_PIXEL_FORMAT_YCBCR_420_888);
		C2StreamUsageTuning::output usage(
			0u, (uint64_t)(C2MemoryUsage::CPU_READ | C2MemoryUsage::CPU_WRITE));
		std::vector<C2Param *> cfg2{ &size, &maxSize, &pixelFormat, &usage };

		st = component->intf()->config_vb(cfg2, C2_MAY_BLOCK, &failures);
		printf("    параметры потока -> %d (отказов %zu)\n", (int)st, failures.size());
		for (const auto &f : failures)
			if (f)
				printf("      отказ, причина %d\n", (int)f->failure);

		/* Спросим у компонента, что он вообще принимает: вслепую подбирать
		 * параметры дороже, чем прочитать список. */
		if (getenv("C2_LIST_PARAMS")) {
			std::vector<std::shared_ptr<C2ParamDescriptor>> descs;

			st = component->intf()->querySupportedParams_nb(&descs);
			printf("    поддерживаемых параметров: %zu (код %d)\n",
			       descs.size(), (int)st);
			for (const auto &d : descs) {
				if (!d)
					continue;
				printf("      %c%c 0x%08x  %s\n",
				       d->isRequired() ? 'R' : '-',
				       d->isReadOnly() ? 'r' : 'w',
				       (unsigned)d->index(), d->name().c_str());
			}
		}

		/* Сам ли пул умеет выделять кадры? Если нет, компонент не сможет
		 * сообщить драйверу размер выходного буфера, и тот отвергнет
		 * параметр. */
		{
			std::shared_ptr<C2GraphicBlock> block;
			C2MemoryUsage gu{ C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };

			st = outPool->fetchGraphicBlock(kWidth, kHeight,
							HAL_PIXEL_FORMAT_YCBCR_420_888,
							gu, &block);
			printf("    пробное выделение кадра -> %d%s\n", (int)st,
			       block ? "  (кадр получен)" : "  (кадра нет)");
			if (block) {
				const C2Handle *h = block->handle();
				native_handle_t *nh = h ?
					android::UnwrapNativeCodec2GrallocHandle(h) : nullptr;

				printf("      C2Handle %p, native_handle %p, fd[0]=%d\n",
				       (const void *)h, (void *)nh,
				       (nh && nh->numFds > 0) ? nh->data[0] : -1);
			}
		}

		printf("    вызываю start()...\n");
		st = component->start();
		printf("    start -> %d%s\n", (int)st,
		       st == C2_OK ? "   << ДЕКОДЕР ЗАПУЩЕН" : "");

		if (getenv("C2_NO_STOP")) {
			printf("    выхожу без остановки (C2_NO_STOP)\n");
			return 0;
		}
		if (st == C2_OK) {
			printf("    вызываю stop()...\n");
			component->stop();
			printf("    stop вернулся\n");
		}
		printf("    вызываю release()...\n");
		component->release();
		printf("    release вернулся\n");
	}
	return 0;
}
