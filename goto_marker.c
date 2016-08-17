/*
 *
 *  Copyright (C) 2016  Leif Persson <leifmariposa@hotmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <glib.h>
#include <glib/gprintf.h>
#include <geanyplugin.h>
#include <libgen.h>


#define D(x) /*x*/


/**********************************************************************/
static const char *PLUGIN_NAME = "Go to Marker";
static const char *PLUGIN_DESCRIPTION = "Dialog with quick search to quickly goto a bookmark in the current document";
static const char *PLUGIN_VERSION = "0.1";
static const char *PLUGIN_AUTHOR = "Leif Persson <leifmariposa@hotmail.com>";
static const char *PLUGIN_KEY_NAME = "goto_marker";
static const int   WINDOW_WIDTH = 720;
static const int   WINDOW_HEIGHT = 500;


/**********************************************************************/
GeanyPlugin *geany_plugin;


/**********************************************************************/
enum
{
	KB_GOTO_FUNCTION,
	KB_COUNT
};


/**********************************************************************/
enum
{
	COL_LINE_TEXT = 0,
	COL_LINE_NUMBER = 1,
	NUM_COLS
};


/**********************************************************************/
struct PLUGIN_DATA
{
	GtkWidget           *main_window;
	GtkWidget           *text_entry;
	GtkWidget           *tree_view;
	GtkTreeSelection    *selection;
	GtkTreeModel        *model;
	GtkTreeModel        *filter;
	GtkTreeModel        *sorted;
	const gchar         *text_value;
	GtkWidget           *remove_all_markers;
	GtkWidget           *remove_marker;
	GtkWidget           *cancel_button;
	GtkWidget           *goto_button;
} PLUGIN_DATA;


/**********************************************************************/
D(static void log_debug(const gchar* s, ...)
{
	gchar* format = g_strconcat("[CTR DEBUG] : ", s, "\n", NULL);
	va_list l;
	va_start(l, s);
	g_vprintf(format, l);
	g_free(format);
	va_end(l);
})

/**********************************************************************/
static GtkTreeModel* get_markers()
{
	GtkListStore *store = gtk_list_store_new(NUM_COLS,
	G_TYPE_STRING,
	G_TYPE_UINT);

	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	GtkTreeIter iter;
	GeanyDocument * doc = document_get_current();
	if (doc && doc->is_valid)
	{

		ScintillaObject* sci = doc->editor->sci;
		gint mask = 1 << 1;
		gint line_nr = 0;

		while((line_nr = scintilla_send_message(sci, SCI_MARKERNEXT, line_nr, mask)) != -1)
		{
			gchar *line = g_strstrip(sci_get_line(sci, line_nr));
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, COL_LINE_NUMBER, line_nr + 1, -1);
			gtk_list_store_set(store, &iter, COL_LINE_TEXT, line, -1);
			g_free(line);
			line_nr++;
		}
	}

	return GTK_TREE_MODEL(store);
}


/**********************************************************************/
static gboolean count(G_GNUC_UNUSED GtkTreeModel *model,
					  G_GNUC_UNUSED GtkTreePath *path,
					  G_GNUC_UNUSED GtkTreeIter *iter,
					  gint *no_rows )
{
	(*no_rows)++;

	return FALSE;
}


/**********************************************************************/
void select_first_row(struct PLUGIN_DATA *plugin_data)
{
	GtkTreePath *path = gtk_tree_path_new_from_indices(0, -1);
	gtk_tree_view_set_cursor(GTK_TREE_VIEW(plugin_data->tree_view), path, NULL, FALSE);
	gtk_tree_path_free(path);
}


/**********************************************************************/
static int update_gui_elements(G_GNUC_UNUSED GtkWidget *widget, struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	plugin_data->text_value = gtk_entry_get_text(GTK_ENTRY(plugin_data->text_entry));

	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(plugin_data->filter));

	gint total_rows = 0;
	gint filtered_rows = 0;
	gtk_tree_model_foreach(plugin_data->model, (GtkTreeModelForeachFunc)count, &total_rows);
	gtk_tree_model_foreach(plugin_data->filter, (GtkTreeModelForeachFunc)count, &filtered_rows);
	gchar buf[20];
	g_sprintf(buf, "%s %d/%d", PLUGIN_NAME, filtered_rows, total_rows);
	gtk_window_set_title(GTK_WINDOW(plugin_data->main_window), buf);

	select_first_row(plugin_data);

	gtk_widget_set_sensitive(plugin_data->remove_all_markers, filtered_rows > 0);
	gtk_widget_set_sensitive(plugin_data->remove_marker, filtered_rows > 0);
	gtk_widget_set_sensitive(plugin_data->goto_button, filtered_rows > 0);

	return 0;
}


