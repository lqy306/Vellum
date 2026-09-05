/*
 * test-plugin-reinstall.c
 * 回归测试：卸载扩展后重新安装，不得因"已由用户移除"标记被立即跳过。
 * 覆盖三条路径：连续"卸载→重装"循环、跨会话（状态落盘后重启模拟）重装。
 */

#include <adwaita.h>
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include "mt-plugin-manager-private.h"
#include "mt-application.h"
#include "mt-settings.h"
#include "mt-vut-package.h"

#define TEST_ID "io.github.vellum.timestamp"
#define TEST_TEMP "/tmp/vellum-plugin-reinstall-test"
#define TEST_PLUGIN_DIR TEST_TEMP "/plugins"
#define TEST_PLUGIN_SRC_DIR TEST_TEMP "/plugin-src"

static gchar *
read_state_removed(void)
{
    GKeyFile *settings;
    gchar *path;
    gchar *removed;

    settings = g_key_file_new();
    path = g_build_filename(g_get_user_config_dir(), "vellum", "plugins.ini", NULL);
    g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL);
    removed = g_key_file_get_string(settings, "Extensions", "removed", NULL);
    g_free(path);
    g_key_file_unref(settings);

    return removed != NULL ? removed : g_strdup("");
}

static gboolean
state_removed_contains(const gchar *id)
{
    gchar *removed;
    gboolean found;

    removed = read_state_removed();
    found = strstr(removed, id) != NULL;
    g_free(removed);

    return found;
}

static MtPluginManager *
new_manager(void)
{
    MtSettings *settings;
    MtApplication *application;
    MtPluginManager *manager;

    settings = mt_settings_new();
    application = mt_application_new(settings);
    manager = mt_plugin_manager_new(application);

    return manager;
}

static guint
find_plugin_index(MtPluginManager *manager, const gchar *id)
{
    guint index;

    for (index = 0; index < manager->plugins->len; index++)
    {
        MtLoadedPlugin *plugin;

        plugin = g_ptr_array_index(manager->plugins, index);
        if (g_strcmp0(plugin->info->id, id) == 0)
        {
            return index;
        }
    }

    return (guint)-1;
}

static gboolean
copy_plugin_module(const gchar *destination_dir)
{
    const gchar *source_env;
    gchar *dest;
    gboolean copied;

    source_env = g_getenv("VELLUM_TEST_PLUGIN");
    if (source_env == NULL || !g_file_test(source_env, G_FILE_TEST_IS_REGULAR))
    {
        g_warning("VELLUM_TEST_PLUGIN is not set to a built plugin module");
        return FALSE;
    }
    g_mkdir_with_parents(destination_dir, 0700);
    dest = g_build_filename(destination_dir, "timestamp-plugin." G_MODULE_SUFFIX, NULL);
    copied = g_file_copy(g_file_new_for_path(source_env),
                         g_file_new_for_path(dest),
                         G_FILE_COPY_OVERWRITE,
                         NULL, NULL, NULL, NULL);
    g_free(dest);

    return copied;
}

static gboolean
build_vut_package(const gchar *module_path, const gchar *destination)
{
    static const MtPluginInfo info = {
        MT_PLUGIN_API_VERSION,
        TEST_ID,
        "Timestamp (test)",
        "Reinstall regression test package",
        "1.0.0"
    };
    GError *error;

    error = NULL;
    if (!mt_vut_package_export(module_path, &info, destination, &error))
    {
        g_warning("Unable to export .vut package: %s", error->message);
        g_clear_error(&error);
        return FALSE;
    }

    return TRUE;
}

