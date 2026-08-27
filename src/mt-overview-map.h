/*
 * mt-overview-map.h
 * 可交互代码概览图：显示主编辑器当前可视区域，并支持拖拽定位。
 */

#ifndef MT_OVERVIEW_MAP_H
#define MT_OVERVIEW_MAP_H

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

/* 返回一个 GtkOverlay：底层为 GtkSourceMap，顶层是半透明、可拖拽的可视区域。 */
GtkWidget *mt_overview_map_new(GtkSourceView *source_view);

G_END_DECLS

#endif
