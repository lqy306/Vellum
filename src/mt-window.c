/*
 * mt-window.c
 * 以 Adwaita 标题栏、标签和 Toast 组织极简编辑器的主要交互。
 */

#include "mt-window-private.h"
#include "mt-overview-map.h"
#include "mt-vut-package.h"
#include "mt-plugin-manager.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <pango/pango.h>
#include <stdarg.h>
#include <string.h>

#ifndef VELLUM_VERSION
#define VELLUM_VERSION "0"
#endif

#define MT_DOCUMENT_DATA_KEY "vellum-document"
#define MT_SNAPSHOT_DELAY_SECONDS 8
/* 主窗口最小尺寸：低于该宽度时布局会错位，禁止继续缩小。 */
#define MT_MIN_WINDOW_WIDTH 800
#define MT_MIN_WINDOW_HEIGHT 480
#define MT_SCHEME_PENDING_KEY "vellum-scheme-pending"
/* 超过该字符数的后台标签延迟到被选中时再换配色，避免为不可见的大文档烧 CPU。 */
#define MT_SCHEME_LAZY_LIMIT 300000
/* 概览地图在换配色时会全量重绘整篇文档；超过该字符数时保持隐藏，避免换主题卡死。 */
#define MT_MAP_RENDER_LIMIT 300000
/* 与参考应用一致：概览仅作导航，不占据过多编辑宽度。 */
#define MT_OVERVIEW_MAP_WIDTH 104
/* 低于此内容宽度时，属性检查器不再挤压编辑器，而是作为覆盖式抽屉出现。 */
#define MT_AUXILIARY_DRAWER_BREAKPOINT 900

/* 设置 VELLUM_DEBUG_TIMING=1 可在终端观察主题/配色切换各阶段耗时。 */
static void
mt_timing_log(const gchar *format, ...)
{
    va_list args;

    if (g_getenv("VELLUM_DEBUG_TIMING") == NULL)
    {
        return;
    }
    g_print("[vellum-timing] ");
    va_start(args, format);
    g_vprintf(format, args);
    va_end(args);
    g_print("\n");
}

static void mt_timing_log_duration(const gchar *label, gint64 start)
{
    mt_timing_log("%s: %lld ms", label,
                  (long long)((g_get_monotonic_time() - start) / 1000));
}

typedef struct _MtFileRequest MtFileRequest;
typedef struct _MtCloseRequest MtCloseRequest;
typedef struct _MtFontRequest MtFontRequest;
typedef struct _MtExtensionFileRequest MtExtensionFileRequest;
typedef struct _MtExtensionDeleteRequest MtExtensionDeleteRequest;
typedef struct _MtThemeChooser MtThemeChooser;
typedef struct _MtPropertiesPanel MtPropertiesPanel;
typedef struct _MtAutoUpdateCheck MtAutoUpdateCheck;

struct _MtAutoUpdateCheck
{
    MtWindow *window;
    SoupSession *session;
    SoupMessage *message;
    GCancellable *cancellable;
};

static void mt_window_auto_update_check_start(MtWindow *window);
static gboolean mt_window_auto_update_check_idle(gpointer user_data);
static void mt_auto_update_check_free(MtAutoUpdateCheck *check);

struct _MtFileRequest
{
    MtWindow *window;
    MtDocument *document;
    AdwTabPage *page;
    GtkFileDialog *dialog;
};

struct _MtCloseRequest
{
    MtWindow *window;
    MtDocument *document;
    AdwTabPage *page;
};

struct _MtFontRequest
{
    MtWindow *window;
    AdwActionRow *row;
    GtkFontDialog *dialog;
};

struct _MtExtensionFileRequest
{
    MtWindow *window;
    GtkFileDialog *dialog;
    guint index;
    gboolean importing;
};

struct _MtExtensionDeleteRequest
{
    MtWindow *window;
    GtkWidget *extensions_window;
    guint index;
};

struct _MtThemeChooser
{
    MtWindow *window;
    AdwActionRow *row;
    GtkFlowBox *flow_box;
    GtkWindow *dialog;
};

typedef struct _MtPropertiesItem
{
    gchar *name;
    guint index;
} MtPropertiesItem;

static void
mt_properties_item_free(gpointer data)
{
    MtPropertiesItem *item;

    item = data;
    if (item != NULL)
    {
        g_free(item->name);
        g_free(item);
    }
}

typedef struct _MtPropertiesPanel
{
    MtWindow *window;
    GtkWidget *page;
    AdwActionRow *name_row;
    AdwActionRow *location_row;
    AdwActionRow *type_row;
    GtkWidget *type_button;
    AdwComboRow *encoding_row;
    GtkStringList *encoding_model;
    AdwComboRow *newline_row;
    GtkStringList *newline_model;
    /* 与弹窗选择器并列的语言 id / 编码对象数组。 */
    GPtrArray *type_language_ids;
    GPtrArray *type_items;
    GPtrArray *encoding_items;
    GPtrArray *encoding_picker_items;
    guint type_selected;
    guint encoding_selected;
    /* 元数据回写控件时抑制选择回调，避免刷新被误作格式转换或重新加载。 */
    gboolean refreshing_metadata;
    AdwSwitchRow *auto_indent_row;
    AdwComboRow *indent_char_row;
    AdwSpinRow *tab_width_row;
    AdwSpinRow *indent_width_row;
    AdwActionRow *lines_row;
    AdwActionRow *words_row;
    AdwActionRow *chars_row;
    AdwActionRow *all_chars_row;
    GtkStringList *indent_model;
} MtPropertiesPanel;

/* 构建树直接运行时，把源码里的 data/icons/hicolor 加入图标主题搜索路径，
 * 让窗口图标（用户提供的 SVG）在未安装的情况下也能被解析到。
 * 从可执行文件所在目录逐级向上找包含 data/icons/hicolor 的源码根。 */
static void
mt_window_register_source_icon_path(AdwApplicationWindow *window)
{
    GtkIconTheme *theme;
    gchar *exe_path;
    gchar *dir;
    gchar *icon_dir;

    exe_path = g_file_read_link("/proc/self/exe", NULL);
    if (exe_path == NULL)
    {
        return;
    }
    dir = g_path_get_dirname(exe_path);
    g_free(exe_path);
    while (dir != NULL)
    {
        gchar *parent;

        icon_dir = g_build_filename(dir, "data", "icons", "hicolor", NULL);
        if (g_file_test(icon_dir, G_FILE_TEST_IS_DIR))
        {
            theme = gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(window)));
            gtk_icon_theme_add_search_path(theme, icon_dir);
            g_free(icon_dir);
            break;
        }
        g_free(icon_dir);

        parent = g_path_get_dirname(dir);
        if (g_strcmp0(parent, dir) == 0)
        {
            g_free(parent);
            break;
        }
        g_free(dir);
        dir = parent;
    }
    g_free(dir);
}

typedef struct _MtSchemeApply
{
    MtWindow *window;
    guint source_id;
    gboolean maps_hidden;
} MtSchemeApply;

static void mt_window_add_document(MtWindow *window, MtDocument *document);
static void mt_window_update_document_title(MtDocument *document);
static void mt_window_update_header_title(MtWindow *window);
static void mt_window_apply_editor_style(MtWindow *window);
static void mt_window_apply_source_scheme(MtWindow *window);
static void mt_window_update_menu_theme_buttons(MtWindow *window);
void mt_window_update_menu_zoom_label(MtWindow *window);
void mt_window_apply_editor_preferences(MtWindow *window);
static void mt_window_update_language_label(MtWindow *window);
void mt_window_show_toast(MtWindow *window, const gchar *message);
static void mt_window_schedule_language_update(MtWindow *window);
static void mt_window_update_position(MtWindow *window);
static void mt_window_find_next(MtWindow *window, gboolean backwards);
static void mt_window_set_replace_mode(MtWindow *window, gboolean replace_mode);
static void mt_window_show_find_bar(MtWindow *window, gboolean replace_mode);
static void mt_window_destroyed(GtkWidget *widget, gpointer user_data);
static void mt_window_monitor_document(MtWindow *window, MtDocument *document);
static void mt_window_document_load_finished(GObject *source,
                                             GAsyncResult *result,
                                             gpointer user_data);
static void mt_window_save_and_close_finished(GObject *source,
                                              GAsyncResult *result,
                                              gpointer user_data);
static PangoAttrList *mt_window_statusbar_attrs(void);
static void mt_window_properties_refresh(MtWindow *window);
static void mt_window_properties_refresh_metadata(MtPropertiesPanel *panel);
static void mt_window_properties_schedule_refresh(MtWindow *window);
static void mt_window_properties_type_picked(MtPropertiesPanel *panel,
                                              guint index,
                                              gpointer user_data);
static void mt_window_properties_encoding_picked(MtPropertiesPanel *panel,
                                                 guint index,
                                                 gpointer user_data);
static void mt_window_properties_encoding_changed(GObject *object,
                                                  GParamSpec *pspec,
                                                  gpointer user_data);
static void mt_window_properties_newline_changed(GObject *object,
                                                 GParamSpec *pspec,
                                                 gpointer user_data);
static void mt_window_properties_convert_newlines(MtPropertiesPanel *panel,
                                                  GtkSourceNewlineType newline);

static MtFileRequest *
mt_file_request_new(MtWindow *window,
                    MtDocument *document,
                    GtkFileDialog *dialog)
{
    MtFileRequest *request;

    request = g_new0(MtFileRequest, 1);
    request->window = window;
    request->document = document;
    request->dialog = dialog != NULL ? g_object_ref(dialog) : NULL;

    if (document != NULL && document->page != NULL)
    {
        request->page = g_object_ref(document->page);
    }

    return request;
}

static void
mt_file_request_free(MtFileRequest *request)
{
    if (request == NULL)
    {
        return;
    }

    g_clear_object(&request->page);
    g_clear_object(&request->dialog);
    g_free(request);
}

static MtCloseRequest *
mt_close_request_new(MtWindow *window, MtDocument *document, AdwTabPage *page)
{
    MtCloseRequest *request;

    request = g_new0(MtCloseRequest, 1);
    request->window = window;
    request->document = document;
    request->page = g_object_ref(page);

    return request;
}

static void
mt_close_request_free(MtCloseRequest *request)
{
    if (request == NULL)
    {
        return;
    }

    g_clear_object(&request->page);
    g_free(request);
}

static MtFontRequest *
mt_font_request_new(MtWindow *window, AdwActionRow *row, GtkFontDialog *dialog)
{
    MtFontRequest *request;

    request = g_new0(MtFontRequest, 1);
    request->window = window;
    request->row = g_object_ref(row);
    request->dialog = g_object_ref(dialog);

    return request;
}

static void
mt_font_request_free(MtFontRequest *request)
{
    if (request == NULL)
    {
        return;
    }

    g_clear_object(&request->row);
    g_clear_object(&request->dialog);
    g_free(request);
}

static void
mt_window_font_dialog_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtFontRequest *request;
    PangoFontDescription *description;
    GError *error;

    request = user_data;
    error = NULL;
    description = gtk_font_dialog_choose_font_finish(GTK_FONT_DIALOG(source), result, &error);

    if (description != NULL)
    {
        const gchar *family;
        gchar *family_copy;
        gchar *message;

        family = pango_font_description_get_family(description);
        family_copy = g_strdup(family != NULL ? family : "Monospace");
        mt_settings_set_font_family(request->window->settings, family_copy);
        adw_action_row_set_subtitle(request->row, family_copy);
        mt_window_apply_editor_style(request->window);
        mt_settings_save(request->window->settings);
        message = g_strdup_printf(_("Editor font set to %s"), family_copy);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_free(family_copy);
        pango_font_description_free(description);
    }
    else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to select font: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
    }

    g_clear_error(&error);
    mt_font_request_free(request);
}

void
mt_window_choose_font_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    AdwActionRow *row;
    GtkFontDialog *dialog;
    PangoFontDescription *initial_font;
    MtFontRequest *request;

    window = user_data;
    row = g_object_get_data(G_OBJECT(button), "vellum-font-row");
    dialog = gtk_font_dialog_new();
    gtk_font_dialog_set_title(dialog, _("Choose Editor Font"));
    gtk_font_dialog_set_modal(dialog, TRUE);
    initial_font = pango_font_description_from_string(mt_settings_get_font_family(window->settings));
    request = mt_font_request_new(window, row, dialog);
    {
        GtkWidget *dialog_parent;

        dialog_parent = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
        if (!GTK_IS_WINDOW(dialog_parent))
        {
            dialog_parent = GTK_WIDGET(window->window);
        }
        gtk_font_dialog_choose_font(dialog,
                                    GTK_WINDOW(dialog_parent),
                                    initial_font,
                                    NULL,
                                    mt_window_font_dialog_finished,
                                    request);
    }
    pango_font_description_free(initial_font);
    g_object_unref(dialog);
}

static MtExtensionFileRequest *
mt_extension_file_request_new(MtWindow *window,
                              GtkFileDialog *dialog,
                              guint index,
                              gboolean importing)
{
    MtExtensionFileRequest *request;

    request = g_new0(MtExtensionFileRequest, 1);
    request->window = window;
    request->dialog = g_object_ref(dialog);
    request->index = index;
    request->importing = importing;

    return request;
}

static void
mt_extension_file_request_free(MtExtensionFileRequest *request)
{
    if (request == NULL)
    {
        return;
    }

    g_clear_object(&request->dialog);
    g_free(request);
}

static void
mt_window_extension_file_finished(GObject *source,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
    MtExtensionFileRequest *request;
    MtPluginManager *manager;
    GFile *file;
    GError *error;
    gboolean success;

    request = user_data;
    manager = request->window->plugin_manager;
    error = NULL;
    success = FALSE;

    if (request->importing)
    {
        file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
    }
    else
    {
        file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
    }

    if (file != NULL && manager != NULL)
    {
        if (request->importing)
        {
            success = mt_plugin_manager_import(manager, file, &error);
        }
        else
        {
            success = mt_plugin_manager_export(manager, request->index, file, &error);
        }
        g_object_unref(file);
    }

    if (success)
    {
        mt_window_show_toast(request->window,
                             request->importing ?
                             _("Extension imported. Reopen Extensions to refresh the list.") :
                             _("Extension exported"));
    }
    else if (error != NULL && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        gchar *message;

        message = g_strdup_printf(_("Extension operation failed: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
    }

    g_clear_error(&error);
    mt_extension_file_request_free(request);
}

static void
mt_window_extension_import_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    GtkFileDialog *dialog;
    MtExtensionFileRequest *request;
    GtkFileFilter *filter;
    GListStore *filters;

    (void)button;

    window = user_data;
    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Import Extension"));
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Vellum Extension Package (.vut)"));
    gtk_file_filter_add_pattern(filter, "*.vut");
    filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);
    g_object_unref(filter);
    request = mt_extension_file_request_new(window, dialog, 0, TRUE);
    gtk_file_dialog_open(dialog,
                         GTK_WINDOW(window->window),
                         NULL,
                         mt_window_extension_file_finished,
                         request);
    g_object_unref(dialog);
}

static void
mt_window_extension_export_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    GtkFileDialog *dialog;
    MtExtensionFileRequest *request;
    guint index;
    const gchar *path;
    gchar *basename;
    gchar *stem;
    gchar *package_name;

    window = user_data;
    manager = window->plugin_manager;
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "vellum-extension-index"));
    path = manager != NULL ? mt_plugin_manager_get_path(manager, index) : NULL;
    if (path == NULL)
    {
        return;
    }

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Export Extension"));
    basename = g_path_get_basename(path);
    stem = g_strdup(basename);
    if (g_str_has_suffix(stem, "." G_MODULE_SUFFIX))
    {
        stem[strlen(stem) - strlen("." G_MODULE_SUFFIX)] = '\0';
    }
    package_name = g_strdup_printf("%s-%s-%s.vut",
                                   stem,
                                   mt_vut_package_get_host_os(),
                                   mt_vut_package_get_host_architecture());
    gtk_file_dialog_set_initial_name(dialog, package_name);
    g_free(package_name);
    g_free(stem);
    g_free(basename);
    request = mt_extension_file_request_new(window, dialog, index, FALSE);
    gtk_file_dialog_save(dialog,
                         GTK_WINDOW(window->window),
                         NULL,
                         mt_window_extension_file_finished,
                         request);
    g_object_unref(dialog);
}

static void
mt_window_extension_delete_response(AdwMessageDialog *dialog,
                                    const gchar *response,
                                    gpointer user_data)
{
    MtExtensionDeleteRequest *request;
    MtPluginManager *manager;
    GError *error;

    request = user_data;
    manager = request->window->plugin_manager;

    if (g_strcmp0(response, "delete") == 0 && manager != NULL)
    {
        error = NULL;
        if (mt_plugin_manager_remove(manager, request->index, &error))
        {
            mt_window_show_toast(request->window,
                                 _("Extension removed"));
            mt_window_sync_plugin_menu(request->window);
        }
        else
        {
            gchar *message;

            message = g_strdup_printf(_("Unable to delete extension: %s"), error->message);
            mt_window_show_toast(request->window, message);
            g_free(message);
            g_clear_error(&error);
        }
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
    if (request->extensions_window != NULL && GTK_IS_WIDGET(request->extensions_window))
    {
        gtk_window_destroy(GTK_WINDOW(request->extensions_window));
        request->extensions_window = NULL;
        mt_window_action_extensions(NULL, NULL, request->window);
    }
    g_free(request);
}

static void
mt_window_extension_configure_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    guint index;

    window = user_data;
    manager = window->plugin_manager;
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "vellum-extension-index"));
    if (manager != NULL)
    {
        mt_plugin_manager_configure(manager, index, GTK_WINDOW(window->window));
    }
}

static void
mt_window_extension_delete_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    MtExtensionDeleteRequest *request;
    AdwMessageDialog *dialog;
    guint index;

    window = user_data;
    request = g_new0(MtExtensionDeleteRequest, 1);
    request->window = window;
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "vellum-extension-index"));
    request->index = index;
    request->extensions_window = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button)));
    manager = window->plugin_manager;
    dialog = ADW_MESSAGE_DIALOG(adw_message_dialog_new(GTK_WINDOW(window->window),
                                                        _("Delete extension?"),
                                                        manager != NULL &&
                                                        mt_plugin_manager_is_user_managed(manager, index) ?
                                                        _("The extension file will be deleted from disk.") :
                                                        _("This built-in extension will be hidden and unloaded. It cannot be restored from the application.")));
    adw_message_dialog_add_response(dialog, "cancel", _("Cancel"));
    adw_message_dialog_add_response(dialog, "delete", _("Delete"));
    adw_message_dialog_set_response_appearance(dialog, "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_message_dialog_set_default_response(dialog, "delete");
    adw_message_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog,
                     "response",
                     G_CALLBACK(mt_window_extension_delete_response),
                     request);
    gtk_window_present(GTK_WINDOW(dialog));
}

static gboolean
mt_window_is_dark(MtWindow *window)
{
    if (mt_settings_get_appearance(window->settings) == MT_APPEARANCE_DARK)
    {
        return TRUE;
    }

    if (mt_settings_get_appearance(window->settings) == MT_APPEARANCE_LIGHT)
    {
        return FALSE;
    }

    return adw_style_manager_get_dark(adw_style_manager_get_default());
}

static const gchar *
mt_window_get_source_scheme_id(MtWindow *window)
{
    const gchar *style_scheme;
    gboolean dark;

    style_scheme = mt_settings_get_style_scheme(window->settings);
    dark = mt_window_is_dark(window);

    if (g_strcmp0(style_scheme, "adwaita") == 0)
    {
        return dark ? "Adwaita-dark" : "Adwaita";
    }
    if (g_strcmp0(style_scheme, "classic") == 0)
    {
        return dark ? "classic-dark" : "classic";
    }
    if (g_strcmp0(style_scheme, "cobalt") == 0)
    {
        return dark ? "cobalt" : "cobalt-light";
    }
    if (g_strcmp0(style_scheme, "kate") == 0)
    {
        return dark ? "kate-dark" : "kate";
    }
    if (g_strcmp0(style_scheme, "oblivion") == 0)
    {
        return "oblivion";
    }
    if (g_strcmp0(style_scheme, "solarized") == 0)
    {
        return dark ? "solarized-dark" : "solarized-light";
    }
    if (g_strcmp0(style_scheme, "tango") == 0)
    {
        return "tango";
    }

    return dark ? "Adwaita-dark" : "Adwaita";
}

static void
mt_scheme_apply_restore_maps(MtWindow *window)
{
    gint count;
    gint index;
    gboolean skipped;
    gint64 start;

    if (window == NULL || window->tab_view == NULL)
    {
        return;
    }

    start = g_get_monotonic_time();
    skipped = FALSE;
    count = adw_tab_view_get_n_pages(window->tab_view);
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document != NULL && document->overview != NULL)
        {
            GtkTextBuffer *buffer;

            buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
            if (gtk_text_buffer_get_char_count(buffer) > MT_MAP_RENDER_LIMIT)
            {
                /* 大文档的概览地图在换配色后会对整篇重绘，可能卡死几秒到几十秒；
                 * 保持隐藏，用户可在首选项里手动重新打开。 */
                gtk_widget_set_visible(document->overview, FALSE);
                skipped = TRUE;
            }
            else
            {
                gtk_widget_set_visible(document->overview,
                                       mt_settings_get_show_overview(window->settings));
            }
        }
    }
    if (skipped)
    {
        mt_window_show_toast(window,
                             _("Overview map kept hidden for a large document"));
    }
    mt_timing_log_duration("restore-maps", start);
}

static void
mt_scheme_apply_cancel(MtWindow *window)
{
    MtSchemeApply *state;

    if (window == NULL || window->scheme_apply == NULL)
    {
        return;
    }

    state = window->scheme_apply;
    if (state->source_id != 0)
    {
        g_source_remove(state->source_id);
        state->source_id = 0;
    }
    if (state->maps_hidden)
    {
        mt_scheme_apply_restore_maps(window);
    }
    g_free(state);
    window->scheme_apply = NULL;
}

