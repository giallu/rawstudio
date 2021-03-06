/*
 * * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>, 
 * * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <rawstudio.h>
#include <math.h>
#include "rs-preview-widget.h"
#include "application.h"
#include "gtk-interface.h"
#include "gtk-helper.h"
#include "config.h"
#include "conf_interface.h"
#include "rs-photo.h"
#include "rs-actions.h"
#include "rs-toolbox.h"
#include "rs-loupe.h"
#include "rs-navigator.h"
#include <gettext.h>

enum {
	ROI_GRID_NONE = 0,
	ROI_GRID_GOLDEN,
	ROI_GRID_THIRDS,
	ROI_GRID_GOLDEN_TRIANGLES1,
	ROI_GRID_GOLDEN_TRIANGLES2,
	ROI_GRID_HARMONIOUS_TRIANGLES1,
	ROI_GRID_HARMONIOUS_TRIANGLES2,
};

typedef enum {
	NORMAL           = 0x000F, /* 0000 0000 0000 1111 */
	WB_PICKER        = 0x0001, /* 0000 0000 0000 0001 */

	STRAIGHTEN       = 0x0030, /* 0000 0000 0011 0000 */
	STRAIGHTEN_START = 0x0020, /* 0000 0000 0010 0000 */
	STRAIGHTEN_MOVE  = 0x0010, /* 0000 0000 0001 0000 */

	CROP             = 0x3FC0, /* 0011 1111 1100 0000 */
	CROP_START       = 0x2000, /* 0010 0000 0000 0000 */
	CROP_IDLE        = 0x1000, /* 0001 0000 0000 0000 */
	CROP_MOVE_ALL    = 0x0080, /* 0000 0000 1000 0000 */
	CROP_MOVE_CORNER = 0x0040, /* 0000 0000 0100 0000 */
	CROP_MOVE_SIDE   = 0x0100, /* 0000 0001 0000 0000 */
	DRAW_ROI         = 0x11C0, /* 0001 0001 1100 0000 */

	MOVE             = 0x4000, /* 0100 0000 0000 0000 */
	SCROLL           = 0x8000, /* 8000 0000 0000 0000 */
} STATE;

/* In win32 windef32.h will define both near and NEAR */
#undef NEAR
#undef near

typedef enum {
	CROP_NEAR_INSIDE  = 0x10, /* 0001 0000 */ 
	CROP_NEAR_OUTSIDE = 0x20, /* 0010 0000 */
	CROP_NEAR_N       = 0x8,  /* 0000 1000 */
	CROP_NEAR_S       = 0x4,  /* 0000 0100 */
	CROP_NEAR_W       = 0x2,  /* 0000 0010 */
	CROP_NEAR_E       = 0x1,  /* 0000 0001 */
	CROP_NEAR_NW      = CROP_NEAR_N | CROP_NEAR_W,
	CROP_NEAR_NE      = CROP_NEAR_N | CROP_NEAR_E,
	CROP_NEAR_SE      = CROP_NEAR_S | CROP_NEAR_E,
	CROP_NEAR_SW      = CROP_NEAR_S | CROP_NEAR_W,
	CROP_NEAR_CORNER  = CROP_NEAR_N | CROP_NEAR_S | CROP_NEAR_W | CROP_NEAR_E,
	CROP_NEAR_NOTHING = CROP_NEAR_INSIDE | CROP_NEAR_OUTSIDE,
} CROP_NEAR;

#define NAVIGATOR_WIDTH (170)
#define NAVIGATOR_HEIGHT (128)

typedef struct {
	gint x;
	gint y;
} RS_COORD;


typedef enum {
	SPLIT_NONE,
	SPLIT_HORIZONTAL,
	SPLIT_VERTICAL,
} VIEW_SPLIT;

const static gint PADDING = 3;
const static gint SPLITTER_WIDTH = 4;
#define MAX_VIEWS 2 /* maximum 32! */
#define VIEW_IS_VALID(view) (((view)>=0) && ((view)<MAX_VIEWS))

static GdkCursor *cur_fleur = NULL;
static GdkCursor *cur_watch = NULL;
static GdkCursor *cur_normal = NULL;
static GdkCursor *cur_nw = NULL;
static GdkCursor *cur_ne = NULL;
static GdkCursor *cur_se = NULL;
static GdkCursor *cur_sw = NULL;
static GdkCursor *cur_n = NULL;
static GdkCursor *cur_e = NULL;
static GdkCursor *cur_s = NULL;
static GdkCursor *cur_w = NULL;
static GdkCursor *cur_busy = NULL;
static GdkCursor *cur_crop = NULL;
static GdkCursor *cur_rotate = NULL;
static GdkCursor *cur_color_picker = NULL;

struct _RSPreviewWidget
{
	GtkTable parent;
	STATE state;
    GtkAdjustment *vadjustment;
    GtkAdjustment *hadjustment;
    GtkWidget *vscrollbar;
    GtkWidget *hscrollbar;
	GtkDrawingArea *canvas;

	RSToolbox *toolbox;

	gboolean zoom_to_fit;
	gboolean exposure_mask;
	gboolean keep_quick_enabled;

	GdkRGBA bgcolor; /* Background color of widget */
	VIEW_SPLIT split;
	gint views;

	GtkWidget *tool;

	/* Crop */
	RS_RECT roi;
	guint roi_grid;
	CROP_NEAR crop_near;
	gfloat crop_aspect;
	GString *crop_text;
	GtkWidget *crop_size_label;
	RS_RECT crop_move;
	RS_COORD crop_start;
	gint crop_other_fixed;

	RS_COORD straighten_start;
	RS_COORD straighten_end;
	gfloat straighten_angle;
	RSFilter *filter_input;

	RSFilter *filter_lensfun[MAX_VIEWS];
	RSFilter *filter_rotate[MAX_VIEWS];
	RSFilter *filter_crop[MAX_VIEWS];
	RSFilter *filter_cache0[MAX_VIEWS];
	RSFilter *filter_resample[MAX_VIEWS];
	RSFilter *filter_cache1[MAX_VIEWS];
	RSFilter *filter_denoise[MAX_VIEWS];
	RSFilter *filter_cache2[MAX_VIEWS];
	RSFilter *filter_transform_input[MAX_VIEWS];
	RSFilter *filter_dcp[MAX_VIEWS];
	RSFilter *filter_transform_display[MAX_VIEWS];
	RSFilter *filter_mask[MAX_VIEWS];
	RSFilter *filter_cache3[MAX_VIEWS];
	RSFilter *filter_end[MAX_VIEWS]; /* For convenience */

	RSFilterRequest *request[MAX_VIEWS];
	GdkRectangle *last_roi[MAX_VIEWS];
	RS_PHOTO *photo;
	RS_PHOTO *photo_blank_stored;
	void *transform;
	gint snapshot[MAX_VIEWS];

	GtkWidget *lightsout_window;

	gint last_x;
	gint last_y;
	gboolean prev_inside_image; /* For motion and leave function*/

	gboolean loupe_enabled;
	RSLoupe *loupe;
	RSFilter *loupe_filter_cache;
	RSFilter *loupe_transform_input;
	RSFilter *loupe_filter_dcp;
	RSFilter *loupe_filter_denoise;
	RSFilter *loupe_transform_display;
	RSFilter *loupe_filter_start;
	RSFilter *loupe_filter_end;
	gint loupe_view;

	RSFilter *navigator_filter_scale;
	RSFilter *navigator_transform_input;
	RSFilter *navigator_filter_rotate;
	RSFilter *navigator_filter_crop;
	RSFilter *navigator_filter_cache;
	RSFilter *navigator_filter_cache2;
	RSFilter *navigator_filter_cache3;
	RSFilter *navigator_filter_scale2;
	RSFilter *navigator_filter_dcp;
	RSFilter *navigator_transform_display;
	RSFilter *navigator_filter_end;
	GtkWidget *navigator;
	RSNavigator *rs_navigator;

	RSColorSpace *display_color_space;
	RSColorSpace *exposure_color_space;
	GdkDisplay *display;
	guint status_num;
};

/* Define the boiler plate stuff using the predefined macro */
G_DEFINE_TYPE (RSPreviewWidget, rs_preview_widget, GTK_TYPE_TABLE);

enum {
	WB_PICKED,
	MOTION_SIGNAL,
	LEAVE_SIGNAL,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };
static void get_max_size(RSPreviewWidget *preview, gint *width, gint *height);
static gboolean get_placement(RSPreviewWidget *preview, const guint view, GdkRectangle *placement);
static void realize(GtkWidget *widget, gpointer data);
static gboolean scroll (GtkWidget *widget, GdkEventScroll *event, gpointer user_data);
static void size_allocate (GtkWidget *widget, GtkAllocation *allocation, gpointer user_data);
static gboolean scrollbar_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static gboolean scrollbar_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void adjustment_changed(GtkAdjustment *adjustment, gpointer user_data);
static gboolean button(GtkWidget *widget, GdkEventButton *event, RSPreviewWidget *preview);
static gboolean motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data);
static gboolean leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data);
static void profile_changed(RS_PHOTO *photo, gpointer profile, RSPreviewWidget *preview);
static void filter_changed(RSFilter *filter, RSFilterChangedMask mask, RSPreviewWidget *preview);
static void lens_changed(RS_PHOTO *photo, RSPreviewWidget *preview);
static gboolean get_image_coord(RSPreviewWidget *preview, gint view, const gint x, const gint y, gint *scaled_x, gint *scaled_y, gint *real_x, gint *real_y, gint *max_w, gint *max_h);
static gint get_view_from_coord(RSPreviewWidget *preview, const gint x, const gint y);
static void crop_aspect_changed(gpointer active, gpointer user_data);
static void crop_grid_changed(gpointer active, gpointer user_data);
static void crop_apply_clicked(GtkButton *button, gpointer user_data);
static void crop_cancel_clicked(GtkButton *button, gpointer user_data);
static void crop_end(RSPreviewWidget *preview, gboolean accept);
static void crop_find_size_from_aspect(RS_RECT *roi, gdouble aspect, CROP_NEAR state);
static CROP_NEAR crop_near(RS_RECT *roi, gint x, gint y);
static gboolean make_cbdata(RSPreviewWidget *preview, const gint view, RS_PREVIEW_CALLBACK_DATA *cbdata, gint screen_x, gint screen_y, gint real_x, gint real_y);
static void canvas_draw(RSPreviewWidget *preview, GdkRectangle *rect, gboolean now);
static void canvas_draw_handler(GtkWidget *widget, cairo_t *cr, RSPreviewWidget *preview);
static void photo_spatial_changed(RS_PHOTO *photo, RSPreviewWidget *preview);
/**
 * Class initializer
 */
