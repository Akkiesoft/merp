/*============================================================================
Copyright (c) 2026 Raspberry Pi
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
============================================================================*/

#include <fcntl.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <menu-cache.h>

extern void show_properties_dialog (MenuCacheItem *item);

/*----------------------------------------------------------------------------*/
/* Macros                                                                     */
/*----------------------------------------------------------------------------*/

#define ITEM_NAME       0
#define ITEM_ICON       1
#define ITEM_ID         2
#define ITEM_VISIBLE    3
#define ITEM_POINTER    4
#define ITEM_TYPE       5
#define ITEM_ACTIVE     6

#define ICON_SIZE 24

/*----------------------------------------------------------------------------*/
/* Globals                                                                    */
/*----------------------------------------------------------------------------*/

/* Controls */

static GtkWidget *main_dlg, *menu_tv, *close_btn, *new_btn;
static GtkTreeStore *store;

/* Cache globals */

MenuCache *menu_cache;

/* Scaling factor */

int scale;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static gboolean load_menu (MenuCacheDir *dir, GtkTreeIter *parent);
static void reload_tree (MenuCache *mc, gpointer);
static gboolean store_expands (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data);
static void expand_row (gpointer data, gpointer user_data);
static void handle_menu_open (GtkWidget *widget, gpointer user_data);
static gboolean handle_tv_button_press (GtkWidget *self, GdkEventButton event, gpointer user_data);
static void handle_visible_toggled (GtkCellRendererToggle *cell, gchar *pat, gpointer user_data);
static gboolean handle_new_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
static gboolean close_prog (GtkWidget *wid, GdkEvent *ev, gpointer user_data);

/*----------------------------------------------------------------------------*/
/* Loading menu cache                                                         */
/*----------------------------------------------------------------------------*/

static gboolean load_menu (MenuCacheDir *dir, GtkTreeIter *parent)
{
    GSList *l, *children;
    GtkTreeIter iter;
    GdkPixbuf *icon;
    MenuCacheItem* item;
    MenuCacheType type;
    const char *name, *id, *icon_name;
    char *markup, *esc;
    gboolean vis;

    children = menu_cache_dir_list_children (dir);
    if (!children) return FALSE;

    for (l = children; l; l = l->next)
    {
        item = MENU_CACHE_ITEM (l->data);

        name = menu_cache_item_get_name (item);
        id = menu_cache_item_get_id (item);
        icon_name = menu_cache_item_get_icon (item);
        type = menu_cache_item_get_type (item);

        icon = NULL;
        if (icon_name)
        {
            if (strstr (icon_name, "/"))
                icon = gdk_pixbuf_new_from_file_at_size (icon_name, ICON_SIZE * scale, ICON_SIZE * scale, NULL);
            else
            {
                icon = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), icon_name,
                    ICON_SIZE, scale, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);

                // fallback for packages using obsolete icon location
                if (!icon)
                {
                    esc = g_strdup_printf ("/usr/share/pixmaps/%s", icon_name);
                    icon = gdk_pixbuf_new_from_file_at_size (esc, ICON_SIZE * scale, ICON_SIZE * scale, NULL);
                    g_free (esc);
                }
            }
        }

        switch (type)
        {
            case MENU_CACHE_TYPE_SEP :
                vis = TRUE;
                markup = g_strdup_printf ("______");
                break;

            case MENU_CACHE_TYPE_APP :
                if (!icon)
                    icon = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), "application-x-executable",
                        ICON_SIZE, scale, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
                vis = menu_cache_app_get_is_visible (MENU_CACHE_APP (item), SHOW_IN_LXDE);
                esc = g_markup_escape_text (name ? name : "<unnamed>", -1);
                if (!vis) markup = g_strdup_printf ("<span foreground=\"#B0B0B0\">%s</span>", esc);
                else markup = g_strdup (esc);
                g_free (esc);
                break;

            case MENU_CACHE_TYPE_DIR :
                if (!icon)
                    icon = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), "folder",
                        ICON_SIZE, scale, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
                vis = menu_cache_dir_is_visible (MENU_CACHE_DIR (item));
                esc = g_markup_escape_text (name ? name : "<unnamed>", -1);
                if (!vis) markup = g_strdup_printf ("<span foreground=\"#B0B0B0\"><b>%s</b></span>", esc);
                else markup = g_strdup_printf ("<b>%s</b>", esc);
                g_free (esc);
                break;

            default:
                break;
        }

        gtk_tree_store_append (store, &iter, parent);
        gtk_tree_store_set (store, &iter, ITEM_NAME, markup, ITEM_ICON, icon, ITEM_ID, id, ITEM_VISIBLE, vis, ITEM_POINTER, item, ITEM_TYPE, type, ITEM_ACTIVE, type == MENU_CACHE_TYPE_APP, -1);

        g_free (markup);
        if (icon) g_object_unref (icon);

        /* process subentries */
        if (menu_cache_item_get_type (item) == MENU_CACHE_TYPE_DIR)
        {
            if (load_menu (MENU_CACHE_DIR (item), &iter))
                gtk_tree_store_set (store, &iter, ITEM_ACTIVE, TRUE, -1);
        }
    }

    g_slist_free (children);

    return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Reloading on cache changes                                                 */
