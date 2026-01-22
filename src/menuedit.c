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
#include <libxml/xpath.h>

#include "menuedit.h"

/*----------------------------------------------------------------------------*/
/* Macros                                                                     */
/*----------------------------------------------------------------------------*/

#define ICON_SIZE 24

#define XC(str) ((xmlChar *) str)

/*----------------------------------------------------------------------------*/
/* Globals                                                                    */
/*----------------------------------------------------------------------------*/

/* Controls */

static GtkBuilder *builder;
GtkWidget *main_dlg;
static GtkWidget *menu_tv, *new_btn, *scroll, *edit_btn, *up_btn, *dn_btn, *root_btn, *sep_btn;
static GtkTreeStore *store;
static gboolean pressed;
static double press_x, press_y;

/* Cache globals */

MenuCache *menu_cache;
MenuCacheNotifyId id;
GtkTreeModelSort *categories;

/* Scaling factor */

int scale;

/* XML menu definition files */

char *sysmenufile, *usermenufile;

/* Globals for use when traversing XML */

xmlNode *root_node, *cur_node;

/* Used to preserve the scroll of the tree view when redrawing */

gdouble tv_scroll;
gboolean rescroll = FALSE;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static gboolean load_menu (MenuCacheDir *dir, GtkTreeIter *parent);
static gboolean can_execute (MenuCacheItem *item);
static gboolean only_dirs (GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static void delete_cache (void);
static void reload_tree (MenuCache *mc, gpointer);
static gboolean store_expands (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data);
static void expand_row (gpointer data, gpointer user_data);
static void set_scroll (GtkWidget *wid, GtkAllocation *alloc, gpointer user_data);
static void write_menu_xml (char *id);
static gboolean add_toplevel_to_xml (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data);
static gboolean add_submenus_to_xml (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data);
static void create_node (const char *name, const char *content, const char *type, gboolean enter);
static char *get_parent (GtkTreePath *path);
static void add_item_to_xml (GtkTreeModel *model, GtkTreeIter *iter);
static void handle_edit_item (GtkWidget *widget, gpointer user_data);
static void handle_item_up (GtkWidget *widget, gpointer user_data);
static void handle_item_down (GtkWidget *widget, gpointer user_data);
static void handle_toggle_separator (GtkWidget *widget, gpointer user_data);
static void handle_move_to_root (GtkWidget *widget, gpointer user_data);
static gboolean handle_tv_button_press (GtkWidget *self, GdkEventButton event, gpointer user_data);
static void create_popup_menu (gdouble x, gdouble y);
static void handle_visible_toggled (GtkCellRendererToggle *cell, gchar *pat, gpointer user_data);
static gboolean handle_new_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
static gboolean handle_edit_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
static gboolean handle_up_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
static gboolean handle_down_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
static gboolean handle_root_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
static void handle_selection_changed (GtkTreeSelection *sel, gpointer user_data);
static void init_main_window (void);
static void gesture_pressed (GtkGestureLongPress *, gdouble x, gdouble y, gpointer);
static void gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer);
#ifndef PLUGIN_NAME
static gboolean close_prog (GtkWidget *wid, GdkEvent *ev, gpointer user_data);
#endif

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

    while (dir == NULL) dir = menu_cache_dup_root_dir (menu_cache);
    menu_cache_item_unref ((MenuCacheItem *) dir);

    children = menu_cache_dir_list_children (dir);
    if (!children) return FALSE;

    for (l = children; l; l = l->next)
    {
        item = MENU_CACHE_ITEM (l->data);

        name = menu_cache_item_get_name (item);
        id = menu_cache_item_get_id (item);
        icon_name = menu_cache_item_get_icon (item);
        type = menu_cache_item_get_type (item);

        if (type != MENU_CACHE_TYPE_SEP && !name) continue;
        if (type == MENU_CACHE_TYPE_APP && !can_execute (item)) continue;

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
                esc = g_markup_escape_text (name, -1);
                if (!vis) markup = g_strdup_printf ("<span foreground=\"#B0B0B0\">%s</span>", esc);
                else markup = g_strdup (esc);
                g_free (esc);
                break;

            case MENU_CACHE_TYPE_DIR :
                if (!icon)
                    icon = gtk_icon_theme_load_icon_for_scale (gtk_icon_theme_get_default (), "folder",
                        ICON_SIZE, scale, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
                vis = menu_cache_dir_is_visible (MENU_CACHE_DIR (item));
                esc = g_markup_escape_text (name, -1);
                if (!vis) markup = g_strdup_printf ("<span foreground=\"#B0B0B0\"><b>%s</b></span>", esc);
                else markup = g_strdup_printf ("<b>%s</b>", esc);
                g_free (esc);
                break;

            default:
                if (icon) g_object_unref (icon);
                continue;
        }

        gtk_tree_store_append (store, &iter, parent);
        gtk_tree_store_set (store, &iter, ITEM_NAME, markup, ITEM_ICON, icon, ITEM_ID, id, ITEM_VISIBLE, vis, ITEM_POINTER, item, ITEM_TYPE, type, ITEM_ACTIVE, type == MENU_CACHE_TYPE_APP, ITEM_CBNAME, name, -1);

        g_free (markup);
        if (icon) g_object_unref (icon);

        /* process subentries */
        if (type == MENU_CACHE_TYPE_DIR)
        {
            if (load_menu (MENU_CACHE_DIR (item), &iter))
                gtk_tree_store_set (store, &iter, ITEM_ACTIVE, TRUE, -1);
        }
    }

    g_slist_free (children);

    return TRUE;
}

