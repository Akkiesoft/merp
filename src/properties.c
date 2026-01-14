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

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <menu-cache.h>

/*----------------------------------------------------------------------------*/
/* Typedefs and macros                                                        */
/*----------------------------------------------------------------------------*/

#define NUM_CATS 12

/*
 * First column = category from FreeDesktop spec, used in app .desktop Categories
 * Second column = name of menu as defined in .menu file
 * Third column = English text name of menu
 */

const char *cat_table[NUM_CATS][3] = {
    {"AudioVideo",   "Multimedia",       "Sound & Video"},
    {"Development",  "Development",      "Programming"},
    {"Education",    "Education",        "Education"},
    {"Game",         "Games",            "Games"},
    {"Graphics",     "Graphics",         "Graphics"},
    {"Help",         "Help",             "Help"},
    {"Network",      "Internet",         "Internet"},
    {"Office",       "Office",           "Office"},
    {"Science",      "Science",          "Science"},
    {"Settings",     "DesktopSettings",  "Preferences"},
    {"System",       "System",           "System Tools"},
    {"Utility",      "Accessories",      "Accessories"}
};

/* Icon view parameters */
#define ITEM_TITLE      0
#define ITEM_ICON       1
#define CELL_WIDTH      100

/*----------------------------------------------------------------------------*/
/* Global data                                                                */
/*----------------------------------------------------------------------------*/

static GtkWidget *dlg, *idlg, *entry_name, *entry_cmd, *entry_dir, *entry_desc, *img_icon, *sw_notif, *sw_terminal, *cb_category, *entry_id, *lbl_target;

static GtkListStore *items;
static GtkTreeModel *sorted;
static GtkTreeModelSort *categories;

static char *icon_name;

extern MenuCache *menu_cache;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static void show_icon (const char *name, GtkWidget *img);
static gboolean update_string_if_changed (GKeyFile *kf, const char *param, const char *value);
static gboolean update_string_if_entry_changed (GKeyFile *kf, const char *param, GtkWidget *widget);
static gboolean update_bool_if_changed (GKeyFile *kf, const char *param, GtkWidget *widget);
static void dialog_cancel (GtkButton *, gpointer);
static void show_icon_dialog (GtkButton *, gpointer);
static void add_icon (gpointer data, gpointer);
static void icon_dialog_ok (GtkButton *, gpointer user_data);
static void load_from_file (GtkButton *, gpointer);
static gboolean set_active_cat (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data);
static void prop_dialog_ok (GtkButton *, gpointer user_data);
static void menu_dialog_ok (GtkButton *, gpointer user_data);

/*----------------------------------------------------------------------------*/
/* Helpers                                                                    */
/*----------------------------------------------------------------------------*/

static void show_icon (const char *name, GtkWidget *img)
{
    GdkPixbuf *pixbuf;
    int scale = gtk_widget_get_scale_factor (img);

    if (strchr (name, '/'))
        pixbuf = gdk_pixbuf_new_from_file_at_scale (name, scale * 32, scale * 32, TRUE, NULL);
    else
        pixbuf = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), name, 32,
            scale, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);

    if (scale == 1) gtk_image_set_from_pixbuf (GTK_IMAGE (img), pixbuf);
    else
    {
        cairo_surface_t *cr = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
        gtk_image_set_from_surface (GTK_IMAGE (img), cr);
        cairo_surface_destroy (cr);
    }

    g_object_unref (pixbuf);
}

static gboolean update_string_if_changed (GKeyFile *kf, const char *param, const char *value)
{
    char *str;
    gboolean update = FALSE;

    str = g_key_file_get_string (kf, "Desktop Entry", param, NULL);
    if (!str && value[0] == 0) return FALSE;
    if (g_strcmp0 (value, str))
    {
        g_key_file_set_string (kf, "Desktop Entry", param, value);
        update = TRUE;
    }
    g_free (str);

    return update;
}

