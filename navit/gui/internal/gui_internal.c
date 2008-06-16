/**
 * Navit, a modular navigation system.
 * Copyright (C) 2005-2008 Navit Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

//##############################################################################################################
//#
//# File: gui_internal.c
//# Description: New "internal" GUI for use with any graphics library
//# Comment: Trying to make a touchscreen friendly GUI
//# Authors: Martin Schaller (04/2008), Stefan Klumpp (04/2008)
//#
//##############################################################################################################


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libintl.h>
#include <glib.h>
#include "config.h"
#include "item.h"
#include "file.h"
#include "navit.h"
#include "debug.h"
#include "gui.h"
#include "coord.h"
#include "point.h"
#include "plugin.h"
#include "graphics.h"
#include "transform.h"
#include "color.h"
#include "map.h"
#include "layout.h"
#include "callback.h"
#include "vehicle.h"
#include "window.h"
#include "main.h"
#include "keys.h"
#include "config.h"

#define STATE_VISIBLE 1
#define STATE_SELECTED 2
#define STATE_HIGHLIGHTED 4
#define STATE_SENSITIVE 8

enum widget_type {
	widget_box=1,
	widget_button,
	widget_label,
	widget_image,
};

enum flags {
	gravity_none=0x00,
	gravity_left=1,
	gravity_xcenter=2,
	gravity_right=4,
	gravity_top=8,
	gravity_ycenter=16,
	gravity_bottom=32,
	gravity_left_top=gravity_left|gravity_top,
	gravity_top_center=gravity_xcenter|gravity_top,
	gravity_right_top=gravity_right|gravity_top,
	gravity_left_center=gravity_left|gravity_ycenter,
	gravity_center=gravity_xcenter|gravity_ycenter,
	gravity_right_center=gravity_right|gravity_ycenter,
	gravity_left_bottom=gravity_left|gravity_bottom,
	gravity_bottom_center=gravity_xcenter|gravity_bottom,
	gravity_right_bottom=gravity_right|gravity_bottom,
	flags_expand=0x100,
	flags_fill=0x200,
	orientation_horizontal=0x10000,
	orientation_vertical=0x20000,
	orientation_horizontal_vertical=0x40000,
};


//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
struct widget {
	enum widget_type type;
	struct graphics_gc *background;
	struct graphics_gc *foreground_frame;
	char *text;
	struct graphics_image *img;
	void (*func)(struct gui_priv *priv, struct widget *widget);
	void *data;
	char *prefix;
	char *name;
	struct pcoord c;
	int state;
	struct point p;
	int wmin,hmin;
	int w,h;
	int bl,br,bt,bb,spx,spy;
	enum flags flags;
	void *instance;
	int (*set_attr)(void *, struct attr *);
	int (*get_attr)(void *, enum attr_type, struct attr *, struct attr_iter *);
	void (*remove_cb)(void *, struct callback *cb);
	struct callback *cb;
	struct attr on;
	struct attr off;
	int is_on;
	int redraw;
	GList *children;
};

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
struct gui_priv {
        struct navit *nav;
	struct window *win;
	struct graphics *gra;
	struct graphics_gc *background;
	struct graphics_gc *background2;
	struct graphics_gc *highlight_background;
	struct graphics_gc *foreground;
	int spacing;
	int font_size;
	int fullscreen;
	struct graphics_font *font;
	int icon_xs, icon_s, icon_l;
	int pressed;
	struct widget *widgets;
	int widgets_count;
	int redraw;
	struct widget root;
	struct widget *highlighted;
	struct widget *highlighted_menu;
};

static void gui_internal_widget_render(struct gui_priv *this, struct widget *w);
static void gui_internal_widget_pack(struct gui_priv *this, struct widget *w);
static struct widget * gui_internal_box_new(struct gui_priv *this, enum flags flags);
static void gui_internal_widget_append(struct widget *parent, struct widget *child);
static void gui_internal_widget_destroy(struct gui_priv *this, struct widget *w);

static struct graphics_image *
image_new_scaled(struct gui_priv *this, char *name, int w, int h)
{
	struct graphics_image *ret=NULL;
	char *full_name=NULL;
	int i;

	for (i = 1 ; i < 5 ; i++) {
		full_name=NULL;
		switch (i) {
		case 1:
			full_name=g_strdup_printf("%s/xpm/%s.svg", getenv("NAVIT_SHAREDIR"), name);
			break;
		case 2:
			if (w != -1 && h != -1) {
				full_name=g_strdup_printf("%s/xpm/%s_%d_%d.png", getenv("NAVIT_SHAREDIR"), name, w, h);
			}
			break;
		case 3:
			full_name=g_strdup_printf("%s/xpm/%s.png", getenv("NAVIT_SHAREDIR"), name);
			break;
		case 4:
			full_name=g_strdup_printf("%s/xpm/%s.xpm", getenv("NAVIT_SHAREDIR"), name);
			break;
		}
		dbg(1,"trying '%s'\n", full_name);
		if (! full_name)
			continue;
		if (!file_exists(full_name)) {
			g_free(full_name);
			continue;
		}
		ret=graphics_image_new_scaled(this->gra, full_name, w, h);
		dbg(1,"ret=%p\n", ret);
		g_free(full_name);
		if (ret) 
			return ret;	
	}
	dbg(0,"failed to load %s with %d,%d\n", name, w, h);
	return NULL;
}

static struct graphics_image *
image_new_o(struct gui_priv *this, char *name)
{
	return image_new_scaled(this, name, -1, -1);
}

static struct graphics_image *
image_new_xs(struct gui_priv *this, char *name)
{
	return image_new_scaled(this, name, this->icon_xs, this->icon_xs);
}


static struct graphics_image *
image_new_s(struct gui_priv *this, char *name)
{
	return image_new_scaled(this, name, this->icon_s, this->icon_s);
}

static struct graphics_image *
image_new_l(struct gui_priv *this, char *name)
{
	return image_new_scaled(this, name, this->icon_l, this->icon_l);
}

static void
gui_internal_background_render(struct gui_priv *this, struct widget *w)
{
	struct point pnt=w->p;
	if (w->state & STATE_HIGHLIGHTED) 
		graphics_draw_rectangle(this->gra, this->highlight_background, &pnt, w->w-1, w->h-1);
	else {
		if (w->background)
			graphics_draw_rectangle(this->gra, w->background, &pnt, w->w-1, w->h-1);
	}
}
static struct widget *
gui_internal_label_new(struct gui_priv *this, char *text)
{
	struct point p[4];
	int h=this->font_size;

	struct widget *widget=g_new0(struct widget, 1);
	widget->type=widget_label;
	widget->text=g_strdup(text);
	graphics_get_text_bbox(this->gra, this->font, text, 0x10000, 0x0, p);
	widget->h=h;
	widget->w=p[2].x-p[0].x+this->spacing;
	widget->flags=gravity_center;

	return widget;
}

static struct widget *
gui_internal_label_new_abbrev(struct gui_priv *this, char *text, int maxwidth)
{
	struct widget *ret=NULL;
	char *tmp=g_malloc(strlen(text)+3);
	int i;

	i=strlen(text)-1;
	while (i >= 0) {
		strcpy(tmp, text);
		strcpy(tmp+i,"..");
		ret=gui_internal_label_new(this, tmp);
		if (ret->w < maxwidth)
			break;
		gui_internal_widget_destroy(this, ret);
		ret=NULL;
		i--;
	}
	g_free(tmp);
	return ret;
}

static struct widget *
gui_internal_image_new(struct gui_priv *this, struct graphics_image *image)
{
	struct widget *widget=g_new0(struct widget, 1);
	widget->type=widget_image;
	widget->img=image;
	if (image) {
		widget->w=image->width;
		widget->h=image->height;
	}
	return widget;
}

static void
gui_internal_image_render(struct gui_priv *this, struct widget *w)
{
	struct point pnt;

	gui_internal_background_render(this, w);
	pnt=w->p;
	pnt.x+=w->w/2-w->img->hot.x;
	pnt.y+=w->h/2-w->img->hot.y;
	graphics_draw_image(this->gra, this->foreground, &pnt, w->img);
}

static void
gui_internal_label_render(struct gui_priv *this, struct widget *w)
{
	struct point pnt=w->p;
	gui_internal_background_render(this, w);
	pnt.y+=w->h-this->spacing;
	graphics_draw_text(this->gra, this->foreground, NULL, this->font, w->text, &pnt, 0x10000, 0x0);
}

static struct widget *
gui_internal_text_new(struct gui_priv *this, char *text)
{
	char *s=g_strdup(text),*s2,*tok;
	struct widget *ret=gui_internal_box_new(this, gravity_center|orientation_vertical);
	s2=s;
	while ((tok=strtok(s2,"\n"))) {
		gui_internal_widget_append(ret, gui_internal_label_new(this, tok));
		s2=NULL;
	}
	return ret;
}


static struct widget *
gui_internal_button_new_with_callback(struct gui_priv *this, char *text, struct graphics_image *image, enum flags flags, void(*func)(struct gui_priv *priv, struct widget *widget), void *data)
{
	struct widget *ret=NULL;
	if (text && image) {
		ret=gui_internal_box_new(this, flags);
		gui_internal_widget_append(ret, gui_internal_image_new(this, image));
		gui_internal_widget_append(ret, gui_internal_text_new(this, text));
	} else {
		if (text)
			ret=gui_internal_label_new(this, text);
		if (image)
			ret=gui_internal_image_new(this, image);
		if (! ret)
			ret=gui_internal_box_new(this, flags);
	}
	if (ret) {
		ret->func=func;
		ret->data=data;
		if (func) 
			ret->state |= STATE_SENSITIVE;
	}
	return ret;

}

static int
gui_internal_button_attr_update(struct gui_priv *this, struct widget *w)
{
	struct widget *wi;
	int is_on=0;
	struct attr curr;
	GList *l;

	if (w->get_attr(w->instance, w->on.type, &curr, NULL))
		is_on=curr.u.data == w->on.u.data;
	if (is_on != w->is_on) {
		if (w->redraw)
			this->redraw=1;
		w->is_on=is_on;
		l=g_list_first(w->children);
		if (l) {
			wi=l->data;
			if (wi->img)
				graphics_image_free(this->gra, wi->img);
			wi->img=image_new_xs(this, is_on ? "gui_active" : "gui_inactive");
		}
		if (w->is_on && w->off.type == attr_none)
			w->state &= ~STATE_SENSITIVE;
		else
			w->state |= STATE_SENSITIVE;
		return 1;
	}
	return 0;
}

static void
gui_internal_button_attr_callback(struct gui_priv *this, struct widget *w)
{
	if (gui_internal_button_attr_update(this, w))
		gui_internal_widget_render(this, w);
}
static void
gui_internal_button_attr_pressed(struct gui_priv *this, struct widget *w)
{
	if (w->is_on) 
		w->set_attr(w->instance, &w->off);
	else
		w->set_attr(w->instance, &w->on);
	gui_internal_button_attr_update(this, w);
	
}

static struct widget *
gui_internal_button_navit_attr_new(struct gui_priv *this, char *text, enum flags flags, struct attr *on, struct attr *off)
{
	struct graphics_image *image=image_new_xs(this, "gui_inactive");
	struct widget *ret;
	ret=gui_internal_button_new_with_callback(this, text, image, flags, gui_internal_button_attr_pressed, NULL);
	if (on)
		ret->on=*on;
	if (off)
		ret->off=*off;
	ret->get_attr=navit_get_attr;
	ret->set_attr=navit_set_attr;
	ret->remove_cb=navit_remove_callback;
	ret->instance=this->nav;
	ret->cb=callback_new_attr_2(gui_internal_button_attr_callback, on->type, this, ret);
	navit_add_callback(this->nav, ret->cb);
	gui_internal_button_attr_update(this, ret);
	return ret;
}

static struct widget *
gui_internal_button_map_attr_new(struct gui_priv *this, char *text, enum flags flags, struct map *map, struct attr *on, struct attr *off)
{
	struct graphics_image *image=image_new_xs(this, "gui_inactive");
	struct widget *ret;
	ret=gui_internal_button_new_with_callback(this, text, image, flags, gui_internal_button_attr_pressed, NULL);
	if (on)
		ret->on=*on;
	if (off)
		ret->off=*off;
	ret->get_attr=map_get_attr;
	ret->set_attr=map_set_attr;
	ret->remove_cb=map_remove_callback;
	ret->instance=map;
	ret->redraw=1;
	ret->cb=callback_new_attr_2(gui_internal_button_attr_callback, on->type, this, ret);
	map_add_callback(map, ret->cb);
	gui_internal_button_attr_update(this, ret);
	return ret;
}

static struct widget *
gui_internal_button_new(struct gui_priv *this, char *text, struct graphics_image *image, enum flags flags)
{
	return gui_internal_button_new_with_callback(this, text, image, flags, NULL, NULL);
}

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static void gui_internal_clear(struct gui_priv *this)
{
	struct graphics *gra=this->gra;
	struct point pnt;
	pnt.x=0;
	pnt.y=0;
	graphics_draw_rectangle(gra, this->background, &pnt, this->root.w, this->root.h);
}

static struct widget *
gui_internal_find_widget(struct widget *wi, struct point *p, int flags)
{
	GList *l=wi->children;
	struct widget *ret,*child;

	if (wi->p.x > p->x || wi->p.y > p->y || wi->p.x + wi->w < p->x || wi->p.y + wi->h < p->y) {
		return NULL;
	}
	if (wi->state & flags) {
		return wi;
	}
	while (l) {
		child=l->data;
		ret=gui_internal_find_widget(child, p, flags);
		if (ret) {
			return ret;
		}
		l=g_list_next(l);
	}
	return NULL;
	
}
//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static void gui_internal_highlight(struct gui_priv *this, struct point *p)
{
	struct widget *menu,*found=NULL;

	if (p) {
		menu=g_list_last(this->root.children)->data;
		found=gui_internal_find_widget(menu, p, STATE_SENSITIVE);
	}
	if (found == this->highlighted)
		return;
	if (this->highlighted) {
		this->highlighted->state &= ~STATE_HIGHLIGHTED;
		if (this->root.children && this->highlighted_menu == g_list_last(this->root.children)->data) 
			gui_internal_widget_render(this, this->highlighted);
		this->highlighted=NULL;
		this->highlighted_menu=NULL;
	}
	if (found) {
		this->highlighted=found;
		this->highlighted_menu=g_list_last(this->root.children)->data;
		this->highlighted->state |= STATE_HIGHLIGHTED;
		gui_internal_widget_render(this, this->highlighted);
#if 0
		dbg(0,"%d,%d %dx%d\n", found->p.x, found->p.y, found->w, found->h);
#endif
	}
}

static struct widget *
gui_internal_box_new_with_label(struct gui_priv *this, enum flags flags, char *label)
{
	struct widget *widget=g_new0(struct widget, 1);

	if (label) 
		widget->text=g_strdup(label);
	widget->type=widget_box;
	widget->flags=flags;
	return widget;
}

static struct widget *
gui_internal_box_new(struct gui_priv *this, enum flags flags)
{
	return gui_internal_box_new_with_label(this, flags, NULL);
}


static void gui_internal_box_render(struct gui_priv *this, struct widget *w)
{
	struct widget *wc;
	GList *l;

	gui_internal_background_render(this, w);
#if 0
	struct point pnt[5];
	pnt[0]=w->p;
        pnt[1].x=pnt[0].x+w->w;
        pnt[1].y=pnt[0].y;
        pnt[2].x=pnt[0].x+w->w;
        pnt[2].y=pnt[0].y+w->h;
        pnt[3].x=pnt[0].x;
        pnt[3].y=pnt[0].y+w->h;
        pnt[4]=pnt[0];
	graphics_draw_lines(this->gra, this->foreground, pnt, 5);
#endif

	l=w->children;
	while (l) {
		wc=l->data;
		gui_internal_widget_render(this, wc);
		l=g_list_next(l);
	}
}

static void gui_internal_box_pack(struct gui_priv *this, struct widget *w)
{
	struct widget *wc;
	int x0,x=0,y=0,width=0,height=0,owidth=0,oheight=0,expand=0,count=0,rows=0,cols=3;
	GList *l;
	int orientation=w->flags & 0xffff0000;

	
	l=w->children;
	while (l) {
		count++;
		l=g_list_next(l);
	}
	if (orientation == orientation_horizontal_vertical && count <= cols)
		orientation=orientation_horizontal;
	switch (orientation) {
	case orientation_horizontal:
		l=w->children;
		while (l) {
			wc=l->data;
			gui_internal_widget_pack(this, wc);
			if (height < wc->h)
				height=wc->h;
			width+=wc->w;
			if (wc->flags & flags_expand)
				expand+=wc->w;
			l=g_list_next(l);
			if (l)
				width+=w->spx;
		}
		owidth=width;
		if (expand && w->w) {
			expand=100*(w->w-width+expand)/expand;
			owidth=w->w;
		} else
			expand=100;
		break;
	case orientation_vertical:
		l=w->children;
		while (l) {
			wc=l->data;
			gui_internal_widget_pack(this, wc);
			if (width < wc->w)
				width=wc->w;
			height+=wc->h;
			if (wc->flags & flags_expand)
				expand+=wc->h;
			l=g_list_next(l);
			if (l)
				height+=w->spy;
		}
		oheight=height;
		if (expand && w->h) {
			expand=100*(w->h-height+expand)/expand;
			oheight=w->h;
		} else
			expand=100;
		break;
	case orientation_horizontal_vertical:
		count=0;
		l=w->children;
		while (l) {
			wc=l->data;
			gui_internal_widget_pack(this, wc);
			if (height < wc->h)
				height=wc->h;
			if (width < wc->w) 
				width=wc->w;
			l=g_list_next(l);
			count++;
		}
		if (count < cols)
			cols=count;
		rows=(count+cols-1)/cols;
		width*=cols;
		height*=rows;
		width+=w->spx*(cols-1);
		height+=w->spy*(rows-1);
		owidth=width;
		oheight=height;
		expand=100;
		break;
	default:
		break;
	}
	if (! w->w && ! w->h) {
		w->w=w->bl+w->br+width;
		w->h=w->bt+w->bb+height;
	}
	if (w->flags & gravity_left) 
		x=w->p.x+w->bl;
	if (w->flags & gravity_xcenter)
		x=w->p.x+w->w/2-owidth/2;
	if (w->flags & gravity_right)
		x=w->p.x+w->w-w->br-owidth;
	if (w->flags & gravity_top)
		y=w->p.y+w->bt;
	if (w->flags & gravity_ycenter) 
		y=w->p.y+w->h/2-oheight/2;
	if (w->flags & gravity_bottom) 
		y=w->p.y+w->h-w->bb-oheight;
	l=w->children;
	switch (orientation) {
	case orientation_horizontal:
		l=w->children;
		while (l) {
			wc=l->data;
			wc->p.x=x;
			if (wc->flags & flags_fill)
				wc->h=w->h;
			if (wc->flags & flags_expand)
				wc->w=wc->w*expand/100;
			if (w->flags & gravity_top) 
				wc->p.y=y;
			if (w->flags & gravity_ycenter) 
				wc->p.y=y-wc->h/2;
			if (w->flags & gravity_bottom) 
				wc->p.y=y-wc->h;
			x+=wc->w+w->spx;
			l=g_list_next(l);
		}
		break;
	case orientation_vertical:
		l=w->children;
		while (l) {
			wc=l->data;
			wc->p.y=y;
			if (wc->flags & flags_fill)
				wc->w=w->w;
			if (wc->flags & flags_expand)
				wc->h=wc->h*expand/100;
			if (w->flags & gravity_left) 
				wc->p.x=x;
			if (w->flags & gravity_xcenter) 
				wc->p.x=x-wc->w/2;
			if (w->flags & gravity_right) 
				wc->p.x=x-wc->w;
			y+=wc->h+w->spy;
			l=g_list_next(l);
		}
		break;
	case orientation_horizontal_vertical:
		l=w->children;
		x0=x;
		count=0;
		width/=cols;
		height/=rows;
		while (l) {
			wc=l->data;
			printf("x=%d y=%d\n", x, y);
			wc->p.x=x;
			wc->p.y=y;
			if (wc->flags & flags_fill) {
				wc->w=width;
				wc->h=height;
			}
			if (w->flags & gravity_left) 
				wc->p.x=x;
			if (w->flags & gravity_xcenter) 
				wc->p.x=x+(width-wc->w)/2;
			if (w->flags & gravity_right) 
				wc->p.x=x+width-wc->w;
			if (w->flags & gravity_top) 
				wc->p.y=y;
			if (w->flags & gravity_ycenter) 
				wc->p.y=y+(height-wc->h)/2;
			if (w->flags & gravity_bottom) 
				wc->p.y=y-height-wc->h;
			x+=width;
			if (++count == cols) {
				count=0;
				x=x0;
				y+=height;
			}
			l=g_list_next(l);
		}
		break;
	default:
		break;
	}
	l=w->children;
	while (l) {
		wc=l->data;
		gui_internal_widget_pack(this, wc);
		l=g_list_next(l);
	}
}

static void gui_internal_widget_append(struct widget *parent, struct widget *child)
{
	if (! child->background)
		child->background=parent->background;
	parent->children=g_list_append(parent->children, child);
}

static void gui_internal_widget_destroy(struct gui_priv *this, struct widget *w)
{
	GList *l;
	struct widget *wc;

	l=w->children;
	while (l) {
		wc=l->data;
		gui_internal_widget_destroy(this, wc);
		l=g_list_next(l);
	}
	g_free(w->text);
	if (w->img)
		graphics_image_free(this->gra, w->img);
	if (w->prefix)
		g_free(w->prefix);
	if (w->name)
		g_free(w->name);
	if (w->cb && w->remove_cb)
		w->remove_cb(w->instance, w->cb);
	g_free(w);
}


static void
gui_internal_widget_render(struct gui_priv *this, struct widget *w)
{
	switch (w->type) {
	case widget_box:
		gui_internal_box_render(this, w);
		break;
	case widget_label:
		gui_internal_label_render(this, w);
		break;
	case widget_image:
		gui_internal_image_render(this, w);
		break;
	default:
		break;
	}
}

static void
gui_internal_widget_pack(struct gui_priv *this, struct widget *w)
{
	switch (w->type) {
	case widget_box:
		gui_internal_box_pack(this, w);
		break;
	default:
		break;
	}
}

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static void gui_internal_call_highlighted(struct gui_priv *this)
{
	if (! this->highlighted || ! this->highlighted->func)
		return;
	this->highlighted->func(this, this->highlighted);
}

static void
gui_internal_prune_menu(struct gui_priv *this, struct widget *w)
{
	GList *l;
	while ((l = g_list_last(this->root.children))) {
		if (l->data == w) {
			gui_internal_widget_render(this, w);
			return;
		}
		gui_internal_widget_destroy(this, l->data);
		this->root.children=g_list_remove(this->root.children, l->data);
	}
}

static void
gui_internal_cmd_return(struct gui_priv *this, struct widget *wm)
{
	gui_internal_prune_menu(this, wm->data);
}

static void
gui_internal_cmd_main_menu(struct gui_priv *this, struct widget *wm)
{
	gui_internal_prune_menu(this, this->root.children->data);
}

static struct widget *
gui_internal_top_bar(struct gui_priv *this)
{
	struct widget *w,*wm,*wh,*wc,*wcn;
	int dots_len, sep_len;
	GList *res=NULL,*l;
	int width,width_used=0,use_sep,incomplete=0;

	w=gui_internal_box_new(this, gravity_left_center|orientation_horizontal|flags_fill);
	w->bl=this->spacing;
	w->spx=this->spacing;
	w->background=this->background2;
	wm=gui_internal_button_new_with_callback(this, NULL,
		image_new_s(this, "gui_map"), gravity_center,
		gui_internal_cmd_return, NULL);
	wh=gui_internal_button_new_with_callback(this, NULL,
		image_new_s(this, "gui_home"), gravity_center,
		gui_internal_cmd_main_menu, NULL);
	gui_internal_widget_append(w, wm);
	gui_internal_widget_append(w, wh);
	width=this->root.w-w->bl-wm->w-w->spx-wh->w;
	l=g_list_last(this->root.children);
	wcn=gui_internal_label_new(this,"..");
	dots_len=wcn->w;
	gui_internal_widget_destroy(this, wcn);
	wcn=gui_internal_label_new(this,"»");
	sep_len=wcn->w;
	gui_internal_widget_destroy(this, wcn);
	while (l) {
		if (g_list_previous(l) || !g_list_next(l)) {
			wc=l->data;
			wcn=gui_internal_label_new(this, wc->text);
			if (g_list_previous(l) && g_list_previous(g_list_previous(l))) 
				use_sep=1;
			else
				use_sep=0;
			if (wcn->w + width_used + w->spx + (use_sep ? sep_len : 0) > width) {
				incomplete=1;
				gui_internal_widget_destroy(this, wcn);
				break;
			}
			width_used+=wcn->w;
			wcn->func=gui_internal_cmd_return;
			wcn->data=wc;
			wcn->state |= STATE_SENSITIVE;
			res=g_list_prepend(res, wcn);
			if (use_sep) {
				res=g_list_prepend(res, gui_internal_label_new(this, "»"));
				width_used+=sep_len+w->spx;
			}
			
		}
		l=g_list_previous(l);
	}
	if (incomplete) {
		if (! res) {
			wcn=gui_internal_label_new_abbrev(this, wc->text, width-width_used-w->spx-dots_len);
			wcn->func=gui_internal_cmd_return;
			wcn->data=wc;
			wcn->state |= STATE_SENSITIVE;
			res=g_list_prepend(res, wcn);
			l=g_list_previous(l);
			wc=l->data;
		}
		wcn=gui_internal_label_new(this, "..");
		wcn->func=gui_internal_cmd_return;
		wcn->data=wc;
		wcn->state |= STATE_SENSITIVE;
		res=g_list_prepend(res, wcn);
	}
	l=res;
	while (l) {
		gui_internal_widget_append(w, l->data);
		l=g_list_next(l);
	}
#if 0
	if (dots)
		gui_internal_widget_destroy(this, dots);
#endif
	return w;
}

static struct widget *
gui_internal_menu(struct gui_priv *this, char *label)
{
	struct widget *menu,*w;
	menu=gui_internal_box_new_with_label(this, gravity_center|orientation_vertical, label);
	menu->w=this->root.w;
	menu->h=this->root.h;
	if (this->root.w > 320 || this->root.h > 320) {
		this->font_size=40;
		this->icon_s=48;
		this->icon_l=96;
		this->spacing=5;
		this->font=graphics_font_new(this->gra, 545, 1);
	} else {
		this->font_size=16;
		this->icon_xs=16;
		this->icon_s=32;
		this->icon_l=48;
		this->spacing=2;
		this->font=graphics_font_new(this->gra, 200, 1);
	}
	gui_internal_widget_append(&this->root, menu);
	gui_internal_clear(this);
	w=gui_internal_top_bar(this);
	gui_internal_widget_append(menu, w);
	w=gui_internal_box_new(this, gravity_center|orientation_horizontal_vertical|flags_expand|flags_fill);
	w->spx=4*this->spacing;
	gui_internal_widget_append(menu, w);

	return w;
}

static void
gui_internal_menu_render(struct gui_priv *this)
{
	GList *l;
	struct widget *menu;

	l=g_list_last(this->root.children);
	menu=l->data;
	gui_internal_widget_pack(this, menu);
	graphics_draw_mode(this->gra, draw_mode_begin);
	gui_internal_widget_render(this, menu);
	graphics_draw_mode(this->gra, draw_mode_end);
}

static void
gui_internal_cmd_set_destination(struct gui_priv *this, struct widget *wm)
{
	struct widget *w=wm->data;
	navit_set_destination(this->nav, &w->c, w->name);
	gui_internal_prune_menu(this, NULL);
}

static void
gui_internal_cmd_position(struct gui_priv *this, struct widget *wm)
{
	struct widget *wb,*w;
	wb=gui_internal_menu(this, wm->text);	
	w=gui_internal_box_new(this, gravity_top_center|orientation_vertical|flags_expand|flags_fill);
	gui_internal_widget_append(wb, w);
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Set as destination",
			image_new_xs(this, "gui_active"), gravity_left_center|orientation_horizontal|flags_fill,
			gui_internal_cmd_set_destination, wm));
#if 0
	gui_internal_widget_append(w,
		gui_internal_button_new(this, "Add to tour",
			image_new_o(this, "gui_active"), gravity_left_center|orientation_horizontal|flags_fill));
	gui_internal_widget_append(w,
		gui_internal_button_new(this, "Add as bookmark",
			image_new_o(this, "gui_active"), gravity_left_center|orientation_horizontal|flags_fill));
	gui_internal_widget_append(w,
		gui_internal_button_new(this, "View on map",
			image_new_o(this, "gui_active"), gravity_left_center|orientation_horizontal|flags_fill));
#endif
	gui_internal_menu_render(this);
}

static void
gui_internal_cmd_bookmarks(struct gui_priv *this, struct widget *wm)
{
	struct attr attr,mattr;
	struct map_rect *mr=NULL;
	struct item *item;
	char *label_full,*l,*prefix,*pos;
	int len,plen,hassub;
	struct widget *wb,*w,*wbm;
	GHashTable *hash;
	struct coord c;


	wb=gui_internal_menu(this, wm->text ? wm->text : "Bookmarks");
	w=gui_internal_box_new(this, gravity_top_center|orientation_vertical|flags_expand|flags_fill);
	w->spy=this->spacing*3;
	gui_internal_widget_append(wb, w);

	prefix=wm->prefix;
	if (! prefix)
		prefix="";
	plen=strlen(prefix);

	if(navit_get_attr(this->nav, attr_bookmark_map, &mattr, NULL) && mattr.u.map && (mr=map_rect_new(mattr.u.map, NULL))) {
		hash=g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
		while ((item=map_rect_get_item(mr))) {
			if (item->type != type_bookmark) continue;
			if (!item_attr_get(item, attr_label, &attr)) continue;
			label_full=attr.u.str;
			if (!strncmp(label_full, prefix, plen)) {
				pos=strchr(label_full+plen, '/');
				if (pos) {
					hassub=1;
					len=pos-label_full;
				} else {
					hassub=0;
					len=strlen(label_full);
				}
				l=g_malloc(len-plen+1);
				strncpy(l, label_full+plen, len-plen);
				l[len-plen]='\0';
				if (!g_hash_table_lookup(hash, l)) {
					wbm=gui_internal_button_new_with_callback(this, l,
						image_new_xs(this, hassub ? "gui_inactive" : "gui_active" ), gravity_left_center|orientation_horizontal|flags_fill,
							hassub ? gui_internal_cmd_bookmarks : gui_internal_cmd_position, NULL);
					if (item_coord_get(item, &c, 1)) {
						wbm->c.x=c.x;
						wbm->c.y=c.y;
						wbm->c.pro=map_projection(mattr.u.map);
						wbm->name=g_strdup_printf("Bookmark %s",label_full);
						wbm->text=g_strdup(l);
						gui_internal_widget_append(w, wbm);
						g_hash_table_insert(hash, g_strdup(l), (void *)1);
						wbm->prefix=g_malloc(plen+len+2);
						strncpy(wbm->prefix, label_full, plen+len+1);
						wbm->prefix[plen+len+1]='\0';
					} else {
						gui_internal_widget_destroy(this, wbm);
					}
				}
				g_free(l);
			}

		}
		g_hash_table_destroy(hash);
	}
	gui_internal_menu_render(this);
}

static void
gui_internal_cmd_layout(struct gui_priv *this, struct widget *wm)
{
	struct attr attr;
	struct widget *w,*wb,*wl;
	struct attr_iter *iter;


	wb=gui_internal_menu(this, "Layout");
	w=gui_internal_box_new(this, gravity_top_center|orientation_vertical|flags_expand|flags_fill);
	w->spy=this->spacing*3;
	gui_internal_widget_append(wb, w);
	iter=navit_attr_iter_new();
	while(navit_get_attr(this->nav, attr_layout, &attr, iter)) {
		wl=gui_internal_button_navit_attr_new(this, attr.u.layout->name, gravity_left_center|orientation_horizontal|flags_fill,
			&attr, NULL);
		gui_internal_widget_append(w, wl);
	}
	navit_attr_iter_destroy(iter);
	gui_internal_menu_render(this);
}

static void
gui_internal_cmd_fullscreen(struct gui_priv *this, struct widget *wm)
{
	this->fullscreen=!this->fullscreen;
	this->win->fullscreen(this->win, this->fullscreen);
}


static void
gui_internal_cmd_display(struct gui_priv *this, struct widget *wm)
{
	struct widget *w;

	w=gui_internal_menu(this, "Display");	
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Layout",
			image_new_l(this, "gui_display"), gravity_center|orientation_vertical,
			gui_internal_cmd_layout, NULL));
	if (this->fullscreen) {
		gui_internal_widget_append(w,
			gui_internal_button_new_with_callback(this, "Window Mode",
				image_new_l(this, "gui_leave_fullscreen"), gravity_center|orientation_vertical,
				gui_internal_cmd_fullscreen, NULL));
	} else {
		gui_internal_widget_append(w,
			gui_internal_button_new_with_callback(this, "Fullscreen",
				image_new_l(this, "gui_fullscreen"), gravity_center|orientation_vertical,
				gui_internal_cmd_fullscreen, NULL));
	}
	gui_internal_menu_render(this);
}

static void
gui_internal_cmd_quit(struct gui_priv *this, struct widget *wm)
{
	struct navit *nav=this->nav;
	navit_destroy(nav);
	main_remove_navit(nav);
}

static void
gui_internal_cmd_abort_navigation(struct gui_priv *this, struct widget *wm)
{
	navit_set_destination(this->nav, NULL, NULL);
}


static void
gui_internal_cmd_actions(struct gui_priv *this, struct widget *wm)
{
	struct widget *w;

	w=gui_internal_menu(this, "Actions");	
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Bookmarks",
			image_new_l(this, "gui_bookmark"), gravity_center|orientation_vertical,
			gui_internal_cmd_bookmarks, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "172°15'23\" E\n55°23'44\" S",
			image_new_l(this, "gui_map"), gravity_center|orientation_vertical,
			gui_internal_cmd_bookmarks, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "172°15'23\" E\n55°23'44\" S",
			image_new_l(this, "gui_rules"), gravity_center|orientation_vertical,
			gui_internal_cmd_bookmarks, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Town",
			image_new_l(this, "gui_rules"), gravity_center|orientation_vertical,
			gui_internal_cmd_bookmarks, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Street",
			image_new_l(this, "gui_rules"), gravity_center|orientation_vertical,
			gui_internal_cmd_bookmarks, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Quit",
			image_new_l(this, "gui_quit"), gravity_center|orientation_vertical,
			gui_internal_cmd_quit, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Abort\nNavigation",
			image_new_l(this, "gui_stop"), gravity_center|orientation_vertical,
			gui_internal_cmd_abort_navigation, NULL));
	gui_internal_menu_render(this);
}

static void
gui_internal_cmd_maps(struct gui_priv *this, struct widget *wm)
{
	struct attr attr;
	struct widget *w,*wb,*wma;
	char *label;
	struct attr_iter *iter;
	struct attr on, off;


	wb=gui_internal_menu(this, "Maps");
	w=gui_internal_box_new(this, gravity_top_center|orientation_vertical|flags_expand|flags_fill);
	w->spy=this->spacing*3;
	gui_internal_widget_append(wb, w);
	iter=navit_attr_iter_new();
	on.type=off.type=attr_active;
	on.u.num=1;
	off.u.num=0;
	while(navit_get_attr(this->nav, attr_map, &attr, iter)) {
		label=g_strdup_printf("%s:%s", map_get_type(attr.u.map), map_get_filename(attr.u.map));
		wma=gui_internal_button_map_attr_new(this, label, gravity_left_center|orientation_horizontal|flags_fill,
			attr.u.map, &on, &off);
		gui_internal_widget_append(w, wma);
		g_free(label);
	}
	navit_attr_iter_destroy(iter);
	gui_internal_menu_render(this);
	
}

static void
gui_internal_cmd_vehicle(struct gui_priv *this, struct widget *wm)
{
	struct attr attr,vattr;
	struct widget *w,*wb,*wl;
	struct attr_iter *iter;


	wb=gui_internal_menu(this, "Vehicle");
	w=gui_internal_box_new(this, gravity_top_center|orientation_vertical|flags_expand|flags_fill);
	w->spy=this->spacing*3;
	gui_internal_widget_append(wb, w);
	iter=navit_attr_iter_new();
	while(navit_get_attr(this->nav, attr_vehicle, &attr, iter)) {
		vehicle_get_attr(attr.u.vehicle, attr_name, &vattr);
		wl=gui_internal_button_navit_attr_new(this, vattr.u.str, gravity_left_center|orientation_horizontal|flags_fill,
			&attr, NULL);
		gui_internal_widget_append(w, wl);
	}
	navit_attr_iter_destroy(iter);
	gui_internal_menu_render(this);
}


static void
gui_internal_cmd_rules(struct gui_priv *this, struct widget *wm)
{
	struct widget *wb,*w;
	struct attr on,off;
	wb=gui_internal_menu(this, "Rules");	
	w=gui_internal_box_new(this, gravity_top_center|orientation_vertical|flags_expand|flags_fill);
	w->spy=this->spacing*3;
	gui_internal_widget_append(wb, w);
	on.u.num=1;
	off.u.num=0;
	on.type=off.type=attr_tracking;
	gui_internal_widget_append(w,
		gui_internal_button_navit_attr_new(this, "Stick to roads", gravity_left_center|orientation_horizontal|flags_fill,
			&on, &off));
	on.type=off.type=attr_orientation;
	gui_internal_widget_append(w,
		gui_internal_button_navit_attr_new(this, "Keep orientation to the North", gravity_left_center|orientation_horizontal|flags_fill,
			&on, &off));
	on.type=off.type=attr_cursor;
	gui_internal_widget_append(w,
		gui_internal_button_navit_attr_new(this, "Map follows Vehicle", gravity_left_center|orientation_horizontal|flags_fill,
			&on, &off));
	gui_internal_widget_append(w,
		gui_internal_button_new(this, "Attack defenseless civilians",
			image_new_xs(this, "gui_active"), gravity_left_center|orientation_horizontal|flags_fill));
	gui_internal_menu_render(this);
}

static void
gui_internal_cmd_settings(struct gui_priv *this, struct widget *wm)
{
	struct widget *w;

	w=gui_internal_menu(this, "Settings");	
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Display",
			image_new_l(this, "gui_display"), gravity_center|orientation_vertical,
			gui_internal_cmd_display, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Maps",
			image_new_l(this, "gui_maps"), gravity_center|orientation_vertical,
			gui_internal_cmd_maps, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Vehicle",
			image_new_l(this, "gui_sound"), gravity_center|orientation_vertical,
			gui_internal_cmd_vehicle, NULL));
	gui_internal_widget_append(w,
		gui_internal_button_new_with_callback(this, "Rules",
			image_new_l(this, "gui_rules"), gravity_center|orientation_vertical,
			gui_internal_cmd_rules, NULL));
	gui_internal_menu_render(this);
}

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static void gui_internal_motion(void *data, struct point *p)
{
	struct gui_priv *this=data;
	if (!this->root.children) {
		navit_handle_motion(this->nav, p);
		return;
	}
	if (!this->pressed)
		return;
	graphics_draw_mode(this->gra, draw_mode_begin);
	gui_internal_highlight(this, p);
	graphics_draw_mode(this->gra, draw_mode_end);
	
}


static void gui_internal_menu_root(struct gui_priv *this)
{
	struct widget *w;

	w=gui_internal_menu(this, "Main menu");	
	w->spx=this->spacing*10;
	gui_internal_widget_append(w, gui_internal_button_new_with_callback(this, "Actions",
			image_new_l(this, "gui_actions"), gravity_center|orientation_vertical,
			gui_internal_cmd_actions, NULL));
	gui_internal_widget_append(w, gui_internal_button_new_with_callback(this, "Settings",
			image_new_l(this, "gui_settings"), gravity_center|orientation_vertical,
			gui_internal_cmd_settings, NULL));
	gui_internal_widget_append(w, gui_internal_button_new(this, "Tools",
			image_new_l(this, "gui_tools"), gravity_center|orientation_vertical));
	gui_internal_menu_render(this);
}


//##############################################################################################################
//# Description: Function to handle mouse clicks and scroll wheel movement
//# Comment: 
//# Authors: Martin Schaller (04/2008), Stefan Klumpp (04/2008)
//##############################################################################################################
static void gui_internal_button(void *data, int pressed, int button, struct point *p)
{
	struct gui_priv *this=data;
	struct graphics *gra=this->gra;
	
	// if still on the map (not in the menu, yet):
	if (!this->root.children) {	
		// check whether the position of the mouse changed during press/release OR if it is the scrollwheel 
		if (!navit_handle_button(this->nav, pressed, button, p, NULL) || button >=4) // Maybe there's a better way to do this
			return;
		navit_block(this->nav, 1);
		graphics_overlay_disable(gra, 1);
		// draw menu
		this->root.p.x=0;
		this->root.p.y=0;
		this->root.background=this->background;
		gui_internal_menu_root(this);
		return;
	}
	
	// if already in the menu:
	if (pressed) {
		this->pressed=1;
		graphics_draw_mode(gra, draw_mode_begin);
		gui_internal_highlight(this, p);
		graphics_draw_mode(gra, draw_mode_end);
	} else {
		this->pressed=0;
		graphics_draw_mode(gra, draw_mode_begin);
		gui_internal_call_highlighted(this);
		gui_internal_highlight(this, NULL);
		graphics_draw_mode(gra, draw_mode_end);
		if (! this->root.children) {
			graphics_overlay_disable(gra, 0);
			if (!navit_block(this->nav, 0)) {
				if (this->redraw)
					navit_draw(this->nav);
				else
					navit_draw_displaylist(this->nav);
			}
		}
	}
}

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static void gui_internal_resize(void *data, int w, int h)
{
	struct gui_priv *this=data;
	this->root.w=w;
	this->root.h=h;
	dbg(0,"w=%d h=%d\n", w, h);
	if (!this->root.children) {
		navit_resize(this->nav, w, h);
		return;
	}
	gui_internal_prune_menu(this, NULL);
	gui_internal_menu_root(this);
} 

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static void gui_internal_keypress(void *data, int key)
{
	struct gui_priv *this=data;
	int w,h;
	struct point p;
	transform_get_size(navit_get_trans(this->nav), &w, &h);
	switch (key) {
	case NAVIT_KEY_UP:
		p.x=w/2;
                p.y=0;
                navit_set_center_screen(this->nav, &p);
                break;
	case NAVIT_KEY_DOWN:
                p.x=w/2;
                p.y=h;
                navit_set_center_screen(this->nav, &p);
                break;
        case NAVIT_KEY_LEFT:
                p.x=0;
                p.y=h/2;
                navit_set_center_screen(this->nav, &p);
                break;
        case NAVIT_KEY_RIGHT:
                p.x=w;
                p.y=h/2;
                navit_set_center_screen(this->nav, &p);
                break;
        case NAVIT_KEY_ZOOM_IN:
                navit_zoom_in(this->nav, 2, NULL);
                break;
        case NAVIT_KEY_ZOOM_OUT:
                navit_zoom_out(this->nav, 2, NULL);
                break;
	default:
		dbg(0,"key=%d\n", key);
	}
} 


//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static int gui_internal_set_graphics(struct gui_priv *this, struct graphics *gra)
{
	struct window *win;
#if 0
	struct color cb={0x7fff,0x7fff,0x7fff,0xffff};
#endif
	struct color cb={0x0,0x0,0x0,0xffff};
	struct color cb2={0x4141,0x4141,0x4141,0xffff};
	struct color cbh={0x9fff,0x9fff,0x9fff,0xffff};
	struct color cf={0xbfff,0xbfff,0xbfff,0xffff};
	struct transformation *trans=navit_get_trans(this->nav);
	
	win=graphics_get_data(gra, "window");
        if (! win)
                return 1;
	this->gra=gra;
	this->win=win;
	transform_get_size(trans, &this->root.w, &this->root.h);
	graphics_register_resize_callback(gra, gui_internal_resize, this);
	graphics_register_button_callback(gra, gui_internal_button, this);
	graphics_register_motion_callback(gra, gui_internal_motion, this);
	graphics_register_keypress_callback(gra, gui_internal_keypress, this);
	this->background=graphics_gc_new(gra);
	graphics_gc_set_foreground(this->background, &cb);
	this->background2=graphics_gc_new(gra);
	graphics_gc_set_foreground(this->background2, &cb2);
	this->highlight_background=graphics_gc_new(gra);
	graphics_gc_set_foreground(this->highlight_background, &cbh);
	this->foreground=graphics_gc_new(gra);
	graphics_gc_set_foreground(this->foreground, &cf);
	return 0;
}

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
struct gui_methods gui_internal_methods = {
	NULL,
	NULL,
        gui_internal_set_graphics,
};

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
static struct gui_priv * gui_internal_new(struct navit *nav, struct gui_methods *meth, struct attr **attrs) 
{
	struct gui_priv *this;
	*meth=gui_internal_methods;
	this=g_new0(struct gui_priv, 1);
	this->nav=nav;

	return this;
}

//##############################################################################################################
//# Description: 
//# Comment: 
//# Authors: Martin Schaller (04/2008)
//##############################################################################################################
void plugin_init(void)
{
	plugin_register_gui_type("internal", gui_internal_new);
}
