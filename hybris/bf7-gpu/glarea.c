/*
 * Простейшее окно GTK3 с GtkGLArea — чтобы понять, заводится ли GL у GDK
 * вообще, или спотыкается только браузер.
 *   cc -O2 -o glarea glarea.c $(pkg-config --cflags --libs gtk+-3.0) -lGLESv2
 */
#include <gtk/gtk.h>
#include <GLES2/gl2.h>

static void gotovo(GtkGLArea *oblast, GdkGLContext *ctx, gpointer d)
{
	GError *oshibka = gtk_gl_area_get_error(oblast);

	if (oshibka)
		g_print("BF7: GL не завёлся: %s\n", oshibka->message);
	else
		g_print("BF7: GL завёлся, контекст %p\n", (void *)ctx);
}

static gboolean risuem(GtkGLArea *oblast, GdkGLContext *ctx, gpointer d)
{
	glClearColor(0.1f, 0.5f, 0.9f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	return TRUE;
}

int main(int argc, char **argv)
{
	GtkWidget *okno, *oblast;

	gtk_init(&argc, &argv);
	okno = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(okno), 300, 300);
	oblast = gtk_gl_area_new();
	g_signal_connect(oblast, "realize", G_CALLBACK(gotovo), NULL);
	g_signal_connect(oblast, "render", G_CALLBACK(risuem), NULL);
	gtk_container_add(GTK_CONTAINER(okno), oblast);
	g_signal_connect(okno, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(okno);
	g_timeout_add_seconds(12, (GSourceFunc)gtk_main_quit, NULL);
	gtk_main();
	return 0;
}
