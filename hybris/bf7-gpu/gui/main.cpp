/*
 * Запуск QML на PowerVR через libhybris.
 *
 * Qt в Debian собран под настольный OpenGL, поэтому eglfs по умолчанию просит
 * конфиг с EGL_OPENGL_BIT, которого у драйвера нет (есть только ES1/ES2/ES3).
 * В Qt6 тип API берётся из QSurfaceFormat в рантайме, поэтому достаточно явно
 * назначить формат по умолчанию до создания QGuiApplication.
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QDebug>
#include <QQmlContext>
#include <QRect>
#include <QWaylandCompositor>
#include <QWaylandOutput>
#include <QWaylandOutputMode>
#include <QKeyEvent>
#include <QFile>

/* BF7: объявление wl_drm — без него Xwayland не включает glamor. */
void bf7InstallWlDrm(QWaylandCompositor *compositor);
/* BF7: объявление android_wlegl — без него браузеры остаются без нашего EGL. */
void bf7InstallWlEgl(QWaylandCompositor *compositor);

/*
 * Кнопка питания гасит и зажигает подсветку.
 *
 * Сна у этого порта нет, поэтому «выключить экран» здесь означает ровно
 * одно — погасить подсветку панели. Раньше кнопку забирал systemd-logind и
 * по короткому нажатию выключал телефон; теперь он её игнорирует (см.
 * units/bf7-power-key.conf), а обрабатываем её мы.
 *
 * Пока экран погашен, ввод не доходит до приложений: иначе первое же касание
 * ушло бы вслепую в то, что окажется под пальцем.
 *
 * Заодно усыпляем и сам тачскрин — выключаем его прерывание. В кармане он не
 * нужен, а ток потребляет. Плата за это: разбудить можно только кнопкой,
 * касанием уже нет.
 *
 * Подстраховка: при запуске композитора прерывание включается безусловно.
 * Иначе, упади он с погашенным экраном, тач остался бы мёртвым до
 * перезагрузки, и это выглядело бы как поломка сенсора.
 */
class Подсветка : public QObject {
public:
	explicit Подсветка(QObject *parent = nullptr) : QObject(parent)
	{
		яркость_до = прочитать();
		if (яркость_до <= 0)
			яркость_до = 140;
		тач(true);
	}

	bool eventFilter(QObject *o, QEvent *e) override
	{
		switch (e->type()) {
		case QEvent::KeyPress:
		case QEvent::KeyRelease: {
			auto *k = static_cast<QKeyEvent *>(e);
			const bool кнопка = k->key() == Qt::Key_PowerOff
				|| k->key() == Qt::Key_PowerDown
				|| k->key() == Qt::Key_Standby
				|| k->key() == Qt::Key_Sleep;

			if (кнопка) {
				if (e->type() == QEvent::KeyPress && !k->isAutoRepeat())
					переключить();
				return true;
			}
			break;
		}
		default:
			break;
		}
		if (!погашено)
			return QObject::eventFilter(o, e);

		switch (e->type()) {
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseMove:
		case QEvent::TouchBegin:
		case QEvent::TouchUpdate:
		case QEvent::TouchEnd:
			зажечь();
			return true;
		default:
			return QObject::eventFilter(o, e);
		}
	}

private:
	static constexpr const char *путь = "/sys/class/leds/lcd-backlight/brightness";
	static constexpr const char *путь_тач =
		"/sys/devices/platform/11012000.spi2/spi_master/spi2/spi2.0"
		"/synaptics_tcm.0/synaptics_tcm/irq_en";

	/* Прерывание тачскрина: 1 — слушаем касания, 0 — спит. */
	static void тач(bool включить)
	{
		QFile f(QString::fromLatin1(путь_тач));

		if (!f.open(QIODevice::WriteOnly)) {
			qWarning() << "тачскрин: не достучаться до" << путь_тач;
			return;
		}
		f.write(включить ? "1\n" : "0\n");
	}

	static int прочитать()
	{
		QFile f(QString::fromLatin1(путь));

		if (!f.open(QIODevice::ReadOnly))
			return -1;
		return f.readAll().trimmed().toInt();
	}

	static bool записать(int v)
	{
		QFile f(QString::fromLatin1(путь));

		if (!f.open(QIODevice::WriteOnly))
			return false;
		return f.write(QByteArray::number(v) + "\n") > 0;
	}

	void переключить()
	{
		if (погашено)
			зажечь();
		else
			погасить();
	}

