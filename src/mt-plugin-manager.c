/*
 * mt-plugin-manager.c
 * 原生插件与主程序处于同一进程；只应加载用户明确安装且可信的模块。
 * 此文件包含 Host 回调实现、安装流程与公共 API。
 */

#include "mt-plugin-manager-private.h"

#include <glib/gstdio.h>

/* —— Host 回调实现 —— */

static void
mt_plugin_manager_plugin_action_activated(GSimpleAction *action,
                                          GVariant *parameter,
                                          gpointer user_data)
{
    MtPluginActionData *data;

    data = user_data;
    data->callback(action, parameter, data->user_data);
}

static void
mt_plugin_manager_plugin_action_destroyed(gpointer user_data, GClosure *closure)
{
    MtPluginActionData *data;

    (void)closure;

    data = user_data;
    if (data->destroy_notify != NULL)
    {
        data->destroy_notify(data->user_data);
    }
    g_free(data);
}

static gboolean
mt_plugin_manager_add_action(MtPluginHost *host,
                             const gchar *name,
                             MtPluginActionCallback callback,
                             gpointer user_data,
                             GDestroyNotify destroy_notify)
{
    MtPluginManager *manager;
    GSimpleAction *action;
    GAction *existing;
    MtPluginActionData *data;

    manager = host->private_data;
    existing = g_action_map_lookup_action(G_ACTION_MAP(manager->application->application), name);

    if (existing != NULL)
    {
        g_warning("Plugin action '%s' already exists", name);
        return FALSE;
    }

    action = g_simple_action_new(name, NULL);
    data = g_new0(MtPluginActionData, 1);
    data->callback = callback;
    data->user_data = user_data;
    data->destroy_notify = destroy_notify;
    g_signal_connect_data(action,
                          "activate",
                          G_CALLBACK(mt_plugin_manager_plugin_action_activated),
                          data,
                          mt_plugin_manager_plugin_action_destroyed,
                          0);
    mt_application_add_action(manager->application, G_ACTION(action));
    if (manager->loading_plugin != NULL)
    {
        g_ptr_array_add(manager->loading_plugin->action_names, g_strdup(name));
    }
    g_object_unref(action);

    return TRUE;
}

void
mt_plugin_manager_key_data_free(MtPluginKeyData *data)
{
    if (data != NULL)
    {
        if (data->destroy_notify != NULL)
        {
            data->destroy_notify(data->user_data);
        }
        g_free(data);
    }
}

void
mt_plugin_manager_document_change_data_free(MtPluginDocumentChangeData *data)
{
    if (data != NULL)
    {
        if (data->destroy_notify != NULL)
        {
            data->destroy_notify(data->user_data);
        }
        g_free(data);
    }
}

static gboolean
mt_plugin_manager_add_key_handler(MtPluginHost *host,
                                  MtPluginKeyCallback callback,
                                  gpointer user_data,
                                  GDestroyNotify destroy_notify)
{
    MtPluginManager *manager;
    MtPluginKeyData *data;

    manager = host->private_data;
    if (manager->loading_plugin == NULL || callback == NULL)
    {
        return FALSE;
    }

    data = g_new0(MtPluginKeyData, 1);
    data->callback = callback;
    data->user_data = user_data;
    data->destroy_notify = destroy_notify;
    g_ptr_array_add(manager->loading_plugin->key_handlers, data);

    return TRUE;
}

static gboolean
mt_plugin_manager_add_document_change_handler(MtPluginHost *host,
                                              MtPluginDocumentChangeFunc callback,
                                              gpointer user_data,
                                              GDestroyNotify destroy_notify)
{
    MtPluginManager *manager;
    MtPluginDocumentChangeData *data;

    manager = host->private_data;
    if (manager->loading_plugin == NULL || callback == NULL)
    {
        return FALSE;
    }

    data = g_new0(MtPluginDocumentChangeData, 1);
    data->callback = callback;
    data->user_data = user_data;
    data->destroy_notify = destroy_notify;
    g_ptr_array_add(manager->loading_plugin->document_change_handlers, data);

    return TRUE;
}

