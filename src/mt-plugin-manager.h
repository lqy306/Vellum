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

MtPluginManager *mt_plugin_manager_new(MtApplication *application);
void mt_plugin_manager_free(MtPluginManager *manager);
guint mt_plugin_manager_get_count(MtPluginManager *manager);
const MtPluginInfo *mt_plugin_manager_get_info(MtPluginManager *manager, guint index);
const gchar *mt_plugin_manager_get_path(MtPluginManager *manager, guint index);
gboolean mt_plugin_manager_is_user_managed(MtPluginManager *manager, guint index);
gboolean mt_plugin_manager_is_enabled(MtPluginManager *manager, guint index);
gboolean mt_plugin_manager_set_enabled(MtPluginManager *manager,
                                       guint index,
                                       gboolean enabled,
                                       GError **error);
gboolean mt_plugin_manager_handle_key(MtPluginManager *manager,
                                      guint keyval,
                                      guint keycode,
                                      guint state);
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