static gboolean
mt_scheme_apply_tick(gpointer user_data)
{
    MtWindow *window;
    MtSchemeApply *state;
    GtkSourceStyleSchemeManager *manager;
    GtkSourceStyleScheme *scheme;
    const gchar *scheme_id;
    gboolean applied;
    gboolean more;
    gint count;
    gint index;

    window = user_data;
    if (window == NULL || window->disposed || window->scheme_apply == NULL)
    {
        if (window != NULL)
        {
            mt_scheme_apply_cancel(window);
        }
        return G_SOURCE_REMOVE;
    }

    state = window->scheme_apply;
    state->source_id = 0;

    /* 每次分片重新解析目标配色，点击多张卡时自然合并为同一批次。 */
    manager = gtk_source_style_scheme_manager_get_default();
    scheme_id = mt_window_get_source_scheme_id(window);
    scheme = gtk_source_style_scheme_manager_get_scheme(manager, scheme_id);
    if (scheme == NULL)
    {
        scheme = gtk_source_style_scheme_manager_get_scheme(manager,
                                                            mt_window_is_dark(window) ? "Adwaita-dark" : "Adwaita");
    }
    if (scheme == NULL)
    {
        mt_scheme_apply_cancel(window);
        return G_SOURCE_REMOVE;
    }

    applied = FALSE;
    more = FALSE;
    count = adw_tab_view_get_n_pages(window->tab_view);
    mt_timing_log("scheme-tick start (scheme %s)", scheme_id);
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;
        GtkSourceBuffer *buffer;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document == NULL)
        {
            continue;
        }

        buffer = mt_document_get_buffer(document);
        if (gtk_source_buffer_get_style_scheme(buffer) != scheme)
        {
            if (gtk_text_buffer_get_char_count(GTK_TEXT_BUFFER(buffer)) > MT_SCHEME_LAZY_LIMIT)
            {
                /* 超大后台标签：打上待应用标记，切到该标签时再换配色。 */
                g_object_set_data(G_OBJECT(buffer), MT_SCHEME_PENDING_KEY, GINT_TO_POINTER(1));
            }
            else if (!applied)
            {
                /* 每个空闲分片只应用一个标签，主循环在标签之间仍处理输入与绘制。 */
                gtk_source_buffer_set_style_scheme(buffer, scheme);
                applied = TRUE;
            }
            else
            {
                more = TRUE;
            }
        }
    }

    if (more)
    {
        state->source_id = g_idle_add(mt_scheme_apply_tick, window);
        mt_timing_log("scheme-tick done, more work");
        return G_SOURCE_REMOVE;
    }

    mt_timing_log("scheme-tick done, batch finished");
    mt_scheme_apply_cancel(window);
    return G_SOURCE_REMOVE;
}

static void
mt_window_apply_source_scheme(MtWindow *window)
{
    GtkSourceStyleSchemeManager *manager;
    GtkSourceStyleScheme *scheme;
    const gchar *scheme_id;
    MtSchemeApply *state;
    MtDocument *current;
    gboolean need_work;
    gint count;
    gint index;

    if (window == NULL || window->disposed || window->tab_view == NULL)
    {
        return;
    }

    manager = gtk_source_style_scheme_manager_get_default();
    scheme_id = mt_window_get_source_scheme_id(window);
    scheme = gtk_source_style_scheme_manager_get_scheme(manager, scheme_id);
    if (scheme == NULL)
    {
        scheme = gtk_source_style_scheme_manager_get_scheme(manager,
                                                            mt_window_is_dark(window) ? "Adwaita-dark" : "Adwaita");
    }
    if (scheme == NULL)
    {
        return;
    }

    if (window->scheme_apply != NULL)
    {
        mt_scheme_apply_cancel(window);
    }

    count = adw_tab_view_get_n_pages(window->tab_view);
    need_work = FALSE;
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document != NULL &&
            gtk_source_buffer_get_style_scheme(mt_document_get_buffer(document)) != scheme)
        {
            need_work = TRUE;
            break;
        }
    }
    /* 配色未变时直接返回，避免重复的全量重高亮。 */
    if (!need_work)
    {
        mt_timing_log("apply-source-scheme: no work (scheme %s)", scheme_id);
        return;
    }
    mt_timing_log("apply-source-scheme: scheme=%s pages=%d", scheme_id, count);

    state = g_new0(MtSchemeApply, 1);
    state->window = window;

    /* 先隐藏概览地图：换配色时 GtkSourceView 会对整篇文档重算高亮，
     * 地图若保持可见会跟着每帧全量重绘，大文件下直接卡死。 */
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document != NULL && document->overview != NULL &&
            gtk_widget_get_visible(document->overview))
        {
            gtk_widget_set_visible(document->overview, FALSE);
            state->maps_hidden = TRUE;
        }
    }

    /* 当前标签立即应用，其余标签按空闲分片处理，界面保持响应。 */
    current = mt_window_get_current_document(window);
    if (current != NULL &&
        gtk_source_buffer_get_style_scheme(mt_document_get_buffer(current)) != scheme)
    {
        gtk_source_buffer_set_style_scheme(mt_document_get_buffer(current), scheme);
    }

    window->scheme_apply = state;
    state->source_id = g_idle_add(mt_scheme_apply_tick, window);
}

void
mt_window_apply_editor_preferences(MtWindow *window)
{
    gint count;
    gint index;

    count = adw_tab_view_get_n_pages(window->tab_view);
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document != NULL)
        {
            mt_document_apply_editor_settings(document, window->settings);
        }
    }
}

/* 与 GNOME Text Editor 状态栏一致：等宽数字、表格数字特性。 */
static PangoAttrList *
mt_window_statusbar_attrs(void)
{
    PangoAttrList *attrs;

    attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_font_features_new("tnum"));
    return attrs;
}

static void
mt_window_apply_editor_style(MtWindow *window)
{
    gchar *css;
    const gchar *family;
    gdouble scale;
    gint64 start;
    start = g_get_monotonic_time();
    family = mt_settings_get_custom_font(window->settings) ?
             mt_settings_get_font_family(window->settings) : "Monospace";
    scale = mt_settings_get_font_scale(window->settings);

    /* 编辑器表面与 token 色完全交给 GtkSourceView 当前选择的方案。 */
    css = g_strdup_printf(
        "textview.vellum-editor { font-family: %s; font-size: %.2fpt; } "
        "box.vellum-statusbar { font-family: monospace; min-width: 100px; padding: 3px 12px; border-top: 1px solid @borders; } "
        "button.vellum-theme-card { padding: 0; border-radius: 12px; border: 1px solid alpha(@borders, .70); background: alpha(@theme_bg_color, .45); } "
        "button.vellum-theme-card:hover { border-color: alpha(@accent_bg_color, .72); background: alpha(@accent_bg_color, .08); } "
        "button.vellum-theme-card.vellum-theme-selected { border: 2px solid @accent_bg_color; box-shadow: 0 0 0 2px alpha(@accent_bg_color, .16); } "
        "frame.vellum-theme-preview-frame { border-radius: 7px; border: 1px solid alpha(@borders, .72); } "
        "label.vellum-theme-name { font-weight: 600; } "
        "scrolledwindow.vellum-properties-drawer { border-left: 1px solid alpha(@borders, .82); background: @sidebar_bg_color; } "
        "preferencespage.vellum-properties { background: @sidebar_bg_color; } "
        "widget.vellum-overview-map { min-width: 104px; border-left: 1px solid alpha(@borders, .70); background-color: alpha(@view_bg_color, .88); padding-top: 5px; padding-bottom: 5px; } "
        "widget.vellum-overview-map slider, widget.vellum-overview-map selection { background-color: alpha(@accent_bg_color, .30); border: 1px solid alpha(@accent_bg_color, .72); } "
        "label.vellum-inline-completion { opacity: .58; color: alpha(@theme_fg_color, .82); font-family: monospace; font: inherit; } "
         "box.vellum-header-title { min-width: 0; } "
         "box.vellum-header-title label { min-width: 0; } "
         "label.vellum-header-modified { color: @accent_color; font-weight: 700; } "
         "grid.searchbar button.flat { min-width: 24px; min-height: 24px; margin: 0; padding: 3px; } "
         "grid.searchbar button.flat.circular { border-radius: 9999px; } "
         "box.vellum-external-change { min-height: 68px; background: linear-gradient(to bottom, #9a7114, #76530d); border-top: 1px solid alpha(#f6d32d, .34); border-bottom: 1px solid alpha(@borders, .82); } "
         "label.vellum-external-title { color: #ffffff; font-weight: 700; font-size: 1.08em; } "
         "label.vellum-external-description { color: alpha(#ffffff, .88); } "
         "box.vellum-external-change button.suggested-action { font-weight: 700; } "
         "row.vellum-destructive label.title { color: @error_color; font-weight: 600; } "
         "row.vellum-destructive label.subtitle { color: alpha(@error_color, .82); } "
         "frame.vellum-onboarding-target { border: 2px solid #e01b24; border-radius: 10px; background: alpha(#e01b24, .06); box-shadow: 0 0 0 4px alpha(#e01b24, .12); } "
         "button.vellum-properties-toggle { min-width: 24px; padding-left: 2px; padding-right: 2px; } "
         "preferencespage.vellum-properties > scrolledwindow > viewport > clamp > box { margin: 4px 8px 8px 8px; border-spacing: 10px; } "
         "stack.vellum-auxiliary { min-width: 0; } "
         "popover button.pill.flat { padding: 0; } "
         "box.vellum-menu-theme { margin: 9px; } "
         "checkbutton.theme-selector { padding: 0; min-width: 44px; min-height: 44px; background-clip: content-box; border-radius: 9999px; box-shadow: inset 0 0 0 1px @borders; } "
         "checkbutton.theme-selector:checked { box-shadow: inset 0 0 0 2px @theme_selected_bg_color; } "
         "checkbutton.theme-selector.follow { background-image: linear-gradient(to bottom right, #f6f5f4 49.9%%, #242424 50.1%%); } "
         "checkbutton.theme-selector.light { background-color: #f6f5f4; } "
         "checkbutton.theme-selector.dark { background-color: #242424; } "
         "checkbutton.theme-selector radio { -gtk-icon-source: none; border: none; background: none; box-shadow: none; min-width: 12px; min-height: 12px; padding: 2px; transform: translate(27px, 14px); } "
         "checkbutton.theme-selector radio:checked { -gtk-icon-source: -gtk-icontheme(\"object-select-symbolic\"); background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; } ",
        family,
        11.0 * scale);

    gtk_css_provider_load_from_string(window->css_provider, css);
    g_free(css);
    mt_timing_log_duration("apply-editor-style", start);
    mt_window_apply_source_scheme(window);
}

static void
mt_window_update_language_label(MtWindow *window)
{
    MtDocument *document;
    GtkSourceLanguage *language;

    if (window == NULL || window->disposed || !GTK_IS_LABEL(window->language_label))
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        gtk_label_set_text(window->language_label, "");
        return;
    }

    language = gtk_source_buffer_get_language(mt_document_get_buffer(document));
    gtk_label_set_text(window->language_label,
                       language != NULL ? gtk_source_language_get_name(language) : _("Plain Text"));
}

static gboolean
mt_window_language_update_cb(gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return G_SOURCE_REMOVE;
    }
    window->language_source_id = 0;
    mt_window_update_language_label(window);

    return G_SOURCE_REMOVE;
}

static void
mt_window_schedule_language_update(MtWindow *window)
{
    if (window->language_source_id != 0)
    {
        g_source_remove(window->language_source_id);
    }

    window->language_source_id = g_timeout_add(520,
                                               mt_window_language_update_cb,
                                               window);
}

static void
mt_window_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;

    window = g_object_get_data(G_OBJECT(buffer), "vellum-window");
    document = user_data;

    if (window != NULL && !window->disposed)
    {
        if (document != NULL && document == window->inline_completion_document)
        {
            mt_window_clear_inline_completion(window);
        }
        mt_window_schedule_language_update(window);
        mt_window_properties_schedule_refresh(window);
    }
}

static guint
mt_window_count_changed_lines(const gchar *text, gint length)
{
    guint lines;
    gint index;

    lines = 1;
    for (index = 0; index < length; index++)
    {
        if (text[index] == '\n')
        {
            lines++;
        }
    }

    return lines;
}

static void
mt_window_notify_document_change(GtkTextBuffer *buffer,
                                 MtDocument *document,
                                 guint changed_lines)
{
    MtWindow *window;

    window = g_object_get_data(G_OBJECT(buffer), "vellum-window");
    if (window == NULL || window->disposed || window->plugin_manager == NULL ||
        document == NULL || document->loader != NULL || document->saving ||
        document != mt_window_get_current_document(window) ||
        gtk_source_buffer_get_language(document->buffer) == NULL)
    {
        return;
    }

    mt_plugin_manager_notify_document_changed((MtPluginManager *)window->plugin_manager,
                                              MAX(changed_lines, 1));
}

static void
mt_window_buffer_inserted(GtkTextBuffer *buffer,
                          GtkTextIter *location,
                          gchar *text,
                          gint length,
                          gpointer user_data)
{
    (void)location;
    mt_window_notify_document_change(buffer,
                                     user_data,
                                     mt_window_count_changed_lines(text, length));
}

static void
mt_window_buffer_deleted(GtkTextBuffer *buffer,
                         GtkTextIter *start,
                         GtkTextIter *end,
                         gpointer user_data)
{
    guint lines;

    lines = (guint)(ABS(gtk_text_iter_get_line(end) - gtk_text_iter_get_line(start)) + 1);
    mt_window_notify_document_change(buffer, user_data, lines);
}

static void
mt_window_update_document_title(MtDocument *document)
{
    gchar *title;

    if (document->page == NULL)
    {
        return;
    }

    title = g_strdup_printf("%s%s",
                            mt_document_is_modified(document) ? "• " : "",
                            mt_document_get_display_name(document));
    adw_tab_page_set_title(document->page, title);
    adw_tab_page_set_needs_attention(document->page, mt_document_is_modified(document));
    g_free(title);
}

/* 保持标题栏的文档身份区与所选标签同步。原版 GNOME Text Editor
 * 将文档名、所在目录和未保存状态放在标题栏中央；Vellum 沿用这一
 * 信息层级，但使用自身的数据模型与控件实现。 */
static void
mt_window_update_header_title(MtWindow *window)
{
    MtDocument *document;
    GFile *file;
    GFile *parent;
    gchar *subtitle;

    if (window == NULL || window->disposed ||
        !GTK_IS_LABEL(window->header_title_label) ||
        !GTK_IS_LABEL(window->header_subtitle_label))
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        gtk_label_set_text(window->header_title_label, "Vellum");
        gtk_label_set_text(window->header_subtitle_label, "");
        if (window->header_modified_indicator != NULL)
        {
            gtk_widget_set_visible(window->header_modified_indicator, FALSE);
        }
        return;
    }

    gtk_label_set_text(window->header_title_label,
                       mt_document_get_display_name(document));
    if (window->header_modified_indicator != NULL)
    {
        gtk_widget_set_visible(window->header_modified_indicator,
                               mt_document_is_modified(document));
    }
    file = mt_document_get_file(document);
    subtitle = NULL;
    if (file == NULL)
    {
        subtitle = g_strdup(_("Draft"));
    }
    else
    {
        parent = g_file_get_parent(file);
        subtitle = parent != NULL ? g_file_get_parse_name(parent) : g_strdup("");
        g_clear_object(&parent);
    }
    gtk_label_set_text(window->header_subtitle_label, subtitle);
    g_free(subtitle);
}

static gboolean
mt_window_snapshot_all(MtWindow *window)
{
    gint count;
    gint index;

    if (window == NULL || window->disposed || !ADW_IS_TAB_VIEW(window->tab_view))
    {
        return FALSE;
    }
    count = adw_tab_view_get_n_pages(window->tab_view);

    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);

        if (document != NULL)
        {
            mt_document_snapshot(document);
        }
    }

    return TRUE;
}

static gboolean
mt_window_snapshot_all_cb(gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_window_snapshot_all(window);

    window->snapshot_source_id = 0;

    return G_SOURCE_REMOVE;
}

static void
mt_window_schedule_snapshot(MtWindow *window)
{
    if (window->snapshot_source_id != 0)
    {
        g_source_remove(window->snapshot_source_id);
    }

    window->snapshot_source_id = g_timeout_add_seconds(MT_SNAPSHOT_DELAY_SECONDS,
                                                        mt_window_snapshot_all_cb,
                                                        window);
}

static void
mt_window_buffer_modified_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    MtDocument *document;
    MtWindow *window;

    document = user_data;
    window = g_object_get_data(G_OBJECT(buffer), "vellum-window");

    mt_window_update_document_title(document);

    if (window != NULL && !window->disposed)
    {
        mt_window_update_header_title(window);
    }

    if (window != NULL && !window->disposed && mt_document_is_modified(document))
    {
        mt_window_schedule_snapshot(window);
    }
}

static void
mt_window_editor_scrolled(GtkAdjustment *adjustment, gpointer user_data)
{
    MtWindow *window;

    (void)adjustment;
    window = user_data;
    if (window != NULL && !window->disposed &&
        window->inline_completion_document != NULL)
    {
        mt_window_clear_inline_completion(window);
    }
}

static void
mt_window_cursor_moved(GtkTextBuffer *buffer,
                       GtkTextIter *new_location,
                       GtkTextMark *mark,
                       gpointer user_data)
{
    MtWindow *window;

    (void)new_location;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }
    if (mark == gtk_text_buffer_get_insert(buffer) &&
        window->inline_completion_document != NULL)
    {
        /* 光标移动表示用户主动导航或编辑，幽灵补全不再与当前位置对应，
         * 必须立即移除，否则会像残影一样挂在旧坐标上。 */
        mt_window_clear_inline_completion(window);
    }
    mt_window_update_position(window);
}

static void
mt_window_update_position(MtWindow *window)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    gchar *position;

    if (window == NULL || window->disposed || !GTK_IS_LABEL(window->position_label))
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        gtk_label_set_text(window->position_label, "");
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    position = g_strdup_printf(_("Ln %d, Col %d"),
                               gtk_text_iter_get_line(&iter) + 1,
                               gtk_text_iter_get_line_offset(&iter) + 1);
    gtk_label_set_text(window->position_label, position);
    g_free(position);
}

static void
mt_window_page_selected(AdwTabView *tab_view,
                        GParamSpec *pspec,
                        gpointer user_data)
{
    MtWindow *window;

    (void)pspec;

    window = user_data;
    if (window == NULL || window->disposed || !ADW_IS_TAB_VIEW(tab_view))
    {
        return;
    }
    if (window->inline_completion_document != mt_window_get_current_document(window))
    {
        mt_window_clear_inline_completion(window);
    }
    mt_window_update_position(window);
    mt_window_update_language_label(window);
    mt_window_update_header_title(window);
    mt_window_properties_refresh(window);
    {
        MtDocument *document;
        GtkSourceBuffer *buffer;

        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            buffer = mt_document_get_buffer(document);
            if (g_object_get_data(G_OBJECT(buffer), MT_SCHEME_PENDING_KEY) != NULL)
            {
                g_object_set_data(G_OBJECT(buffer), MT_SCHEME_PENDING_KEY, NULL);
                mt_window_apply_source_scheme(window);
            }
        }
    }
}

static void
mt_window_hide_external_change(MtWindow *window)
{
    if (window != NULL && !window->disposed && window->external_change_bar != NULL)
    {
        gtk_widget_set_visible(window->external_change_bar, FALSE);
        window->external_change_document = NULL;
    }
}

static void
mt_window_file_monitor_changed(GFileMonitor *monitor,
                               GFile *file,
                               GFile *other_file,
                               GFileMonitorEvent event_type,
                               gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;

    (void)file;
    (void)other_file;
    window = user_data;
    document = g_object_get_data(G_OBJECT(monitor), MT_DOCUMENT_DATA_KEY);
    if (window == NULL || window->disposed || document == NULL ||
        g_get_monotonic_time() < document->monitor_suppress_until)
    {
        return;
    }

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
        event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
        event_type == G_FILE_MONITOR_EVENT_MOVED_IN)
    {
        window->external_change_document = document;
        if (window->external_change_bar != NULL)
        {
            gtk_widget_set_visible(window->external_change_bar, TRUE);
        }
    }
}

static void
mt_window_monitor_document(MtWindow *window, MtDocument *document)
{
    GFile *file;
    GError *error;

    if (window == NULL || window->disposed || document == NULL)
    {
        return;
    }

    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }

    if (document->file_monitor != NULL)
    {
        g_file_monitor_cancel(document->file_monitor);
        g_clear_object(&document->file_monitor);
    }
    error = NULL;
    document->file_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
    if (document->file_monitor == NULL)
    {
        g_clear_error(&error);
        return;
    }
    g_object_set_data(G_OBJECT(document->file_monitor), MT_DOCUMENT_DATA_KEY, document);
    g_signal_connect(document->file_monitor,
                     "changed",
                     G_CALLBACK(mt_window_file_monitor_changed),
                     window);
}

static void
mt_window_external_reload_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GFile *file;
    MtFileRequest *request;

    (void)button;
    window = user_data;
    document = window != NULL ? window->external_change_document : NULL;
    if (document == NULL)
    {
        return;
    }
    file = mt_document_get_file(document);
    if (file == NULL)
    {
        mt_window_hide_external_change(window);
        return;
    }

    document->monitor_suppress_until = g_get_monotonic_time() + 1000000;
    request = mt_file_request_new(window, document, NULL);
    mt_document_load_async(document, file, mt_window_document_load_finished, request);
    mt_window_hide_external_change(window);
    mt_window_show_toast(window, _("Reloading file from disk…"));
}

static void
mt_window_external_change_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    mt_window_hide_external_change(user_data);
}

static void
mt_window_document_load_finished(GObject *source,
                                 GAsyncResult *result,
                                 gpointer user_data)
{
    MtFileRequest *request;
    GError *error;

    (void)source;

    request = user_data;
    error = NULL;

    if (!mt_document_load_finish(request->document, result, &error))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to open file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_clear_error(&error);
    }
    else
    {
        mt_window_update_document_title(request->document);
        mt_window_update_header_title(request->window);
        mt_window_monitor_document(request->window, request->document);
        mt_window_apply_source_scheme(request->window);
        mt_window_update_language_label(request->window);
        mt_window_properties_refresh(request->window);
        mt_window_show_toast(request->window, _("File opened"));
    }

    mt_file_request_free(request);
}

static void
mt_window_document_save_finished(GObject *source,
                                 GAsyncResult *result,
                                 gpointer user_data)
{
    MtFileRequest *request;
    GError *error;

    (void)source;

    request = user_data;
    error = NULL;

    if (!mt_document_save_finish(request->document, result, &error))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_clear_error(&error);
    }
    else
    {
        request->document->monitor_suppress_until = g_get_monotonic_time() + 1000000;
        mt_window_monitor_document(request->window, request->document);
        mt_window_update_document_title(request->document);
        mt_window_update_header_title(request->window);
        mt_window_properties_refresh(request->window);
        mt_window_show_toast(request->window, _("Saved"));
    }

    mt_file_request_free(request);
}

static void
mt_window_open_dialog_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtFileRequest *request;
    GListModel *files;
    GError *error;

    request = user_data;
    error = NULL;
    files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);

    if (files != NULL)
    {
        mt_window_open_files(request->window, files);
        g_object_unref(files);
    }
    else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to select files: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
    }

    g_clear_error(&error);
    mt_file_request_free(request);
}