static gboolean can_execute (MenuCacheItem *item)
{
    GKeyFile *kf;
    const char *filepath;
    char *exec, *path;
    gboolean result = TRUE;

    kf = g_key_file_new ();
    filepath = menu_cache_item_get_file_path (item);
    g_key_file_load_from_file (kf, filepath, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);

    if (g_key_file_has_key (kf, "Desktop Entry", "TryExec", NULL))
    {
        exec = g_key_file_get_string (kf, "Desktop Entry", "TryExec", NULL);
        path = g_find_program_in_path (exec);
        if (!path) result = FALSE;
        g_free (path);
        g_free (exec);
    }

    g_key_file_free (kf);
    return result;
}

static gboolean only_dirs (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    MenuCacheType type;

    gtk_tree_model_get (model, iter, ITEM_TYPE, &type, -1);
    return type == MENU_CACHE_TYPE_DIR;
}

static void delete_cache (void)
{
    struct dirent *dp;
    DIR *dfd;
    char *cache_path, *file;

    cache_path = g_build_filename (g_get_home_dir (), ".cache", "menus", NULL);
    if ((dfd = opendir (cache_path)))
    {
        while ((dp = readdir (dfd)))
        {
            file = g_build_filename (cache_path, dp->d_name, NULL);
            remove (file);
            g_free (file);
        }
    }
    g_free (cache_path);
}

/*----------------------------------------------------------------------------*/
/* Reloading on cache changes                                                 */
/*----------------------------------------------------------------------------*/

static void reload_tree (MenuCache *mc, gpointer)
{
    GtkTreeSelection *sel;
    GtkTreeModel *model;
    GList *expands = NULL, *selects = NULL;

    // if a reload request occurs while a dialog is open, save it for later...
    if (dialog_reload != DIALOG_NOT_OPEN)
    {
        dialog_reload = DIALOG_OPEN_RELOAD;
        return;
    }

    // store the current expanders
    gtk_tree_model_foreach (GTK_TREE_MODEL (store), store_expands, &expands);

    // store the current selection
    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv));
    if (sel) selects = gtk_tree_selection_get_selected_rows (sel, &model);

    // store the current scroll
    tv_scroll = gtk_adjustment_get_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll)));

    // reload cache and tree view
    gtk_tree_store_clear (store);
    load_menu (NULL, NULL);

    // restore the expanders
    g_list_foreach (expands, expand_row, NULL);
    g_list_free_full (expands, (GDestroyNotify) gtk_tree_path_free);

    // restore the selection
    if (sel && selects) gtk_tree_selection_select_path (sel, (GtkTreePath *) selects->data);
    g_list_free_full (selects, (GDestroyNotify) gtk_tree_path_free);

    // restore the scroll
    rescroll = TRUE;
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

static void set_scroll (GtkWidget *wid, GtkAllocation *alloc, gpointer user_data)
{
    if (rescroll)
    {
        gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll)), tv_scroll);
        rescroll = FALSE;
    }
}

