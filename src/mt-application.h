/*
 * mt-application.h
 * 应用对象负责 GTK 生命周期、窗口创建和跨窗口服务入口。
 */

#ifndef MT_APPLICATION_H
#define MT_APPLICATION_H

#include <adwaita.h>

#include "mt-settings.h"
#include "mt-window.h"

G_BEGIN_DECLS

typedef struct _MtApplication MtApplication;

struct _MtApplication
{
    AdwApplication *application;
    MtSettings *settings;
    MtWindow *window;
    gpointer plugin_manager;
};

MtApplication *mt_application_new(MtSettings *settings);
void mt_application_free(MtApplication *application);
int mt_application_run(MtApplication *application, int argc, char **argv);

AdwApplication *mt_application_get_gtk_application(MtApplication *application);
MtWindow *mt_application_get_active_window(MtApplication *application);
void mt_application_add_action(MtApplication *application,
                               GAction *action);

G_END_DECLS

#endif