static void
mt_window_save_dialog_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtFileRequest *request;
    GFile *file;
    GError *error;

    request = user_data;
    error = NULL;
    file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);

    if (file != NULL)
    {
        if (mt_document_is_saving(request->document))
        {
            mt_window_show_toast(request->window, _("Saving is already in progress"));
        }
        else
        {
            MtFileRequest *save_request;

            save_request = mt_file_request_new(request->window, request->document, request->dialog);
            mt_document_save_async(request->document,
                                   file,
                                   mt_window_document_save_finished,
                                   save_request);
            mt_window_show_toast(request->window, _("Saving…"));
        }
        g_object_unref(file);
    }
    else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to choose save location: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
    }

    g_clear_error(&error);
    mt_file_request_free(request);
}

static void
mt_window_close_saved_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtCloseRequest *request;
    GError *error;
    gboolean saved;

    (void)source;

    request = user_data;
    error = NULL;
    saved = mt_document_save_finish(request->document, result, &error);

    if (saved)
    {
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, TRUE);
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, FALSE);
        g_clear_error(&error);
    }

    mt_close_request_free(request);
}

static void
mt_window_close_dialog_response(AdwMessageDialog *dialog,
                                const gchar *response,
                                gpointer user_data)
{
    MtCloseRequest *request;

    request = user_data;

    if (g_strcmp0(response, "discard") == 0)
    {
        /* 丢弃未保存内容时同步删除草稿，避免同一篇草稿每次启动都被恢复。 */
        mt_document_remove_snapshot(request->document);
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, TRUE);
        mt_close_request_free(request);
    }
    else if (g_strcmp0(response, "save") == 0)
    {
        mt_document_save_async(request->document,
                               NULL,
                               mt_window_close_saved_finished,
                               request);
    }
    else
    {
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, FALSE);
        mt_close_request_free(request);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static gboolean
mt_window_close_page_requested(AdwTabView *tab_view,
                               AdwTabPage *page,
                               gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;

    window = user_data;
    document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);

    if (window != NULL && document == window->inline_completion_document)
    {
        mt_window_clear_inline_completion(window);
    }

    if (document == NULL || !mt_document_is_modified(document))
    {
        adw_tab_view_close_page_finish(tab_view, page, TRUE);
        return TRUE;
    }

    {
        AdwMessageDialog *dialog;
        MtCloseRequest *request;

        dialog = ADW_MESSAGE_DIALOG(adw_message_dialog_new(GTK_WINDOW(window->window),
                                                            _("Save changes?"),
                                                            _("This document has unsaved changes.")));
        adw_message_dialog_add_response(dialog, "cancel", _("Cancel"));
        adw_message_dialog_add_response(dialog, "discard", _("Discard"));
        adw_message_dialog_set_response_appearance(dialog,
                                                    "discard",
                                                    ADW_RESPONSE_DESTRUCTIVE);

        if (!mt_document_is_untitled(document))
        {
            adw_message_dialog_add_response(dialog, "save", _("Save"));
            adw_message_dialog_set_response_appearance(dialog,
                                                        "save",
                                                        ADW_RESPONSE_SUGGESTED);
            adw_message_dialog_set_default_response(dialog, "save");
        }

        adw_message_dialog_set_close_response(dialog, "cancel");
        request = mt_close_request_new(window, document, page);
        g_signal_connect(dialog,
                         "response",
                         G_CALLBACK(mt_window_close_dialog_response),
                         request);
        gtk_window_present(GTK_WINDOW(dialog));
    }

    return TRUE;
}

static void
mt_window_find_next(MtWindow *window, gboolean backwards)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter match_start;
    GtkTextIter match_end;
    const gchar *needle;
    gboolean found;
    GtkTextSearchFlags flags;

    needle = gtk_editable_get_text(GTK_EDITABLE(window->find_entry));
    if (needle == NULL || *needle == '\0')
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!window->search_case_sensitive)
    {
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    }
    gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));

    if (backwards)
    {
        found = gtk_text_iter_backward_search(&start,
                                              needle,
                                              flags,
                                              &match_start,
                                              &match_end,
                                              NULL);
        if (!found)
        {
            gtk_text_buffer_get_end_iter(buffer, &start);
            found = gtk_text_iter_backward_search(&start,
                                                  needle,
                                                  flags,
                                                  &match_start,
                                                  &match_end,
                                                  NULL);
        }
    }
    else
    {
        if (gtk_text_buffer_get_has_selection(buffer))
        {
            gtk_text_buffer_get_selection_bounds(buffer, NULL, &start);
        }

        found = gtk_text_iter_forward_search(&start,
                                             needle,
                                             GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &match_start,
                                             &match_end,
                                             NULL);
        if (!found)
        {
            gtk_text_buffer_get_start_iter(buffer, &start);
            found = gtk_text_iter_forward_search(&start,
                                                 needle,
                                                 flags,
                                                 &match_start,
                                                 &match_end,
                                                 NULL);
        }
    }

    if (found)
    {
        gtk_text_buffer_select_range(buffer, &match_start, &match_end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(mt_document_get_view(document)),
                                     &match_start,
                                     0.25,
                                     FALSE,
                                     0.0,
                                     0.0);
    }
    else
    {
        mt_window_show_toast(window, _("No matches found"));
    }
}

static void
mt_window_find_next_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    mt_window_find_next(user_data, FALSE);
}

static void
mt_window_find_previous_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    mt_window_find_next(user_data, TRUE);
}

static void
mt_window_replace_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *selected;
    const gchar *needle;
    const gchar *replacement;

    (void)button;

    window = user_data;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    needle = gtk_editable_get_text(GTK_EDITABLE(window->find_entry));
    replacement = gtk_editable_get_text(GTK_EDITABLE(window->replace_entry));

    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
    {
        selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (g_strcmp0(selected, needle) == 0)
        {
            gtk_text_buffer_delete(buffer, &start, &end);
            gtk_text_buffer_insert(buffer, &start, replacement, -1);
        }
        g_free(selected);
    }

    mt_window_find_next(window, FALSE);
}

static void
mt_window_replace_all_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter search_from;
    GtkTextIter match_start;
    GtkTextIter match_end;
    const gchar *needle;
    const gchar *replacement;
    GtkTextSearchFlags flags;
    guint count;

    (void)button;

    window = user_data;
    document = mt_window_get_current_document(window);
    needle = gtk_editable_get_text(GTK_EDITABLE(window->find_entry));
    replacement = gtk_editable_get_text(GTK_EDITABLE(window->replace_entry));
    if (document == NULL || needle == NULL || *needle == '\0')
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!window->search_case_sensitive)
    {
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    }
    count = 0;
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_get_start_iter(buffer, &search_from);
    while (gtk_text_iter_forward_search(&search_from,
                                        needle,
                                        flags,
                                        &match_start,
                                        &match_end,
                                        NULL))
    {
        gint offset;

        offset = gtk_text_iter_get_offset(&match_start);
        gtk_text_buffer_delete(buffer, &match_start, &match_end);
        gtk_text_buffer_get_iter_at_offset(buffer, &search_from, offset);
        gtk_text_buffer_insert(buffer, &search_from, replacement, -1);
        gtk_text_buffer_get_iter_at_offset(buffer,
                                           &search_from,
                                           offset + g_utf8_strlen(replacement, -1));
        count++;
    }
    gtk_text_buffer_end_user_action(buffer);

    if (count == 0)
    {
        mt_window_show_toast(window, _("No matches found"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(ngettext("Replaced %u occurrence",
                                           "Replaced %u occurrences",
                                           count),
                                  count);
        mt_window_show_toast(window, message);
        g_free(message);
    }
}

static void
mt_window_set_replace_mode(MtWindow *window, gboolean replace_mode)
{
    if (window == NULL || window->disposed)
    {
        return;
    }

    if (window->replace_entry != NULL)
    {
        gtk_widget_set_visible(GTK_WIDGET(window->replace_entry), replace_mode);
    }
    if (window->replace_button != NULL)
    {
        gtk_widget_set_visible(window->replace_button, replace_mode);
    }
    if (window->replace_all_button != NULL)
    {
        gtk_widget_set_visible(window->replace_all_button, replace_mode);
    }
}

static void
mt_window_replace_mode_toggled(GtkToggleButton *button, gpointer user_data)
{
    mt_window_set_replace_mode(user_data, gtk_toggle_button_get_active(button));
}

static void
mt_window_search_case_toggled(GtkCheckButton *button, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }
    window->search_case_sensitive = gtk_check_button_get_active(button);
}

static void
mt_window_hide_find_bar(GtkButton *button, gpointer user_data)
{
    MtWindow *window;

    (void)button;

    window = user_data;
    if (window->find_revealer != NULL)
    {
        gtk_revealer_set_reveal_child(GTK_REVEALER(window->find_revealer), FALSE);
    }
    {
        MtDocument *document;

        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            gtk_widget_grab_focus(mt_document_get_view(document));
        }
    }
}

static void
mt_window_show_find_bar(MtWindow *window, gboolean replace_mode)
{
    if (window == NULL || window->disposed)
    {
        return;
    }
    if (window->find_revealer != NULL)
    {
        gtk_revealer_set_reveal_child(GTK_REVEALER(window->find_revealer), TRUE);
    }
    mt_window_set_replace_mode(window, replace_mode);
    if (window->replace_mode_button != NULL)
    {
        gtk_toggle_button_set_active(window->replace_mode_button, replace_mode);
    }
    gtk_widget_grab_focus(GTK_WIDGET(window->find_entry));
}

void
mt_window_apply_font_scale(MtWindow *window)
{
    mt_window_apply_editor_style(window);
}

static void
mt_window_system_dark_changed(AdwStyleManager *manager,
                              GParamSpec *pspec,
                              gpointer user_data)
{
    MtWindow *window;

    (void)manager;
    (void)pspec;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }

    if (mt_settings_get_appearance(window->settings) == MT_APPEARANCE_SYSTEM)
    {
        mt_window_apply_editor_style(window);
    }
}

void
mt_window_appearance_selected(GObject *object,
                              GParamSpec *pspec,
                              gpointer user_data)
{
    AdwComboRow *row;
    MtWindow *window;

    (void)pspec;

    row = ADW_COMBO_ROW(object);
    window = user_data;
    mt_settings_set_appearance(window->settings, (MtAppearance)adw_combo_row_get_selected(row));
    mt_settings_apply_appearance(window->settings);
    mt_window_apply_editor_style(window);
    mt_window_update_menu_theme_buttons(window);
    mt_settings_save(window->settings);
}

const gchar *
mt_window_theme_label(const gchar *style_scheme)
{
    if (g_strcmp0(style_scheme, "adwaita") == 0)
    {
        return _("Adwaita");
    }
    if (g_strcmp0(style_scheme, "classic") == 0)
    {
        return _("Classic");
    }
    if (g_strcmp0(style_scheme, "cobalt") == 0)
    {
        return _("Cobalt");
    }
    if (g_strcmp0(style_scheme, "kate") == 0)
    {
        return _("Kate");
    }
    if (g_strcmp0(style_scheme, "oblivion") == 0)
    {
        return _("Oblivion");
    }
    if (g_strcmp0(style_scheme, "solarized") == 0)
    {
        return _("Solarized");
    }
    if (g_strcmp0(style_scheme, "tango") == 0)
    {
        return _("Tango");
    }

    return _("Automatic (Adwaita)");
}

static void
mt_window_theme_card_clicked(GtkButton *button, gpointer user_data)
{
    MtThemeChooser *chooser;
    const gchar *style_scheme;

    chooser = user_data;
    style_scheme = g_object_get_data(G_OBJECT(button), "vellum-theme-style-scheme");
    if (style_scheme == NULL)
    {
        return;
    }

    {
        MtWindow *window;

        window = chooser->window;
        mt_timing_log("theme-card-clicked: %s", style_scheme);
        mt_settings_set_style_scheme(window->settings, style_scheme);
        mt_settings_save(window->settings);
        adw_action_row_set_subtitle(chooser->row, mt_window_theme_label(style_scheme));
        /* 选中即应用并关闭，避免配色应用较慢时窗口“退不出来”。 */
        gtk_window_close(chooser->dialog);
        mt_window_apply_source_scheme(window);
    }
}

static const gchar *
mt_window_preview_scheme_id(MtWindow *window, const gchar *style_scheme)
{
    gboolean dark;

    dark = mt_window_is_dark(window);
    if (g_strcmp0(style_scheme, "adwaita") == 0 ||
        g_strcmp0(style_scheme, "auto") == 0)
    {
        return dark ? "Adwaita-dark" : "Adwaita";
    }
    if (g_strcmp0(style_scheme, "classic") == 0)
    {
        return dark ? "classic-dark" : "classic";
    }
    if (g_strcmp0(style_scheme, "cobalt") == 0)
    {
        return dark ? "cobalt" : "cobalt-light";
    }
    if (g_strcmp0(style_scheme, "kate") == 0)
    {
        return dark ? "kate-dark" : "kate";
    }
    if (g_strcmp0(style_scheme, "oblivion") == 0)
    {
        return "oblivion";
    }
    if (g_strcmp0(style_scheme, "solarized") == 0)
    {
        return dark ? "solarized-dark" : "solarized-light";
    }
    if (g_strcmp0(style_scheme, "tango") == 0)
    {
        return "tango";
    }

    return dark ? "Adwaita-dark" : "Adwaita";
}

static GtkWidget *
mt_window_create_theme_card(MtThemeChooser *chooser, const gchar *style_scheme)
{
    GtkSourceStyleSchemeManager *scheme_manager;
    GtkSourceStyleScheme *scheme;
    GtkSourceLanguageManager *language_manager;
    GtkSourceLanguage *language;
    GtkSourceBuffer *buffer;
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *preview_frame;
    GtkWidget *preview;
    GtkWidget *label;

    scheme_manager = gtk_source_style_scheme_manager_get_default();
    scheme = gtk_source_style_scheme_manager_get_scheme(
        scheme_manager,
        mt_window_preview_scheme_id(chooser->window, style_scheme));
    language_manager = gtk_source_language_manager_get_default();
    language = gtk_source_language_manager_get_language(language_manager, "c");
    buffer = gtk_source_buffer_new_with_language(language);
    if (scheme != NULL)
    {
        gtk_source_buffer_set_style_scheme(buffer, scheme);
    }
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer),
                             "/* Vellum */\nint main(void)\n{\n    return 0;\n}\n",
                             -1);

    preview = gtk_source_view_new_with_buffer(buffer);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(preview), TRUE);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(preview), FALSE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(preview), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview), TRUE);
    gtk_widget_set_size_request(preview, 212, 132);
    gtk_widget_add_css_class(preview, "vellum-theme-preview");
    g_object_unref(buffer);

    preview_frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(preview_frame), preview);
    gtk_widget_add_css_class(preview_frame, "vellum-theme-preview-frame");

    label = gtk_label_new(mt_window_theme_label(style_scheme));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "vellum-theme-name");

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_box_append(GTK_BOX(box), preview_frame);
    gtk_box_append(GTK_BOX(box), label);

    button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(button), box);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_tooltip_text(button, mt_window_theme_label(style_scheme));
    gtk_widget_add_css_class(button, "vellum-theme-card");
    g_object_set_data_full(G_OBJECT(button),
                           "vellum-theme-style-scheme",
                           g_strdup(style_scheme),
                           g_free);
    if (g_strcmp0(style_scheme, mt_settings_get_style_scheme(chooser->window->settings)) == 0)
    {
        gtk_widget_add_css_class(button, "vellum-theme-selected");
    }
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(mt_window_theme_card_clicked),
                     chooser);

    return button;
}

static void
mt_window_theme_chooser_free(MtThemeChooser *chooser)
{
    g_free(chooser);
}

void
mt_window_show_theme_chooser(GtkButton *button, gpointer user_data)
{
    static const gchar * const themes[] = {
        "auto", "adwaita", "classic", "cobalt",
        "kate", "oblivion", "solarized", "tango"
    };
    MtWindow *window;
    MtThemeChooser *chooser;
    GtkWidget *content;
    GtkWidget *heading;
    GtkWidget *description;
    GtkWidget *flow_box;
    GtkWidget *scroller;
    GtkWidget *footer;
    guint index;

    window = user_data;
    chooser = g_new0(MtThemeChooser, 1);
    chooser->window = window;
    chooser->row = ADW_ACTION_ROW(g_object_get_data(G_OBJECT(button), "vellum-theme-row"));
    chooser->dialog = GTK_WINDOW(adw_window_new());
    gtk_window_set_title(chooser->dialog, _("Choose Code Theme"));
    /*
     * 挂在打开它的首选项窗口下而不是主窗口：多个 modal 窗口都挂主窗口时，
     * 关闭后一个会令前一个（首选项）无法再获得输入抓取，表现为窗口“冻住”。
     */
    {
        GtkWidget *dialog_parent;

        dialog_parent = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(chooser->row)));
        if (GTK_IS_WINDOW(dialog_parent))
        {
            gtk_window_set_transient_for(chooser->dialog, GTK_WINDOW(dialog_parent));
        }
    }
    gtk_window_set_modal(chooser->dialog, TRUE);
    gtk_window_set_default_size(chooser->dialog, 570, 520);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);

    heading = gtk_label_new(_("Code Themes"));
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_widget_add_css_class(heading, "title-2");
    description = gtk_label_new(_("Choose a color scheme for source code. The preview uses the same GtkSourceView scheme as the editor."));
    gtk_label_set_xalign(GTK_LABEL(description), 0.0);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_widget_add_css_class(description, "dim-label");

    flow_box = gtk_flow_box_new();
    chooser->flow_box = GTK_FLOW_BOX(flow_box);
    gtk_flow_box_set_selection_mode(chooser->flow_box, GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(chooser->flow_box, 2);
    gtk_flow_box_set_min_children_per_line(chooser->flow_box, 2);
    gtk_flow_box_set_row_spacing(chooser->flow_box, 12);
    gtk_flow_box_set_column_spacing(chooser->flow_box, 12);

    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), flow_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);

    for (index = 0; index < G_N_ELEMENTS(themes); index++)
    {
        gtk_flow_box_insert(chooser->flow_box,
                            mt_window_create_theme_card(chooser, themes[index]),
                            -1);
    }

    footer = gtk_label_new(_("Theme colors are provided by installed GtkSourceView schemes; Vellum does not copy third-party theme assets."));
    gtk_label_set_xalign(GTK_LABEL(footer), 0.0);
    gtk_label_set_wrap(GTK_LABEL(footer), TRUE);
    gtk_widget_add_css_class(footer, "dim-label");
    gtk_widget_add_css_class(footer, "caption");

    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), description);
    gtk_box_append(GTK_BOX(content), scroller);
    gtk_box_append(GTK_BOX(content), footer);
    /* AdwWindow 自身没有标题栏，必须显式提供 AdwHeaderBar，
     * 否则没有关闭按钮，模态窗口会“退不出来”。 */
    {
        GtkWidget *toolbar;
        GtkWidget *header;

        toolbar = adw_toolbar_view_new();
        header = adw_header_bar_new();
        adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
        adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
        adw_window_set_content(ADW_WINDOW(chooser->dialog), toolbar);
    }
    g_object_set_data_full(G_OBJECT(chooser->dialog),
                           "vellum-theme-chooser",
                           chooser,
                           (GDestroyNotify)mt_window_theme_chooser_free);
    gtk_window_present(chooser->dialog);
}

void
mt_window_language_selected(GObject *object,
                            GParamSpec *pspec,
                            gpointer user_data)
{
    AdwComboRow *row;
    MtWindow *window;
    const gchar *language;

    (void)pspec;

    row = ADW_COMBO_ROW(object);
    window = user_data;
    language = adw_combo_row_get_selected(row) == 1 ? "en" :
               (adw_combo_row_get_selected(row) == 2 ? "zh_CN" : "system");
    mt_settings_set_language(window->settings, language);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Language will apply after restart"));
}

void
mt_window_tab_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_settings_set_tab_width(window->settings,
                              (gint)gtk_adjustment_get_value(adw_spin_row_get_adjustment(row)));
    mt_window_apply_editor_preferences(window);
    mt_settings_save(window->settings);
}

void
mt_window_font_scale_changed(AdwSpinRow *row, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_settings_set_font_scale(window->settings,
                               gtk_adjustment_get_value(adw_spin_row_get_adjustment(row)));
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
}

static gboolean
mt_window_key_pressed(GtkEventControllerKey *controller,
                      guint keyval,
                      guint keycode,
                      GdkModifierType state,
                      gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    MtPluginManager *manager;

    (void)controller;
    window = user_data;
    manager = window->plugin_manager;
    document = mt_window_get_current_document(window);
    if (manager == NULL || document == NULL ||
        !gtk_widget_has_focus(mt_document_get_view(document)))
    {
        return FALSE;
    }

    return mt_plugin_manager_handle_key(manager, keyval, keycode, (guint)state);
}


/* ---------- 文档属性面板（学习原版 GNOME Text Editor 的 Document Properties） ---------- */


#define MT_PROPERTIES_STATS_LIMIT 500000

static void
mt_window_properties_open_uri(GtkWindow *parent, const gchar *uri)
{
    GtkUriLauncher *launcher;

    if (parent == NULL || uri == NULL || *uri == '\0')
    {
        return;
    }

    launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher, parent, NULL, NULL, NULL);
    g_object_unref(launcher);
}

static void
mt_window_properties_name_activated(AdwActionRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;
    MtDocument *document;
    GFile *file;
    gchar *uri;

    (void)row;
    panel = user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL || mt_document_is_untitled(document))
    {
        return;
    }
    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }
    uri = g_file_get_uri(file);
    mt_window_properties_open_uri(GTK_WINDOW(panel->window->window), uri);
    g_free(uri);
}

static void
mt_window_properties_location_activated(AdwActionRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;
    MtDocument *document;
    GFile *file;
    GFile *parent;
    gchar *uri;

    (void)row;
    panel = user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL || mt_document_is_untitled(document))
    {
        return;
    }
    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }
    parent = g_file_get_parent(file);
    uri = parent != NULL ? g_file_get_uri(parent) : g_file_get_uri(file);
    mt_window_properties_open_uri(GTK_WINDOW(panel->window->window), uri);
    g_free(uri);
    g_clear_object(&parent);
}

static GtkSourceNewlineType
mt_window_properties_detect_newline(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    const gchar *p;
    GtkSourceNewlineType newline;

    gtk_text_buffer_get_start_iter(buffer, &start);
    end = start;
    gtk_text_iter_forward_chars(&end, 4096);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    newline = GTK_SOURCE_NEWLINE_TYPE_LF;
    for (p = text; *p != '\0'; p = g_utf8_next_char(p))
    {
        if (*p == '\r')
        {
            if (p[1] == '\n')
            {
                newline = GTK_SOURCE_NEWLINE_TYPE_CR_LF;
            }
            else
            {
                newline = GTK_SOURCE_NEWLINE_TYPE_CR;
            }
            break;
        }
        if (*p == '\n')
        {
            newline = GTK_SOURCE_NEWLINE_TYPE_LF;
            break;
        }
    }
    g_free(text);
    return newline;
}

