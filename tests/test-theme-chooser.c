/*
 * test-theme-chooser.c
 * 验证独立主题卡片选择器可打开、提供全部内置方案并立即保存选择。
 */

#include <adwaita.h>
#include <glib/gstdio.h>

#include "mt-window-private.h"

static void
spin_main_context(void)
{
    gint index;

    for (index = 0; index < 12; index++)
    {
        while (g_main_context_iteration(NULL, FALSE))
        {
        }
    }
}

static void
wait_until_saved(MtDocument *document)
{
    gint attempts;

    for (attempts = 0; attempts < 10000 && mt_document_is_modified(document); attempts++)
    {
        while (g_main_context_iteration(NULL, FALSE))
        {
        }
        g_usleep(1000);
    }

    g_assert_false(mt_document_is_modified(document));
    g_assert_false(mt_document_is_saving(document));
}

static GtkWidget *
find_button_with_label(GtkWidget *widget, const gchar *label)
{
    GtkWidget *child;

    if (GTK_IS_BUTTON(widget) &&
        g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)
    {
        return widget;
    }

    child = gtk_widget_get_first_child(widget);
    while (child != NULL)
    {
        GtkWidget *found;

        found = find_button_with_label(child, label);
        if (found != NULL)
        {
            return found;
        }
        child = gtk_widget_get_next_sibling(child);
    }

    return NULL;
}

static gboolean
widget_contains_label(GtkWidget *widget, const gchar *text)
{
    GtkWidget *child;

    if (GTK_IS_LABEL(widget) &&
        g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), text) == 0)
    {
        return TRUE;
    }

    child = gtk_widget_get_first_child(widget);
    while (child != NULL)
    {
        if (widget_contains_label(child, text))
        {
            return TRUE;
        }
        child = gtk_widget_get_next_sibling(child);
    }

    return FALSE;
}

static guint
count_theme_cards(GtkWidget *widget)
{
    GtkWidget *child;
    guint count;

    count = GTK_IS_BUTTON(widget) &&
            gtk_widget_has_css_class(widget, "vellum-theme-card") ? 1 : 0;
    child = gtk_widget_get_first_child(widget);
    while (child != NULL)
    {
        count += count_theme_cards(child);
        child = gtk_widget_get_next_sibling(child);
    }

    return count;
}

static GtkWidget *
find_theme_card(GtkWidget *widget, const gchar *label)
{
    GtkWidget *child;

    if (GTK_IS_BUTTON(widget) &&
        gtk_widget_has_css_class(widget, "vellum-theme-card") &&
        widget_contains_label(widget, label))
    {
        return widget;
    }

    child = gtk_widget_get_first_child(widget);
    while (child != NULL)
    {
        GtkWidget *found;

        found = find_theme_card(child, label);
        if (found != NULL)
        {
            return found;
        }
        child = gtk_widget_get_next_sibling(child);
    }

    return NULL;
}

static GtkWindow *
find_toplevel_with_title(const gchar *title)
{
    GListModel *windows;
    guint index;

    windows = gtk_window_get_toplevels();
    for (index = 0; index < g_list_model_get_n_items(windows); index++)
    {
        GtkWindow *window;

        window = g_list_model_get_item(windows, index);
        if (g_strcmp0(gtk_window_get_title(window), title) == 0)
        {
            return window;
        }
        g_object_unref(window);
    }

    return NULL;
}

static void
test_theme_chooser(void)
{
    GError *error;
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    GtkWindow *preferences;
    GtkWindow *chooser;
    GtkWidget *choose_button;
    GtkWidget *cobalt_card;
    guint card_count;

    application = ADW_APPLICATION(adw_application_new("io.github.vellum.ThemeChooserTest",
                                                       G_APPLICATION_NON_UNIQUE));
    error = NULL;
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, &error));
    g_assert_no_error(error);
    settings = mt_settings_new();
    window = mt_window_new(application, settings);
    gtk_window_present(GTK_WINDOW(window->window));
    mt_window_action_preferences(NULL, NULL, window);
    spin_main_context();

    preferences = find_toplevel_with_title("Preferences");
    g_assert_nonnull(preferences);
    choose_button = find_button_with_label(GTK_WIDGET(preferences), "Choose");
    g_assert_nonnull(choose_button);
    g_signal_emit_by_name(choose_button, "clicked");
    spin_main_context();

    chooser = find_toplevel_with_title("Choose Code Theme");
    g_assert_nonnull(chooser);
    card_count = count_theme_cards(GTK_WIDGET(chooser));
    cobalt_card = find_theme_card(GTK_WIDGET(chooser), "Cobalt");
    g_assert_cmpuint(card_count, ==, 8);
    g_assert_nonnull(cobalt_card);
    g_signal_emit_by_name(cobalt_card, "clicked");
    g_assert_cmpstr(mt_settings_get_style_scheme(settings), ==, "cobalt");

    gtk_window_destroy(chooser);
    gtk_window_destroy(preferences);
    gtk_window_destroy(GTK_WINDOW(window->window));
    spin_main_context();
    mt_window_free(window);
    mt_settings_free(settings);
    g_object_unref(application);
}

