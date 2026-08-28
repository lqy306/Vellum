/*
 * mt-plugin-manager.c
 * 原生插件与主程序处于同一进程；只应加载用户明确安装且可信的模块。
 */

#include "mt-plugin-manager.h"
#include "mt-vut-package.h"

#include <gmodule.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#ifndef VELLUM_PLUGIN_DIR
#define VELLUM_PLUGIN_DIR "/usr/lib/vellum/plugins"
#endif

typedef struct _MtLoadedPlugin MtLoadedPlugin;
typedef struct _MtPluginActionData MtPluginActionData;
typedef struct _MtPluginKeyData MtPluginKeyData;
typedef struct _MtPluginDocumentChangeData MtPluginDocumentChangeData;

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

struct _MtPluginDocumentChangeData
{
    MtPluginDocumentChangeFunc callback;
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
    GPtrArray *document_change_handlers;
};

typedef struct _MtPluginBootstrap MtPluginBootstrap;

struct _MtPluginManager
{
    MtApplication *application;
    MtPluginHost host;
    GPtrArray *plugins;
    GHashTable *disabled_ids;
    GHashTable *removed_ids;
    MtLoadedPlugin *loading_plugin;
    GPtrArray *preference_switches;
    MtPluginBootstrap *bootstrap;
    gboolean bootstrap_needed;
    GPtrArray *marketplace;   /* MtMarketplaceEntry*，NULL = 尚未拉取目录 */
};

/* 内置扩展引导下载：包内不再自带扩展，首次启动从 GitHub Releases 拉取
 * 二进制或源码到用户插件目录（用户管理 → 删除即真删除文件）。
 * 源码模式下载 .vut 并本机编译，与扩展市场源码安装同一链路。 */
struct _MtPluginBootstrap
{
    MtPluginManager *manager;
    SoupSession *session;
    GPtrArray *names;      /* 待下载的文件名（含 .so 或 .vut 后缀） */
    guint index;
    const gchar *current;  /* 正在下载的文件名（借用 names 的引用） */
    gchar *directory;      /* 用户插件目录 */
    guint downloaded;
    gboolean cancelled;
    gboolean prefer_source;
};

typedef struct
{
    const gchar *file;     /* 构建产物文件名（不含后缀） */
    const gchar *id;       /* 插件稳定标识：已删除的插件不再补拉 */
} MtPluginBootstrapEntry;

static const MtPluginBootstrapEntry mt_plugin_bootstrap_entries[] = {
    { "timestamp-plugin",       "io.github.vellum.timestamp" },
    { "word-count-plugin",      "io.github.vellum.document-statistics" },
    { "ai-completion-plugin",   "io.github.vellum.ai-completion" },
    { "link-check-plugin",      "io.github.vellum.link-check" },
    { "project-sidebar-plugin", "io.github.vellum.project-sidebar" },
    { "build-run-plugin",       "io.github.vellum.build-run" },
    { "vim-mode-plugin",        "io.github.vellum.vim-mode" },
    { "screenshot-plugin",      "io.github.vellum.screenshot" },
    { "welcome-plugin",         "io.github.vellum.welcome" }
};

#include <sys/utsname.h>

static gboolean
mt_bootstrap_is_x64(void)
{
    struct utsname info;

    if (uname(&info) != 0)
        return FALSE;
    return g_strcmp0(info.machine, "x86_64") == 0 ||
           g_strcmp0(info.machine, "amd64") == 0;
}

static gboolean
mt_bootstrap_should_prefer_source(void)
{
    GKeyFile *key_file;
    gchar *path;
    gboolean prefer_source;

    /* 非 x64 强制源码 */
    if (!mt_bootstrap_is_x64())
        return TRUE;

    path = g_build_filename(g_get_user_config_dir(), "vellum", "install-pref.ini", NULL);
    key_file = g_key_file_new();
    prefer_source = FALSE;
    if (g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL))
    {
        GError *error;

        error = NULL;
        prefer_source = g_key_file_get_boolean(key_file, "Install", "prefer-source", &error);
        if (error != NULL)
        {
            prefer_source = FALSE;
            g_clear_error(&error);
        }
    }
    g_key_file_unref(key_file);
    g_free(path);
    return prefer_source;
}