static void
mt_window_properties_convert_newlines(MtPropertiesPanel *panel,
                                      GtkSourceNewlineType newline)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    GString *converted;
    const gchar *target;
    const gchar *p;

    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    if (gtk_text_buffer_get_char_count(buffer) > MT_PROPERTIES_STATS_LIMIT)
    {
        mt_window_properties_refresh_metadata(panel);
        mt_window_show_toast(panel->window,
                             _("The document is too large to convert line endings"));
        return;
    }

    if (mt_window_properties_detect_newline(buffer) == newline)
    {
        return;
    }

    switch (newline)
    {
        case GTK_SOURCE_NEWLINE_TYPE_CR:
            target = "\r";
            break;
        case GTK_SOURCE_NEWLINE_TYPE_CR_LF:
            target = "\r\n";
            break;
        default:
            target = "\n";
            break;
    }

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    converted = g_string_new(NULL);
    for (p = text; *p != '\0'; p++)
    {
        if (*p == '\r')
        {
            if (p[1] == '\n')
            {
                p++;
            }
            g_string_append(converted, target);
        }
        else if (*p == '\n')
        {
            g_string_append(converted, target);
        }
        else
        {
            g_string_append_c(converted, *p);
        }
    }
    g_free(text);

    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, converted->str, -1);
    gtk_text_buffer_end_user_action(buffer);
    g_string_free(converted, TRUE);

    mt_window_show_toast(panel->window, _("Line endings converted"));
}

static void
mt_window_properties_refresh_metadata(MtPropertiesPanel *panel)
{
    MtWindow *window;
    MtDocument *document;
    GtkSourceBuffer *buffer;
    GtkSourceLanguage *language;
    GtkSourceFile *source_file;
    const GtkSourceEncoding *encoding;
    GtkSourceNewlineType newline;
    GFile *file;

    window = panel->window;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        adw_action_row_set_subtitle(panel->name_row, "");
        adw_action_row_set_subtitle(panel->location_row, "");
        g_object_set(G_OBJECT(panel->name_row), "activatable", FALSE, NULL);
        g_object_set(G_OBJECT(panel->location_row), "activatable", FALSE, NULL);
        adw_action_row_set_subtitle(panel->type_row, _("Plain Text"));
        panel->type_selected = 0;
        panel->encoding_selected = 0;
        panel->refreshing_metadata = TRUE;
        adw_combo_row_set_selected(panel->encoding_row, 0);
        adw_combo_row_set_selected(panel->newline_row, 0);
        panel->refreshing_metadata = FALSE;
        return;
    }

    adw_action_row_set_subtitle(panel->name_row, mt_document_get_display_name(document));
    g_object_set(G_OBJECT(panel->name_row), "activatable", !mt_document_is_untitled(document), NULL);

    file = mt_document_get_file(document);
    if (file != NULL && !mt_document_is_untitled(document))
    {
        GFile *parent;
        gchar *path;

        parent = g_file_get_parent(file);
        path = parent != NULL ? g_file_get_path(parent) : NULL;
        adw_action_row_set_subtitle(panel->location_row,
                                    path != NULL ? path : _("Unknown location"));
        g_free(path);
        g_clear_object(&parent);
        g_object_set(G_OBJECT(panel->location_row), "activatable", TRUE, NULL);
    }
    else
    {
        adw_action_row_set_subtitle(panel->location_row, _("Not saved yet"));
        g_object_set(G_OBJECT(panel->location_row), "activatable", FALSE, NULL);
    }

    buffer = mt_document_get_buffer(document);
    language = gtk_source_buffer_get_language(buffer);
    source_file = document->source_file;
    encoding = source_file != NULL ? gtk_source_file_get_encoding(source_file) : NULL;

    if (language != NULL)
    {
        const gchar *id;
        const gchar *name;
        guint index;

        id = gtk_source_language_get_id(language);
        name = gtk_source_language_get_name(language);
        adw_action_row_set_subtitle(panel->type_row, name != NULL ? name : id);
        panel->type_selected = 0;
        for (index = 0; index < panel->type_language_ids->len; index++)
        {
            if (g_strcmp0(g_ptr_array_index(panel->type_language_ids, index), id) == 0)
            {
                panel->type_selected = index + 1;
                break;
            }
        }
    }
    else
    {
        adw_action_row_set_subtitle(panel->type_row, _("Plain Text"));
        panel->type_selected = 0;
    }

    if (encoding != NULL)
    {
        const gchar *name;
        guint index;

        name = gtk_source_encoding_get_name(encoding);
        panel->encoding_selected = 0;
        for (index = 0; index < panel->encoding_items->len; index++)
        {
            GtkSourceEncoding *item;

            item = g_ptr_array_index(panel->encoding_items, index);
            if (g_strcmp0(gtk_source_encoding_get_name(item), name) == 0)
            {
                panel->encoding_selected = index;
                break;
            }
        }
    }
    else
    {
        panel->encoding_selected = 0;
    }

    /* 编码和换行均使用原生选择行；刷新时统一抑制回调，避免误触发重载或转换。 */
    panel->refreshing_metadata = TRUE;
    adw_combo_row_set_selected(panel->encoding_row, panel->encoding_selected);
    {
        guint selected;

        newline = mt_window_properties_detect_newline(GTK_TEXT_BUFFER(buffer));
        switch (newline)
        {
            case GTK_SOURCE_NEWLINE_TYPE_CR:
                selected = 2;
                break;
            case GTK_SOURCE_NEWLINE_TYPE_CR_LF:
                selected = 1;
                break;
            default:
                selected = 0;
                break;
        }
        adw_combo_row_set_selected(panel->newline_row, selected);
    }
    panel->refreshing_metadata = FALSE;
}

static void
mt_window_properties_refresh_stats(MtPropertiesPanel *panel)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    const gchar *pointer;
    guint lines;
    guint words;
    guint chars;
    guint printable;
    gchar *label;

    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    lines = gtk_text_buffer_get_line_count(buffer);
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    chars = gtk_text_iter_get_offset(&end);
    words = 0;
    printable = 0;

    /* 超大文档避免整篇拷贝统计，防止输入时卡顿。 */
    if (chars <= MT_PROPERTIES_STATS_LIMIT)
    {
        gboolean in_word;

        text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (text != NULL)
        {
            in_word = FALSE;
            for (pointer = text; *pointer != '\0'; pointer = g_utf8_next_char(pointer))
            {
                gunichar ch;

                ch = g_utf8_get_char(pointer);
                if (g_unichar_isspace(ch))
                {
                    in_word = FALSE;
                }
                else
                {
                    printable++;
                    if (!in_word)
                    {
                        words++;
                        in_word = TRUE;
                    }
                }
            }
            g_free(text);
        }
    }

    label = g_strdup_printf("%u", lines);
    adw_action_row_set_subtitle(panel->lines_row, label);
    g_free(label);
    label = g_strdup_printf("%u", words);
    adw_action_row_set_subtitle(panel->words_row, label);
    g_free(label);
    label = g_strdup_printf("%u", printable);
    adw_action_row_set_subtitle(panel->chars_row, label);
    g_free(label);
    label = g_strdup_printf("%u", chars);
    adw_action_row_set_subtitle(panel->all_chars_row, label);
    g_free(label);
}

static void
mt_window_properties_refresh(MtWindow *window)
{
    MtPropertiesPanel *panel;

    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return;
    }
    panel = window->properties;
    mt_window_properties_refresh_metadata(panel);
    mt_window_properties_refresh_stats(panel);
}

static gboolean
mt_window_properties_refresh_cb(gpointer user_data)
{
    MtWindow *window;
    MtPropertiesPanel *panel;

    window = user_data;
    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return G_SOURCE_REMOVE;
    }
    window->properties_source_id = 0;
    panel = window->properties;
    if (gtk_widget_get_visible(panel->page))
    {
        mt_window_properties_refresh(window);
    }
    return G_SOURCE_REMOVE;
}

static void
mt_window_properties_schedule_refresh(MtWindow *window)
{
    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return;
    }
    if (window->properties_source_id != 0)
    {
        g_source_remove(window->properties_source_id);
    }
    window->properties_source_id = g_timeout_add(400,
                                                 mt_window_properties_refresh_cb,
                                                 window);
}

/* 属性面板在停靠时保持紧凑的固定视觉宽度；窄窗口时由 OverlaySplitView 以抽屉呈现。 */
#define MT_PROPERTIES_PANEL_WIDTH 300

/* 右侧检查器由 AdwOverlaySplitView 管理：宽屏停靠且宽度稳定，窄屏以不压缩编辑区的抽屉显示。 */
static void
mt_window_auxiliary_visible(MtWindow *window, gboolean visible)
{
    if (window == NULL || window->auxiliary_stack == NULL)
    {
        return;
    }

    if (window->auxiliary_wrap != NULL)
    {
        gtk_widget_set_visible(GTK_WIDGET(window->auxiliary_wrap), TRUE);
    }
    gtk_widget_set_visible(GTK_WIDGET(window->auxiliary_stack), TRUE);
    if (window->auxiliary_split != NULL)
    {
        adw_overlay_split_view_set_show_sidebar(window->auxiliary_split, visible);
    }
}

static void
mt_window_auxiliary_width_changed(GObject *object,
                                  GParamSpec *pspec,
                                  gpointer user_data)
{
    MtWindow *window;
    gboolean collapsed;

    (void)object;
    (void)pspec;
    window = user_data;
    if (window == NULL || window->disposed || window->auxiliary_split == NULL)
    {
        return;
    }

    collapsed = gtk_widget_get_width(GTK_WIDGET(window->auxiliary_split)) <
                MT_AUXILIARY_DRAWER_BREAKPOINT;
    if (adw_overlay_split_view_get_collapsed(window->auxiliary_split) != collapsed)
    {
        adw_overlay_split_view_set_collapsed(window->auxiliary_split, collapsed);
    }
}

static void
mt_window_properties_set_visible(MtWindow *window, gboolean visible)
{
    MtPropertiesPanel *panel;

    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return;
    }
    panel = window->properties;

    if (visible)
    {
        if (gtk_widget_get_parent(panel->page) == NULL)
        {
            gtk_stack_add_named(window->auxiliary_stack,
                                panel->page,
                                "vellum-properties");
        }
        gtk_stack_set_visible_child_name(window->auxiliary_stack, "vellum-properties");
        mt_window_auxiliary_visible(window, TRUE);
        mt_window_properties_refresh(window);
    }
    else
    {
        gtk_stack_remove(window->auxiliary_stack, panel->page);
        if (g_list_model_get_n_items(G_LIST_MODEL(gtk_stack_get_pages(window->auxiliary_stack))) == 0)
        {
            mt_window_auxiliary_visible(window, FALSE);
        }
    }
}

static void
mt_window_properties_toggled(GtkToggleButton *button, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }
    mt_window_properties_set_visible(window, gtk_toggle_button_get_active(button));
}

static void
mt_window_toggle_properties(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;

    (void)action;
    (void)parameter;
    window = user_data;
    if (window == NULL || window->disposed || window->properties_button == NULL)
    {
        return;
    }
    gtk_toggle_button_set_active(window->properties_button,
                                 !gtk_toggle_button_get_active(window->properties_button));
}

static gboolean
mt_window_properties_name_matches(const gchar *name, const gchar *filter)
{
    gchar *name_folded;
    gchar *filter_folded;
    gboolean matches;

    if (filter == NULL || *filter == '\0')
    {
        return TRUE;
    }

    name_folded = g_utf8_casefold(name != NULL ? name : "", -1);
    filter_folded = g_utf8_casefold(filter, -1);
    matches = strstr(name_folded, filter_folded) != NULL;
    g_free(name_folded);
    g_free(filter_folded);

    return matches;
}

/* 弹窗选择器的一行：左侧名称，右侧对当前项显示勾选标记。 */
static GtkListBoxRow *
mt_window_properties_picker_row(const gchar *name, guint index, gboolean selected)
{
    GtkWidget *row;
    GtkWidget *box;
    GtkWidget *label;
    GtkWidget *check;

    row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "vellum-picker-index", GUINT_TO_POINTER(index));
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    check = gtk_image_new_from_icon_name("object-select-symbolic");
    gtk_widget_set_visible(check, selected);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), check);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    return GTK_LIST_BOX_ROW(row);
}

/* 按搜索词重建弹窗列表；items 里每个条目的 index 指向底层数组。 */
static void
mt_window_properties_picker_populate(GtkListBox *list,
                                     GPtrArray *items,
                                     guint selected_index,
                                     const gchar *filter)
{
    guint index;

    while (TRUE)
    {
        GtkWidget *child;

        child = gtk_widget_get_first_child(GTK_WIDGET(list));
        if (child == NULL)
        {
            break;
        }
        gtk_list_box_remove(list, child);
    }

    for (index = 0; index < items->len; index++)
    {
        MtPropertiesItem *item;

        item = g_ptr_array_index(items, index);
        if (!mt_window_properties_name_matches(item->name, filter))
        {
            continue;
        }
        gtk_list_box_append(list,
                            GTK_WIDGET(mt_window_properties_picker_row(item->name,
                                                                       item->index,
                                                                       item->index == selected_index)));
    }
}

static void
mt_window_properties_type_dialog_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    AdwDialog *dialog;
    MtPropertiesPanel *panel;
    GtkWidget *list;

    dialog = ADW_DIALOG(user_data);
    panel = g_object_get_data(G_OBJECT(dialog), "vellum-type-panel");
    list = g_object_get_data(G_OBJECT(dialog), "vellum-type-list");
    if (panel != NULL && list != NULL)
    {
        mt_window_properties_picker_populate(GTK_LIST_BOX(list),
                                             panel->type_items,
                                             panel->type_selected,
                                             gtk_editable_get_text(GTK_EDITABLE(entry)));
    }
}

static void
mt_window_properties_type_dialog_row_activated(GtkListBox *list,
                                               GtkListBoxRow *row,
                                               gpointer user_data)
{
    AdwDialog *dialog;
    MtPropertiesPanel *panel;
    guint index;

    (void)list;
    dialog = ADW_DIALOG(user_data);
    panel = g_object_get_data(G_OBJECT(dialog), "vellum-type-panel");
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "vellum-picker-index"));
    if (panel != NULL)
    {
        mt_window_properties_type_picked(panel, index, NULL);
    }
    adw_dialog_close(dialog);
}

/* 原版把文档类型作为独立、可搜索的自适应选择器；窄屏时由 AdwDialog 自动表现为抽屉。 */
static void
mt_window_properties_type_dialog_show(AdwActionRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;
    AdwDialog *dialog;
    GtkWidget *toolbar;
    GtkWidget *header;
    GtkWidget *box;
    GtkWidget *search;
    GtkWidget *scroller;
    GtkWidget *list;

    (void)row;
    panel = user_data;
    dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, _("Document Type"));
    adw_dialog_set_content_width(dialog, 360);
    adw_dialog_set_content_height(dialog, 520);
    adw_dialog_set_presentation_mode(dialog, ADW_DIALOG_AUTO);

    toolbar = adw_toolbar_view_new();
    header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 12);
    search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), _("Search"));
    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_box_append(GTK_BOX(box), search);
    gtk_box_append(GTK_BOX(box), scroller);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), box);
    adw_dialog_set_child(dialog, toolbar);

    g_object_set_data(G_OBJECT(dialog), "vellum-type-panel", panel);
    g_object_set_data(G_OBJECT(dialog), "vellum-type-list", list);
    g_signal_connect(search,
                     "search-changed",
                     G_CALLBACK(mt_window_properties_type_dialog_search_changed),
                     dialog);
    g_signal_connect(list,
                     "row-activated",
                     G_CALLBACK(mt_window_properties_type_dialog_row_activated),
                     dialog);
    mt_window_properties_picker_populate(GTK_LIST_BOX(list),
                                         panel->type_items,
                                         panel->type_selected,
                                         "");
    adw_dialog_present(dialog, GTK_WIDGET(panel->window->window));
    adw_dialog_set_focus(dialog, search);
}

static void
mt_window_properties_type_picked(MtPropertiesPanel *panel,
                                 guint index,
                                 gpointer user_data)
{
    MtDocument *document;
    GtkSourceLanguageManager *manager;
    GtkSourceLanguage *language;
    const gchar *id;

    (void)user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    id = NULL;
    if (index > 0 && index - 1 < panel->type_language_ids->len)
    {
        id = g_ptr_array_index(panel->type_language_ids, index - 1);
    }
    manager = gtk_source_language_manager_get_default();
    language = id != NULL ? gtk_source_language_manager_get_language(manager, id) : NULL;
    gtk_source_buffer_set_language(mt_document_get_buffer(document), language);

    panel->type_selected = index;
    mt_window_update_language_label(panel->window);
    mt_window_properties_refresh_metadata(panel);
}

static void
mt_window_properties_encoding_picked(MtPropertiesPanel *panel,
                                     guint index,
                                     gpointer user_data)
{
    MtDocument *document;
    GtkSourceEncoding *encoding;

    (void)user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    encoding = index < panel->encoding_items->len ?
               g_ptr_array_index(panel->encoding_items, index) : NULL;
    if (encoding == NULL)
    {
        return;
    }
    panel->encoding_selected = index;

    /* 未保存的修改不允许换编码重载，否则会丢失内容。 */
    if (mt_document_is_modified(document))
    {
        mt_window_properties_refresh_metadata(panel);
        mt_window_show_toast(panel->window,
                             _("Save or discard changes before changing the encoding"));
        return;
    }

    if (document->source_file == NULL || mt_document_get_file(document) == NULL)
    {
        mt_window_properties_refresh_metadata(panel);
        mt_window_show_toast(panel->window,
                             _("Save the file first to change its encoding"));
        return;
    }

    {
        MtFileRequest *request;

        request = mt_file_request_new(panel->window, document, NULL);
        mt_document_reload_with_encoding(document,
                                         encoding,
                                         mt_window_document_load_finished,
                                         request);
        mt_window_show_toast(panel->window, _("Reloading with the selected encoding…"));
    }
}

static void
mt_window_properties_encoding_changed(GObject *object,
                                      GParamSpec *pspec,
                                      gpointer user_data)
{
    MtPropertiesPanel *panel;

    (void)pspec;
    panel = user_data;
    if (panel->refreshing_metadata)
    {
        return;
    }
    mt_window_properties_encoding_picked(panel,
                                         adw_combo_row_get_selected(ADW_COMBO_ROW(object)),
                                         NULL);
}

static void
mt_window_properties_newline_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    MtPropertiesPanel *panel;
    MtDocument *document;
    GtkSourceNewlineType newline;

    (void)object;
    (void)pspec;
    panel = user_data;
    if (panel->refreshing_metadata)
    {
        return;
    }
    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    switch (adw_combo_row_get_selected(panel->newline_row))
    {
        case 1:
            newline = GTK_SOURCE_NEWLINE_TYPE_CR_LF;
            break;
        case 2:
            newline = GTK_SOURCE_NEWLINE_TYPE_CR;
            break;
        default:
            newline = GTK_SOURCE_NEWLINE_TYPE_LF;
            break;
    }

    mt_window_properties_convert_newlines(panel, newline);
}