static void
test_inline_completion_overlay_lifecycle(void)
{
    GError *error;
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    MtDocument *first_document;
    MtDocument *second_document;

    error = NULL;
    application = ADW_APPLICATION(adw_application_new("io.github.vellum.InlineCompletionTest",
                                                       G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, &error));
    g_assert_no_error(error);
    settings = mt_settings_new();
    window = mt_window_new(application, settings);
    gtk_window_present(GTK_WINDOW(window->window));
    mt_window_new_document(window);
    spin_main_context();

    first_document = mt_window_get_current_document(window);
    g_assert_nonnull(first_document);
    mt_window_show_inline_completion(window, "candidate");
    spin_main_context();
    g_assert_nonnull(first_document->inline_completion_label);
    g_assert_true(GTK_IS_LABEL(first_document->inline_completion_label));

    mt_window_new_document(window);
    spin_main_context();
    second_document = mt_window_get_current_document(window);
    g_assert_nonnull(second_document);
    g_assert_true(second_document != first_document);
    /* 覆盖层采用“隐藏复用”：切换文档后旧 label 隐藏而非销毁。 */
    g_assert_nonnull(first_document->inline_completion_label);
    g_assert_false(gtk_widget_get_visible(first_document->inline_completion_label));
    g_assert_null(window->inline_completion_document);

    mt_window_show_inline_completion(window, "next candidate");
    spin_main_context();
    g_assert_nonnull(second_document->inline_completion_label);
    g_assert_true(window->inline_completion_document == second_document);
    mt_window_clear_inline_completion(window);
    g_assert_nonnull(second_document->inline_completion_label);
    g_assert_false(gtk_widget_get_visible(second_document->inline_completion_label));
    g_assert_null(window->inline_completion_document);

    gtk_window_destroy(GTK_WINDOW(window->window));
    spin_main_context();
    mt_window_free(window);
    mt_settings_free(settings);
    g_object_unref(application);
}

