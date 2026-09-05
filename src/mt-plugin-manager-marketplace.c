/*
 * mt-plugin-manager-marketplace.c
 * 扩展市场：核心内置、不可卸载。支持多个扩展源（类似 apt 多源），
 * 每个源提供一个 extensions.json 目录；刷新时逐个拉取并合并去重。
 */

#include "mt-plugin-manager-private.h"

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
const gchar *
mt_plugin_manager_marketplace_default_base(void)
{
    const gchar *base;

    base = g_getenv("VELLUM_MARKETPLACE_URL");
    return (base != NULL && *base != '\0') ?
           base : "https://github.com/lqy306/Vellum-extensions/releases/latest/download";
}

/* 用户配置的额外源：~/.config/vellum/market-sources.ini 的 [Sources] urls=，
 * 分号分隔（与 disabled-languages 风格一致）。[Sources] default-enabled 控制
 * 是否包含官方默认源（可被用户在“扩展”页关闭，不强制开启）。 */
gboolean
mt_plugin_manager_get_default_source_enabled(MtPluginManager *manager)
{
    gchar *path;
    GKeyFile *settings;
    gboolean enabled;
    GError *error;

    (void)manager;
    path = g_build_filename(g_get_user_config_dir(), "vellum", "market-sources.ini", NULL);
    settings = g_key_file_new();
    enabled = TRUE;
    if (g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL))
    {
        error = NULL;
        if (g_key_file_has_key(settings, "Sources", "default-enabled", NULL))
        {
            enabled = g_key_file_get_boolean(settings, "Sources", "default-enabled", &error);
            if (error != NULL)
            {
                enabled = TRUE;
                g_clear_error(&error);
            }
        }
    }
    g_key_file_unref(settings);
    g_free(path);

    return enabled;
}

void
mt_plugin_manager_set_default_source_enabled(MtPluginManager *manager,
                                             gboolean enabled)
{
    gchar *path;
    GKeyFile *settings;

    (void)manager;
    path = g_build_filename(g_get_user_config_dir(), "vellum", "market-sources.ini", NULL);
    settings = g_key_file_new();
    g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL);
    g_key_file_set_boolean(settings, "Sources", "default-enabled", enabled);
    g_mkdir_with_parents(g_path_get_dirname(path), 0700);
    g_key_file_save_to_file(settings, path, NULL);
    g_key_file_unref(settings);
    g_free(path);
}

GPtrArray *
mt_plugin_manager_get_marketplace_sources(MtPluginManager *manager)
{
    GPtrArray *sources;
    gchar *path;
    GKeyFile *settings;
    gchar *value;
    gchar **parts;
    gchar **part;

    (void)manager;
    sources = g_ptr_array_new_with_free_func(g_free);
    if (mt_plugin_manager_get_default_source_enabled(manager))
    {
        g_ptr_array_add(sources, g_strdup(mt_plugin_manager_marketplace_default_base()));
    }

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

void
mt_plugin_manager_set_user_sources(MtPluginManager *manager,
                                   gchar * const *urls,
                                   gsize count)
{
    gchar *path;
    GKeyFile *key_file;
    GPtrArray *clean;
    gsize index;
    gchar *value;

    (void)manager;
    path = g_build_filename(g_get_user_config_dir(), "vellum", "market-sources.ini", NULL);
    key_file = g_key_file_new();
    g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL);
    clean = g_ptr_array_new_with_free_func(g_free);
    for (index = 0; index < count; index++)
    {
        gchar *trimmed;

        if (urls == NULL || urls[index] == NULL)
        {
            continue;
        }
        trimmed = g_strdup(urls[index]);
        g_strstrip(trimmed);
        if (*trimmed != '\0')
        {
            g_ptr_array_add(clean, trimmed);
        }
        else
        {
            g_free(trimmed);
        }
    }
    value = g_strjoinv(";", (gchar **)clean->pdata);
    g_key_file_set_string(key_file, "Sources", "urls", value == NULL ? "" : value);
    g_mkdir_with_parents(g_path_get_dirname(path), 0700);
    g_key_file_save_to_file(key_file, path, NULL);
    g_free(value);
    g_ptr_array_unref(clean);
    g_key_file_unref(key_file);
    g_free(path);
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

static void
mt_plugin_manager_marketplace_refresh_next(GTask *task);

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
    data->sources = mt_plugin_manager_get_marketplace_sources(manager);
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

            /* 热更新：源码安装前清除“已删除”标记，否则 load_module 会跳过 */
            g_hash_table_remove(manager->removed_ids, data->entry->id);
            mt_plugin_manager_save_state(manager, NULL);
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