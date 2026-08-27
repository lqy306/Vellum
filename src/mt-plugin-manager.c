/*
 * mt-plugin-manager.c
 * 原生插件与主程序处于同一进程；只应加载用户明确安装且可信的模块。
 */

#include "mt-plugin-manager.h"
#include "mt-vut-package.h"

#include <gmodule.h>
#include <glib/gstdio.h>

#ifndef VELLUM_PLUGIN_DIR
#define VELLUM_PLUGIN_DIR "/usr/lib/vellum/plugins"
#endif

typedef struct _MtLoadedPlugin MtLoadedPlugin;
typedef struct _MtPluginActionData MtPluginActionData;
typedef struct _MtPluginKeyData MtPluginKeyData;

struct _MtPluginActionData
{
    MtPluginActionCallback callback;
    gpointer user_data;
    GDestroyNotify destroy_notify;
};

struct _MtPluginKeyData
{
    MtPluginKeyCallback callback;
    gpointer user_data;
    GDestroyNotify destroy_notify;
};

struct _MtLoadedPlugin
{
    GModule *module;
    const MtPluginInfo *info;
    MtPluginDeactivateFunc deactivate;
    MtPluginConfigureFunc configure;
    gchar *path;
    gboolean user_managed;
    gboolean enabled;
    GPtrArray *action_names;
    GPtrArray *key_handlers;
};

struct _MtPluginManager
{
    MtApplication *application;
    MtPluginHost host;
    GPtrArray *plugins;
    GHashTable *disabled_ids;
    GHashTable *removed_ids;
    MtLoadedPlugin *loading_plugin;
    GPtrArray *preference_switches;
};

static void mt_plugin_manager_request_plugin_removal(MtPluginHost *host,
                                                     const gchar *plugin_id);

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

static void
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
mt_loaded_plugin_free(MtLoadedPlugin *plugin)
{
    if (plugin == NULL)
    {
        return;
    }

    if (plugin->module != NULL)
    {
        g_module_close(plugin->module);
    }

    g_free(plugin->path);
    g_clear_pointer(&plugin->action_names, g_ptr_array_unref);
    g_clear_pointer(&plugin->key_handlers, g_ptr_array_unref);
    g_free(plugin);
}

static gchar *
mt_plugin_manager_state_path(void)
{
    gchar *directory;
    gchar *path;

    directory = g_build_filename(g_get_user_config_dir(), "vellum", NULL);
    g_mkdir_with_parents(directory, 0700);
    path = g_build_filename(directory, "plugins.ini", NULL);
    g_free(directory);

    return path;
}

static void
mt_plugin_manager_load_state(MtPluginManager *manager)
{
    GKeyFile *settings;
    gchar *path;
    gchar **ids;
    gsize count;
    guint index;

    settings = g_key_file_new();
    path = mt_plugin_manager_state_path();
    if (g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL))
    {
        ids = g_key_file_get_string_list(settings, "Extensions", "disabled", &count, NULL);
        if (ids != NULL)
        {
            for (index = 0; index < count; index++)
            {
                if (ids[index][0] != '\0')
                {
                    g_hash_table_add(manager->disabled_ids, g_strdup(ids[index]));
                }
            }
            g_strfreev(ids);
        }
        ids = g_key_file_get_string_list(settings, "Extensions", "removed", &count, NULL);
        if (ids != NULL)
        {
            for (index = 0; index < count; index++)
            {
                if (ids[index][0] != '\0')
                {
                    g_hash_table_add(manager->removed_ids, g_strdup(ids[index]));
                }
            }
            g_strfreev(ids);
        }
    }
    g_free(path);
    g_key_file_unref(settings);
}