/* 循环一：卸载 → .vut 重装，反复两轮（用户报告"每一次"都会触发）。 */
static void
test_uninstall_then_reinstall_cycle(void)
{
    MtPluginManager *manager;
    GError *error;
    gchar *module_path;
    gchar *vut_path;
    guint round;

    manager = new_manager();
    g_assert_cmpuint(find_plugin_index(manager, TEST_ID), !=, (guint)-1);

    module_path = g_build_filename(TEST_PLUGIN_DIR,
                                   "timestamp-plugin." G_MODULE_SUFFIX, NULL);
    vut_path = g_build_filename(TEST_TEMP, "timestamp.vut", NULL);
    g_assert_true(build_vut_package(module_path, vut_path));

    for (round = 1; round <= 2; round++)
    {
        guint index;

        /* 卸载。 */
        index = find_plugin_index(manager, TEST_ID);
        g_assert_cmpuint(index, !=, (guint)-1);
        error = NULL;
        g_assert_true(mt_plugin_manager_remove(manager, index, &error));
        g_assert_no_error(error);
        g_assert_cmpuint(find_plugin_index(manager, TEST_ID), ==, (guint)-1);
        g_assert_true(state_removed_contains(TEST_ID));

        /* 重装：.vut 导入后必须立即加载成功，而不是被"已由用户移除"跳过。 */
        error = NULL;
        g_assert_true(mt_plugin_manager_import(manager,
                                               g_file_new_for_path(vut_path),
                                               &error));
        g_assert_no_error(error);
        g_assert_cmpuint(find_plugin_index(manager, TEST_ID), !=, (guint)-1);
        g_assert_false(state_removed_contains(TEST_ID));
    }

    g_free(module_path);
    g_free(vut_path);
    mt_plugin_manager_free(manager);
}

/* 循环二：卸载后模拟重启（removed 标记已落盘），重装仍须成功。 */
static void
test_uninstall_survives_restart_then_reinstall(void)
{
    MtPluginManager *manager;
    GError *error;
    gchar *module_path;
    gchar *vut_path;

    /* 会话一：加载、卸载、退出。 */
    manager = new_manager();
    g_assert_cmpuint(find_plugin_index(manager, TEST_ID), !=, (guint)-1);
    {
        guint index;

        index = find_plugin_index(manager, TEST_ID);
        error = NULL;
        g_assert_true(mt_plugin_manager_remove(manager, index, &error));
        g_assert_no_error(error);
    }
    mt_plugin_manager_free(manager);

    /* 落盘状态此刻应包含 removed 标记。 */
    g_assert_true(state_removed_contains(TEST_ID));

    /* 会话二：重启后标记被加载，然后 .vut 重装必须成功。 */
    manager = new_manager();
    g_assert_cmpuint(find_plugin_index(manager, TEST_ID), ==, (guint)-1);
    g_assert_true(state_removed_contains(TEST_ID));

    module_path = g_build_filename(TEST_PLUGIN_DIR,
                                   "timestamp-plugin." G_MODULE_SUFFIX, NULL);
    vut_path = g_build_filename(TEST_TEMP, "timestamp.vut", NULL);
    g_assert_true(build_vut_package(module_path, vut_path));

    error = NULL;
    g_assert_true(mt_plugin_manager_import(manager,
                                           g_file_new_for_path(vut_path),
                                           &error));
    g_assert_no_error(error);
    g_assert_cmpuint(find_plugin_index(manager, TEST_ID), !=, (guint)-1);
    g_assert_false(state_removed_contains(TEST_ID));

    g_free(module_path);
    g_free(vut_path);
    mt_plugin_manager_free(manager);
}

/* 循环三：卸载后走完整市场安装链路（本地 HTTP 源），必须立即恢复可用。 */
static gboolean market_install_done;
static gboolean market_refresh_done;

static void
market_install_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtPluginManager *manager;
    GError *error;

    (void)source;
    manager = user_data;
    error = NULL;
    if (!mt_plugin_manager_marketplace_install_finish(manager, result, &error))
    {
        g_warning("marketplace install failed: %s",
                  error != NULL ? error->message : "unknown");
        g_clear_error(&error);
    }
    market_install_done = TRUE;
}

static void
market_refresh_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtPluginManager *manager;
    GError *error;

    (void)source;
    manager = user_data;
    error = NULL;
    if (!mt_plugin_manager_marketplace_refresh_finish(manager, result, &error))
    {
        g_warning("marketplace refresh failed: %s",
                  error != NULL ? error->message : "unknown");
        g_clear_error(&error);
    }
    market_refresh_done = TRUE;
}

static void
spin_until_flag(const gchar *flag)
{
    gboolean *target;
    gint attempts;

    target = g_strcmp0(flag, "install") == 0 ? &market_install_done : &market_refresh_done;
    for (attempts = 0; attempts < 5000 && !*target; attempts++)
    {
        while (g_main_context_iteration(NULL, FALSE))
        {
        }
        g_usleep(2000);
    }
    g_assert_true(*target);
}