static void
rs_preview_widget_class_init(RSPreviewWidgetClass *klass)
{
	signals[WB_PICKED] = g_signal_new ("wb-picked",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL, 
		NULL,                
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[MOTION_SIGNAL] = g_signal_new ("motion",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
	signals[LEAVE_SIGNAL] = g_signal_new ("leave",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0, /* Is this right? */
		NULL,
		NULL,
		g_cclosure_marshal_VOID__POINTER,
		G_TYPE_NONE, 1, G_TYPE_POINTER);
}

/**
 * Instance initialization
 */
static void
rs_preview_widget_init(RSPreviewWidget *preview)
{
	gint i;
	GtkTable *table = GTK_TABLE(preview);
	preview->display = gtk_widget_get_display(GTK_WIDGET(preview));

	/* Initialize cursors */
	if (!cur_fleur) cur_fleur = gdk_cursor_new(GDK_FLEUR);
	if (!cur_watch) cur_watch = gdk_cursor_new(GDK_WATCH);
	if (!cur_nw) cur_nw = gdk_cursor_new(GDK_TOP_LEFT_CORNER);
	if (!cur_ne) cur_ne = gdk_cursor_new(GDK_TOP_RIGHT_CORNER);
	if (!cur_se) cur_se = gdk_cursor_new(GDK_BOTTOM_RIGHT_CORNER);
	if (!cur_sw) cur_sw = gdk_cursor_new(GDK_BOTTOM_LEFT_CORNER);
	if (!cur_n) cur_n = gdk_cursor_new(GDK_TOP_SIDE);
	if (!cur_e) cur_e = gdk_cursor_new(GDK_RIGHT_SIDE);
	if (!cur_s) cur_s = gdk_cursor_new(GDK_BOTTOM_SIDE);
	if (!cur_w) cur_w = gdk_cursor_new(GDK_LEFT_SIDE);
	if (!cur_busy) cur_busy = gdk_cursor_new(GDK_WATCH);
	if (!cur_crop) cur_crop = rs_cursor_new (preview->display, RS_CURSOR_CROP);
	if (!cur_rotate) cur_rotate = rs_cursor_new (preview->display, RS_CURSOR_ROTATE);
	if (!cur_color_picker) cur_color_picker = rs_cursor_new (preview->display, RS_CURSOR_COLOR_PICKER);

	gtk_table_set_homogeneous(table, FALSE);
	gtk_table_resize (table, 2, 2);

	/* Initialize */
	preview->canvas = GTK_DRAWING_AREA(gtk_drawing_area_new());
	g_signal_connect_after (G_OBJECT (preview->canvas), "button_press_event",
		G_CALLBACK (button), preview);
	g_signal_connect_after (G_OBJECT (preview->canvas), "button_release_event",
		G_CALLBACK (button), preview);
	g_signal_connect (G_OBJECT (preview->canvas), "motion_notify_event",
		G_CALLBACK (motion), preview);
	g_signal_connect (G_OBJECT (preview->canvas), "leave_notify_event",
		G_CALLBACK (leave), preview);

	/* Let us know about pointer movements */
	gtk_widget_set_events(GTK_WIDGET(preview->canvas), 0
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_LEAVE_NOTIFY_MASK);

	preview->state = WB_PICKER;
	preview->split = SPLIT_VERTICAL;
	preview->views = 1;
	preview->zoom_to_fit = TRUE;
	preview->exposure_mask = FALSE;
	preview->crop_near = CROP_NEAR_NOTHING;
	preview->keep_quick_enabled = FALSE;

	gchar* name;
	preview->display_color_space = NULL;
	if ((name = rs_conf_get_string("display-colorspace")))
	{
		if (0 != g_strcmp0(name, "_builtin_display"))
			preview->display_color_space = rs_color_space_new_singleton(name);
	}
	if (!preview->display_color_space)
		preview->display_color_space = rs_get_display_profile(GTK_WIDGET(preview));

	name = NULL;
	name = rs_conf_get_string("exposure-mask-colorspace");
	if (name)
		preview->exposure_color_space = rs_color_space_new_singleton(name);
	else
		preview->exposure_color_space = rs_color_space_new_singleton("RSSrgb");

	preview->vadjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 10.0, 10.0));
	preview->hadjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 100.0, 1.0, 10.0, 10.0));
	g_signal_connect(G_OBJECT(preview->vadjustment), "value-changed", G_CALLBACK(adjustment_changed), preview);
	g_signal_connect(G_OBJECT(preview->hadjustment), "value-changed", G_CALLBACK(adjustment_changed), preview);
	preview->vscrollbar = gtk_vscrollbar_new(preview->vadjustment);
	preview->hscrollbar = gtk_hscrollbar_new(preview->hadjustment);

	gtk_widget_set_events(GTK_WIDGET(preview->vscrollbar), GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
	gtk_widget_set_events(GTK_WIDGET(preview->hscrollbar), GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(preview->vscrollbar, "button-press-event", G_CALLBACK(scrollbar_press), preview);
	g_signal_connect(preview->hscrollbar, "button-press-event", G_CALLBACK(scrollbar_press), preview);
	g_signal_connect(preview->vscrollbar, "button-release-event", G_CALLBACK(scrollbar_release), preview);
	g_signal_connect(preview->hscrollbar, "button-release-event", G_CALLBACK(scrollbar_release), preview);

	gtk_table_attach(table, GTK_WIDGET(preview->canvas), 0, 1, 0, 1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 0, 0);
	gtk_table_attach(table, preview->vscrollbar, 1, 2, 0, 1, GTK_SHRINK, GTK_EXPAND|GTK_FILL, 0, 0);
    gtk_table_attach(table, preview->hscrollbar, 0, 1, 1, 2, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 0, 0);

	preview->filter_input = NULL;
	for(i=0;i<MAX_VIEWS;i++)
	{
		preview->filter_lensfun[i] = rs_filter_new("RSLensfun", NULL);
		preview->filter_rotate[i] = rs_filter_new("RSRotate", preview->filter_lensfun[i]);
		preview->filter_crop[i] = rs_filter_new("RSCrop", preview->filter_rotate[i]);
		preview->filter_cache0[i] = rs_filter_new("RSCache", preview->filter_crop[i]);
		preview->filter_resample[i] = rs_filter_new("RSResample", preview->filter_cache0[i]);
		/* Careful - "make_cbdata" grabs data from "filter_cache1" */
		preview->filter_cache1[i] = rs_filter_new("RSCache", preview->filter_resample[i]);
		preview->filter_transform_input[i] = rs_filter_new("RSColorspaceTransform", preview->filter_cache1[i]);
		preview->filter_dcp[i] = rs_filter_new("RSDcp", preview->filter_transform_input[i]);
		preview->filter_cache2[i] = rs_filter_new("RSCache", preview->filter_dcp[i]);
		preview->filter_denoise[i] = rs_filter_new("RSDenoise", preview->filter_cache2[i]);
		preview->filter_transform_display[i] = rs_filter_new("RSColorspaceTransform", preview->filter_denoise[i]);
		preview->filter_cache3[i] = rs_filter_new("RSCache", preview->filter_transform_display[i]);
		preview->filter_mask[i] = rs_filter_new("RSExposureMask", preview->filter_cache3[i]);
		preview->filter_end[i] = preview->filter_mask[i];
		g_signal_connect(preview->filter_end[i], "changed", G_CALLBACK(filter_changed), preview);

		rs_filter_set_recursive(preview->filter_end[i], "bounding-box", TRUE, NULL);
		g_object_set(preview->filter_cache3[i], "latency", 1, NULL);

		preview->request[i] = rs_filter_request_new();
		rs_filter_param_set_object(RS_FILTER_PARAM(preview->request[i]), "colorspace", preview->display_color_space);
#if MAX_VIEWS > 3
#error Fix line below
#endif
		preview->snapshot[i] = i;
		preview->last_roi[i] = NULL;
	}
#if MAX_VIEWS != 2
#error Fix lines below
#endif
	rs_filter_set_label(preview->filter_resample[0], "RSPreviewWidget-0");
	rs_filter_set_label(preview->filter_resample[1], "RSPreviewWidget-1");

	preview->loupe_transform_input = rs_filter_new("RSColorspaceTransform", NULL);
	preview->loupe_filter_dcp = rs_filter_new("RSDcp", preview->loupe_transform_input);
	preview->loupe_filter_cache = rs_filter_new("RSCache", preview->loupe_filter_dcp);
	preview->loupe_filter_denoise = rs_filter_new("RSDenoise", preview->loupe_filter_cache);
	preview->loupe_transform_display = rs_filter_new("RSColorspaceTransform", preview->loupe_filter_denoise);
	preview->loupe_filter_start = preview->loupe_transform_input;
	preview->loupe_filter_end = preview->loupe_transform_display;
	preview->loupe = rs_loupe_new();
	g_object_set(preview->loupe_filter_cache, "ignore-roi", TRUE, NULL);
	preview->photo = NULL;
	preview->loupe_view = -1;

	preview->navigator_filter_scale = rs_filter_new("RSResample", NULL);
	preview->navigator_filter_cache = rs_filter_new("RSCache", preview->navigator_filter_scale);
	preview->navigator_transform_input = rs_filter_new("RSColorspaceTransform", preview->navigator_filter_cache);
	preview->navigator_filter_rotate = rs_filter_new("RSRotate", preview->navigator_transform_input);
	preview->navigator_filter_crop = rs_filter_new("RSCrop", preview->navigator_filter_rotate);
	preview->navigator_filter_scale2 = rs_filter_new("RSResample", preview->navigator_filter_crop);
	preview->navigator_filter_cache2 = rs_filter_new("RSCache", preview->navigator_filter_scale2);
	preview->navigator_filter_dcp = rs_filter_new("RSDcp", preview->navigator_filter_cache2);
	preview->navigator_filter_cache3 = rs_filter_new("RSCache", preview->navigator_filter_dcp);
	preview->navigator_transform_display = rs_filter_new("RSColorspaceTransform", preview->navigator_filter_cache3);
	preview->navigator_filter_end = preview->navigator_transform_display;

	g_object_set(preview->navigator_filter_cache, "ignore-roi", TRUE, NULL);
	g_object_set(preview->navigator_filter_cache2, "ignore-roi", TRUE, NULL);
	g_object_set(preview->navigator_filter_cache3, "ignore-roi", TRUE, NULL);

	g_signal_connect(G_OBJECT(preview->canvas), "size-allocate", G_CALLBACK(size_allocate), preview);
	g_signal_connect(G_OBJECT(preview), "realize", G_CALLBACK(realize), NULL);
	g_signal_connect(G_OBJECT(preview->canvas), "scroll_event", G_CALLBACK (scroll), preview);
	g_signal_connect(preview->canvas, "draw", G_CALLBACK(canvas_draw_handler), preview);

	preview->lightsout_window = NULL;
	preview->prev_inside_image = FALSE;

	g_object_ref(preview->display_color_space);
}

/**
 * Creates a new RSPreviewWidget
 * @return A new RSPreviewWidget
 */
GtkWidget *
rs_preview_widget_new(GtkWidget *toolbox)
{
	GtkWidget *widget;
	RSPreviewWidget *preview;
	widget = g_object_new (RS_PREVIEW_TYPE_WIDGET, NULL);

	preview = RS_PREVIEW_WIDGET(widget);
	preview->toolbox = RS_TOOLBOX(toolbox);

	rs_toolbox_set_histogram_input(preview->toolbox, preview->navigator_filter_end, preview->exposure_color_space);
	return widget;
}

static void
photo_spatial_changed(RS_PHOTO *photo, RSPreviewWidget *preview)
{
	if (photo == preview->photo)
	{
		int i;
		/* Update crop and rotate filters */
		for(i=0;i<MAX_VIEWS;i++)
		{
			rs_filter_set_recursive(preview->filter_end[i],
				"rectangle", rs_photo_get_crop(photo),
				"angle", rs_photo_get_angle(photo),
				"orientation", photo->orientation,
				NULL);
		}
	}
}

void
rs_preview_widget_update_display_colorspace(RSPreviewWidget *preview)
{
	gint i;
	gchar *name;

	preview->display = gtk_widget_get_display(GTK_WIDGET(preview));
	RSColorSpace *new_cs = rs_get_display_profile(GTK_WIDGET(preview));
	RSColorSpace *exp_cs = rs_color_space_new_singleton("RSSrgb");

	name = rs_conf_get_string("exposure-mask-colorspace");
	if (name)
		exp_cs = rs_color_space_new_singleton(name);

	/* If exposure mask, use the cs for that */
	if (preview->exposure_mask)
		new_cs = g_object_ref(exp_cs);
	else if (!preview->exposure_mask && (name = rs_conf_get_string("display-colorspace")))
	{
		if (0 == g_strcmp0(name, "_builtin_display"))
			new_cs = rs_get_display_profile(GTK_WIDGET(preview));
		else
			new_cs = rs_color_space_new_singleton(name);
	}

	if (preview->zoom_to_fit && preview->navigator)
		rs_navigator_set_colorspace(RS_NAVIGATOR(preview->navigator), new_cs);

	if (preview->display_color_space == new_cs && preview->exposure_color_space == exp_cs)
		return;

	preview->display_color_space = new_cs;
	preview->exposure_color_space = exp_cs;

	rs_toolbox_set_histogram_input(preview->toolbox, preview->navigator_filter_end, preview->exposure_color_space);
	rs_loupe_set_colorspace(preview->loupe, preview->display_color_space);
	for(i=0;i<MAX_VIEWS;i++)
		rs_filter_param_set_object(RS_FILTER_PARAM(preview->request[i]), "colorspace", preview->display_color_space);
	canvas_draw(preview, NULL, FALSE);
}

static void
rs_preview_widget_set_scrollbars(RSPreviewWidget *preview, int width, int height, int real_x, int real_y, gboolean force_pos)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	gdouble v_val, h_val, v_val_new, h_val_new;

	g_object_get(G_OBJECT(preview->hadjustment), "upper", &h_val, NULL);
	g_object_get(G_OBJECT(preview->vadjustment), "upper", &v_val, NULL);

	/* Update scrollbars to reflect the change */
	h_val_new = (gdouble) width;
	g_object_set(G_OBJECT(preview->hadjustment), "upper", h_val_new, NULL);
	v_val_new = (gdouble) height;
	g_object_set(G_OBJECT(preview->vadjustment), "upper", v_val_new, NULL);

	/* Modify adjusters, if size changed */
	if (force_pos || fabs(v_val_new-v_val) > 1.0 || fabs(h_val_new-h_val) > 1.0)
	{
		const gdouble hpage = gtk_adjustment_get_page_size(preview->hadjustment);
		const gdouble vpage = gtk_adjustment_get_page_size(preview->vadjustment);
		gdouble hvalue = MIN((double)width-hpage+10,((gdouble) real_x) - hpage/2.0);
		gdouble vvalue = MIN((double)height-vpage+10,((gdouble) real_y) - vpage/2.0);

		g_object_set(preview->hadjustment, "value", hvalue, NULL);
		g_object_set(preview->vadjustment, "value", vvalue, NULL);
	} 

	if (preview->navigator)
	{
			/* Build navigator */
		rs_navigator_set_adjustments(preview->rs_navigator, preview->vadjustment, preview->hadjustment);
		rs_navigator_set_source_filter(preview->rs_navigator, preview->navigator_filter_end);
	}
	rs_filter_set_recursive(preview->navigator_filter_end,
			"orientation", preview->photo->orientation,
			"rectangle", rs_photo_get_crop(preview->photo),
			"angle", rs_photo_get_angle(preview->photo),
			"settings", preview->photo->settings[preview->snapshot[0]],
			NULL);
}