static void
mt_plugin_manager_set_accelerators(MtPluginHost *host,
                                   const gchar *detailed_action_name,
                                   const gchar * const *accelerators)
{
    MtPluginManager *manager;
    const gchar *custom;
    const gchar *configured_accelerators[2];

    manager = host->private_data;
    custom = mt_settings_get_shortcut(manager->application->settings, detailed_action_name);
    if (custom != NULL)
    {
        configured_accelerators[0] = custom;
        configured_accelerators[1] = NULL;
        gtk_application_set_accels_for_action(GTK_APPLICATION(manager->application->application),
                                              detailed_action_name,
                                              configured_accelerators);
        return;
    }

    gtk_application_set_accels_for_action(GTK_APPLICATION(manager->application->application),
                                          detailed_action_name,
                                          accelerators);
}

static void
mt_plugin_manager_insert_text(MtPluginHost *host, const gchar *text)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);

    if (window != NULL)
    {
        mt_window_insert_text(window, text);
    }
}

static gchar *
mt_plugin_manager_get_current_text(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;
    MtDocument *document;
    GtkTextIter start;
    GtkTextIter end;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window == NULL)
    {
        return g_strdup("");
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return g_strdup("");
    }

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(mt_document_get_buffer(document)), &start, &end);
    /* 插件应分析可保存的源文本，不能因所见即所得标签隐藏标记而误判。 */
    return gtk_text_buffer_get_text(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                    &start,
                                    &end,
                                    TRUE);
}

static gchar *
mt_plugin_manager_get_text_before_cursor(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;
    MtDocument *document;
    GtkTextIter start;
    GtkTextIter cursor;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window == NULL)
    {
        return g_strdup("");
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return g_strdup("");
    }

    gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(mt_document_get_buffer(document)), &start);
    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                     &cursor,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(mt_document_get_buffer(document))));
    return gtk_text_buffer_get_text(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                    &start,
                                    &cursor,
                                    TRUE);
}

static gchar *
mt_plugin_manager_get_text_after_cursor(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;
    MtDocument *document;
    GtkTextIter cursor;
    GtkTextIter end;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window == NULL)
    {
        return g_strdup("");
    }

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

static gchar *
mt_plugin_manager_get_current_file_path(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);

    return window != NULL ? mt_window_get_current_file_path(window) : NULL;
}

static void
mt_plugin_manager_open_file_path(MtPluginHost *host, const gchar *path)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_open_file_path(window, path);
    }
}

static GtkWindow *
mt_plugin_manager_get_parent_window(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);

    return window != NULL ? mt_window_get_gtk_window(window) : NULL;
}

static gboolean
mt_plugin_manager_get_is_code_document(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;
    MtDocument *document;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window == NULL)
    {
        return FALSE;
    }
    document = mt_window_get_current_document(window);
    return document != NULL &&
           gtk_source_buffer_get_language(mt_document_get_buffer(document)) != NULL;
}

static const gchar *
mt_plugin_manager_get_document_language_id(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;
    MtDocument *document;
    GtkSourceLanguage *language;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window == NULL)
    {
        return NULL;
    }
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return NULL;
    }
    language = gtk_source_buffer_get_language(mt_document_get_buffer(document));
    return language != NULL ? gtk_source_language_get_id(language) : NULL;
}

static gchar **
mt_plugin_manager_get_open_documents(MtPluginHost *host, gsize *count)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    return window != NULL ? mt_window_get_open_document_paths(window, count) : NULL;
}

static gboolean
mt_plugin_manager_has_plugin_host(MtPluginHost *host, const gchar *plugin_id)
{
    MtPluginManager *manager;

    manager = host->private_data;
    return mt_plugin_manager_has_plugin(manager, plugin_id);
}

