/*
 * mt-document.h
 * 文档模型：封装 GtkSourceBuffer、文件位置、标签标题和安全草稿快照。
 */

#ifndef MT_DOCUMENT_H
#define MT_DOCUMENT_H

#include <adwaita.h>
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

typedef struct _MtDocument MtDocument;
typedef struct _MtSettings MtSettings;
typedef struct _SpellingChecker SpellingChecker;
typedef struct _SpellingTextBufferAdapter SpellingTextBufferAdapter;

struct _MtDocument
{
    GtkSourceBuffer *buffer;
    GtkWidget *view;
    SpellingChecker *spell_checker;
    SpellingTextBufferAdapter *spell_adapter;
    GtkWidget *overview;
    GtkSourceFile *source_file;
    GtkWidget *inline_completion_label;
    GtkSourceFileLoader *loader;
    GFileMonitor *file_monitor;
    gint64 monitor_suppress_until;
    GFile *save_target;
    gchar *save_contents;
    AdwTabPage *page;
    gchar *display_name;
    gchar *draft_path;
    guint analysis_source_id;
    gboolean is_draft;
    gboolean saving;
    /* 是否在代码文档中自动配对括号/引号（由“首选项 → 行为”同步）。 */
    gboolean auto_pair_brackets;
};

MtDocument *mt_document_new(void);
void mt_document_free(MtDocument *document);

GtkWidget *mt_document_get_view(MtDocument *document);
void mt_document_set_tab_width(MtDocument *document, gint tab_width);
void mt_document_apply_editor_settings(MtDocument *document, MtSettings *settings);
GtkSourceBuffer *mt_document_get_buffer(MtDocument *document);
GFile *mt_document_get_file(MtDocument *document);
const gchar *mt_document_get_display_name(MtDocument *document);
gboolean mt_document_is_modified(MtDocument *document);
gboolean mt_document_is_untitled(MtDocument *document);
gboolean mt_document_is_saving(MtDocument *document);

void mt_document_set_page(MtDocument *document, AdwTabPage *page);
void mt_document_set_display_name(MtDocument *document, const gchar *name);
void mt_document_set_file(MtDocument *document, GFile *file);
void mt_document_guess_language(MtDocument *document);
void mt_document_schedule_analysis(MtDocument *document);

void mt_document_load_async(MtDocument *document,
                            GFile *file,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
void mt_document_reload_with_encoding(MtDocument *document,
                                      const GtkSourceEncoding *encoding,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
gboolean mt_document_load_finish(MtDocument *document,
                                 GAsyncResult *result,
                                 GError **error);

void mt_document_save_async(MtDocument *document,
                            GFile *target,
                            GAsyncReadyCallback callback,
                            gpointer user_data);
gboolean mt_document_save_finish(MtDocument *document,
                                 GAsyncResult *result,
                                 GError **error);

void mt_document_snapshot(MtDocument *document);
gboolean mt_document_restore_snapshot(MtDocument *document,
                                     const gchar *path,
                                     GError **error);
void mt_document_remove_snapshot(MtDocument *document);
GPtrArray *mt_document_list_snapshots(void);

G_END_DECLS

#endif
