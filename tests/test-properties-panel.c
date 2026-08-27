/*
 * test-properties-panel.c
 * 复现“从菜单打开文档属性”路径：win.toggle-properties 动作
 * 触发 properties_button 的 toggled 信号，进而显示/隐藏右侧属性面板。
 */

#include <adwaita.h>

#include "mt-window-private.h"

static void
spin_main_context(void)
{
    gint index;

    for (index = 0; index < 20; index++)
    {
        while (g_main_context_iteration(NULL, FALSE))
        {
        }
    }
}

static gboolean
stack_has_properties_page(GtkStack *stack)
{
    guint index;
    guint count;

    count = g_list_model_get_n_items(G_LIST_MODEL(gtk_stack_get_pages(stack)));
    for (index = 0; index < count; index++)
    {
        GtkStackPage *page;
        GtkWidget *child;

        page = g_list_model_get_item(G_LIST_MODEL(gtk_stack_get_pages(stack)), index);
        child = gtk_stack_page_get_child(page);
        g_object_unref(page);
        if (child != NULL && gtk_widget_has_css_class(child, "vellum-properties"))
        {
            return TRUE;
        }
    }

    return FALSE;
}

static void
test_toggle_properties_from_menu(void)
{
    GError *error;
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    gint round;

    error = NULL;
    application = ADW_APPLICATION(adw_application_new("io.github.vellum.PropertiesPanelTest",
                                                       G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(application), NULL, &error));
    g_assert_no_error(error);
    settings = mt_settings_new();
    window = mt_window_new(application, settings);
    gtk_window_present(GTK_WINDOW(window->window));
    mt_window_new_document(window);
    spin_main_context();

    g_assert_true(ADW_IS_OVERLAY_SPLIT_VIEW(window->auxiliary_split));
    g_assert_cmpfloat(adw_overlay_split_view_get_min_sidebar_width(window->auxiliary_split), ==, 300.0);
    g_assert_cmpfloat(adw_overlay_split_view_get_max_sidebar_width(window->auxiliary_split), ==, 300.0);

    for (round = 0; round < 3; round++)
    {
        g_print("round %d: activating show\n", round);
        g_assert_true(gtk_widget_activate_action(GTK_WIDGET(window->window),
                                                 "win.toggle-properties",
                                                 NULL));
        g_print("round %d: shown, spinning\n", round);
        spin_main_context();
        g_assert_true(gtk_widget_get_visible(GTK_WIDGET(window->auxiliary_stack)));
        g_assert_true(adw_overlay_split_view_get_show_sidebar(window->auxiliary_split));
        g_assert_true(stack_has_properties_page(window->auxiliary_stack));

        g_print("round %d: activating hide\n", round);
        g_assert_true(gtk_widget_activate_action(GTK_WIDGET(window->window),
                                                 "win.toggle-properties",
                                                 NULL));
        spin_main_context();
        g_print("round %d: hidden\n", round);
        g_assert_false(adw_overlay_split_view_get_show_sidebar(window->auxiliary_split));
        g_assert_false(stack_has_properties_page(window->auxiliary_stack));
    }

    gtk_window_destroy(GTK_WINDOW(window->window));
    spin_main_context();
    mt_window_free(window);
    mt_settings_free(settings);
    g_object_unref(application);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/vellum/properties/from-menu-toggle", test_toggle_properties_from_menu);

    return g_test_run();
}