static const gchar *
mt_bootstrap_source_asset_for(const gchar *file)
{
    /* 映射 binary file -> source vut 名（与 collect 脚本一致） */
    if (g_strcmp0(file, "timestamp-plugin") == 0)
        return "timestamp-linux-source.vut";
    if (g_strcmp0(file, "word-count-plugin") == 0)
        return "document-statistics-linux-source.vut";
    if (g_strcmp0(file, "ai-completion-plugin") == 0)
        return "ai-completion-linux-source.vut";
    if (g_strcmp0(file, "link-check-plugin") == 0)
        return "link-check-linux-source.vut";
    if (g_strcmp0(file, "project-sidebar-plugin") == 0)
        return "project-sidebar-linux-source.vut";
    if (g_strcmp0(file, "build-run-plugin") == 0)
        return "build-run-linux-source.vut";
    if (g_strcmp0(file, "vim-mode-plugin") == 0)
        return "vim-mode-linux-source.vut";
    if (g_strcmp0(file, "screenshot-plugin") == 0)
        return "screenshot-linux-source.vut";
    if (g_strcmp0(file, "welcome-plugin") == 0)
        return "welcome-linux-source.vut";
    return NULL;
}

static void mt_plugin_manager_request_plugin_removal(MtPluginHost *host,
                                                     const gchar *plugin_id);
static gboolean mt_plugin_manager_set_extension_enabled(MtPluginHost *host,
                                                        const gchar *plugin_id,
                                                        gboolean enabled,
                                                        GError **error);

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

static void
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
    g_clear_pointer(&plugin->document_change_handlers, g_ptr_array_unref);
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
    g_ptr_array_set_size(plugin->document_change_handlers, 0);
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
    plugin->document_change_handlers = g_ptr_array_new_with_free_func((GDestroyNotify)mt_plugin_manager_document_change_data_free);

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

/* —— 扩展引导下载：首次启动（或缺失时）从 GitHub Releases 拉取内置扩展 —— */

static void
mt_plugin_manager_bootstrap_finish(MtPluginBootstrap *bootstrap)
{
    MtPluginManager *manager;

    manager = bootstrap->manager;
    if (manager != NULL && manager->bootstrap == bootstrap)
    {
        manager->bootstrap = NULL;
    }
    if (!bootstrap->cancelled && bootstrap->downloaded > 0)
    {
        /* 重新扫描用户目录，加载刚下载的扩展。 */
        mt_plugin_manager_load_directory(manager, bootstrap->directory, TRUE);
        g_message("Downloaded %u Vellum extension(s) from GitHub", bootstrap->downloaded);
    }
    if (bootstrap->session != NULL)
    {
        g_object_unref(bootstrap->session);
    }
    g_ptr_array_unref(bootstrap->names);
    g_free(bootstrap->directory);
    g_free(bootstrap);
}

static void mt_plugin_manager_bootstrap_next(MtPluginBootstrap *bootstrap);

static void
mt_plugin_manager_bootstrap_response(GObject *source,
                                     GAsyncResult *result,
                                     gpointer user_data)
{
    MtPluginBootstrap *bootstrap;
    GBytes *bytes;
    GError *error;

    bootstrap = user_data;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    if (!bootstrap->cancelled && bytes != NULL && error == NULL)
    {
        if (bootstrap->prefer_source)
        {
            gchar *tmp_path;
            gint fd;
            GError *tmp_error;

            tmp_error = NULL;
            fd = g_file_open_tmp("vellum-bootstrap-XXXXXX.vut", &tmp_path, &tmp_error);
            if (fd >= 0)
            {
                close(fd);
                if (g_file_set_contents(tmp_path,
                                        g_bytes_get_data(bytes, NULL),
                                        g_bytes_get_size(bytes),
                                        &tmp_error))
                {
                    GFile *file;
                    GError *import_error;

                    file = g_file_new_for_path(tmp_path);
                    import_error = NULL;
                    if (mt_plugin_manager_import(bootstrap->manager, file, &import_error))
                    {
                        bootstrap->downloaded++;
                    }
                    else
                    {
                        g_message("Unable to import source extension '%s': %s",
                                  bootstrap->current,
                                  import_error != NULL ? import_error->message : "unknown error");
                        g_clear_error(&import_error);
                    }
                    g_object_unref(file);
                }
                else
                {
                    g_message("Unable to save source extension '%s': %s",
                              bootstrap->current, tmp_error->message);
                }
                g_remove(tmp_path);
                g_free(tmp_path);
                g_clear_error(&tmp_error);
            }
            else
            {
                g_message("Unable to create temp file for '%s': %s",
                          bootstrap->current, tmp_error->message);
                g_clear_error(&tmp_error);
            }
        }
        else
        {
            gchar *path;

            path = g_build_filename(bootstrap->directory, bootstrap->current, NULL);
            if (g_file_set_contents(path,
                                    g_bytes_get_data(bytes, NULL),
                                    g_bytes_get_size(bytes),
                                    &error))
            {
                g_chmod(path, 0600);
                bootstrap->downloaded++;
            }
            else
            {
                g_message("Unable to save extension '%s': %s",
                          bootstrap->current, error->message);
            }
            g_free(path);
        }
    }
    else if (!bootstrap->cancelled)
    {
        g_message("Unable to download extension '%s': %s",
                  bootstrap->current,
                  error != NULL ? error->message : "no response");
    }
    g_clear_error(&error);
    if (bytes != NULL)
    {
        g_bytes_unref(bytes);
    }
    mt_plugin_manager_bootstrap_next(bootstrap);
}