/* —— 安装流程 —— */

typedef struct
{
    MtPluginManager *manager;
    gchar *plugin_id;
    gboolean prefer_source;
    GTask *outer_task;
} HostInstallData;

static void
host_install_data_free(gpointer data)
{
    HostInstallData *install;

    install = data;
    g_free(install->plugin_id);
    if (install->outer_task != NULL)
        g_object_unref(install->outer_task);
    g_free(install);
}

static void mt_plugin_manager_host_install_next(GTask *outer_task);

static void
mt_plugin_manager_host_install_marketplace_done(GObject *source,
                                                GAsyncResult *result,
                                                gpointer user_data)
{
    GTask *outer_task;
    HostInstallData *install;
    MtPluginManager *manager;
    GError *error;

    outer_task = user_data;
    install = g_task_get_task_data(outer_task);
    manager = install->manager;
    error = NULL;
    if (!mt_plugin_manager_marketplace_refresh_finish(manager, result, &error))
    {
        g_task_return_error(outer_task, error);
        g_object_unref(outer_task);
        return;
    }
    mt_plugin_manager_host_install_next(outer_task);
}

static void
mt_plugin_manager_host_install_done(GObject *source,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
    GTask *outer_task;
    HostInstallData *install;
    GError *error;

    (void)source;
    outer_task = user_data;
    install = g_task_get_task_data(outer_task);
    error = NULL;
    if (!mt_plugin_manager_marketplace_install_finish(install->manager, result, &error))
    {
        g_task_return_error(outer_task, error);
    }
    else
    {
        /* 热更新：插件已通过 marketplace 链路加载，同步窗口菜单 */
        if (install->manager->application != NULL)
        {
            MtWindow *window;

            window = mt_application_get_active_window(install->manager->application);
            if (window != NULL)
                mt_window_sync_plugin_menu(window);
        }
        g_task_return_boolean(outer_task, TRUE);
    }
    g_object_unref(outer_task);
}

static void
mt_plugin_manager_host_install_next(GTask *outer_task)
{
    HostInstallData *install;
    MtPluginManager *manager;
    GPtrArray *marketplace;
    MtMarketplaceEntry *entry;
    guint i;

    install = g_task_get_task_data(outer_task);
    manager = install->manager;
    marketplace = mt_plugin_manager_get_marketplace(manager);
    if (marketplace == NULL)
    {
        mt_plugin_manager_marketplace_refresh_async(manager, NULL,
                                                    mt_plugin_manager_host_install_marketplace_done,
                                                    outer_task);
        return;
    }
    entry = NULL;
    for (i = 0; i < marketplace->len; i++)
    {
        MtMarketplaceEntry *candidate;

        candidate = g_ptr_array_index(marketplace, i);
        if (g_strcmp0(candidate->id, install->plugin_id) == 0)
        {
            entry = candidate;
            break;
        }
    }
    if (entry == NULL)
    {
        g_task_return_new_error(outer_task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                "Extension '%s' not found in marketplace", install->plugin_id);
        g_object_unref(outer_task);
        return;
    }
    mt_plugin_manager_marketplace_install_async(manager, entry, install->prefer_source, NULL,
                                                mt_plugin_manager_host_install_done,
                                                outer_task);
}

static void
mt_plugin_manager_install_extension_async_host(MtPluginHost *host,
                                               const gchar *plugin_id,
                                               gboolean prefer_source,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer user_data)
{
    MtPluginManager *manager;
    GTask *outer_task;
    HostInstallData *install;

    manager = host->private_data;
    outer_task = g_task_new(NULL, cancellable, callback, user_data);
    install = g_new0(HostInstallData, 1);
    install->manager = manager;
    install->plugin_id = g_strdup(plugin_id);
    install->prefer_source = prefer_source;
    install->outer_task = NULL; /* not used */
    g_task_set_task_data(outer_task, install, host_install_data_free);
    /* 非 x64 时强制源码，与 bootstrap 一致 */
    if (!mt_bootstrap_is_x64())
        install->prefer_source = TRUE;
    mt_plugin_manager_host_install_next(outer_task);
}