/**
 * Select zoom-to-fit of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param zoom_to_fit Set to TRUE to enable zoom-to-fit.
 */
void
rs_preview_widget_set_zoom_to_fit(RSPreviewWidget *preview, gboolean zoom_to_fit)
{
	gint width;
	gint height;
	gint view;
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (zoom_to_fit == preview->zoom_to_fit)
		return;

	rs_preview_widget_quick_start(preview, TRUE);
	if (zoom_to_fit)
	{
		gint max_width, max_height;
		get_max_size(preview, &max_width, &max_height);
		rs_filter_set_enabled(preview->filter_resample[0], TRUE);
		for(view=0;view<preview->views;view++)
		{
			rs_filter_set_recursive(preview->filter_end[view],
				"width", max_width,
				"height", max_height,
				NULL);
		}

		/* FIXME: Update scale somehow! */
		gtk_widget_hide(preview->vscrollbar);
		gtk_widget_hide(preview->hscrollbar);

		if (preview->navigator)
		{
			gtk_widget_destroy(preview->navigator);
			preview->navigator = NULL;
		}
	}
	else
	{
		GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(rawstudio_window));
		gint real_x, real_y;
		const gint view = get_view_from_coord(preview, preview->last_x, preview->last_y);
		const gboolean inside_image = get_image_coord(preview, view, preview->last_x, preview->last_y, NULL, NULL, &real_x, &real_y, NULL, NULL);

		/* Unsplit if needed */
		if (preview->views > 1)
			rs_core_action_group_activate("Split");

		gdk_window_set_cursor(window, cur_busy);
		GUI_CATCHUP_DISPLAY(preview->display);

		/* Disable resample filter */
		rs_filter_set_enabled(preview->filter_resample[0], FALSE);

		gdk_window_set_cursor(window, NULL);

		gtk_widget_show(preview->vscrollbar);
		gtk_widget_show(preview->hscrollbar);

		gdk_window_set_cursor(window, NULL);

		preview->rs_navigator = rs_navigator_new();
		gtk_widget_set_size_request(GTK_WIDGET(preview->rs_navigator), NAVIGATOR_WIDTH, NAVIGATOR_HEIGHT);

		preview->navigator = rs_toolbox_add_widget(preview->toolbox, GTK_WIDGET(preview->rs_navigator), _("Display Navigation"));
		rs_navigator_set_preview_widget(preview->rs_navigator, preview);
		rs_navigator_set_colorspace(preview->rs_navigator, preview->display_color_space);
		gtk_widget_show_all(GTK_WIDGET(preview->navigator));
		if (preview->photo)
		{
			rs_filter_get_size_simple(preview->filter_end[0], preview->request[0], &width, &height);
			if (!inside_image)
			{
				real_x = 0.5 * width;
				real_y = 0.5 * height;
			}
			rs_preview_widget_set_scrollbars(preview, width, height, real_x, real_y, inside_image);
		}
	}

	preview->zoom_to_fit = zoom_to_fit;
	GtkToggleAction *fit_action = GTK_TOGGLE_ACTION(rs_core_action_group_get_action("ZommToFit"));
	gtk_toggle_action_set_active(fit_action, zoom_to_fit);
	rs_filter_set_recursive(RS_FILTER(preview->filter_input), "demosaic-allow-downscale",  preview->zoom_to_fit, NULL);
	GUI_CATCHUP_DISPLAY(preview->display);
	rs_preview_widget_quick_end(preview);
}

/**
 * Enable the loupe
 */
void
rs_preview_widget_set_loupe_enabled(RSPreviewWidget *preview, int view, gboolean enabled)
{
	if (preview->loupe_enabled != enabled)
	{
		preview->loupe_enabled = enabled;

		if (!preview->zoom_to_fit)
			preview->loupe_enabled = FALSE;

		if (preview->loupe_enabled)
		{
			rs_loupe_set_filter(preview->loupe, preview->loupe_filter_end);

			if (view != preview->loupe_view)
			{
				rs_filter_set_previous(preview->loupe_filter_start, preview->filter_cache0[view]);
				preview->loupe_view = view;
			}
			if (rs_photo_get_dcp_profile(preview->photo))
				g_object_set(preview->loupe_filter_dcp, "profile", rs_photo_get_dcp_profile(preview->photo), NULL);
			else
				g_object_set(preview->loupe_filter_dcp, "use-profile", FALSE, NULL);
			rs_filter_set_recursive(preview->loupe_filter_end, "settings", preview->photo->settings[preview->snapshot[view]], NULL);
			rs_loupe_set_colorspace(preview->loupe, preview->display_color_space);

			gtk_widget_show_all(GTK_WIDGET(preview->loupe));
		}
		else
		{
			if (preview->loupe)
				gtk_widget_hide(GTK_WIDGET(preview->loupe));
		}
	}
}

/**
 * Sets active photo of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param photo A RS_PHOTO
 */
void
rs_preview_widget_set_photo(RSPreviewWidget *preview, RS_PHOTO *photo)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	preview->photo = photo;
	if (preview->state & CROP)
		crop_end(preview, FALSE);
	if (preview->state & STRAIGHTEN)
	{
		preview->state = WB_PICKER;
		gui_status_pop(preview->status_num);
	}

	if (preview->photo)
	{
		rs_preview_widget_set_photo_settings(preview);
		photo->thumbnail_filter = preview->navigator_filter_end;
		g_signal_connect(G_OBJECT(preview->photo), "lens-changed", G_CALLBACK(lens_changed), preview);
		g_signal_connect(G_OBJECT(preview->photo), "profile-changed", G_CALLBACK(profile_changed), preview);
		g_signal_connect(G_OBJECT(preview->photo), "spatial-changed", G_CALLBACK(photo_spatial_changed), preview);
	}
}

/**
 * Sets settings of active photo of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_set_photo_settings(RSPreviewWidget *preview)
{
	gint view;
	GList *filters = NULL;

	/* Apply snapshot 0 to histogram & curve */
	filters = g_list_append(NULL, preview->loupe_filter_end);
	filters = g_list_append(filters, preview->navigator_filter_end);
	rs_photo_apply_to_filters(preview->photo, filters, preview->snapshot[0]);
	g_list_free(filters);

	for(view=0;view<MAX_VIEWS;view++) 
		g_signal_handlers_block_by_func(preview->filter_end[view], G_CALLBACK(filter_changed), preview);

	for(view=0;view<MAX_VIEWS;view++) 
	{
		rs_filter_request_set_quick(preview->request[view], TRUE);
		filters = g_list_append(NULL, preview->filter_end[view]);
		rs_photo_apply_to_filters(preview->photo, filters, preview->snapshot[view]);
		g_list_free(filters);
	}

	/* Set toolbox while display updates are locked */
	rs_toolbox_set_photo(RS_TOOLBOX(preview->toolbox), preview->photo);

	for(view=0;view<MAX_VIEWS;view++) 
		g_signal_handlers_unblock_by_func(preview->filter_end[view], G_CALLBACK(filter_changed), preview);


	g_object_set(preview->navigator_filter_scale,
		"bounding-box", TRUE,
		"width", NAVIGATOR_WIDTH*2,
		"height", NAVIGATOR_HEIGHT*2,
		NULL);

	g_object_set(preview->navigator_filter_scale2,
		"bounding-box", TRUE,
		"width", NAVIGATOR_WIDTH,
		"height", NAVIGATOR_HEIGHT,
		"never-quick", TRUE,
		NULL);

	if (preview->photo)
	{
		/* Update scrollbars */
		if (!preview->zoom_to_fit)
		{
			gint width, height;
			rs_filter_get_size_simple(preview->filter_end[0], preview->request[0], &width, &height);
			rs_preview_widget_set_scrollbars(preview, width, height, 0.5*width, 0.5*height, FALSE);
		}
	}
}

/**
 * Set input filter for a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param filter A filter to listen for
 */
void
rs_preview_widget_set_filter(RSPreviewWidget *preview, RSFilter *filter, RSFilter *fast_filter)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	g_assert(RS_IS_FILTER(filter));

	preview->filter_input = filter;
	rs_filter_set_recursive(RS_FILTER(preview->filter_input), "demosaic-allow-downscale",  preview->zoom_to_fit, NULL);
	rs_filter_set_previous(preview->filter_lensfun[0], preview->filter_input);
	rs_filter_set_previous(preview->filter_lensfun[1], preview->filter_input);
	if (fast_filter)
	{
		g_assert(RS_IS_FILTER(fast_filter));
		rs_filter_set_previous(preview->navigator_filter_scale, fast_filter);
	} else
		rs_filter_set_previous(preview->navigator_filter_scale, filter);
}

/**
 * Sets the CMS profile used in preview
 * @param preview A RSPreviewWidget
 * @param profile The profile to use
 */
void
rs_preview_widget_set_profile(RSPreviewWidget *preview, RSIccProfile *profile)
{
	gint view;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	g_assert(RS_IS_ICC_PROFILE(profile));

	for(view=0;view<MAX_VIEWS;view++)
		rs_filter_set_recursive(preview->filter_end[view], "icc-profile", profile, NULL);

	rs_filter_set_recursive(preview->loupe_filter_end, "icc-profile", profile, NULL);
	rs_filter_set_recursive(preview->navigator_filter_end, "icc-profile", profile, NULL);

	/* FIXME: Implement this properly */
	/* 1. Make this assept RSColorSpace */
	/* 2. Remove the standard sRGB ICC profile (and others!) */
	/* 3. Use RSSrgb instead */
	/* 4. Assign the value to preview->display_color_space */
//	if (preview->display_color_space)
//		g_object_unref(preview->display_color_space);
//	preview->display_color_space = rs_color_space_icc_new_from_icc(profile);
// 	rs_toolbox_set_histogram_input(preview->toolbox, preview->navigator_filter_end, preview->display_color_space);
}

/**
 * Sets the background color of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param color The new background color
 */
void
rs_preview_widget_set_bgcolor(RSPreviewWidget *preview, GdkColor *color)
{
	g_return_if_fail (RS_IS_PREVIEW_WIDGET(preview));
	g_return_if_fail (color != NULL);

	preview->bgcolor.red = color->red / 65535.0;
	preview->bgcolor.green = color->green / 65535.0;
	preview->bgcolor.blue = color->blue / 65535.0;
	preview->bgcolor.alpha = 1.0;

	gtk_widget_override_background_color(GTK_WIDGET(preview->canvas), GTK_STATE_NORMAL, &preview->bgcolor);

	if (gtk_widget_get_realized(GTK_WIDGET(preview->canvas)))
		canvas_draw(preview, NULL, FALSE);
}

/**
 * Enables or disables split-view
 * @param preview A RSPreviewWidget
 * @param split_screen Enables split-view if TRUE, disables if FALSE
 */
void
rs_preview_widget_set_split(RSPreviewWidget *preview, gboolean split_screen)
{
	gint view, max_width, max_height;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (split_screen)
	{
		preview->split = SPLIT_VERTICAL;
		preview->views = 2;
	}
	else
	{
		preview->split = SPLIT_NONE;
		preview->views = 1;
	}

	get_max_size(preview, &max_width, &max_height);

	for(view=0;view<preview->views;view++)
		rs_filter_set_recursive(preview->filter_end[view], "width", max_width, "height", max_height, NULL); 

	rs_preview_widget_set_zoom_to_fit(preview, TRUE);
	canvas_draw(preview, NULL, FALSE);
}

static gboolean
lightsout_window_on_draw(GtkWidget *widget, cairo_t *cairo_context, RSPreviewWidget *preview)
{
	gint view;
	gint x, y;
	gint origin_x, origin_y;
	gint root_origin_x, root_origin_y;
	gint width, height;
	GdkWindow *window = gtk_widget_get_window(widget);

	gtk_window_get_size(GTK_WINDOW(widget), &width, &height);

	cairo_set_source_rgba (cairo_context, 0.0f, 0.0f, 0.0f, 0.8f);
	cairo_set_operator (cairo_context, CAIRO_OPERATOR_SOURCE);
	cairo_paint (cairo_context);

	/* Make sure the window is fullscreen and above everything */
	gdk_window_raise(window);
	gdk_window_set_keep_above(window, TRUE);
	gdk_window_fullscreen(window);

	/* Get position of canvas widget */
	gdk_window_get_origin(gtk_widget_get_window(GTK_WIDGET(preview->canvas)), &origin_x, &origin_y);

	/* This is nothing but a hack. Since the "lightsout" window is maximized,
	   we can use the position of this to measure the size of Gnome3 left and
	   top panels */
	gdk_window_get_origin(window, &root_origin_x, &root_origin_y);

	/* Paint the images with alpha=0 */
	for(view=0;view<preview->views;view++)
	{
		GdkRectangle rect;
		if (get_placement(preview, view, &rect))
		{
			/* Calculate the position as canvas position - PANEL sizes + canvas
			   placement of preview */
			x = origin_x - root_origin_x + rect.x;
			y = origin_y - root_origin_y + rect.y;

			cairo_set_source_rgba(cairo_context, 0.0, 0.0, 0.0, 0.0);
			cairo_rectangle (cairo_context, x, y, rect.width, rect.height);
			cairo_fill (cairo_context);
		}
	}

	/* Set opacity to 100% when we're done drawing */
	gtk_window_set_opacity(GTK_WINDOW(widget), 1.0);

	return FALSE;
}