static void
mt_plugin_manager_bootstrap_next(MtPluginBootstrap *bootstrap)
{
    const gchar *base_url;
    gchar *url;
    SoupMessage *message;

    if (bootstrap->cancelled || bootstrap->index >= bootstrap->names->len)
    {
        mt_plugin_manager_bootstrap_finish(bootstrap);
        return;
    }

    bootstrap->current = g_ptr_array_index(bootstrap->names, bootstrap->index);
    bootstrap->index++;
    base_url = g_getenv("VELLUM_EXTENSIONS_BOOTSTRAP_URL");
    if (base_url == NULL || *base_url == '\0')
    {
        base_url = "https://github.com/lqy306/Vellum-extensions/releases/latest/download";
    }
    url = g_strdup_printf("%s/%s", base_url, bootstrap->current);
    message = soup_message_new("GET", url);
    g_free(url);
    if (message == NULL)
    {
        g_message("Invalid extension download URL for '%s'", bootstrap->current);
        mt_plugin_manager_bootstrap_next(bootstrap);
        return;
    }
    soup_session_send_and_read_async(bootstrap->session,
                                     message,
                                     G_PRIORITY_DEFAULT,
                                     NULL,
                                     mt_plugin_manager_bootstrap_response,
                                     bootstrap);
    g_object_unref(message);
}

static void
mt_plugin_manager_bootstrap_start(MtPluginManager *manager)
{
    MtPluginBootstrap *bootstrap;
    const gchar *bootstrap_env;
    guint index;

    /* 测试或离线环境可用 VELLUM_EXTENSIONS_BOOTSTRAP=0 关闭。 */
    bootstrap_env = g_getenv("VELLUM_EXTENSIONS_BOOTSTRAP");
    if (bootstrap_env != NULL && g_strcmp0(bootstrap_env, "0") == 0)
    {
        return;
    }

    bootstrap = g_new0(MtPluginBootstrap, 1);
    bootstrap->manager = manager;
    bootstrap->session = soup_session_new();
    g_object_set(bootstrap->session, "timeout", 20, NULL);
    bootstrap->names = g_ptr_array_new_with_free_func(g_free);
    bootstrap->directory = g_build_filename(g_get_user_data_dir(),
                                            "vellum", "plugins", NULL);
    bootstrap->prefer_source = mt_bootstrap_should_prefer_source();

    for (index = 0; index < G_N_ELEMENTS(mt_plugin_bootstrap_entries); index++)
    {
        const MtPluginBootstrapEntry *entry;
        gchar *module_name;
        gchar *path;

        entry = &mt_plugin_bootstrap_entries[index];
        /* 用户删除过（真删除文件 + removed 标记）或已存在：不再补拉。 */
        if (g_hash_table_contains(manager->removed_ids, entry->id))
        {
            continue;
        }
        module_name = g_strdup_printf("%s." G_MODULE_SUFFIX, entry->file);
        path = g_build_filename(bootstrap->directory, module_name, NULL);
        g_free(module_name);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
        {
            g_free(path);
            continue;
        }
        g_free(path);
        if (bootstrap->prefer_source)
        {
            const gchar *source_asset;

            source_asset = mt_bootstrap_source_asset_for(entry->file);
            if (source_asset != NULL)
                g_ptr_array_add(bootstrap->names, g_strdup(source_asset));
            else
                g_ptr_array_add(bootstrap->names,
                                g_strdup_printf("%s." G_MODULE_SUFFIX, entry->file));
        }
        else
        {
            g_ptr_array_add(bootstrap->names,
                            g_strdup_printf("%s." G_MODULE_SUFFIX, entry->file));
        }
    }

    if (bootstrap->names->len == 0)
    {
        g_object_unref(bootstrap->session);
        g_ptr_array_unref(bootstrap->names);
        g_free(bootstrap->directory);
        g_free(bootstrap);
        return;
    }

    g_mkdir_with_parents(bootstrap->directory, 0700);
    manager->bootstrap = bootstrap;
    mt_plugin_manager_bootstrap_next(bootstrap);
}