/*----------------------------------------------------------------------------*/
/* Writing menu XML file                                                      */
/*----------------------------------------------------------------------------*/

static void write_menu_xml (char *id)
{
    xmlDocPtr xDoc = NULL;
    xmlXPathContextPtr xpathCtx;
    xmlXPathObjectPtr xpathObj;
    xmlNodePtr node;
    xmlChar *cont;
    char *str;
    int i;

    LIBXML_TEST_VERSION

    str = g_path_get_dirname (usermenufile);
    g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
    g_free (str);

    // read in the user file if it exists; init if not
    if (g_file_test (usermenufile, G_FILE_TEST_IS_REGULAR)) xDoc = xmlReadFile (usermenufile, NULL, XML_PARSE_NOBLANKS);
    if (!xDoc) xDoc = xmlNewDoc (XC ("1.0"));
    xpathCtx = xmlXPathNewContext (xDoc);
    root_node = xmlDocGetRootElement (xDoc);
    if (root_node == NULL)
    {
        root_node = xmlNewNode (NULL, XC ("Menu"));
        xmlDocSetRootElement (xDoc, root_node);
        cur_node = root_node;
        create_node ("Name", "Applications", NULL, FALSE);
        create_node ("MergeFile", sysmenufile, "parent", FALSE);
    }
    else cur_node = root_node;

    if (strlen (id) == 0)
    {
        // delete any current top-level layout sections
        xpathObj = xmlXPathEvalExpression (XC ("/*[local-name()='Menu']/*[local-name()='Layout']"), xpathCtx);
        if (xpathObj->nodesetval)
        {
            for (i = 0; i < xpathObj->nodesetval->nodeNr; i++)
            {
                node = xpathObj->nodesetval->nodeTab[i];
                xmlUnlinkNode (node);
                xmlFreeNode (node);
            }
        }
        xmlXPathFreeObject (xpathObj);

        // create a new top-level layout section
        create_node ("Layout", NULL, NULL, TRUE);
        create_node ("Merge", NULL, "menus", FALSE);

        // loop through store adding an item to the Applications layout XML for each top-level element
        gtk_tree_model_foreach (GTK_TREE_MODEL (store), add_toplevel_to_xml, NULL);

        create_node ("Merge", NULL, "files", FALSE);
    }
    else
    {
        // delete any current menu layout sections matching the id
        xpathObj = xmlXPathEvalExpression (XC ("/*[local-name()='Menu']/*[local-name()='Menu']/*[local-name()='Name']"), xpathCtx);
        if (xpathObj->nodesetval)
        {
            for (i = 0; i < xpathObj->nodesetval->nodeNr; i++)
            {
                node = xpathObj->nodesetval->nodeTab[i];
                cont = xmlNodeGetContent (node);
                if (!xmlStrcmp (cont, XC (id)))
                {
                    xmlUnlinkNode (node->parent);
                    xmlFreeNode (node->parent);
                }
                xmlFree (cont);
            }
        }
        xmlXPathFreeObject (xpathObj);

        // create a new menu and layout section
        create_node ("Menu", NULL, NULL, TRUE);
        create_node ("Name", id, NULL, FALSE);

        create_node ("Layout", NULL, NULL, TRUE);
        create_node ("Merge", NULL, "menus", FALSE);

        // loop through the store adding the submenu items matching the id
        gtk_tree_model_foreach (GTK_TREE_MODEL (store), add_submenus_to_xml, id);

        create_node ("Merge", NULL, "files", FALSE);
    }

    xmlSaveFormatFile (usermenufile, xDoc, 1);
    xmlFreeDoc (xDoc);
    xmlCleanupParser ();
}

static gboolean add_toplevel_to_xml (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
    if (gtk_tree_path_get_depth (path) == 1) add_item_to_xml (model, iter);
    return FALSE;
}

static gboolean add_submenus_to_xml (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
    char *parent;

    if (gtk_tree_path_get_depth (path) == 2)
    {
        // only add if the parent of this item matches the one desired
        parent = get_parent (path);
        if (!g_strcmp0 (parent, (char *) data)) add_item_to_xml (model, iter);
        g_free (parent);
    }

    return FALSE;
}

