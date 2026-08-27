/*
 * mt-vut-package.h
 * `.vut` 原生扩展 ZIP 包的安全导入与导出接口。
 */

#ifndef MT_VUT_PACKAGE_H
#define MT_VUT_PACKAGE_H

#include <glib.h>

#include "mt-plugin.h"

G_BEGIN_DECLS

gboolean mt_vut_package_import(const gchar *archive_path,
                               const gchar *destination_directory,
                               gchar **module_path_out,
                               gchar **extension_id_out,
                               GError **error);
gboolean mt_vut_package_export(const gchar *module_path,
                               const MtPluginInfo *info,
                               const gchar *destination_path,
                               GError **error);
const gchar *mt_vut_package_get_host_os(void);
const gchar *mt_vut_package_get_host_architecture(void);
const gchar *mt_vut_package_get_host_abi(void);

G_END_DECLS

#endif