/**
 * Enables or disables lights out mode
 * @param preview A RSPreviewWidget
 * @param lightsout Enables lights out mode if TRUE, disables if FALSE
 */
void
rs_preview_widget_set_lightsout(RSPreviewWidget *preview, gboolean lightsout)
{
	/* FIXME: Make this follow the loaded image(s) somehow */
	if (lightsout && !preview->lightsout_window)
	{
		GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(preview->canvas));
		gint width = gdk_screen_get_width(screen);
		gint height = gdk_screen_get_height(screen);
		GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
		GdkVisual *visual = gdk_screen_get_rgba_visual(screen);

		/* Check if the system even supports composite - and bail out if needed */
		if (!visual || !gdk_display_supports_composite(gdk_display_get_default()))
		{
			GtkWidget *dialog = gui_dialog_make_from_text(
				GTK_STOCK_DIALOG_ERROR,
				_("Light out mode not available"),
				_("Your setup doesn't seem to support RGBA visuals and/or compositing. Consult your operating system manual for enabling RGBA visuals and compositing.")
			);
			
			GtkWidget *button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
			gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_ACCEPT);

            gtk_widget_show_all(dialog);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
			return;
		}

		/* Set the visual to RGBA */
		gtk_widget_set_visual(window, visual);

		/* Cover whole screen */
		gtk_window_resize(GTK_WINDOW(window), width, height);
		gtk_window_move(GTK_WINDOW(window), 0, 0);

		/* Set the input shape to an empty cairo region,
		 * to let everything pass through */
		cairo_region_t *region = cairo_region_create();
		gtk_widget_input_shape_combine_region(window, region);
		cairo_region_destroy(region);

		g_signal_connect(window, "draw", G_CALLBACK(lightsout_window_on_draw), preview);

		gtk_widget_set_app_paintable (window, TRUE);
		gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_UTILITY);
		gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
		gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
		gtk_window_set_accept_focus(GTK_WINDOW(window), FALSE);
		gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
		gtk_window_set_skip_pager_hint(GTK_WINDOW(window), TRUE);
		gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
		gtk_window_set_title(GTK_WINDOW(window), "Rawstudio lights out helper");

		/* Let the window be completely transparent for now to avoid initial flicker */
		gtk_window_set_opacity(GTK_WINDOW(window), 0.0);
		gtk_widget_show_all(window);
		preview->lightsout_window = window;
	}
	else if (!lightsout && preview->lightsout_window)
	{
		gtk_widget_destroy(preview->lightsout_window);
		preview->lightsout_window = NULL;
	}
}

/**
 * Sets the active snapshot of a RSPreviewWidget
 * @param preview A RSPreviewWidget
 * @param view Which view to set (0..1)
 * @param snapshot Which snapshot to view (0..2)
 */
void
rs_preview_widget_set_snapshot(RSPreviewWidget *preview, const guint view, const gint snapshot)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));
	g_assert(VIEW_IS_VALID(view));

	if (preview->snapshot[view] == snapshot)
		return;

	preview->snapshot[view] = snapshot;

	if (!preview->photo)
		return;

	rs_filter_set_recursive(preview->filter_end[view], "settings", preview->photo->settings[preview->snapshot[view]], NULL);

	canvas_draw(preview, NULL, FALSE);
}

/**
 * Enables or disables the exposure mask
 * @param preview A RSPreviewWidget
 * @param show_exposure_mask Set to TRUE to enabled
 */
void
rs_preview_widget_set_show_exposure_mask(RSPreviewWidget *preview, gboolean show_exposure_mask)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (preview->exposure_mask != show_exposure_mask)
	{
		gint view;
		preview->exposure_mask = show_exposure_mask;

		for(view=0;view<preview->views;view++)
			rs_filter_set_recursive(preview->filter_end[view], "exposure-mask", preview->exposure_mask, NULL);

		canvas_draw(preview, NULL, FALSE);
	}
}


/**
 * Gets the status of whether the exposure mask is displayed
 * @param preview A RSPreviewWidget
 * @return TRUE is exposure mask is displayed, FALSE otherwise
 */
gboolean
rs_preview_widget_get_show_exposure_mask(RSPreviewWidget *preview, gboolean show_exposure_mask)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	return preview->exposure_mask;
}

/**
 * Puts a RSPreviewWidget in crop-mode
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_crop_start(RSPreviewWidget *preview)
{
	GtkWidget *vbox;
	GtkWidget *roi_size_hbox;
	GtkWidget *label;
	GtkWidget *roi_grid_hbox;
	GtkWidget *roi_grid_label;
	GtkWidget *roi_grid_combobox;
	GtkWidget *aspect_hbox;
	GtkWidget *aspect_label;
	GtkWidget *button_box;
	GtkWidget *apply_button;
	GtkWidget *cancel_button;
	RS_CONFBOX *grid_confbox;
	RS_CONFBOX *aspect_confbox;

	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (!(preview->state & NORMAL))
		return;

	/* predefined aspects */
	/* aspect MUST be => 1.0 */
	const static gdouble aspect_freeform = 0.0f;
	const static gdouble aspect_32 = 3.0f/2.0f;
	const static gdouble aspect_43 = 4.0f/3.0f;
	const static gdouble aspect_1008 = 10.0f/8.0f;
	const static gdouble aspect_1610 = 16.0f/10.0f;
	const static gdouble aspect_169 = 16.0f/9.0f;
	const static gdouble aspect_83 = 8.0f/3.0f;
	const static gdouble aspect_11 = 1.0f;
	static gdouble aspect_org ;
	aspect_org = (gdouble)preview->photo->input->w / preview->photo->input->h;

	if (aspect_org < 1.0 && aspect_org != 0.0)
		aspect_org = 1.0 / aspect_org;

	static gdouble aspect_iso216;
	static gdouble aspect_golden;
	aspect_iso216 = sqrt(2.0f);
	aspect_golden = (1.0f+sqrt(5.0f))/2.0f;

	vbox = gtk_vbox_new(FALSE, 4);

	label = gtk_label_new(_("Size"));
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

	roi_size_hbox = gtk_hbox_new(FALSE, 0);

	/* Default aspect (freeform) */
	preview->crop_aspect = 0.0f;

	preview->crop_size_label = gtk_label_new(_("-"));
	gtk_box_pack_start (GTK_BOX (roi_size_hbox), label, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (roi_size_hbox), preview->crop_size_label, FALSE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (vbox), roi_size_hbox, FALSE, TRUE, 0);

	roi_grid_hbox = gtk_hbox_new(FALSE, 0);
	roi_grid_label = gtk_label_new(_("Grid"));
	gtk_misc_set_alignment(GTK_MISC(roi_grid_label), 0.0, 0.5);

	grid_confbox = gui_confbox_new(CONF_ROI_GRID);
	gui_confbox_set_callback(grid_confbox, preview, crop_grid_changed);
	gui_confbox_add_entry(grid_confbox, "none", _("None"), (gpointer) ROI_GRID_NONE);
	gui_confbox_add_entry(grid_confbox, "goldensections", _("Golden sections"), (gpointer) ROI_GRID_GOLDEN);
	gui_confbox_add_entry(grid_confbox, "ruleofthirds", _("Rule of thirds"), (gpointer) ROI_GRID_THIRDS);
	gui_confbox_add_entry(grid_confbox, "goldentriangles1", _("Golden triangles #1"), (gpointer) ROI_GRID_GOLDEN_TRIANGLES1);
	gui_confbox_add_entry(grid_confbox, "goldentriangles2", _("Golden triangles #2"), (gpointer) ROI_GRID_GOLDEN_TRIANGLES2);
	gui_confbox_add_entry(grid_confbox, "harmonioustriangles1", _("Harmonious triangles #1"), (gpointer) ROI_GRID_HARMONIOUS_TRIANGLES1);
	gui_confbox_add_entry(grid_confbox, "harmonioustriangles2", _("Harmonious triangles #2"), (gpointer) ROI_GRID_HARMONIOUS_TRIANGLES2);
	gui_confbox_load_conf(grid_confbox, "none");

	roi_grid_combobox = gui_confbox_get_widget(grid_confbox);

	gtk_box_pack_start (GTK_BOX (roi_grid_hbox), roi_grid_label, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (roi_grid_hbox), roi_grid_combobox, FALSE, TRUE, 4);

	aspect_hbox = gtk_hbox_new(FALSE, 0);
	aspect_label = gtk_label_new(_("Aspect"));
	gtk_misc_set_alignment(GTK_MISC(aspect_label), 0.0, 0.5);

	aspect_confbox = gui_confbox_new(CONF_CROP_ASPECT);
	gui_confbox_set_callback(aspect_confbox, preview, crop_aspect_changed);
	gui_confbox_add_entry(aspect_confbox, "freeform", _("Freeform"), (gpointer) &aspect_freeform);
	gui_confbox_add_entry(aspect_confbox, "original", _("Original Aspect"), (gpointer) &aspect_org);
	gui_confbox_add_entry(aspect_confbox, "iso216", _("ISO paper (A4)"), (gpointer) &aspect_iso216);
	gui_confbox_add_entry(aspect_confbox, "3:2", _("3:2 (35mm)"), (gpointer) &aspect_32);
	gui_confbox_add_entry(aspect_confbox, "4:3", _("4:3"), (gpointer) &aspect_43);
	gui_confbox_add_entry(aspect_confbox, "10:8", _("10:8 (SXGA)"), (gpointer) &aspect_1008);
	gui_confbox_add_entry(aspect_confbox, "16:10", _("16:10 (Wide XGA)"), (gpointer) &aspect_1610);
	gui_confbox_add_entry(aspect_confbox, "16:9", _("16:9 (HDTV)"), (gpointer) &aspect_169);
	gui_confbox_add_entry(aspect_confbox, "8:3", _("8:3 (Dualhead XGA)"), (gpointer) &aspect_83);
	gui_confbox_add_entry(aspect_confbox, "1:1", _("1:1"), (gpointer) &aspect_11);
	gui_confbox_add_entry(aspect_confbox, "goldenrectangle", _("Golden rectangle"), (gpointer) &aspect_golden);
	gui_confbox_load_conf(aspect_confbox, "freeform");

	gtk_box_pack_start (GTK_BOX (aspect_hbox), aspect_label, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (aspect_hbox),
		gui_confbox_get_widget(aspect_confbox), FALSE, TRUE, 4);

	button_box = gtk_hbox_new(FALSE, 0);
	apply_button = gtk_button_new_with_label(_("Crop"));
	g_signal_connect (G_OBJECT(apply_button), "clicked", G_CALLBACK (crop_apply_clicked), preview);
	cancel_button = gtk_button_new_with_label(_("Don't crop"));
	g_signal_connect (G_OBJECT(cancel_button), "clicked", G_CALLBACK (crop_cancel_clicked), preview);
	gtk_box_pack_start (GTK_BOX (button_box), apply_button, TRUE, TRUE, 4);
	gtk_box_pack_start (GTK_BOX (button_box), cancel_button, TRUE, TRUE, 4);

	gtk_box_pack_start (GTK_BOX (vbox), roi_grid_hbox, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), aspect_hbox, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), button_box, FALSE, TRUE, 0);

	preview->tool = rs_toolbox_add_widget(preview->toolbox, vbox, _("Crop"));
	gtk_widget_show_all(preview->tool);

	if (preview->photo->crop)
	{
		preview->roi = *preview->photo->crop;
		rs_photo_set_crop(preview->photo, NULL);
		preview->state = CROP_IDLE;
	}
	else
		preview->state = CROP_START;

	/* Help text for cropping */
	preview->status_num = gui_status_push(_("Crop: Drag to select cropped area. Right Mouse Button inside cropped area: Apply Crop; Outside: Cancel crop"));

	if (!preview->zoom_to_fit)
		rs_preview_widget_set_zoom_to_fit(preview, TRUE);
}

/**
 * Removes crop from the loaded photo
 * @param preview A RSpreviewWidget
 */
void
rs_preview_widget_uncrop(RSPreviewWidget *preview)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (!preview->photo) return;

	rs_photo_set_crop(preview->photo, NULL);
}

/**
 * Puts a RSPreviewWidget in straighten-mode
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_straighten(RSPreviewWidget *preview)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	if (!(preview->state & NORMAL))
		return;

	preview->state = STRAIGHTEN_START;
	preview->status_num = gui_status_push(_("Straighten: Draw a line in the image that should be horizontal or vertical. Right Mouse Button cancels."));
}

/**
 * Removes straighten from the loaded photo
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_unstraighten(RSPreviewWidget *preview)
{
	g_assert(RS_IS_PREVIEW_WIDGET(preview));

	rs_photo_set_angle(preview->photo, 0.0, FALSE);
}

/**
 * Enables quick mode in display
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_quick_start(RSPreviewWidget *preview, gboolean keep_quick) 
{
	gint i;

	preview->keep_quick_enabled = keep_quick;

	for(i=0;i<MAX_VIEWS;i++)
		rs_filter_request_set_quick(preview->request[i], TRUE);

}

/**
 * Disables quick mode in display and redraws screen
 * @param preview A RSPreviewWidget
 */
