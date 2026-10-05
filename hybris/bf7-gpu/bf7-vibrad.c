/*
 * BF7: мост от вибромотора к подсистеме обратной связи.
 *
 * Зачем. feedbackd (он отвечает за отклик на нажатия, звонки и уведомления в
 * телефонной оболочке) ищет устройство ввода с обратной связью и, не найдя,
 * пишет «No vibra capable device found». У нас же мотор выведен светодиодом
 * /sys/class/leds/vibrator — но включается он НЕ яркостью, а спуском:
 * записать срок в duration и единицу в activate, дальше мотор выключится сам.
 *
 * До 2026-09-23 здесь писали в brightness — и мотор не вибрировал ни разу:
 * state оставался 0. Вибро «работало» только на бумаге. Проверено на
 * железе: яркость 200 — state 0; duration 300 и activate 1 — state 1, через
 * 0,3 с снова 0.
 *
 * Что делает. Заводит через uinput устройство с обратной связью типа FF_RUMBLE,
 * принимает загрузку эффектов и команды воспроизведения, и на время эффекта
 * держит мотор включённым. Силу сильного мотора переводим в яркость
 * светодиода, слабый мотор складываем с ним в половину силы: отдельного
 * слабого мотора у нас нет.
 *
 * Тонкость uinput: загрузка эффекта приходит не событием, а парой ioctl
 * (UI_BEGIN_FF_UPLOAD / UI_END_FF_UPLOAD), и ответить на неё обязательно,
 * иначе отправитель останется ждать. Поэтому события EV_UINPUT разбираются
 * отдельно от обычных.
 *
 * Мелодия звонка (2026-09-25, просьба хозяина). feedbackd 0.0.3 умеет лишь
 * ровные импульсы и сплошную вибрацию; звонок у него — FF_PERIODIC на 10 с
 * без нарастания. Такой эффект мы играем ритмом из /etc/bf7/vibro-melodiya
 * (одно слово: betkhoven — по умолчанию, sos, serdce, rovno — сплошная, как
 * было). Будильник — тот же FF_PERIODIC, но с нарастанием (fade-in): мотор
 * силы не умеет, поэтому нарастание — импульсы, которые учащаются.
 *
 * Сборка:  gcc -O2 -o bf7-vibrad bf7-vibrad.c -lpthread
 */
#define _GNU_SOURCE
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <poll.h>
#include <errno.h>

#define MOTOR_DIR "/sys/class/leds/vibrator"
#define MAX_EFFECTS 16