static void
mt_window_properties_auto_indent_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data)
{
    MtPropertiesPanel *panel;

    (void)object;
    (void)pspec;
    panel = user_data;
    mt_settings_set_auto_indent(panel->window->settings,
                                adw_switch_row_get_active(panel->auto_indent_row));
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

static void
mt_window_properties_indent_char_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data)
{
    MtPropertiesPanel *panel;

    (void)object;
    (void)pspec;
    panel = user_data;
    mt_settings_set_insert_spaces(panel->window->settings,
                                  adw_combo_row_get_selected(panel->indent_char_row) == 1);
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

static void
mt_window_properties_tab_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;

    panel = user_data;
    mt_settings_set_tab_width(panel->window->settings,
                              (gint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(row)));
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

static void
mt_window_properties_indent_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;

    panel = user_data;
    mt_settings_set_indent_width(panel->window->settings,
                                 (gint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(row)));
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

static MtPropertiesPanel *
mt_window_properties_create(MtWindow *window)
{
    MtPropertiesPanel *panel;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    AdwActionRow *row;

    panel = g_new0(MtPropertiesPanel, 1);
    panel->window = window;
    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    /* 覆盖 AdwPreferencesPage 默认的内缩外边距，内容贴齐侧栏，避免“陷进去”。 */
    gtk_widget_add_css_class(GTK_WIDGET(page), "vellum-properties");
    /* 面板自身持有页面引用：隐藏时 gtk_stack_remove 会解除父引用，
     * 若没有这份引用，页面对像会在首次隐藏时被销毁，再次显示即段错误。 */
    panel->page = GTK_WIDGET(page);
    g_object_ref(panel->page);

    /* 文件元信息 */
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("File Name"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_action_row_set_subtitle_selectable(row, TRUE);
    g_object_set(G_OBJECT(row), "activatable", TRUE, NULL);
    g_signal_connect(row, "activated", G_CALLBACK(mt_window_properties_name_activated), panel);
    panel->name_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Location"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_action_row_set_subtitle_selectable(row, TRUE);
    g_object_set(G_OBJECT(row), "activatable", TRUE, NULL);
    adw_action_row_set_subtitle(row, _("Not saved yet"));
    g_signal_connect(row, "activated", G_CALLBACK(mt_window_properties_location_activated), panel);
    panel->location_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    /* 类型：行 + 弹出子窗口（学习原版 GNOME Text Editor 的语法高亮子菜单方式），
     * 顶部带搜索框，可以直接输入语言名过滤。 */
    panel->type_language_ids = g_ptr_array_new_with_free_func(g_free);
    panel->type_items = g_ptr_array_new_with_free_func(mt_properties_item_free);
    {
        GtkSourceLanguageManager *manager;
        const gchar * const *ids;
        MtPropertiesItem *item;
        guint index;

        item = g_new(MtPropertiesItem, 1);
        item->name = g_strdup(_("Plain Text"));
        item->index = 0;
        g_ptr_array_add(panel->type_items, item);

        manager = gtk_source_language_manager_get_default();
        ids = gtk_source_language_manager_get_language_ids(manager);
        for (index = 0; ids != NULL && ids[index] != NULL; index++)
        {
            GtkSourceLanguage *language;
            const gchar *name;

            g_ptr_array_add(panel->type_language_ids, g_strdup(ids[index]));
            item = g_new(MtPropertiesItem, 1);
            language = gtk_source_language_manager_get_language(manager, ids[index]);
            name = language != NULL ? gtk_source_language_get_name(language) : ids[index];
            item->name = g_strdup(name);
            item->index = index + 1;
            g_ptr_array_add(panel->type_items, item);
        }
    }
    panel->type_selected = 0;
    panel->type_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->type_row), _("Document Type"));
    adw_action_row_set_subtitle(panel->type_row, _("Plain Text"));
    g_object_set(G_OBJECT(panel->type_row), "activatable", TRUE, NULL);
    panel->type_button = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_valign(panel->type_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(panel->type_button, 6);
    adw_action_row_add_suffix(panel->type_row, panel->type_button);
    gtk_widget_set_tooltip_text(GTK_WIDGET(panel->type_row), _("Change Document Type"));
    g_signal_connect(panel->type_row,
                     "activated",
                     G_CALLBACK(mt_window_properties_type_dialog_show),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->type_row));

    /* 编码沿用原版的 AdwComboRow：标准下拉列表，搜索行为由 Libadwaita 统一提供。 */
    /* GtkSourceEncoding 是静态单例，不参与引用计数。 */
    panel->encoding_items = g_ptr_array_new();
    panel->encoding_picker_items = g_ptr_array_new_with_free_func(mt_properties_item_free);
    panel->encoding_model = gtk_string_list_new(NULL);
    {
        GSList *encodings;
        GSList *link;
        guint index;

        encodings = gtk_source_encoding_get_all();
        index = 0;
        for (link = encodings; link != NULL; link = link->next)
        {
            GtkSourceEncoding *encoding;
            MtPropertiesItem *item;

            encoding = link->data;
            g_ptr_array_add(panel->encoding_items, encoding);
            item = g_new(MtPropertiesItem, 1);
            item->name = g_strdup(gtk_source_encoding_get_name(encoding));
            item->index = index;
            gtk_string_list_append(panel->encoding_model, item->name);
            g_ptr_array_add(panel->encoding_picker_items, item);
            index++;
        }
        g_slist_free(encodings);
    }
    panel->encoding_selected = 0;
    panel->encoding_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->encoding_row), _("Encoding"));
    adw_combo_row_set_model(panel->encoding_row, G_LIST_MODEL(panel->encoding_model));
    adw_combo_row_set_enable_search(panel->encoding_row, TRUE);
    adw_combo_row_set_selected(panel->encoding_row, 0);
    g_signal_connect(panel->encoding_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_properties_encoding_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->encoding_row));

    /* 换行符：三种常用行尾。 */
    panel->newline_model = gtk_string_list_new(NULL);
    gtk_string_list_append(panel->newline_model, _("Unix (LF)"));
    gtk_string_list_append(panel->newline_model, _("Windows (CRLF)"));
    gtk_string_list_append(panel->newline_model, _("Classic Mac (CR)"));
    panel->newline_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->newline_row), _("Newline"));
    adw_combo_row_set_model(panel->newline_row, G_LIST_MODEL(panel->newline_model));
    adw_combo_row_set_selected(panel->newline_row, 0);
    g_signal_connect(panel->newline_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_properties_newline_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->newline_row));
    adw_preferences_page_add(page, group);

    /* 缩进配置 */
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Indentation"));
    panel->auto_indent_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->auto_indent_row),
                                  _("Auto Indentation"));
    adw_switch_row_set_active(panel->auto_indent_row,
                              mt_settings_get_auto_indent(window->settings));
    g_signal_connect(panel->auto_indent_row,
                     "notify::active",
                     G_CALLBACK(mt_window_properties_auto_indent_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->auto_indent_row));

    panel->indent_model = gtk_string_list_new(NULL);
    gtk_string_list_append(panel->indent_model, _("Tabs"));
    gtk_string_list_append(panel->indent_model, _("Spaces"));
    panel->indent_char_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->indent_char_row), _("Character"));
    adw_combo_row_set_model(panel->indent_char_row, G_LIST_MODEL(panel->indent_model));
    adw_combo_row_set_selected(panel->indent_char_row,
                               mt_settings_get_insert_spaces(window->settings) ? 1 : 0);
    g_signal_connect(panel->indent_char_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_properties_indent_char_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->indent_char_row));

    panel->tab_width_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1.0, 16.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->tab_width_row),
                                  _("Spaces Per Tab"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(panel->tab_width_row),
                             mt_settings_get_tab_width(window->settings));
    adw_spin_row_set_digits(panel->tab_width_row, 0);
    g_signal_connect(panel->tab_width_row,
                     "changed",
                     G_CALLBACK(mt_window_properties_tab_width_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->tab_width_row));

    panel->indent_width_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1.0, 16.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->indent_width_row),
                                  _("Spaces Per Indent"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(panel->indent_width_row),
                             mt_settings_get_indent_width(window->settings));
    adw_spin_row_set_digits(panel->indent_width_row, 0);
    g_signal_connect(panel->indent_width_row,
                     "changed",
                     G_CALLBACK(mt_window_properties_indent_width_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->indent_width_row));
    adw_preferences_page_add(page, group);

    /* 统计 */
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Statistics"));
    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Lines"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->lines_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Words"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->words_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Characters, No Spaces"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->chars_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("All Characters"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->all_chars_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));
    adw_preferences_page_add(page, group);

    return panel;
}

/* ---------- 主菜单（学习原版 GNOME Text Editor 的分组汉堡菜单） ---------- */

/* ---------- 主菜单顶部：主题切换与缩放控件（学习原版 GNOME Text Editor） ---------- */

static void
mt_window_menu_theme_toggled(GtkCheckButton *button, gpointer user_data)
{
    MtWindow *window;
    const gchar *mode;
    MtAppearance appearance;

    window = user_data;
    if (window == NULL || window->disposed || !gtk_check_button_get_active(button))
    {
        return;
    }

    mode = g_object_get_data(G_OBJECT(button), "vellum-theme-mode");
    if (g_strcmp0(mode, "light") == 0)
    {
        appearance = MT_APPEARANCE_LIGHT;
    }
    else if (g_strcmp0(mode, "dark") == 0)
    {
        appearance = MT_APPEARANCE_DARK;
    }
    else
    {
        appearance = MT_APPEARANCE_SYSTEM;
    }

    /* 与首选项里的 Appearance 下拉框共用同一状态机，避免重复应用。 */
    if (mt_settings_get_appearance(window->settings) == appearance)
    {
        return;
    }

    mt_timing_log("menu-theme-toggled: %s", mode);
    mt_settings_set_appearance(window->settings, appearance);
    mt_settings_apply_appearance(window->settings);
    mt_window_apply_editor_style(window);
    mt_settings_save(window->settings);
}

static void
mt_window_update_menu_theme_buttons(MtWindow *window)
{
    MtAppearance appearance;

    if (window == NULL || window->menu_theme_follow == NULL)
    {
        return;
    }

    appearance = mt_settings_get_appearance(window->settings);
    gtk_check_button_set_active(window->menu_theme_follow, appearance == MT_APPEARANCE_SYSTEM);
    gtk_check_button_set_active(window->menu_theme_light, appearance == MT_APPEARANCE_LIGHT);
    gtk_check_button_set_active(window->menu_theme_dark, appearance == MT_APPEARANCE_DARK);
}

void
mt_window_update_menu_zoom_label(MtWindow *window)
{
    gchar *text;

    if (window == NULL || window->menu_zoom_label == NULL)
    {
        return;
    }

    text = g_strdup_printf("%d%%",
                           (gint)(mt_settings_get_font_scale(window->settings) * 100.0 + 0.5));
    gtk_button_set_label(window->menu_zoom_label, text);
    g_free(text);
}

static GtkWidget *
mt_window_build_theme_selector(MtWindow *window)
{
    GtkWidget *box;
    GtkCheckButton *follow;
    GtkCheckButton *light;
    GtkCheckButton *dark;

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(box, "vellum-menu-theme");

    follow = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_widget_add_css_class(GTK_WIDGET(follow), "theme-selector");
    gtk_widget_add_css_class(GTK_WIDGET(follow), "follow");
    gtk_widget_set_hexpand(GTK_WIDGET(follow), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(follow), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(follow), _("Follow System Style"));
    g_object_set_data(G_OBJECT(follow), "vellum-theme-mode", (gpointer)"follow");
    g_signal_connect(follow, "toggled", G_CALLBACK(mt_window_menu_theme_toggled), window);

    light = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_widget_add_css_class(GTK_WIDGET(light), "theme-selector");
    gtk_widget_add_css_class(GTK_WIDGET(light), "light");
    gtk_widget_set_hexpand(GTK_WIDGET(light), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(light), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(light), _("Light Style"));
    g_object_set_data(G_OBJECT(light), "vellum-theme-mode", (gpointer)"light");
    g_signal_connect(light, "toggled", G_CALLBACK(mt_window_menu_theme_toggled), window);

    dark = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_widget_add_css_class(GTK_WIDGET(dark), "theme-selector");
    gtk_widget_add_css_class(GTK_WIDGET(dark), "dark");
    gtk_widget_set_hexpand(GTK_WIDGET(dark), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(dark), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(dark), _("Dark Style"));
    g_object_set_data(G_OBJECT(dark), "vellum-theme-mode", (gpointer)"dark");
    g_signal_connect(dark, "toggled", G_CALLBACK(mt_window_menu_theme_toggled), window);

    /* 三个按钮组成单选组；跟随系统为锚点。 */
    gtk_check_button_set_group(light, follow);
    gtk_check_button_set_group(dark, follow);

    window->menu_theme_follow = follow;
    window->menu_theme_light = light;
    window->menu_theme_dark = dark;

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(follow));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(light));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(dark));

    return box;
}

static GtkWidget *
mt_window_build_zoom_box(MtWindow *window)
{
    GtkWidget *box;
    GtkWidget *zoom_out;
    GtkWidget *zoom_in;

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);

    zoom_out = gtk_button_new_from_icon_name("zoom-out-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom_out), "win.zoom-out");
    gtk_widget_add_css_class(zoom_out, "circular");
    gtk_widget_add_css_class(zoom_out, "flat");
    gtk_widget_set_tooltip_text(zoom_out, _("Zoom Out"));

    window->menu_zoom_label = GTK_BUTTON(gtk_button_new_with_label("100%"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(window->menu_zoom_label), "win.zoom-reset");
    gtk_widget_add_css_class(GTK_WIDGET(window->menu_zoom_label), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(window->menu_zoom_label), "pill");
    gtk_widget_set_hexpand(GTK_WIDGET(window->menu_zoom_label), TRUE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(window->menu_zoom_label), _("Reset Zoom"));

    zoom_in = gtk_button_new_from_icon_name("zoom-in-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom_in), "win.zoom-in");
    gtk_widget_add_css_class(zoom_in, "circular");
    gtk_widget_add_css_class(zoom_in, "flat");
    gtk_widget_set_tooltip_text(zoom_in, _("Zoom In"));

    gtk_box_append(GTK_BOX(box), zoom_out);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(window->menu_zoom_label));
    gtk_box_append(GTK_BOX(box), zoom_in);

    return box;
}

static GMenu *
mt_window_build_primary_menu(MtWindow *window)
{
    GMenu *menu;
    GMenu *section;
    GMenuItem *item;

    menu = g_menu_new();

    /* 顶部：自定义主题切换器与缩放控件（由 GtkPopoverMenu 的 custom 子项提供）。 */
    section = g_menu_new();
    item = g_menu_item_new(NULL, NULL);
    g_menu_item_set_attribute(item, "custom", "s", "theme");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(NULL, NULL);
    g_menu_item_set_attribute(item, "custom", "s", "zoom");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_New"), "win.new");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>n");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Save"), "win.save");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>s");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("Save _As…"), "win.save-as");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary><Shift>s");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Close"), "win.close");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>w");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Find…"), "win.find");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>f");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Replace…"), "win.replace");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>h");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Document Properties"), "win.toggle-properties");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary><Shift>i");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Preferences"), "win.preferences");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>comma");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Keyboard Shortcuts"), "win.shortcuts");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Extensions"), "win.extensions");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_About Vellum"), "win.about");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    /* 新手引导入口：welcome 插件加载后动态追加，删除后自动移除。 */
    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    window->welcome_menu_section = section;

    return menu;
}

void
mt_window_sync_plugin_menu(MtWindow *window)
{
    MtPluginManager *manager;
    gboolean loaded;

    if (window == NULL || window->welcome_menu_section == NULL)
    {
        return;
    }

    manager = window->plugin_manager;
    loaded = manager != NULL &&
             mt_plugin_manager_has_plugin(manager, "io.github.vellum.welcome");

    if (loaded && !window->welcome_menu_present)
    {
        GMenuItem *item;

        item = g_menu_item_new(_("Welcome Guide"), "app.show-welcome");
        g_menu_append_item(window->welcome_menu_section, item);
        g_object_unref(item);
        window->welcome_menu_present = TRUE;
    }
    else if (!loaded && window->welcome_menu_present)
    {
        gint n_items;

        n_items = g_menu_model_get_n_items(G_MENU_MODEL(window->welcome_menu_section));
        if (n_items > 0)
        {
            g_menu_remove(window->welcome_menu_section, n_items - 1);
        }
        window->welcome_menu_present = FALSE;
    }
}

/* ---------- 清除历史（行为组里的红色危险操作） ---------- */

static gboolean
mt_window_draft_is_open(MtWindow *window, const gchar *path)
{
    gint count;
    gint index;

    count = adw_tab_view_get_n_pages(window->tab_view);
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document != NULL && document->draft_path != NULL &&
            g_strcmp0(document->draft_path, path) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void
mt_window_clear_history_response(AdwAlertDialog *dialog,
                                 const gchar *response,
                                 gpointer user_data)
{
    MtWindow *window;
    GPtrArray *snapshots;
    guint removed;
    guint index;

    (void)dialog;
    window = user_data;
    if (g_strcmp0(response, "clear") != 0)
    {
        return;
    }

    removed = 0;
    snapshots = mt_document_list_snapshots();
    for (index = 0; index < snapshots->len; index++)
    {
        const gchar *path;

        path = g_ptr_array_index(snapshots, index);
        if (!mt_window_draft_is_open(window, path) && g_remove(path) == 0)
        {
            removed++;
        }
    }
    g_ptr_array_unref(snapshots);

    if (removed > 0)
    {
        gchar *message;

        message = g_strdup_printf(ngettext("Cleared %u draft", "Cleared %u drafts", removed),
                                  removed);
        mt_window_show_toast(window, message);
        g_free(message);
    }
    else
    {
        mt_window_show_toast(window, _("No history to clear"));
    }
}

void
mt_window_clear_history_clicked(AdwActionRow *row, gpointer user_data)
{
    MtWindow *window;
    AdwAlertDialog *dialog;
    GtkWidget *content;
    GtkWidget *label;

    (void)row;
    window = user_data;

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    label = gtk_label_new(_("All recoverable drafts will be deleted permanently. "
                            "Open documents are not affected."));
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(content), label);
    gtk_widget_set_visible(content, TRUE);

    dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Clear History?"), NULL));
    adw_alert_dialog_set_extra_child(dialog, content);
    adw_alert_dialog_add_responses(dialog,
                                   "cancel", _("Cancel"),
                                   "clear", _("Clear"),
                                   NULL);
    adw_alert_dialog_set_response_appearance(dialog, "clear", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(mt_window_clear_history_response), window);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window->window));
}

/* ---------- 首选项：自定义字体开关 ---------- */

void
mt_window_custom_font_toggled(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    MtWindow *window;
    AdwSwitchRow *row;
    AdwActionRow *font_row;
    gboolean active;

    (void)pspec;
    window = user_data;
    row = ADW_SWITCH_ROW(object);
    active = adw_switch_row_get_active(row);
    mt_settings_set_custom_font(window->settings, active);

    font_row = g_object_get_data(G_OBJECT(row), "vellum-font-row");
    if (font_row != NULL)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(font_row), active);
    }
    if (!active && g_strcmp0(mt_settings_get_font_family(window->settings), "Monospace") != 0)
    {
        mt_settings_set_font_family(window->settings, "Monospace");
        if (font_row != NULL)
        {
            adw_action_row_set_subtitle(font_row, "Monospace");
        }
    }
    mt_window_apply_editor_style(window);
    mt_settings_save(window->settings);
}