static gboolean
mt_plugin_manager_save_state(MtPluginManager *manager, GError **error)
{
    GKeyFile *settings;
    gchar *path;
    GList *disabled_keys;
    GList *removed_keys;
    GList *item;
    gchar **disabled_ids;
    gchar **removed_ids;
    gsize disabled_count;
    gsize removed_count;
    gchar *contents;
    gsize length;
    gboolean saved;

    settings = g_key_file_new();
    disabled_keys = g_hash_table_get_keys(manager->disabled_ids);
    disabled_count = g_list_length(disabled_keys);
    disabled_ids = g_new0(gchar *, disabled_count + 1);
    item = disabled_keys;
    disabled_count = 0;
    while (item != NULL)
    {
        disabled_ids[disabled_count++] = item->data;
        item = item->next;
    }
    g_key_file_set_string_list(settings,
                               "Extensions",
                               "disabled",
                               (const gchar * const *)disabled_ids,
                               disabled_count);
    removed_keys = g_hash_table_get_keys(manager->removed_ids);
    removed_count = g_list_length(removed_keys);
    removed_ids = g_new0(gchar *, removed_count + 1);
    item = removed_keys;
    removed_count = 0;
    while (item != NULL)
    {
        removed_ids[removed_count++] = item->data;
        item = item->next;
    }
    g_key_file_set_string_list(settings,
                               "Extensions",
                               "removed",
                               (const gchar * const *)removed_ids,
                               removed_count);
    contents = g_key_file_to_data(settings, &length, error);
    if (contents == NULL)
    {
        g_free(disabled_ids);
        g_free(removed_ids);
        g_list_free(disabled_keys);
        g_list_free(removed_keys);
        g_key_file_unref(settings);
        return FALSE;
    }

    path = mt_plugin_manager_state_path();
    saved = g_file_set_contents(path, contents, (gssize)length, error);
    if (saved)
    {
        g_chmod(path, 0600);
    }
    g_free(path);
    g_free(contents);
    g_free(disabled_ids);
    g_free(removed_ids);
    g_list_free(disabled_keys);
    g_list_free(removed_keys);
    g_key_file_unref(settings);

    return saved;
}

static void
mt_plugin_manager_remove_plugin_actions(MtPluginManager *manager, MtLoadedPlugin *plugin)
{
    guint index;

    for (index = 0; index < plugin->action_names->len; index++)
    {
        const gchar *name;
        gchar *detailed_name;
        const gchar *empty_accelerators[] = { NULL };

        name = g_ptr_array_index(plugin->action_names, index);
        detailed_name = g_strconcat("app.", name, NULL);
        gtk_application_set_accels_for_action(GTK_APPLICATION(manager->application->application),
                                              detailed_name,
                                              empty_accelerators);
        g_action_map_remove_action(G_ACTION_MAP(manager->application->application), name);
        g_free(detailed_name);
    }
    g_ptr_array_set_size(plugin->action_names, 0);
    g_ptr_array_set_size(plugin->key_handlers, 0);
}

static gboolean
mt_plugin_manager_mark_removed(MtPluginManager *manager,
                               MtLoadedPlugin *plugin,
                               GError **error)
{
    if (g_hash_table_contains(manager->removed_ids, plugin->info->id))
    {
        return TRUE;
    }

    g_hash_table_add(manager->removed_ids, g_strdup(plugin->info->id));
    if (!mt_plugin_manager_save_state(manager, error))
    {
        g_hash_table_remove(manager->removed_ids, plugin->info->id);
        return FALSE;
    }

    return TRUE;
}

/* 立即卸载插件：先通知插件停用并移除其动作，再把它从列表里摘掉。
 * 模块改为常驻内存，避免卸载后仍有悬空回调导致崩溃。 */
static void
mt_plugin_manager_unload_plugin(MtPluginManager *manager, MtLoadedPlugin *plugin)
{
    if (plugin->enabled && plugin->deactivate != NULL)
    {
        plugin->deactivate(&manager->host);
    }
    mt_plugin_manager_remove_plugin_actions(manager, plugin);
    plugin->enabled = FALSE;
    if (plugin->module != NULL)
    {
        g_module_make_resident(plugin->module);
    }
    g_ptr_array_remove(manager->plugins, plugin);
}