/**********************************************************************/
static gboolean row_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	struct PLUGIN_DATA *plugin_data = data;
	gchar *short_name;
	gint   line_number;
	gboolean visible = FALSE;
	gchar *line;

	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	gtk_tree_model_get(model, iter, COL_LINE_TEXT, &short_name, COL_LINE_NUMBER, &line_number, -1);
	const gchar *text_value = plugin_data->text_value;

	if(!text_value)
		visible = TRUE;

	if(g_strcmp0(text_value, "") == 0)
		visible = TRUE;

	if(short_name && g_str_match_string(text_value, short_name, TRUE))
		visible = TRUE;

	line = g_strdup_printf("%d", line_number);
	if(short_name && g_str_match_string(text_value, line, TRUE))
		visible = TRUE;

	g_free(short_name);
	g_free(line);

	return visible;
}

/**********************************************************************/
void activate_selected_function_and_quit(struct PLUGIN_DATA *plugin_data)
{
	GtkTreePath *path = NULL;

	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(plugin_data->tree_view), &path, NULL);
	if (path)
	{
		GtkTreeIter iter;
		if(gtk_tree_model_get_iter(plugin_data->sorted, &iter, path))
		{
			guint line;
			gtk_tree_model_get(plugin_data->sorted, &iter, COL_LINE_NUMBER, &line, -1);

			GeanyDocument *doc = document_get_current();
			if(doc && doc->is_valid)
			{
				navqueue_goto_line(doc, doc, line);

				gtk_widget_destroy(plugin_data->main_window);
				g_free(plugin_data);
			}
		}
		gtk_tree_path_free(path);
	}
}


/**********************************************************************/
void remove_all_markers(struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	GeanyDocument * doc = document_get_current();
	if (doc && doc->is_valid)
	{
		ScintillaObject* sci = doc->editor->sci;
		gint mask = 1 << 0;
		scintilla_send_message(sci, SCI_MARKERDELETEALL, mask, 0);
		
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(plugin_data->tree_view));
		GtkTreeModel *filter_model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(model));
		GtkTreeModel *data_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER(filter_model));
		gtk_list_store_clear(GTK_LIST_STORE(data_model));
	}
}


/**********************************************************************/
void remove_selected_marker(struct PLUGIN_DATA *plugin_data)
{
	GtkTreePath *path = NULL;

	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(plugin_data->tree_view), &path, NULL);
	if(path)
	{
		GtkTreeIter iter;
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(plugin_data->tree_view));
		if(gtk_tree_model_get_iter(model, &iter, path))
		{
			guint line;
			gtk_tree_model_get(plugin_data->sorted, &iter, COL_LINE_NUMBER, &line, -1);

			GeanyDocument * doc = document_get_current();
			if (doc && doc->is_valid)
			{
				ScintillaObject* sci = doc->editor->sci;
				gint mask = 1 << 0;
				scintilla_send_message(sci, SCI_MARKERDELETE , line - 1, mask);

				/* We have the sorted iter and model */
				/* Get filter iter and model */
				GtkTreeIter filter_iter;
				gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(model), &filter_iter, &iter);
				GtkTreeModel *filter_model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(model));

				/* Get iter and model */
				GtkTreeIter child_iter;
				gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(filter_model), &child_iter, &filter_iter);
				GtkTreeModel *data_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER(filter_model));

				/* Remove row */
				gtk_list_store_remove(GTK_LIST_STORE(data_model), &child_iter);

				/* Select the same row as before the deletion */
				gtk_tree_view_set_cursor(GTK_TREE_VIEW(plugin_data->tree_view), path, NULL, FALSE);
			}
		}
		gtk_tree_path_free(path);
	}
}


/**********************************************************************/
void view_on_row_activated(G_GNUC_UNUSED GtkTreeView *treeview,
													 G_GNUC_UNUSED GtkTreePath *path,
													 G_GNUC_UNUSED GtkTreeViewColumn *col,
													 gpointer data)
{
	struct PLUGIN_DATA *plugin_data = data;

	activate_selected_function_and_quit(plugin_data);
}