MtWindow *
mt_window_new(AdwApplication *application, MtSettings *settings)
{

    MtWindow *window;
    GtkWidget *toolbar_view;
    GtkWidget *header_bar;
    GtkWidget *open_button;
    GtkWidget *new_button;
    GtkWidget *menu_button;
    GMenu *menu;
    GtkWidget *content;
    GtkWidget *editor_content;
    GtkWidget *status_bar;
    GtkWidget *find_previous_button;
    GtkWidget *find_next_button;
    GtkWidget *find_close_button;
    GtkWidget *external_text_box;
    GtkWidget *external_title;
    GtkWidget *external_description;
    GtkWidget *external_reload_button;
    GtkWidget *external_close_button;
    GtkWidget *header_title_box;
    GtkWidget *header_title_row;
    GtkWidget *header_subtitle_row;
    GtkWidget *header_title_center;
    GtkWidget *find_navigation_box;
    GtkWidget *replace_mode_button;
    GtkWidget *find_options_button;
    GtkWidget *find_options_popover;
    GtkWidget *search_case_button;
    GtkWidget *replace_all_button;
    GtkEventController *key_controller;
    static const GActionEntry entries[] = {
        { "new", mt_window_action_new, NULL, NULL, NULL, { 0, 0, 0 } },
        { "open", mt_window_action_open, NULL, NULL, NULL, { 0, 0, 0 } },
        { "save", mt_window_action_save, NULL, NULL, NULL, { 0, 0, 0 } },
        { "save-as", mt_window_action_save_as, NULL, NULL, NULL, { 0, 0, 0 } },
        { "close", mt_window_action_close, NULL, NULL, NULL, { 0, 0, 0 } },
        { "find", mt_window_action_find, NULL, NULL, NULL, { 0, 0, 0 } },
        { "replace", mt_window_action_replace, NULL, NULL, NULL, { 0, 0, 0 } },
        { "zoom-in", mt_window_action_zoom_in, NULL, NULL, NULL, { 0, 0, 0 } },
        { "zoom-out", mt_window_action_zoom_out, NULL, NULL, NULL, { 0, 0, 0 } },
        { "zoom-reset", mt_window_action_zoom_reset, NULL, NULL, NULL, { 0, 0, 0 } },
        { "preferences", mt_window_action_preferences, NULL, NULL, NULL, { 0, 0, 0 } },
        { "shortcuts", mt_window_action_shortcuts, NULL, NULL, NULL, { 0, 0, 0 } },
        { "extensions", mt_window_action_extensions, NULL, NULL, NULL, { 0, 0, 0 } },
        { "about", mt_window_action_about, NULL, NULL, NULL, { 0, 0, 0 } },
        { "open-releases", mt_window_action_open_releases, NULL, NULL, NULL, { 0, 0, 0 } },
        { "toggle-properties", mt_window_toggle_properties, NULL, NULL, NULL, { 0, 0, 0 } }
    };

    window = g_new0(MtWindow, 1);
    window->settings = settings;
    window->window = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(application)));
    mt_window_register_source_icon_path(window->window);
    gtk_window_set_icon_name(GTK_WINDOW(window->window), "io.github.vellum.Vellum");
    window->tab_view = ADW_TAB_VIEW(adw_tab_view_new());
    window->tab_bar = ADW_TAB_BAR(adw_tab_bar_new());
    window->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    window->css_provider = gtk_css_provider_new();
    window->display = g_object_ref(gtk_widget_get_display(GTK_WIDGET(window->window)));

    gtk_window_set_default_size(GTK_WINDOW(window->window), 980, 680);
    gtk_widget_set_size_request(GTK_WIDGET(window->window),
                                MT_MIN_WINDOW_WIDTH,
                                MT_MIN_WINDOW_HEIGHT);
    gtk_window_set_title(GTK_WINDOW(window->window), "Vellum");
    adw_tab_bar_set_view(window->tab_bar, window->tab_view);
    adw_tab_view_set_menu_model(window->tab_view, NULL);

    g_action_map_add_action_entries(G_ACTION_MAP(window->window),
                                    entries,
                                    G_N_ELEMENTS(entries),
                                    window);
    key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller,
                     "key-pressed",
                     G_CALLBACK(mt_window_key_pressed),
                     window);
    gtk_widget_add_controller(GTK_WIDGET(window->window), key_controller);

    toolbar_view = adw_toolbar_view_new();
    header_bar = adw_header_bar_new();

    /* 左侧与 GNOME Text Editor 保持同一信息顺序：打开入口在前，
     * 新建标签在后；使用 Vellum 自己的动作和图标资源。 */
    open_button = gtk_button_new();
    {
        GtkWidget *open_content;
        GtkWidget *open_label;
        GtkWidget *open_arrow;

        open_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        open_label = gtk_label_new_with_mnemonic(_("_Open"));
        open_arrow = gtk_image_new_from_icon_name("pan-down-symbolic");
        gtk_box_append(GTK_BOX(open_content), open_label);
        gtk_box_append(GTK_BOX(open_content), open_arrow);
        gtk_button_set_child(GTK_BUTTON(open_button), open_content);
    }
    new_button = gtk_button_new_from_icon_name("tab-new-symbolic");
    gtk_widget_set_tooltip_text(new_button, _("New Tab"));
    gtk_widget_set_tooltip_text(open_button, _("Open File"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(new_button), "win.new");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "win.open");
    adw_header_bar_pack_start(ADW_HEADER_BAR(header_bar), open_button);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header_bar), new_button);

    /* 标题栏中部使用标题、目录和独立的未保存圆点，避免修改文档时
     * 文字横向跳动，同时保持原生 GNOME 的视觉层级。 */
    header_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    header_title_row = gtk_center_box_new();
    header_subtitle_row = gtk_center_box_new();
    header_title_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    window->header_modified_indicator = gtk_label_new("•");
    window->header_title_label = GTK_LABEL(gtk_label_new("Vellum"));
    window->header_subtitle_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(header_title_box, "vellum-header-title");
    gtk_widget_add_css_class(GTK_WIDGET(window->header_title_label), "title");
    gtk_widget_add_css_class(GTK_WIDGET(window->header_subtitle_label), "subtitle");
    gtk_widget_add_css_class(window->header_modified_indicator, "vellum-header-modified");
    gtk_label_set_ellipsize(window->header_title_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_ellipsize(window->header_subtitle_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_visible(window->header_modified_indicator, FALSE);
    gtk_box_append(GTK_BOX(header_title_center), window->header_modified_indicator);
    gtk_box_append(GTK_BOX(header_title_center), GTK_WIDGET(window->header_title_label));
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(header_title_row), header_title_center);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(header_subtitle_row),
                                     GTK_WIDGET(window->header_subtitle_label));
    gtk_box_append(GTK_BOX(header_title_box), header_title_row);
    gtk_box_append(GTK_BOX(header_title_box), header_subtitle_row);
    gtk_widget_set_hexpand(header_title_box, TRUE);
    gtk_widget_set_valign(header_title_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(header_title_box, 12);
    gtk_widget_set_margin_end(header_title_box, 12);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_bar), header_title_box);

    menu = mt_window_build_primary_menu(window);
    window->primary_menu = menu;
    menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(menu_button, _("Main Menu"));
    {
        GtkPopoverMenu *popover;

        popover = GTK_POPOVER_MENU(gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)));
        gtk_popover_menu_add_child(popover,
                                   mt_window_build_theme_selector(window),
                                   "theme");
        gtk_popover_menu_add_child(popover,
                                   mt_window_build_zoom_box(window),
                                   "zoom");
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), GTK_WIDGET(popover));
    }
    mt_window_update_menu_theme_buttons(window);
    mt_window_update_menu_zoom_label(window);
    window->properties_button = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_button_set_icon_name(GTK_BUTTON(window->properties_button), "info-outline-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(window->properties_button),
                                _("Document Properties"));
    gtk_widget_add_css_class(GTK_WIDGET(window->properties_button), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(window->properties_button), "vellum-properties-toggle");
    g_signal_connect(window->properties_button,
                     "toggled",
                     G_CALLBACK(mt_window_properties_toggled),
                     window);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), menu_button);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar),
                            GTK_WIDGET(window->properties_button));

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    window->external_change_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    external_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    external_title = gtk_label_new(_("File changed on disk"));
    external_description = gtk_label_new(_("The file was changed by another program."));
    external_reload_button = gtk_button_new_with_label(_("Discard Changes and Reload"));
    external_close_button = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(window->external_change_bar, "vellum-external-change");
    gtk_widget_add_css_class(external_title, "vellum-external-title");
    gtk_widget_add_css_class(external_description, "vellum-external-description");
    gtk_widget_add_css_class(external_reload_button, "suggested-action");
    gtk_label_set_xalign(GTK_LABEL(external_title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(external_description), 0.0f);
    gtk_widget_set_hexpand(external_text_box, TRUE);
    gtk_widget_set_margin_start(window->external_change_bar, 12);
    gtk_widget_set_margin_end(window->external_change_bar, 8);
    gtk_widget_set_margin_top(window->external_change_bar, 8);
    gtk_widget_set_margin_bottom(window->external_change_bar, 8);
    gtk_widget_set_valign(external_reload_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(external_reload_button, 12);
    gtk_widget_set_margin_end(external_reload_button, 4);
    gtk_widget_set_tooltip_text(external_close_button, _("Dismiss external file change notification"));
    gtk_box_append(GTK_BOX(external_text_box), external_title);
    gtk_box_append(GTK_BOX(external_text_box), external_description);
    gtk_box_append(GTK_BOX(window->external_change_bar), external_text_box);
    gtk_box_append(GTK_BOX(window->external_change_bar), external_reload_button);
    gtk_box_append(GTK_BOX(window->external_change_bar), external_close_button);
    gtk_widget_set_visible(window->external_change_bar, FALSE);
    g_signal_connect(external_reload_button,
                     "clicked",
                     G_CALLBACK(mt_window_external_reload_clicked),
                     window);
    g_signal_connect(external_close_button,
                     "clicked",
                     G_CALLBACK(mt_window_external_change_close_clicked),
                     window);
    gtk_box_append(GTK_BOX(content), window->external_change_bar);
    window->content_paned = GTK_PANED(gtk_paned_new(GTK_ORIENTATION_HORIZONTAL));
    window->sidebar_stack = GTK_STACK(gtk_stack_new());
    window->auxiliary_stack = GTK_STACK(gtk_stack_new());
    window->auxiliary_wrap = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                              MT_PROPERTIES_PANEL_WIDTH);
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                              MT_PROPERTIES_PANEL_WIDTH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                  GTK_WIDGET(window->auxiliary_stack));
    gtk_widget_add_css_class(GTK_WIDGET(window->auxiliary_wrap), "vellum-properties-drawer");

    /* 使用 GNOME 原生 OverlaySplitView：大窗口内固定宽度停靠，小窗口改为右侧覆盖式抽屉。 */
    window->auxiliary_split = ADW_OVERLAY_SPLIT_VIEW(adw_overlay_split_view_new());
    adw_overlay_split_view_set_sidebar_position(window->auxiliary_split, GTK_PACK_END);
    adw_overlay_split_view_set_min_sidebar_width(window->auxiliary_split,
                                                  MT_PROPERTIES_PANEL_WIDTH);
    adw_overlay_split_view_set_max_sidebar_width(window->auxiliary_split,
                                                  MT_PROPERTIES_PANEL_WIDTH);
    adw_overlay_split_view_set_sidebar_width_fraction(window->auxiliary_split, 0.30);
    adw_overlay_split_view_set_pin_sidebar(window->auxiliary_split, TRUE);
    adw_overlay_split_view_set_enable_show_gesture(window->auxiliary_split, TRUE);
    adw_overlay_split_view_set_enable_hide_gesture(window->auxiliary_split, TRUE);
    g_signal_connect(window->auxiliary_split,
                     "notify::width",
                     G_CALLBACK(mt_window_auxiliary_width_changed),
                     window);

    editor_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);


    gtk_stack_set_transition_type(window->sidebar_stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_stack_set_transition_type(window->auxiliary_stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_widget_set_size_request(GTK_WIDGET(window->sidebar_stack), 220, -1);
    window->properties = mt_window_properties_create(window);
    gtk_widget_set_visible(GTK_WIDGET(window->sidebar_stack), FALSE);
    mt_window_auxiliary_visible(window, FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(window->content_paned), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(window->content_paned), TRUE);

    gtk_box_append(GTK_BOX(editor_content), GTK_WIDGET(window->tab_bar));
    gtk_box_append(GTK_BOX(editor_content), GTK_WIDGET(window->tab_view));
    gtk_paned_set_start_child(window->content_paned, GTK_WIDGET(window->sidebar_stack));
    gtk_paned_set_end_child(window->content_paned, GTK_WIDGET(window->auxiliary_split));
    gtk_widget_set_hexpand(editor_content, TRUE);
    adw_overlay_split_view_set_content(window->auxiliary_split, editor_content);
    adw_overlay_split_view_set_sidebar(window->auxiliary_split,
                                       GTK_WIDGET(window->auxiliary_wrap));
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(window->content_paned));

    /* 搜索栏采用与 GNOME Text Editor 相同的两行网格结构：查找、
     * 导航、模式/选项和关闭控件位于第一行；替换控件按需展开到第二行。 */
    window->find_bar = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(window->find_bar), 3);
    gtk_grid_set_row_spacing(GTK_GRID(window->find_bar), 3);
    gtk_widget_add_css_class(window->find_bar, "searchbar");
    gtk_widget_add_css_class(window->find_bar, "toolbar");
    gtk_widget_set_margin_start(window->find_bar, 12);
    gtk_widget_set_margin_end(window->find_bar, 12);
    gtk_widget_set_margin_top(window->find_bar, 6);
    gtk_widget_set_margin_bottom(window->find_bar, 6);
    window->find_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(window->find_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(window->find_revealer), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(window->find_revealer), FALSE);
    window->find_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    window->replace_entry = GTK_ENTRY(gtk_entry_new());
    gtk_search_entry_set_placeholder_text(window->find_entry, _("Find"));
    gtk_entry_set_placeholder_text(window->replace_entry, _("Replace"));
    gtk_entry_set_icon_from_icon_name(window->replace_entry,
                                      GTK_ENTRY_ICON_PRIMARY,
                                      "edit-find-replace-symbolic");
    gtk_widget_set_hexpand(GTK_WIDGET(window->find_entry), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(window->replace_entry), TRUE);
    g_object_set(window->replace_entry, "max-width-chars", 20, NULL);

    find_navigation_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_set_homogeneous(GTK_BOX(find_navigation_box), TRUE);
    find_previous_button = gtk_button_new_from_icon_name("go-up-symbolic");
    find_next_button = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_set_tooltip_text(find_previous_button, _("Previous Match"));
    gtk_widget_set_tooltip_text(find_next_button, _("Next Match"));
    gtk_widget_add_css_class(find_previous_button, "flat");
    gtk_widget_add_css_class(find_next_button, "flat");
    gtk_widget_set_focus_on_click(find_previous_button, FALSE);
    gtk_widget_set_focus_on_click(find_next_button, FALSE);
    gtk_box_append(GTK_BOX(find_navigation_box), find_previous_button);
    gtk_box_append(GTK_BOX(find_navigation_box), find_next_button);

    replace_mode_button = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(replace_mode_button), "edit-find-replace-symbolic");
    window->replace_mode_button = GTK_TOGGLE_BUTTON(replace_mode_button);
    gtk_widget_set_tooltip_text(replace_mode_button, _("Search and Replace"));
    gtk_widget_add_css_class(replace_mode_button, "flat");
    gtk_widget_set_focus_on_click(replace_mode_button, FALSE);

    find_options_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(find_options_button), "emblem-system-symbolic");
    gtk_widget_set_tooltip_text(find_options_button, _("Search Options"));
    gtk_widget_add_css_class(find_options_button, "flat");
    find_options_popover = gtk_popover_new();
    search_case_button = gtk_check_button_new_with_mnemonic(_("_Case Sensitive"));
    gtk_widget_set_margin_start(search_case_button, 12);
    gtk_widget_set_margin_end(search_case_button, 12);
    gtk_widget_set_margin_top(search_case_button, 9);
    gtk_widget_set_margin_bottom(search_case_button, 9);
    gtk_popover_set_child(GTK_POPOVER(find_options_popover), search_case_button);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(find_options_button), find_options_popover);

    window->replace_button = gtk_button_new_with_mnemonic(_("_Replace"));
    replace_all_button = gtk_button_new_with_mnemonic(_("Replace _All"));
    window->replace_all_button = replace_all_button;
    gtk_widget_set_focus_on_click(window->replace_button, FALSE);
    gtk_widget_set_focus_on_click(window->replace_all_button, FALSE);
    mt_window_set_replace_mode(window, FALSE);

    find_close_button = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(find_close_button, _("Close Search"));
    gtk_widget_add_css_class(find_close_button, "flat");
    gtk_widget_add_css_class(find_close_button, "circular");
    gtk_widget_set_focus_on_click(find_close_button, FALSE);

    gtk_grid_attach(GTK_GRID(window->find_bar), GTK_WIDGET(window->find_entry), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), find_navigation_box, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), replace_mode_button, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), find_options_button, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), find_close_button, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), GTK_WIDGET(window->replace_entry), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), window->replace_button, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), window->replace_all_button, 2, 1, 3, 1);
    gtk_revealer_set_child(GTK_REVEALER(window->find_revealer), window->find_bar);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(window->find_revealer));

    status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_bar, "vellum-statusbar");
    window->language_label = GTK_LABEL(gtk_label_new(_("Plain Text")));
    gtk_widget_set_halign(GTK_WIDGET(window->language_label), GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(window->language_label));
    window->position_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_halign(GTK_WIDGET(window->position_label), GTK_ALIGN_END);
    gtk_widget_set_hexpand(GTK_WIDGET(window->position_label), TRUE);
    gtk_label_set_attributes(window->position_label, mt_window_statusbar_attrs());
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(window->position_label));
    gtk_box_append(GTK_BOX(content), status_bar);

    adw_toast_overlay_set_child(window->toast_overlay, content);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), GTK_WIDGET(window->toast_overlay));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header_bar);
    adw_application_window_set_content(window->window, toolbar_view);

    gtk_widget_add_css_class(GTK_WIDGET(window->tab_view), "vellum-tab-view");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(window->window)),
                                               GTK_STYLE_PROVIDER(window->css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    mt_window_apply_font_scale(window);

    window->style_manager_handler_id = g_signal_connect(adw_style_manager_get_default(),
                                                        "notify::dark",
                                                        G_CALLBACK(mt_window_system_dark_changed),
                                                        window);
    g_signal_connect(window->window,
                     "destroy",
                     G_CALLBACK(mt_window_destroyed),
                     window);
    g_signal_connect(window->tab_view,
                     "close-page",
                     G_CALLBACK(mt_window_close_page_requested),
                     window);
    g_signal_connect(window->tab_view,
                     "notify::selected-page",
                     G_CALLBACK(mt_window_page_selected),
                     window);
    g_signal_connect(find_previous_button,
                     "clicked",
                     G_CALLBACK(mt_window_find_previous_clicked),
                     window);
    g_signal_connect(find_next_button,
                     "clicked",
                     G_CALLBACK(mt_window_find_next_clicked),
                     window);
    g_signal_connect(window->replace_button,
                     "clicked",
                     G_CALLBACK(mt_window_replace_clicked),
                     window);
    g_signal_connect(window->replace_all_button,
                     "clicked",
                     G_CALLBACK(mt_window_replace_all_clicked),
                     window);
    g_signal_connect(window->replace_mode_button,
                     "toggled",
                     G_CALLBACK(mt_window_replace_mode_toggled),
                     window);
    g_signal_connect(search_case_button,
                     "toggled",
                     G_CALLBACK(mt_window_search_case_toggled),
                     window);
    g_signal_connect(find_close_button,
                     "clicked",
                     G_CALLBACK(mt_window_hide_find_bar),
                     window);
    g_signal_connect(window->find_entry,
                     "activate",
                     G_CALLBACK(mt_window_find_next_clicked),
                     window);

    if (mt_settings_get_auto_check_updates(window->settings))
    {
        window->auto_update_source_id = g_timeout_add_seconds(5,
                                                              mt_window_auto_update_check_idle,
                                                              window);
    }

    return window;
}

static void
mt_window_destroyed(GtkWidget *widget, gpointer user_data)
{
    MtWindow *window;

    (void)widget;
    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }

    window->disposed = TRUE;
    if (window->snapshot_source_id != 0)
    {
        g_source_remove(window->snapshot_source_id);
        window->snapshot_source_id = 0;
    }
    /* 退出前把每个未保存文档的最新内容写入草稿，保证“加载上次的”。 */
    mt_window_snapshot_all(window);
    if (window->language_source_id != 0)
    {
        g_source_remove(window->language_source_id);
        window->language_source_id = 0;
    }
    if (window->style_manager_handler_id != 0)
    {
        g_signal_handler_disconnect(adw_style_manager_get_default(),
                                    window->style_manager_handler_id);
        window->style_manager_handler_id = 0;
    }
    if (window->auto_update_source_id != 0)
    {
        g_source_remove(window->auto_update_source_id);
        window->auto_update_source_id = 0;
    }
    /* 进行中的自动检查在回调里自行释放；此处仅取消请求避免界面已销毁后弹窗。 */
    if (window->auto_update_check != NULL)
    {
        MtAutoUpdateCheck *check;

        check = window->auto_update_check;
        if (check->cancellable != NULL)
        {
            g_cancellable_cancel(check->cancellable);
        }
    }

    window->tab_view = NULL;
    window->tab_bar = NULL;
    window->toast_overlay = NULL;
    window->find_entry = NULL;
    window->replace_entry = NULL;
    window->find_bar = NULL;
    window->find_revealer = NULL;
    window->sidebar_stack = NULL;
    window->auxiliary_stack = NULL;
    window->auxiliary_wrap = NULL;
    window->content_paned = NULL;
    window->auxiliary_split = NULL;
    window->language_label = NULL;
    window->position_label = NULL;
    window->header_title_label = NULL;
    window->header_subtitle_label = NULL;
    window->header_modified_indicator = NULL;
    window->replace_mode_button = NULL;
    window->replace_button = NULL;
    window->replace_all_button = NULL;
    window->inline_completion_document = NULL;
}

void
mt_window_free(MtWindow *window)
{
    if (window == NULL)
    {
        return;
    }

    mt_scheme_apply_cancel(window);
    if (window->snapshot_source_id != 0)
    {
        g_source_remove(window->snapshot_source_id);
    }

    if (window->language_source_id != 0)
    {
        g_source_remove(window->language_source_id);
    }

    if (window->properties_source_id != 0)
    {
        g_source_remove(window->properties_source_id);
        window->properties_source_id = 0;
    }

    /* 主循环已退出，不会再派发回调，这里兜底释放未完成的自动检查。 */
    if (window->auto_update_check != NULL)
    {
        mt_auto_update_check_free(window->auto_update_check);
        window->auto_update_check = NULL;
    }

    if (window->properties != NULL)
    {
        MtPropertiesPanel *panel;

        panel = window->properties;
        if (panel->indent_model != NULL)
        {
            g_object_unref(panel->indent_model);
        }
        if (panel->newline_model != NULL)
        {
            g_object_unref(panel->newline_model);
        }
        if (panel->type_language_ids != NULL)
        {
            g_ptr_array_unref(panel->type_language_ids);
        }
        if (panel->type_items != NULL)
        {
            g_ptr_array_unref(panel->type_items);
        }
        if (panel->encoding_items != NULL)
        {
            g_ptr_array_unref(panel->encoding_items);
        }
        if (panel->encoding_picker_items != NULL)
        {
            g_ptr_array_unref(panel->encoding_picker_items);
        }
        g_clear_object(&panel->encoding_model);
        g_clear_object(&panel->page);
        g_free(panel);
        window->properties = NULL;
    }

    if (window->style_manager_handler_id != 0)
    {
        g_signal_handler_disconnect(adw_style_manager_get_default(),
                                    window->style_manager_handler_id);
    }

    if (window->css_provider != NULL && window->display != NULL)
    {
        gtk_style_context_remove_provider_for_display(window->display,
                                                      GTK_STYLE_PROVIDER(window->css_provider));
    }

    g_clear_object(&window->css_provider);
    g_clear_object(&window->display);
    g_clear_object(&window->welcome_menu_section);
    g_clear_object(&window->primary_menu);
    g_free(window);
}

GtkWindow *
mt_window_get_gtk_window(MtWindow *window)
{
    if (window == NULL || window->disposed || !GTK_IS_WINDOW(window->window))
    {
        return NULL;
    }

    return GTK_WINDOW(window->window);
}

void
mt_window_set_plugin_manager(MtWindow *window, gpointer plugin_manager)
{
    window->plugin_manager = plugin_manager;
    mt_window_sync_plugin_menu(window);
}

static void
mt_window_add_document(MtWindow *window, MtDocument *document)
{
    GtkWidget *scroll;
    GtkWidget *content;
    AdwTabPage *page;

    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), mt_document_get_view(document));
    content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(content), scroll);
    /* 独立组件在 GtkSourceMap 上绘制半透明可视区域，并把点击/拖拽映射到编辑器滚动位置。 */
    document->overview = mt_overview_map_new(GTK_SOURCE_VIEW(mt_document_get_view(document)));
    gtk_widget_set_overflow(document->overview, GTK_OVERFLOW_VISIBLE);
    gtk_widget_set_size_request(document->overview, MT_OVERVIEW_MAP_WIDTH, -1);
    gtk_widget_add_css_class(document->overview, "vellum-overview-map");
    gtk_widget_set_visible(document->overview,
                           mt_settings_get_show_overview(window->settings));
    gtk_box_append(GTK_BOX(content), document->overview);
    gtk_widget_add_css_class(mt_document_get_view(document), "vellum-editor");
    page = adw_tab_view_append(window->tab_view, content);
    mt_document_set_page(document, page);
    g_object_set_data_full(G_OBJECT(page),
                           MT_DOCUMENT_DATA_KEY,
                           document,
                           (GDestroyNotify)mt_document_free);
    mt_window_update_document_title(document);
    adw_tab_view_set_selected_page(window->tab_view, page);

    g_object_set_data(G_OBJECT(mt_document_get_buffer(document)), "vellum-window", window);
    g_signal_connect(mt_document_get_buffer(document),
                     "modified-changed",
                     G_CALLBACK(mt_window_buffer_modified_changed),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "changed",
                     G_CALLBACK(mt_window_buffer_changed),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "insert-text",
                     G_CALLBACK(mt_window_buffer_inserted),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "delete-range",
                     G_CALLBACK(mt_window_buffer_deleted),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "mark-set",
                     G_CALLBACK(mt_window_cursor_moved),
                     window);
    {
        GtkAdjustment *vadjustment;
        GtkAdjustment *hadjustment;

        /* 幽灵补全的覆盖层锚定在固定坐标：滚动后位置必然错位，
         * 必须立即移除，否则就像残影一样留在原来的行上。 */
        vadjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(mt_document_get_view(document)));
        if (vadjustment != NULL)
        {
            g_signal_connect(vadjustment,
                             "value-changed",
                             G_CALLBACK(mt_window_editor_scrolled),
                             window);
        }
        hadjustment = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(mt_document_get_view(document)));
        if (hadjustment != NULL)
        {
            g_signal_connect(hadjustment,
                             "value-changed",
                             G_CALLBACK(mt_window_editor_scrolled),
                             window);
        }
    }
    gtk_widget_grab_focus(mt_document_get_view(document));
    mt_window_apply_source_scheme(window);
    mt_window_apply_editor_preferences(window);
    mt_window_update_position(window);
    mt_window_update_language_label(window);
    mt_window_update_header_title(window);
    mt_window_properties_refresh(window);
}

void
mt_window_new_document(MtWindow *window)
{
    MtDocument *document;

    document = mt_document_new();
    mt_window_add_document(window, document);
}

void
mt_window_open_files(MtWindow *window, GListModel *files)
{
    guint index;
    guint count;

    count = g_list_model_get_n_items(files);

    for (index = 0; index < count; index++)
    {
        GFile *file;

        file = g_list_model_get_item(files, index);
        mt_window_open_file(window, file);
        g_object_unref(file);
    }
}

void
mt_window_open_file(MtWindow *window, GFile *file)
{
    MtDocument *document;
    MtFileRequest *request;

    document = mt_document_new();
    mt_window_add_document(window, document);
    request = mt_file_request_new(window, document, NULL);
    mt_document_load_async(document, file, mt_window_document_load_finished, request);
}

guint
mt_window_restore_snapshots(MtWindow *window)
{
    GPtrArray *snapshots;
    guint index;
    guint restored;

    snapshots = mt_document_list_snapshots();
    restored = 0;

    for (index = 0; index < snapshots->len; index++)
    {
        const gchar *path;
        MtDocument *document;
        GError *error;

        path = g_ptr_array_index(snapshots, index);
        document = mt_document_new();
        error = NULL;

        if (mt_document_restore_snapshot(document, path, &error))
        {
            GtkTextBuffer *buffer;
            GtkTextIter start;
            GtkTextIter end;

            buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            if (gtk_text_iter_equal(&start, &end))
            {
                /* 空草稿没有恢复价值：删除文件并跳过，避免残留垃圾。 */
                g_remove(path);
                mt_document_free(document);
            }
            else
            {
                g_free(document->draft_path);
                document->draft_path = g_strdup(path);
                mt_window_add_document(window, document);
                restored++;
            }
        }
        else
        {
            g_warning("Unable to restore draft snapshot: %s", error->message);
            g_clear_error(&error);
            mt_document_free(document);
        }
    }

    g_ptr_array_unref(snapshots);

    if (restored > 0)
    {
        gchar *message;

        message = g_strdup_printf(ngettext("Recovered %u draft", "Recovered %u drafts", restored), restored);
        mt_window_show_toast(window, message);
        g_free(message);
    }

    return restored;
}

