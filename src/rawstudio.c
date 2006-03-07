#include <gtk/gtk.h>
#include <math.h> /* pow() */
#include <string.h> /* memset() */
#include "dcraw_api.h"
#include "rawstudio.h"
#include "gtk-interface.h"
#include "color.h"
#include "matrix.h"

#define GETVAL(adjustment) \
	gtk_adjustment_get_value((GtkAdjustment *) adjustment)
#define SETVAL(adjustment, value) \
	gtk_adjustment_set_value((GtkAdjustment *) adjustment, value)

guchar previewtable[3][65536];

void
update_previewtable(RS_BLOB *rs, const gdouble gamma, const gdouble contrast)
{
	gint n,c;
	gdouble nd;
	gint res;
	static double gammavalue;
	if (gammavalue == (contrast/gamma)) return;
	gammavalue = contrast/gamma;

	for(c=0;c<3;c++)
	{
		for(n=0;n<65536;n++)
		{
			nd = ((gdouble) n) / 65535.0;
			res = (gint) (pow(nd, gammavalue) * 255.0);
			_CLAMP255(res);
			previewtable[c][n] = res;
		}
	}
}

void
rs_debug(RS_BLOB *rs)
{
	printf("rs: %d\n", (guint) rs);
	printf("rs->w: %d\n", rs->w);
	printf("rs->h: %d\n", rs->h);
	printf("rs->pitch: %d\n", rs->pitch);
	printf("rs->channels: %d\n", rs->channels);
	printf("rs->pixels: %d\n", (guint) rs->pixels);
	printf("rs->vis_w: %d\n", rs->vis_w);
	printf("rs->vis_h: %d\n", rs->vis_h);
	printf("rs->vis_pitch: %d\n", rs->vis_pitch);
	printf("rs->vis_scale: %d\n", rs->vis_scale);
	printf("rs->vis_pixels: %d\n", (guint) rs->vis_pixels);
	printf("\n");
	return;
}

void
update_scaled(RS_BLOB *rs)
{
	guint y,x;
	guint srcoffset, destoffset;

	if (!rs->in_use) return;

	/* 16 bit downscaled */
	rs->vis_scale = GETVAL(rs->scale);
	if (rs->vis_w != rs->w/rs->vis_scale) /* do we need to? */
	{
		if (rs->vis_w!=0) /* old allocs? */
		{
			g_free(rs->vis_pixels);
			rs->vis_w=0;
			rs->vis_h=0;
			rs->vis_pitch=0;
		}
		rs->vis_w = rs->w/rs->vis_scale;
		rs->vis_h = rs->h/rs->vis_scale;
		rs->vis_pitch = PITCH(rs->vis_w);
		rs->vis_pixels = (gushort *) g_malloc(rs->channels*rs->vis_pitch*rs->vis_h*sizeof(gushort));
		for(y=0; y<rs->vis_h; y++)
		{
			destoffset = y*rs->vis_pitch*rs->channels;
			srcoffset = y*rs->vis_scale*rs->pitch*rs->channels;
			for(x=0; x<rs->vis_w; x++)
			{
				rs->vis_pixels[destoffset+R] = rs->pixels[srcoffset+R];
				rs->vis_pixels[destoffset+G] = rs->pixels[srcoffset+G];
				rs->vis_pixels[destoffset+B] = rs->pixels[srcoffset+B];
				if (rs->channels==4) rs->vis_pixels[destoffset+G2] = rs->pixels[srcoffset+G2];
				destoffset += rs->channels;
				srcoffset += rs->vis_scale*rs->channels;
			}
		}
		rs->vis_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, rs->vis_w, rs->vis_h);
		gtk_image_set_from_pixbuf((GtkImage *) rs->vis_image, rs->vis_pixbuf);
		g_object_unref(rs->vis_pixbuf);
	}
	return;
}

