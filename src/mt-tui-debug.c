/*
 * mt-tui-debug.c
 * 用行协议驱动真实 GTK 窗口，供自动化回归与人工排错使用。
 * 此程序默认使用临时配置且不加载扩展，绝不执行 shell 命令。
 */

#include "mt-window-private.h"
#include "mt-plugin-manager.h"

#include <adwaita.h>
#include <json-glib/json-glib.h>

#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define MT_TUI_LINE_SIZE 65536

typedef struct _MtTuiSession MtTuiSession;

struct _MtTuiSession
{
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    gchar *temporary_config_home;
};

static void
mt_tui_spin(guint milliseconds)
{
    guint index;

    for (index = 0; index < milliseconds; index++)
    {
        while (g_main_context_iteration(NULL, FALSE))
        {
        }
        g_usleep(1000);
    }
    while (g_main_context_iteration(NULL, FALSE))
    {
    }
}

static void
mt_tui_write_builder(JsonBuilder *builder)
{
    JsonGenerator *generator;
    JsonNode *root;
    gchar *data;

    generator = json_generator_new();
    root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    data = json_generator_to_data(generator, NULL);
    fputs(data, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    g_free(data);
    json_node_free(root);
    g_object_unref(generator);
}

static void
mt_tui_emit_result(gboolean success, const gchar *event, const gchar *message)
{
    JsonBuilder *builder;

    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "ok");
    json_builder_add_boolean_value(builder, success);
    json_builder_set_member_name(builder, "event");
    json_builder_add_string_value(builder, event != NULL ? event : "");
    if (message != NULL)
    {
        json_builder_set_member_name(builder, "message");
        json_builder_add_string_value(builder, message);
    }
    json_builder_end_object(builder);
    mt_tui_write_builder(builder);
    g_object_unref(builder);
}

static void
mt_tui_emit_state(MtTuiSession *session)
{
    JsonBuilder *builder;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    GFile *file;
    gchar *path;

    builder = json_builder_new();
    document = mt_window_get_current_document(session->window);
    text = g_strdup("");
    path = NULL;

    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "ok");
    json_builder_add_boolean_value(builder, TRUE);
    json_builder_set_member_name(builder, "document_count");
    json_builder_add_int_value(builder,
                               ADW_IS_TAB_VIEW(session->window->tab_view) ?
                               adw_tab_view_get_n_pages(session->window->tab_view) : 0);
    json_builder_set_member_name(builder, "extensions_loaded");
    json_builder_add_boolean_value(builder, session->window->plugin_manager != NULL);
    json_builder_set_member_name(builder, "sidebar_visible");
    json_builder_add_boolean_value(builder,
                                   GTK_IS_WIDGET(session->window->sidebar_stack) &&
                                   gtk_widget_get_visible(GTK_WIDGET(session->window->sidebar_stack)));
    json_builder_set_member_name(builder, "auxiliary_visible");
    json_builder_add_boolean_value(builder,
                                   GTK_IS_WIDGET(session->window->auxiliary_stack) &&
                                   gtk_widget_get_visible(GTK_WIDGET(session->window->auxiliary_stack)));

    if (document != NULL)
    {
        buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        g_free(text);
        text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
        file = mt_document_get_file(document);
        if (file != NULL)
        {
            path = g_file_get_path(file);
        }

        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, mt_document_get_display_name(document));
        json_builder_set_member_name(builder, "modified");
        json_builder_add_boolean_value(builder, mt_document_is_modified(document));
        json_builder_set_member_name(builder, "saving");
        json_builder_add_boolean_value(builder, mt_document_is_saving(document));
        json_builder_set_member_name(builder, "path");
        if (path != NULL)
        {
            json_builder_add_string_value(builder, path);
        }
        else
        {
            json_builder_add_null_value(builder);
        }
    }
    else
    {
        json_builder_set_member_name(builder, "title");
        json_builder_add_null_value(builder);
        json_builder_set_member_name(builder, "modified");
        json_builder_add_boolean_value(builder, FALSE);
        json_builder_set_member_name(builder, "saving");
        json_builder_add_boolean_value(builder, FALSE);
        json_builder_set_member_name(builder, "path");
        json_builder_add_null_value(builder);
    }

    json_builder_set_member_name(builder, "text");
    json_builder_add_string_value(builder, text);
    json_builder_end_object(builder);
    mt_tui_write_builder(builder);
    g_object_unref(builder);
    g_free(path);
    g_free(text);
}

static MtDocument *
mt_tui_get_document(MtTuiSession *session)
{
    MtDocument *document;

    document = mt_window_get_current_document(session->window);
    if (document == NULL)
    {
        mt_window_new_document(session->window);
        document = mt_window_get_current_document(session->window);
    }

    return document;
}