/* —— 扩展市场：核心内置、不可卸载。支持多个扩展源（类似 apt 多源），
 *    每个源提供一个 extensions.json 目录；刷新时逐个拉取并合并去重。 —— */

static void
mt_marketplace_entry_free(gpointer data)
{
    MtMarketplaceEntry *entry;

    entry = data;
    g_free(entry->id);
    g_free(entry->name);
    g_free(entry->description);
    g_free(entry->version);
    g_free(entry->binary);
    g_free(entry->source);
    g_free(entry->base);
    g_free(entry);
}

/* 默认源地址：可用环境变量覆盖（便于测试与私有源）。 */
static const gchar *
mt_plugin_manager_marketplace_default_base(void)
{
    const gchar *base;

    base = g_getenv("VELLUM_MARKETPLACE_URL");
    return (base != NULL && *base != '\0') ?
           base : "https://github.com/lqy306/Vellum-extensions/releases/latest/download";
}

/* 用户配置的额外源：~/.config/vellum/market-sources.ini 的 [Sources] urls=，
 * 分号分隔（与 disabled-languages 风格一致）。 */
static GPtrArray *
mt_plugin_manager_marketplace_sources(MtPluginManager *manager)
{
    GPtrArray *sources;
    gchar *path;
    GKeyFile *settings;
    gchar *value;
    gchar **parts;
    gchar **part;

    (void)manager;
    sources = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(sources, g_strdup(mt_plugin_manager_marketplace_default_base()));

    path = g_build_filename(g_get_user_config_dir(), "vellum", "market-sources.ini", NULL);
    settings = g_key_file_new();
    if (g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL))
    {
        value = g_key_file_get_string(settings, "Sources", "urls", NULL);
        if (value != NULL && *value != '\0')
        {
            parts = g_strsplit(value, ";", -1);
            for (part = parts; part != NULL && *part != NULL; part++)
            {
                gchar *trimmed;

                trimmed = g_strdup(*part);
                g_strstrip(trimmed);
                if (*trimmed != '\0')
                {
                    g_ptr_array_add(sources, trimmed);
                }
                else
                {
                    g_free(trimmed);
                }
            }
            g_strfreev(parts);
        }
        g_free(value);
    }
    g_key_file_unref(settings);
    g_free(path);

    return sources;
}

typedef struct
{
    MtPluginManager *manager;
    GPtrArray *sources;   /* gchar* 源地址 */
    guint index;
    GPtrArray *entries;   /* 合并结果（MtMarketplaceEntry*） */
} MtMarketplaceRefreshData;

static void
mt_marketplace_refresh_data_free(gpointer data)
{
    MtMarketplaceRefreshData *refresh;

    refresh = data;
    g_ptr_array_unref(refresh->sources);
    if (refresh->entries != NULL)
    {
        g_ptr_array_unref(refresh->entries);
    }
    g_free(refresh);
}

static void mt_plugin_manager_marketplace_refresh_next(GTask *task);