static void add_item_to_xml (GtkTreeModel *model, GtkTreeIter *iter)
{
    MenuCacheType type;
    char *id;

    gtk_tree_model_get (model, iter, ITEM_ID, &id, ITEM_TYPE, &type, -1);

    switch (type)
    {
        case MENU_CACHE_TYPE_SEP :  create_node ("Separator", NULL, NULL, FALSE);
                                    break;
        case MENU_CACHE_TYPE_DIR :  create_node ("Menuname", id, NULL, FALSE);
                                    break;
        case MENU_CACHE_TYPE_APP :  create_node ("Filename", id, NULL, FALSE);
                                    break;
        default :                   break;
    }
}

static void create_node (const char *name, const char *content, const char *type, gboolean enter)
{
    xmlNodePtr node = xmlNewNode (NULL, XC (name));
    if (content) xmlNodeSetContent (node, XC (content));
    if (type) xmlSetProp (node, XC ("type"), XC (type));
    xmlAddChild (cur_node, node);
    if (enter) cur_node = node;
}

static char *get_parent (GtkTreePath *path)
{
    GtkTreeIter this, dest;

    gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &this, path);
    if (!gtk_tree_model_iter_parent (GTK_TREE_MODEL (store), &dest, &this)) return g_strdup ("");
    else
    {
        char *id;
        gtk_tree_model_get (GTK_TREE_MODEL (store), &dest, ITEM_ID, &id, -1);
        return g_strdup (id);
    }
}

void remove_id_from_xml (const char *id)
{
    xmlDocPtr xDoc = NULL;
    xmlXPathContextPtr xpathCtx;
    xmlXPathObjectPtr xpathObj, xpathObj2;
    xmlNodePtr node;
    xmlChar *cont;
    char *str;
    int i;
    gboolean changed = FALSE;

    LIBXML_TEST_VERSION

    // read in the user file
    if (g_file_test (usermenufile, G_FILE_TEST_IS_REGULAR))
    {
        xDoc = xmlReadFile (usermenufile, NULL, XML_PARSE_NOBLANKS);
        xpathCtx = xmlXPathNewContext (xDoc);
    }
    else
    {
        // no user file - read in the system file and manipulate it
        xDoc = xmlReadFile (sysmenufile, NULL, XML_PARSE_NOBLANKS);
        xpathCtx = xmlXPathNewContext (xDoc);

        // remove all nodes other than Name and Layout from the top-level menu
        xpathObj = xmlXPathEvalExpression (XC ("/*[local-name()='Menu']/*[not(local-name()='Name') and not(local-name()='Layout')]"), xpathCtx);
        if (xpathObj->nodesetval)
        {
            for (i = 0; i < xpathObj->nodesetval->nodeNr; i++)
            {
                node = xpathObj->nodesetval->nodeTab[i];
                xmlUnlinkNode (node);
                xmlFreeNode (node);
            }
        }
        xmlXPathFreeObject (xpathObj);

        // add the MergeFile node to the top-level menu
        xpathObj = xmlXPathEvalExpression (XC ("/*[local-name()='Menu']/*"), xpathCtx);
        if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr)
        {
            node = xmlNewNode (NULL, XC ("MergeFile"));
            xmlNodeSetContent (node, XC (sysmenufile));
            xmlAddPrevSibling (xpathObj->nodesetval->nodeTab[0], node);
        }
        xmlXPathFreeObject (xpathObj);

        // remove all Menu nodes which do not contain a Layout node
        xpathObj = xmlXPathEvalExpression (XC ("//*[local-name()='Menu']"), xpathCtx);
        if (xpathObj->nodesetval)
        {
            for (i = 0; i < xpathObj->nodesetval->nodeNr; i++)
            {
                node = xpathObj->nodesetval->nodeTab[i];
                xmlXPathSetContextNode (node, xpathCtx);
                xpathObj2 = xmlXPathEvalExpression (XC ("./*[local-name()='Layout']"), xpathCtx);
                if (xpathObj2->nodesetval && xpathObj2->nodesetval->nodeNr == 0)
                {
                    xmlUnlinkNode (node);
                    xmlFreeNode (node);
                }
                xmlXPathFreeObject (xpathObj2);
            }
        }
        xmlXPathFreeObject (xpathObj);

        xmlXPathSetContextNode (xmlDocGetRootElement (xDoc), xpathCtx);
    }

    // delete any current menu layout sections matching the id
    xpathObj = xmlXPathEvalExpression (XC ("//*[local-name()='Filename']"), xpathCtx);
    if (xpathObj->nodesetval)
    {
        for (i = 0; i < xpathObj->nodesetval->nodeNr; i++)
        {
            node = xpathObj->nodesetval->nodeTab[i];
            cont = xmlNodeGetContent (node);
            if (!xmlStrcmp (cont, XC (id)))
            {
                xmlUnlinkNode (node);
                xmlFreeNode (node);
                changed = TRUE;
            }
            xmlFree (cont);
        }
    }
    xmlXPathFreeObject (xpathObj);

    if (changed)
    {
        str = g_path_get_dirname (usermenufile);
        g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
        g_free (str);

        xmlSaveFormatFile (usermenufile, xDoc, 1);
    }

    xmlFreeDoc (xDoc);
    xmlCleanupParser ();
}

