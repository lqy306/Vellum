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
void
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

void mt_timing_log_duration(const gchar *label, gint64 start)
{
    mt_timing_log("%s: %lld ms", label,
                  (long long)((g_get_monotonic_time() - start) / 1000));
}

void
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

/* 构建树直接运行时，把源码里的 data/icons/hicolor 加入图标主题搜索路径，
 * 让窗口图标（用户提供的 SVG）在未安装的情况下也能被解析到。
 * 从可执行文件所在目录逐级向上找包含 data/icons/hicolor 的源码根。 */
void
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

MtFileRequest *
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

void
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

MtCloseRequest *
mt_close_request_new(MtWindow *window, MtDocument *document, AdwTabPage *page)
{
    MtCloseRequest *request;

    request = g_new0(MtCloseRequest, 1);
    request->window = window;
    request->document = document;
    request->page = g_object_ref(page);

    return request;
}

void
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

void
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

void
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

void
mt_extension_file_request_free(MtExtensionFileRequest *request)
{
    if (request == NULL)
    {
        return;
    }

    g_clear_object(&request->dialog);
    g_free(request);
}

void
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

void
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

void
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

void
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

void
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

void
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

gboolean
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

void
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

void
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

gboolean
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

void
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