static void
mt_plugin_manager_marketplace_refresh_cb(GObject *source,
                                         GAsyncResult *result,
                                         gpointer user_data)
{
    GTask *task;
    MtMarketplaceRefreshData *data;
    MtPluginManager *manager;
    GBytes *bytes;
    GError *error;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *object;
    JsonArray *extensions;
    const gchar *source_base;
    guint index;

    task = user_data;
    data = g_task_get_task_data(task);
    manager = data->manager;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    g_object_unref(SOUP_SESSION(source));
    if (bytes == NULL)
    {
        /* 单个源失败不致命：继续下一个源。 */
        g_message("Marketplace source '%s' failed: %s",
                  (const gchar *)g_ptr_array_index(data->sources, data->index),
                  error != NULL ? error->message : "no response");
        g_clear_error(&error);
        g_bytes_unref(bytes);
        mt_plugin_manager_marketplace_refresh_next(task);
        return;
    }

    source_base = g_ptr_array_index(data->sources, data->index);
    parser = json_parser_new();
    if (!json_parser_load_from_data(parser,
                                    g_bytes_get_data(bytes, NULL),
                                    g_bytes_get_size(bytes),
                                    &error))
    {
        g_message("Marketplace source '%s' has invalid catalog: %s",
                  source_base, error->message);
        g_clear_error(&error);
        g_object_unref(parser);
        g_bytes_unref(bytes);
        mt_plugin_manager_marketplace_refresh_next(task);
        return;
    }
    root = json_parser_get_root(parser);
    object = root != NULL ? json_node_get_object(root) : NULL;
    extensions = (object != NULL && json_object_has_member(object, "extensions"))
                 ? json_object_get_array_member(object, "extensions") : NULL;
    if (extensions != NULL)
    {
        const gchar *base;

        /* 每个目录可自定资产下载基址；缺省用源地址本身。 */
        base = json_object_get_string_member(object, "base");
        base = (base != NULL && *base != '\0') ? base : source_base;
        for (index = 0; index < json_array_get_length(extensions); index++)
        {
            JsonObject *item;
            MtMarketplaceEntry *entry;
            const gchar *id;
            guint existing;

            item = json_array_get_object_element(extensions, index);
            if (item == NULL)
            {
                continue;
            }
            id = json_object_get_string_member(item, "id");
            if (id == NULL || *id == '\0')
            {
                continue;
            }
            /* 去重：同 id 先到先得。 */
            for (existing = 0; existing < data->entries->len; existing++)
            {
                MtMarketplaceEntry *known;

                known = g_ptr_array_index(data->entries, existing);
                if (g_strcmp0(known->id, id) == 0)
                {
                    break;
                }
            }
            if (existing < data->entries->len)
            {
                continue;
            }
            entry = g_new0(MtMarketplaceEntry, 1);
            entry->id = g_strdup(id);
            entry->name = g_strdup(json_object_get_string_member(item, "name"));
            entry->description = g_strdup(json_object_get_string_member(item, "description"));
            entry->version = g_strdup(json_object_get_string_member(item, "version"));
            if (json_object_has_member(item, "binary"))
            {
                entry->binary = g_strdup(json_object_get_string_member(item, "binary"));
            }
            if (json_object_has_member(item, "source"))
            {
                entry->source = g_strdup(json_object_get_string_member(item, "source"));
            }
            entry->base = g_strdup(base);
            g_ptr_array_add(data->entries, entry);
        }
    }
    g_object_unref(parser);
    g_bytes_unref(bytes);
    mt_plugin_manager_marketplace_refresh_next(task);
}

static void
mt_plugin_manager_marketplace_refresh_next(GTask *task)
{
    MtMarketplaceRefreshData *data;
    MtPluginManager *manager;
    const gchar *base;
    gchar *url;
    SoupMessage *message;
    SoupSession *session;

    data = g_task_get_task_data(task);
    manager = data->manager;
    if (data->index >= data->sources->len)
    {
        if (data->entries->len == 0)
        {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                    "No extension catalog could be loaded");
            g_object_unref(task);
            return;
        }
        g_clear_pointer(&manager->marketplace, g_ptr_array_unref);
        manager->marketplace = data->entries;
        data->entries = NULL;
        g_task_return_boolean(task, TRUE);
        g_object_unref(task);
        return;
    }

    base = g_ptr_array_index(data->sources, data->index);
    data->index++;
    url = g_strdup_printf("%s/extensions.json", base);
    message = soup_message_new("GET", url);
    g_free(url);
    if (message == NULL)
    {
        g_message("Invalid marketplace source URL");
        mt_plugin_manager_marketplace_refresh_next(task);
        return;
    }
    session = soup_session_new();
    g_object_set(session, "timeout", 20, NULL);
    soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT,
                                     g_task_get_cancellable(task),
                                     mt_plugin_manager_marketplace_refresh_cb,
                                     task);
    g_object_unref(message);
}