MtDocument *
mt_window_get_current_document(MtWindow *window)
{
    AdwTabPage *page;

    if (window == NULL || window->disposed || !ADW_IS_TAB_VIEW(window->tab_view))
    {
        return NULL;
    }

    page = adw_tab_view_get_selected_page(window->tab_view);
    if (page == NULL)
    {
        return NULL;
    }

    return g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
}

void
mt_window_insert_text(MtWindow *window, const gchar *text)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter iter;

    if (window == NULL || window->disposed || text == NULL)
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        mt_window_new_document(window);
        document = mt_window_get_current_document(window);
    }

    /* 所有扩展插入都归为一次撤销；若插入来自其他动作，先清掉可能过期的候选覆盖层。 */
    if (window->inline_completion_document == document)
    {
        mt_window_clear_inline_completion(window);
    }
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_insert(buffer, &iter, text, -1);
    gtk_text_buffer_end_user_action(buffer);
}

void
mt_window_clear_inline_completion(MtWindow *window)
{
    MtDocument *document;

    if (window == NULL || window->disposed)
    {
        return;
    }

    document = window->inline_completion_document;
    window->inline_completion_document = NULL;
    if (document == NULL || document->inline_completion_label == NULL)
    {
        return;
    }

    /* GTK 4.22 中 gtk_text_view_add_overlay 添加的子项没有公开的移除 API：
     * gtk_text_view_remove 只处理 anchored children，直接 unparent 会留下
     * center_child 内部 overlays 队列的悬挂条目，重排时触发 snapshot_child
     * 断言。因此“清除”= 隐藏并复用同一个 label，视图销毁时自动释放。 */
    gtk_widget_set_visible(document->inline_completion_label, FALSE);
}

void
mt_window_show_inline_completion(MtWindow *window, const gchar *text)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter cursor;
    GtkWidget *view;
    GdkRectangle rectangle;
    gint x;
    gint y;
    gchar **lines;
    gchar *preview;

    if (window == NULL || window->disposed || text == NULL || *text == '\0')
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    view = mt_document_get_view(document);
    if (!GTK_IS_TEXT_VIEW(view) || gtk_widget_get_root(view) == NULL)
    {
        return;
    }

    mt_window_clear_inline_completion(window);
    lines = g_strsplit(text, "\n", 2);
    preview = g_strdup(lines[0] != NULL ? lines[0] : "");
    g_strfreev(lines);
    if (*preview == '\0')
    {
        g_free(preview);
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer,
                                     &cursor,
                                     gtk_text_buffer_get_insert(buffer));
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view),
                                     &cursor,
                                     &rectangle);
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view),
                                          GTK_TEXT_WINDOW_TEXT,
                                          rectangle.x,
                                          rectangle.y,
                                          &x,
                                          &y);
    if (document->inline_completion_label == NULL)
    {
        document->inline_completion_label = gtk_label_new(preview);
        gtk_widget_add_css_class(document->inline_completion_label,
                                 "vellum-inline-completion");
        gtk_label_set_xalign(GTK_LABEL(document->inline_completion_label), 0.0f);
        gtk_text_view_add_overlay(GTK_TEXT_VIEW(view),
                                   document->inline_completion_label,
                                   x + rectangle.width,
                                   y);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(document->inline_completion_label), preview);
        gtk_text_view_move_overlay(GTK_TEXT_VIEW(view),
                                   document->inline_completion_label,
                                   x + rectangle.width,
                                   y);
        gtk_widget_set_visible(document->inline_completion_label, TRUE);
    }
    window->inline_completion_document = document;
    g_free(preview);
}

gchar *
mt_window_get_text_after_cursor(MtWindow *window)
{
    MtDocument *document;
    GtkTextIter cursor;
    GtkTextIter end;

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return g_strdup("");
    }

    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                     &cursor,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(mt_document_get_buffer(document))));
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(mt_document_get_buffer(document)), &end);
    return gtk_text_buffer_get_text(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                    &cursor,
                                    &end,
                                    TRUE);
}

void
mt_window_show_toast(MtWindow *window, const gchar *message)
{
    AdwToast *toast;

    if (window == NULL || window->disposed || message == NULL ||
        !ADW_IS_TOAST_OVERLAY(window->toast_overlay))
    {
        return;
    }

    toast = adw_toast_new(message);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);
}

static GtkStack *
mt_window_get_panel_stack(MtWindow *window, MtPluginPanelLocation location)
{
    return location == MT_PLUGIN_PANEL_SIDEBAR ?
           window->sidebar_stack : window->auxiliary_stack;
}

gchar *
mt_window_get_current_file_path(MtWindow *window)
{
    MtDocument *document;
    GFile *file;

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return NULL;
    }

    file = mt_document_get_file(document);
    return file != NULL ? g_file_get_path(file) : NULL;
}

void
mt_window_open_file_path(MtWindow *window, const gchar *path)
{
    GFile *file;

    if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        return;
    }

    file = g_file_new_for_path(path);
    mt_window_open_file(window, file);
    g_object_unref(file);
}

static void
mt_window_save_and_close_finished(GObject *source,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
    MtFileRequest *request;
    GError *error;

    (void)source;
    request = user_data;
    error = NULL;
    if (mt_document_save_finish(request->document, result, &error))
    {
        mt_window_update_document_title(request->document);
        if (!request->window->disposed && ADW_IS_TAB_VIEW(request->window->tab_view))
        {
            adw_tab_view_close_page(request->window->tab_view, request->page);
        }
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_file_request_free(request);
}

static void
mt_window_paste_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtWindow *window;
    gchar *text;
    GError *error;

    window = user_data;
    error = NULL;
    text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, &error);
    if (text != NULL && *text != '\0')
    {
        mt_window_insert_text(window, text);
    }
    g_free(text);
    g_clear_error(&error);
}

gboolean
mt_window_run_editor_command(MtWindow *window,
                             MtPluginEditorCommand command)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter cursor;
    GtkTextIter target;
    gboolean moved;

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return FALSE;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    target = cursor;
    moved = FALSE;

    switch (command)
    {
        case MT_PLUGIN_EDITOR_MOVE_LEFT:
            moved = gtk_text_iter_backward_char(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_RIGHT:
            moved = gtk_text_iter_forward_char(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_UP:
            moved = gtk_text_iter_backward_line(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_DOWN:
            moved = gtk_text_iter_forward_line(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_LINE_START:
            gtk_text_iter_set_line_offset(&target, 0);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_LINE_END:
            gtk_text_iter_forward_to_line_end(&target);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_DOCUMENT_START:
            gtk_text_buffer_get_start_iter(buffer, &target);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_DOCUMENT_END:
            gtk_text_buffer_get_end_iter(buffer, &target);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_WORD_FORWARD:
            moved = gtk_text_iter_forward_word_end(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_WORD_BACKWARD:
            moved = gtk_text_iter_backward_word_start(&target);
            break;
        case MT_PLUGIN_EDITOR_DELETE_FORWARD_CHAR:
            if (gtk_text_iter_forward_char(&target))
            {
                gtk_text_buffer_delete(buffer, &cursor, &target);
                return TRUE;
            }
            return FALSE;
        case MT_PLUGIN_EDITOR_DELETE_CURRENT_LINE:
            gtk_text_iter_set_line_offset(&cursor, 0);
            target = cursor;
            gtk_text_iter_forward_to_line_end(&target);
            if (!gtk_text_iter_is_end(&target))
            {
                gtk_text_iter_forward_char(&target);
            }
            gtk_text_buffer_delete(buffer, &cursor, &target);
            return TRUE;
        case MT_PLUGIN_EDITOR_YANK_LINE:
        {
            GtkTextIter line_start;
            GtkTextIter line_end;
            gchar *line_text;
            GdkClipboard *clipboard;

            gtk_text_iter_set_line_offset(&cursor, 0);
            line_start = cursor;
            line_end = line_start;
            gtk_text_iter_forward_to_line_end(&line_end);
            line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);
            clipboard = gdk_display_get_clipboard(window->display);
            gdk_clipboard_set_text(clipboard, line_text);
            g_free(line_text);
            return TRUE;
        }
        case MT_PLUGIN_EDITOR_CHANGE_LINE:
        {
            GtkTextIter line_start;
            GtkTextIter line_end;

            gtk_text_iter_set_line_offset(&cursor, 0);
            line_start = cursor;
            line_end = line_start;
            gtk_text_iter_forward_to_line_end(&line_end);
            gtk_text_buffer_begin_user_action(buffer);
            gtk_text_buffer_delete(buffer, &line_start, &line_end);
            gtk_text_buffer_end_user_action(buffer);
            return TRUE;
        }
        case MT_PLUGIN_EDITOR_PASTE:
        {
            GdkClipboard *clipboard;

            clipboard = gdk_display_get_clipboard(window->display);
            gdk_clipboard_read_text_async(clipboard,
                                          NULL,
                                          mt_window_paste_finished,
                                          window);
            return TRUE;
        }
        case MT_PLUGIN_EDITOR_UNDO:
            if (gtk_text_buffer_get_can_undo(buffer))
            {
                gtk_text_buffer_undo(buffer);
                return TRUE;
            }
            return FALSE;
        case MT_PLUGIN_EDITOR_REDO:
            if (gtk_text_buffer_get_can_redo(buffer))
            {
                gtk_text_buffer_redo(buffer);
                return TRUE;
            }
            return FALSE;
        case MT_PLUGIN_EDITOR_SAVE:
            mt_window_action_save(NULL, NULL, window);
            return TRUE;
        case MT_PLUGIN_EDITOR_SAVE_AND_CLOSE:
            if (mt_document_is_untitled(document))
            {
                mt_window_show_toast(window, _("Save an untitled document with :w before closing"));
                return FALSE;
            }
            if (mt_document_is_saving(document))
            {
                mt_window_show_toast(window, _("Saving is already in progress"));
                return FALSE;
            }
            {
                MtFileRequest *request;

                request = mt_file_request_new(window, document, NULL);
                mt_document_save_async(document,
                                       NULL,
                                       mt_window_save_and_close_finished,
                                       request);
                mt_window_show_toast(window, _("Saving before closing…"));
                return TRUE;
            }
        case MT_PLUGIN_EDITOR_CLOSE:
            mt_window_action_close(NULL, NULL, window);
            return TRUE;
        case MT_PLUGIN_EDITOR_FORCE_CLOSE:
            gtk_text_buffer_set_modified(buffer, FALSE);
            mt_document_remove_snapshot(document);
            if (document->page != NULL)
            {
                adw_tab_view_close_page(window->tab_view, document->page);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }

    if (moved)
    {
        gtk_text_buffer_place_cursor(buffer, &target);
    }

    return moved;
}

void
mt_window_set_plugin_panel(MtWindow *window,
                           const gchar *id,
                           MtPluginPanelLocation location,
                           GtkWidget *panel)
{
    GtkStack *stack;
    GtkWidget *existing;

    if (window == NULL || window->disposed || id == NULL || panel == NULL)
    {
        return;
    }

    stack = mt_window_get_panel_stack(window, location);
    if (!GTK_IS_STACK(stack))
    {
        return;
    }
    existing = gtk_stack_get_child_by_name(stack, id);
    if (existing != NULL && existing != panel)
    {
        gtk_stack_remove(stack, existing);
    }

    if (gtk_widget_get_parent(panel) == NULL)
    {
        gtk_stack_add_named(stack, panel, id);
    }
    gtk_stack_set_visible_child_name(stack, id);
    if (stack == window->auxiliary_stack)
    {
        mt_window_auxiliary_visible(window, TRUE);
    }
    else
    {
        gtk_widget_set_visible(GTK_WIDGET(stack), TRUE);
    }

    if (location == MT_PLUGIN_PANEL_SIDEBAR)
    {
        gtk_paned_set_position(window->content_paned, 240);
    }
}

void
mt_window_hide_plugin_panel(MtWindow *window,
                            const gchar *id,
                            MtPluginPanelLocation location)
{
    GtkStack *stack;
    GtkWidget *panel;

    if (window == NULL || window->disposed || id == NULL)
    {
        return;
    }

    stack = mt_window_get_panel_stack(window, location);
    if (!GTK_IS_STACK(stack))
    {
        return;
    }
    panel = gtk_stack_get_child_by_name(stack, id);
    if (panel != NULL)
    {
        gtk_stack_remove(stack, panel);
    }

    if (g_list_model_get_n_items(G_LIST_MODEL(gtk_stack_get_pages(stack))) == 0)
    {
        if (stack == window->auxiliary_stack)
        {
            mt_window_auxiliary_visible(window, FALSE);
        }
        else
        {
            gtk_widget_set_visible(GTK_WIDGET(stack), FALSE);
        }
    }
}

void
mt_window_action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    mt_window_new_document(user_data);
}

void
mt_window_action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    GtkFileDialog *dialog;
    MtFileRequest *request;

    (void)action;
    (void)parameter;

    window = user_data;
    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Open Files"));
    request = mt_file_request_new(window, NULL, dialog);
    gtk_file_dialog_open_multiple(dialog,
                                  GTK_WINDOW(window->window),
                                  NULL,
                                  mt_window_open_dialog_finished,
                                  request);
    g_object_unref(dialog);
}

void
mt_window_action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    MtFileRequest *request;

    (void)action;
    (void)parameter;

    window = user_data;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    if (mt_document_is_saving(document))
    {
        mt_window_show_toast(window, _("Saving is already in progress"));
        return;
    }

    if (mt_document_is_untitled(document))
    {
        mt_window_action_save_as(action, parameter, user_data);
        return;
    }

    request = mt_file_request_new(window, document, NULL);
    mt_document_save_async(document, NULL, mt_window_document_save_finished, request);
    mt_window_show_toast(window, _("Saving…"));
}

void
mt_window_action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GtkFileDialog *dialog;
    MtFileRequest *request;

    (void)action;
    (void)parameter;

    window = user_data;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    if (mt_document_is_saving(document))
    {
        mt_window_show_toast(window, _("Saving is already in progress"));
        return;
    }

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Save As"));
    gtk_file_dialog_set_initial_name(dialog, mt_document_get_display_name(document));
    request = mt_file_request_new(window, document, dialog);
    gtk_file_dialog_save(dialog,
                         GTK_WINDOW(window->window),
                         NULL,
                         mt_window_save_dialog_finished,
                         request);
    g_object_unref(dialog);
}

void
mt_window_action_close(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    AdwTabPage *page;

    (void)action;
    (void)parameter;

    window = user_data;
    page = adw_tab_view_get_selected_page(window->tab_view);

    if (page != NULL)
    {
        adw_tab_view_close_page(window->tab_view, page);
    }
}

void
mt_window_action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    mt_window_show_find_bar(user_data, FALSE);
}

void
mt_window_action_replace(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    mt_window_show_find_bar(user_data, TRUE);
}

void
mt_window_action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    gdouble scale;

    (void)action;
    (void)parameter;

    window = user_data;
    scale = mt_settings_get_font_scale(window->settings);
    mt_settings_set_font_scale(window->settings, scale + 0.1);
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Zoomed in"));
}

void
mt_window_action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    gdouble scale;

    (void)action;
    (void)parameter;

    window = user_data;
    scale = mt_settings_get_font_scale(window->settings);
    mt_settings_set_font_scale(window->settings, scale - 0.1);
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Zoomed out"));
}

void
mt_window_action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;

    (void)action;
    (void)parameter;

    window = user_data;
    mt_settings_set_font_scale(window->settings, 1.0);
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Zoom reset to 100%"));
}

static const gchar *
mt_window_extension_display_name(const MtPluginInfo *info)
{
    if (info == NULL || info->id == NULL)
    {
        return "";
    }
    if (g_str_equal(info->id, "io.github.vellum.ai-completion")) return _("AI Code Assistant");
    if (g_str_equal(info->id, "io.github.vellum.timestamp")) return _("Timestamp");
    if (g_str_equal(info->id, "io.github.vellum.document-statistics")) return _("Document Statistics");
    if (g_str_equal(info->id, "io.github.vellum.link-check")) return _("Link Check");
    if (g_str_equal(info->id, "io.github.vellum.project-sidebar")) return _("Project Sidebar");
    if (g_str_equal(info->id, "io.github.vellum.build-run")) return _("Build and Run");
    if (g_str_equal(info->id, "io.github.vellum.vim-mode")) return _("Vi Mode");
    if (g_str_equal(info->id, "io.github.vellum.screenshot")) return _("Editor Screenshot");
    if (g_str_equal(info->id, "io.github.vellum.welcome")) return _("Welcome Guide");
    return info->name != NULL ? info->name : "";
}

static const gchar *
mt_window_extension_display_description(const MtPluginInfo *info)
{
    if (info == NULL || info->id == NULL)
    {
        return "";
    }
    if (g_str_equal(info->id, "io.github.vellum.ai-completion")) return _("Inline completion and configurable code summaries through your AI service");
    if (g_str_equal(info->id, "io.github.vellum.timestamp")) return _("Insert the current local date and time");
    if (g_str_equal(info->id, "io.github.vellum.document-statistics")) return _("Show character, word and line counts for the current document");
    if (g_str_equal(info->id, "io.github.vellum.link-check")) return _("Check HTTP and HTTPS links in the current document");
    if (g_str_equal(info->id, "io.github.vellum.project-sidebar")) return _("Browse an explicitly selected project directory");
    if (g_str_equal(info->id, "io.github.vellum.build-run")) return _("Build or run user-configured commands in a separate tool window");
    if (g_str_equal(info->id, "io.github.vellum.vim-mode")) return _("Basic modal editing commands for Vi users");
    if (g_str_equal(info->id, "io.github.vellum.screenshot")) return _("Export the current editor content as an image");
    if (g_str_equal(info->id, "io.github.vellum.welcome")) return _("Interactive first-run guide with extension selection");
    return info->description != NULL ? info->description : "";
}

static void
mt_window_set_plain_row_title(AdwPreferencesRow *row, const gchar *text)
{
    gchar *escaped;

    escaped = g_markup_escape_text(text != NULL ? text : "", -1);
    adw_preferences_row_set_title(row, escaped);
    g_free(escaped);
}

static void
mt_window_set_plain_action_subtitle(AdwActionRow *row, const gchar *text)
{
    gchar *escaped;

    escaped = g_markup_escape_text(text != NULL ? text : "", -1);
    adw_action_row_set_subtitle(row, escaped);
    g_free(escaped);
}

static gboolean
mt_window_extension_enabled_state_set(GtkSwitch *toggle,
                                      gboolean enabled,
                                      gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    guint index;
    GError *error;

    window = user_data;
    manager = window->plugin_manager;
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(toggle), "vellum-extension-index"));
    error = NULL;
    if (manager != NULL && mt_plugin_manager_set_enabled(manager, index, enabled, &error))
    {
        return FALSE;
    }

    if (error != NULL)
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to change extension state: %s"), error->message);
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    return TRUE;
}

/* —— 扩展市场：核心内置模块，浏览多个扩展源并安装/卸载 —— */

static void
mt_window_reopen_extensions(MtWindow *window)
{
    if (window->extensions_window != NULL && GTK_IS_WIDGET(window->extensions_window))
    {
        gtk_window_destroy(GTK_WINDOW(window->extensions_window));
        window->extensions_window = NULL;
    }
    mt_window_action_extensions(NULL, NULL, window);
}

static void
mt_window_market_refresh_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    GError *error;

    (void)source;
    window = user_data;
    manager = window->plugin_manager;
    error = NULL;
    if (mt_plugin_manager_marketplace_refresh_finish(manager, result, &error))
    {
        mt_window_show_toast(window, _("Extension catalog refreshed"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to refresh extension catalog: %s"),
                                  error != NULL ? error->message : _("Unknown error"));
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_window_reopen_extensions(window);
}

static void
mt_window_market_refresh_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;

    (void)button;
    window = user_data;
    manager = window->plugin_manager;
    mt_plugin_manager_marketplace_refresh_async(manager, NULL,
                                                mt_window_market_refresh_ready,
                                                window);
}

static void
mt_window_market_install_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    GError *error;

    (void)source;
    window = user_data;
    manager = window->plugin_manager;
    error = NULL;
    if (mt_plugin_manager_marketplace_install_finish(manager, result, &error))
    {
        mt_window_show_toast(window, _("Extension installed from the marketplace"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to install extension: %s"),
                                  error != NULL ? error->message : _("Unknown error"));
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_window_reopen_extensions(window);
}

static void
mt_window_market_install_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    MtMarketplaceEntry *entry;
    gboolean prefer_source;

    window = user_data;
    manager = window->plugin_manager;
    entry = g_object_get_data(G_OBJECT(button), "vellum-market-entry");
    if (entry == NULL)
    {
        return;
    }
    prefer_source = g_object_get_data(G_OBJECT(button), "vellum-market-source") != NULL;
    mt_plugin_manager_marketplace_install_async(manager, entry, prefer_source, NULL,
                                                mt_window_market_install_ready,
                                                window);
}