void
update_preview(RS_BLOB *rs)
{
	RS_MATRIX4 mat;
	RS_MATRIX4Int mati;
	gint rowstride, x, y, srcoffset, destoffset;
	register gint r,g,b;
	guchar *pixels;

	if(!rs->in_use) return;

	SETVAL(rs->scale, floor(GETVAL(rs->scale))); // we only do integer scaling
	update_scaled(rs);
	update_previewtable(rs, GETVAL(rs->gamma), GETVAL(rs->contrast));
	matrix4_identity(&mat);
	matrix4_color_exposure(&mat, GETVAL(rs->exposure));
	matrix4_color_mixer(&mat, GETVAL(rs->rgb_mixer[R]), GETVAL(rs->rgb_mixer[G]), GETVAL(rs->rgb_mixer[B]));
	matrix4_color_saturate(&mat, GETVAL(rs->saturation));
	matrix4_color_hue(&mat, GETVAL(rs->hue));
	matrix4_to_matrix4int(&mat, &mati);

	pixels = gdk_pixbuf_get_pixels(rs->vis_pixbuf);
	rowstride = gdk_pixbuf_get_rowstride(rs->vis_pixbuf);
	memset(rs->vis_histogram, 0, sizeof(guint)*3*256); // reset histogram
	for(y=0 ; y<rs->vis_h ; y++)
	{
		srcoffset = y * rs->vis_pitch * rs->channels;
		destoffset = y * rowstride;
		for(x=0 ; x<rs->vis_w ; x++)
		{
			r = (rs->vis_pixels[srcoffset+R]*mati.coeff[0][0]
				+ rs->vis_pixels[srcoffset+G]*mati.coeff[0][1]
				+ rs->vis_pixels[srcoffset+B]*mati.coeff[0][2])>>MATRIX_RESOLUTION;
			g = (rs->vis_pixels[srcoffset+R]*mati.coeff[1][0]
				+ rs->vis_pixels[srcoffset+G]*mati.coeff[1][1]
				+ rs->vis_pixels[srcoffset+B]*mati.coeff[1][2])>>MATRIX_RESOLUTION;
			b = (rs->vis_pixels[srcoffset+R]*mati.coeff[2][0]
				+ rs->vis_pixels[srcoffset+G]*mati.coeff[2][1]
				+ rs->vis_pixels[srcoffset+B]*mati.coeff[2][2])>>MATRIX_RESOLUTION;
			_CLAMP65535_TRIPLET(r,g,b);
			pixels[destoffset] = previewtable[R][r];
			rs->vis_histogram[R][pixels[destoffset++]]++;
			pixels[destoffset] = previewtable[G][g];
			rs->vis_histogram[G][pixels[destoffset++]]++;
			pixels[destoffset] = previewtable[B][b];
			rs->vis_histogram[B][pixels[destoffset++]]++;
			srcoffset+=rs->channels; /* increment srcoffset by rs->channels */
		}
	}
	update_histogram(rs);
	gtk_image_set_from_pixbuf((GtkImage *) rs->vis_image, rs->vis_pixbuf);
	return;
}	

void
rs_reset(RS_BLOB *rs)
{
	guint c;
	gtk_adjustment_set_value((GtkAdjustment *) rs->exposure, 0.0);
	gtk_adjustment_set_value((GtkAdjustment *) rs->gamma, 2.2);
	gtk_adjustment_set_value((GtkAdjustment *) rs->saturation, 1.0);
	gtk_adjustment_set_value((GtkAdjustment *) rs->hue, 0.0);
	gtk_adjustment_set_value((GtkAdjustment *) rs->contrast, 1.0);
	for(c=0;c<3;c++)
		gtk_adjustment_set_value((GtkAdjustment *) rs->rgb_mixer[c], rs->raw->pre_mul[c]);
	rs->vis_scale = 2;
	rs->vis_w = 0;
	rs->vis_h = 0;
	rs->vis_pitch = 0;
	return;
}

void
rs_free_raw(RS_BLOB *rs)
{
	dcraw_close(rs->raw);
	g_free(rs->raw);
	rs->raw = NULL;
}

void
rs_free(RS_BLOB *rs)
{
	if (rs->in_use)
	{
		g_free(rs->pixels);
		rs->channels=0;
		rs->w=0;
		rs->h=0;
		if (rs->raw!=NULL)
			rs_free_raw(rs);
		rs->in_use=FALSE;
	}
}

void
rs_alloc(RS_BLOB *rs, const guint width, const guint height, const guint channels)
{
	if(rs->in_use)
		rs_free(rs);
	rs->w = width;
	rs->pitch = PITCH(width);
	rs->h = height;
	rs->channels = channels;
	rs->pixels = (gushort *) g_malloc(rs->channels*rs->pitch*rs->h*sizeof(unsigned short));
	rs->in_use = TRUE;
}

