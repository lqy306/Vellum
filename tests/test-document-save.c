/*
 * test-document-save.c
 * 验证文档保存不会卡住主循环，并且异步回调会完成。
 */

#include <adwaita.h>
#include <glib/gstdio.h>

#include "mt-document.h"

typedef struct
{
    MtDocument *document;
    gboolean completed;
    gboolean success;
    GError *error;
} SaveResult;

static void
save_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SaveResult *save_result;

    (void)source;

    save_result = user_data;
    save_result->success = mt_document_save_finish(save_result->document,
                                                   result,
                                                   &save_result->error);
    save_result->completed = TRUE;
}

static void
wait_for_save(SaveResult *save_result)
{
    gint attempts;

    for (attempts = 0; attempts < 2000 && !save_result->completed; attempts++)
    {
        g_main_context_iteration(NULL, TRUE);
    }

    g_assert_true(save_result->completed);
}

static void
test_document_save_completes(void)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GFile *target;
    SaveResult save_result;
    gchar *directory;
    gchar *path;
    gchar *contents;
    gsize length;
    GError *error;

    error = NULL;
    directory = g_dir_make_tmp("vellum-save-test-XXXXXX", &error);
    g_assert_no_error(error);
    path = g_build_filename(directory, "saved.c", NULL);
    target = g_file_new_for_path(path);
    document = mt_document_new();
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_set_text(buffer, "int main(void) { return 0; }\n", -1);

    save_result.document = document;
    save_result.completed = FALSE;
    save_result.success = FALSE;
    save_result.error = NULL;
    mt_document_save_async(document, target, save_finished, &save_result);
    wait_for_save(&save_result);

    g_assert_true(save_result.success);
    g_assert_no_error(save_result.error);
    g_assert_false(mt_document_is_modified(document));
    contents = NULL;
    length = 0;
    g_assert_true(g_file_get_contents(path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, "int main(void) { return 0; }\n");

    g_free(contents);
    mt_document_free(document);
    g_object_unref(target);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

int
main(int argc, char **argv)
{
    gtk_init();
    adw_init();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vellum/document/save-completes", test_document_save_completes);

    return g_test_run();
}