/*----------------------------------------------------------------------------*/
/* Handlers for main window user interaction                                  */
/*----------------------------------------------------------------------------*/

static void handle_edit_item (GtkWidget *widget, gpointer user_data)
{
    MenuCacheItem *cacheitem = (MenuCacheItem *) user_data;
    switch (menu_cache_item_get_type (cacheitem))
    {
        case MENU_CACHE_TYPE_DIR :  show_menu_dialog (cacheitem);
                                    break;
        case MENU_CACHE_TYPE_APP :  show_properties_dialog (cacheitem);
                                    break;
        default :                   break;
    }
}

static void handle_item_up (GtkWidget *widget, gpointer user_data)
{
    GtkTreePath *path;
    GtkTreeIter this, dest;
    char *parent;

    if (!widget) path = (GtkTreePath *) user_data;
    else path = gtk_tree_path_new_from_string (gtk_widget_get_name (widget));

    gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &this, path);
    dest = this;
    gtk_tree_model_iter_previous (GTK_TREE_MODEL (store), &dest);
    gtk_tree_store_move_before (store, &this, &dest);

    parent = get_parent (path);
    write_menu_xml (parent);
    g_free (parent);
    if (widget) gtk_tree_path_free (path);
}

static void handle_item_down (GtkWidget *widget, gpointer user_data)
{
    GtkTreePath *path;
    GtkTreeIter this, dest;
    char *parent;

    if (!widget) path = (GtkTreePath *) user_data;
    else path = gtk_tree_path_new_from_string (gtk_widget_get_name (widget));

    gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &this, path);
    dest = this;
    gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &dest);
    gtk_tree_store_move_after (store, &this, &dest);

    parent = get_parent (path);
    write_menu_xml (parent);
    g_free (parent);
    if (widget) gtk_tree_path_free (path);
}

static void handle_toggle_separator (GtkWidget *widget, gpointer user_data)
{
    GtkTreePath *path;
    GtkTreeIter this, dest;
    MenuCacheType type;
    char *parent;

    if (!widget) path = (GtkTreePath *) user_data;
    else path = gtk_tree_path_new_from_string (gtk_widget_get_name (widget));

    gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &this, path);
    gtk_tree_model_get (GTK_TREE_MODEL (store), &this, ITEM_TYPE, &type, -1);
    if (type == MENU_CACHE_TYPE_SEP) gtk_tree_store_remove (store, &this);
    else
    {
        gtk_tree_store_insert_after (store, &dest, NULL, &this);
        gtk_tree_store_set (store, &dest, ITEM_NAME, "______", ITEM_TYPE, MENU_CACHE_TYPE_SEP, ITEM_VISIBLE, TRUE, -1);
    }

    parent = get_parent (path);
    write_menu_xml (parent);
    g_free (parent);
    if (widget) gtk_tree_path_free (path);
}