static int node;
static struct ff_effect effects[MAX_EFFECTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int running_id = -1;
static pthread_t worker;
/* каждое «пуск» и «стоп» меняет поколение — прежний поток видит и уходит */
static unsigned pokolenie;

static void zapisat(const char *imya, unsigned value)
{
	char put[96], buf[16];
	int fd;

	snprintf(put, sizeof put, "%s/%s", MOTOR_DIR, imya);
	fd = open(put, O_WRONLY);
	if (fd < 0)
		return;
	snprintf(buf, sizeof buf, "%u\n", value);
	if (write(fd, buf, strlen(buf)) < 0)
		perror("вибро");
	close(fd);
}

/* Включить мотор на ms миллисекунд; выключится он сам. */
static void motor_on(unsigned ms)
{
	zapisat("duration", ms);
	zapisat("activate", 1);
}

static void motor_off(void)
{
	/*
	 * Только если мотор и правда работает: выключение выключенного
	 * драйвер отмечает строкой «vibr_reg already disabled» в журнале ядра —
	 * 108 строк за три дня (2026-09-30).
	 */
	char put[96], c = '0';
	int fd;

	snprintf(put, sizeof put, "%s/state", MOTOR_DIR);
	fd = open(put, O_RDONLY);
	if (fd >= 0) {
		if (read(fd, &c, 1) != 1)
			c = '1';
		close(fd);
	} else {
		c = '1';
	}
	if (c != '0')
		zapisat("activate", 0);
}

/*
 * Есть ли у эффекта сила. Разбираем по типу: u — объединение, и у FF_PERIODIC
 * на месте rumble.strong_magnitude лежит тип волны, а на месте weak — период.
 * Прежний код читал rumble у всех эффектов, а feedbackd шлёт как раз
 * FF_PERIODIC. Силу мотор всё равно не умеет — он только вкл/выкл, — так что
 * важно лишь, ноль она или нет.
 */
static int est_sila(const struct ff_effect *e)
{
	switch (e->type) {
	case FF_RUMBLE:
		return e->u.rumble.strong_magnitude || e->u.rumble.weak_magnitude;
	case FF_PERIODIC:
		return e->u.periodic.magnitude != 0;
	case FF_CONSTANT:
		return e->u.constant.level != 0;
	default:
		return 1;
	}
}

/* Ритмы: пары «вибро, пауза» в мс. Все мотивы — общественное достояние. */
struct shag { unsigned vkl, pauza; };

/* Бетховен, Пятая симфония: та-та-та-таам, та-та-та-таам */
static const struct shag betkhoven[] = {
	{ 130, 70 }, { 130, 70 }, { 130, 70 }, { 800, 450 },
	{ 130, 70 }, { 130, 70 }, { 130, 70 }, { 1000, 1200 },
};
/* ... --- ... */
static const struct shag sos[] = {
	{ 120, 120 }, { 120, 120 }, { 120, 360 },
	{ 400, 120 }, { 400, 120 }, { 400, 360 },
	{ 120, 120 }, { 120, 120 }, { 120, 1200 },
};
/* тук-тук … тук-тук */
static const struct shag serdce[] = {
	{ 90, 140 }, { 160, 900 },
};

static const struct shag *vybrat_melodiyu(size_t *n)
{
	char slovo[32] = "";
	FILE *f = fopen("/etc/bf7/vibro-melodiya", "r");

	if (f) {
		if (fscanf(f, "%31s", slovo) != 1)
			slovo[0] = 0;
		fclose(f);
	}
	if (!strcmp(slovo, "rovno")) {
		*n = 0;
		return NULL;
	}
	if (!strcmp(slovo, "sos")) {
		*n = sizeof(sos) / sizeof(sos[0]);
		return sos;
	}
	if (!strcmp(slovo, "serdce")) {
		*n = sizeof(serdce) / sizeof(serdce[0]);
		return serdce;
	}
	*n = sizeof(betkhoven) / sizeof(betkhoven[0]);
	return betkhoven;
}

static int tekushchee(unsigned gen)
{
	int da;

	pthread_mutex_lock(&lock);
	da = pokolenie == gen;
	pthread_mutex_unlock(&lock);
	return da;
}

/* поспать ms; 0 — если за это время эффект остановили или сменили */
static int spat(unsigned ms, unsigned gen)
{
	while (ms) {
		unsigned kusok = ms > 25 ? 25 : ms;

		usleep(kusok * 1000);
		ms -= kusok;
		if (!tekushchee(gen))
			return 0;
	}
	return 1;
}

struct zadanie { int id; unsigned gen; };

static void *play(void *arg)
{
	struct zadanie z = *(struct zadanie *)arg;
	struct ff_effect e;

	free(arg);
	pthread_mutex_lock(&lock);
	e = effects[z.id];
	pthread_mutex_unlock(&lock);

	unsigned ms = e.replay.length ? e.replay.length : 150;
	unsigned delay = e.replay.delay;

	if (delay && !spat(delay, z.gen))
		return NULL;

	if (!est_sila(&e))
		goto konec;

	/*
	 * Нарастание настоящее, только если начальный уровень ниже силы: у
	 * звонка feedbackd тоже ставит attack_length = 10000, но с attack_level
	 * = 32767 = magnitude (проверено BF7_VIBRAD_TRACE), у будильника — 4096.
	 */
	if (e.type == FF_PERIODIC && e.u.periodic.envelope.attack_length > 0 &&
	    e.u.periodic.envelope.attack_level < abs(e.u.periodic.magnitude)) {
		/* будильник: импульсы по 250 мс, паузы от 1,5 с до 0,1 с за время нарастания */
		unsigned nar = e.u.periodic.envelope.attack_length, t = 0;

		while (t < ms) {
			unsigned pauza = t >= nar ? 100 : 1500 - 1400 * t / nar;

			motor_on(250);
			if (!spat(250 + pauza, z.gen))
				return NULL;
			t += 250 + pauza;
		}
	} else if (e.type == FF_PERIODIC && ms >= 5000) {
		/* звонок: ритм мелодии, пока не кончится эффект или его не остановят */
		size_t n;
		const struct shag *m = vybrat_melodiyu(&n);
		unsigned t = 0;

		if (!m) {
			motor_on(ms);
			spat(ms, z.gen);
			goto konec;
		}
		while (t < ms) {
			for (size_t i = 0; i < n && t < ms; i++) {
				motor_on(m[i].vkl);
				if (!spat(m[i].vkl + m[i].pauza, z.gen))
					return NULL;
				t += m[i].vkl + m[i].pauza;
			}
		}
	} else {
		motor_on(ms);
		if (!spat(ms, z.gen))
			return NULL;
	}

konec:
	pthread_mutex_lock(&lock);
	if (running_id == z.id)
		running_id = -1;
	pthread_mutex_unlock(&lock);
	return NULL;
}

static void handle_upload(void)
{
	struct uinput_ff_upload up;

	memset(&up, 0, sizeof up);
	up.request_id = 0;
	/* Номер запроса приходит в самом событии, но ядро позволяет взять
	   последний через UI_BEGIN_FF_UPLOAD с заполненным request_id. */
}

int main(void)
{
	node = open("/dev/uinput", O_RDWR | O_NONBLOCK);
	if (node < 0) { perror("/dev/uinput"); return 1; }

	ioctl(node, UI_SET_EVBIT, EV_FF);
	ioctl(node, UI_SET_FFBIT, FF_RUMBLE);
	ioctl(node, UI_SET_FFBIT, FF_PERIODIC);
	ioctl(node, UI_SET_FFBIT, FF_CONSTANT);
	/*
	 * Формы волны. Ядро (ff-core) принимает FF_PERIODIC, только если форма
	 * объявлена устройством, иначе загрузка — EINVAL. Их тут не было, и
	 * звонок с будильником (у feedbackd это FF_PERIODIC с FF_SINE) до
	 * 2026-09-25 не вибрировали вовсе — работали лишь короткие FF_RUMBLE.
	 */
	ioctl(node, UI_SET_FFBIT, FF_SINE);
	ioctl(node, UI_SET_FFBIT, FF_SQUARE);
	ioctl(node, UI_SET_FFBIT, FF_TRIANGLE);
	ioctl(node, UI_SET_FFBIT, FF_SAW_UP);
	ioctl(node, UI_SET_FFBIT, FF_SAW_DOWN);
	ioctl(node, UI_SET_EVBIT, EV_SYN);

	struct uinput_setup s;
	memset(&s, 0, sizeof s);
	s.id.bustype = BUS_VIRTUAL;
	s.id.vendor = 0xbf07;
	s.id.product = 0x0003;
	s.ff_effects_max = MAX_EFFECTS;
	snprintf(s.name, sizeof s.name, "bf7-vibrator");
	if (ioctl(node, UI_DEV_SETUP, &s) < 0) { perror("UI_DEV_SETUP"); return 1; }
	if (ioctl(node, UI_DEV_CREATE) < 0) { perror("UI_DEV_CREATE"); return 1; }

	setvbuf(stderr, NULL, _IOLBF, 0);
	/* BF7_VIBRAD_TRACE=1 — печатать каждый загруженный эффект */
	int trace = getenv("BF7_VIBRAD_TRACE") != NULL;
	fprintf(stderr, "bf7-vibrad: устройство обратной связи создано\n");
	motor_off();

	struct input_event ev;
	/*
	 * Ждём события через poll, а не опросом. Прежде здесь было чтение без
	 * ожидания и sleep на 20 мс при пустом ответе — 50 пробуждений
	 * процессора в секунду круглосуточно, ради события, которое бывает раз
	 * в несколько минут. Для телефона это прямой расход батареи.
	 */
	struct pollfd zhdat = { .fd = node, .events = POLLIN };

	for (;;) {
		if (poll(&zhdat, 1, -1) < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			return 1;
		}

		ssize_t n = read(node, &ev, sizeof ev);
		if (n != sizeof ev)
			continue;

		if (ev.type == EV_UINPUT && ev.code == UI_FF_UPLOAD) {
			struct uinput_ff_upload up;
			memset(&up, 0, sizeof up);
			up.request_id = ev.value;
			if (ioctl(node, UI_BEGIN_FF_UPLOAD, &up) < 0) continue;
			int id = up.effect.id;
			if (id >= 0 && id < MAX_EFFECTS) {
				pthread_mutex_lock(&lock);
				effects[id] = up.effect;
				pthread_mutex_unlock(&lock);
				if (trace)
					fprintf(stderr, "эффект %d: тип 0x%x, %u мс, задержка %u, нарастание %u мс до %u, сила %d\n",
						id, up.effect.type, up.effect.replay.length, up.effect.replay.delay,
						up.effect.type == FF_PERIODIC ? up.effect.u.periodic.envelope.attack_length : 0,
						up.effect.type == FF_PERIODIC ? up.effect.u.periodic.envelope.attack_level : 0,
						up.effect.type == FF_PERIODIC ? up.effect.u.periodic.magnitude : 0);
			}
			up.retval = 0;
			ioctl(node, UI_END_FF_UPLOAD, &up);
			continue;
		}

		if (ev.type == EV_UINPUT && ev.code == UI_FF_ERASE) {
			struct uinput_ff_erase er;
			memset(&er, 0, sizeof er);
			er.request_id = ev.value;
			if (ioctl(node, UI_BEGIN_FF_ERASE, &er) < 0) continue;
			er.retval = 0;
			ioctl(node, UI_END_FF_ERASE, &er);
			continue;
		}

		if (ev.type == EV_FF) {
			int id = ev.code;
			if (id < 0 || id >= MAX_EFFECTS)
				continue;
			if (ev.value) {
				struct zadanie *z = malloc(sizeof(*z));

				if (!z)
					continue;
				pthread_mutex_lock(&lock);
				running_id = id;
				z->id = id;
				z->gen = ++pokolenie;
				pthread_mutex_unlock(&lock);
				if (pthread_create(&worker, NULL, play, z) == 0)
					pthread_detach(worker);
				else
					free(z);
			} else {
				pthread_mutex_lock(&lock);
				pokolenie++;
				pthread_mutex_unlock(&lock);
				motor_off();
			}
		}
	}
	(void)handle_upload;
	return 0;
}
