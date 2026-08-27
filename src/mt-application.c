/*
 * mt-application.c
 * 应用对象将 GTK 的生命周期事件转换为 Vellum 的窗口操作。
 */

#include "mt-application.h"
#include "mt-plugin-manager.h"

#include <glib/gi18n.h>

static void
mt_application_set_accelerators(MtApplication *application,
                                const gchar *detailed_action_name,
                                const gchar * const *fallback)
{
    const gchar *custom;
    const gchar *accelerators[2];

    custom = mt_settings_get_shortcut(application->settings, detailed_action_name);
    if (custom != NULL)
    {
        accelerators[0] = custom;
        accelerators[1] = NULL;
        gtk_application_set_accels_for_action(GTK_APPLICATION(application->application),
                                              detailed_action_name,
                                              accelerators);
        return;
    }

    gtk_application_set_accels_for_action(GTK_APPLICATION(application->application),
                                          detailed_action_name,
                                          fallback);
}

static void
mt_application_ensure_window(MtApplication *application)
{
    guint recovered;

    if (application->window != NULL)
    {
        return;
    }

    application->window = mt_window_new(application->application, application->settings);

    /* 极简模式不加载任何动态模块，窗口仍可完成核心编辑工作。 */
    if (mt_settings_get_extensions_enabled(application->settings))
    {
        application->plugin_manager = mt_plugin_manager_new(application);
        mt_window_set_plugin_manager(application->window, application->plugin_manager);
    }

    recovered = mt_settings_get_restore_session(application->settings) ?
                mt_window_restore_snapshots(application->window) : 0;

    if (recovered == 0)
    {
        mt_window_new_document(application->window);
    }
}

static void
mt_application_activate(GApplication *gapplication, gpointer user_data)
{
    MtApplication *application;

    (void)gapplication;

    application = user_data;
    mt_application_ensure_window(application);
    gtk_window_present(mt_window_get_gtk_window(application->window));
}

static void
mt_application_open(GApplication *gapplication,
                    GFile **files,
                    gint n_files,
                    const gchar *hint,
                    gpointer user_data)
{
    MtApplication *application;
    gint index;

    (void)gapplication;
    (void)hint;

    application = user_data;
    mt_application_ensure_window(application);

    for (index = 0; index < n_files; index++)
    {
        mt_window_open_file(application->window, files[index]);
    }

    gtk_window_present(mt_window_get_gtk_window(application->window));
}

MtApplication *
mt_application_new(MtSettings *settings)
{
    MtApplication *application;
    static const gchar *new_accels[] = { "<Primary>n", NULL };
    static const gchar *open_accels[] = { "<Primary>o", NULL };
    static const gchar *save_accels[] = { "<Primary>s", NULL };
    static const gchar *save_as_accels[] = { "<Primary><Shift>s", NULL };
    static const gchar *close_accels[] = { "<Primary>w", NULL };
    static const gchar *find_accels[] = { "<Primary>f", NULL };
    static const gchar *replace_accels[] = { "<Primary>h", NULL };
    static const gchar *zoom_in_accels[] = { "<Primary>plus", "<Primary>equal", NULL };
    static const gchar *zoom_out_accels[] = { "<Primary>minus", NULL };
    static const gchar *zoom_reset_accels[] = { "<Primary>0", NULL };
    static const gchar *preferences_accels[] = { "<Primary>comma", NULL };
    static const gchar *toggle_properties_accels[] = { "<Primary><Shift>i", NULL };

    application = g_new0(MtApplication, 1);
    application->settings = settings;
    application->application = ADW_APPLICATION(adw_application_new("io.github.vellum.Vellum",
                                                                     G_APPLICATION_HANDLES_OPEN));

    mt_application_set_accelerators(application, "win.new", new_accels);
    mt_application_set_accelerators(application, "win.open", open_accels);
    mt_application_set_accelerators(application, "win.save", save_accels);
    mt_application_set_accelerators(application, "win.save-as", save_as_accels);
    mt_application_set_accelerators(application, "win.close", close_accels);
    mt_application_set_accelerators(application, "win.find", find_accels);
    mt_application_set_accelerators(application, "win.replace", replace_accels);
    mt_application_set_accelerators(application, "win.zoom-in", zoom_in_accels);
    mt_application_set_accelerators(application, "win.zoom-out", zoom_out_accels);
    mt_application_set_accelerators(application, "win.zoom-reset", zoom_reset_accels);
    mt_application_set_accelerators(application, "win.preferences", preferences_accels);
    mt_application_set_accelerators(application, "win.toggle-properties", toggle_properties_accels);

    g_signal_connect(application->application,
                     "activate",
                     G_CALLBACK(mt_application_activate),
                     application);
    g_signal_connect(application->application,
                     "open",
                     G_CALLBACK(mt_application_open),
                     application);

    return application;
}

void
mt_application_free(MtApplication *application)
{
    if (application == NULL)
    {
        return;
    }

    mt_plugin_manager_free(application->plugin_manager);
    mt_window_free(application->window);
    g_clear_object(&application->application);
    g_free(application);
}

int
mt_application_run(MtApplication *application, int argc, char **argv)
{
    return g_application_run(G_APPLICATION(application->application), argc, argv);
}

AdwApplication *
mt_application_get_gtk_application(MtApplication *application)
{
    return application->application;
}

MtWindow *
mt_application_get_active_window(MtApplication *application)
{
    return application->window;
}

void
mt_application_add_action(MtApplication *application, GAction *action)
{
    g_action_map_add_action(G_ACTION_MAP(application->application), action);
}