void
rs_preview_widget_quick_end(RSPreviewWidget *preview) 
{
	preview->keep_quick_enabled = FALSE;

	canvas_draw(preview, NULL, FALSE);
	GUI_CATCHUP_DISPLAY(preview->display);
}

void 
rs_preview_widget_blank(RSPreviewWidget *preview)
{
  preview->photo_blank_stored = preview->photo;
  preview->photo = NULL;
  GtkWidget *widget = GTK_WIDGET(preview->canvas);
  GdkWindow *window = gtk_widget_get_window(widget);
	GdkRectangle rect;
	g_return_if_fail (RS_IS_PREVIEW_WIDGET(preview));

	if (gtk_widget_get_realized(GTK_WIDGET(preview->canvas)))
		gtk_widget_get_allocation(GTK_WIDGET(preview->canvas), &rect);
  
  gdk_window_begin_paint_rect(window, &rect);
  
  cairo_t *cr = gdk_cairo_create(window);
  cairo_rectangle(cr, 0.0, 0.0, rect.width, rect.height);
  cairo_set_source_rgb(cr, preview->bgcolor.red/65535.0, preview->bgcolor.green/65535.0, preview->bgcolor.blue/65535.0);
  cairo_fill(cr);
  cairo_destroy(cr);

  gdk_window_end_paint(window);
  GUI_CATCHUP_DISPLAY(preview->display);
}

void 
rs_preview_widget_unblank(RSPreviewWidget *preview)
{
  preview->photo = preview->photo_blank_stored;
  preview->photo_blank_stored = NULL;

  GUI_CATCHUP_DISPLAY(preview->display);
}

static void
get_max_size(RSPreviewWidget *preview, gint *width, gint *height)
{
	gint splitters = preview->views - 1; /* Splitters between the views */
	GdkRectangle rect;

	gtk_widget_get_allocation(GTK_WIDGET(preview), &rect);

	*width = rect.width - PADDING*2;
	*height = rect.height - PADDING*2;

	if (preview->split == SPLIT_VERTICAL)
		*width = (rect.width - splitters*SPLITTER_WIDTH)/preview->views - PADDING*2;

	if (preview->split == SPLIT_HORIZONTAL)
		*height = (rect.height - splitters*SPLITTER_WIDTH)/preview->views - PADDING*2;
}

static gint
get_view_from_coord(RSPreviewWidget *preview, const gint x, const gint y)
{
	gint view;
	GdkRectangle rect;

	gtk_widget_get_allocation(GTK_WIDGET(preview), &rect);

	if (preview->split == SPLIT_VERTICAL)
		view = preview->views*x/rect.width;
	else
		view = preview->views*y/rect.height;

	if (view>=MAX_VIEWS)
		view=MAX_VIEWS-1;

	/* Clamp */
	view = MAX(MIN(view, MAX_VIEWS-1), 0);

	return view;
}

static void
get_canvas_placement(RSPreviewWidget *preview, const guint view, GdkRectangle *placement)
{
	gint xoffset = 0, yoffset = 0;
	gint width = 0, height = 0;
	GdkRectangle rect;

	g_assert(VIEW_IS_VALID(view));
	g_assert(placement);

	gtk_widget_get_allocation(GTK_WIDGET(preview), &rect);

	if (preview->split == SPLIT_VERTICAL)
	{
		xoffset = view * (width/preview->views + SPLITTER_WIDTH/2);
		width = (width - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	if (preview->split == SPLIT_HORIZONTAL)
	{
		yoffset = view * (height/preview->views + SPLITTER_WIDTH/2);
		height = (height - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	placement->x = xoffset;
	placement->y = yoffset;
	placement->width = width;
	placement->height = height;
}

static gboolean
get_placement(RSPreviewWidget *preview, const guint view, GdkRectangle *placement)
{
	gint xoffset = 0, yoffset = 0;
	gint width, height;
	gint filter_width, filter_height;
	GdkRectangle rect;

	rs_filter_get_size_simple(preview->filter_end[view], preview->request[view], &filter_width, &filter_height);
	if (filter_width<1)
		return FALSE;
	if (!VIEW_IS_VALID(view))
		return FALSE;

	gtk_widget_get_allocation(GTK_WIDGET(preview), &rect);
	width = rect.width;
	height = rect.height;

	if (preview->split == SPLIT_VERTICAL)
	{
		xoffset = view * (width/preview->views + SPLITTER_WIDTH/2);
		width = (width - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	if (preview->split == SPLIT_HORIZONTAL)
	{
		yoffset = view * (height/preview->views + SPLITTER_WIDTH/2);
		height = (height - preview->views*SPLITTER_WIDTH)/preview->views;
	}

	placement->x = xoffset + (width - filter_width)/2;
	placement->y = yoffset + (height - filter_height)/2;
	placement->width = filter_width;
	placement->height = filter_height;
	return TRUE;
}

/**
 * Get the image coordinates from canvas-coordinates
 * @note Output will be clamped to image-space - ie all values are valid
 * @param preview A RSPreviewWidget
 * @param view The current view
 * @param x X coordinate as returned by GDK
 * @param y Y coordinate as returned by GDK
 * @param scaled_x A pointer to the scaled x or NULL
 * @param scaled_y A pointer to the scaled x or NULL
 * @param real_x A pointer to the "real" x (scale at 100%) or NULL
 * @param real_y A pointer to the "real" y (scale at 100%) or NULL
 * @param max_w A pointer to the width of the image at 100% scale or NULL
 * @param max_h A pointer to the height of the image at 100% scale or NULL
 * @return TRUE if coordinate is inside image, FALSE otherwise
 */
static gboolean
get_image_coord(RSPreviewWidget *preview, gint view, const gint x, const gint y, gint *scaled_x, gint *scaled_y, gint *real_x, gint *real_y, gint *max_w, gint *max_h)
{
	gboolean ret = FALSE;
	GdkRectangle placement;
	gint _scaled_x, _scaled_y;
	gint _real_x, _real_y;
	gint _max_w, _max_h;
	gint filter_width, filter_height;

	/* FIXME: This is so outdated */
	if (!preview->photo)
		return ret;

	if (!rs_filter_get_size_simple(preview->filter_end[view], preview->request[view], &filter_width, &filter_height))
		return ret;

	rs_image16_transform_getwh(preview->photo->input, preview->photo->crop, preview->photo->angle, preview->photo->orientation, &_max_w, &_max_h);

	get_placement(preview, view, &placement);

	if (preview->zoom_to_fit)
	{
		gfloat scale;
		g_object_get(preview->filter_resample[view], "scale", &scale, NULL);
		_scaled_x = x - placement.x;
		_scaled_y = y - placement.y;
		_real_x = _scaled_x / scale;
		_real_y = _scaled_y / scale;
	}
	else
	{
		_scaled_x = x + gtk_adjustment_get_value(preview->hadjustment);
		_scaled_y = y + gtk_adjustment_get_value(preview->vadjustment);
		_real_x = _scaled_x;
		_real_y = _scaled_y;
	}

	if ((_scaled_x < filter_width) && (_scaled_y < filter_height) && (_scaled_x >= 0) && (_scaled_y >= 0))
		ret = TRUE;

	if (scaled_x)
		*scaled_x = MIN(MAX(0, _scaled_x), filter_width);
	if (scaled_y)
		*scaled_y = MIN(MAX(0, _scaled_y), filter_height);
	if (real_x)
		*real_x = MIN(MAX(0, _real_x), _max_w);
	if (real_y)
		*real_y = MIN(MAX(0, _real_y), _max_h);
	if (max_w)
		*max_w = _max_w;
	if (max_h)
		*max_h = _max_h;

	return ret;
}

static void
realize(GtkWidget *widget, gpointer data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(widget);

	if (preview->zoom_to_fit)
	{
		gtk_widget_hide(preview->vscrollbar);
		gtk_widget_hide(preview->hscrollbar);
	}
	else
	{
		gtk_widget_show(preview->vscrollbar);
		gtk_widget_show(preview->hscrollbar);
	}
}

static gboolean
scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	if (!preview->zoom_to_fit)
	{
		GtkAdjustment *adj;
		gdouble value;
		gdouble page_size;
		gdouble upper;

		if (event->state & GDK_CONTROL_MASK || event->direction == GDK_SCROLL_LEFT || event->direction == GDK_SCROLL_RIGHT)
			adj = preview->hadjustment;
		else
			adj = preview->vadjustment;
		g_object_get(G_OBJECT(adj), "page-size", &page_size, "upper", &upper, NULL);
		
		if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_LEFT)
			value = MIN(gtk_adjustment_get_value(adj)-page_size/5.0, upper-page_size);
		else
			value = MIN(gtk_adjustment_get_value(adj)+page_size/5.0, upper-page_size);
			
			
		gtk_adjustment_set_value(adj, value);
	}
	return TRUE;
}

static void
size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	gint view, max_width, max_height;
	const gdouble width = (gdouble) allocation->width;
	const gdouble height = (gdouble) allocation->height;

	g_object_set(G_OBJECT(preview->hadjustment), "page_size", width, "page-increment", width/1.2, NULL);
	g_object_set(G_OBJECT(preview->vadjustment), "page_size", height, "page-increment", height/1.2, NULL);

	get_max_size(preview, &max_width, &max_height);

	for(view=0;view<preview->views;view++)
		rs_filter_set_recursive(preview->filter_end[view], "width", max_width, "height", max_height, NULL);
}

static gboolean
scrollbar_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	rs_preview_widget_quick_start(preview, TRUE);
	if (preview->state == WB_PICKER)
		preview->state = SCROLL;

	return FALSE;
}

static gboolean
scrollbar_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	if (preview->state == SCROLL)
		preview->state = WB_PICKER;
	GUI_CATCHUP_DISPLAY(preview->display);
	rs_preview_widget_quick_end(preview);

	return FALSE;
}

static void
adjustment_changed(GtkAdjustment *adjustment, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);
	if (!preview->zoom_to_fit)
		canvas_draw(preview, NULL, FALSE);
}