static void handle_move_to_root (GtkWidget *widget, gpointer user_data)
{
    MenuCacheItem *item = (MenuCacheItem *) user_data;
    GKeyFile *kf;
    char *path, *str;
    gsize len;

    kf = g_key_file_new ();

    path = menu_cache_item_get_file_path (item);
    g_key_file_load_from_file (kf, path, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL);
    g_free (path);

    g_key_file_set_string (kf, "Desktop Entry", "Categories", "Applications");

    path = g_build_filename (g_get_home_dir (), ".local", "share", "applications", menu_cache_item_get_file_basename (item), NULL);

    str = g_path_get_dirname (path);
    g_mkdir_with_parents (str, S_IRUSR | S_IWUSR | S_IXUSR);
    g_free (str);

    str = g_key_file_to_data (kf, &len, NULL);
    g_file_set_contents (path, str, len, NULL);
    g_free (str);

    g_free (path);

    // remove any reference to this id from the menu XML file, or it will be duplicated
    remove_id_from_xml (menu_cache_item_get_file_basename (item));

    menu_cache_reload (menu_cache);
}

static gboolean handle_tv_button_press (GtkWidget *self, GdkEventButton event, gpointer user_data)
{
    if (event.type == GDK_BUTTON_PRESS && event.button == 3)
    {
        create_popup_menu (event.x, event.y);
        return TRUE;
    }

    return FALSE;
}

static void create_popup_menu (gdouble x, gdouble y)
{
    MenuCacheItem *cacheitem;
    MenuCacheType type;
    GtkWidget *menu, *mi;
    GtkTreePath *path;
    GtkTreeIter iter;
    char *pathstr;

    gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (menu_tv), x, y, &path, NULL, NULL, NULL);
    pathstr = gtk_tree_path_to_string (path);

    if (path)
    {
        gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ITEM_POINTER, &cacheitem, ITEM_TYPE, &type, -1);

        menu = gtk_menu_new ();

        mi = gtk_menu_item_new_with_label (_("Edit Item..."));
        g_signal_connect (mi, "activate", G_CALLBACK (handle_edit_item), cacheitem);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
        gtk_widget_set_sensitive (mi, type != MENU_CACHE_TYPE_SEP);

        mi = gtk_menu_item_new_with_label (_("Move Up"));
        gtk_widget_set_name (mi, pathstr);
        g_signal_connect (mi, "activate", G_CALLBACK (handle_item_up), NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
        if (!gtk_tree_model_iter_previous (GTK_TREE_MODEL (store), &iter))
            gtk_widget_set_sensitive (mi, FALSE);

        mi = gtk_menu_item_new_with_label (_("Move Down"));
        gtk_widget_set_name (mi, pathstr);
        g_signal_connect (mi, "activate", G_CALLBACK (handle_item_down), NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
        gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
        if (!gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter))
            gtk_widget_set_sensitive (mi, FALSE);

        if (type != MENU_CACHE_TYPE_SEP && gtk_tree_path_get_depth (path) == 2)
        {
            mi = gtk_menu_item_new_with_label (_("Move to Root"));
            g_signal_connect (mi, "activate", G_CALLBACK (handle_move_to_root), cacheitem);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
        }

        mi = gtk_menu_item_new_with_label (type == MENU_CACHE_TYPE_SEP ? _("Remove Separator") : _("Add Separator"));
        gtk_widget_set_name (mi, pathstr);
        g_signal_connect (mi, "activate", G_CALLBACK (handle_toggle_separator), NULL);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);

        gtk_widget_show_all (menu);
        gtk_menu_popup_at_pointer (GTK_MENU (menu), NULL);
    }

    g_free (pathstr);
    gtk_tree_path_free (path);
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

static gboolean handle_edit_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    GtkTreeSelection *sel;
    GtkTreeIter iter;
    MenuCacheItem *cacheitem;

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv));
    if (sel && gtk_tree_selection_get_selected (sel, NULL, &iter))
    {
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ITEM_POINTER, &cacheitem, -1);
        handle_edit_item (NULL, cacheitem);
    }
    return TRUE;
}

static gboolean handle_up_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    GtkTreeSelection *sel;
    GtkTreePath *path;
    GList *rows;

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv));
    if (sel && (rows = gtk_tree_selection_get_selected_rows (sel, NULL)))
    {
        path = (GtkTreePath *) rows->data;
        handle_item_up (NULL, path);
        g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);

        handle_selection_changed (sel, NULL);
    }
    return TRUE;
}