static gboolean
mt_plugin_manager_load_module(MtPluginManager *manager,
                              const gchar *path,
                              gboolean user_managed)
{
    GModule *module;
    MtPluginQueryFunc query;
    MtPluginActivateFunc activate;
    MtPluginDeactivateFunc deactivate;
    MtPluginConfigureFunc configure;
    const MtPluginInfo *info;
    GError *error;
    MtLoadedPlugin *plugin;

    module = g_module_open(path, G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        g_warning("Unable to load plugin '%s': %s", path, g_module_error());
        return FALSE;
    }

    query = NULL;
    activate = NULL;
    deactivate = NULL;
    configure = NULL;

    if (!g_module_symbol(module, "mt_plugin_query", (gpointer *)&query) ||
        !g_module_symbol(module, "mt_plugin_activate", (gpointer *)&activate))
    {
        g_warning("Plugin '%s' does not implement the required Vellum ABI", path);
        g_module_close(module);
        return FALSE;
    }

    g_module_symbol(module, "mt_plugin_deactivate", (gpointer *)&deactivate);
    g_module_symbol(module, "mt_plugin_configure", (gpointer *)&configure);
    info = query();

    if (info == NULL || info->api_version != MT_PLUGIN_API_VERSION ||
        info->id == NULL || info->name == NULL)
    {
        g_warning("Plugin '%s' is incompatible with Vellum plugin API %u",
                  path,
                  MT_PLUGIN_API_VERSION);
        g_module_close(module);
        return FALSE;
    }

    if (g_hash_table_contains(manager->removed_ids, info->id))
    {
        g_message("Vellum plugin removed by user: %s (%s)", info->name, info->id);
        g_module_close(module);
        return TRUE;
    }

    plugin = g_new0(MtLoadedPlugin, 1);
    plugin->module = module;
    plugin->info = info;
    plugin->deactivate = deactivate;
    plugin->configure = configure;
    plugin->path = g_strdup(path);
    plugin->user_managed = user_managed;
    plugin->enabled = !g_hash_table_contains(manager->disabled_ids, info->id);
    plugin->action_names = g_ptr_array_new_with_free_func(g_free);
    plugin->key_handlers = g_ptr_array_new_with_free_func((GDestroyNotify)mt_plugin_manager_key_data_free);

    if (!plugin->enabled)
    {
        g_ptr_array_add(manager->plugins, plugin);
        g_message("Vellum plugin disabled by user: %s (%s)", info->name, info->id);
        return TRUE;
    }

    error = NULL;
    manager->loading_plugin = plugin;
    if (!activate(&manager->host, &error))
    {
        manager->loading_plugin = NULL;
        g_warning("Unable to activate plugin '%s': %s",
                  path,
                  error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        mt_loaded_plugin_free(plugin);
        return FALSE;
    }
    manager->loading_plugin = NULL;
    g_ptr_array_add(manager->plugins, plugin);
    g_message("Loaded Vellum plugin: %s (%s)", info->name, info->id);

    return TRUE;
}

static gboolean
mt_plugin_manager_add_preference_switch(MtPluginHost *host,
                                        const gchar *group_title,
                                        const gchar *title,
                                        const gchar *subtitle,
                                        MtPluginPreferenceGetFunc get_callback,
                                        MtPluginPreferenceSetFunc set_callback,
                                        gpointer user_data,
                                        GDestroyNotify destroy_notify)
{
    MtPluginManager *manager;
    MtPreferenceSwitch *item;

    manager = host->private_data;
    item = g_new0(MtPreferenceSwitch, 1);
    item->group = g_strdup(group_title);
    item->title = g_strdup(title);
    item->subtitle = g_strdup(subtitle);
    item->get_callback = get_callback;
    item->set_callback = set_callback;
    item->user_data = user_data;
    item->destroy_notify = destroy_notify;
    g_ptr_array_add(manager->preference_switches, item);

    return TRUE;
}

guint
mt_plugin_manager_get_preference_switch_count(MtPluginManager *manager)
{
    if (manager == NULL || manager->preference_switches == NULL)
    {
        return 0;
    }

    return manager->preference_switches->len;
}

const MtPreferenceSwitch *
mt_plugin_manager_get_preference_switch(MtPluginManager *manager, guint index)
{
    if (manager == NULL || manager->preference_switches == NULL ||
        index >= manager->preference_switches->len)
    {
        return NULL;
    }

    return g_ptr_array_index(manager->preference_switches, index);
}

static void
mt_preference_switch_free(MtPreferenceSwitch *item)
{
    g_free(item->group);
    g_free(item->title);
    g_free(item->subtitle);
    if (item->destroy_notify != NULL)
    {
        item->destroy_notify(item->user_data);
    }
    g_free(item);
}

static void
mt_plugin_manager_load_directory(MtPluginManager *manager,
                                 const gchar *directory,
                                 gboolean user_managed)
{
    GDir *dir;
    const gchar *name;

    dir = g_dir_open(directory, 0, NULL);
    if (dir == NULL)
    {
        return;
    }

    while ((name = g_dir_read_name(dir)) != NULL)
    {
        gchar *path;

        if (!g_str_has_suffix(name, "." G_MODULE_SUFFIX))
        {
            continue;
        }

        path = g_build_filename(directory, name, NULL);
        mt_plugin_manager_load_module(manager, path, user_managed);
        g_free(path);
    }

    g_dir_close(dir);
}

/*
 * 未安装到系统时，插件目录的解析顺序：
 * 1. VELLUM_PLUGIN_DIR 环境变量（AppImage 启动器也用它）；
 * 2. 可执行文件相对位置推导的构建树插件目录（build/src/vellum -> build/src/plugins）。
 * 解析不到时回退到编译期 VELLUM_PLUGIN_DIR。
 */
static gchar *
mt_plugin_manager_resolve_system_directory(void)
{
    const gchar *env_dir;
    gchar *exe_path;
    gchar *exe_dir;
    gchar *build_plugins;
    gchar *probe;

    env_dir = g_getenv("VELLUM_PLUGIN_DIR");
    if (env_dir != NULL && *env_dir != '\0')
        return g_strdup(env_dir);

    exe_path = g_file_read_link("/proc/self/exe", NULL);
    if (exe_path != NULL)
    {
        exe_dir = g_path_get_dirname(exe_path);
        build_plugins = g_build_filename(exe_dir, "plugins", NULL);
        probe = g_build_filename(build_plugins, "timestamp-plugin." G_MODULE_SUFFIX,
                                 NULL);
        if (g_file_test(probe, G_FILE_TEST_IS_REGULAR))
        {
            g_free(exe_path);
            g_free(exe_dir);
            g_free(probe);
            return build_plugins;
        }
        g_free(build_plugins);
        g_free(probe);
        g_free(exe_path);
        g_free(exe_dir);
    }

    return g_strdup(VELLUM_PLUGIN_DIR);
}

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
    manager->host.request_plugin_removal = mt_plugin_manager_request_plugin_removal;
    manager->host.add_preference_switch = mt_plugin_manager_add_preference_switch;

    if (!g_module_supported())
    {
        g_warning("Dynamic modules are not supported on this platform");
        return manager;
    }

    system_directory = mt_plugin_manager_resolve_system_directory();
    mt_plugin_manager_load_directory(manager, system_directory, FALSE);
    g_free(system_directory);

    user_directory = g_build_filename(g_get_user_data_dir(), "vellum", "plugins", NULL);
    mt_plugin_manager_load_directory(manager, user_directory, TRUE);
    g_free(user_directory);

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

static MtLoadedPlugin *
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
mt_plugin_manager_set_enabled(MtPluginManager *manager,
                              guint index,
                              gboolean enabled,
                              GError **error)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    if (plugin == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The extension is no longer available");
        return FALSE;
    }
    if (plugin->enabled == enabled)
    {
        return TRUE;
    }

    if (!enabled)
    {
        g_hash_table_add(manager->disabled_ids, g_strdup(plugin->info->id));
        if (!mt_plugin_manager_save_state(manager, error))
        {
            g_hash_table_remove(manager->disabled_ids, plugin->info->id);
            return FALSE;
        }
        if (plugin->deactivate != NULL)
        {
            plugin->deactivate(&manager->host);
        }
        mt_plugin_manager_remove_plugin_actions(manager, plugin);
        plugin->enabled = FALSE;
        return TRUE;
    }

    g_hash_table_remove(manager->disabled_ids, plugin->info->id);
    if (!mt_plugin_manager_save_state(manager, error))
    {
        g_hash_table_add(manager->disabled_ids, g_strdup(plugin->info->id));
        return FALSE;
    }

    manager->loading_plugin = plugin;
    if (!plugin->module || !plugin->info)
    {
        manager->loading_plugin = NULL;
        g_hash_table_add(manager->disabled_ids, g_strdup(plugin->info->id));
        mt_plugin_manager_save_state(manager, NULL);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                    "The extension cannot be reactivated because its module entry points are unavailable");
        return FALSE;
    }
    {
        MtPluginActivateFunc activate;
        GError *activation_error;

        activate = NULL;
        activation_error = NULL;
        g_module_symbol(plugin->module, "mt_plugin_activate", (gpointer *)&activate);
        if (activate == NULL || !activate(&manager->host, &activation_error))
        {
            manager->loading_plugin = NULL;
            mt_plugin_manager_remove_plugin_actions(manager, plugin);
            g_hash_table_add(manager->disabled_ids, g_strdup(plugin->info->id));
            mt_plugin_manager_save_state(manager, NULL);
            if (activation_error != NULL)
            {
                g_propagate_error(error, activation_error);
            }
            else
            {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "The extension could not be activated");
            }
            return FALSE;
        }
    }
    manager->loading_plugin = NULL;
    plugin->enabled = TRUE;

    return TRUE;
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