static gboolean
button(GtkWidget *widget, GdkEventButton *event, RSPreviewWidget *preview)
{
	const gint x = (gint) (event->x+0.5f);
	const gint y = (gint) (event->y+0.5f);
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(preview->canvas));
	const gint view = get_view_from_coord(preview, x, y);
	GtkUIManager *ui_manager = gui_get_uimanager();
	GdkScreen *preview_screen = gtk_widget_get_screen(GTK_WIDGET(preview));
	gint screen_number = gdk_screen_get_monitor_at_point(preview_screen, x,y);
	gint real_x = -1, real_y = -1;
	gint scaled_x, scaled_y;
	gboolean inside_image = get_image_coord(preview, view, x, y, &scaled_x, &scaled_y, &real_x, &real_y, NULL, NULL);

	g_return_val_if_fail(VIEW_IS_VALID(view), FALSE);

	/* White balance picker */
	if (inside_image
		&& (event->type == GDK_BUTTON_PRESS)
		&& (event->button == 1)
		&& (preview->state & WB_PICKER)
	    && !(event->state & GDK_CONTROL_MASK)
		&& g_signal_has_handler_pending(preview, signals[WB_PICKED], 0, FALSE))
	{
		RS_PREVIEW_CALLBACK_DATA cbdata;
		make_cbdata(preview, view, &cbdata, scaled_x, scaled_y, real_x, real_y);
		g_signal_emit (G_OBJECT (preview), signals[WB_PICKED], 0, &cbdata);
	}
	/* Pop-up-menu */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==3)
		&& (preview->state & NORMAL))
	{
		/* Hack to mark uncrop and unstraighten as in/sensitive */
		rs_core_action_group_activate("PhotoMenu");
		if (view==0)
		{
			GtkWidget *menu = gtk_ui_manager_get_widget (ui_manager, "/PreviewPopup");
			gtk_menu_set_screen(GTK_MENU(menu), preview_screen);
			gtk_menu_set_monitor(GTK_MENU(menu),screen_number);
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
		}
		else
		{
			GtkWidget *menu = gtk_ui_manager_get_widget (ui_manager, "/PreviewPopupRight");
			gtk_menu_set_screen(GTK_MENU(menu), preview_screen);
			gtk_menu_set_monitor(GTK_MENU(menu),screen_number);
			gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
		}
	}
	/* Crop begin */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (preview->state & CROP_START))
	{
		preview->crop_start.x = real_x;
		preview->crop_start.y = real_y;
		preview->crop_near = CROP_NEAR_SE;
		preview->state = CROP_MOVE_CORNER;
	}
	/* Crop release */
	else if ((event->type == GDK_BUTTON_RELEASE)
		&& (event->button==1)
		&& (preview->state & CROP))
	{
		preview->state = CROP_IDLE;
	}
	/* Crop move corner */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (preview->state & CROP_IDLE))
	{
		preview->crop_start.x = real_x;
		preview->crop_start.y = real_y;
		switch(preview->crop_near)
		{
			case CROP_NEAR_N:
				preview->crop_other_fixed = preview->roi.x1;
				preview->crop_start.x = preview->roi.x2;
				preview->crop_start.y = preview->roi.y2;
				preview->state = CROP_MOVE_SIDE;
				break;
			case CROP_NEAR_W:
				preview->crop_other_fixed = preview->roi.y1;
				preview->crop_start.x = preview->roi.x2;
				preview->crop_start.y = preview->roi.y2;
				preview->state = CROP_MOVE_SIDE;
				break;
			case CROP_NEAR_S:
				preview->crop_other_fixed = preview->roi.x2;
				preview->crop_start.x = preview->roi.x1;
				preview->crop_start.y = preview->roi.y1;
				preview->state = CROP_MOVE_SIDE;
				break;
			case CROP_NEAR_E:
				preview->crop_other_fixed = preview->roi.y2;
				preview->crop_start.x = preview->roi.x1;
				preview->crop_start.y = preview->roi.y1;
				preview->state = CROP_MOVE_SIDE;
				break;
			case CROP_NEAR_INSIDE:
				preview->state = CROP_MOVE_ALL;
				break;
			case CROP_NEAR_NW:
				preview->crop_start.x = preview->roi.x2;
				preview->crop_start.y = preview->roi.y2;
				preview->state = CROP_MOVE_CORNER;
				break;
			case CROP_NEAR_NE:
				preview->crop_start.x = preview->roi.x1;
				preview->crop_start.y = preview->roi.y2;
				preview->state = CROP_MOVE_CORNER;
				break;
			case CROP_NEAR_SE:
				preview->crop_start.x = preview->roi.x1;
				preview->crop_start.y = preview->roi.y1;
				preview->state = CROP_MOVE_CORNER;
				break;
			case CROP_NEAR_SW:
				preview->crop_start.x = preview->roi.x2;
				preview->crop_start.y = preview->roi.y1;
				preview->state = CROP_MOVE_CORNER;
				break;
			default:
				preview->crop_start.x = real_x;
				preview->crop_start.y = real_y;
				preview->state = CROP_MOVE_CORNER;
				break;
		}
	}
	/* Cancel */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==3)
		&& (!(preview->state & NORMAL)))
	{
		if (preview->state & CROP)
		{
			if (preview->crop_near == CROP_NEAR_INSIDE)
				crop_end(preview, TRUE);
			else
				crop_end(preview, FALSE);
		}
		if (preview->state & STRAIGHTEN_START)
			gui_status_pop(preview->status_num);

		preview->state = WB_PICKER;
		gdk_window_set_cursor(window, cur_normal);
	}
	/* Begin straighten */
	else if ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (preview->state & STRAIGHTEN_START))
	{
		preview->straighten_start.x = (gint) (event->x+0.5f);
		preview->straighten_start.y = (gint) (event->y+0.5f);
		preview->state = STRAIGHTEN_MOVE;
	}
	/* Move straighten */
	else if ((event->type == GDK_BUTTON_RELEASE)
		&& (event->button==1)
		&& (preview->state & STRAIGHTEN_MOVE))
	{
		preview->straighten_end.x = (gint) (event->x+0.5f);
		preview->straighten_end.y = (gint) (event->y+0.5f);
		preview->state = WB_PICKER;
		rs_photo_set_angle(preview->photo, preview->straighten_angle, TRUE);
		gui_status_pop(preview->status_num);
	}
	/* Middle mouse , ctrl + left -> loupe */
	else if (((event->type == GDK_BUTTON_PRESS)
		&& (event->button==2)) 
		|| ((event->type == GDK_BUTTON_PRESS)
		&& (event->button==1)
		&& (event->state & GDK_CONTROL_MASK)))
	{
		rs_loupe_set_screen(preview->loupe, preview_screen, screen_number);
		rs_loupe_set_coord(preview->loupe, real_x, real_y);
		rs_preview_widget_set_loupe_enabled(preview, view, TRUE);
	}
	if (event->type == GDK_BUTTON_RELEASE)
		rs_preview_widget_set_loupe_enabled(preview, view, FALSE);

	return FALSE;
}
static gboolean
motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);
	GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(preview->canvas));
	gint x, y;
	gint real_x, real_y;
	gint scaled_x, scaled_y;
	gint max_w, max_h;
	gint view;
	GdkModifierType mask;
	RS_RECT scaled;
	gboolean inside_image = FALSE;

	gdk_window_get_pointer(window, &x, &y, &mask);
	view = get_view_from_coord(preview, x, y);

	if ((x == preview->last_x) && (y == preview->last_y))
		return TRUE;
	preview->last_x = x;
	preview->last_y = y;

	/* Bail in silence */
	if (!VIEW_IS_VALID(view))
		return TRUE;

	if (preview->photo)
		inside_image = get_image_coord(preview, view, x, y, &scaled_x, &scaled_y, &real_x, &real_y, &max_w, &max_h);

/*	if (preview->state & MOVE)
	{
		GtkAdjustment *adj;
		gdouble val;

		if (coord_diff.x != 0)
		{
			adj = gtk_viewport_get_hadjustment(GTK_VIEWPORT(preview->viewport[0]));
			val = gtk_adjustment_get_value(adj) + coord_diff.x;
			if (val > (preview->scaled->w - adj->page_size))
				val = preview->scaled->w - adj->page_size;
			gtk_adjustment_set_value(adj, val);
		}
		if (coord_diff.y != 0)
		{
			adj = gtk_viewport_get_vadjustment(GTK_VIEWPORT(preview->viewport[0]));
			val = gtk_adjustment_get_value(adj) + coord_diff.y;
			if (val > (preview->scaled->h - adj->page_size))
				val = preview->scaled->h - adj->page_size;
			gtk_adjustment_set_value(adj, val);
		}
	}
*/

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & CROP_MOVE_CORNER))
	{
		preview->roi.x1 = preview->crop_start.x;
		preview->roi.y1 = preview->crop_start.y;
		preview->roi.x2 = real_x;
		preview->roi.y2 = real_y;

		rs_rect_normalize(&preview->roi, &preview->roi);

		/* Update near */
		if (real_x > preview->crop_start.x)
			preview->crop_near = CROP_NEAR_E;
		else
			preview->crop_near = CROP_NEAR_W;

		if (real_y > preview->crop_start.y)
			preview->crop_near |= CROP_NEAR_S;
		else
			preview->crop_near |= CROP_NEAR_N;

		/* Do aspect restriction */
		crop_find_size_from_aspect(&preview->roi, preview->crop_aspect, preview->crop_near);

		canvas_draw(preview, NULL, TRUE);
	}

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & CROP_MOVE_SIDE))
	{
		preview->roi.x1 = preview->crop_start.x;
		preview->roi.y1 = preview->crop_start.y;

		if (preview->crop_near & (CROP_NEAR_N | CROP_NEAR_S))
		{
				preview->roi.x2 = preview->crop_other_fixed;
				preview->roi.y2 = real_y;
		} 
		else
		{
				preview->roi.y2 = preview->crop_other_fixed;
				preview->roi.x2 = real_x;
		}

		rs_rect_normalize(&preview->roi, &preview->roi);

		/* Do aspect restriction */
		crop_find_size_from_aspect(&preview->roi, preview->crop_aspect, preview->crop_near);

		/* FIXME: When clipping we are not forcibly keeping aspect ration */
		preview->roi.x1 = MAX(0, preview->roi.x1);
		preview->roi.y1 = MAX(0, preview->roi.y1);
		preview->roi.x2 = MIN(max_w, preview->roi.x2);
		preview->roi.y2 = MIN(max_h, preview->roi.y2);

		canvas_draw(preview, NULL, TRUE);
	}

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & CROP_MOVE_ALL))
	{
		gint dist_x, dist_y;
		dist_x = real_x - preview->crop_start.x;
		dist_y = real_y - preview->crop_start.y;

		/* check borders */
		if ((preview->crop_move.x1 + dist_x) < 0)
			dist_x = 0 - preview->crop_move.x1;
		if ((preview->crop_move.y1 + dist_y) < 0)
			dist_y = 0 - preview->crop_move.y1;
		if (((preview->crop_move.x2 + dist_x) > max_w))
			dist_x = max_w - preview->crop_move.x2;
		if (((preview->crop_move.y2 + dist_y) > max_h))
			dist_y = max_h - preview->crop_move.y2;

		preview->roi.x1 = preview->crop_move.x1+dist_x;
		preview->roi.y1 = preview->crop_move.y1+dist_y;
		preview->roi.x2 = preview->crop_move.x2+dist_x;
		preview->roi.y2 = preview->crop_move.y2+dist_y;

		canvas_draw(preview, NULL, TRUE);
	}

	/* Update crop_near if mouse button 1 is NOT pressed */
	if ((preview->state & CROP) && !(mask & GDK_BUTTON1_MASK) && (preview->state != CROP_START))
	{
		gfloat scale;
		g_object_get(preview->filter_resample[view], "scale", &scale, NULL);
		scaled.x1 = preview->roi.x1 * scale;
		scaled.y1 = preview->roi.y1 * scale;
		scaled.x2 = preview->roi.x2 * scale;
		scaled.y2 = preview->roi.y2 * scale;
		preview->crop_near = crop_near(&scaled, scaled_x, scaled_y);
		/* Set cursor accordingly */
		switch(preview->crop_near)
		{
			case CROP_NEAR_NW:
				gdk_window_set_cursor(window, cur_nw);
				break;
			case CROP_NEAR_NE:
				gdk_window_set_cursor(window, cur_ne);
				break;
			case CROP_NEAR_SE:
				gdk_window_set_cursor(window, cur_se);
				break;
			case CROP_NEAR_SW:
				gdk_window_set_cursor(window, cur_sw);
				break;
			case CROP_NEAR_N:
				gdk_window_set_cursor(window, cur_n);
				break;
			case CROP_NEAR_S:
				gdk_window_set_cursor(window, cur_s);
				break;
			case CROP_NEAR_W:
				gdk_window_set_cursor(window, cur_w);
				break;
			case CROP_NEAR_E:
				gdk_window_set_cursor(window, cur_e);
				break;
			case CROP_NEAR_INSIDE:
				preview->crop_move = preview->roi;
				gdk_window_set_cursor(window, cur_fleur);
				break;
			default:
				if (inside_image)
					gdk_window_set_cursor(window, cur_crop);
				else
					gdk_window_set_cursor(window, cur_normal);
				break;
		}
	}

	if (preview->state == CROP_START)
	{
		if (inside_image)
			gdk_window_set_cursor(window, cur_crop);
		else
			gdk_window_set_cursor(window, cur_normal);
	}

	if (preview->state & STRAIGHTEN)
	{
		if (inside_image)
			gdk_window_set_cursor(window, cur_rotate);
		else
			gdk_window_set_cursor(window, cur_normal);
	}

	if ((preview->state & WB_PICKER))
	{
		if (inside_image)
			gdk_window_set_cursor(window, cur_color_picker);
		else
			gdk_window_set_cursor(window, cur_normal);
	}

	if ((mask & GDK_BUTTON1_MASK) && (preview->state & STRAIGHTEN_MOVE))
	{
		gdouble degrees;
		gint vx, vy;

		preview->straighten_end.x = x;
		preview->straighten_end.y = y;
		vx = preview->straighten_start.x - preview->straighten_end.x;
		vy = preview->straighten_start.y - preview->straighten_end.y;
		canvas_draw(preview, NULL, TRUE);
		degrees = -atan2(vy,vx)*180/M_PI;
		if (degrees>=0.0)
		{
			if ((degrees>45.0) && (degrees<=135.0))
				degrees -= 90.0;
			else if (degrees>135.0)
				degrees -= 180.0;
		}
		else /* <0.0 */
		{
			if ((degrees < -45.0) && (degrees >= -135.0))
				degrees += 90.0;
			else if (degrees<-135.0)
				degrees += 180.0;
		}
		preview->straighten_angle = degrees;
	}

	/* If anyone is listening, go ahead and emit signal */
	if (inside_image && g_signal_has_handler_pending(preview, signals[MOTION_SIGNAL], 0, FALSE))
	{
		RS_PREVIEW_CALLBACK_DATA cbdata;
		if (make_cbdata(preview, view, &cbdata, scaled_x, scaled_y, real_x, real_y))
		{
			g_signal_emit (G_OBJECT (preview), signals[MOTION_SIGNAL], 0, &cbdata);
			rs_toolbox_hover_value_updated(preview->toolbox, cbdata.pixel8);
		}
	}

	/* Check not to generate superfluous signals "leave"*/
	if (inside_image != preview->prev_inside_image)
	{
		preview->prev_inside_image = inside_image;
		if (!inside_image && g_signal_has_handler_pending(preview, signals[LEAVE_SIGNAL], 0, FALSE))	
		{
			RS_PREVIEW_CALLBACK_DATA cbdata;
			if (make_cbdata(preview, view, &cbdata, scaled_x, scaled_y, real_x, real_y))
			{
				g_signal_emit (G_OBJECT (preview), signals[LEAVE_SIGNAL], 0, &cbdata);
				rs_toolbox_hover_value_updated(preview->toolbox, NULL);
			}
		}
	}

	/* Update loupe if needed */
	if (preview->loupe_enabled)
	{
		if (view != preview->loupe_view)
		{
			rs_filter_set_previous(preview->loupe_filter_start, preview->filter_cache0[view]);
			rs_filter_set_recursive(preview->loupe_filter_end, "settings", preview->photo->settings[preview->snapshot[view]], NULL);
			preview->loupe_view = view;
		}
		rs_loupe_set_coord(preview->loupe, real_x, real_y);
	}


	return TRUE;
}