/**********************************************************************/
static void create_tree_view(struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	plugin_data->model = get_markers();

	plugin_data->filter = gtk_tree_model_filter_new(plugin_data->model, NULL);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(plugin_data->filter), row_visible, plugin_data, NULL);

	plugin_data->sorted = gtk_tree_model_sort_new_with_model(plugin_data->filter);

	plugin_data->tree_view = gtk_tree_view_new_with_model(plugin_data->sorted);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(plugin_data->tree_view ),FALSE);
	g_signal_connect(plugin_data->tree_view, "row-activated", (GCallback) view_on_row_activated, plugin_data);

	GtkCellRenderer *renderer1 = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(plugin_data->tree_view ), -1, "line_number", renderer1, "text", COL_LINE_NUMBER, NULL);

	GtkCellRenderer *renderer2 = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(plugin_data->tree_view ), -1, "line_text", renderer2, "text", COL_LINE_TEXT, NULL);
}


/**********************************************************************/
static void close_plugin(struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	gtk_widget_destroy(plugin_data->main_window);
	g_free(plugin_data);
}


/**********************************************************************/
static gboolean on_key_press(G_GNUC_UNUSED GtkWidget *widget, GdkEventKey *event, struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	switch(event->keyval)
	{
		case 0xff0d: /* GDK_Return */
		case 0xff8d: /* GDK_KP_Enter */
		activate_selected_function_and_quit(plugin_data);
		break;
	case 65307: /* Escape */
		close_plugin(plugin_data);
		break;
	case 0xff54: /* GDK_Down */
		gtk_widget_grab_focus(plugin_data->tree_view);
		break;
	default:
		return FALSE;
	}

	return FALSE;
}


/**********************************************************************/
static void on_remove_all_markers(G_GNUC_UNUSED GtkButton *button, struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));
	
	remove_all_markers(plugin_data);
	update_gui_elements(NULL, plugin_data);
}


/**********************************************************************/
static void on_remove_marker(G_GNUC_UNUSED GtkButton *button, struct PLUGIN_DATA *plugin_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	remove_selected_marker(plugin_data);
	update_gui_elements(NULL, plugin_data);
}


/**********************************************************************/
static void on_cancel_button(G_GNUC_UNUSED GtkButton *button, struct PLUGIN_DATA *plugin_data)
{
	close_plugin(plugin_data);
}


/**********************************************************************/
static void on_goto_button(G_GNUC_UNUSED GtkButton *button, struct PLUGIN_DATA *plugin_data)
{
	activate_selected_function_and_quit(plugin_data);
}


/**********************************************************************/
static gboolean on_quit(G_GNUC_UNUSED GtkWidget *widget,
											  G_GNUC_UNUSED GdkEvent *event,
											  G_GNUC_UNUSED gpointer   data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	return FALSE;
}