gboolean
mt_plugin_manager_import(MtPluginManager *manager, GFile *source, GError **error)
{
    gchar *source_path;
    gchar *directory;
    gchar *module_path;
    gchar *extension_id;
    gboolean loaded;

    source_path = g_file_get_path(source);
    if (source_path == NULL || !g_str_has_suffix(source_path, ".vut"))
    {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_NOT_SUPPORTED,
                    "The selected file is not a Vellum .vut extension package");
        g_free(source_path);
        return FALSE;
    }

    directory = g_build_filename(g_get_user_data_dir(), "vellum", "plugins", NULL);
    module_path = NULL;
    extension_id = NULL;
    loaded = mt_vut_package_import(source_path,
                                   directory,
                                   &module_path,
                                   &extension_id,
                                   error);
    if (loaded && !mt_plugin_manager_load_module(manager, module_path, TRUE))
    {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_FAILED,
                    "The .vut package passed metadata checks but its module could not be loaded by the Vellum plugin ABI");
        g_remove(module_path);
        loaded = FALSE;
    }

    g_free(extension_id);
    g_free(module_path);
    g_free(directory);
    g_free(source_path);

    return loaded;
}

gboolean
mt_plugin_manager_export(MtPluginManager *manager,
                         guint index,
                         GFile *destination,
                         GError **error)
{
    MtLoadedPlugin *plugin;
    gchar *destination_path;
    gboolean exported;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    if (plugin == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The extension is no longer available");
        return FALSE;
    }

    destination_path = g_file_get_path(destination);
    if (destination_path == NULL || !g_str_has_suffix(destination_path, ".vut"))
    {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_NOT_SUPPORTED,
                    "Extension packages must be exported with the .vut filename extension");
        g_free(destination_path);
        return FALSE;
    }

    exported = mt_vut_package_export(plugin->path,
                                     plugin->info,
                                     destination_path,
                                     error);
    g_free(destination_path);

    return exported;
}