static gboolean
mt_tui_set_text(MtTuiSession *session, const gchar *encoded)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    gchar *text;

    document = mt_tui_get_document(session);
    if (document == NULL)
    {
        return FALSE;
    }

    text = g_strcompress(encoded != NULL ? encoded : "");
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_set_text(buffer, text, -1);
    g_free(text);
    return TRUE;
}

static gboolean
mt_tui_insert_text(MtTuiSession *session, const gchar *encoded)
{
    gchar *text;

    if (mt_tui_get_document(session) == NULL)
    {
        return FALSE;
    }

    text = g_strcompress(encoded != NULL ? encoded : "");
    mt_window_insert_text(session->window, text);
    g_free(text);
    return TRUE;
}

static gboolean
mt_tui_save_to_path(MtTuiSession *session, const gchar *path)
{
    MtDocument *document;
    GFile *file;

    if (path == NULL || *path == '\0' || !g_path_is_absolute(path))
    {
        return FALSE;
    }

    document = mt_tui_get_document(session);
    if (document == NULL)
    {
        return FALSE;
    }

    file = g_file_new_for_path(path);
    mt_document_set_file(document, file);
    mt_window_action_save(NULL, NULL, session->window);
    g_object_unref(file);
    return TRUE;
}

static void
mt_tui_market_refresh_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtTuiSession *session;
    MtPluginManager *manager;
    GPtrArray *market;
    GError *error;

    (void)source;
    session = user_data;
    manager = session->window->plugin_manager;
    error = NULL;
    if (manager != NULL && mt_plugin_manager_marketplace_refresh_finish(manager, result, &error))
    {
        market = mt_plugin_manager_get_marketplace(manager);
        mt_tui_emit_result(TRUE, "market-refresh",
                           g_strdup_printf("%u entries", market != NULL ? market->len : 0));
    }
    else
    {
        mt_tui_emit_result(FALSE, "market-refresh",
                           error != NULL ? error->message : "(no error)");
        g_clear_error(&error);
    }
}