static void
mt_window_market_uninstall_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    MtMarketplaceEntry *entry;
    GError *error;

    window = user_data;
    manager = window->plugin_manager;
    entry = g_object_get_data(G_OBJECT(button), "vellum-market-entry");
    if (entry == NULL)
    {
        return;
    }
    error = NULL;
    if (mt_plugin_manager_marketplace_uninstall(manager, entry, &error))
    {
        mt_window_show_toast(window, _("Extension uninstalled"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to uninstall extension: %s"),
                                  error != NULL ? error->message : _("Unknown error"));
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_window_reopen_extensions(window);
}

void
mt_window_action_extensions(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    AdwPreferencesWindow *extensions_window;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    GtkWidget *import_button;
    guint count;
    guint index;

    (void)action;
    (void)parameter;

    window = user_data;
    manager = window->plugin_manager;
    if (window->extensions_window != NULL && GTK_IS_WIDGET(window->extensions_window))
    {
        gtk_window_destroy(GTK_WINDOW(window->extensions_window));
        window->extensions_window = NULL;
    }
    extensions_window = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    window->extensions_window = GTK_WIDGET(extensions_window);
    gtk_window_set_transient_for(GTK_WINDOW(extensions_window), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(extensions_window), TRUE);
    gtk_window_set_title(GTK_WINDOW(extensions_window), _("Extensions"));
    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Loaded Extensions"));
    if (manager == NULL)
    {
        adw_preferences_group_set_description(group,
                                              _("Extensions are disabled. Enable them in Preferences and restart Vellum to load or manage extensions."));
    }
    else
    {
        adw_preferences_group_set_description(group,
                                              _("Extensions can be disabled without deleting them. Any extension can be removed: user extensions are deleted from disk, built-in extensions are hidden until vellum is reinstalled. Native modules and source packages must come from trusted sources."));
    }
    import_button = gtk_button_new_with_label(_("Import"));
    gtk_widget_set_valign(import_button, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(import_button, manager != NULL);
    adw_preferences_group_set_header_suffix(group, import_button);
    g_signal_connect(import_button,
                     "clicked",
                     G_CALLBACK(mt_window_extension_import_clicked),
                     window);

    count = manager != NULL ? mt_plugin_manager_get_count(manager) : 0;
    if (count == 0)
    {
        AdwActionRow *row;

        row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                      manager == NULL ?
                                      _("Extension loading is disabled for this session") :
                                      _("No extensions are currently loaded"));
        adw_preferences_group_add(group, GTK_WIDGET(row));
    }

    for (index = 0; index < count; index++)
    {
        const MtPluginInfo *info;
        AdwActionRow *row;
        GtkWidget *enabled_switch;
        GtkWidget *export_button;
        GtkWidget *configure_button;
        GtkWidget *delete_button;
        GtkWidget *action_box;
        gchar *subtitle;

        info = mt_plugin_manager_get_info(manager, index);
        if (info == NULL)
        {
            continue;
        }

        row = ADW_ACTION_ROW(adw_action_row_new());
        mt_window_set_plain_row_title(ADW_PREFERENCES_ROW(row),
                                      mt_window_extension_display_name(info));
        subtitle = g_strdup_printf("%s · %s",
                                   mt_window_extension_display_description(info),
                                   info->version != NULL ? info->version : "");
        mt_window_set_plain_action_subtitle(row, subtitle);
        action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_valign(action_box, GTK_ALIGN_CENTER);
        enabled_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(enabled_switch),
                              mt_plugin_manager_is_enabled(manager, index));
        g_object_set_data(G_OBJECT(enabled_switch),
                          "vellum-extension-index",
                          GUINT_TO_POINTER(index));
        g_signal_connect(enabled_switch,
                         "state-set",
                         G_CALLBACK(mt_window_extension_enabled_state_set),
                         window);
        gtk_widget_set_valign(enabled_switch, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(enabled_switch, _("Enable extension"));
        gtk_box_append(GTK_BOX(action_box), enabled_switch);
        export_button = gtk_button_new_from_icon_name("document-save-symbolic");
        gtk_widget_add_css_class(export_button, "flat");
        gtk_widget_set_valign(export_button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(export_button, _("Export extension"));
        g_object_set_data(G_OBJECT(export_button),
                          "vellum-extension-index",
                          GUINT_TO_POINTER(index));
        g_signal_connect(export_button,
                         "clicked",
                         G_CALLBACK(mt_window_extension_export_clicked),
                         window);
        gtk_box_append(GTK_BOX(action_box), export_button);

        if (mt_plugin_manager_has_configure(manager, index))
        {
            configure_button = gtk_button_new_from_icon_name("emblem-system-symbolic");
            gtk_widget_add_css_class(configure_button, "flat");
            gtk_widget_set_valign(configure_button, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(configure_button, _("Configure extension"));
            gtk_widget_set_sensitive(configure_button,
                                     mt_plugin_manager_is_enabled(manager, index));
            g_object_set_data(G_OBJECT(configure_button),
                              "vellum-extension-index",
                              GUINT_TO_POINTER(index));
            g_signal_connect(configure_button,
                             "clicked",
                             G_CALLBACK(mt_window_extension_configure_clicked),
                             window);
            gtk_box_append(GTK_BOX(action_box), configure_button);
        }

        delete_button = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(delete_button, "flat");
        gtk_widget_add_css_class(delete_button, "destructive-action");
        gtk_widget_set_valign(delete_button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(delete_button, _("Delete extension"));
        g_object_set_data(G_OBJECT(delete_button),
                          "vellum-extension-index",
                          GUINT_TO_POINTER(index));
        g_signal_connect(delete_button,
                         "clicked",
                         G_CALLBACK(mt_window_extension_delete_clicked),
                         window);
        gtk_box_append(GTK_BOX(action_box), delete_button);

        adw_action_row_add_suffix(row, action_box);
        adw_preferences_group_add(group, GTK_WIDGET(row));
        g_free(subtitle);
    }

    adw_preferences_page_add(page, group);

    {
        /* 扩展市场：核心内置模块，不可卸载；列出多个扩展源的目录。 */
        AdwPreferencesGroup *market_group;
        GPtrArray *marketplace;
        GtkWidget *refresh_button;
        guint market_index;

        market_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        adw_preferences_group_set_title(market_group, _("Extension Marketplace"));
        adw_preferences_group_set_description(market_group,
                                              _("Browse extensions from the configured sources (GitHub release by default). Binary and source packages are both offered; source packages need make, cc, pkg-config and the GTK/GLib development packages listed in the package."));
        refresh_button = gtk_button_new_with_label(_("Refresh"));
        gtk_widget_set_valign(refresh_button, GTK_ALIGN_CENTER);
        adw_preferences_group_set_header_suffix(market_group, refresh_button);
        g_signal_connect(refresh_button,
                         "clicked",
                         G_CALLBACK(mt_window_market_refresh_clicked),
                         window);

        marketplace = mt_plugin_manager_get_marketplace(manager);
        if (marketplace == NULL || marketplace->len == 0)
        {
            AdwActionRow *row;

            row = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                          _("Extension catalog not loaded yet"));
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                        _("Press Refresh to download the extension catalog from the configured sources."));
            adw_preferences_group_add(market_group, GTK_WIDGET(row));
        }
        for (market_index = 0;
             marketplace != NULL && market_index < marketplace->len;
             market_index++)
        {
            MtMarketplaceEntry *entry;
            AdwActionRow *row;
            GtkWidget *button;
            GtkWidget *source_button;
            GtkWidget *action_box;
            gchar *subtitle;
            gboolean installed;

            entry = g_ptr_array_index(marketplace, market_index);
            installed = mt_plugin_manager_has_plugin(manager, entry->id);
            row = ADW_ACTION_ROW(adw_action_row_new());
            mt_window_set_plain_row_title(ADW_PREFERENCES_ROW(row),
                                          entry->name != NULL ? entry->name : entry->id);
            subtitle = g_strdup_printf("%s · %s%s",
                                       entry->description != NULL ? entry->description : "",
                                       entry->version != NULL ? entry->version : "",
                                       (entry->source != NULL && *entry->source != '\0') ?
                                       _(" · source available") : "");
            mt_window_set_plain_action_subtitle(row, subtitle);
            g_free(subtitle);

            action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_widget_set_valign(action_box, GTK_ALIGN_CENTER);
            button = gtk_button_new_with_label(installed ? _("Uninstall") : _("Install"));
            gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
            gtk_widget_add_css_class(button, installed ? "destructive-action" : "suggested-action");
            g_object_set_data(G_OBJECT(button), "vellum-market-entry", entry);
            if (installed)
            {
                g_signal_connect(button,
                                 "clicked",
                                 G_CALLBACK(mt_window_market_uninstall_clicked),
                                 window);
            }
            else
            {
                g_signal_connect(button,
                                 "clicked",
                                 G_CALLBACK(mt_window_market_install_clicked),
                                 window);
            }
            gtk_box_append(GTK_BOX(action_box), button);

            if (!installed && entry->source != NULL && *entry->source != '\0')
            {
                source_button = gtk_button_new_with_label(_("Source"));
                gtk_widget_set_valign(source_button, GTK_ALIGN_CENTER);
                gtk_widget_set_tooltip_text(source_button,
                                            _("Install from source (requires make, cc, pkg-config and the listed development packages)"));
                g_object_set_data(G_OBJECT(source_button), "vellum-market-entry", entry);
                g_object_set_data(G_OBJECT(source_button), "vellum-market-source", GINT_TO_POINTER(1));
                g_signal_connect(source_button,
                                 "clicked",
                                 G_CALLBACK(mt_window_market_install_clicked),
                                 window);
                gtk_box_append(GTK_BOX(action_box), source_button);
            }

            adw_action_row_add_suffix(row, action_box);
            adw_preferences_group_add(market_group, GTK_WIDGET(row));
        }
        adw_preferences_page_add(page, market_group);

        /* 源码安装环境说明：与欢迎引导同款，按发行版列出依赖 */
        {
            AdwPreferencesGroup *source_group;
            GtkWidget *label;

            source_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
            adw_preferences_group_set_title(source_group, _("Source Build Environment"));
            adw_preferences_group_set_description(source_group,
                                                  _("Source packages are built locally with make, cc and pkg-config. Install the required development packages first:"));
            label = gtk_label_new(NULL);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_widget_add_css_class(label, "monospace");
            gtk_label_set_text(GTK_LABEL(label),
                               "Debian/Ubuntu:\n"
                               "  sudo apt install make gcc pkg-config libgtk-4-dev libadwaita-1-dev libgtksourceview-5-dev libsoup-3.0-dev libjson-glib-dev\n"
                               "Fedora/RHEL (Red Hat):\n"
                               "  sudo dnf install make gcc pkgconf-pkg-config gtk4-devel libadwaita-devel gtksourceview5-devel libsoup-devel json-glib-devel\n"
                               "Arch Linux:\n"
                               "  sudo pacman -S make gcc pkgconf gtk4 libadwaita gtksourceview5 libsoup json-glib\n"
                               "openSUSE:\n"
                               "  sudo zypper install make gcc pkgconf gtk4-devel libadwaita-devel gtksourceview5-devel libsoup-devel json-glib-devel");
            adw_preferences_group_add(source_group, label);
            adw_preferences_page_add(page, source_group);
        }
    }

    adw_preferences_window_add(extensions_window, page);
    gtk_window_present(GTK_WINDOW(extensions_window));
}

typedef struct _MtUpdateCheck
{
    MtWindow *window;
    GtkWidget *dialog;
    GtkWidget *check_button;
    GtkWidget *result_label;
    GtkWidget *download_button;
    SoupSession *session;
    SoupMessage *message;
    GCancellable *cancellable;
} MtUpdateCheck;

static void
mt_update_check_free(MtUpdateCheck *check)
{
    if (check == NULL)
    {
        return;
    }
    if (check->cancellable != NULL)
    {
        g_cancellable_cancel(check->cancellable);
    }
    g_clear_object(&check->cancellable);
    g_clear_object(&check->message);
    g_clear_object(&check->session);
    g_free(check);
}

/* 比较 “YYYY.MM.DD” 形式的日期版本号；a>b 返回正数，相等返回 0。 */
static gint
mt_version_compare(const gchar *a, const gchar *b)
{
    guint av[3] = { 0, 0, 0 };
    guint bv[3] = { 0, 0, 0 };
    gchar **as;
    gchar **bs;
    guint index;

    as = g_strsplit(a != NULL ? a : "", ".", 3);
    bs = g_strsplit(b != NULL ? b : "", ".", 3);
    for (index = 0; index < 3; index++)
    {
        if (as[index] != NULL)
        {
            av[index] = (guint)g_ascii_strtoull(as[index], NULL, 10);
        }
        if (bs[index] != NULL)
        {
            bv[index] = (guint)g_ascii_strtoull(bs[index], NULL, 10);
        }
    }
    g_strfreev(as);
    g_strfreev(bs);
    for (index = 0; index < 3; index++)
    {
        if (av[index] != bv[index])
        {
            return av[index] < bv[index] ? -1 : 1;
        }
    }
    return 0;
}

static void
mt_window_update_check_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtUpdateCheck *check;
    GBytes *bytes;
    GError *error;
    guint status;

    check = user_data;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    status = SOUP_STATUS_NONE;
    if (check->message != NULL)
    {
        status = soup_message_get_status(check->message);
    }
    g_clear_object(&check->cancellable);
    g_clear_object(&check->message);

    if (error != NULL)
    {
        gchar *text;

        text = g_strdup_printf(_("Check failed: %s"), error->message);
        gtk_label_set_text(GTK_LABEL(check->result_label), text);
        g_free(text);
        g_clear_error(&error);
        gtk_widget_set_sensitive(check->check_button, TRUE);
        return;
    }

    if (status == SOUP_STATUS_NOT_FOUND)
    {
        gtk_label_set_text(GTK_LABEL(check->result_label), _("No published releases yet"));
    }
    else if (status < 200 || status >= 300)
    {
        gchar *text;

        text = g_strdup_printf(_("Check failed: HTTP %u"), status);
        gtk_label_set_text(GTK_LABEL(check->result_label), text);
        g_free(text);
    }
    else if (bytes != NULL)
    {
        JsonParser *parser;
        JsonNode *root;
        const gchar *tag;

        parser = json_parser_new();
        if (json_parser_load_from_data(parser,
                                       g_bytes_get_data(bytes, NULL),
                                       g_bytes_get_size(bytes),
                                       &error))
        {
            root = json_parser_get_root(parser);
            tag = NULL;
            if (root != NULL && JSON_NODE_HOLDS_OBJECT(root))
            {
                JsonObject *object;

                object = json_node_get_object(root);
                tag = json_object_get_string_member_with_default(object, "tag_name", NULL);
            }
            if (tag != NULL && *tag != '\0')
            {
                gint comparison;

                comparison = mt_version_compare(tag, VELLUM_VERSION);
                if (comparison > 0)
                {
                    gchar *text;

                    text = g_strdup_printf(_("New version available: %s"), tag);
                    gtk_label_set_text(GTK_LABEL(check->result_label), text);
                    g_free(text);
                    gtk_widget_set_visible(check->download_button, TRUE);
                }
                else
                {
                    gtk_label_set_text(GTK_LABEL(check->result_label),
                                       _("You are up to date"));
                }
            }
            else
            {
                gtk_label_set_text(GTK_LABEL(check->result_label),
                                   _("Unable to read release information"));
            }
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(check->result_label), _("Unable to read release information"));
            g_clear_error(&error);
        }
        g_object_unref(parser);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(check->result_label), _("Check failed: no response"));
    }

    g_bytes_unref(bytes);
    gtk_widget_set_sensitive(check->check_button, TRUE);
}

static void
mt_window_update_check_clicked(GtkButton *button, gpointer user_data)
{
    MtUpdateCheck *check;

    (void)button;
    check = user_data;
    if (check->message != NULL)
    {
        return;
    }

    gtk_widget_set_visible(check->download_button, FALSE);
    gtk_label_set_text(GTK_LABEL(check->result_label), _("Checking for updates…"));
    gtk_widget_set_sensitive(check->check_button, FALSE);

    if (check->session == NULL)
    {
        check->session = soup_session_new();
        g_object_set(check->session, "timeout", 20, NULL);
    }
    check->cancellable = g_cancellable_new();
    check->message = soup_message_new("GET",
                                      "https://api.github.com/repos/lqy306/Vellum/releases/latest");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "Accept",
                                "application/vnd.github+json");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "User-Agent",
                                "Vellum-update-check");
    soup_session_send_and_read_async(check->session,
                                     check->message,
                                     G_PRIORITY_DEFAULT,
                                     check->cancellable,
                                     mt_window_update_check_finished,
                                     check);
}

static void
mt_auto_update_check_free(MtAutoUpdateCheck *check)
{
    if (check == NULL)
    {
        return;
    }
    if (check->cancellable != NULL)
    {
        g_cancellable_cancel(check->cancellable);
    }
    g_clear_object(&check->cancellable);
    g_clear_object(&check->message);
    g_clear_object(&check->session);
    g_free(check);
}

/* 启动后的静默检查：仅在发现更新时弹 Toast，其余情况一律不打扰用户。 */
static void
mt_window_auto_update_check_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtAutoUpdateCheck *check;
    GBytes *bytes;
    GError *error;
    guint status;

    check = user_data;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    status = SOUP_STATUS_NONE;
    if (check->message != NULL)
    {
        status = soup_message_get_status(check->message);
    }

    if (check->window != NULL && check->window->auto_update_check == check)
    {
        check->window->auto_update_check = NULL;
    }

    if (error == NULL && status >= 200 && status < 300 && bytes != NULL)
    {
        JsonParser *parser;
        JsonNode *root;
        const gchar *tag;

        parser = json_parser_new();
        if (json_parser_load_from_data(parser,
                                       g_bytes_get_data(bytes, NULL),
                                       g_bytes_get_size(bytes),
                                       NULL))
        {
            root = json_parser_get_root(parser);
            tag = NULL;
            if (root != NULL && JSON_NODE_HOLDS_OBJECT(root))
            {
                JsonObject *object;

                object = json_node_get_object(root);
                tag = json_object_get_string_member_with_default(object, "tag_name", NULL);
            }
            if (tag != NULL && *tag != '\0' &&
                mt_version_compare(tag, VELLUM_VERSION) > 0)
            {
                MtWindow *window;

                window = check->window;
                if (window != NULL && !window->disposed &&
                    ADW_IS_TOAST_OVERLAY(window->toast_overlay))
                {
                    AdwToast *toast;
                    gchar *text;

                    text = g_strdup_printf(_("New version available: %s"), tag);
                    toast = adw_toast_new(text);
                    g_free(text);
                    adw_toast_set_button_label(toast, _("View"));
                    adw_toast_set_detailed_action_name(toast, "win.open-releases");
                    adw_toast_overlay_add_toast(window->toast_overlay, toast);
                }
            }
        }
        g_object_unref(parser);
    }

    g_bytes_unref(bytes);
    g_clear_error(&error);
    mt_auto_update_check_free(check);
}

static void
mt_window_auto_update_check_start(MtWindow *window)
{
    MtAutoUpdateCheck *check;

    if (window == NULL || window->disposed || window->auto_update_check != NULL)
    {
        return;
    }

    check = g_new0(MtAutoUpdateCheck, 1);
    check->window = window;
    check->session = soup_session_new();
    g_object_set(check->session, "timeout", 20, NULL);
    check->cancellable = g_cancellable_new();
    check->message = soup_message_new("GET",
                                      "https://api.github.com/repos/lqy306/Vellum/releases/latest");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "Accept",
                                "application/vnd.github+json");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "User-Agent",
                                "Vellum-update-check");
    window->auto_update_check = check;
    soup_session_send_and_read_async(check->session,
                                     check->message,
                                     G_PRIORITY_DEFAULT,
                                     check->cancellable,
                                     mt_window_auto_update_check_finished,
                                     check);
}

static gboolean
mt_window_auto_update_check_idle(gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    window->auto_update_source_id = 0;
    if (!window->disposed &&
        mt_settings_get_auto_check_updates(window->settings))
    {
        mt_window_auto_update_check_start(window);
    }
    return G_SOURCE_REMOVE;
}

void
mt_window_action_open_releases(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    GtkUriLauncher *launcher;

    (void)action;
    (void)parameter;
    window = user_data;
    launcher = gtk_uri_launcher_new("https://github.com/lqy306/Vellum/releases/latest");
    gtk_uri_launcher_launch(launcher,
                            GTK_WINDOW(window->window),
                            NULL,
                            NULL,
                            NULL);
    g_object_unref(launcher);
}

static void
mt_window_open_uri_clicked(GtkButton *button, gpointer user_data)
{
    const gchar *uri;
    GtkUriLauncher *launcher;

    (void)user_data;
    uri = g_object_get_data(G_OBJECT(button), "vellum-uri");
    if (uri == NULL)
    {
        return;
    }
    launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher,
                            GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                            NULL,
                            NULL,
                            NULL);
    g_object_unref(launcher);
}

void
mt_window_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtUpdateCheck *check;
    AdwWindow *dialog;
    GtkWidget *toolbar;
    GtkWidget *header;
    GtkWidget *content;
    GtkWidget *icon;
    GtkWidget *name;
    GtkWidget *description;
    GtkWidget *check_button;
    GtkWidget *result_label;
    GtkWidget *download_button;
    GtkWidget *repo_button;
    GtkWidget *footer;

    (void)action;
    (void)parameter;

    window = user_data;
    dialog = ADW_WINDOW(adw_window_new());
    gtk_window_set_title(GTK_WINDOW(dialog), _("About Vellum"));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 480);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    toolbar = adw_toolbar_view_new();
    header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 28);
    gtk_widget_set_margin_end(content, 28);
    gtk_widget_set_margin_top(content, 28);
    gtk_widget_set_margin_bottom(content, 24);

    icon = gtk_image_new_from_icon_name("io.github.vellum.Vellum");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 96);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), icon);

    name = gtk_label_new("Vellum");
    gtk_widget_add_css_class(name, "title-1");
    gtk_widget_set_halign(name, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), name);

    description = gtk_label_new(_("A focused GTK4 text editor."));
    gtk_widget_add_css_class(description, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_justify(GTK_LABEL(description), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content), description);

    check_button = gtk_button_new_with_label(_("Check for Updates"));
    gtk_widget_set_halign(check_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(check_button, 10);
    gtk_box_append(GTK_BOX(content), check_button);

    result_label = gtk_label_new("");
    gtk_widget_add_css_class(result_label, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(result_label), TRUE);
    gtk_label_set_justify(GTK_LABEL(result_label), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content), result_label);

    download_button = gtk_button_new_with_label(_("Open Download Page"));
    gtk_widget_set_halign(download_button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(download_button, FALSE);
    g_object_set_data(G_OBJECT(download_button),
                      "vellum-uri",
                      "https://github.com/lqy306/Vellum/releases/latest");
    g_signal_connect(download_button,
                     "clicked",
                     G_CALLBACK(mt_window_open_uri_clicked),
                     NULL);
    gtk_box_append(GTK_BOX(content), download_button);

    repo_button = gtk_button_new_with_label(_("GitHub Repository"));
    gtk_widget_set_halign(repo_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(repo_button, 10);
    g_object_set_data(G_OBJECT(repo_button),
                      "vellum-uri",
                      "https://github.com/lqy306/Vellum");
    g_signal_connect(repo_button,
                     "clicked",
                     G_CALLBACK(mt_window_open_uri_clicked),
                     NULL);
    gtk_box_append(GTK_BOX(content), repo_button);

    footer = gtk_label_new(_("vellum is licensed under BSD-2-Clause."));
    gtk_widget_add_css_class(footer, "dim-label");
    gtk_widget_add_css_class(footer, "caption");
    gtk_widget_set_halign(footer, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(footer, 14);
    gtk_box_append(GTK_BOX(content), footer);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
    adw_window_set_content(dialog, toolbar);

    check = g_new0(MtUpdateCheck, 1);
    check->window = window;
    check->dialog = GTK_WIDGET(dialog);
    check->check_button = check_button;
    check->result_label = result_label;
    check->download_button = download_button;
    g_signal_connect(check_button,
                     "clicked",
                     G_CALLBACK(mt_window_update_check_clicked),
                     check);
    g_object_set_data_full(G_OBJECT(dialog),
                           "vellum-update-check",
                           check,
                           (GDestroyNotify)mt_update_check_free);

    gtk_window_present(GTK_WINDOW(dialog));
}