/*----------------------------------------------------------------------------*/

static void reload_tree (MenuCache *mc, gpointer)
{
    MenuCacheDir *dir;
    GList *expands = NULL;

    // store the current expanders
    gtk_tree_model_foreach (GTK_TREE_MODEL (store), store_expands, &expands);

    // reload cache and tree view
    dir = NULL;
    while (dir == NULL) dir = menu_cache_dup_root_dir (menu_cache);
    gtk_tree_store_clear (store);
    load_menu (dir, NULL);
    menu_cache_item_unref ((MenuCacheItem *) dir);

    // restore the expanders
    g_list_foreach (expands, expand_row, NULL);
    g_list_free_full (expands, (GDestroyNotify) gtk_tree_path_free);
}

static gboolean store_expands (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
    GList **expands = (GList **) data;
    if (gtk_tree_view_row_expanded (GTK_TREE_VIEW (menu_tv), path))
        *expands = g_list_append (*expands, gtk_tree_path_copy (path));
    return FALSE;
}

static void expand_row (gpointer data, gpointer user_data)
{
    gtk_tree_view_expand_row (GTK_TREE_VIEW (menu_tv), (GtkTreePath *) data, FALSE);
}

/*----------------------------------------------------------------------------*/
/* Handlers for main window user interaction                                  */
/*----------------------------------------------------------------------------*/

static void handle_menu_open (GtkWidget *widget, gpointer user_data)
{
     show_properties_dialog ((MenuCacheItem *) user_data);
}

static gboolean handle_tv_button_press (GtkWidget *self, GdkEventButton event, gpointer user_data)
{
    MenuCacheItem *cacheitem;
    MenuCacheType type;
    GtkWidget *menu, *mi;
    GtkTreePath *path;
    GtkTreeIter iter;

    if (event.type == GDK_BUTTON_PRESS && event.button == 3)
    {
        gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (self), event.x, event.y, &path, NULL, NULL, NULL);
        if (path)
        {
            gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
            gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ITEM_POINTER, &cacheitem, ITEM_TYPE, &type, -1);

            if (type != MENU_CACHE_TYPE_APP) return FALSE;

            menu = gtk_menu_new ();

            mi = gtk_menu_item_new_with_label (_("Edit item"));
            g_signal_connect (mi, "activate", G_CALLBACK (handle_menu_open), cacheitem);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);

            gtk_widget_show_all (menu);
            gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) &event);
        }
        return TRUE;
    }

    return FALSE;
}