static gboolean update_string_if_entry_changed (GKeyFile *kf, const char *param, GtkWidget *widget)
{
    char *str, *lcparam;
    const char *ent;
    gboolean update;

    // check for a localised version of this parameter in the key file
    str = g_strdup (getenv ("LANG"));
    if (strchr (str, '.')) *(strchr (str, '.')) = 0;
    lcparam = g_strdup_printf ("%s[%s]", param, str);
    if (!g_key_file_has_key (kf, "Desktop Entry", lcparam, NULL))
    {
        g_free (lcparam);
        lcparam = g_strdup (param);
    }
    g_free (str);

    ent = gtk_entry_get_text (GTK_ENTRY (widget));
    update = update_string_if_changed (kf, lcparam, ent);

    g_free (lcparam);
    return update;
}

static gboolean update_bool_if_changed (GKeyFile *kf, const char *param, GtkWidget *widget)
{
    gboolean sw, update = FALSE;

    sw = gtk_switch_get_state (GTK_SWITCH (widget));
    if (sw != g_key_file_get_boolean (kf, "Desktop Entry", param, NULL))
    {
        g_key_file_set_boolean (kf, "Desktop Entry", param, sw);
        update = TRUE;
    }

    return update;
}

static void dialog_cancel (GtkButton *, gpointer data)
{
    gtk_widget_destroy (GTK_WIDGET (data));
}

/*----------------------------------------------------------------------------*/
/* Change icon dialog                                                         */
/*----------------------------------------------------------------------------*/

static void show_icon_dialog (GtkButton *, gpointer)
{
    GtkBuilder *builder;
    GtkCellRenderer *renderer;
    GList *icon_list;
    GtkWidget *iv_icons;
    GValue val = G_VALUE_INIT;

    textdomain (GETTEXT_PACKAGE);
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/merp.ui");
    idlg = (GtkWidget *) gtk_builder_get_object (builder, "wd_icons");
    iv_icons = (GtkWidget *) gtk_builder_get_object (builder, "iv_icons");
    gtk_window_set_transient_for (GTK_WINDOW (idlg), GTK_WINDOW (dlg));
    gtk_window_set_destroy_with_parent (GTK_WINDOW (idlg), TRUE);

    items = gtk_list_store_new (2, G_TYPE_STRING, GDK_TYPE_PIXBUF);
    sorted = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (items));
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sorted), ITEM_TITLE, GTK_SORT_ASCENDING);

    renderer = gtk_cell_renderer_pixbuf_new ();
    gtk_cell_renderer_set_fixed_size (renderer, CELL_WIDTH, -1);
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (iv_icons), renderer, FALSE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (iv_icons), renderer, "pixbuf", ITEM_ICON);
    g_value_init (&val, G_TYPE_INT);
    g_value_set_int (&val, gtk_widget_get_scale_factor (dlg));
    g_object_set_property (G_OBJECT (renderer), "scale", &val);

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_renderer_set_alignment (renderer, 0.5, 0.0);
    g_object_set (renderer, "wrap-width", CELL_WIDTH, "wrap-mode", PANGO_WRAP_WORD, "alignment", PANGO_ALIGN_CENTER, NULL);
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (iv_icons), renderer, FALSE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (iv_icons), renderer, "markup", ITEM_TITLE);

    gtk_icon_view_set_model (GTK_ICON_VIEW (iv_icons), sorted);

    icon_list = gtk_icon_theme_list_icons (gtk_icon_theme_get_default (), "Applications");
    g_list_foreach (icon_list, add_icon, NULL);
    g_list_free_full (icon_list, (GDestroyNotify) g_free);
    gtk_window_set_default_size (GTK_WINDOW (idlg), 500, 400);

    g_signal_connect (gtk_builder_get_object (builder, "btn_i_ok"), "clicked", G_CALLBACK (icon_dialog_ok), iv_icons);
    g_signal_connect (gtk_builder_get_object (builder, "btn_i_cancel"), "clicked", G_CALLBACK (dialog_cancel), idlg);
    g_signal_connect (gtk_builder_get_object (builder, "btn_i_file"), "clicked", G_CALLBACK (load_from_file), idlg);

    gtk_widget_show (idlg);
    g_object_unref (builder);
}