static gboolean
mt_plugin_manager_install_extension_finish_host(MtPluginHost *host,
                                                GAsyncResult *result,
                                                GError **error)
{
    (void)host;
    g_return_val_if_fail(G_IS_TASK(result), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}

/* —— 面板与编辑器命令 —— */

static void
mt_plugin_manager_set_panel(MtPluginHost *host,
                            const gchar *id,
                            MtPluginPanelLocation location,
                            GtkWidget *panel)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_set_plugin_panel(window, id, location, panel);
    }
}

static void
mt_plugin_manager_hide_panel(MtPluginHost *host,
                             const gchar *id,
                             MtPluginPanelLocation location)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_hide_plugin_panel(window, id, location);
    }
}

static gboolean
mt_plugin_manager_run_editor_command(MtPluginHost *host,
                                     MtPluginEditorCommand command)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    return window != NULL && mt_window_run_editor_command(window, command);
}

static void
mt_plugin_manager_show_toast(MtPluginHost *host, const gchar *message)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);

    if (window != NULL)
    {
        mt_window_show_toast(window, message);
    }
}

static void
mt_plugin_manager_show_inline_completion(MtPluginHost *host, const gchar *text)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_show_inline_completion(window, text);
    }
}

static void
mt_plugin_manager_clear_inline_completion(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_clear_inline_completion(window);
    }
}

static void
mt_plugin_manager_show_inline_diff(MtPluginHost *host,
                                   gint offset,
                                   const gchar *old_text,
                                   const gchar *new_text)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_show_inline_diff(window, offset, old_text, new_text);
    }
}

static void
mt_plugin_manager_clear_inline_diff(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_clear_inline_diff(window);
    }
}

static void
mt_plugin_manager_apply_inline_diff(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_apply_inline_diff(window);
    }
}

static void
mt_plugin_manager_show_error_underline(MtPluginHost *host,
                                       gint offset,
                                       gint length,
                                       const gchar *message)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_show_error_underline(window, offset, length, message);
    }
}

static void
mt_plugin_manager_clear_error_underlines(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_clear_error_underlines(window);
    }
}

static void
mt_plugin_manager_set_breakpoint(MtPluginHost *host, gint line)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_set_breakpoint(window, line);
    }
}

static void
mt_plugin_manager_clear_breakpoint(MtPluginHost *host, gint line)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_clear_breakpoint(window, line);
    }
}

static void
mt_plugin_manager_clear_all_breakpoints(MtPluginHost *host)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_clear_all_breakpoints(window);
    }
}

static void
mt_plugin_manager_scroll_to_line(MtPluginHost *host, gint line)
{
    MtPluginManager *manager;
    MtWindow *window;

    manager = host->private_data;
    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_scroll_to_line(window, line);
    }
}

/* —— 公共 API —— */