/* 极简 HTTP 服务器：整体运行在独立线程 + 独立 GMainContext。
 * 仅支持 GET /extensions.json 与 GET /timestamp.vut。
 * 关键点：不能在主上下文里同步处理连接——处理器阻塞读请求时，
 * 同处主上下文的 libsoup 永远发不出请求，形成死锁。 */
typedef struct
{
    GThread *thread;
    GMutex lock;
    GCond cond;
    gint port;
    gboolean ready;
    volatile gboolean stop;
    gchar *extensions_json;
    gsize extensions_len;
    gchar *vut;
    gsize vut_len;
} MiniHttpServer;

static gboolean
mini_http_read_request(GInputStream *in, GString *buf)
{
    gchar c;
    GError *error;

    error = NULL;
    while (buf->len < 8192)
    {
        gssize n;

        n = g_input_stream_read(in, &c, 1, NULL, &error);
        if (n <= 0)
        {
            if (error != NULL)
            {
                g_clear_error(&error);
            }
            return FALSE;
        }
        g_string_append_c(buf, c);
        if (buf->len >= 4 &&
            strcmp(buf->str + buf->len - 4, "\r\n\r\n") == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void
mini_http_serve_once(GSocketService *service,
                     GSocketConnection *connection,
                     GObject *source_object,
                     MiniHttpServer *srv)
{
    GSocket *sock;
    gint fd;
    GInputStream *in;
    GOutputStream *out;
    GString *req_buf;
    gchar *request;
    const gchar *payload;
    gsize payload_len;
    GBytes *response;
    GError *error;

    (void)service;
    (void)source_object;
    sock = g_socket_connection_get_socket(connection);
    fd = g_socket_get_fd(sock);
    in = g_unix_input_stream_new(fd, FALSE);
    out = g_unix_output_stream_new(fd, FALSE);
    error = NULL;

    req_buf = g_string_new(NULL);
    if (!mini_http_read_request(in, req_buf))
    {
        g_string_free(req_buf, TRUE);
        g_object_unref(in);
        g_object_unref(out);
        return;
    }
    request = g_strdup(req_buf->str);
    g_string_free(req_buf, TRUE);

    payload = NULL;
    payload_len = 0;
    if (strncmp(request, "GET /", 5) == 0)
    {
        const gchar *path;

        path = request + 5;
        if (strncmp(path, "extensions.json", 15) == 0)
        {
            payload = srv->extensions_json;
            payload_len = srv->extensions_len;
        }
        else if (strncmp(path, "timestamp.vut", 13) == 0)
        {
            payload = srv->vut;
            payload_len = srv->vut_len;
        }
    }
    g_free(request);

    {
        gchar *header;
        gchar *body;

        if (payload != NULL)
        {
            header = g_strdup_printf("HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/json\r\n"
                                     "Content-Length: %zu\r\n"
                                     "Connection: close\r\n\r\n",
                                     payload_len);
            body = g_strconcat(header, payload, NULL);
        }
        else
        {
            header = g_strdup_printf("HTTP/1.1 404 Not Found\r\n"
                                     "Content-Length: 0\r\n"
                                     "Connection: close\r\n\r\n");
            body = g_strdup(header);
        }
        g_free(header);
        response = g_bytes_new_take(body, strlen(body));
    }

    g_output_stream_write_bytes(out, response, NULL, &error);
    g_bytes_unref(response);
    g_socket_close(sock, NULL);
    g_object_unref(in);
    g_object_unref(out);
    if (error != NULL)
    {
        g_clear_error(&error);
    }
}

static gpointer
mini_http_thread(MiniHttpServer *srv)
{
    GMainContext *ctx;
    GSocketService *service;
    GError *error;
    GInetAddress *addr;
    GSocketAddress *saddr;
    GSocket *sock;
    GSocketAddress *baddr;
    gint port;

    ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);

    error = NULL;
    service = g_socket_service_new();
    addr = g_inet_address_new_from_string("127.0.0.1");
    saddr = g_inet_socket_address_new(addr, 0);
    sock = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, &error);
    g_assert_no_error(error);
    g_assert_true(g_socket_bind(sock, saddr, TRUE, &error));
    g_assert_no_error(error);
    g_assert_true(g_socket_listen(sock, &error));
    g_assert_no_error(error);
    baddr = g_socket_get_local_address(sock, NULL);
    port = g_inet_socket_address_get_port(G_INET_SOCKET_ADDRESS(baddr));
    g_object_unref(baddr);

    g_signal_connect(service, "incoming",
                     G_CALLBACK(mini_http_serve_once), srv);
    g_assert_true(g_socket_listener_add_socket(G_SOCKET_LISTENER(service),
                                               sock, NULL, &error));
    g_assert_no_error(error);
    g_object_unref(sock);
    g_object_unref(saddr);
    g_object_unref(addr);
    g_socket_service_start(G_SOCKET_SERVICE(service));

    g_mutex_lock(&srv->lock);
    srv->port = port;
    srv->ready = TRUE;
    g_cond_broadcast(&srv->cond);
    g_mutex_unlock(&srv->lock);

    while (!srv->stop)
    {
        /* 阻塞式 iteration 永远不会被 stop 唤醒，用非阻塞轮询。 */
        g_main_context_iteration(ctx, FALSE);
        g_usleep(20000);
    }

    g_socket_service_stop(G_SOCKET_SERVICE(service));
    g_object_unref(service);
    g_main_context_pop_thread_default(ctx);
    g_main_context_unref(ctx);

    return NULL;
}

static MiniHttpServer *
mini_http_server_new(const gchar *extensions_json, const gchar *vut)
{
    MiniHttpServer *srv;

    srv = g_new0(MiniHttpServer, 1);
    srv->extensions_json = g_strdup(extensions_json);
    srv->extensions_len = strlen(extensions_json);
    srv->vut = g_strdup(vut);
    srv->vut_len = strlen(vut);
    g_mutex_init(&srv->lock);
    g_cond_init(&srv->cond);

    srv->thread = g_thread_new("mini-http", (GThreadFunc)mini_http_thread, srv);

    g_mutex_lock(&srv->lock);
    while (!srv->ready)
    {
        g_cond_wait(&srv->cond, &srv->lock);
    }
    g_mutex_unlock(&srv->lock);

    return srv;
}

static void
mini_http_server_free(MiniHttpServer *srv)
{
    if (srv == NULL)
    {
        return;
    }
    srv->stop = TRUE;
    g_thread_join(srv->thread);
    g_mutex_clear(&srv->lock);
    g_cond_clear(&srv->cond);
    g_free(srv->extensions_json);
    g_free(srv->vut);
    g_free(srv);
}

static void
test_uninstall_then_marketplace_install(void)
{
    MtPluginManager *manager;
    GError *error;
    gchar *module_path;
    gchar *vut_path;
    MiniHttpServer *srv;
    gint port = 0;
    gchar *sources_ini;
    gchar *catalog;
    GPtrArray *marketplace;
    MtMarketplaceEntry *entry;
    guint index;

    module_path = g_build_filename(TEST_PLUGIN_DIR,
                                   "timestamp-plugin." G_MODULE_SUFFIX, NULL);
    vut_path = g_build_filename(TEST_TEMP, "timestamp.vut", NULL);
    g_assert_true(build_vut_package(module_path, vut_path));

    {
        gchar *contents;

        if (!g_file_get_contents(vut_path, &contents, NULL, NULL))
        {
            g_assert_not_reached();
        }
        srv = mini_http_server_new("", contents);
        port = srv->port;
        catalog = g_strdup_printf(
            "{\n"
            "  \"format\": 1,\n"
            "  \"base\": \"http://127.0.0.1:%d\",\n"
            "  \"extensions\": [\n"
            "    {\n"
            "      \"id\": \"%s\",\n"
            "      \"name\": \"Timestamp (market)\",\n"
            "      \"description\": \"marketplace reinstall test\",\n"
            "      \"version\": \"1.0.0\",\n"
            "      \"binary\": \"timestamp.vut\"\n"
            "    }\n"
            "  ]\n"
            "}\n", port, TEST_ID);
        g_free(srv->extensions_json);
        srv->extensions_json = g_strdup(catalog);
        srv->extensions_len = strlen(catalog);
        g_free(catalog);
        g_free(contents);
        g_free(vut_path);
    }

    sources_ini = g_build_filename(g_get_user_config_dir(), "vellum",
                                   "market-sources.ini", NULL);
    {
        gchar *contents;

        contents = g_strdup_printf("[Sources]\n"
                                   "default-enabled=false\n"
                                   "urls=http://127.0.0.1:%d\n", port);
        g_file_set_contents(sources_ini, contents, -1, NULL);
        g_free(contents);
    }

    manager = new_manager();
    index = find_plugin_index(manager, TEST_ID);
    g_assert_cmpuint(index, !=, (guint)-1);

    /* 卸载。 */
    error = NULL;
    g_assert_true(mt_plugin_manager_remove(manager, index, &error));
    g_assert_no_error(error);
    g_assert_true(state_removed_contains(TEST_ID));

    /* 刷新市场目录并按用户操作安装（二进制包）。 */
    market_refresh_done = FALSE;
    mt_plugin_manager_marketplace_refresh_async(manager, NULL, market_refresh_ready, manager);
    spin_until_flag("refresh");
    marketplace = mt_plugin_manager_get_marketplace(manager);
    g_assert_true(marketplace != NULL && marketplace->len > 0);
    entry = NULL;
    {
        guint i;

        for (i = 0; i < marketplace->len; i++)
        {
            MtMarketplaceEntry *candidate;

            candidate = g_ptr_array_index(marketplace, i);
            if (g_strcmp0(candidate->id, TEST_ID) == 0)
            {
                entry = candidate;
                break;
            }
        }
    }
    g_assert_true(entry != NULL);

    market_install_done = FALSE;
    mt_plugin_manager_marketplace_install_async(manager, entry, FALSE, NULL,
                                                market_install_ready, manager);
    spin_until_flag("install");

    /* 重装后必须立即可用，removed 标记必须已清除。 */
    g_assert_cmpuint(find_plugin_index(manager, TEST_ID), !=, (guint)-1);
    g_assert_false(state_removed_contains(TEST_ID));

    mini_http_server_free(srv);
    g_free(sources_ini);
    g_free(module_path);
    mt_plugin_manager_free(manager);
}

int
main(int argc, char **argv)
{
    /* 每次运行从干净状态开始：清掉上一次运行遗留的配置/数据。 */
    {
        gchar *wipe;

        wipe = g_strdup_printf("rm -rf %s/config %s/data %s/http",
                               TEST_TEMP, TEST_TEMP, TEST_TEMP);
        g_spawn_command_line_sync(wipe, NULL, NULL, NULL, NULL);
        g_free(wipe);
    }
    g_mkdir_with_parents(TEST_TEMP, 0700);
    g_mkdir_with_parents(TEST_PLUGIN_DIR, 0700);
    g_mkdir_with_parents(TEST_PLUGIN_SRC_DIR, 0700);
    /* 隔离状态：配置与数据都指向临时目录，插件目录换成测试副本。 */
    g_setenv("XDG_CONFIG_HOME", TEST_TEMP "/config", TRUE);
    g_setenv("XDG_DATA_HOME", TEST_TEMP "/data", TRUE);
    g_assert_true(copy_plugin_module(TEST_PLUGIN_SRC_DIR));
    /* 暂存一份纯净副本，再往管理器扫描的插件目录放一份工作副本。 */
    g_assert_true(copy_plugin_module(TEST_PLUGIN_DIR));
    g_setenv("VELLUM_PLUGIN_DIR", TEST_PLUGIN_DIR, TRUE);
    g_setenv("VELLUM_EXTENSIONS_BOOTSTRAP", "0", TRUE);

    gtk_init();
    adw_init();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vellum/plugin/reinstall-cycle",
                    test_uninstall_then_reinstall_cycle);
    g_test_add_func("/vellum/plugin/reinstall-after-restart",
                    test_uninstall_survives_restart_then_reinstall);
    g_test_add_func("/vellum/plugin/reinstall-via-marketplace",
                    test_uninstall_then_marketplace_install);

    return g_test_run();
}