static void
test_window_save_action(void)
{
    GError *error;
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GFile *file;
    gchar *directory;
    gchar *path;
    gchar *contents;

    error = NULL;
    directory = g_dir_make_tmp("vellum-window-save-test-XXXXXX", &error);
    g_assert_no_error(error);
    path = g_build_filename(directory, "window-save.txt", NULL);
    file = g_file_new_for_path(path);
    application = ADW_APPLICATION(adw_application_new("io.github.vellum.WindowSaveTest",
                                                       G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, &error));
    g_assert_no_error(error);
    settings = mt_settings_new();
    window = mt_window_new(application, settings);
    gtk_window_present(GTK_WINDOW(window->window));
    mt_window_new_document(window);
    document = mt_window_get_current_document(window);
    mt_document_set_file(document, file);
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_set_text(buffer, "Saved through the window action.\n", -1);

    mt_window_action_save(NULL, NULL, window);
    wait_until_saved(document);
    contents = NULL;
    g_assert_true(g_file_get_contents(path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, "Saved through the window action.\n");

    g_free(contents);
    gtk_window_destroy(GTK_WINDOW(window->window));
    spin_main_context();
    mt_window_free(window);
    mt_settings_free(settings);
    g_object_unref(application);
    g_object_unref(file);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

static void
test_big_document_theme_switch(void)
{
    GError *error;
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    MtDocument *document;
    GtkSourceBuffer *buffer;
    GFile *file;
    GtkWindow *preferences;
    GtkWindow *chooser;
    GtkWidget *choose_button;
    GtkWidget *cobalt_card;
    gchar *directory;
    gchar *path;
    GString *big;
    gint64 start;
    gint64 elapsed;
    gint index;

    error = NULL;
    directory = g_dir_make_tmp("vellum-big-theme-XXXXXX", &error);
    g_assert_no_error(error);
    path = g_build_filename(directory, "big-theme.c", NULL);
    file = g_file_new_for_path(path);
    application = ADW_APPLICATION(adw_application_new("io.github.vellum.BigThemeTest",
                                                       G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, &error));
    g_assert_no_error(error);
    settings = mt_settings_new();
    mt_settings_set_show_overview(settings, TRUE);
    window = mt_window_new(application, settings);
    gtk_window_present(GTK_WINDOW(window->window));
    mt_window_new_document(window);
    document = mt_window_get_current_document(window);
    g_assert_nonnull(document);
    mt_document_set_file(document, file);
    mt_document_guess_language(document);
    buffer = mt_document_get_buffer(document);
    g_assert_nonnull(gtk_source_buffer_get_language(buffer));

    big = g_string_new(NULL);
    for (index = 0; index < 9000; index++)
    {
        g_string_append_printf(big,
            "static gint func_%d(gint value)\n"
            "{\n"
            "    if (value > 42 && value < 1000) { return value * 2; }\n"
            "    else if (value < 42) { return value + 1; }\n"
            "    return 0;\n"
            "}\n\n", index);
    }
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer), big->str, -1);
    g_string_free(big, TRUE);

    spin_main_context();
    mt_window_action_preferences(NULL, NULL, window);
    spin_main_context();
    preferences = find_toplevel_with_title("Preferences");
    g_assert_nonnull(preferences);
    choose_button = find_button_with_label(GTK_WIDGET(preferences), "Choose");
    g_assert_nonnull(choose_button);
    g_signal_emit_by_name(choose_button, "clicked");
    spin_main_context();
    chooser = find_toplevel_with_title("Choose Code Theme");
    g_assert_nonnull(chooser);
    cobalt_card = find_theme_card(GTK_WIDGET(chooser), "Cobalt");
    g_assert_nonnull(cobalt_card);

    start = g_get_monotonic_time();
    g_signal_emit_by_name(cobalt_card, "clicked");
    elapsed = g_get_monotonic_time() - start;
    g_print("THEME CARD CLICK on %d-char buffer: %lld ms\n",
            gtk_text_buffer_get_char_count(GTK_TEXT_BUFFER(buffer)),
            (long long)(elapsed / 1000));
    {
        gint64 drain_start;
        guint drain_rounds;

        drain_start = g_get_monotonic_time();
        drain_rounds = 0;
        {
            gint64 last_progress;

            last_progress = drain_start;
            while (g_main_context_iteration(NULL, FALSE))
            {
                drain_rounds++;
                if (g_get_monotonic_time() - last_progress > G_TIME_SPAN_SECOND)
                {
                    g_print("  drain ... %lld ms (%u rounds)\n",
                            (long long)((g_get_monotonic_time() - drain_start) / 1000),
                            drain_rounds);
                    last_progress = g_get_monotonic_time();
                }
                if (g_get_monotonic_time() - drain_start > 120 * G_TIME_SPAN_SECOND)
                {
                    g_print("DRAIN TIMEOUT after %u rounds\n", drain_rounds);
                    break;
                }
            }
        }
        g_print("DRAIN took %lld ms (%u rounds)\n",
                (long long)((g_get_monotonic_time() - drain_start) / 1000),
                drain_rounds);
    }

    {
        gint64 appearance_start;

        appearance_start = g_get_monotonic_time();
        mt_settings_set_appearance(settings, MT_APPEARANCE_DARK);
        mt_settings_apply_appearance(settings);
        g_print("APPEARANCE DARK SWITCH: %lld ms\n",
                (long long)((g_get_monotonic_time() - appearance_start) / 1000));
        appearance_start = g_get_monotonic_time();
        mt_settings_set_appearance(settings, MT_APPEARANCE_LIGHT);
        mt_settings_apply_appearance(settings);
        g_print("APPEARANCE LIGHT SWITCH: %lld ms\n",
                (long long)((g_get_monotonic_time() - appearance_start) / 1000));
    }

    gtk_window_destroy(chooser);
    gtk_window_destroy(preferences);
    gtk_window_destroy(GTK_WINDOW(window->window));
    spin_main_context();
    mt_window_free(window);
    mt_settings_free(settings);
    g_object_unref(application);
    g_object_unref(file);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

int
main(int argc, char **argv)
{
    g_mkdir_with_parents("/tmp/vellum-theme-chooser-test", 0700);
    g_setenv("XDG_CONFIG_HOME", "/tmp/vellum-theme-chooser-test", TRUE);
    gtk_init();
    adw_init();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vellum/theme-chooser/cards-and-persistence", test_theme_chooser);
    g_test_add_func("/vellum/window/inline-completion-overlay-lifecycle", test_inline_completion_overlay_lifecycle);
    g_test_add_func("/vellum/window/save-action-completes", test_window_save_action);
    g_test_add_func("/vellum/window/big-document-theme-switch", test_big_document_theme_switch);

    return g_test_run();
}