MtPluginManager *
mt_plugin_manager_new(MtApplication *application)
{
    MtPluginManager *manager;
    gchar *user_directory;
    gchar *system_directory;

    manager = g_new0(MtPluginManager, 1);
    manager->application = application;
    manager->plugins = g_ptr_array_new_with_free_func((GDestroyNotify)mt_loaded_plugin_free);
    manager->disabled_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    manager->removed_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    manager->preference_switches = g_ptr_array_new_with_free_func((GDestroyNotify)mt_preference_switch_free);
    mt_plugin_manager_load_state(manager);
    manager->host.api_version = MT_PLUGIN_API_VERSION;
    manager->host.private_data = manager;
    manager->host.add_action = mt_plugin_manager_add_action;
    manager->host.set_accelerators = mt_plugin_manager_set_accelerators;
    manager->host.insert_text = mt_plugin_manager_insert_text;
    manager->host.get_current_text = mt_plugin_manager_get_current_text;
    manager->host.get_text_before_cursor = mt_plugin_manager_get_text_before_cursor;
    manager->host.get_text_after_cursor = mt_plugin_manager_get_text_after_cursor;
    manager->host.get_current_file_path = mt_plugin_manager_get_current_file_path;
    manager->host.open_file_path = mt_plugin_manager_open_file_path;
    manager->host.get_parent_window = mt_plugin_manager_get_parent_window;
    manager->host.set_panel = mt_plugin_manager_set_panel;
    manager->host.hide_panel = mt_plugin_manager_hide_panel;
    manager->host.add_key_handler = mt_plugin_manager_add_key_handler;
    manager->host.run_editor_command = mt_plugin_manager_run_editor_command;
    manager->host.show_toast = mt_plugin_manager_show_toast;
    manager->host.show_inline_completion = mt_plugin_manager_show_inline_completion;
    manager->host.clear_inline_completion = mt_plugin_manager_clear_inline_completion;
    manager->host.show_inline_diff = mt_plugin_manager_show_inline_diff;
    manager->host.clear_inline_diff = mt_plugin_manager_clear_inline_diff;
    manager->host.apply_inline_diff = mt_plugin_manager_apply_inline_diff;
    manager->host.show_error_underline = mt_plugin_manager_show_error_underline;
    manager->host.clear_error_underlines = mt_plugin_manager_clear_error_underlines;
    manager->host.set_breakpoint = mt_plugin_manager_set_breakpoint;
    manager->host.clear_breakpoint = mt_plugin_manager_clear_breakpoint;
    manager->host.clear_all_breakpoints = mt_plugin_manager_clear_all_breakpoints;
    manager->host.scroll_to_line = mt_plugin_manager_scroll_to_line;
    manager->host.request_plugin_removal = mt_plugin_manager_request_plugin_removal;
    manager->host.add_preference_switch = mt_plugin_manager_add_preference_switch;
    manager->host.get_is_code_document = mt_plugin_manager_get_is_code_document;
    manager->host.add_document_change_handler = mt_plugin_manager_add_document_change_handler;
    manager->host.set_extension_enabled = mt_plugin_manager_set_extension_enabled;
    manager->host.get_document_language_id = mt_plugin_manager_get_document_language_id;
    manager->host.get_open_documents = mt_plugin_manager_get_open_documents;
    manager->host.has_plugin = mt_plugin_manager_has_plugin_host;
    manager->host.install_extension_async = mt_plugin_manager_install_extension_async_host;
    manager->host.install_extension_finish = mt_plugin_manager_install_extension_finish_host;

    if (!g_module_supported())
    {
        g_warning("Dynamic modules are not supported on this platform");
        return manager;
    }

    system_directory = mt_plugin_manager_resolve_system_directory();
    mt_plugin_manager_load_directory(manager, system_directory, FALSE);
    g_free(system_directory);
    /* 系统目录已有插件（本地 meson install 全装）时不再引导下载。 */
    manager->bootstrap_needed = manager->plugins->len == 0;

    user_directory = g_build_filename(g_get_user_data_dir(), "vellum", "plugins", NULL);
    mt_plugin_manager_load_directory(manager, user_directory, TRUE);
    g_free(user_directory);

    /* 包内不再自带扩展：缺失时从 GitHub Releases 拉取到用户目录。 */
    if (manager->bootstrap_needed)
    {
        mt_plugin_manager_bootstrap_start(manager);
    }

    return manager;
}

