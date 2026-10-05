/*
 * BF7: речевой тракт модема через заводской звуковой HAL MediaTek.
 *
 * Звонок соединяет модем, а звук разговора идёт мимо процессора
 * приложений: модем сам говорит с кодеком через PCM Voice_MD1. Включает
 * этот путь на Android не ядро, а звуковой HAL (audio.primary.mt6761.so):
 * когда AudioFlinger переводит его в AUDIO_MODE_IN_CALL, SpeechDriver
 * внутри HAL открывает Voice_MD1, выставляет регуляторы и шлёт модему
 * «речь вкл» по /dev/ccci_aud. Здесь HAL загружается напрямую (dlopen +
 * символ HMI) и получает те же вызовы, что от AudioFlinger.
 *
 * Вызов (бионик, с подпорками — его запускает bf7-zvonki):
 *   bf7-rech zvonok [устройство]  — режим разговора, пока не придёт «stop»,
 *                                   конец ввода или SIGTERM; затем обычный
 *   bf7-rech proba [секунд]       — войти в разговор и выйти (проверка)
 * устройство: trubka (динамик у уха, по умолчанию), gromko, garnitura
 *
 * Команды со входа в режиме zvonok, по строке:
 *   trubka | gromko | garnitura   — куда вести разговор
 *   mikrofon 0|1                  — выключить / включить микрофон
 *   gromkost 0.0..1.0             — громкость разговора
 *   zapis <файл> | zapis stop     — писать голос собеседника (s16le,
 *                                   16 кГц, моно) в файл: вход HAL
 *                                   TELEPHONY_RX, источник VOICE_DOWNLINK;
 *                                   файл дописывается, его читает bf7-sluh
 *   igraj <файл>                  — проиграть в разговор сырой звук
 *                                   (s16le, 48 кГц, стерео): в режиме
 *                                   IN_CALL HAL MediaTek отдаёт основной
 *                                   выход модему как «фоновый звук»
 *                                   (AudioALSAPlaybackHandlerVoice) — его
 *                                   слышит собеседник
 *   stop
 *
 * Найдено по дороге: HAL при маршрутизации опрашивает Bluetooth-чип через
 * /vendor/lib64/libbluetooth_mtk_pure.so, а тот ждёт свойства
 * vendor.connsys.driver.ready и persist.vendor.connsys.chipid (0x6761) —
 * без них каждый вызов висит минутами. Оба лежат в bf7.prop.
 */
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hardware/audio.h>
#include <hardware/hardware.h>

#define HAL "/vendor/lib64/hw/audio.primary.mt6761.so"

/* Запись собеседника: отдельный поток читает вход HAL и дописывает файл. */
static struct audio_stream_in *vhod;
static FILE *zapis_f;
static volatile int zapis_idet;
static pthread_t zapis_potok;

static void *zapis_cikl(void *arg)
{
	char buf[640];	/* 20 мс при 16 кГц, моно, 16 бит */

	(void)arg;
	while (zapis_idet) {
		ssize_t n = vhod->read(vhod, buf, sizeof(buf));

		if (n <= 0) {
			usleep(10000);
			continue;
		}
		fwrite(buf, 1, n, zapis_f);
		fflush(zapis_f);
	}
	return NULL;
}

static int zapis_stop(audio_hw_device_t *dev)
{
	if (!zapis_idet)
		return 0;
	zapis_idet = 0;
	pthread_join(zapis_potok, NULL);
	dev->close_input_stream(dev, vhod);
	vhod = NULL;
	fclose(zapis_f);
	zapis_f = NULL;
	return 0;
}

static int zapis_start(audio_hw_device_t *dev, const char *put)
{
	struct audio_config c = {
		.sample_rate = 16000,
		.channel_mask = AUDIO_CHANNEL_IN_MONO,
		.format = AUDIO_FORMAT_PCM_16_BIT,
	};
	int r;

	zapis_stop(dev);
	r = dev->open_input_stream(dev, 2, AUDIO_DEVICE_IN_TELEPHONY_RX, &c,
				   &vhod, AUDIO_INPUT_FLAG_NONE, "",
				   AUDIO_SOURCE_VOICE_DOWNLINK);
	printf("bf7-rech: вход собеседника: %d, %u Гц\n", r, c.sample_rate);
	if (r)
		return r;
	zapis_f = fopen(put, "ab");
	if (!zapis_f) {
		dev->close_input_stream(dev, vhod);
		vhod = NULL;
		return -2;
	}
	zapis_idet = 1;
	if (pthread_create(&zapis_potok, NULL, zapis_cikl, NULL)) {
		zapis_idet = 0;
		fclose(zapis_f);
		dev->close_input_stream(dev, vhod);
		return -12;
	}
	return 0;
}

/*
 * Записать файл в основной выход; HAL сам держит темп воспроизведения.
 * «Фоновый звук» к собеседнику (UL) и к себе (DL) у HAL по умолчанию может
 * быть заглушён — при первой пробе собеседник слышал только микрофон.
 */
static int igraj(audio_hw_device_t *dev, struct audio_stream_out *out,
		 const char *put)
{
	char buf[3840];	/* 20 мс при 48 кГц, стерео, 16 бит */
	FILE *f;
	size_t n;
	ssize_t w = 0;

	if (!out)
		return -19;
	/* К себе — заглушено: иначе свой же голос попал бы в запись
	 * собеседника и в распознавание. */
	printf("bf7-rech: фоновый звук к собеседнику: %d, к себе выкл: %d\n",
	       dev->set_parameters(dev, "Set_BGS_UL_Mute=0"),
	       dev->set_parameters(dev, "Set_BGS_DL_Mute=1"));
	fflush(stdout);
	f = fopen(put, "rb");
	if (!f)
		return -2;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		w = out->write(out, buf, n);
		if (w < 0)
			break;
	}
	fclose(f);
	return w < 0 ? (int)w : 0;
}