static void add_icon (gpointer data, gpointer)
{
    GtkTreeIter entry;
    GdkPixbuf *icon;
    const char *name = (const char *) data;

    icon = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), name, 32,
        gtk_widget_get_scale_factor (dlg), GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    gtk_list_store_append (items, &entry);
    gtk_list_store_set (items, &entry, ITEM_TITLE, name, ITEM_ICON, icon, -1);
    if (icon) g_object_unref (icon);
}

static void icon_dialog_ok (GtkButton *, gpointer user_data)
{
    GtkTreeIter iter;
    GList *sel;
    GtkWidget *iv_icons = (GtkWidget *) user_data;

    sel = gtk_icon_view_get_selected_items (GTK_ICON_VIEW (iv_icons));
    if (sel)
    {
        g_free (icon_name);
        gtk_tree_model_get_iter (sorted, &iter, (GtkTreePath *) sel->data);
        gtk_tree_model_get (sorted, &iter, ITEM_TITLE, &icon_name, -1);
        g_list_free_full (sel, (GDestroyNotify) gtk_tree_path_free);
        show_icon (icon_name, img_icon);
    }
    gtk_widget_destroy (idlg);
}

static void load_from_file (GtkButton *, gpointer)
{
    GtkWidget *dialog;
    GtkFileFilter *filter;

    filter = gtk_file_filter_new ();
    gtk_file_filter_add_pixbuf_formats (filter);
    dialog = gtk_file_chooser_dialog_new (_("Select Image File"), GTK_WINDOW (idlg), GTK_FILE_CHOOSER_ACTION_OPEN,
        _("_Cancel"), GTK_RESPONSE_CANCEL, _("_Select"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
    {
        g_free (icon_name);
        GtkFileChooser *chooser = GTK_FILE_CHOOSER (dialog);
        icon_name = gtk_file_chooser_get_filename (chooser);
        show_icon (icon_name, img_icon);
        gtk_widget_destroy (idlg);
    }

    gtk_widget_destroy (dialog);
}

/*----------------------------------------------------------------------------*/
/* Menu item dialog                                                           */
/*----------------------------------------------------------------------------*/

void show_properties_dialog (MenuCacheItem *item)
{
    GtkBuilder *builder;
    GtkWidget *lbl_file, *box_path;
    GtkTreeIter entry;
    GtkListStore *cats;
    GtkCellRenderer *rend;
    MenuCacheDir *parent;
    char *path;
    int i;

    textdomain (GETTEXT_PACKAGE);
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/merp.ui");
    dlg = (GtkWidget *) gtk_builder_get_object (builder, "wd_properties");
    lbl_target = (GtkWidget *) gtk_builder_get_object (builder, "lbl_target");
    lbl_file = (GtkWidget *) gtk_builder_get_object (builder, "lbl_file");
    entry_name = (GtkWidget *) gtk_builder_get_object (builder, "entry_name");
    entry_cmd = (GtkWidget *) gtk_builder_get_object (builder, "entry_cmd");
    entry_dir = (GtkWidget *) gtk_builder_get_object (builder, "entry_dir");
    entry_desc = (GtkWidget *) gtk_builder_get_object (builder, "entry_desc");
    entry_id = (GtkWidget *) gtk_builder_get_object (builder, "entry_id");
    img_icon = (GtkWidget *) gtk_builder_get_object (builder, "img_icon");
    sw_notif = (GtkWidget *) gtk_builder_get_object (builder, "sw_notif");
    sw_terminal = (GtkWidget *) gtk_builder_get_object (builder, "sw_terminal");
    cb_category = (GtkWidget *) gtk_builder_get_object (builder, "cb_category");
    box_path = (GtkWidget *) gtk_builder_get_object (builder, "box3");

    cats = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    for (i = 0; i < NUM_CATS; i++)
    {
        gtk_list_store_append (cats, &entry);
        gtk_list_store_set (cats, &entry, 0, cat_table[i][0], 1, cat_table[i][1], 2, _(cat_table[i][2]), -1);
    }
    categories = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (cats)));
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (categories), 2, GTK_SORT_ASCENDING);

    gtk_combo_box_set_model (GTK_COMBO_BOX (cb_category), GTK_TREE_MODEL (categories));
    rend = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (cb_category), rend, FALSE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (cb_category), rend, "text", 2);

    g_signal_connect (gtk_builder_get_object (builder, "btn_cancel"), "clicked", G_CALLBACK (dialog_cancel), dlg);
    g_signal_connect (gtk_builder_get_object (builder, "btn_icons"), "clicked", G_CALLBACK (show_icon_dialog), NULL);
    g_signal_connect (gtk_builder_get_object (builder, "btn_ok"), "clicked", G_CALLBACK (prop_dialog_ok), NULL);

    gtk_window_set_default_size (GTK_WINDOW (dlg), 500, -1);
    g_object_unref (builder);

    if (item)
    {
        icon_name = g_strdup (menu_cache_item_get_icon (item));
        show_icon (icon_name, img_icon);

        path = menu_cache_item_get_file_path (item);
        gtk_label_set_text (GTK_LABEL (lbl_target), path);
        g_free (path);

        gtk_label_set_text (GTK_LABEL (lbl_file), menu_cache_item_get_file_basename (item));
        gtk_entry_set_text (GTK_ENTRY (entry_name), menu_cache_item_get_name (item));
        gtk_entry_set_text (GTK_ENTRY (entry_cmd), menu_cache_app_get_exec (MENU_CACHE_APP (item)));
        if (menu_cache_item_get_comment (item))
            gtk_entry_set_text (GTK_ENTRY (entry_desc), menu_cache_item_get_comment (item));
        if (menu_cache_app_get_working_dir (MENU_CACHE_APP (item)))
            gtk_entry_set_text (GTK_ENTRY (entry_dir), menu_cache_app_get_working_dir (MENU_CACHE_APP (item)));

        gtk_switch_set_active (GTK_SWITCH (sw_notif), menu_cache_app_get_use_sn (MENU_CACHE_APP (item)));
        gtk_switch_set_active (GTK_SWITCH (sw_terminal), menu_cache_app_get_use_terminal (MENU_CACHE_APP (item)));

        parent = menu_cache_item_dup_parent (item);
        path = menu_cache_dir_make_path (parent);
        menu_cache_item_unref (MENU_CACHE_ITEM (parent));
        gtk_tree_model_foreach (GTK_TREE_MODEL (categories), set_active_cat, path);
        g_free (path);

        gtk_widget_hide (entry_id);
    }
    else
    {
        gtk_label_set_text (GTK_LABEL (lbl_target), NULL);
        gtk_widget_hide (lbl_file);
        gtk_widget_hide (box_path);
    }

    gtk_widget_show (dlg);
}