static gboolean
mt_tui_run_action(MtTuiSession *session, const gchar *action)
{
    if (g_strcmp0(action, "new") == 0)
    {
        mt_window_action_new(NULL, NULL, session->window);
    }
    else if (g_strcmp0(action, "find") == 0)
    {
        mt_window_action_find(NULL, NULL, session->window);
    }
    else if (g_strcmp0(action, "replace") == 0)
    {
        mt_window_action_replace(NULL, NULL, session->window);
    }
    else if (g_strcmp0(action, "zoom-in") == 0)
    {
        mt_window_action_zoom_in(NULL, NULL, session->window);
    }
    else if (g_strcmp0(action, "zoom-out") == 0)
    {
        mt_window_action_zoom_out(NULL, NULL, session->window);
    }
    else if (g_strcmp0(action, "zoom-reset") == 0)
    {
        mt_window_action_zoom_reset(NULL, NULL, session->window);
    }
    else if (g_strcmp0(action, "force-close") == 0)
    {
        return mt_window_run_editor_command(session->window, MT_PLUGIN_EDITOR_FORCE_CLOSE);
    }
    else if (g_strcmp0(action, "market-refresh") == 0)
    {
        if (session->window->plugin_manager == NULL)
        {
            return FALSE;
        }
        mt_plugin_manager_marketplace_refresh_async(session->window->plugin_manager,
                                                    NULL,
                                                    mt_tui_market_refresh_done,
                                                    session);
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

static gboolean
mt_tui_handle_line(MtTuiSession *session, gchar *line)
{
    gchar *argument;
    gint64 milliseconds;

    g_strchomp(line);
    if (*line == '\0')
    {
        return TRUE;
    }
    if (g_strcmp0(line, "HELP") == 0)
    {
        mt_tui_emit_result(TRUE,
                           "help",
                           "STATE | NEW | TEXT <C-escaped text> | INSERT <C-escaped text> | OPEN <absolute path> | SAVE <absolute path> | ACTION <new|find|replace|zoom-in|zoom-out|zoom-reset|force-close|market-refresh> | WAIT <milliseconds> | QUIT");
        return TRUE;
    }
    if (g_strcmp0(line, "STATE") == 0)
    {
        mt_tui_emit_state(session);
        return TRUE;
    }
    if (g_strcmp0(line, "NEW") == 0)
    {
        mt_window_new_document(session->window);
        mt_tui_emit_result(TRUE, "new", NULL);
        return TRUE;
    }
    if (g_strcmp0(line, "QUIT") == 0)
    {
        mt_tui_emit_result(TRUE, "quit", NULL);
        return FALSE;
    }
    if (g_str_has_prefix(line, "TEXT "))
    {
        if (mt_tui_set_text(session, line + 5))
        {
            mt_tui_emit_result(TRUE, "text", NULL);
        }
        else
        {
            mt_tui_emit_result(FALSE, "text", "No editable document is available");
        }
        return TRUE;
    }
    if (g_str_has_prefix(line, "INSERT "))
    {
        if (mt_tui_insert_text(session, line + 7))
        {
            mt_tui_emit_result(TRUE, "insert", NULL);
        }
        else
        {
            mt_tui_emit_result(FALSE, "insert", "No editable document is available");
        }
        return TRUE;
    }
    if (g_str_has_prefix(line, "OPEN "))
    {
        argument = line + 5;
        if (*argument == '\0' || !g_path_is_absolute(argument))
        {
            mt_tui_emit_result(FALSE, "open", "OPEN requires an absolute path");
        }
        else
        {
            mt_window_open_file_path(session->window, argument);
            mt_tui_emit_result(TRUE, "open", NULL);
        }
        return TRUE;
    }
    if (g_str_has_prefix(line, "SAVE "))
    {
        if (mt_tui_save_to_path(session, line + 5))
        {
            mt_tui_emit_result(TRUE, "save", NULL);
        }
        else
        {
            mt_tui_emit_result(FALSE, "save", "SAVE requires an absolute path");
        }
        return TRUE;
    }
    if (g_str_has_prefix(line, "ACTION "))
    {
        if (mt_tui_run_action(session, line + 7))
        {
            mt_tui_emit_result(TRUE, "action", NULL);
        }
        else
        {
            mt_tui_emit_result(FALSE, "action", "Action is not in the debug allow-list");
        }
        return TRUE;
    }
    if (g_str_has_prefix(line, "WAIT "))
    {
        milliseconds = g_ascii_strtoll(line + 5, NULL, 10);
        if (milliseconds < 0 || milliseconds > 5000)
        {
            mt_tui_emit_result(FALSE, "wait", "WAIT accepts 0 through 5000 milliseconds");
        }
        else
        {
            mt_tui_spin((guint)milliseconds);
            mt_tui_emit_result(TRUE, "wait", NULL);
        }
        return TRUE;
    }

    mt_tui_emit_result(FALSE, "command", "Unknown command; use HELP");
    return TRUE;
}

static void
mt_tui_cleanup(MtTuiSession *session)
{
    gchar *settings_path;
    gchar *settings_directory;

    if (session->window != NULL)
    {
        gtk_window_destroy(GTK_WINDOW(session->window->window));
        mt_tui_spin(10);
        mt_window_free(session->window);
    }
    mt_settings_free(session->settings);
    g_clear_object(&session->application);

    if (session->temporary_config_home != NULL)
    {
        settings_directory = g_build_filename(session->temporary_config_home, "vellum", NULL);
        settings_path = g_build_filename(settings_directory, "settings.ini", NULL);
        g_remove(settings_path);
        g_rmdir(settings_directory);
        g_rmdir(session->temporary_config_home);
        g_free(settings_path);
        g_free(settings_directory);
        g_free(session->temporary_config_home);
    }
}

int
main(int argc, char **argv)
{
    MtTuiSession session;
    GError *error;
    gchar line[MT_TUI_LINE_SIZE];

    (void)argc;
    (void)argv;
    memset(&session, 0, sizeof(session));

    if (g_getenv("VELLUM_TUI_CONFIG_HOME") == NULL)
    {
        error = NULL;
        session.temporary_config_home = g_dir_make_tmp("vellum-tui-debug-XXXXXX", &error);
        if (session.temporary_config_home == NULL)
        {
            g_printerr("Unable to create temporary TUI configuration: %s\n", error->message);
            g_clear_error(&error);
            return 1;
        }
        g_setenv("XDG_CONFIG_HOME", session.temporary_config_home, TRUE);
    }
    else
    {
        g_setenv("XDG_CONFIG_HOME", g_getenv("VELLUM_TUI_CONFIG_HOME"), TRUE);
    }

    gtk_init();
    adw_init();
    session.application = ADW_APPLICATION(adw_application_new("io.github.vellum.TuiDebug",
                                                               G_APPLICATION_NON_UNIQUE));
    error = NULL;
    if (!g_application_register(G_APPLICATION(session.application), NULL, &error))
    {
        g_printerr("Unable to register TUI debug application: %s\n", error->message);
        g_clear_error(&error);
        mt_tui_cleanup(&session);
        return 1;
    }

    session.settings = mt_settings_new();
    mt_settings_set_extensions_enabled(session.settings, FALSE);
    session.window = mt_window_new(session.application, session.settings);
    gtk_window_present(GTK_WINDOW(session.window->window));
    mt_window_new_document(session.window);
    mt_tui_spin(20);
    mt_tui_emit_result(TRUE, "ready", "Core-only GTK debug session started; use HELP");

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        if (!mt_tui_handle_line(&session, line))
        {
            break;
        }
        mt_tui_spin(5);
    }

    mt_tui_cleanup(&session);
    return 0;
}
