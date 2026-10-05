/*
 * BF7: проверка касаний в GTK3 под чужим композитором.
 *
 * Оболочка Phosh подсвечивает нажатую клавишу, но щелчка не выдаёт: после
 * wl_touch.up клиент ничего не перерисовывает. Непонятно, чья это беда —
 * нашего стека (композитор, libinput, драйвер) или самой Phosh с её
 * жестами прокрутки. Это окно отвечает на вопрос: голая кнопка GTK3,
 * которая считает щелчки и печатает каждое событие касания.
 *
 * Сборка:  gcc -O2 -o gtktouch gtktouch.c $(pkg-config --cflags --libs gtk+-3.0)
 */
#include <gtk/gtk.h>

static int щелчков;
static GtkWidget *метка;

static void щелчок(GtkButton *к, gpointer д)
{
	(void)к; (void)д;
	щелчков++;
	g_print("ЩЕЛЧОК %d\n", щелчков);
	char *т = g_strdup_printf("щелчков: %d", щелчков);
	gtk_label_set_text(GTK_LABEL(метка), т);
	g_free(т);
}

static gboolean событие(GtkWidget *в, GdkEvent *e, gpointer д)
{
	(void)в; (void)д;
	const char *имя = NULL;
	switch (e->type) {
	case GDK_TOUCH_BEGIN:  имя = "TOUCH_BEGIN";  break;
	case GDK_TOUCH_UPDATE: имя = "TOUCH_UPDATE"; break;
	case GDK_TOUCH_END:    имя = "TOUCH_END";    break;
	case GDK_TOUCH_CANCEL: имя = "TOUCH_CANCEL"; break;
	case GDK_BUTTON_PRESS: имя = "BUTTON_PRESS"; break;
	case GDK_BUTTON_RELEASE: имя = "BUTTON_RELEASE"; break;
	default: break;
	}
	if (имя)
		g_print("событие %s  x=%.1f y=%.1f\n", имя,
			((GdkEventTouch *)e)->x, ((GdkEventTouch *)e)->y);
	return FALSE;
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	GtkWidget *окно = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(окно), 360, 806);
	g_signal_connect(окно, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *столбец = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
	gtk_container_set_border_width(GTK_CONTAINER(столбец), 40);

	метка = gtk_label_new("щелчков: 0");
	GtkWidget *кнопка = gtk_button_new_with_label("НАЖМИ МЕНЯ");
	gtk_widget_set_size_request(кнопка, 200, 200);
	g_signal_connect(кнопка, "clicked", G_CALLBACK(щелчок), NULL);

	gtk_widget_add_events(окно, GDK_TOUCH_MASK | GDK_BUTTON_PRESS_MASK |
				    GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(окно, "event", G_CALLBACK(событие), NULL);

	gtk_box_pack_start(GTK_BOX(столбец), метка, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(столбец), кнопка, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(окно), столбец);
	gtk_widget_show_all(окно);
	gtk_main();
	return 0;
}