static gboolean set_active_cat (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
    char *str;
    gboolean end = FALSE;

    gtk_tree_model_get (model, iter, 1, &str, -1);
    if (strstr ((const char *) data, str))
    {
        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (cb_category), iter);
        end = TRUE;
    }
    g_free (str);
    return end;
}

static void prop_dialog_ok (GtkButton *, gpointer user_data)
{
    GKeyFile *kf;
    char *path, *str;
    const char *targ, *cat;
    gsize len;
    gboolean update;
    GtkTreeIter iter;

    targ = gtk_label_get_text (GTK_LABEL (lbl_target));
    if (!strlen (targ)) targ = NULL;

    // use the target file as source
    kf = g_key_file_new ();
    if (targ)
        g_key_file_load_from_file (kf, targ, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
    else
        g_key_file_set_string (kf, "Desktop Entry", "Type", "Application");

    update = FALSE;
    update |= update_string_if_entry_changed (kf, "Name", entry_name);
    update |= update_string_if_entry_changed (kf, "Comment", entry_desc);
    update |= update_string_if_entry_changed (kf, "Exec", entry_cmd);
    update |= update_string_if_entry_changed (kf, "Path", entry_dir);
    update |= update_bool_if_changed (kf, "StartupNotify", sw_notif);
    update |= update_bool_if_changed (kf, "Terminal", sw_terminal);
    update |= update_string_if_changed (kf, "Icon", icon_name);

    if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (cb_category), &iter))
        gtk_tree_model_get (GTK_TREE_MODEL (categories), &iter, 0, &cat, -1);

    update |= update_string_if_changed (kf, "Categories", cat);

    // write to the override in local
    if (update)
    {
        if (targ)
            str = g_path_get_basename (targ);
        else
        {
            if (strstr (gtk_entry_get_text (GTK_ENTRY (entry_id)), ".desktop"))
                str = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry_id)));
            else
                str = g_strdup_printf ("%s.desktop", gtk_entry_get_text (GTK_ENTRY (entry_id)));
        }
        path = g_build_filename (g_get_home_dir (), ".local", "share", "applications", str, NULL);
        g_free (str);

        str = g_path_get_dirname (path);
        g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
        g_free (str);

        str = g_key_file_to_data (kf, &len, NULL);
        g_file_set_contents (path, str, len, NULL);
        g_free (str);

        g_free (path);

        menu_cache_reload (menu_cache);
    }
    g_key_file_free (kf);

    gtk_widget_destroy (dlg);
}