void
mt_plugin_manager_marketplace_refresh_async(MtPluginManager *manager,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data)
{
    GTask *task;
    MtMarketplaceRefreshData *data;

    task = g_task_new(NULL, cancellable, callback, user_data);
    data = g_new0(MtMarketplaceRefreshData, 1);
    data->manager = manager;
    data->sources = mt_plugin_manager_marketplace_sources(manager);
    data->entries = g_ptr_array_new_with_free_func(mt_marketplace_entry_free);
    g_task_set_task_data(task, data, (GDestroyNotify)mt_marketplace_refresh_data_free);
    mt_plugin_manager_marketplace_refresh_next(task);
}

gboolean
mt_plugin_manager_marketplace_refresh_finish(MtPluginManager *manager,
                                             GAsyncResult *result,
                                             GError **error)
{
    (void)manager;
    g_return_val_if_fail(G_IS_TASK(result), FALSE);

    return g_task_propagate_boolean(G_TASK(result), error);
}

GPtrArray *
mt_plugin_manager_get_marketplace(MtPluginManager *manager)
{
    return manager != NULL ? manager->marketplace : NULL;
}

typedef struct
{
    MtPluginManager *manager;
    MtMarketplaceEntry *entry;   /* 借用 marketplace 数组中的条目 */
    gboolean is_source;
    gchar *asset;                /* 选中的资产名 */
    gchar *path;                 /* 目标（二进制）或临时（源码）路径 */
} MtMarketplaceInstallData;

static void
mt_marketplace_install_data_free(gpointer data)
{
    MtMarketplaceInstallData *install;

    install = data;
    g_free(install->asset);
    g_free(install->path);
    g_free(install);
}

