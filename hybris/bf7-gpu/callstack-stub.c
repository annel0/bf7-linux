/*
 * Заглушка вместо android::CallStack / android::ProcessCallStack.
 *
 * MTK libgralloc_extra на каждый gralloc_extra_query/perform собирает и
 * печатает полную раскрутку стека: ~44 кадра стека и 5.7 КиБ лога на каждый
 * кадр, десятки миллисекунд в eglSwapBuffers. Здесь все эти символы
 * превращены в пустые функции.
 *
 * Собирается с -nostdlib, чтобы не было ни одной записи DT_NEEDED: библиотеку
 * грузит линковщик libhybris, который glibc искать не умеет. По той же причине
 * здесь нет ни одного вызова в libc.
 *
 * Строковые возвращаемые значения (toString) — это android::String8, то есть
 * структура с одним указателем. Отдаём указатель на пустую строку: владелец
 * String8 просто уменьшит счётчик ссылок в заголовке перед ней, поэтому
 * возвращаем указатель внутрь заранее подготовленного блока.
 */

/* Заголовок SharedBuffer: mRefs=2 (чтобы никогда не освободилось), mSize=1. */
static const unsigned long empty_string8_block[4] = {
	0x0000000200000002UL,	/* mRefs = 2, mSize = 2 (не 0, чтобы не делить на ноль) */
	0x0000000000000000UL,
	0x0000000000000000UL,
	0x0000000000000000UL,
};

static const char *empty_string8(void)
{
	/* SharedBuffer занимает 16 байт перед данными. */
	return (const char *)&empty_string8_block[2];
}

void _ZN7android9CallStackC1Ev(void *self) { (void)self; }
void _ZN7android9CallStackC2Ev(void *self) { (void)self; }
void _ZN7android9CallStackD1Ev(void *self) { (void)self; }
void _ZN7android9CallStackD2Ev(void *self) { (void)self; }

void _ZN7android9CallStackC1EPKci(void *self, const char *a, int b)
{
	(void)self; (void)a; (void)b;
}
void _ZN7android9CallStackC2EPKci(void *self, const char *a, int b)
{
	(void)self; (void)a; (void)b;
}

void _ZN7android9CallStack6updateEii(void *self, int a, int b)
{
	(void)self; (void)a; (void)b;
}

void _ZNK7android9CallStack3logEPKc19android_LogPriorityS2_(const void *self,
							    const char *a, int b,
							    const char *c)
{
	(void)self; (void)a; (void)b; (void)c;
}

void _ZNK7android9CallStack4dumpEiiPKc(const void *self, int a, int b, const char *c)
{
	(void)self; (void)a; (void)b; (void)c;
}

void _ZNK7android9CallStack5printERNS_7PrinterE(const void *self, void *p)
{
	(void)self; (void)p;
}

/* android::String8 CallStack::toString(char const*) const — sret в x8. */
const char **_ZNK7android9CallStack8toStringEPKc(const void *self, const char *a)
{
	static const char *out;

	(void)self; (void)a;
	out = empty_string8();
	return &out;
}

void *_ZN7android9CallStack18getCurrentInternalEi(int a) { (void)a; return 0; }
void _ZN7android9CallStack11deleteStackEPS0_(void *p) { (void)p; }
void _ZN7android9CallStack16logStackInternalEPKcPKS0_19android_LogPriority(
	const char *a, const void *b, int c)
{
	(void)a; (void)b; (void)c;
}

void _ZN7android16ProcessCallStackC1Ev(void *self) { (void)self; }
void _ZN7android16ProcessCallStackC2Ev(void *self) { (void)self; }
void _ZN7android16ProcessCallStackC1ERKS0_(void *self, const void *o) { (void)self; (void)o; }
void _ZN7android16ProcessCallStackC2ERKS0_(void *self, const void *o) { (void)self; (void)o; }
void _ZN7android16ProcessCallStackD1Ev(void *self) { (void)self; }
void _ZN7android16ProcessCallStackD2Ev(void *self) { (void)self; }
void _ZN7android16ProcessCallStack6updateEv(void *self) { (void)self; }
void _ZN7android16ProcessCallStack5clearEv(void *self) { (void)self; }
void _ZNK7android16ProcessCallStack4dumpEiiPKc(const void *self, int a, int b, const char *c)
{
	(void)self; (void)a; (void)b; (void)c;
}