/*----------------------------------------------------------------------------*/
/* Menu dialog                                                                */
/*----------------------------------------------------------------------------*/

void show_menu_dialog (MenuCacheItem *item)
{
    GtkBuilder *builder;
    GtkWidget *lbl_file;
    char *path;

    textdomain (GETTEXT_PACKAGE);
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/merp.ui");
    dlg = (GtkWidget *) gtk_builder_get_object (builder, "wd_menu");
    lbl_target = (GtkWidget *) gtk_builder_get_object (builder, "lbl_mtarget");
    lbl_file = (GtkWidget *) gtk_builder_get_object (builder, "lbl_mfile");
    entry_name = (GtkWidget *) gtk_builder_get_object (builder, "entry_mname");
    img_icon = (GtkWidget *) gtk_builder_get_object (builder, "img_micon");

    g_signal_connect (gtk_builder_get_object (builder, "btn_mcancel"), "clicked", G_CALLBACK (dialog_cancel), dlg);
    g_signal_connect (gtk_builder_get_object (builder, "btn_micons"), "clicked", G_CALLBACK (show_icon_dialog), NULL);
    g_signal_connect (gtk_builder_get_object (builder, "btn_mok"), "clicked", G_CALLBACK (menu_dialog_ok), NULL);

    gtk_window_set_default_size (GTK_WINDOW (dlg), 500, -1);
    g_object_unref (builder);

    if (item)
    {
        icon_name = g_strdup (menu_cache_item_get_icon (item));
        show_icon (icon_name, img_icon);

        path = menu_cache_item_get_file_path (item);
        gtk_label_set_text (GTK_LABEL (lbl_target), path);
        g_free (path);

        gtk_label_set_text (GTK_LABEL (lbl_file), menu_cache_item_get_file_basename (item));
        gtk_entry_set_text (GTK_ENTRY (entry_name), menu_cache_item_get_name (item));
    }

    gtk_widget_show (dlg);
}

static void menu_dialog_ok (GtkButton *, gpointer user_data)
{
    GKeyFile *kf;
    char *path, *str;
    const char *targ;
    gsize len;
    gboolean update;

    targ = gtk_label_get_text (GTK_LABEL (lbl_target));

    // use the target file as source
    kf = g_key_file_new ();
    g_key_file_load_from_file (kf, targ, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);

    update = FALSE;
    update |= update_string_if_entry_changed (kf, "Name", entry_name);
    update |= update_string_if_changed (kf, "Icon", icon_name);

    // write to the override in local
    if (update)
    {
        str = g_path_get_basename (targ);
        path = g_build_filename (g_get_home_dir (), ".local", "share", "desktop-directories",  str, NULL);
        g_free (str);

        str = g_path_get_dirname (path);
        g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
        g_free (str);

        str = g_key_file_to_data (kf, &len, NULL);
        g_file_set_contents (path, str, len, NULL);
        g_free (str);

        g_free (path);

        menu_cache_reload (menu_cache);
    }
    g_key_file_free (kf);

    gtk_widget_destroy (dlg);
}


/* End of file */
/*----------------------------------------------------------------------------*/
