/*
 * BF7: проверка, что клиент Wayland рисует на GPU и композитор принимает его буфер.
 *
 * Окно GTK3 с GtkGLArea: область заливается меняющимся цветом средствами
 * GL ES. Если на экране видна заливка — значит клиент получил контекст EGL
 * через libhybris, отрисовал кадр на PowerVR и отдал буфер композитору, а тот
 * сумел его показать. Если окно пустое или программа падает — сломано одно из
 * трёх, и печать в поток ошибок говорит, какое именно.
 *
 * Сборка:  gcc -O2 -o glarea glarea.c $(pkg-config --cflags --libs gtk+-3.0 epoxy)
 */
#include <gtk/gtk.h>
/*
 * Через epoxy, а не напрямую: имена gl* у нас берутся только динамическим
 * поиском, иначе glGetString молча отдаёт пустоту (glvnd). GTK внутри себя
 * пользуется той же epoxy, так что контекст будет общий.
 */
#include <epoxy/gl.h>

static int кадров;

static void подготовка(GtkGLArea *обл, gpointer д)
{
	(void)д;
	gtk_gl_area_make_current(обл);
	GError *ош = gtk_gl_area_get_error(обл);
	if (ош) {
		g_printerr("ОШИБКА контекста GL: %s\n", ош->message);
		return;
	}
	g_print("контекст GL создан\n");
	g_print("  GL_VENDOR:   %s\n", glGetString(GL_VENDOR));
	g_print("  GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	g_print("  GL_VERSION:  %s\n", glGetString(GL_VERSION));
}

static gboolean рисовать(GtkGLArea *обл, GdkGLContext *к, gpointer д)
{
	(void)обл; (void)к; (void)д;
	кадров++;
	float t = (float)(кадров % 120) / 120.0f;
	glClearColor(t, 0.25f, 1.0f - t, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	if (кадров == 1 || кадров % 60 == 0)
		g_print("кадр %d нарисован\n", кадров);
	return TRUE;
}

static gboolean тик(gpointer д)
{
	gtk_widget_queue_draw(GTK_WIDGET(д));
	return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	GtkWidget *окно = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(окно), 360, 600);
	g_signal_connect(окно, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *обл = gtk_gl_area_new();
	gtk_gl_area_set_use_es(GTK_GL_AREA(обл), TRUE);
	gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(обл), FALSE);
	g_signal_connect(обл, "realize", G_CALLBACK(подготовка), NULL);
	g_signal_connect(обл, "render", G_CALLBACK(рисовать), NULL);
	gtk_container_add(GTK_CONTAINER(окно), обл);

	gtk_widget_show_all(окно);
	g_timeout_add(33, тик, обл);
	gtk_main();
	g_print("всего кадров: %d\n", кадров);
	return 0;
}