void
mt_plugin_manager_free(MtPluginManager *manager)
{
    guint index;

    if (manager == NULL)
    {
        return;
    }

    g_clear_pointer(&manager->marketplace, g_ptr_array_unref);
    if (manager->bootstrap != NULL)
    {
        MtPluginBootstrap *bootstrap;

        /* 取消在途下载；结构体由进行中的回调释放，避免悬空访问。 */
        bootstrap = manager->bootstrap;
        bootstrap->cancelled = TRUE;
        if (bootstrap->session != NULL)
        {
            soup_session_abort(bootstrap->session);
            g_object_unref(bootstrap->session);
            bootstrap->session = NULL;
        }
        manager->bootstrap = NULL;
    }

    for (index = 0; index < manager->plugins->len; index++)
    {
        MtLoadedPlugin *plugin;

        plugin = g_ptr_array_index(manager->plugins, index);
        if (plugin->enabled && plugin->deactivate != NULL)
        {
            plugin->deactivate(&manager->host);
        }
        mt_plugin_manager_remove_plugin_actions(manager, plugin);
    }

    g_ptr_array_unref(manager->plugins);
    g_ptr_array_unref(manager->preference_switches);
    g_hash_table_unref(manager->disabled_ids);
    g_hash_table_unref(manager->removed_ids);
    g_free(manager);
}

guint
mt_plugin_manager_get_count(MtPluginManager *manager)
{
    return manager->plugins->len;
}

MtLoadedPlugin *
mt_plugin_manager_get_loaded_plugin(MtPluginManager *manager, guint index)
{
    if (index >= manager->plugins->len)
    {
        return NULL;
    }

    return g_ptr_array_index(manager->plugins, index);
}

const MtPluginInfo *
mt_plugin_manager_get_info(MtPluginManager *manager, guint index)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    return plugin != NULL ? plugin->info : NULL;
}

const gchar *
mt_plugin_manager_get_path(MtPluginManager *manager, guint index)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    return plugin != NULL ? plugin->path : NULL;
}

gboolean
mt_plugin_manager_is_user_managed(MtPluginManager *manager, guint index)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    return plugin != NULL && plugin->user_managed;
}

gboolean
mt_plugin_manager_is_enabled(MtPluginManager *manager, guint index)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    return plugin != NULL && plugin->enabled;
}

gboolean
mt_plugin_manager_set_extension_enabled(MtPluginHost *host,
                                        const gchar *plugin_id,
                                        gboolean enabled,
                                        GError **error)
{
    MtPluginManager *manager;

    manager = host != NULL ? host->private_data : NULL;
    return mt_plugin_manager_set_enabled_by_id(manager, plugin_id, enabled, error);
}

gboolean
mt_plugin_manager_handle_key(MtPluginManager *manager,
                             guint keyval,
                             guint keycode,
                             guint state)
{
    guint index;

    for (index = manager->plugins->len; index > 0; index--)
    {
        MtLoadedPlugin *plugin;
        guint handler_index;

        plugin = g_ptr_array_index(manager->plugins, index - 1);
        if (!plugin->enabled)
        {
            continue;
        }
        for (handler_index = plugin->key_handlers->len; handler_index > 0; handler_index--)
        {
            MtPluginKeyData *data;

            data = g_ptr_array_index(plugin->key_handlers, handler_index - 1);
            if (data->callback(&manager->host,
                               keyval,
                               keycode,
                               state,
                               data->user_data))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

void
mt_plugin_manager_notify_document_changed(MtPluginManager *manager,
                                          guint changed_lines)
{
    guint plugin_index;

    if (manager == NULL || changed_lines == 0)
    {
        return;
    }

    for (plugin_index = 0; plugin_index < manager->plugins->len; plugin_index++)
    {
        MtLoadedPlugin *plugin;
        guint handler_index;

        plugin = g_ptr_array_index(manager->plugins, plugin_index);
        if (plugin == NULL || !plugin->enabled || plugin->document_change_handlers == NULL)
        {
            continue;
        }
        for (handler_index = 0; handler_index < plugin->document_change_handlers->len; handler_index++)
        {
            MtPluginDocumentChangeData *data;

            data = g_ptr_array_index(plugin->document_change_handlers, handler_index);
            if (data != NULL && data->callback != NULL)
            {
                data->callback(&manager->host, changed_lines, data->user_data);
            }
        }
    }
}