	void погасить()
	{
		const int v = прочитать();

		if (v > 0)
			яркость_до = v;
		if (записать(0)) {
			погашено = true;
			тач(false);
			qInfo() << "экран погашен и тачскрин усыплён, яркость запомнена:"
				<< яркость_до;
		}
	}

	void зажечь()
	{
		/* Сперва будим тач, потом зажигаем: чтобы первое же касание
		 * после пробуждения не пропало. */
		тач(true);
		if (записать(яркость_до)) {
			погашено = false;
			qInfo() << "экран зажжён:" << яркость_до;
		}
	}

	int яркость_до = 140;
	bool погашено = false;
};

int main(int argc, char *argv[])
{
	QSurfaceFormat fmt;
	fmt.setRenderableType(QSurfaceFormat::OpenGLES);
	fmt.setVersion(3, 0);
	fmt.setProfile(QSurfaceFormat::NoProfile);
	fmt.setRedBufferSize(8);
	fmt.setGreenBufferSize(8);
	fmt.setBlueBufferSize(8);
	fmt.setAlphaBufferSize(8);
	fmt.setDepthBufferSize(24);
	fmt.setStencilBufferSize(8);
	fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	QSurfaceFormat::setDefaultFormat(fmt);

	QGuiApplication app(argc, argv);

	Подсветка подсветка;
	app.installEventFilter(&подсветка);

	QQmlApplicationEngine engine;

	/* Масштаб и поворот делает композитор, а не сеанс X: под Xwayland без
	 * -rootless размер экрана X задаёт вывод Wayland, и настройки Xfce
	 * применять просто некуда. */
	bool ok = false;
	double scale = qEnvironmentVariable("BF7_SCALE", "1.0").toDouble(&ok);
	if (!ok || scale < 0.25 || scale > 4.0)
		scale = 1.0;
	int rotation = qEnvironmentVariableIntValue("BF7_ROTATION");
	rotation = ((rotation % 360) + 360) % 360;
	if (rotation % 90 != 0)
		rotation = 0;
	engine.rootContext()->setContextProperty("bf7Scale", scale);
	engine.rootContext()->setContextProperty("bf7Rotation", rotation);
	/* Пульс для разделения задачи: см. пояснение в compositor.qml. */
	engine.rootContext()->setContextProperty("bf7Heartbeat",
						 qEnvironmentVariableIsSet("BF7_HEARTBEAT"));
	const QString file = argc > 1 ? QString::fromLocal8Bit(argv[1])
				      : QStringLiteral("demo.qml");
	QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
			 &app, []() { qWarning() << "не удалось создать сцену"; QCoreApplication::exit(1); },
			 Qt::QueuedConnection);
	engine.load(QUrl::fromLocalFile(file));
	if (engine.rootObjects().isEmpty()) {
		qWarning() << "сцена пуста";
		return 1;
	}

	/* Размер вывода Wayland задаётся только из C++: свойство geometry у
	 * WaylandOutput доступно из QML лишь на чтение, а Xwayland берёт размер
	 * своего экрана именно оттуда, а не из конфигурации xdg_toplevel. */
	for (QObject *root : engine.rootObjects()) {
		QObject *o = root->findChild<QObject *>(QStringLiteral("bf7Output"));
		auto *out = qobject_cast<QWaylandOutput *>(o);
		if (!out)
			continue;
		const int pw = out->window() ? out->window()->width() : 720;
		const int ph = out->window() ? out->window()->height() : 1612;
		const bool sideways = (rotation == 90 || rotation == 270);
		const int lw = qMax(160, qRound((sideways ? ph : pw) / scale));
		const int lh = qMax(160, qRound((sideways ? pw : ph) / scale));
		/* Размер вывода задаётся режимом: geometry только для чтения. */
		const QWaylandOutputMode mode(QSize(lw, lh), 60000);
		out->setSizeFollowsWindow(false);
		out->addMode(mode, true);
		out->setCurrentMode(mode);
		qInfo().noquote() << QStringLiteral("вывод %1x%2 -> панель %3x%4, масштаб %5, поворот %6")
					 .arg(lw).arg(lh).arg(pw).arg(ph)
					 .arg(scale, 0, 'g', 3).arg(rotation);

		/*
		 * Композитор достаём отсюда же: до вывода мы уже добрались, а
		 * отдельного пути к объекту WaylandCompositor из C++ в этой
		 * сборке нет.
		 */
		bf7InstallWlDrm(out->compositor());
		bf7InstallWlEgl(out->compositor());
	}
	return app.exec();
}