static void
mt_plugin_manager_marketplace_install_cb(GObject *source,
                                         GAsyncResult *result,
                                         gpointer user_data)
{
    GTask *task;
    MtMarketplaceInstallData *data;
    MtPluginManager *manager;
    GBytes *bytes;
    GError *error;

    task = user_data;
    data = g_task_get_task_data(task);
    manager = data->manager;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    g_object_unref(SOUP_SESSION(source));
    if (bytes == NULL)
    {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    if (data->is_source)
    {
        /* 源码包：写入临时文件后走 .vut 导入（校验 + 本机构建 + 加载）。 */
        if (g_file_set_contents(data->path,
                                g_bytes_get_data(bytes, NULL),
                                g_bytes_get_size(bytes),
                                &error))
        {
            GFile *file;
            gboolean ok;

            file = g_file_new_for_path(data->path);
            ok = mt_plugin_manager_import(manager, file, &error);
            g_object_unref(file);
            g_remove(data->path);
            if (ok)
            {
                g_task_return_boolean(task, TRUE);
            }
            else
            {
                g_task_return_error(task, error);
            }
        }
        else
        {
            g_task_return_error(task, error);
        }
    }
    else
    {
        /* 二进制：写入用户插件目录，清除 removed 标记后重新扫描加载。 */
        if (g_file_set_contents(data->path,
                                g_bytes_get_data(bytes, NULL),
                                g_bytes_get_size(bytes),
                                &error))
        {
            gchar *directory;

            g_chmod(data->path, 0600);
            g_hash_table_remove(manager->removed_ids, data->entry->id);
            mt_plugin_manager_save_state(manager, NULL);
            directory = g_path_get_dirname(data->path);
            mt_plugin_manager_load_directory(manager, directory, TRUE);
            g_free(directory);
            g_task_return_boolean(task, TRUE);
        }
        else
        {
            g_task_return_error(task, error);
        }
    }
    g_bytes_unref(bytes);
    g_object_unref(task);
}

void
mt_plugin_manager_marketplace_install_async(MtPluginManager *manager,
                                            MtMarketplaceEntry *entry,
                                            gboolean prefer_source,
                                            GCancellable *cancellable,
                                            GAsyncReadyCallback callback,
                                            gpointer user_data)
{
    GTask *task;
    MtMarketplaceInstallData *data;
    const gchar *asset;
    gchar *url;
    SoupMessage *message;
    SoupSession *session;
    gint fd;
    GError *error;

    asset = prefer_source ? entry->source : entry->binary;
    if (prefer_source && (asset == NULL || *asset == '\0'))
    {
        /* 该源未提供源码包：回退二进制。 */
        asset = entry->binary;
    }
    if (asset == NULL || *asset == '\0')
    {
        g_task_report_new_error(NULL,
                                callback,
                                user_data,
                                mt_plugin_manager_marketplace_install_async,
                                G_IO_ERROR,
                                G_IO_ERROR_NOT_SUPPORTED,
                                "该扩展没有可用的安装包");
        return;
    }

    task = g_task_new(NULL, cancellable, callback, user_data);
    data = g_new0(MtMarketplaceInstallData, 1);
    data->manager = manager;
    data->entry = entry;
    data->is_source = g_strcmp0(asset, entry->source) == 0;
    data->asset = g_strdup(asset);
    error = NULL;
    if (data->is_source)
    {
        fd = g_file_open_tmp("vellum-market-XXXXXX.vut", &data->path, &error);
        if (fd < 0)
        {
            g_task_return_error(task, error);
            g_object_unref(task);
            return;
        }
        close(fd);
    }
    else
    {
        data->path = g_build_filename(g_get_user_data_dir(),
                                      "vellum", "plugins", asset, NULL);
        g_mkdir_with_parents(g_path_get_dirname(data->path), 0700);
    }
    g_task_set_task_data(task, data, (GDestroyNotify)mt_marketplace_install_data_free);

    url = g_strdup_printf("%s/%s", entry->base, asset);
    message = soup_message_new("GET", url);
    g_free(url);
    if (message == NULL)
    {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                                "Invalid extension download URL");
        g_object_unref(task);
        return;
    }
    session = soup_session_new();
    g_object_set(session, "timeout", 30, NULL);
    soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT, cancellable,
                                     mt_plugin_manager_marketplace_install_cb, task);
    g_object_unref(message);
}

gboolean
mt_plugin_manager_marketplace_install_finish(MtPluginManager *manager,
                                             GAsyncResult *result,
                                             GError **error)
{
    (void)manager;
    g_return_val_if_fail(G_IS_TASK(result), FALSE);

    return g_task_propagate_boolean(G_TASK(result), error);
}

gboolean
mt_plugin_manager_marketplace_uninstall(MtPluginManager *manager,
                                        MtMarketplaceEntry *entry,
                                        GError **error)
{
    guint index;

    for (index = 0; index < manager->plugins->len; index++)
    {
        MtLoadedPlugin *plugin;

        plugin = g_ptr_array_index(manager->plugins, index);
        if (g_strcmp0(plugin->info->id, entry->id) == 0)
        {
            return mt_plugin_manager_remove(manager, index, error);
        }
    }
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Extension is not installed");

    return FALSE;
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
    manager->host.get_is_code_document = mt_plugin_manager_get_is_code_document;
    manager->host.add_document_change_handler = mt_plugin_manager_add_document_change_handler;
    manager->host.set_extension_enabled = mt_plugin_manager_set_extension_enabled;
    manager->host.get_document_language_id = mt_plugin_manager_get_document_language_id;

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
mt_plugin_manager_set_enabled_by_id(MtPluginManager *manager,
                                    const gchar *plugin_id,
                                    gboolean enabled,
                                    GError **error)
{
    guint index;

    if (manager == NULL || plugin_id == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Extension identifier is required");
        return FALSE;
    }
    for (index = 0; index < manager->plugins->len; index++)
    {
        MtLoadedPlugin *plugin;

        plugin = g_ptr_array_index(manager->plugins, index);
        if (plugin != NULL && plugin->info != NULL &&
            g_strcmp0(plugin->info->id, plugin_id) == 0)
        {
            return mt_plugin_manager_set_enabled(manager, index, enabled, error);
        }
    }
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "The requested extension is not loaded");
    return FALSE;
}

static gboolean
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
