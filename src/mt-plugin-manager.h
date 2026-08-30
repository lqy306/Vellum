/*
 * mt-plugin-manager.h
 * 动态模块发现与生命周期管理接口。
 */

#ifndef MT_PLUGIN_MANAGER_H
#define MT_PLUGIN_MANAGER_H

#include "mt-application.h"
#include "mt-plugin.h"

G_BEGIN_DECLS

typedef struct _MtPluginManager MtPluginManager;

/* 插件注册到“偏好设置”页面的开关行；字段由插件在激活时提供。 */
typedef struct _MtPreferenceSwitch
{
    gchar *group;
    gchar *title;
    gchar *subtitle;
    MtPluginPreferenceGetFunc get_callback;
    MtPluginPreferenceSetFunc set_callback;
    gpointer user_data;
    GDestroyNotify destroy_notify;
} MtPreferenceSwitch;

/* 扩展市场条目：来自某个扩展源（目录）的插件。base 为该源资产下载地址。 */
typedef struct _MtMarketplaceEntry
{
    gchar *id;
    gchar *name;
    gchar *description;
    gchar *version;
    gchar *binary;   /* 二进制资产名（相对 base），可为 NULL */
    gchar *source;   /* 源码包资产名（相对 base），可为 NULL */
    gchar *base;     /* 所属源的资产下载地址 */
} MtMarketplaceEntry;

MtPluginManager *mt_plugin_manager_new(MtApplication *application);
void mt_plugin_manager_free(MtPluginManager *manager);
/* —— 扩展市场：核心内置、不可卸载；支持多个扩展源（类似 apt 多源）。 —— */
GPtrArray *mt_plugin_manager_get_marketplace(MtPluginManager *manager);
void mt_plugin_manager_marketplace_refresh_async(MtPluginManager *manager,
                                                 GCancellable *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mt_plugin_manager_marketplace_refresh_finish(MtPluginManager *manager,
                                                      GAsyncResult *result,
                                                      GError **error);
void mt_plugin_manager_marketplace_install_async(MtPluginManager *manager,
                                                 MtMarketplaceEntry *entry,
                                                 gboolean prefer_source,
                                                 GCancellable *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data);
gboolean mt_plugin_manager_marketplace_install_finish(MtPluginManager *manager,
                                                      GAsyncResult *result,
                                                      GError **error);
gboolean mt_plugin_manager_marketplace_uninstall(MtPluginManager *manager,
                                                  MtMarketplaceEntry *entry,
                                                  GError **error);
/* 默认扩展源地址（可用环境变量 VELLUM_MARKETPLACE_URL 覆盖）。 */
const gchar *mt_plugin_manager_marketplace_default_base(void);
/* 默认（官方）源是否启用；用户可在“扩展”页关闭，不强制开启。 */
gboolean mt_plugin_manager_get_default_source_enabled(MtPluginManager *manager);
void mt_plugin_manager_set_default_source_enabled(MtPluginManager *manager, gboolean enabled);
/* 当前生效的全部扩展源（默认源在前，其余为用户在 market-sources.ini 中配置的）。 */
GPtrArray *mt_plugin_manager_get_marketplace_sources(MtPluginManager *manager);
/* 写入用户额外扩展源（不含默认源）；用于“扩展”页管理安装源。 */
void mt_plugin_manager_set_user_sources(MtPluginManager *manager,
                                         gchar * const *urls,
                                         gsize count);
guint mt_plugin_manager_get_count(MtPluginManager *manager);
const MtPluginInfo *mt_plugin_manager_get_info(MtPluginManager *manager, guint index);
const gchar *mt_plugin_manager_get_path(MtPluginManager *manager, guint index);
gboolean mt_plugin_manager_is_user_managed(MtPluginManager *manager, guint index);
gboolean mt_plugin_manager_is_enabled(MtPluginManager *manager, guint index);
gboolean mt_plugin_manager_has_plugin(MtPluginManager *manager, const gchar *id);
gboolean mt_plugin_manager_set_enabled(MtPluginManager *manager,
                                       guint index,
                                       gboolean enabled,
                                       GError **error);
gboolean mt_plugin_manager_set_enabled_by_id(MtPluginManager *manager,
                                             const gchar *plugin_id,
                                             gboolean enabled,
                                             GError **error);
gboolean mt_plugin_manager_handle_key(MtPluginManager *manager,
                                      guint keyval,
                                      guint keycode,
                                      guint state);
/* 向已启用扩展分发活动代码文档的用户编辑行数。 */
void mt_plugin_manager_notify_document_changed(MtPluginManager *manager,
                                               guint changed_lines);
gboolean mt_plugin_manager_import(MtPluginManager *manager, GFile *source, GError **error);
gboolean mt_plugin_manager_export(MtPluginManager *manager,
                                  guint index,
                                  GFile *destination,
                                  GError **error);
gboolean mt_plugin_manager_remove(MtPluginManager *manager, guint index, GError **error);
gboolean mt_plugin_manager_has_configure(MtPluginManager *manager, guint index);
void mt_plugin_manager_configure(MtPluginManager *manager, guint index, GtkWindow *parent);
guint mt_plugin_manager_get_preference_switch_count(MtPluginManager *manager);
const MtPreferenceSwitch *mt_plugin_manager_get_preference_switch(MtPluginManager *manager,
                                                                   guint index);

G_END_DECLS

#endif