static void handle_visible_toggled (GtkCellRendererToggle *cell, gchar *path, gpointer user_data)
{
    MenuCacheItem *cacheitem;
    MenuCacheType type;
    GtkTreeIter iter;
    GKeyFile *kf;
    gboolean state;
    gchar *filepath, *str;
    gsize len;

    state = gtk_cell_renderer_toggle_get_active (cell);

    gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (store), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ITEM_POINTER, &cacheitem, ITEM_TYPE, &type, -1);
    gtk_tree_store_set (store, &iter, ITEM_VISIBLE, 1 - gtk_cell_renderer_toggle_get_active (cell), -1);

    if (type == MENU_CACHE_TYPE_APP || type == MENU_CACHE_TYPE_DIR)
    {
        kf = g_key_file_new ();
        filepath = menu_cache_item_get_file_path (cacheitem);
        g_key_file_load_from_file (kf, filepath, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);

        g_key_file_set_boolean (kf, "Desktop Entry", "NoDisplay", state);

        str = g_path_get_basename (filepath);
        g_free (filepath);
        filepath = g_build_filename (g_get_home_dir (), ".local", "share", type == MENU_CACHE_TYPE_APP ? "applications" : "desktop-directories", str, NULL);
        g_free (str);

        str = g_path_get_dirname (filepath);
        g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
        g_free (str);

        str = g_key_file_to_data (kf, &len, NULL);
        g_file_set_contents (filepath, str, len, NULL);
        g_free (str);

        g_free (filepath);

        g_key_file_free (kf);
    }
}

static gboolean handle_new_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    show_properties_dialog (NULL);
    return TRUE;
}

static gboolean close_prog (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    gtk_main_quit ();
    return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Main window                                                                */
/*----------------------------------------------------------------------------*/

int main (int argc, char *argv[])
{
    GtkBuilder *builder;
    GtkCellRenderer *renderer;
    MenuCacheDir *dir;
    MenuCacheNotifyId id;

    // setup localisation
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    // setup GTK
    gtk_init (&argc, &argv);

    store = gtk_tree_store_new (7, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_BOOLEAN);

    // build the UI
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/menuedit.ui");
    main_dlg = (GtkWidget *) gtk_builder_get_object (builder, "main_window");
    close_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_ok");
    new_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_new");
    menu_tv = (GtkWidget *) gtk_builder_get_object (builder, "tv_menu");
    g_object_unref (builder);

    scale = gtk_widget_get_scale_factor (main_dlg);

    // setup handlers
    g_signal_connect (main_dlg, "delete_event", G_CALLBACK (close_prog), NULL);
    g_signal_connect (close_btn, "clicked", G_CALLBACK (close_prog), NULL);
    g_signal_connect (new_btn, "clicked", G_CALLBACK (handle_new_button), NULL);
    g_signal_connect (menu_tv, "button-press-event", G_CALLBACK (handle_tv_button_press), NULL);
    
    // setup tree view
    renderer = gtk_cell_renderer_toggle_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 0, "Visible", renderer, "active", ITEM_VISIBLE, "activatable", ITEM_ACTIVE, NULL);
    g_signal_connect (renderer, "toggled", G_CALLBACK (handle_visible_toggled), NULL);

    renderer = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 1, "Icon", renderer, "pixbuf", ITEM_ICON, NULL);
    GValue val = G_VALUE_INIT;
    g_value_init (&val, G_TYPE_INT);
    g_value_set_int (&val, scale);
    g_object_set_property (G_OBJECT (renderer), "scale", &val);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 2, "Name", renderer, "markup", ITEM_NAME, NULL);

    gtk_tree_view_set_model (GTK_TREE_VIEW (menu_tv), GTK_TREE_MODEL (store));

    // read menu cache and load into tree store
    menu_cache = menu_cache_lookup ("applications.menu+hidden");
    id = menu_cache_add_reload_notify (menu_cache, reload_tree, NULL);

    dir = NULL;
    while (dir == NULL) dir = menu_cache_dup_root_dir (menu_cache);
    load_menu (dir, NULL);
    menu_cache_item_unref ((MenuCacheItem *) dir);

    gtk_widget_show_all (main_dlg);

    gtk_main ();

    gtk_widget_destroy (main_dlg);

    menu_cache_remove_reload_notify (menu_cache, id);
    menu_cache_unref (menu_cache);

    return 0;
}

/* End of file                                                                */
/*----------------------------------------------------------------------------*/
