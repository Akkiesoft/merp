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

#define ICON_SIZE 24

/*----------------------------------------------------------------------------*/
/* Globals                                                                    */
/*----------------------------------------------------------------------------*/

/* Controls */

static GtkWidget *main_dlg, *menu_tv, *close_btn;
static GtkTreeStore *store;

/* Cache globals */

MenuCache *menu_cache;
MenuCacheDir *dir;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Helpers                                                                    */
/*----------------------------------------------------------------------------*/

static void load_menu (MenuCacheDir *dir, GtkTreeIter *parent)
{
    GSList *l, *children;
    GtkTreeIter iter;
    GdkPixbuf *icon;
    MenuCacheItem* item;
    const char *name, *id, *icon_name;
    gboolean vis;
    
    int scale = 1; //gtk_widget_get_scale_factor (main_dlg);
    
    if (!menu_cache_dir_is_visible (dir)) return;

    children = menu_cache_dir_list_children (dir);

    for (l = children; l; l = l->next)
    {
        item = MENU_CACHE_ITEM (l->data);
        name = menu_cache_item_get_name (item);
        id = menu_cache_item_get_id (item);
        icon_name = menu_cache_item_get_icon (item);
        if (menu_cache_item_get_type (item) == MENU_CACHE_TYPE_APP) vis = menu_cache_app_get_is_visible (MENU_CACHE_APP (item), SHOW_IN_LXDE);
        else vis = TRUE;
        
        gtk_tree_store_append (store, &iter, parent);
        
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
                    char *fname = g_strdup_printf ("/usr/share/pixmaps/%s", icon_name);
                    icon = gdk_pixbuf_new_from_file_at_size (fname, ICON_SIZE * scale, ICON_SIZE * scale, NULL);
                    g_free (fname);
                }
            }
        }
        if (!icon)
            icon = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), "application-x-executable",
                ICON_SIZE, scale, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        
        if (menu_cache_item_get_type (item) == MENU_CACHE_TYPE_SEP)
        {
            gtk_tree_store_set (store, &iter, ITEM_NAME, "----", ITEM_ICON, NULL, ITEM_ID, "", ITEM_POINTER, item, ITEM_TYPE, menu_cache_item_get_type (item), -1);
        }
        else
        {
            gtk_tree_store_set (store, &iter, ITEM_NAME, name ? name : "NO NAME", ITEM_ICON, icon, ITEM_ID, id ? id : "NO ID", ITEM_VISIBLE, vis, ITEM_POINTER, item, ITEM_TYPE, menu_cache_item_get_type (item), -1);
        }

        if ((menu_cache_item_get_type (item) != MENU_CACHE_TYPE_APP) || (menu_cache_app_get_is_visible (MENU_CACHE_APP (item), SHOW_IN_LXDE)))
        {
            /* process subentries */
            if (menu_cache_item_get_type (item) == MENU_CACHE_TYPE_DIR) load_menu (MENU_CACHE_DIR (item), &iter);
        }
    }

    g_slist_free (children);
}

/*----------------------------------------------------------------------------*/
/* Handlers for main window user interaction                                  */
/*----------------------------------------------------------------------------*/

static void handle_menu_open (GtkWidget *widget, gpointer user_data)
{
     show_properties_dialog (user_data);
}

static gboolean tv_button_press (GtkWidget *self, GdkEventButton event, gpointer user_data)
{
    GtkWidget *menu, *mi;
    GtkTreeModel *mod;
    GtkTreePath *path;
    GtkTreeIter iter;
    MenuCacheItem *cacheitem;

    if (event.type == GDK_BUTTON_PRESS && event.button == 3)
    {
        gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (self), event.x, event.y, &path, NULL, NULL, NULL);
        if (path)
        {
            mod = gtk_tree_view_get_model (GTK_TREE_VIEW (self));
            gtk_tree_model_get_iter (mod, &iter, path);
            gtk_tree_model_get (mod, &iter, 4, &cacheitem, -1);

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

static void visible_toggled (GtkCellRendererToggle *cell, gchar *pat, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    MenuCacheItem *cacheitem;
    gchar *path, *str;
    GKeyFile *kf;
    gsize len;
    int type;

    gboolean state = gtk_cell_renderer_toggle_get_active (cell);

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (menu_tv));
    gtk_tree_model_get_iter_from_string (model, &iter, pat);
    gtk_tree_model_get (model, &iter, 4, &cacheitem, 5, &type, -1);
    gtk_tree_store_set (GTK_TREE_STORE (model), &iter, 3, 1 - gtk_cell_renderer_toggle_get_active (cell), -1);

    if (type == MENU_CACHE_TYPE_APP || type == MENU_CACHE_TYPE_DIR)
    {
        kf = g_key_file_new ();
        path = menu_cache_item_get_file_path (cacheitem);
        g_key_file_load_from_file (kf, path, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);

        g_key_file_set_boolean (kf, "Desktop Entry", "NoDisplay", state);

        str = g_path_get_basename (path);
        g_free (path);
        path = g_build_filename (g_get_home_dir (), ".local", "share", type == MENU_CACHE_TYPE_APP ? "applications" : "desktop-directories", str, NULL);
        g_free (str);

        str = g_path_get_dirname (path);
        g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
        g_free (str);

        str = g_key_file_to_data (kf, &len, NULL);
        g_file_set_contents (path, str, len, NULL);
        g_free (str);

        g_free (path);

        g_key_file_free (kf);
    }
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

    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    // GTK setup
    gtk_init (&argc, &argv);

    // build the UI
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/menuedit.ui");
    main_dlg = (GtkWidget *) gtk_builder_get_object (builder, "main_window");
    close_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_ok");
    menu_tv = (GtkWidget *) gtk_builder_get_object (builder, "tv_menu");
    g_object_unref (builder);

    g_signal_connect (main_dlg, "delete_event", G_CALLBACK (close_prog), NULL);
    g_signal_connect (close_btn, "clicked", G_CALLBACK (close_prog), NULL);
    
    menu_cache = menu_cache_lookup ("applications.menu+hidden");
    menu_cache_add_reload_notify (menu_cache, NULL, NULL);
    
    dir = NULL;
    while (dir == NULL) dir = menu_cache_dup_root_dir (menu_cache);
    
    store = gtk_tree_store_new (6, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER, G_TYPE_INT);

    renderer = gtk_cell_renderer_toggle_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 0, "Visible", renderer, "active", ITEM_VISIBLE, NULL);
    g_signal_connect (renderer, "toggled", G_CALLBACK (visible_toggled), NULL);

    renderer = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 1, "Icon", renderer, "pixbuf", ITEM_ICON, NULL);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 2, "Name", renderer, "text", ITEM_NAME, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 3, "ID", renderer, "text", ITEM_ID, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv), 4, "Type", renderer, "text", ITEM_TYPE, NULL);

    g_signal_connect (menu_tv, "button-press-event", G_CALLBACK (tv_button_press), NULL);

    load_menu (dir, NULL);
    gtk_tree_view_set_model (GTK_TREE_VIEW (menu_tv), GTK_TREE_MODEL (store));
    gtk_widget_show_all (main_dlg);

    gtk_main ();

    gtk_widget_destroy (main_dlg);
    return 0;
}

/* End of file                                                                */
/*----------------------------------------------------------------------------*/
