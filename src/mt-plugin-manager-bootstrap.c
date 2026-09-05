/*
 * mt-plugin-manager-bootstrap.c
 * 首次启动时从 GitHub Releases 下载内置扩展。
 */

#include "mt-plugin-manager-private.h"

#include <sys/utsname.h>

gboolean
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
    if (g_strcmp0(file, "dev-experience-plugin") == 0)
        return "dev-experience-linux-source.vut";
    if (g_strcmp0(file, "vim-mode-plugin") == 0)
        return "vim-mode-linux-source.vut";
    if (g_strcmp0(file, "screenshot-plugin") == 0)
        return "screenshot-linux-source.vut";
    if (g_strcmp0(file, "welcome-plugin") == 0)
        return "welcome-linux-source.vut";
    return NULL;
}

void
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

static void
mt_plugin_manager_bootstrap_next(MtPluginBootstrap *bootstrap);

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

void
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