RS_BLOB *
rs_new()
{
	RS_BLOB *rs;
	guint c;
	rs = g_malloc(sizeof(RS_BLOB));

	rs->exposure = make_adj(rs, 0.0, -2.0, 2.0, 0.1, 0.5);
	rs->gamma = make_adj(rs, 2.2, 0.0, 3.0, 0.1, 0.5);
	rs->saturation = make_adj(rs, 1.0, 0.0, 3.0, 0.1, 0.5);
	rs->hue = make_adj(rs, 0.0, 0.0, 360.0, 0.5, 30.0);
	rs->contrast = make_adj(rs, 1.0, 0.0, 3.0, 0.1, 0.1);
	rs->scale = make_adj(rs, 2.0, 1.0, 5.0, 1.0, 1.0);
	for(c=0;c<3;c++)
		rs->rgb_mixer[c] = make_adj(rs, 0.0, 0.0, 5.0, 0.1, 0.5);
	rs->raw = NULL;
	rs->in_use = FALSE;
	return(rs);
}

void
rs_load_raw_from_memory(RS_BLOB *rs)
{
	gushort *src = (gushort *) rs->raw->raw.image;
	guint x,y;
	guint srcoffset, destoffset;

	for (y=0; y<rs->raw->raw.height; y++)
	{
#ifdef __i386__
		destoffset = (guint) (rs->pixels + y*rs->pitch * rs->channels);
		srcoffset = (guint) (src + y * rs->w * rs->channels);
		x = rs->raw->raw.width;
		while(x)
		{
			asm volatile (
				"xorl %%ecx, %%ecx\n\t" /* set %ecx to zero */
				"movw (%1), %%eax\n\t" /* copy source into register */
				"subl %2, %%eax\n\t" /* subtract black */
				"cmovs %%ecx, %%eax\n\t" /* if negative, set to %ecx */
				"sall $4, %%eax\n\t" /* bitshift (12 -> 16 bits) */
				"movl %%eax, (%0)\n\t" /* copy to dest */

				"add $2, %0\n\t" /* increment destination pointer */
				"add $2, %1\n\t" /* increment source pointer */
				"movw (%1), %%ebx\n\t"
				"subl %2, %%ebx\n\t"
				"cmovs %%ecx, %%ebx\n\t"
				"sall $4, %%ebx\n\t"
				"movl %%ebx, (%0)\n\t"

				"add $2, %0\n\t"
				"add $2, %1\n\t"
				"movw (%1), %%eax\n\t"
				"subl %2, %%eax\n\t"
				"cmovs %%ecx, %%eax\n\t"
				"sall $4, %%eax\n\t"
				"movl %%eax, (%0)\n\t"

				"add $2, %0\n\t"
				"add $2, %1\n\t"
				"movw (%1), %%ebx\n\t"
				"subl %2, %%ebx\n\t"
				"cmovs %%ecx, %%ebx\n\t"
				"sall $4, %%ebx\n\t"
				"movl %%ebx, (%0)\n\t"

				"add $2, %0\n\t"
				"add $2, %1\n\t"

				: "+r" (destoffset), "+r" (srcoffset)
				: "r" (rs->raw->black)
				: "%eax", "%ebx", "%ecx"
			);
			x--;
		}
#else
		destoffset = y*rs->pitch*rs->channels;
		srcoffset = y*rs->w*rs->channels;
		for (x=0; x<rs->raw->raw.width; x++)
		{
			register gint r,g,b;
			r = (src[srcoffset++] - rs->raw->black)<<4;
			g = (src[srcoffset++] - rs->raw->black)<<4;
			b = (src[srcoffset++] - rs->raw->black)<<4;
			_CLAMP65535_TRIPLET(r, g, b);
			rs->pixels[destoffset++] = r;
			rs->pixels[destoffset++] = g;
			rs->pixels[destoffset++] = b;

			if (rs->channels==4)
			{
				g = (src[srcoffset++] - rs->raw->black)<<4;
				_CLAMP65535(g);
				rs->pixels[destoffset++] = g;
			}
		}
#endif
	}
	rs->in_use=TRUE;
	return;
}

void
rs_load_raw_from_file(RS_BLOB *rs, const gchar *filename)
{
	dcraw_data *raw;

	if (rs->raw!=NULL) rs_free_raw(rs);
	raw = (dcraw_data *) g_malloc(sizeof(dcraw_data));
	dcraw_open(raw, (char *) filename);
	dcraw_load_raw(raw);
	rs_alloc(rs, raw->raw.width, raw->raw.height, 4);
	rs->raw = raw;
	rs_load_raw_from_memory(rs);
	update_preview(rs);
	return;
}


int
main(int argc, char **argv)
{
	gui_init(argc, argv);
	return(0);
}
