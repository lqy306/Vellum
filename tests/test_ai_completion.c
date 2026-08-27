/*
 * test_ai_completion.c
 * 针对 ai-completion-plugin 的本地端到端测试宿主；不使用真实外部服务或密钥。
 */

#include "mt-plugin.h"

#include <gio/gio.h>
#include <gmodule.h>
#include <stdio.h>

static MtPluginActionCallback completion_callback;
static gpointer completion_data;
static GMainLoop *main_loop;
static gchar *inserted_text;
static gboolean success;

static gboolean
test_add_action(MtPluginHost *host,
                const gchar *name,
                MtPluginActionCallback callback,
                gpointer user_data,
                GDestroyNotify destroy_notify)
{
    (void)host;
    (void)name;
    (void)destroy_notify;

    completion_callback = callback;
    completion_data = user_data;

    return TRUE;
}

static void
test_set_accelerators(MtPluginHost *host,
                      const gchar *detailed_action_name,
                      const gchar * const *accelerators)
{
    (void)host;
    (void)detailed_action_name;
    (void)accelerators;
}

static void
test_insert_text(MtPluginHost *host, const gchar *text)
{
    (void)host;

    g_free(inserted_text);
    inserted_text = g_strdup(text);
    success = TRUE;
    g_main_loop_quit(main_loop);
}

static gchar *
test_get_current_text(MtPluginHost *host)
{
    (void)host;

    return g_strdup("int main(void) {\n");
}

static gchar *
test_get_text_before_cursor(MtPluginHost *host)
{
    (void)host;

    return g_strdup("int main(void) {\n");
}

static void
test_show_toast(MtPluginHost *host, const gchar *message)
{
    (void)host;

    fprintf(stderr, "toast: %s\n", message);
}

static gboolean
test_timeout(gpointer user_data)
{
    (void)user_data;

    g_main_loop_quit(main_loop);
    return G_SOURCE_REMOVE;
}

int
main(void)
{
    MtPluginHost host;
    GModule *module;
    MtPluginQueryFunc query;
    MtPluginActivateFunc activate;
    MtPluginDeactivateFunc deactivate;
    GError *error;
    const MtPluginInfo *info;

    if (g_getenv("VELLUM_AI_TEST_PLUGIN") == NULL)
    {
        return 2;
    }

    module = g_module_open(g_getenv("VELLUM_AI_TEST_PLUGIN"), G_MODULE_BIND_LAZY);
    if (module == NULL)
    {
        fprintf(stderr, "unable to open test module: %s\n", g_module_error());
        return 3;
    }

    query = NULL;
    activate = NULL;
    deactivate = NULL;
    if (!g_module_symbol(module, "mt_plugin_query", (gpointer *)&query) ||
        !g_module_symbol(module, "mt_plugin_activate", (gpointer *)&activate))
    {
        g_module_close(module);
        return 4;
    }
    g_module_symbol(module, "mt_plugin_deactivate", (gpointer *)&deactivate);
    info = query();
    if (info == NULL || info->api_version != MT_PLUGIN_API_VERSION)
    {
        g_module_close(module);
        return 5;
    }

    host.api_version = MT_PLUGIN_API_VERSION;
    host.private_data = NULL;
    host.add_action = test_add_action;
    host.set_accelerators = test_set_accelerators;
    host.insert_text = test_insert_text;
    host.get_current_text = test_get_current_text;
    host.get_text_before_cursor = test_get_text_before_cursor;
    host.show_toast = test_show_toast;
    error = NULL;

    if (!activate(&host, &error) || completion_callback == NULL)
    {
        fprintf(stderr, "unable to activate: %s\n", error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        g_module_close(module);
        return 6;
    }

    main_loop = g_main_loop_new(NULL, FALSE);
    completion_callback(NULL, NULL, completion_data);
    g_timeout_add_seconds(5, test_timeout, NULL);
    g_main_loop_run(main_loop);

    if (deactivate != NULL)
    {
        deactivate(&host);
    }

    g_main_loop_unref(main_loop);
    g_module_close(module);

    if (!success || g_strcmp0(inserted_text, " return 0;\n}") != 0)
    {
        fprintf(stderr, "unexpected completion: %s\n", inserted_text != NULL ? inserted_text : "(null)");
        g_free(inserted_text);
        return 7;
    }

    g_free(inserted_text);
    return 0;
}
