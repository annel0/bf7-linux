/* Крошечное окно GTK3 с полем ввода — проверка экранной клавиатуры. */
#include <gtk/gtk.h>

static void on_changed(GtkEditable *e, gpointer d)
{
	(void) d;
	g_message("BF7TEST: текст теперь «%s»", gtk_entry_get_text(GTK_ENTRY(e)));
}

int main(int argc, char **argv)
{
	GtkWidget *win, *box, *entry, *label;

	gtk_init(&argc, &argv);
	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "Проба ввода");
	gtk_window_set_default_size(GTK_WINDOW(win), 340, 200);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(box), 16);
	label = gtk_label_new("Нажмите в поле и печатайте");
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "сюда");
	g_signal_connect(entry, "changed", G_CALLBACK(on_changed), NULL);

	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), box);
	gtk_widget_show_all(win);
	gtk_widget_grab_focus(entry);
	gtk_main();
	return 0;
}
