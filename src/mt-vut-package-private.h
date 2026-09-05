/*
 * mt-vut-package-private.h
 * `.vut` 原生扩展 ZIP 包的内部共享声明。
 */

#ifndef MT_VUT_PACKAGE_PRIVATE_H
#define MT_VUT_PACKAGE_PRIVATE_H

#include <glib.h>
#include <glib/gstdio.h>
#include <zlib.h>

#define MT_VUT_MANIFEST_NAME "vellum-extension.ini"
#define MT_VUT_MAX_ARCHIVE_SIZE (32 * 1024 * 1024)
#define MT_VUT_MAX_ENTRY_SIZE (16 * 1024 * 1024)
#define MT_VUT_MAX_ENTRIES 256

typedef struct _MtVutEntry MtVutEntry;

struct _MtVutEntry
{
    gchar *name;
    guint16 flags;
    guint16 method;
    guint32 crc;
    guint32 compressed_size;
    guint32 uncompressed_size;
    guint32 local_offset;
};

/* 读取辅助 */
guint16 mt_vut_read_u16(const guchar *data);
guint32 mt_vut_read_u32(const guchar *data);

/* 写入辅助 */
void mt_vut_append_u16(GByteArray *array, guint16 value);
void mt_vut_append_u32(GByteArray *array, guint32 value);

/* 安全校验 */
gboolean mt_vut_is_safe_entry_name(const gchar *name);
gboolean mt_vut_is_safe_archive_name(const gchar *name);
gboolean mt_vut_is_safe_identifier(const gchar *identifier);

/* 条目释放 */
void mt_vut_entry_free(MtVutEntry *entry);

/* 解析与提取（由 mt-vut-package-parse.c 实现） */
GPtrArray *mt_vut_parse_entries(const guchar *data,
                                gsize length,
                                GError **error);
MtVutEntry *mt_vut_find_entry(GPtrArray *entries, const gchar *name);
gboolean mt_vut_extract_entry(const guchar *archive,
                              gsize archive_length,
                              MtVutEntry *entry,
                              gchar **contents_out,
                              gsize *length_out,
                              GError **error);

/* 清单与安装（由 mt-vut-package-manifest.c 实现） */
gboolean mt_vut_validate_manifest(const gchar *contents,
                                  gsize length,
                                  gchar **module_name_out,
                                  gchar **extension_id_out,
                                  GError **error);
gboolean mt_vut_install_module(const gchar *destination_directory,
                               const gchar *extension_id,
                               const gchar *contents,
                               gsize length,
                               gchar **module_path_out,
                               GError **error);
void mt_vut_remove_tree(const gchar *path);
gboolean mt_vut_validate_build_arguments(const gchar *arguments,
                                         gchar ***arguments_out,
                                         GError **error);
gboolean mt_vut_check_required_tools(GKeyFile *settings, GError **error);
gboolean mt_vut_extract_source_tree(const guchar *archive,
                                    gsize archive_length,
                                    GPtrArray *entries,
                                    const gchar *source_root,
                                    const gchar *temporary_directory,
                                    GError **error);
gboolean mt_vut_import_source_package(const guchar *archive,
                                      gsize archive_length,
                                      GPtrArray *entries,
                                      const gchar *manifest_contents,
                                      gsize manifest_length,
                                      const gchar *destination_directory,
                                      gchar **module_path_out,
                                      gchar **extension_id_out,
                                      GError **error);

#endif /* MT_VUT_PACKAGE_PRIVATE_H */