static volatile sig_atomic_t stop;

static void na_signal(int s)
{
	(void)s;
	stop = 1;
}

static audio_devices_t ustrojstvo(const char *s)
{
	if (s && !strcmp(s, "gromko"))
		return AUDIO_DEVICE_OUT_SPEAKER;
	if (s && !strcmp(s, "garnitura"))
		return AUDIO_DEVICE_OUT_WIRED_HEADSET;
	return AUDIO_DEVICE_OUT_EARPIECE;
}

int main(int argc, char **argv)
{
	const char *rezhim = argc > 1 ? argv[1] : "proba";
	struct hw_module_t *mod;
	audio_hw_device_t *dev;
	struct audio_stream_out *out = NULL;
	struct audio_config cfg = {
		.sample_rate = 48000,
		.channel_mask = AUDIO_CHANNEL_OUT_STEREO,
		.format = AUDIO_FORMAT_PCM_16_BIT,
	};
	audio_devices_t kuda;
	char marshrut[64];
	void *h;
	int r;

	h = dlopen(HAL, RTLD_NOW);
	if (!h) {
		fprintf(stderr, "bf7-rech: %s\n", dlerror());
		return 1;
	}
	mod = dlsym(h, HAL_MODULE_INFO_SYM_AS_STR);
	if (!mod) {
		fprintf(stderr, "bf7-rech: нет символа HMI\n");
		return 1;
	}
	mod->dso = h;
	r = mod->methods->open(mod, AUDIO_HARDWARE_INTERFACE,
			       (struct hw_device_t **)&dev);
	if (r) {
		fprintf(stderr, "bf7-rech: open: %d\n", r);
		return 1;
	}
	printf("bf7-rech: HAL открыт, версия 0x%x, init_check %d\n",
	       dev->common.version, dev->init_check(dev));

	/*
	 * Основной выход: на него AudioFlinger вешает маршрут, и SpeechDriver
	 * берёт устройство разговора оттуда.
	 */
	kuda = ustrojstvo(!strcmp(rezhim, "zvonok") && argc > 2 ? argv[2] : NULL);
	r = dev->open_output_stream(dev, 1, kuda, AUDIO_OUTPUT_FLAG_PRIMARY,
				    &cfg, &out, "");
	printf("bf7-rech: основной выход: %d\n", r);
	/* Без SA_RESTART: сигнал должен прервать чтение команды. */
	{
		struct sigaction sa = { .sa_handler = na_signal };
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGINT, &sa, NULL);
	}

	/*
	 * Порядок как у AudioFlinger: сначала режим, потом маршрут. Разговор в
	 * HAL MediaTek открывается маршрутом, пришедшим уже в режиме IN_CALL
	 * (AudioALSAStreamManager::routingOutputDevice); маршрут до смены режима
	 * только переключает динамик.
	 */
	r = dev->set_mode(dev, AUDIO_MODE_IN_CALL);
	printf("bf7-rech: режим разговора: %d\n", r);
	snprintf(marshrut, sizeof(marshrut), "routing=%u", (unsigned)kuda);
	if (out)
		printf("bf7-rech: маршрут %s: %d\n", marshrut,
		       out->common.set_parameters(&out->common, marshrut));
	/*
	 * Громкость разговора — как у Android на верхней ступени. HAL переводит
	 * число в ступень своих таблиц (SpeechVol); при 0,8 хозяин слышал
	 * собеседника тихо и в трубке, и на громкой связи.
	 */
	dev->set_voice_volume(dev, 1.0f);
	dev->set_mic_mute(dev, false);
	fflush(stdout);

	if (!strcmp(rezhim, "proba")) {
		int s = argc > 2 ? atoi(argv[2]) : 5;
		while (s-- > 0 && !stop)
			sleep(1);
	} else {
		char str[128];

		printf("bf7-rech: разговор, жду команд\n");
		fflush(stdout);
		while (!stop && fgets(str, sizeof(str), stdin)) {
			float g;
			int m;

			str[strcspn(str, "\r\n")] = 0;
			if (!strcmp(str, "stop"))
				break;
			if (!strcmp(str, "trubka") || !strcmp(str, "gromko") ||
			    !strcmp(str, "garnitura")) {
				kuda = ustrojstvo(str);
				snprintf(marshrut, sizeof(marshrut), "routing=%u",
					 (unsigned)kuda);
				r = out ? out->common.set_parameters(&out->common,
								     marshrut) : -1;
			} else if (sscanf(str, "mikrofon %d", &m) == 1) {
				r = dev->set_mic_mute(dev, !m);
			} else if (sscanf(str, "gromkost %f", &g) == 1) {
				r = dev->set_voice_volume(dev, g);
			} else if (!strncmp(str, "igraj ", 6)) {
				r = igraj(dev, out, str + 6);
			} else if (!strcmp(str, "zapis stop")) {
				r = zapis_stop(dev);
			} else if (!strncmp(str, "zapis ", 6)) {
				r = zapis_start(dev, str + 6);
			} else {
				r = -22;
			}
			printf("bf7-rech: «%s»: %d\n", str, r);
			fflush(stdout);
		}
	}

	zapis_stop(dev);
	r = dev->set_mode(dev, AUDIO_MODE_NORMAL);
	printf("bf7-rech: обычный режим: %d\n", r);
	if (out)
		dev->close_output_stream(dev, out);
	dev->common.close(&dev->common);
	return 0;
}
