/*
 * mt-overview-map.c
 * 将 GtkSourceMap 的导航能力封装为可重用组件。可视区域始终映射活动编辑器的
 * 垂直滚动范围，用户可点击或拖动该区域跳转到相应代码位置。
 */

#include "mt-overview-map.h"

#include <gtk/gtk.h>

typedef struct _MtOverviewMapData
{
    GtkAdjustment *adjustment;
    GtkWidget *viewport;
    gdouble drag_offset;
} MtOverviewMapData;

static void
mt_overview_map_data_free(gpointer user_data)
{
    MtOverviewMapData *data;

    data = user_data;
    g_clear_object(&data->adjustment);
    g_free(data);
}

static void
mt_overview_map_get_rect(MtOverviewMapData *data,
                         GtkWidget *widget,
                         gdouble *top,
                         gdouble *height)
{
    gdouble upper;
    gdouble page_size;
    gdouble usable;
    gint widget_height;

    upper = gtk_adjustment_get_upper(data->adjustment);
    page_size = gtk_adjustment_get_page_size(data->adjustment);
    widget_height = gtk_widget_get_height(widget);
    usable = MAX(upper, 1.0);
    *height = CLAMP(page_size / usable * widget_height, 18.0, (gdouble)widget_height);
    *top = gtk_adjustment_get_value(data->adjustment) / usable * widget_height;
    *top = CLAMP(*top, 0.0, MAX(0.0, widget_height - *height));
}

static void
mt_overview_map_draw_viewport(GtkDrawingArea *area,
                              cairo_t *cr,
                              gint width,
                              gint height,
                              gpointer user_data)
{
    MtOverviewMapData *data;
    gdouble top;
    gdouble visible_height;

    data = user_data;
    if (height <= 0 || width <= 0 || data->adjustment == NULL)
    {
        return;
    }

    mt_overview_map_get_rect(data, GTK_WIDGET(area), &top, &visible_height);
    cairo_set_source_rgba(cr, 0.208, 0.518, 0.894, 0.22);
    cairo_rectangle(cr, 2.0, top, MAX(0, width - 4), visible_height);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.208, 0.518, 0.894, 0.88);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
}

static void
mt_overview_map_queue_draw(GtkAdjustment *adjustment, gpointer user_data)
{
    MtOverviewMapData *data;

    (void)adjustment;
    data = user_data;
    if (data->viewport != NULL)
    {
        gtk_widget_queue_draw(data->viewport);
    }
}

static void
mt_overview_map_set_value_from_y(MtOverviewMapData *data,
                                 GtkWidget *widget,
                                 gdouble y,
                                 gboolean preserve_drag_offset)
{
    gdouble upper;
    gdouble page_size;
    gdouble value;
    gint height;

    height = gtk_widget_get_height(widget);
    if (height <= 0)
    {
        return;
    }

    upper = MAX(gtk_adjustment_get_upper(data->adjustment), 1.0);
    page_size = gtk_adjustment_get_page_size(data->adjustment);
    if (preserve_drag_offset)
    {
        y -= data->drag_offset;
    }
    else
    {
        y -= page_size / upper * height / 2.0;
    }
    value = y / height * upper;
    value = CLAMP(value,
                  gtk_adjustment_get_lower(data->adjustment),
                  MAX(gtk_adjustment_get_lower(data->adjustment), upper - page_size));
    gtk_adjustment_set_value(data->adjustment, value);
}

static void
mt_overview_map_drag_begin(GtkGestureDrag *gesture,
                           gdouble start_x,
                           gdouble start_y,
                           gpointer user_data)
{
    MtOverviewMapData *data;
    gdouble top;
    gdouble visible_height;

    (void)start_x;
    data = user_data;
    mt_overview_map_get_rect(data, GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture))),
                             &top, &visible_height);
    data->drag_offset = start_y - top;
    if (data->drag_offset < 0.0 || data->drag_offset > visible_height)
    {
        data->drag_offset = visible_height / 2.0;
    }
    mt_overview_map_set_value_from_y(data,
                                     GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture))),
                                     start_y,
                                     TRUE);
}

static void
mt_overview_map_drag_update(GtkGestureDrag *gesture,
                            gdouble offset_x,
                            gdouble offset_y,
                            gpointer user_data)
{
    gdouble start_x;
    gdouble start_y;
    GtkWidget *widget;

    (void)offset_x;
    gtk_gesture_drag_get_start_point(gesture, &start_x, &start_y);
    (void)start_x;
    widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
    mt_overview_map_set_value_from_y(user_data, widget, start_y + offset_y, TRUE);
}

GtkWidget *
mt_overview_map_new(GtkSourceView *source_view)
{
    GtkWidget *map;
    GtkWidget *overlay;
    GtkWidget *viewport;
    GtkAdjustment *adjustment;
    GtkGesture *drag;
    MtOverviewMapData *data;

    g_return_val_if_fail(GTK_SOURCE_IS_VIEW(source_view), NULL);

    map = gtk_source_map_new();
    gtk_source_map_set_view(GTK_SOURCE_MAP(map), source_view);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(map), 5);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(map), 5);

    overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), map);
    viewport = gtk_drawing_area_new();
    gtk_widget_set_hexpand(viewport, TRUE);
    gtk_widget_set_vexpand(viewport, TRUE);
    gtk_widget_add_css_class(viewport, "vellum-overview-viewport");
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), viewport);

    data = g_new0(MtOverviewMapData, 1);
    data->viewport = viewport;
    adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(source_view));
    if (adjustment != NULL)
    {
        data->adjustment = g_object_ref(adjustment);
        g_signal_connect(adjustment, "value-changed",
                         G_CALLBACK(mt_overview_map_queue_draw), data);
        g_signal_connect(adjustment, "changed",
                         G_CALLBACK(mt_overview_map_queue_draw), data);
    }
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(viewport),
                                   mt_overview_map_draw_viewport,
                                   data,
                                   NULL);
    drag = gtk_gesture_drag_new();
    g_signal_connect(drag, "drag-begin", G_CALLBACK(mt_overview_map_drag_begin), data);
    g_signal_connect(drag, "drag-update", G_CALLBACK(mt_overview_map_drag_update), data);
    gtk_widget_add_controller(viewport, GTK_EVENT_CONTROLLER(drag));
    g_object_set_data_full(G_OBJECT(overlay), "vellum-overview-data", data,
                           mt_overview_map_data_free);

    return overlay;
}