static gboolean handle_down_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    GtkTreeSelection *sel;
    GtkTreePath *path;
    GList *rows;

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv));
    if (sel && (rows = gtk_tree_selection_get_selected_rows (sel, NULL)))
    {
        path = (GtkTreePath *) rows->data;
        handle_item_down (NULL, path);
        g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);

        handle_selection_changed (sel, NULL);
    }
    return TRUE;
}

static gboolean handle_root_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    GtkTreeSelection *sel;
    GtkTreeIter iter;
    MenuCacheItem *cacheitem;

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv));
    if (sel && gtk_tree_selection_get_selected (sel, NULL, &iter))
    {
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ITEM_POINTER, &cacheitem, -1);
        handle_move_to_root (NULL, cacheitem);
    }
    return TRUE;
}

static gboolean handle_sep_button (GtkWidget *wid, GdkEvent *ev, gpointer user_data)
{
    GtkTreeSelection *sel;
    GtkTreePath *path;
    GList *rows;

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv));
    if (sel && (rows = gtk_tree_selection_get_selected_rows (sel, NULL)))
    {
        path = (GtkTreePath *) rows->data;
        handle_toggle_separator (NULL, path);
        g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);

        handle_selection_changed (sel, NULL);
    }
    return TRUE;
}

static void handle_selection_changed (GtkTreeSelection *sel, gpointer user_data)
{
    GtkTreePath *path;
    GtkTreeIter iter;
    GList *rows;
    MenuCacheItem *cacheitem;
    MenuCacheType type;

    gtk_widget_set_sensitive (edit_btn, FALSE);
    gtk_widget_set_sensitive (up_btn, FALSE);
    gtk_widget_set_sensitive (dn_btn, FALSE);
    gtk_widget_set_sensitive (root_btn, FALSE);
    gtk_widget_set_sensitive (sep_btn, FALSE);

    rows = gtk_tree_selection_get_selected_rows (sel, NULL);
    if (rows)
    {
        path = (GtkTreePath *) rows->data;
        gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
        gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, ITEM_POINTER, &cacheitem, ITEM_TYPE, &type, -1);

        if (type != MENU_CACHE_TYPE_SEP)
        {
            gtk_widget_set_sensitive (edit_btn, TRUE);

            if (gtk_tree_path_get_depth (path) == 2)
                gtk_widget_set_sensitive (root_btn, TRUE);
        }

        if (gtk_tree_model_iter_previous (GTK_TREE_MODEL (store), &iter))
            gtk_widget_set_sensitive (up_btn, TRUE);

        gtk_tree_model_get_iter (GTK_TREE_MODEL (store), &iter, path);
        if (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter))
        {
            gtk_widget_set_sensitive (dn_btn, TRUE);
            gtk_widget_set_sensitive (sep_btn, TRUE);
        }

        g_list_free_full (rows, (GDestroyNotify) gtk_tree_path_free);
    }
}

static void init_main_window (void)
{
    GtkCellRenderer *renderer;
    GtkTreeModelFilter *cat_filter;

    // delete the cache first to force it to update - it makes life so much easier...
    delete_cache ();
    dialog_reload = DIALOG_NOT_OPEN;

    // read menu prefix and get system and local filenames
    sysmenufile = g_strdup_printf ("/etc/xdg/menus/%sapplications.menu", getenv ("XDG_MENU_PREFIX"));
    usermenufile = g_strdup_printf ("%s/menus/%sapplications.menu", g_get_user_config_dir (), getenv ("XDG_MENU_PREFIX"));

    store = gtk_tree_store_new (8, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER, G_TYPE_INT, G_TYPE_BOOLEAN, G_TYPE_STRING);

    new_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_new");
    edit_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_edit");
    up_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_up");
    dn_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_down");
    root_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_root");
    sep_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_sep");
    menu_tv = (GtkWidget *) gtk_builder_get_object (builder, "tv_menu");
    scroll = (GtkWidget *) gtk_builder_get_object (builder, "scroll");

    scale = gtk_widget_get_scale_factor (main_dlg);

    // setup handlers
    g_signal_connect (new_btn, "clicked", G_CALLBACK (handle_new_button), NULL);
    g_signal_connect (edit_btn, "clicked", G_CALLBACK (handle_edit_button), NULL);
    g_signal_connect (up_btn, "clicked", G_CALLBACK (handle_up_button), NULL);
    g_signal_connect (dn_btn, "clicked", G_CALLBACK (handle_down_button), NULL);
    g_signal_connect (root_btn, "clicked", G_CALLBACK (handle_root_button), NULL);
    g_signal_connect (sep_btn, "clicked", G_CALLBACK (handle_sep_button), NULL);
    g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (menu_tv)), "changed", G_CALLBACK (handle_selection_changed), NULL);
    g_signal_connect (menu_tv, "button-press-event", G_CALLBACK (handle_tv_button_press), NULL);
    g_signal_connect (menu_tv, "size-allocate", G_CALLBACK (set_scroll), NULL);
    
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

    load_menu (NULL, NULL);

    cat_filter = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (GTK_TREE_MODEL (store), NULL));
    gtk_tree_model_filter_set_visible_func (cat_filter, (GtkTreeModelFilterVisibleFunc) only_dirs, NULL, NULL);

    categories = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (cat_filter)));
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (categories), ITEM_NAME, GTK_SORT_ASCENDING);

    // set up long press
    GtkGesture *gesture = gtk_gesture_long_press_new (menu_tv);
    gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (gesture), FALSE);
    g_signal_connect (gesture, "pressed", G_CALLBACK (gesture_pressed), NULL);
    g_signal_connect (gesture, "end", G_CALLBACK (gesture_end), NULL);
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_TARGET);
    pressed = FALSE;
}