static gboolean 
leave(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	/* Check not to generate superfluous signals "leave"*/
	if (preview->prev_inside_image)
	{
		preview->prev_inside_image = FALSE;
		if (g_signal_has_handler_pending(preview, signals[LEAVE_SIGNAL], 0, FALSE))
			g_signal_emit (G_OBJECT (preview), signals[LEAVE_SIGNAL], 0, NULL);
	}
	return TRUE;
}

static void
lens_changed(RS_PHOTO *photo, RSPreviewWidget *preview)
{
	/* For now lensfun is the same for all views, so we update the first */
	rs_filter_set_recursive(preview->filter_end[0], "distortion-enabled", TRUE, NULL);
}

static void
profile_changed(RS_PHOTO *photo, gpointer profile, RSPreviewWidget *preview)
{
	gint view;

	if (photo == preview->photo)
	{
		/* Set view profile */
		for(view=0;view<MAX_VIEWS;view++)
		{
			/* We should only deal with this, if it's DCP, ICC is catched elsewhere */
			if (RS_IS_DCP_FILE(profile))
				g_object_set(preview->filter_dcp[view], "profile", profile, NULL);
			else
				g_object_set(preview->filter_dcp[view], "use-profile", FALSE, NULL);

			rs_filter_set_recursive(preview->filter_end[view], "settings", preview->photo->settings[preview->snapshot[view]], NULL);
		}

		/* Set navigator profile, uses view 0 */
		if (RS_IS_DCP_FILE(profile))
			g_object_set(preview->navigator_filter_dcp, "profile", profile, NULL);
		else
			g_object_set(preview->navigator_filter_dcp, "use-profile", FALSE, NULL);

		rs_filter_set_recursive(preview->navigator_filter_end, "settings", preview->photo->settings[preview->snapshot[0]], NULL);
	}
}

static void
filter_changed(RSFilter *filter, RSFilterChangedMask mask, RSPreviewWidget *preview)
{
	gint view;
	GdkRectangle placement;
	GdkRectangle dirty;

	/* Seed the dirty area as "nothing" */
	dirty.x = 0xffffff;
	dirty.y = 0xffffff;
	dirty.width = 0;
	dirty.height = 0;

	/* See if we can find a matching plugin */
	for(view=0;view<preview->views;view++)
	{
		if (filter == preview->filter_end[view])
		{
			if ((view==0) && (mask & RS_FILTER_CHANGED_DIMENSION))
			{
				gint width, height;
				rs_filter_get_size_simple(preview->filter_end[0], preview->request[0], &width, &height);
				gdouble val;
				val = (gdouble) width;
				g_object_set(G_OBJECT(preview->hadjustment), "upper", val, NULL);
				val = (gdouble) height;
				g_object_set(G_OBJECT(preview->vadjustment), "upper", val, NULL);

				/* We need to redraw everything if there's any spatial change */
				gtk_widget_get_allocation(GTK_WIDGET(preview->canvas), &dirty);
				dirty.x = 0;
				dirty.y = 0;
				break;
			}

			if (get_placement(preview, view, &placement))
			{
				dirty.x = MIN(placement.x, dirty.x);
				dirty.y = MIN(placement.y, dirty.y);
				dirty.width = MAX(placement.width + placement.x, dirty.width + dirty.x) - dirty.x;
				dirty.height = MAX(placement.height + placement.y, dirty.height + dirty.y) - dirty.y;
			}
		}
	}

	if (dirty.width > 0)
		canvas_draw(preview, &dirty, FALSE);
}

static void
crop_aspect_changed(gpointer active, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	preview->crop_aspect = *((gdouble *)active);
	canvas_draw(preview, NULL, FALSE);
}

static void
crop_grid_changed(gpointer active, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	preview->roi_grid = GPOINTER_TO_INT(active);
	canvas_draw(preview, NULL, FALSE);
}

static void
crop_apply_clicked(GtkButton *button, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	crop_end(preview, TRUE);
}

static void
crop_cancel_clicked(GtkButton *button, gpointer user_data)
{
	RSPreviewWidget *preview = RS_PREVIEW_WIDGET(user_data);

	crop_end(preview, FALSE);
}

static void
crop_end(RSPreviewWidget *preview, gboolean accept)
{
	if (accept)
		rs_photo_set_crop(preview->photo, &preview->roi);

	gtk_widget_destroy(preview->tool);
	preview->state = WB_PICKER;

	gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(preview->canvas)), cur_normal);

	gui_status_pop(preview->status_num);

	canvas_draw(preview, NULL, FALSE);
}

static void
crop_find_size_from_aspect(RS_RECT *roi, gdouble aspect, CROP_NEAR near)
{
	const gdouble original_w = (gdouble) ABS(roi->x2 - roi->x1 + 1);
	const gdouble original_h = (gdouble) ABS(roi->y2 - roi->y1 + 1);
	gdouble corrected_w, corrected_h;
	gdouble original_aspect = original_w/original_h;
	gboolean moving_top_bottom = (near == CROP_NEAR_N) || (near == CROP_NEAR_S);
	gboolean moving_left_right = (near == CROP_NEAR_E) || (near == CROP_NEAR_W);

	if (aspect == 0.0)
		return;

	if (original_aspect > 1.0)
	{ /* landscape */
		if ((original_aspect > aspect || moving_top_bottom) && !moving_left_right)
		{
			corrected_h = original_h;
			corrected_w = original_h * aspect;
		}
		else
		{
			corrected_w = original_w;
			corrected_h = original_w / aspect;
		}
	}
	else
	{ /* portrait */
		if (((1.0/original_aspect) > aspect || moving_left_right) && !moving_top_bottom)
		{
			corrected_w = original_w;
			corrected_h = original_w * aspect;
		}
		else
		{
			corrected_h = original_h;
			corrected_w = original_h / aspect;
		}
	}

	gdouble middle;
	switch(near)
	{
		case CROP_NEAR_NW: /* x1,y1 */
			roi->x1 = roi->x2 - ((gint)corrected_w) + 1;
			roi->y1 = roi->y2 - ((gint)corrected_h) + 1;
			break;
		case CROP_NEAR_NE: /* x2,y1 */
			roi->x2 = roi->x1 + ((gint)corrected_w) - 1;
			roi->y1 = roi->y2 - ((gint)corrected_h) + 1;
			break;
		case CROP_NEAR_SE: /* x2,y2 */
			roi->x2 = roi->x1 + ((gint)corrected_w) - 1;
			roi->y2 = roi->y1 + ((gint)corrected_h) - 1;
			break;
		case CROP_NEAR_SW: /* x1,y2 */
			roi->x1 = roi->x2 - ((gint)corrected_w) + 1;
			roi->y2 = roi->y1 + ((gint)corrected_h) - 1;
			break;
		case CROP_NEAR_N:
			middle = roi->x1 + 0.5 * (roi->x2 - roi->x1);
			roi->x1 = (gint)(middle - corrected_w * 0.5) + 1;
			roi->x2 = (gint)(middle + corrected_w * 0.5) - 1;
			roi->y1 = roi->y2 - ((gint)corrected_h) + 1;
			break;
		case CROP_NEAR_S:
			middle = roi->x1 + 0.5 * (roi->x2 - roi->x1);
			roi->x1 = (gint)(middle - corrected_w * 0.5) + 1;
			roi->x2 = (gint)(middle + corrected_w * 0.5) - 1;
			roi->y2 = roi->y1 + ((gint)corrected_h) - 1;
			break;
		case CROP_NEAR_W:
			middle = roi->y1 + 0.5 * (roi->y2 - roi->y1);
			roi->y1 = (gint)(middle - corrected_h * 0.5) + 1;
			roi->y2 = (gint)(middle + corrected_h * 0.5) - 1;
			roi->x1 = roi->x2 - corrected_w + 1;
			break;
		case CROP_NEAR_E:
			middle = roi->y1 + 0.5 * (roi->y2 - roi->y1);
			roi->y1 = (gint)(middle - corrected_h * 0.5) + 1;
			roi->y2 = (gint)(middle + corrected_h * 0.5) - 1;
			roi->x2 = roi->x1 + corrected_w - 1;
			break;
		default: /* Shut up GCC! */
			break;
	}
}

static CROP_NEAR
crop_near(RS_RECT *roi, gint x, gint y)
{
	CROP_NEAR near = CROP_NEAR_NOTHING;
#define NEAR(aim, target) (ABS((target)-(aim))<15)
	if (NEAR(y, roi->y1)) /* N */
	{
		if (NEAR(x,roi->x1)) /* NW */
			near = CROP_NEAR_NW;
		else if (NEAR(x,roi->x2)) /* NE */
			near = CROP_NEAR_NE;
		else if ((x > roi->x1) && (x < roi->x2)) /* N */
			near = CROP_NEAR_N;
	}
	else if (NEAR(y, roi->y2)) /* S */
	{
		if (NEAR(x,roi->x1)) /* SW */
			near = CROP_NEAR_SW;
		else if (NEAR(x,roi->x2)) /* SE */
			near = CROP_NEAR_SE;
		else if ((x > roi->x1) && (x < roi->x2))
			near = CROP_NEAR_S;
	}
	else if (NEAR(x, roi->x1)
		&& (y > roi->y1)
		&& (y < roi->y2)) /* West */
		near = CROP_NEAR_W;
	else if (NEAR(x, roi->x2)
		&& (y > roi->y1)
		&& (y < roi->y2)) /* East */
		near = CROP_NEAR_E;

	if (near == CROP_NEAR_NOTHING)
	{
		if (((x>roi->x1) && (x<roi->x2))
			&& ((y>roi->y1) && (y<roi->y2))
			&& (((roi->x2-roi->x1)>2) && ((roi->y2-roi->y1)>2)))
			near = CROP_NEAR_INSIDE;
		else
			near = CROP_NEAR_OUTSIDE;
	}
	return near;
#undef NEAR
}

static gboolean
make_cbdata(RSPreviewWidget *preview, const gint view, RS_PREVIEW_CALLBACK_DATA *cbdata, gint screen_x, gint screen_y, gint real_x, gint real_y)
{
	gint row, col;
	gushort *pixel;
	gdouble r=0.0f, g=0.0f, b=0.0f;

	if ((view<0) || (view>(preview->views-1)))
		return FALSE;

	if (!preview->last_roi[view])
		return FALSE;

	RSFilterRequest *request = rs_filter_request_clone(preview->request[view]);
	rs_filter_request_set_quick(request, TRUE);
	if (preview->zoom_to_fit)
		rs_filter_request_set_roi(request, NULL);
	rs_filter_set_recursive(RS_FILTER(preview->filter_input), "demosaic-allow-downscale",  preview->zoom_to_fit, NULL);

	RSFilterResponse *response = rs_filter_get_image(preview->filter_cache1[view], request);
	RS_IMAGE16 *image = rs_filter_response_get_image(response);
	g_object_unref(response);

	rs_filter_param_set_object(RS_FILTER_PARAM(request), "colorspace", preview->exposure_color_space);

	/* We set input to the cache placed before exposure mask */
	response = rs_filter_get_image8(preview->filter_cache3[view], request);
	GdkPixbuf *buffer = rs_filter_response_get_image8(response);
	g_object_unref(response);
	g_object_unref(request);

	if (!image)
		return FALSE;

	if (!buffer)
		return FALSE;

	/* Get the real coordinates */
	cbdata->pixel = rs_image16_get_pixel(image, screen_x, screen_y, TRUE);

	cbdata->x = real_x;
	cbdata->y = real_y;

	/* Make sure these is within boundaries */
	screen_x = CLAMP(screen_x, 0, gdk_pixbuf_get_width(buffer)-1);
	screen_y = CLAMP(screen_y, 0, gdk_pixbuf_get_height(buffer)-1);

	cbdata->pixel8[R] = GET_PIXBUF_PIXEL(buffer, screen_x, screen_y)[R];
	cbdata->pixel8[G] = GET_PIXBUF_PIXEL(buffer, screen_x, screen_y)[G];
	cbdata->pixel8[B] = GET_PIXBUF_PIXEL(buffer, screen_x, screen_y)[B];

	/* Find average pixel values from 3x3 pixels */
	for(row=-1; row<2; row++)
	{
		for(col=-1; col<2; col++)
		{
			pixel = rs_image16_get_pixel(image, screen_x+col, screen_y+row, TRUE);
			r += pixel[R]/65535.0;
			g += pixel[G]/65535.0;
			b += pixel[B]/65535.0;
		}
	}
	cbdata->pixelfloat[R] = (gfloat) r/9.0f;
	cbdata->pixelfloat[G] = (gfloat) g/9.0f;
	cbdata->pixelfloat[B] = (gfloat) b/9.0f;

	g_object_unref(buffer);
	g_object_unref(image);
	return TRUE;
}

