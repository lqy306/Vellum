/*
 * mt-plugin-manager-private.h
 * 插件管理器的内部共享声明。
 */

#ifndef MT_PLUGIN_MANAGER_PRIVATE_H
#define MT_PLUGIN_MANAGER_PRIVATE_H

#include "mt-plugin-manager.h"
#include "mt-application.h"
#include "mt-plugin.h"

#include <gmodule.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

G_BEGIN_DECLS

/* —— 内部结构 —— */

typedef struct _MtLoadedPlugin MtLoadedPlugin;
typedef struct _MtPluginActionData MtPluginActionData;
typedef struct _MtPluginKeyData MtPluginKeyData;
typedef struct _MtPluginDocumentChangeData MtPluginDocumentChangeData;

struct _MtPluginActionData
{
    MtPluginActionCallback callback;
    gpointer user_data;
    GDestroyNotify destroy_notify;
};

struct _MtPluginKeyData
{
    MtPluginKeyCallback callback;
    gpointer user_data;
    GDestroyNotify destroy_notify;
};

struct _MtPluginDocumentChangeData
{
    MtPluginDocumentChangeFunc callback;
    gpointer user_data;
    GDestroyNotify destroy_notify;
};

struct _MtLoadedPlugin
{
    GModule *module;
    const MtPluginInfo *info;
    MtPluginDeactivateFunc deactivate;
    MtPluginConfigureFunc configure;
    gchar *path;
    gboolean user_managed;
    gboolean enabled;
    GPtrArray *action_names;
    GPtrArray *key_handlers;
    GPtrArray *document_change_handlers;
};

typedef struct _MtPluginBootstrap MtPluginBootstrap;

struct _MtPluginManager
{
    MtApplication *application;
    MtPluginHost host;
    GPtrArray *plugins;
    GHashTable *disabled_ids;
    GHashTable *removed_ids;
    MtLoadedPlugin *loading_plugin;
    GPtrArray *preference_switches;
    MtPluginBootstrap *bootstrap;
    gboolean bootstrap_needed;
    GPtrArray *marketplace;   /* MtMarketplaceEntry*，NULL = 尚未拉取目录 */
};

struct _MtPluginBootstrap
{
    MtPluginManager *manager;
    SoupSession *session;
    GPtrArray *names;      /* 待下载的文件名（含 .so 或 .vut 后缀） */
    guint index;
    const gchar *current;  /* 正在下载的文件名（借用 names 的引用） */
    gchar *directory;      /* 用户插件目录 */
    guint downloaded;
    gboolean cancelled;
    gboolean prefer_source;
};

typedef struct
{
    const gchar *file;     /* 构建产物文件名（不含后缀） */
    const gchar *id;       /* 插件稳定标识：已删除的插件不再补拉 */
} MtPluginBootstrapEntry;

static const MtPluginBootstrapEntry mt_plugin_bootstrap_entries[] = {
    { "timestamp-plugin",       "io.github.vellum.timestamp" },
    { "word-count-plugin",      "io.github.vellum.document-statistics" },
    { "ai-completion-plugin",   "io.github.vellum.ai-completion" },
    { "link-check-plugin",      "io.github.vellum.link-check" },
    { "project-sidebar-plugin", "io.github.vellum.project-sidebar" },
    { "dev-experience-plugin",  "io.github.vellum.dev-experience" },
    { "vim-mode-plugin",        "io.github.vellum.vim-mode" },
    { "screenshot-plugin",      "io.github.vellum.screenshot" },
    { "welcome-plugin",         "io.github.vellum.welcome" }
};

/* —— 引导下载辅助 —— */
gboolean mt_bootstrap_is_x64(void);
static gboolean mt_bootstrap_should_prefer_source(void);
static const gchar *mt_bootstrap_source_asset_for(const gchar *file);

/* —— 引导下载（由 mt-plugin-manager-bootstrap.c 实现） —— */
void mt_plugin_manager_bootstrap_start(MtPluginManager *manager);
void mt_plugin_manager_bootstrap_finish(MtPluginBootstrap *bootstrap);

/* —— 插件生命周期（由 mt-plugin-manager-lifecycle.c 实现，供 bootstrap/marketplace 调用） —— */
void mt_loaded_plugin_free(MtLoadedPlugin *plugin);
void mt_preference_switch_free(MtPreferenceSwitch *item);
void mt_plugin_manager_load_state(MtPluginManager *manager);
void mt_plugin_manager_key_data_free(MtPluginKeyData *data);
void mt_plugin_manager_document_change_data_free(MtPluginDocumentChangeData *data);
MtLoadedPlugin *mt_plugin_manager_get_loaded_plugin(MtPluginManager *manager, guint index);
gchar *mt_plugin_manager_resolve_system_directory(void);
gboolean mt_plugin_manager_add_preference_switch(MtPluginHost *host,
                                                 const gchar *group_title,
                                                 const gchar *title,
                                                 const gchar *subtitle,
                                                 MtPluginPreferenceGetFunc get_callback,
                                                 MtPluginPreferenceSetFunc set_callback,
                                                 gpointer user_data,
                                                 GDestroyNotify destroy_notify);
gboolean mt_plugin_manager_set_extension_enabled(MtPluginHost *host,
                                                 const gchar *plugin_id,
                                                 gboolean enabled,
                                                 GError **error);
void mt_plugin_manager_load_directory(MtPluginManager *manager,
                                      const gchar *directory,
                                      gboolean user_managed);
gboolean mt_plugin_manager_load_module(MtPluginManager *manager,
                                       const gchar *path,
                                       gboolean user_managed);
void mt_plugin_manager_remove_plugin_actions(MtPluginManager *manager,
                                             MtLoadedPlugin *plugin);
void mt_plugin_manager_unload_plugin(MtPluginManager *manager,
                                     MtLoadedPlugin *plugin);
gboolean mt_plugin_manager_mark_removed(MtPluginManager *manager,
                                        MtLoadedPlugin *plugin,
                                        GError **error);
gboolean mt_plugin_manager_save_state(MtPluginManager *manager, GError **error);
void mt_plugin_manager_sync_plugin_menu(MtPluginManager *manager);

/* —— 插件生命周期扩展（由 mt-plugin-manager-lifecycle.c 实现） —— */
gboolean mt_plugin_manager_import(MtPluginManager *manager, GFile *source, GError **error);
gboolean mt_plugin_manager_export(MtPluginManager *manager,
                                  guint index,
                                  GFile *destination,
                                  GError **error);
gboolean mt_plugin_manager_remove(MtPluginManager *manager, guint index, GError **error);
gboolean mt_plugin_manager_has_configure(MtPluginManager *manager, guint index);
void mt_plugin_manager_configure(MtPluginManager *manager, guint index, GtkWindow *parent);
gboolean mt_plugin_manager_has_plugin(MtPluginManager *manager, const gchar *id);
void mt_plugin_manager_request_plugin_removal(MtPluginHost *host, const gchar *plugin_id);

/* —— 市场系统（由 mt-plugin-manager-marketplace.c 实现） —— */
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
GPtrArray *mt_plugin_manager_get_marketplace_sources(MtPluginManager *manager);

G_END_DECLS

#endif /* MT_PLUGIN_MANAGER_PRIVATE_H */