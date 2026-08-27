/*
 * test-document.c
 * Vellum 文档模型的行为级回归测试。
 */

#include "../src/mt-document.h"

static gboolean
quit_main_loop(gpointer user_data)
{
    GMainLoop *loop;

    loop = user_data;
    g_main_loop_quit(loop);

    return G_SOURCE_REMOVE;
}

static void
run_pending_analysis(void)
{
    GMainLoop *loop;

    loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(750, quit_main_loop, loop);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
}

static void
test_tab_is_character_and_width_is_visual(void)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;

    document = mt_document_new();
    mt_document_set_tab_width(document, 8);
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));

    g_assert_false(gtk_source_view_get_insert_spaces_instead_of_tabs(
                       GTK_SOURCE_VIEW(mt_document_get_view(document))));
    g_assert_cmpint(gtk_source_view_get_tab_width(
                        GTK_SOURCE_VIEW(mt_document_get_view(document))), ==, 8);

    /* 此处模拟编辑动作写入 Tab；配置决定编辑器不会将其展开为空格。 */
    gtk_text_buffer_insert_at_cursor(buffer, "\t", -1);
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_assert_cmpstr(text, ==, "\t");

    g_free(text);
    mt_document_free(document);
}

static gboolean
send_key_to_document(MtDocument *document, guint keyval)
{
    GListModel *controllers;
    guint index;
    gboolean handled;

    controllers = gtk_widget_observe_controllers(mt_document_get_view(document));
    handled = FALSE;
    for (index = 0; index < g_list_model_get_n_items(controllers); index++)
    {
        GObject *controller;

        controller = g_list_model_get_item(controllers, index);
        if (GTK_IS_EVENT_CONTROLLER_KEY(controller) &&
            g_object_get_data(controller, "vellum-smart-pair-controller") != NULL)
        {
            gboolean controller_handled;

            controller_handled = FALSE;
            g_signal_emit_by_name(controller,
                                  "key-pressed",
                                  keyval,
                                  0,
                                  (GdkModifierType)0,
                                  &controller_handled);
            handled = handled || controller_handled;
        }
        g_object_unref(controller);
    }
    g_object_unref(controllers);

    return handled;
}

static void
test_smart_pairs_only_for_code(void)
{
    MtDocument *code_document;
    MtDocument *plain_document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    GtkTextIter cursor;
    gchar *text;

    code_document = mt_document_new();
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(code_document));
    gtk_text_buffer_set_text(buffer, "int main(void) {}\n", -1);
    run_pending_analysis();
    g_assert_nonnull(gtk_source_buffer_get_language(mt_document_get_buffer(code_document)));
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_place_cursor(buffer, &end);
    g_assert_true(send_key_to_document(code_document, GDK_KEY_less));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_assert_true(g_str_has_suffix(text, "<>"));
    gtk_text_buffer_get_iter_at_mark(buffer,
                                     &cursor,
                                     gtk_text_buffer_get_insert(buffer));
    g_assert_cmpint(gtk_text_iter_get_offset(&cursor), ==,
                    gtk_text_iter_get_offset(&end) - 1);
    g_free(text);
    mt_document_free(code_document);

    plain_document = mt_document_new();
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(plain_document));
    g_assert_false(send_key_to_document(plain_document, GDK_KEY_parenleft));
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    g_assert_cmpstr(text, ==, "");
    g_free(text);
    mt_document_free(plain_document);
}

static void
test_content_analysis_and_diagnostic_tag(void)
{
    const gchar *source;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkSourceLanguage *language;
    GtkTextTag *tag;
    GtkTextIter iter;
    gint opening_offset;

    source = "def greeting():\n    return (42\n";
    document = mt_document_new();
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_set_text(buffer, source, -1);
    run_pending_analysis();

    language = gtk_source_buffer_get_language(mt_document_get_buffer(document));
    g_assert_nonnull(language);
    g_assert_true(g_strcmp0(gtk_source_language_get_id(language), "python") == 0 ||
                  g_strcmp0(gtk_source_language_get_id(language), "python3") == 0);

    tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer),
                                    "vellum-diagnostic-error");
    g_assert_nonnull(tag);
    opening_offset = (gint)g_utf8_pointer_to_offset(source, g_strrstr(source, "("));
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, opening_offset);
    g_assert_true(gtk_text_iter_has_tag(&iter, tag));

    mt_document_free(document);
}

int
main(int argc, char **argv)
{
    gtk_init();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vellum/document/tab-character", test_tab_is_character_and_width_is_visual);
    g_test_add_func("/vellum/document/content-analysis", test_content_analysis_and_diagnostic_tag);
    g_test_add_func("/vellum/document/smart-pairs", test_smart_pairs_only_for_code);

    return g_test_run();
}
