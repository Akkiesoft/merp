/*
Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
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
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include <menu-cache.h>

/*----------------------------------------------------------------------------*/
/* Macros                                                                     */
/*----------------------------------------------------------------------------*/

#define ICON_SIZE 24

/*----------------------------------------------------------------------------*/
/* Globals                                                                    */
/*----------------------------------------------------------------------------*/

/* Controls */

static GtkWidget *main_dlg, *menu_tv, *close_btn;
static GtkTreeStore *store;

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
            gtk_tree_store_set (store, &iter, 0, "Separator", 1, icon, 2, id ? id : "NO ID", 3, TRUE, -1);
        }
        else
        {
            gtk_tree_store_set (store, &iter, 0, name ? name : "NO NAME", 1, icon, 2, id ? id : "NO ID", 3, vis, -1);
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

static void close_prog (GtkButton* btn, gpointer ptr)
{
    gtk_main_quit ();
}

/*----------------------------------------------------------------------------*/
/* Main window                                                                */
/*----------------------------------------------------------------------------*/

int main (int argc, char *argv[])
{
    GtkBuilder *builder;
    GtkCellRenderer *renderer;
    GtkTreeIter iter;
    MenuCache *menu_cache;
    MenuCacheDir *dir;

#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    // GTK setup
    gtk_init (&argc, &argv);

    // build the UI
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/menuedit.ui");

    main_dlg = (GtkWidget *) gtk_builder_get_object (builder, "main_window");
    close_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_ok");
    menu_tv = (GtkWidget *) gtk_builder_get_object (builder, "tv_menu");

    g_object_unref (builder);

    g_signal_connect (main_dlg, "delete_event", G_CALLBACK (close_prog), NULL);
    
    gboolean need_prefix = (g_getenv ("XDG_MENU_PREFIX") == NULL);
    menu_cache = menu_cache_lookup (need_prefix ? "lxde-applications.menu+hidden" : "applications.menu+hidden");
    menu_cache_add_reload_notify (menu_cache, NULL, NULL);
    
    dir = NULL;
    while (dir == NULL) dir = menu_cache_dup_root_dir (menu_cache);
    
    store = gtk_tree_store_new (4, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN);
    gtk_tree_store_append (store, &iter, NULL);
    gtk_tree_store_set (store, &iter, 0, "Root", -1);

    renderer = gtk_cell_renderer_toggle_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv),
                                               -1,      
                                               "Visible",  
                                               renderer,
                                               "active", 3,
                                               NULL);
                                               
    renderer = gtk_cell_renderer_pixbuf_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv),
                                               -1,      
                                               "Icon",  
                                               renderer,
                                               "pixbuf", 1,
                                               NULL);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv),
                                               -1,      
                                               "Name",  
                                               renderer,
                                               "text", 0,
                                               NULL);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (menu_tv),
                                               -1,      
                                               "ID",  
                                               renderer,
                                               "text", 2,
                                               NULL);
                                               
    load_menu (dir, &iter);
    gtk_tree_view_set_model (GTK_TREE_VIEW (menu_tv), GTK_TREE_MODEL (store));
    gtk_widget_show_all (main_dlg);

    gtk_main ();

    gtk_widget_destroy (main_dlg);
    return 0;
}

/* End of file                                                                */
/*----------------------------------------------------------------------------*/