/**********************************************************************/
int launch_widget(void)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	struct PLUGIN_DATA * plugin_data =  g_malloc(sizeof(PLUGIN_DATA));
	memset(plugin_data, 0, sizeof(PLUGIN_DATA));

	plugin_data->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_modal(GTK_WINDOW(plugin_data->main_window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(plugin_data->main_window), 5);

	create_tree_view(plugin_data);

	GtkWidget *main_grid = gtk_table_new(3, 1, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(main_grid), 8);
	gtk_table_set_col_spacings(GTK_TABLE(main_grid), 0);

	plugin_data->text_entry = gtk_entry_new();
	g_signal_connect(plugin_data->text_entry, "changed", G_CALLBACK(update_gui_elements), plugin_data);
	gtk_table_attach(GTK_TABLE(main_grid), plugin_data->text_entry, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

	GtkWidget *scrolled_file_list_window = gtk_scrolled_window_new(NULL,NULL);
	gtk_container_add(GTK_CONTAINER(scrolled_file_list_window), plugin_data->tree_view );
	gtk_table_attach_defaults(GTK_TABLE(main_grid), scrolled_file_list_window, 0, 1, 1, 2);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_file_list_window), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	gtk_window_set_title(GTK_WINDOW(plugin_data->main_window), PLUGIN_NAME);
	gtk_widget_set_size_request(plugin_data->main_window, WINDOW_WIDTH, WINDOW_HEIGHT);
	gtk_window_set_position(GTK_WINDOW(plugin_data->main_window), GTK_WIN_POS_CENTER);
	gtk_window_set_modal(GTK_WINDOW(plugin_data->main_window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(plugin_data->main_window), GTK_WINDOW (geany_plugin->geany_data->main_widgets->window));
	g_signal_connect(plugin_data->main_window, "delete_event", G_CALLBACK(on_quit), plugin_data);
	g_signal_connect(plugin_data->main_window, "key-press-event", G_CALLBACK(on_key_press), plugin_data);

	/* Buttons */
	GtkWidget *bbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);

	plugin_data->remove_all_markers = gtk_button_new_with_mnemonic(_("Remove _all Markers"));
	gtk_widget_set_tooltip_text(plugin_data->remove_all_markers, _("Removes all Markers from the active document"));
	gtk_container_add(GTK_CONTAINER(bbox), plugin_data->remove_all_markers);
	g_signal_connect(plugin_data->remove_all_markers, "clicked", G_CALLBACK(on_remove_all_markers), plugin_data);

	plugin_data->remove_marker = gtk_button_new_with_mnemonic(_("_Remove Marker"));
	gtk_widget_set_tooltip_text(plugin_data->remove_marker, _("Remove selected Marker from the active document"));
	gtk_container_add(GTK_CONTAINER(bbox), plugin_data->remove_marker);
	g_signal_connect(plugin_data->remove_marker, "clicked", G_CALLBACK(on_remove_marker), plugin_data);

	plugin_data->cancel_button = gtk_button_new_with_mnemonic(_("_Cancel"));
	gtk_container_add(GTK_CONTAINER(bbox), plugin_data->cancel_button);
	g_signal_connect(plugin_data->cancel_button, "clicked", G_CALLBACK(on_cancel_button), plugin_data);

	plugin_data->goto_button = gtk_button_new_with_mnemonic(_("_Goto"));
	gtk_container_add(GTK_CONTAINER(bbox), plugin_data->goto_button);
	g_signal_connect(plugin_data->goto_button, "clicked", G_CALLBACK(on_goto_button), plugin_data);

	gtk_table_attach(GTK_TABLE(main_grid), bbox, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, GTK_SHRINK, 0, 0);

	gtk_container_add(GTK_CONTAINER(plugin_data->main_window), main_grid);
	gtk_widget_show_all(plugin_data->main_window);

	select_first_row(plugin_data);
	update_gui_elements(NULL, plugin_data);

	return 0;
}


/**********************************************************************/
static void item_activate_cb(G_GNUC_UNUSED GtkMenuItem *menuitem, G_GNUC_UNUSED gpointer user_data)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	launch_widget();
}


/**********************************************************************/
static void kb_activate(G_GNUC_UNUSED guint key_id)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	launch_widget();
}


/**********************************************************************/
static gboolean init(GeanyPlugin *plugin, G_GNUC_UNUSED gpointer pdata)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	GtkWidget* edit_menu = ui_lookup_widget(plugin->geany_data->main_widgets->window, "search1_menu");

	GtkWidget *main_menu_item;
	/* Create a new menu item and show it */
	main_menu_item = gtk_menu_item_new_with_mnemonic(PLUGIN_NAME);
	gtk_widget_show(main_menu_item);
	gtk_container_add(GTK_CONTAINER(edit_menu), main_menu_item);

	GeanyKeyGroup *key_group = plugin_set_key_group(plugin, PLUGIN_KEY_NAME, KB_COUNT, NULL);
	keybindings_set_item(key_group, KB_GOTO_FUNCTION, kb_activate, 0, 0, PLUGIN_KEY_NAME, PLUGIN_NAME, main_menu_item);

	g_signal_connect(main_menu_item, "activate", G_CALLBACK(item_activate_cb), NULL);
	geany_plugin_set_data(plugin, main_menu_item, NULL);

	return TRUE;
}


/**********************************************************************/
static void cleanup(G_GNUC_UNUSED GeanyPlugin *plugin, gpointer pdata)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	GtkWidget *main_menu_item = (GtkWidget*)pdata;
	gtk_widget_destroy(main_menu_item);
}


/**********************************************************************/
G_MODULE_EXPORT
void geany_load_module(GeanyPlugin *plugin)
{
	D(log_debug("%s:%s", __FILE__, __FUNCTION__));

	geany_plugin = plugin;
	plugin->info->name = PLUGIN_NAME;
	plugin->info->description = PLUGIN_DESCRIPTION;
	plugin->info->version = PLUGIN_VERSION;
	plugin->info->author = PLUGIN_AUTHOR;
	plugin->funcs->init = init;
	plugin->funcs->cleanup = cleanup;
	GEANY_PLUGIN_REGISTER(plugin, 225);
}