gboolean
mt_plugin_manager_has_configure(MtPluginManager *manager, guint index)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    return plugin != NULL && plugin->configure != NULL;
}

void
mt_plugin_manager_configure(MtPluginManager *manager, guint index, GtkWindow *parent)
{
    MtLoadedPlugin *plugin;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    if (plugin != NULL && plugin->configure != NULL)
    {
        plugin->configure(&manager->host, parent);
    }
}

gboolean
mt_plugin_manager_remove(MtPluginManager *manager, guint index, GError **error)
{
    MtLoadedPlugin *plugin;
    GFile *file;
    gboolean removed;

    plugin = mt_plugin_manager_get_loaded_plugin(manager, index);
    if (plugin == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "The extension is no longer available");
        return FALSE;
    }

    /* 用户安装的插件连同磁盘文件一起删除；内置插件无法改动系统目录，
     * 改为持久化标记，加载时直接跳过。 */
    if (plugin->user_managed)
    {
        file = g_file_new_for_path(plugin->path);
        removed = g_file_delete(file, NULL, error);
        g_object_unref(file);
        if (!removed)
        {
            return FALSE;
        }
    }

    if (!mt_plugin_manager_mark_removed(manager, plugin, error))
    {
        return FALSE;
    }

    mt_plugin_manager_unload_plugin(manager, plugin);

    return TRUE;
}

gboolean
mt_plugin_manager_has_plugin(MtPluginManager *manager, const gchar *id)
{
    guint index;

    if (manager == NULL || id == NULL)
    {
        return FALSE;
    }

    for (index = 0; index < manager->plugins->len; index++)
    {
        MtLoadedPlugin *plugin;

        plugin = g_ptr_array_index(manager->plugins, index);
        if (plugin->enabled && plugin->info != NULL &&
            g_strcmp0(plugin->info->id, id) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static void
mt_plugin_manager_request_plugin_removal(MtPluginHost *host, const gchar *plugin_id)
{
    MtPluginManager *manager;
    MtLoadedPlugin *plugin;
    MtWindow *window;
    guint index;
    GError *error;

    manager = host->private_data;
    plugin = NULL;
    for (index = 0; index < manager->plugins->len; index++)
    {
        MtLoadedPlugin *candidate;

        candidate = g_ptr_array_index(manager->plugins, index);
        if (g_strcmp0(candidate->info->id, plugin_id) == 0)
        {
            plugin = candidate;
            break;
        }
    }
    if (plugin == NULL || !plugin->enabled)
    {
        return;
    }

    error = NULL;
    if (!mt_plugin_manager_mark_removed(manager, plugin, &error))
    {
        g_warning("Unable to persist extension removal: %s",
                  error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    mt_plugin_manager_unload_plugin(manager, plugin);

    window = mt_application_get_active_window(manager->application);
    if (window != NULL)
    {
        mt_window_sync_plugin_menu(window);
    }
}