static void
canvas_draw(RSPreviewWidget *preview, GdkRectangle *rect, gboolean now)
{
	GtkWidget *widget = GTK_WIDGET(preview->canvas);
	GdkWindow *window = gtk_widget_get_window(widget);

	if (now)
	{
		cairo_t *cr = gdk_cairo_create(window);

		if (rect)
		{
			cairo_new_path(cr);
			cairo_rectangle(cr, rect->x, rect->y, rect->width, rect->height);
			cairo_clip(cr);
		}
		canvas_draw_handler(widget, cr, preview);
	}
	else
	{
		if (rect)
			gtk_widget_queue_draw_area(widget, rect->x, rect->y, rect->width, rect->height);
		else
			gtk_widget_queue_draw(widget);
	}
}

static void
canvas_draw_handler(GtkWidget *widget, cairo_t *cr, RSPreviewWidget *preview)
{
	GdkRectangle area;
	GdkRectangle placement;
	gint i;
	const static gdouble dashes[] = { 4.0, 4.0, };
	gint width, height;
	GdkRectangle dirty_area;
	GdkRectangle rect;

	gtk_widget_get_allocation(GTK_WIDGET(preview->canvas), &rect);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_GRAY);
	if (!gdk_cairo_get_clip_rectangle(cr, &dirty_area))
	{
		dirty_area.x = 0;
		dirty_area.y = 0;
		dirty_area.width = rect.width;
		dirty_area.height = rect.height;
	}

#define CAIRO_LINE(cr, x1, y1, x2, y2) do { \
	cairo_move_to((cr), (x1), (y1)); \
	cairo_line_to((cr), (x2), (y2)); } while (0);

	for(i=0;i<preview->views;i++)
	{
		rs_filter_get_size_simple(preview->filter_end[i], preview->request[i], &width, &height);

		if (preview->zoom_to_fit)
			get_placement(preview, i, &placement);
		else
		{
			if (width > rect.width)
				placement.x = -gtk_adjustment_get_value(preview->hadjustment);
			else
				placement.x = ((rect.width)-width)/2;

			if (height > rect.height)
				placement.y = -gtk_adjustment_get_value(preview->vadjustment);
			else
				placement.y = ((rect.height)-height)/2;

			placement.width = width;
			placement.height = height;
		}

		/* Render the photo itself */
		if (preview->photo && gdk_rectangle_intersect(&dirty_area, &placement, &area))
		{
			GdkRectangle roi = area;
			roi.x -= placement.x;
			roi.y -= placement.y;

			if (!preview->last_roi[i])
				preview->last_roi[i] = g_new(GdkRectangle, 1);
			*preview->last_roi[i] = roi;

			if (preview->zoom_to_fit)
				rs_filter_request_set_roi(preview->request[i], NULL);
			else
				rs_filter_request_set_roi(preview->request[i], &roi);

			/* Clone, now so it cannot change while filters are being called */
			RSFilterRequest *new_request = rs_filter_request_clone(preview->request[i]);  

			RSFilterResponse *response = rs_filter_get_image8(preview->filter_end[i], new_request);
			GdkPixbuf *buffer = rs_filter_response_get_image8(response);

			if (buffer)
			{
				if (area.x-placement.x >= 0 && area.x-placement.x + area.width <= gdk_pixbuf_get_width(buffer)
					&& area.y-placement.y >= 0 && area.y-placement.y + area.height <= gdk_pixbuf_get_height(buffer))
				{
					gdk_cairo_set_source_pixbuf(cr, buffer, area.x, area.y);
					cairo_rectangle(cr, area.x, area.y, area.width, area.height);
					cairo_fill(cr);
				}

				g_object_unref(buffer);
			}

			if(preview->views > 1 && rs_filter_request_get_quick(new_request) && !preview->keep_quick_enabled)
			{
				rs_filter_request_set_quick(preview->request[i], FALSE);
				canvas_draw(preview, &area, FALSE);
			}
			else if(rs_filter_request_get_quick(new_request) && !preview->keep_quick_enabled)
			{
				/* Catch up, so we can get new signals */
				g_object_unref(new_request);
				g_object_unref(response);
				if (!(preview->photo && preview->photo->signal && *preview->photo->signal == MAIN_SIGNAL_CANCEL_LOAD))
				{
					rs_filter_request_set_quick(preview->request[i], FALSE);
					canvas_draw(preview, &area, FALSE);
				}
				return;
			}
			else if (preview->photo && NULL==preview->photo->crop && NULL==preview->photo->proposed_crop)
			{
				preview->photo->proposed_crop = g_new(RS_RECT,1);
				if (ABS(preview->photo->angle) < 0.001 &&
					rs_filter_param_get_integer(RS_FILTER_PARAM(response), "proposed-crop-x1", &preview->photo->proposed_crop->x1) &&
						rs_filter_param_get_integer(RS_FILTER_PARAM(response), "proposed-crop-y1", &preview->photo->proposed_crop->y1) &&
							rs_filter_param_get_integer(RS_FILTER_PARAM(response), "proposed-crop-x2", &preview->photo->proposed_crop->x2) &&
								rs_filter_param_get_integer(RS_FILTER_PARAM(response), "proposed-crop-y2", &preview->photo->proposed_crop->y2))
				{
					if (preview->photo->orientation)
						rs_photo_rotate_rect_inverse(preview->photo, preview->photo->proposed_crop);
				}
				else 
				{
					g_free(preview->photo->proposed_crop);
					preview->photo->proposed_crop = NULL;
				}
			}
			g_object_unref(new_request);
			g_object_unref(response);
		}

		if (preview->state & DRAW_ROI)
		{
			gfloat scale;
			gchar *text;
			cairo_text_extents_t te;

			gint x1,y1,x2,y2;
			/* Translate to screen coordinates */
			g_object_get(preview->filter_resample[i], "scale", &scale, NULL);
			x1 = preview->roi.x1 * scale;
			y1 = preview->roi.y1 * scale;
			x2 = preview->roi.x2 * scale;
			y2 = preview->roi.y2 * scale;

			text = g_strdup_printf("%d x %d", preview->roi.x2-preview->roi.x1, preview->roi.y2-preview->roi.y1);

			/* creates a rectangle that matches the photo */
			gdk_cairo_rectangle(cr, &placement);

			/* Translate to photo coordinates */
			cairo_translate(cr, placement.x, placement.y);

			/* creates a rectangle that matches ROI */
			cairo_rectangle(cr, x1, y1, x2-x1, y2-y1);
			/* create fill rule that only fills between the two rectangles */
			cairo_set_fill_rule (cr, CAIRO_FILL_RULE_EVEN_ODD);
			cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.5);
			/* fill acording to rule */
			cairo_fill_preserve (cr);
			/* center rectangle */
			cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.0);
			cairo_stroke (cr);

			cairo_set_line_width(cr, 2.0);

			cairo_set_dash(cr, dashes, 0, 0.0);
			cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.5);
			cairo_rectangle(cr, x1, y1, x2-x1, y2-y1);
			cairo_stroke(cr);

			cairo_set_line_width(cr, 1.0);
			cairo_set_dash(cr, dashes, 2, 0.0);
			cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
			/* Print size below rectangle */
			cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cr, 12.0);
				cairo_text_extents (cr, text, &te);
			if (y2 > (placement.height-18))
				cairo_move_to(cr, (x2+x1)/2.0 - te.width/2.0, y2-5.0);
			else
				cairo_move_to(cr, (x2+x1)/2.0 - te.width/2.0, y2+14.0);
			cairo_show_text (cr, text);

			switch(preview->roi_grid)
			{
				case ROI_GRID_NONE:
					break;
				case ROI_GRID_GOLDEN:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					/* vertical */
					golden = ((x2-x1)/goldenratio);

					t = (x1+golden);
					CAIRO_LINE(cr, t, y1, t, y2);
					t = (x2-golden);
					CAIRO_LINE(cr, t, y1, t, y2);

					/* horizontal */
					golden = ((y2-y1)/goldenratio);

					t = (y1+golden);
					CAIRO_LINE(cr, x1, t, x2, t);
					t = (y2-golden);
					CAIRO_LINE(cr, x1, t, x2, t);
					break;
				}
				case ROI_GRID_THIRDS:
				{
					gint t;

					/* vertical */
					t = ((x2-x1+1)/3*1+x1);
					CAIRO_LINE(cr, t, y1, t, y2);
					t = ((x2-x1+1)/3*2+x1);
					CAIRO_LINE(cr, t, y1, t, y2);

					/* horizontal */
					t = ((y2-y1+1)/3*1+y1);
					CAIRO_LINE(cr, x1, t, x2, t);
					t = ((y2-y1+1)/3*2+y1);
					CAIRO_LINE(cr, x1, t, x2, t);
					break;
				}

				case ROI_GRID_GOLDEN_TRIANGLES1:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x1, y1, x2, y2);

					t = (x2-golden);
					CAIRO_LINE(cr, x1, y2, t, y1);

					t = (x1+golden);
					CAIRO_LINE(cr, x2, y1, t, y2);
					break;
				}
				case ROI_GRID_GOLDEN_TRIANGLES2:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x2, y1, x1, y2);

					t = (x2-golden);
					CAIRO_LINE(cr, x1, y1, t, y2);

					t = (x1+golden);
					CAIRO_LINE(cr, x2, y2, t, y1);
					break;
				}

				case ROI_GRID_HARMONIOUS_TRIANGLES1:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x1, y1, x2, y2);

					t = (x1+golden);
					CAIRO_LINE(cr, x1, y2, t, y1);

					t = (x2-golden);
					CAIRO_LINE(cr, x2, y1, t, y2);
					break;
				}
				case ROI_GRID_HARMONIOUS_TRIANGLES2:
				{
					gdouble goldenratio = ((1+sqrt(5))/2);
					gint t, golden;

					golden = ((x2-x1)/goldenratio);

					CAIRO_LINE(cr, x1, y2, x2, y1);

					t = (x1+golden);
					CAIRO_LINE(cr, x1, y1, t, y2);

					t = (x2-golden);
					CAIRO_LINE(cr, x2, y2, t, y1);
					break;
				}
			}
			cairo_stroke(cr);

			/* Translate "back" */
			cairo_translate(cr, -placement.x, -placement.y);
			gtk_label_set_text(GTK_LABEL(preview->crop_size_label), text);
			g_free(text);
		}

		/* Draw snapshot-identifier */
		if (preview->views > 1)
		{
			GdkRectangle canvas;
			const gchar *txt;
			switch (preview->snapshot[i])
			{
				case 0:
					txt = "A";
					break;
				case 1:
					txt = "B";
					break;
				case 2:
					txt = "C";
					break;
				default:
					txt = "-";
					break;
			}
			get_canvas_placement(preview, i, &canvas);

			cairo_set_dash(cr, dashes, 0, 0.0);
			cairo_select_font_face(cr, "Arial", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, 20.0);

			cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.7);
			cairo_move_to(cr, canvas.x+3.0, canvas.y+21.0);
			cairo_text_path(cr, txt);
			cairo_fill(cr);

			cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0);
			cairo_move_to(cr, canvas.x+3.0, canvas.y+21.0);
			cairo_text_path(cr, txt);
			cairo_stroke(cr);
		}
	}

	/* Draw straighten-line */
	if (preview->state & STRAIGHTEN_MOVE)
	{
		cairo_set_line_width(cr, 1.0);

		cairo_set_dash(cr, dashes, 2, 0.0);
		cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1.0);
		cairo_move_to(cr, preview->straighten_start.x, preview->straighten_start.y);
		cairo_line_to(cr, preview->straighten_end.x, preview->straighten_end.y);
		cairo_stroke(cr);

		cairo_set_dash(cr, dashes, 2, 10.0);
		cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
		cairo_move_to(cr, preview->straighten_start.x, preview->straighten_start.y);
		cairo_line_to(cr, preview->straighten_end.x, preview->straighten_end.y);
		cairo_stroke(cr);
	}

	/* Draw splitters */
	if (preview->views>0)
	{
		for(i=1;i<preview->views;i++)
		{
			GtkStyle *style;
			GdkRectangle canvas_allocation;
			GdkRectangle preview_allocation;

			style = gtk_widget_get_style(GTK_WIDGET(preview));
			gtk_widget_get_allocation(GTK_WIDGET(preview->canvas), &canvas_allocation);
			gtk_widget_get_allocation(GTK_WIDGET(preview), &preview_allocation);

			if (preview->split == SPLIT_VERTICAL)
				gtk_paint_vline(style, cr, GTK_STATE_NORMAL, widget, NULL,
					0,
					canvas_allocation.height,
					i * preview_allocation.width/preview->views - SPLITTER_WIDTH/2);
			else if (preview->split == SPLIT_HORIZONTAL)
				gtk_paint_hline(style, cr, GTK_STATE_NORMAL, widget, NULL,
					0,
					canvas_allocation.width,
					i * preview_allocation.height/preview->views - SPLITTER_WIDTH/2);
		}
	}
}

#undef CAIRO_LINE