static void gesture_pressed (GtkGestureLongPress *, gdouble x, gdouble y, gpointer)
{
    pressed = TRUE;
    press_x = x;
    press_y = y;
}

static void gesture_end (GtkGestureLongPress *, GdkEventSequence *, gpointer)
{
    if (pressed)
    {
        create_popup_menu (press_x, press_y);
        pressed = FALSE;
    }
}

/*----------------------------------------------------------------------------*/
/* Plugin interface */
/*----------------------------------------------------------------------------*/

#ifdef PLUGIN_NAME

void init_plugin (GtkWidget *parent)
{
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    main_dlg = parent;
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/ui/merp.ui");

    init_main_window ();
}

int plugin_tabs (void)
{
    return 1;
}

const char *tab_name (int tab)
{
    switch (tab)
    {
        case 0 : return C_("tab", "Main Menu");
        default : return _("No such tab");
    }
}

const char *icon_name (int tab)
{
    switch (tab)
    {
        case 0 : return "alacarte";
        default : return NULL;
    }
}

const char *tab_id (int tab)
{
    switch (tab)
    {
        case 0 : return ("main_menu");
        default : return NULL;
    }
}

GtkWidget *get_tab (int tab)
{
    GtkWidget *window, *plugin;

    window = (GtkWidget *) gtk_builder_get_object (builder, "vbox1");
    switch (tab)
    {
        case 0 :
            plugin = (GtkWidget *) gtk_builder_get_object (builder, "hbox1");
            break;
        default :
            plugin = NULL;
    }

    gtk_container_remove (GTK_CONTAINER (window), plugin);

    return plugin;
}

gboolean reboot_needed (void)
{
    return FALSE;
}

void free_plugin (void)
{
    g_object_unref (builder);
    menu_cache_remove_reload_notify (menu_cache, id);
    menu_cache_unref (menu_cache);
}

void on_menu_edit (char *id)
{
    MenuCacheItem *item = menu_cache_find_item_by_id (menu_cache, id);
    if (item) show_properties_dialog (item);
}

#else

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
    GtkWidget *close_btn;

    // setup localisation
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    // setup GTK
    gtk_init (&argc, &argv);

    // build the UI
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/ui/merp.ui");
    main_dlg = (GtkWidget *) gtk_builder_get_object (builder, "main_window");
    close_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_ok");

    g_signal_connect (main_dlg, "delete_event", G_CALLBACK (close_prog), NULL);
    g_signal_connect (close_btn, "clicked", G_CALLBACK (close_prog), NULL);

    init_main_window ();

    gtk_widget_show_all (main_dlg);

    gtk_main ();

    gtk_widget_destroy (main_dlg);

    menu_cache_remove_reload_notify (menu_cache, id);
    menu_cache_unref (menu_cache);

    return 0;
}

#endif

/* End of file                                                                */
/*----------------------------------------------------------------------------*/
