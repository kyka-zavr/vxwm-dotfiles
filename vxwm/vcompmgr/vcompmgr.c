/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include <sys/un.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <pthread.h>
#include <signal.h>

#if !HAVE_REALLOCARRAY
#include <stdint.h>
static inline void *
xreallocarray(void *old, size_t num, size_t size)
{
	if (size && num > SIZE_MAX / size) return NULL;
	return realloc(old, num * size);
}
#define reallocarray xreallocarray
#endif

#if COMPOSITE_MAJOR > 0 || COMPOSITE_MINOR >= 2
#define HAS_NAME_WINDOW_PIXMAP 1
#endif

typedef struct _ignore {
	struct _ignore *next;
	unsigned long sequence;
} ignore;

typedef struct _win {
	struct _win *next;
	Window id;
#if HAS_NAME_WINDOW_PIXMAP
	Pixmap pixmap;
#endif
	XWindowAttributes a;
	int mode;
	int damaged;
	Damage damage;
	Picture picture;
	Picture alphaPict;
	Picture shadowPict;
	XserverRegion borderSize;
	XserverRegion extents;
	Picture shadow;
	int shadow_dx;
	int shadow_dy;
	int shadow_width;
	int shadow_height;
	unsigned int opacity;
	Atom windowType;
	unsigned long damage_sequence;
	Bool shaped;
	Bool shadow_excluded;
	Bool zoom_excluded;
	char *wm_name;
	char *wm_class;
	XRectangle shape_bounds;
	XserverRegion borderClip;
	struct _win *prev_trans;
} win;

typedef struct _conv {
	int size;
	double *data;
} conv;

typedef struct _fade {
	struct _fade *next;
	win *w;
	double cur;
	double finish;
	double step;
	void (*callback)(Display *dpy, win *w, Bool gone);
	Bool gone;
} fade;

static win *list;
static fade *fades;
static int scr;
static Window root;
static Picture rootPicture;
static Picture rootBuffer;
static Picture transBlackPicture;

static Picture shadowColorPicture;

static Picture rootTile;
static XserverRegion allDamage;
static Bool clipChanged;
#if HAS_NAME_WINDOW_PIXMAP
static Bool hasNamePixmap;
#endif
static int root_height, root_width;
static ignore *ignore_head, **ignore_tail = &ignore_head;
static int xfixes_event, xfixes_error;
static int damage_event, damage_error;
static int composite_event, composite_error;
static int render_event, render_error;
static int xshape_event, xshape_error;
static Bool synchronize;
static int composite_opcode;

static Atom opacityAtom;
static Atom winTypeAtom;
static Atom winDesktopAtom;
static Atom winNormalAtom;

#define OPACITY_PROP "_NET_WM_WINDOW_OPACITY"

#define OPAQUE      0xffffffff

static conv *gaussianMap;

#define WINDOW_SOLID 0
#define WINDOW_TRANS 1
#define WINDOW_ARGB  2

typedef enum _compMode {
	CompSimple,
	CompServerShadows,
	CompClientShadows,
} CompMode;

static void determine_mode(Display *dpy, win *w);
static double get_opacity_percent(Display *dpy, win *w, double def);
static XserverRegion win_extents(Display *dpy, win *w);
static void invalidate_shadow(Display *dpy, win *w);

static void paint_all(Display *dpy, XserverRegion region);
static void zoom_present(Display *dpy);
static void zoom_wait_vblank(void);
static void *zoom_anim_thread(void *arg);
static void zoom_cleanup(void);
static void zoom_signal_handler(int sig);
static Bool zoom_is_fullscreen(win *w);

#include "config.h"

static const char *shadow_exclude_types[]   = SHADOW_EXCLUDE_TYPES;
static const char *shadow_exclude_classes[] = SHADOW_EXCLUDE_CLASSES;
#define N_SHADOW_EXCLUDE_TYPES   (sizeof(shadow_exclude_types)   / sizeof(char *))
#define N_SHADOW_EXCLUDE_CLASSES (sizeof(shadow_exclude_classes) / sizeof(char *))
static Atom shadow_exclude_type_atoms[N_SHADOW_EXCLUDE_TYPES];

static const char *zoom_exclude_types[]   = ZOOM_EXCLUDE_TYPES;
static const char *zoom_exclude_classes[] = ZOOM_EXCLUDE_CLASSES;
#define N_ZOOM_EXCLUDE_TYPES   (sizeof(zoom_exclude_types)   / sizeof(char *))
#define N_ZOOM_EXCLUDE_CLASSES (sizeof(zoom_exclude_classes) / sizeof(char *))
static Atom zoom_exclude_type_atoms[N_ZOOM_EXCLUDE_TYPES];

#if SHADOWS == 2
static CompMode compMode = CompServerShadows;
#elif SHADOWS == 1
static CompMode compMode = CompClientShadows;
#else
static CompMode compMode = CompSimple;
#endif

static int shadowRadius = SHADOW_RADIUS;
static int shadowOffsetX = SHADOW_OFFSET_X;
static int shadowOffsetY = SHADOW_OFFSET_Y;
static double shadowOpacity = SHADOW_OPACITY;

static double fade_in_step = FADE_IN_STEP;
static double fade_out_step = FADE_OUT_STEP;
static int fade_delta = FADE_DELTA;
static int fade_time = 0;
static Bool fadeWindows = FADE_WINDOWS;
static Bool fadeTrans = FADE_TRANS;

static int Gsize = -1;
static unsigned char *shadowCorner = NULL;
static unsigned char *shadowTop = NULL;

static double zoom_factor = 1.0;
static double zoom_target = 1.0;
static double zoom_start = 1.0;
static double zoom_duration = ZOOM_DURATION;
static double zoom_bezier[4] = {ZOOM_BEZIER};
static Bool zoom_animating = False;
static struct timespec zoom_anim_start;
static double zoom_min = ZOOM_MIN;
static double zoom_max = ZOOM_MAX;
static Bool zoom_flag = False;
static Bool zoom_get_flag = False;
static char zoom_opt_str[32] = "1.0";
static int zoom_ipc_fd = -1;
static char zoom_socket_path[108];
static int zoom_drm_fd = -1;
static long zoom_vblank_ns = 16666667;
static pthread_mutex_t zoom_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t zoom_cond = PTHREAD_COND_INITIALIZER;
static int zoom_pipe[2] = {-1, -1};

static Atom determine_wintype(Display *dpy, Window w);

static int
get_time_in_milliseconds(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static fade *
find_fade(const win *w)
{
	for (fade *f = fades; f; f = f->next)
		if (f->w == w)
			return f;
	return NULL;
}

static void
dequeue_fade(Display *dpy, fade *f)
{
	for (fade **prev = &fades; *prev; prev = &(*prev)->next) {
		if (*prev == f) {
			*prev = f->next;
			if (f->callback)
				(*f->callback)(dpy, f->w, f->gone);
			free(f);
			break;
		}
	}
}

static void
cleanup_fade(Display *dpy, win *w)
{
	fade *f = find_fade(w);
	if (f)
		dequeue_fade(dpy, f);
}

static void
enqueue_fade(Display *dpy, fade *f)
{
	if (!fades)
		fade_time = get_time_in_milliseconds() + fade_delta;
	f->next = fades;
	fades = f;
}

static void
set_fade(Display *dpy, win *w, double start, double finish, double step,
         void (*callback)(Display *dpy, win *w, Bool gone),
         Bool gone, Bool exec_callback, Bool override)
{
	fade *f = find_fade(w);
	if (!f) {
		f = malloc(sizeof(fade));
		if (!f) return;
		*f = (fade){.next = NULL, .w = w, .cur = start};
		enqueue_fade(dpy, f);
	} else if (!override) {
		return;
	} else {
		if (exec_callback && f->callback)
			(*f->callback)(dpy, f->w, f->gone);
	}

	if (finish < 0) finish = 0;
	if (finish > 1) finish = 1;
	f->finish = finish;
	f->step = (f->cur < finish) ? step : -step;
	f->callback = callback;
	f->gone = gone;
	w->opacity = f->cur * OPAQUE;
	determine_mode(dpy, w);
	if (w->shadow)
		invalidate_shadow(dpy, w);
}

static int
fade_timeout(void)
{
	if (!fades)
		return -1;
	int delta = fade_time - get_time_in_milliseconds();
	return delta < 0 ? 0 : delta;
}

static void
run_fades(Display *dpy)
{
	int now = get_time_in_milliseconds();
	if (fade_time - now > 0)
		return;
	int steps = 1 + (now - fade_time) / fade_delta;
	fade *next = fades;
	while (next) {
		fade *f = next;
		win *w = f->w;
		next = f->next;
		f->cur += f->step * steps;
		if (f->cur >= 1) f->cur = 1;
		else if (f->cur < 0) f->cur = 0;
		w->opacity = f->cur * OPAQUE;
		Bool need_dequeue = (f->step > 0) ? (f->cur >= f->finish) : (f->cur <= f->finish);
		if (need_dequeue)
			w->opacity = f->finish * OPAQUE;
		determine_mode(dpy, w);
		invalidate_shadow(dpy, w);
		if (need_dequeue)
			dequeue_fade(dpy, f);
	}
	fade_time = now + fade_delta;
}

static double
gaussian(double r, double x, double y)
{
	return (1 / (sqrt(2 * M_PI * r))) * exp((-(x * x + y * y)) / (2 * r * r));
}

static conv *
make_gaussian_map(double r)
{
	int size = ((int)ceil(r * 3) + 1) & ~1;
	int center = size / 2;
	conv *c = malloc(sizeof(conv) + size * size * sizeof(double));
	if (!c) return NULL;
	*c = (conv){.size = size, .data = (double *)(c + 1)};
	double t = 0.0;
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++) {
			double g = gaussian(r, (double)(x - center), (double)(y - center));
			t += g;
			c->data[y * size + x] = g;
		}
	for (int y = 0; y < size; y++)
		for (int x = 0; x < size; x++)
			c->data[y * size + x] /= t;
	return c;
}

static unsigned char
sum_gaussian(conv *map, double opacity, int x, int y, int width, int height)
{
	int g_size = map->size;
	int center = g_size / 2;
	int fx_start = center - x; if (fx_start < 0) fx_start = 0;
	int fx_end   = width + center - x; if (fx_end > g_size) fx_end = g_size;
	int fy_start = center - y; if (fy_start < 0) fy_start = 0;
	int fy_end   = height + center - y; if (fy_end > g_size) fy_end = g_size;
	double *g_line = map->data + fy_start * g_size + fx_start;
	double v = 0;
	for (int fy = fy_start; fy < fy_end; fy++) {
		double *g_data = g_line;
		g_line += g_size;
		for (int fx = fx_start; fx < fx_end; fx++)
			v += *g_data++;
	}
	if (v > 1) v = 1;
	return (unsigned char)(v * opacity * 255.0);
}

static void
presum_gaussian(conv *map)
{
	int center = map->size / 2;
	Gsize = map->size;
	free(shadowCorner);
	free(shadowTop);
	shadowCorner = malloc((Gsize + 1) * (Gsize + 1) * 26);
	shadowTop    = malloc((Gsize + 1) * 26);
	if (!shadowCorner || !shadowTop) {
		fprintf(stderr, "vcompmgr: out of memory in presum_gaussian\n");
		exit(1);
	}
	for (int x = 0; x <= Gsize; x++) {
		shadowTop[25 * (Gsize + 1) + x] =
		    sum_gaussian(map, 1, x - center, center, Gsize * 2, Gsize * 2);
		for (int opacity = 0; opacity < 25; opacity++)
			shadowTop[opacity * (Gsize + 1) + x] =
			    shadowTop[25 * (Gsize + 1) + x] * opacity / 25;
		for (int y = 0; y <= x; y++) {
			shadowCorner[25 * (Gsize + 1) * (Gsize + 1) + y * (Gsize + 1) + x] =
			shadowCorner[25 * (Gsize + 1) * (Gsize + 1) + x * (Gsize + 1) + y] =
			    sum_gaussian(map, 1, x - center, y - center, Gsize * 2, Gsize * 2);
			for (int opacity = 0; opacity < 25; opacity++)
				shadowCorner[opacity * (Gsize + 1) * (Gsize + 1) + y * (Gsize + 1) + x] =
				shadowCorner[opacity * (Gsize + 1) * (Gsize + 1) + x * (Gsize + 1) + y] =
				    shadowCorner[25 * (Gsize + 1) * (Gsize + 1) + y * (Gsize + 1) + x] * opacity / 25;
		}
	}
}

static XImage *
make_shadow(Display *dpy, double opacity, int width, int height)
{
	int gsize = gaussianMap->size;
	int swidth  = width + gsize;
	int sheight = height + gsize;
	int center  = gsize / 2;
	int opacity_int = (int)(opacity * 25);
	unsigned char *data = malloc(swidth * sheight * sizeof(unsigned char));
	if (!data)
		return NULL;
	XImage *ximage = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)),
	                               8, ZPixmap, 0, (char *)data,
	                               swidth, sheight, 8, swidth * sizeof(unsigned char));
	if (!ximage) {
		free(data);
		return NULL;
	}
	unsigned char d = (Gsize > 0)
	    ? shadowTop[opacity_int * (Gsize + 1) + Gsize]
	    : sum_gaussian(gaussianMap, opacity, center, center, width, height);
	memset(data, d, sheight * swidth);

	int ylimit = (gsize < (sheight + 1) / 2) ? gsize : (sheight + 1) / 2;
	int xlimit = (gsize < (swidth  + 1) / 2) ? gsize : (swidth  + 1) / 2;

	for (int y = 0; y < ylimit; y++) {
		for (int x = 0; x < xlimit; x++) {
			d = (xlimit == Gsize && ylimit == Gsize)
			    ? shadowCorner[opacity_int * (Gsize + 1) * (Gsize + 1) + y * (Gsize + 1) + x]
			    : sum_gaussian(gaussianMap, opacity, x - center, y - center, width, height);
			data[y * swidth + x] = d;
			data[(sheight - y - 1) * swidth + x] = d;
			data[(sheight - y - 1) * swidth + (swidth - x - 1)] = d;
			data[y * swidth + (swidth - x - 1)] = d;
		}
	}

	int x_diff = swidth - (gsize * 2);
	if (x_diff > 0 && ylimit > 0) {
		for (int y = 0; y < ylimit; y++) {
			d = (ylimit == Gsize)
			    ? shadowTop[opacity_int * (Gsize + 1) + y]
			    : sum_gaussian(gaussianMap, opacity, center, y - center, width, height);
			memset(&data[y * swidth + gsize], d, x_diff);
			memset(&data[(sheight - y - 1) * swidth + gsize], d, x_diff);
		}
	}

	for (int x = 0; x < xlimit; x++) {
		d = (xlimit == Gsize)
		    ? shadowTop[opacity_int * (Gsize + 1) + x]
		    : sum_gaussian(gaussianMap, opacity, x - center, center, width, height);
		for (int y = gsize; y < sheight - gsize; y++) {
			data[y * swidth + x] = d;
			data[y * swidth + (swidth - x - 1)] = d;
		}
	}
	return ximage;
}

typedef struct _shadow_cache {
	struct _shadow_cache *next;
	int width, height, opacity_int;
	Picture picture;
	int shadow_width, shadow_height;
	int refcount;
} shadow_cache;

static shadow_cache *shadow_cache_list;

static void
shadow_cache_unref(Display *dpy, Picture picture)
{
	for (shadow_cache **prev = &shadow_cache_list; *prev; prev = &(*prev)->next) {
		shadow_cache *e = *prev;
		if (e->picture != picture) continue;
		if (--e->refcount == 0) {
			XRenderFreePicture(dpy, e->picture);
			*prev = e->next;
			free(e);
		}
		return;
	}
	fprintf(stderr, "vcompmgr: BUG: shadow_cache_unref called with unknown picture\n");
}

static Picture
shadow_picture(Display *dpy, double opacity, int width, int height, int *wp, int *hp)
{
	int opacity_int = (int)(opacity * 25);
	for (shadow_cache *e = shadow_cache_list; e; e = e->next)
		if (e->width == width && e->height == height && e->opacity_int == opacity_int) {
			*wp = e->shadow_width;
			*hp = e->shadow_height;
			e->refcount++;
			return e->picture;
		}
	XImage *shadowImage = make_shadow(dpy, opacity, width, height);
	if (!shadowImage)
		return None;
	Pixmap shadowPixmap = XCreatePixmap(dpy, root,
	                                    shadowImage->width, shadowImage->height, 8);
	if (!shadowPixmap) {
		XDestroyImage(shadowImage);
		return None;
	}
	Picture shadowPicture = XRenderCreatePicture(dpy, shadowPixmap,
	                                              XRenderFindStandardFormat(dpy, PictStandardA8),
	                                              0, NULL);
	if (!shadowPicture) {
		XDestroyImage(shadowImage);
		XFreePixmap(dpy, shadowPixmap);
		return None;
	}
	GC gc = XCreateGC(dpy, shadowPixmap, 0, NULL);
	if (!gc) {
		XDestroyImage(shadowImage);
		XFreePixmap(dpy, shadowPixmap);
		XRenderFreePicture(dpy, shadowPicture);
		return None;
	}
	XPutImage(dpy, shadowPixmap, gc, shadowImage, 0, 0, 0, 0,
	          shadowImage->width, shadowImage->height);
	*wp = shadowImage->width;
	*hp = shadowImage->height;
	XFreeGC(dpy, gc);
	XDestroyImage(shadowImage);
	XFreePixmap(dpy, shadowPixmap);
	shadow_cache *e = malloc(sizeof(shadow_cache));
	if (e) {
		*e = (shadow_cache){
		    .next = shadow_cache_list, .width = width, .height = height,
		    .opacity_int = opacity_int, .picture = shadowPicture,
		    .shadow_width = *wp, .shadow_height = *hp, .refcount = 1,
		};
		shadow_cache_list = e;
	}
	return shadowPicture;
}

static Picture
solid_picture(Display *dpy, Bool argb, double a, double r, double g, double b)
{
	Pixmap pixmap = XCreatePixmap(dpy, root, 1, 1, argb ? 32 : 8);
	if (!pixmap)
		return None;
	XRenderPictureAttributes pa = {.repeat = True};
	Picture picture = XRenderCreatePicture(dpy, pixmap,
	                                       XRenderFindStandardFormat(dpy, argb ? PictStandardARGB32 : PictStandardA8),
	                                       CPRepeat, &pa);
	if (!picture) {
		XFreePixmap(dpy, pixmap);
		return None;
	}
	XRenderColor c = {
	    .alpha = a * 0xffff,
	    .red   = r * 0xffff,
	    .green = g * 0xffff,
	    .blue  = b * 0xffff};
	XRenderFillRectangle(dpy, PictOpSrc, picture, &c, 0, 0, 1, 1);
	XFreePixmap(dpy, pixmap);
	return picture;
}

static void
discard_ignore(Display *dpy __attribute__((unused)), unsigned long sequence)
{
	while (ignore_head) {
		if ((long)(sequence - ignore_head->sequence) > 0) {
			ignore *next = ignore_head->next;
			free(ignore_head);
			ignore_head = next;
			if (!ignore_head)
				ignore_tail = &ignore_head;
		} else
			break;
	}
}

static void
set_ignore(Display *dpy __attribute__((unused)), unsigned long sequence)
{
	ignore *i = malloc(sizeof(ignore));
	if (!i)
		return;
	*i = (ignore){.sequence = sequence, .next = NULL};
	*ignore_tail = i;
	ignore_tail = &i->next;
}

static int
should_ignore(Display *dpy, unsigned long sequence)
{
	discard_ignore(dpy, sequence);
	return ignore_head && ignore_head->sequence == sequence;
}

static win *
find_win(Display *dpy __attribute__((unused)), Window id)
{
	for (win *w = list; w; w = w->next)
		if (w->id == id)
			return w;
	return NULL;
}

static Atom backgroundProps[3];
static Atom pixmapAtom;

static Picture
root_tile(Display *dpy)
{
	Atom actual_type;
	Pixmap pixmap = None;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop;
	Bool fill = False;
	XRenderPictureAttributes pa;

	for (int p = 0; backgroundProps[p]; p++) {
		if (XGetWindowProperty(dpy, root, backgroundProps[p],
		                       0, 4, False, AnyPropertyType,
		                       &actual_type, &actual_format,
		                       &nitems, &bytes_after, &prop) == Success &&
		    actual_type == pixmapAtom &&
		    actual_format == 32 && nitems == 1) {
			memcpy(&pixmap, prop, 4);
			XFree(prop);
			break;
		}
	}
	if (!pixmap) {
		pixmap = XCreatePixmap(dpy, root, 1, 1, DefaultDepth(dpy, scr));
		fill = True;
	}
	pa.repeat = True;
	Picture picture = XRenderCreatePicture(dpy, pixmap,
	                                       XRenderFindVisualFormat(dpy, DefaultVisual(dpy, scr)),
	                                       CPRepeat, &pa);
	if (fill) {
		XRenderColor c = {
		    .red = 0x8080, .green = 0x8080, .blue = 0x8080, .alpha = 0xffff};
		XRenderFillRectangle(dpy, PictOpSrc, picture, &c, 0, 0, 1, 1);
	}
	return picture;
}

static void
win_geom(win *w, int *x, int *y, int *wid, int *hei)
{
#if HAS_NAME_WINDOW_PIXMAP
	*x   = w->a.x;
	*y   = w->a.y;
	*wid = w->a.width  + w->a.border_width * 2;
	*hei = w->a.height + w->a.border_width * 2;
#else
	*x   = w->a.x + w->a.border_width;
	*y   = w->a.y + w->a.border_width;
	*wid = w->a.width;
	*hei = w->a.height;
#endif
}

static void add_damage(Display *dpy, XserverRegion damage);
static void zoom_paint_panels(Display *dpy);

static double
zoom_bezier_x(double t)
{
	return 3.0 * t * (1 - t) * (1 - t) * zoom_bezier[0] +
	       3.0 * t * t * (1 - t) * zoom_bezier[2] +
	       t * t * t;
}

static double
zoom_bezier_y(double t)
{
	return 3.0 * t * (1 - t) * (1 - t) * zoom_bezier[1] +
	       3.0 * t * t * (1 - t) * zoom_bezier[3] +
	       t * t * t;
}

static double
zoom_bezier_dx(double t)
{
	return 3.0 * (1 - t) * (1 - t) * zoom_bezier[0] +
	       6.0 * t * (1 - t) * (zoom_bezier[2] - zoom_bezier[0]) +
	       3.0 * t * t * (1.0 - zoom_bezier[2]);
}

static double
zoom_ease(double tp)
{
	if (tp <= 0.0) return 0.0;
	if (tp >= 1.0) return 1.0;
	double t = tp;
	for (int i = 0; i < 8; i++) {
		double dx = zoom_bezier_dx(t);
		if (dx < 1e-7) break;
		t -= (zoom_bezier_x(t) - tp) / dx;
		if      (t < 0.0) t = 0.0;
		else if (t > 1.0) t = 1.0;
	}
	return zoom_bezier_y(t);
}

static void
zoom_write_state(double target)
{
	char path[140];
	snprintf(path, sizeof(path), "%s.zoom", zoom_socket_path);
	FILE *f = fopen(path, "w");
	if (f) {
		fprintf(f, "%g\n", target);
		fclose(f);
	}
}

static void
zoom_start_animation(double target)
{
	pthread_mutex_lock(&zoom_mutex);
	if (fabs(target - zoom_factor) < 1e-9) {
		pthread_mutex_unlock(&zoom_mutex);
		return;
	}
	zoom_start = zoom_factor;
	zoom_target = target;
	zoom_animating = True;
	clock_gettime(CLOCK_MONOTONIC, &zoom_anim_start);
	pthread_cond_signal(&zoom_cond);
	pthread_mutex_unlock(&zoom_mutex);
	zoom_write_state(target);
}

static void *
zoom_anim_thread(void *arg __attribute__((unused)))
{
	pthread_mutex_lock(&zoom_mutex);
	while (1) {
		while (!zoom_animating && zoom_factor == 1.0)
			pthread_cond_wait(&zoom_cond, &zoom_mutex);
		pthread_mutex_unlock(&zoom_mutex);
		zoom_wait_vblank();
		pthread_mutex_lock(&zoom_mutex);
		if (zoom_animating) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			double elapsed = (now.tv_sec - zoom_anim_start.tv_sec) * 1000.0 +
			                 (now.tv_nsec - zoom_anim_start.tv_nsec) / 1e6;
			double tp = (zoom_duration > 0.0) ? elapsed / zoom_duration : 1.0;
			if (tp >= 1.0) {
				zoom_factor = zoom_target;
				zoom_animating = False;
			} else {
				zoom_factor = zoom_start + (zoom_target - zoom_start) * zoom_ease(tp);
			}
		}
		pthread_mutex_unlock(&zoom_mutex);
		if (zoom_pipe[1] >= 0) {
			char b = 1;
			write(zoom_pipe[1], &b, 1);
		}
		pthread_mutex_lock(&zoom_mutex);
	}
	return NULL;
}

static void
zoom_present(Display *dpy)
{
	if (!rootBuffer)
		return;
	XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, None);

	pthread_mutex_lock(&zoom_mutex);
	double zf = zoom_factor;
	pthread_mutex_unlock(&zoom_mutex);

	if (zf == 1.0) {
		XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, None);
		XRenderComposite(dpy, PictOpSrc, rootBuffer, None, rootPicture,
		                 0, 0, 0, 0, 0, 0, root_width, root_height);
		return;
	}

	double iz = 1.0 / zf;
	double cx = root_width  / 2.0;
	double cy = root_height / 2.0;

	XTransform scale = {{
	    {XDoubleToFixed(iz),  XDoubleToFixed(0.0), XDoubleToFixed(0.0)},
	    {XDoubleToFixed(0.0), XDoubleToFixed(iz),  XDoubleToFixed(0.0)},
	    {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)},
	}};
	XTransform identity = {{
	    {XDoubleToFixed(1.0), XDoubleToFixed(0.0), XDoubleToFixed(0.0)},
	    {XDoubleToFixed(0.0), XDoubleToFixed(1.0), XDoubleToFixed(0.0)},
	    {XDoubleToFixed(0.0), XDoubleToFixed(0.0), XDoubleToFixed(1.0)},
	}};

	if (!rootTile)
		rootTile = root_tile(dpy);
	XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, None);
	XRenderComposite(dpy, PictOpSrc, rootTile, None, rootPicture,
	                 0, 0, 0, 0, 0, 0, root_width, root_height);

	int n = 0;
	for (win *ww = list; ww; ww = ww->next) n++;
	win **wins = malloc((size_t)n * sizeof(win *));
	if (!wins) return;
	n = 0;
	for (win *ww = list; ww; ww = ww->next) wins[n++] = ww;

	for (int i = n - 1; i >= 0; i--) {
		win *ww = wins[i];
		if (ww->a.map_state != IsViewable && !find_fade(ww))
			continue;
		if (ww->windowType == winDesktopAtom) continue;
		if (ww->a.override_redirect)          continue;
		if (ww->zoom_excluded)                continue;
		if (zoom_is_fullscreen(ww))           continue;
		if (!ww->picture) {
			XRenderPictureAttributes pa;
			Drawable draw = ww->id;
#if HAS_NAME_WINDOW_PIXMAP
			if (hasNamePixmap && !ww->pixmap)
				ww->pixmap = XCompositeNameWindowPixmap(dpy, ww->id);
			if (ww->pixmap) draw = ww->pixmap;
#endif
			pa.subwindow_mode = IncludeInferiors;
			ww->picture = XRenderCreatePicture(dpy, draw,
			                                   XRenderFindVisualFormat(dpy, ww->a.visual),
			                                   CPSubwindowMode, &pa);
		}
		if (!ww->picture) continue;
		int wx, wy, ww2, wh;
		win_geom(ww, &wx, &wy, &ww2, &wh);
		int zx = (int)((wx  - cx) * zf + cx);
		int zy = (int)((wy  - cy) * zf + cy);
		int zw = (int)(ww2 * zf);
		int zh = (int)(wh  * zf);
		if (zw <= 0 || zh <= 0)           continue;
		if (zx >= root_width  || zy >= root_height) continue;
		if (zx + zw <= 0 || zy + zh <= 0) continue;

		if (compMode == CompClientShadows &&
		    ww->shadow && !ww->shadow_excluded && ww->windowType != winDesktopAtom) {
			int szx = (int)((ww->a.x + ww->shadow_dx - cx) * zf + cx);
			int szy = (int)((ww->a.y + ww->shadow_dy - cy) * zf + cy);
			int szw = (int)(ww->shadow_width  * zf);
			int szh = (int)(ww->shadow_height * zf);
			if (szw > 0 && szh > 0) {
				if (ww->mode == WINDOW_ARGB) {
					XRectangle wr = {zx, zy, zw, zh};
					XRectangle screen = {0, 0, root_width, root_height};
					XserverRegion full   = XFixesCreateRegion(dpy, NULL, 0);
					XserverRegion winRgn = XFixesCreateRegion(dpy, &wr, 1);
					XFixesSetRegion(dpy, full, &screen, 1);
					XFixesSubtractRegion(dpy, full, full, winRgn);
					XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, full);
					XFixesDestroyRegion(dpy, full);
					XFixesDestroyRegion(dpy, winRgn);
				}
				XRenderSetPictureTransform(dpy, ww->shadow, &scale);
				XRenderSetPictureFilter(dpy, ww->shadow, FilterBilinear, NULL, 0);
				XRenderComposite(dpy, PictOpOver, shadowColorPicture, ww->shadow,
				                 rootPicture, 0, 0, 0, 0, szx, szy, szw, szh);
				XRenderSetPictureTransform(dpy, ww->shadow, &identity);
				XRenderSetPictureFilter(dpy, ww->shadow, FilterNearest, NULL, 0);
				if (ww->mode == WINDOW_ARGB)
					XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, None);
			}
		}
		if (ww->opacity != OPAQUE && !ww->alphaPict)
			ww->alphaPict = solid_picture(dpy, False,
			                              (double)ww->opacity / OPAQUE, 0, 0, 0);
		XRenderSetPictureTransform(dpy, ww->picture, &scale);
		XRenderSetPictureFilter(dpy, ww->picture, FilterBilinear, NULL, 0);
		if (ww->mode == WINDOW_SOLID)
			XRenderComposite(dpy, PictOpSrc, ww->picture, None, rootPicture,
			                 0, 0, 0, 0, zx, zy, zw, zh);
		else
			XRenderComposite(dpy, PictOpOver, ww->picture, ww->alphaPict,
			                 rootPicture, 0, 0, 0, 0, zx, zy, zw, zh);
		XRenderSetPictureTransform(dpy, ww->picture, &identity);
		XRenderSetPictureFilter(dpy, ww->picture, FilterNearest, NULL, 0);
	}
	free(wins);
	zoom_paint_panels(dpy);
}

static void
zoom_paint_panels(Display *dpy)
{
	int n = 0;
	for (win *w = list; w; w = w->next) n++;
	win **wins = malloc((size_t)n * sizeof(win *));
	if (!wins) return;
	n = 0;
	for (win *w = list; w; w = w->next) wins[n++] = w;

	for (int i = n - 1; i >= 0; i--) {
		win *w = wins[i];
		if (!w->a.override_redirect && !w->zoom_excluded && !zoom_is_fullscreen(w)) continue;
		if (w->a.map_state != IsViewable && !find_fade(w)) continue;
		if (!w->picture) {
			XRenderPictureAttributes pa;
			Drawable draw = w->id;
#if HAS_NAME_WINDOW_PIXMAP
			if (hasNamePixmap && !w->pixmap)
				w->pixmap = XCompositeNameWindowPixmap(dpy, w->id);
			if (w->pixmap) draw = w->pixmap;
#endif
			pa.subwindow_mode = IncludeInferiors;
			w->picture = XRenderCreatePicture(dpy, draw,
			                                  XRenderFindVisualFormat(dpy, w->a.visual),
			                                  CPSubwindowMode, &pa);
		}
		if (!w->picture) continue;
		if (w->a.x + w->a.width  < 1 || w->a.x >= root_width  ||
		    w->a.y + w->a.height < 1 || w->a.y >= root_height)
			continue;
		int x, y, wid, hei;
		win_geom(w, &x, &y, &wid, &hei);
		if (compMode != CompSimple && !w->shadow_excluded) {
			XRectangle wr     = {x, y, wid, hei};
			XRectangle screen = {0, 0, root_width, root_height};
			XserverRegion shadowClip = XFixesCreateRegion(dpy, NULL, 0);
			XserverRegion winRgn     = XFixesCreateRegion(dpy, &wr, 1);
			XFixesSetRegion(dpy, shadowClip, &screen, 1);
			XFixesSubtractRegion(dpy, shadowClip, shadowClip, winRgn);
			XFixesDestroyRegion(dpy, winRgn);
			XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, shadowClip);
			XFixesDestroyRegion(dpy, shadowClip);
			if (compMode == CompServerShadows && w->windowType != winDesktopAtom) {
				if (w->opacity != OPAQUE && !w->shadowPict)
					w->shadowPict = solid_picture(dpy, True,
					                              (double)w->opacity / OPAQUE * 0.3, 0, 0, 0);
				XRenderComposite(dpy, PictOpOver,
				                 w->shadowPict ? w->shadowPict : transBlackPicture,
				                 w->picture, rootPicture, 0, 0, 0, 0,
				                 w->a.x + w->shadow_dx, w->a.y + w->shadow_dy,
				                 w->shadow_width, w->shadow_height);
			} else if (compMode == CompClientShadows && w->shadow && w->windowType != winDesktopAtom) {
				XRenderComposite(dpy, PictOpOver, shadowColorPicture, w->shadow, rootPicture,
				                 0, 0, 0, 0,
				                 w->a.x + w->shadow_dx, w->a.y + w->shadow_dy,
				                 w->shadow_width, w->shadow_height);
			}
			XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, None);
		}
		if (w->opacity != OPAQUE && !w->alphaPict)
			w->alphaPict = solid_picture(dpy, False, (double)w->opacity / OPAQUE, 0, 0, 0);
		if (w->mode == WINDOW_SOLID && w->opacity == OPAQUE)
			XRenderComposite(dpy, PictOpSrc, w->picture, None, rootPicture,
			                 0, 0, 0, 0, x, y, wid, hei);
		else
			XRenderComposite(dpy, PictOpOver, w->picture, w->alphaPict, rootPicture,
			                 0, 0, 0, 0, x, y, wid, hei);
	}
	free(wins);
}

static void
zoom_make_socket_path(const char *display_str, char *out, size_t outsz)
{
	char safe[64];
	int j = 0;
	for (int i = 0; display_str[i] && j < 60; i++)
		safe[j++] = (display_str[i] == '/') ? '_' : display_str[i];
	safe[j] = '\0';
	snprintf(out, outsz, "/tmp/vcompmgr_%s.sock", safe);
}

static Bool
zoom_read_state(const char *display_str, double *out)
{
	char sock[108], path[140];
	zoom_make_socket_path(display_str, sock, sizeof(sock));
	snprintf(path, sizeof(path), "%s.zoom", sock);
	FILE *f = fopen(path, "r");
	if (!f) return False;
	Bool ok = fscanf(f, "%lf", out) == 1;
	fclose(f);
	return ok;
}

static void
zoom_init_drm(void)
{
	char path[32];
	for (int i = 0; i < 8; i++) {
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0) continue;
		drmVBlank probe = {.request = {DRM_VBLANK_RELATIVE, 0, 0}};
		if (drmWaitVBlank(fd, &probe) == 0) {
			zoom_drm_fd = fd;
			drmVBlank v1 = {.request = {DRM_VBLANK_RELATIVE, 1, 0}};
			drmVBlank v2 = {.request = {DRM_VBLANK_RELATIVE, 1, 0}};
			if (drmWaitVBlank(fd, &v1) == 0 && drmWaitVBlank(fd, &v2) == 0) {
				long ns = (long)(v2.reply.tval_sec  - v1.reply.tval_sec)  * 1000000000L +
				          (long)(v2.reply.tval_usec - v1.reply.tval_usec) * 1000L;
				if (ns > 1000000L && ns < 100000000L)
					zoom_vblank_ns = ns;
			}
			break;
		}
		close(fd);
	}
}

static void
zoom_wait_vblank(void)
{
	if (zoom_drm_fd < 0) {
		struct timespec req = {0, zoom_vblank_ns};
		clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
		return;
	}
	drmVBlank vbl = {.request = {DRM_VBLANK_RELATIVE, 1, 0}};
	if (drmWaitVBlank(zoom_drm_fd, &vbl) != 0) {
		close(zoom_drm_fd);
		zoom_drm_fd = -1;
	}
}

static void
zoom_cleanup(void)
{
	if (zoom_ipc_fd >= 0) {
		close(zoom_ipc_fd);
		zoom_ipc_fd = -1;
	}
	if (zoom_socket_path[0]) {
		char state_path[140];
		unlink(zoom_socket_path);
		snprintf(state_path, sizeof(state_path), "%s.zoom", zoom_socket_path);
		unlink(state_path);
		zoom_socket_path[0] = '\0';
	}
}

static void
zoom_signal_handler(int sig)
{
	zoom_cleanup();
	signal(sig, SIG_DFL);
	raise(sig);
}

static void
zoom_socket_init(Display *dpy)
{
	zoom_make_socket_path(DisplayString(dpy), zoom_socket_path, sizeof(zoom_socket_path));
	zoom_ipc_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (zoom_ipc_fd < 0) return;
	unlink(zoom_socket_path);
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	strncpy(addr.sun_path, zoom_socket_path, sizeof(addr.sun_path) - 1);
	if (bind(zoom_ipc_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(zoom_ipc_fd);
		zoom_ipc_fd = -1;
		return;
	}
	fcntl(zoom_ipc_fd, F_SETFL, O_NONBLOCK);
}

static Bool
ipc_send(const char *display_str, const char *msg)
{
	char path[108];
	zoom_make_socket_path(display_str, path, sizeof(path));
	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) return False;
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	ssize_t r = sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&addr, sizeof(addr));
	close(fd);
	return r > 0;
}

static void
set_shadow_color(Display *dpy, const char *hex)
{
	unsigned int ri = 0, gi = 0, bi = 0;
	const char *s = (hex && hex[0] == '#') ? hex + 1 : hex;
	if (s) sscanf(s, "%2x%2x%2x", &ri, &gi, &bi);
	if (shadowColorPicture)
		XRenderFreePicture(dpy, shadowColorPicture);
	shadowColorPicture = solid_picture(dpy, True, 1,
	                                   ri / 255.0, gi / 255.0, bi / 255.0);
}

static void
zoom_poll_ipc(Display *dpy)
{
	char buf[64];
	ssize_t n;
	if (zoom_ipc_fd < 0) return;
	while ((n = recv(zoom_ipc_fd, buf, sizeof(buf) - 1, 0)) > 0) {
		char tok[32];
		buf[n] = '\0';
		if (sscanf(buf, "zoom %31s", tok) == 1) {
			double target;
			if (tok[0] == '+' || tok[0] == '-') {
				pthread_mutex_lock(&zoom_mutex);
				target = round((zoom_target + atof(tok)) * 1e6) / 1e6;
				pthread_mutex_unlock(&zoom_mutex);
			} else {
				target = atof(tok);
			}
			target = target < zoom_min ? zoom_min : target > zoom_max ? zoom_max : target;
			zoom_start_animation(target);
		} else if (sscanf(buf, "shadowcolor %31s", tok) == 1) {
			set_shadow_color(dpy, tok);
			XRectangle r = {0, 0, root_width, root_height};
			if (allDamage)
				XFixesDestroyRegion(dpy, allDamage);
			allDamage = XFixesCreateRegion(dpy, &r, 1);
		}
	}
}

static void
paint_root(Display *dpy)
{
	if (!rootTile)
		rootTile = root_tile(dpy);
	XRenderComposite(dpy, PictOpSrc, rootTile, None, rootBuffer,
	                 0, 0, 0, 0, 0, 0, root_width, root_height);
}

static XserverRegion
win_extents(Display *dpy, win *w)
{
	XRectangle r = {
	    .x      = w->a.x,
	    .y      = w->a.y,
	    .width  = w->a.width  + w->a.border_width * 2,
	    .height = w->a.height + w->a.border_width * 2};

	if (compMode != CompSimple && !w->shadow_excluded) {
		XRectangle sr;
		if (compMode == CompServerShadows) {
			w->shadow_dx     = 2;
			w->shadow_dy     = 7;
			w->shadow_width  = w->a.width;
			w->shadow_height = w->a.height;
		} else {
			w->shadow_dx = shadowOffsetX - (gaussianMap ? gaussianMap->size : 0) / 2;
			w->shadow_dy = shadowOffsetY - (gaussianMap ? gaussianMap->size : 0) / 2;
			if (!w->shadow) {
				double opacity = shadowOpacity;
				if (w->mode == WINDOW_TRANS || w->mode == WINDOW_ARGB)
					opacity *= (double)w->opacity / (double)OPAQUE;
				w->shadow = shadow_picture(dpy, opacity,
				                           w->a.width  + w->a.border_width * 2,
				                           w->a.height + w->a.border_width * 2,
				                           &w->shadow_width, &w->shadow_height);
			}
		}
		sr = (XRectangle){
		    .x      = w->a.x + w->shadow_dx,
		    .y      = w->a.y + w->shadow_dy,
		    .width  = w->shadow_width,
		    .height = w->shadow_height};
		if (sr.x < r.x) { r.width = (r.x + r.width) - sr.x; r.x = sr.x; }
		if (sr.y < r.y) { r.height = (r.y + r.height) - sr.y; r.y = sr.y; }
		if (sr.x + sr.width  > r.x + r.width)  r.width  = sr.x + sr.width  - r.x;
		if (sr.y + sr.height > r.y + r.height)  r.height = sr.y + sr.height - r.y;
	}
	return XFixesCreateRegion(dpy, &r, 1);
}

static void
invalidate_shadow(Display *dpy, win *w)
{
	if (w->shadow) {
		shadow_cache_unref(dpy, w->shadow);
		w->shadow = None;
	}
	XserverRegion r = win_extents(dpy, w);
	if (w->extents) {
		XFixesUnionRegion(dpy, r, r, w->extents);
		XFixesDestroyRegion(dpy, w->extents);
	}
	w->extents = None;
	add_damage(dpy, r);
}

static XserverRegion
border_size(Display *dpy, win *w)
{
	set_ignore(dpy, NextRequest(dpy));
	XserverRegion border = XFixesCreateRegionFromWindow(dpy, w->id, WindowRegionBounding);
	set_ignore(dpy, NextRequest(dpy));
	XFixesTranslateRegion(dpy, border,
	                      w->a.x + w->a.border_width,
	                      w->a.y + w->a.border_width);
	return border;
}

static void
paint_all(Display *dpy, XserverRegion region)
{
	win *t = NULL;

	if (!region) {
		XRectangle r = {0, 0, root_width, root_height};
		region = XFixesCreateRegion(dpy, &r, 1);
	}
	if (!rootBuffer) {
		Pixmap rootPixmap = XCreatePixmap(dpy, root, root_width, root_height,
		                                  DefaultDepth(dpy, scr));
		rootBuffer = XRenderCreatePicture(dpy, rootPixmap,
		                                  XRenderFindVisualFormat(dpy, DefaultVisual(dpy, scr)),
		                                  0, NULL);
		XFreePixmap(dpy, rootPixmap);
	}
	XFixesSetPictureClipRegion(dpy, rootPicture, 0, 0, region);

	for (win *w = list; w; w = w->next) {
		if (!w->damaged) continue;
		if (w->a.map_state != IsViewable && !find_fade(w)) continue;
		if (!w->picture) {
			XRenderPictureAttributes pa;
			Drawable draw = w->id;
#if HAS_NAME_WINDOW_PIXMAP
			if (hasNamePixmap && !w->pixmap)
				w->pixmap = XCompositeNameWindowPixmap(dpy, w->id);
			if (w->pixmap) draw = w->pixmap;
#endif
			XRenderPictFormat *format = XRenderFindVisualFormat(dpy, w->a.visual);
			pa.subwindow_mode = IncludeInferiors;
			w->picture = XRenderCreatePicture(dpy, draw, format, CPSubwindowMode, &pa);
		}
		if (clipChanged) {
			if (w->borderSize) {
				set_ignore(dpy, NextRequest(dpy));
				XFixesDestroyRegion(dpy, w->borderSize);
				w->borderSize = None;
			}
			if (w->extents) {
				XFixesDestroyRegion(dpy, w->extents);
				w->extents = None;
			}
			if (w->borderClip) {
				XFixesDestroyRegion(dpy, w->borderClip);
				w->borderClip = None;
			}
		}
		if (!w->borderSize) w->borderSize = border_size(dpy, w);
		if (!w->extents)    w->extents    = win_extents(dpy, w);
		if (w->mode == WINDOW_SOLID) {
			int x, y, wid, hei;
			win_geom(w, &x, &y, &wid, &hei);
			XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, region);
			set_ignore(dpy, NextRequest(dpy));
			XFixesSubtractRegion(dpy, region, region, w->borderSize);
			set_ignore(dpy, NextRequest(dpy));
			XRenderComposite(dpy, PictOpSrc, w->picture, None, rootBuffer,
			                 0, 0, 0, 0, x, y, wid, hei);
		}
		if (!w->borderClip) {
			w->borderClip = XFixesCreateRegion(dpy, NULL, 0);
			XFixesCopyRegion(dpy, w->borderClip, region);
		}
		w->prev_trans = t;
		t = w;
	}

	XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, region);
	paint_root(dpy);

	for (win *w = t; w; w = w->prev_trans) {
		XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, w->borderClip);
		switch (compMode) {
		case CompSimple:
			break;
		case CompServerShadows:
			if (w->windowType == winDesktopAtom) break;
			set_ignore(dpy, NextRequest(dpy));
			if (w->opacity != OPAQUE && !w->shadowPict)
				w->shadowPict = solid_picture(dpy, True,
				                              (double)w->opacity / OPAQUE * 0.3, 0, 0, 0);
			XRenderComposite(dpy, PictOpOver,
			                 w->shadowPict ? w->shadowPict : transBlackPicture,
			                 w->picture, rootBuffer, 0, 0, 0, 0,
			                 w->a.x + w->shadow_dx, w->a.y + w->shadow_dy,
			                 w->shadow_width, w->shadow_height);
			break;
		case CompClientShadows:
			if (w->shadow && w->windowType != winDesktopAtom) {
				if (w->mode == WINDOW_ARGB) {
					XserverRegion shadowClip = XFixesCreateRegion(dpy, NULL, 0);
					XFixesCopyRegion(dpy, shadowClip, w->borderClip);
					XFixesSubtractRegion(dpy, shadowClip, shadowClip, w->borderSize);
					XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, shadowClip);
					XFixesDestroyRegion(dpy, shadowClip);
				}
				XRenderComposite(dpy, PictOpOver, shadowColorPicture, w->shadow, rootBuffer,
				                 0, 0, 0, 0,
				                 w->a.x + w->shadow_dx, w->a.y + w->shadow_dy,
				                 w->shadow_width, w->shadow_height);
			}
			break;
		}
		if (w->opacity != OPAQUE && !w->alphaPict)
			w->alphaPict = solid_picture(dpy, False,
			                             (double)w->opacity / OPAQUE, 0, 0, 0);
		int x, y, wid, hei;
		win_geom(w, &x, &y, &wid, &hei);
		if (w->mode == WINDOW_TRANS) {
			XFixesIntersectRegion(dpy, w->borderClip, w->borderClip, w->borderSize);
			XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, w->borderClip);
			set_ignore(dpy, NextRequest(dpy));
			XRenderComposite(dpy, PictOpOver, w->picture, w->alphaPict, rootBuffer,
			                 0, 0, 0, 0, x, y, wid, hei);
		} else if (w->mode == WINDOW_ARGB) {
			XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, w->borderClip);
			set_ignore(dpy, NextRequest(dpy));
			XRenderComposite(dpy, PictOpOver, w->picture, w->alphaPict, rootBuffer,
			                 0, 0, 0, 0, x, y, wid, hei);
		}
		XFixesDestroyRegion(dpy, w->borderClip);
		w->borderClip = None;
	}
	XFixesDestroyRegion(dpy, region);
	if (rootBuffer && rootBuffer != rootPicture)
		XFixesSetPictureClipRegion(dpy, rootBuffer, 0, 0, None);
}

static void
add_damage(Display *dpy, XserverRegion damage)
{
	if (allDamage) {
		XFixesUnionRegion(dpy, allDamage, allDamage, damage);
		XFixesDestroyRegion(dpy, damage);
	} else {
		allDamage = damage;
	}
}

static void
repair_win(Display *dpy, win *w)
{
	XserverRegion parts;
	if (!w->damaged) {
		parts = win_extents(dpy, w);
		set_ignore(dpy, NextRequest(dpy));
		XDamageSubtract(dpy, w->damage, None, None);
	} else {
		parts = XFixesCreateRegion(dpy, NULL, 0);
		set_ignore(dpy, NextRequest(dpy));
		XDamageSubtract(dpy, w->damage, None, parts);
		XFixesTranslateRegion(dpy, parts,
		                      w->a.x + w->a.border_width,
		                      w->a.y + w->a.border_width);
		if (compMode == CompServerShadows) {
			XserverRegion o = XFixesCreateRegion(dpy, NULL, 0);
			XFixesCopyRegion(dpy, o, parts);
			XFixesTranslateRegion(dpy, o, w->shadow_dx, w->shadow_dy);
			XFixesUnionRegion(dpy, parts, parts, o);
			XFixesDestroyRegion(dpy, o);
		}
	}
	add_damage(dpy, parts);
	w->damaged = 1;
}

static unsigned int get_opacity_prop(Display *dpy, win *w, unsigned int def);

static void
win_cache_class(Display *dpy, win *w)
{
	free(w->wm_name);
	free(w->wm_class);
	w->wm_name  = NULL;
	w->wm_class = NULL;
	XClassHint ch;
	if (!XGetClassHint(dpy, w->id, &ch))
		return;
	w->wm_name  = ch.res_name  ? strdup(ch.res_name)  : NULL;
	w->wm_class = ch.res_class ? strdup(ch.res_class) : NULL;
	XFree(ch.res_name);
	XFree(ch.res_class);
}

static Bool
win_shadow_excluded(win *w)
{
	for (size_t i = 0; i < N_SHADOW_EXCLUDE_TYPES; i++)
		if (w->windowType == shadow_exclude_type_atoms[i])
			return True;
	if (!w->wm_name && !w->wm_class)
		return False;
	for (size_t i = 0; i < N_SHADOW_EXCLUDE_CLASSES; i++)
		if ((w->wm_name  && strcmp(w->wm_name,  shadow_exclude_classes[i]) == 0) ||
		    (w->wm_class && strcmp(w->wm_class, shadow_exclude_classes[i]) == 0))
			return True;
	return False;
}

static inline Bool
zoom_is_fullscreen(win *w)
{
	int fw = w->a.width  + w->a.border_width * 2;
	int fh = w->a.height + w->a.border_width * 2;
	return (w->a.x == 0 && w->a.y == 0 &&
	        fw >= root_width && fh >= root_height);
}

static Bool
win_zoom_excluded(win *w)
{
	for (size_t i = 0; i < N_ZOOM_EXCLUDE_TYPES; i++)
		if (w->windowType == zoom_exclude_type_atoms[i])
			return True;
	if (!w->wm_name && !w->wm_class)
		return False;
	for (size_t i = 0; i < N_ZOOM_EXCLUDE_CLASSES; i++)
		if ((w->wm_name  && strcmp(w->wm_name,  zoom_exclude_classes[i]) == 0) ||
		    (w->wm_class && strcmp(w->wm_class, zoom_exclude_classes[i]) == 0))
			return True;
	return False;
}

static void
map_win(Display *dpy, Window id, unsigned long sequence __attribute__((unused)), Bool doFade)
{
	win *w = find_win(dpy, id);
	if (!w) return;
	w->a.map_state = IsViewable;
	XSelectInput(dpy, id, PropertyChangeMask);
	w->opacity = get_opacity_prop(dpy, w, OPAQUE);
	determine_mode(dpy, w);
	w->damaged = 0;
	w->windowType     = determine_wintype(dpy, id);
	win_cache_class(dpy, w);
	w->shadow_excluded = win_shadow_excluded(w);
	w->zoom_excluded   = win_zoom_excluded(w);
#if HAS_NAME_WINDOW_PIXMAP
	if (w->pixmap) {
		XFreePixmap(dpy, w->pixmap);
		w->pixmap = None;
	}
#endif
	if (w->picture) {
		set_ignore(dpy, NextRequest(dpy));
		XRenderFreePicture(dpy, w->picture);
		w->picture = None;
	}
	if (doFade && fadeWindows)
		set_fade(dpy, w, 0, 1.0, fade_in_step,
		         NULL, False, True, True);
}

static void
finish_unmap_win(Display *dpy, win *w)
{
	w->damaged = 0;
	XserverRegion damage;
	if (w->extents != None) {
		damage = w->extents;
		w->extents = None;
	} else {
		XRectangle r = {w->a.x, w->a.y,
		                w->a.width + w->a.border_width * 2,
		                w->a.height + w->a.border_width * 2};
		damage = XFixesCreateRegion(dpy, &r, 1);
	}
	add_damage(dpy, damage);
#if HAS_NAME_WINDOW_PIXMAP
	if (w->pixmap) {
		XFreePixmap(dpy, w->pixmap);
		w->pixmap = None;
	}
#endif
	if (w->picture) {
		set_ignore(dpy, NextRequest(dpy));
		XRenderFreePicture(dpy, w->picture);
		w->picture = None;
	}
	set_ignore(dpy, NextRequest(dpy));
	XSelectInput(dpy, w->id, 0);
	if (w->borderSize) {
		set_ignore(dpy, NextRequest(dpy));
		XFixesDestroyRegion(dpy, w->borderSize);
		w->borderSize = None;
	}
	if (w->shadow) {
		shadow_cache_unref(dpy, w->shadow);
		w->shadow = None;
	}
	if (w->borderClip) {
		XFixesDestroyRegion(dpy, w->borderClip);
		w->borderClip = None;
	}
	clipChanged = True;
}

#if HAS_NAME_WINDOW_PIXMAP
static void
unmap_callback(Display *dpy, win *w, Bool gone __attribute__((unused)))
{
	if (w->a.map_state == IsViewable) return;
	finish_unmap_win(dpy, w);
}
#endif

static void
unmap_win(Display *dpy, Window id, Bool doFade)
{
	win *w = find_win(dpy, id);
	if (!w) return;
	w->a.map_state = IsUnmapped;
#if HAS_NAME_WINDOW_PIXMAP
	if (doFade && fadeWindows)
		set_fade(dpy, w, w->opacity * 1.0 / OPAQUE, 0.0, fade_out_step,
		         unmap_callback, False, False, True);
	else
#endif
		finish_unmap_win(dpy, w);
}

static unsigned int
get_opacity_prop(Display *dpy, win *w, unsigned int def)
{
	Atom actual;
	int format;
	unsigned long n, left;
	unsigned char *data;
	if (XGetWindowProperty(dpy, w->id, opacityAtom, 0L, 1L, False,
	                       XA_CARDINAL, &actual, &format, &n, &left, &data) == Success &&
	    data != NULL) {
		unsigned int i;
		memcpy(&i, data, sizeof(unsigned int));
		XFree((void *)data);
		return i;
	}
	return def;
}

static double
get_opacity_percent(Display *dpy, win *w, double def)
{
	return get_opacity_prop(dpy, w, (unsigned int)(OPAQUE * def)) * 1.0 / OPAQUE;
}

static Atom
get_wintype_prop(Display *dpy, Window w)
{
	Atom actual;
	int format;
	unsigned long n, left;
	unsigned char *data;
	if (XGetWindowProperty(dpy, w, winTypeAtom, 0L, 1L, False,
	                       XA_ATOM, &actual, &format, &n, &left, &data) == Success &&
	    data != (unsigned char *)None) {
		Atom a;
		memcpy(&a, data, sizeof(Atom));
		XFree((void *)data);
		return a;
	}
	return winNormalAtom;
}

static void
determine_mode(Display *dpy, win *w)
{
	if (w->alphaPict) {
		XRenderFreePicture(dpy, w->alphaPict);
		w->alphaPict = None;
	}
	if (w->shadowPict) {
		XRenderFreePicture(dpy, w->shadowPict);
		w->shadowPict = None;
	}
	XRenderPictFormat *format = (w->a.class == InputOnly)
	    ? NULL
	    : XRenderFindVisualFormat(dpy, w->a.visual);
	if (format && format->type == PictTypeDirect && format->direct.alphaMask)
		w->mode = WINDOW_ARGB;
	else if (w->opacity != OPAQUE)
		w->mode = WINDOW_TRANS;
	else
		w->mode = WINDOW_SOLID;
	if (w->extents) {
		XserverRegion damage = XFixesCreateRegion(dpy, NULL, 0);
		XFixesCopyRegion(dpy, damage, w->extents);
		add_damage(dpy, damage);
	}
}

static Atom
determine_wintype(Display *dpy, Window w)
{
	Atom type = get_wintype_prop(dpy, w);
	if (type != winNormalAtom)
		return type;

	Window root_return, parent_return;
	Window *children = NULL;
	unsigned int nchildren;
	if (!XQueryTree(dpy, w, &root_return, &parent_return, &children, &nchildren)) {
		if (children) XFree((void *)children);
		return winNormalAtom;
	}
	for (unsigned int i = 0; i < nchildren; i++) {
		type = get_wintype_prop(dpy, children[i]);
		if (type != winNormalAtom) {
			XFree((void *)children);
			return type;
		}
	}
	if (children) XFree((void *)children);
	return winNormalAtom;
}

static void
add_win(Display *dpy, Window id, Window prev)
{
	win *nw = malloc(sizeof(win));
	if (!nw) return;
	win **p = &list;
	if (prev)
		for (; *p; p = &(*p)->next)
			if ((*p)->id == prev) break;
	set_ignore(dpy, NextRequest(dpy));
	if (!XGetWindowAttributes(dpy, id, &nw->a)) {
		free(nw);
		return;
	}
	XWindowAttributes attrs = nw->a;
	unsigned long damage_seq = (attrs.class == InputOnly) ? 0 : NextRequest(dpy);
	*nw = (win){
	    .id            = id,
	    .a             = attrs,
	    .shaped        = False,
	    .shape_bounds  = {attrs.x, attrs.y, attrs.width, attrs.height},
	    .damaged       = 0,
#if HAS_NAME_WINDOW_PIXMAP
	    .pixmap        = None,
#endif
	    .picture       = None,
	    .damage_sequence = damage_seq,
	    .damage        = None,
	    .alphaPict     = None,
	    .shadowPict    = None,
	    .borderSize    = None,
	    .extents       = None,
	    .shadow        = None,
	    .opacity       = OPAQUE,
	    .borderClip    = None,
	    .prev_trans    = NULL,
	    .wm_name      = NULL,
	    .wm_class     = NULL,
	};
	if (nw->a.class != InputOnly)
		nw->damage = XDamageCreate(dpy, id, XDamageReportNonEmpty);
	XShapeSelectInput(dpy, id, ShapeNotifyMask);
	nw->windowType = determine_wintype(dpy, id);
	nw->next = *p;
	*p = nw;
	if (nw->a.map_state == IsViewable)
		map_win(dpy, id, nw->damage_sequence - 1, True);
}

static void
restack_win(Display *dpy __attribute__((unused)), win *w, Window new_above)
{
	Window old_above = w->next ? w->next->id : None;
	if (old_above == new_above) return;
	win **prev;
	for (prev = &list; *prev; prev = &(*prev)->next)
		if (*prev == w) break;
	*prev = w->next;
	for (prev = &list; *prev; prev = &(*prev)->next)
		if ((*prev)->id == new_above) break;
	w->next = *prev;
	*prev = w;
}

static void
configure_win(Display *dpy, XConfigureEvent *ce)
{
	win *w = find_win(dpy, ce->window);
	XserverRegion damage = None;

	if (!w) {
		if (ce->window == root) {
			if (rootBuffer) {
				XRenderFreePicture(dpy, rootBuffer);
				rootBuffer = None;
			}
			root_width  = ce->width;
			root_height = ce->height;
		}
		return;
	}
	damage = XFixesCreateRegion(dpy, NULL, 0);
	if (w->extents != None)
		XFixesCopyRegion(dpy, damage, w->extents);
	w->shape_bounds.x -= w->a.x;
	w->shape_bounds.y -= w->a.y;
	w->a.x = ce->x;
	w->a.y = ce->y;
	if (w->a.width != ce->width || w->a.height != ce->height) {
#if HAS_NAME_WINDOW_PIXMAP
		if (w->pixmap) {
			XFreePixmap(dpy, w->pixmap);
			w->pixmap = None;
			if (w->picture) {
				XRenderFreePicture(dpy, w->picture);
				w->picture = None;
			}
		}
#endif
		if (w->shadow) {
			shadow_cache_unref(dpy, w->shadow);
			w->shadow = None;
		}
	}
	w->a.width           = ce->width;
	w->a.height          = ce->height;
	w->a.border_width    = ce->border_width;
	w->a.override_redirect = ce->override_redirect;
	restack_win(dpy, w, ce->above);
	if (damage) {
		XserverRegion extents = win_extents(dpy, w);
		XFixesUnionRegion(dpy, damage, damage, extents);
		XFixesDestroyRegion(dpy, extents);
		add_damage(dpy, damage);
	}
	w->shape_bounds.x += w->a.x;
	w->shape_bounds.y += w->a.y;
	if (!w->shaped) {
		w->shape_bounds.width  = w->a.width;
		w->shape_bounds.height = w->a.height;
	}
	clipChanged = True;
}

static void
circulate_win(Display *dpy, XCirculateEvent *ce)
{
	win *w = find_win(dpy, ce->window);
	if (!w) return;
	Window new_above = (ce->place == PlaceOnTop) ? list->id : None;
	restack_win(dpy, w, new_above);
	clipChanged = True;
}

static void
finish_destroy_win(Display *dpy, Window id, Bool gone)
{
	win **prev, *w;
	for (prev = &list; (w = *prev); prev = &w->next) {
		if (w->id != id) continue;
		if (gone) finish_unmap_win(dpy, w);
		*prev = w->next;
		if (w->picture) {
			set_ignore(dpy, NextRequest(dpy));
			XRenderFreePicture(dpy, w->picture);
		}
		if (w->alphaPict)  XRenderFreePicture(dpy, w->alphaPict);
		if (w->shadowPict) XRenderFreePicture(dpy, w->shadowPict);
		if (w->shadow)     shadow_cache_unref(dpy, w->shadow);
		if (w->damage != None) {
			set_ignore(dpy, NextRequest(dpy));
			XDamageDestroy(dpy, w->damage);
		}
		cleanup_fade(dpy, w);
		free(w->wm_name);
		free(w->wm_class);
		free(w);
		break;
	}
}

#if HAS_NAME_WINDOW_PIXMAP
static void
destroy_callback(Display *dpy, win *w, Bool gone)
{
	if (find_win(dpy, w->id) != w) return;
	finish_destroy_win(dpy, w->id, gone);
}
#endif

static void
destroy_win(Display *dpy, Window id, Bool gone, Bool doFade)
{
	win *w = find_win(dpy, id);
#if HAS_NAME_WINDOW_PIXMAP
	if (w && w->pixmap && doFade && fadeWindows)
		set_fade(dpy, w, w->opacity * 1.0 / OPAQUE, 0.0, fade_out_step,
		         destroy_callback, gone, False, True);
	else
#endif
		finish_destroy_win(dpy, id, gone);
}

static void
damage_win(Display *dpy, XDamageNotifyEvent *de)
{
	win *w = find_win(dpy, de->drawable);
	if (!w) return;
	repair_win(dpy, w);
}

static void
shape_win(Display *dpy, XShapeEvent *se)
{
	win *w = find_win(dpy, se->window);
	if (!w) return;
	if (se->kind != ShapeClip && se->kind != ShapeBounding) return;

	clipChanged = True;
	XserverRegion region0 = XFixesCreateRegion(dpy, &w->shape_bounds, 1);
	if (se->shaped == True) {
		w->shaped = True;
		w->shape_bounds = (XRectangle){
		    w->a.x + se->x, w->a.y + se->y, se->width, se->height};
	} else {
		w->shaped = False;
		w->shape_bounds = (XRectangle){
		    w->a.x, w->a.y, w->a.width, w->a.height};
	}
	XserverRegion region1 = XFixesCreateRegion(dpy, &w->shape_bounds, 1);
	XFixesUnionRegion(dpy, region0, region0, region1);
	XFixesDestroyRegion(dpy, region1);
	paint_all(dpy, region0);
}

static int
error(Display *dpy, XErrorEvent *ev)
{
	if (should_ignore(dpy, ev->serial))
		return 0;
	if (ev->request_code == composite_opcode &&
	    ev->minor_code == X_CompositeRedirectSubwindows) {
		fprintf(stderr, "Another composite manager is already running\n");
		exit(1);
	}
	const char *name = NULL;
	switch (ev->error_code - xfixes_error) {
	case BadRegion: name = "BadRegion"; break;
	}
	switch (ev->error_code - damage_error) {
	case BadDamage: name = "BadDamage"; break;
	}
	switch (ev->error_code - render_error) {
	case BadPictFormat: name = "BadPictFormat"; break;
	case BadPicture:    name = "BadPicture";    break;
	case BadPictOp:     name = "BadPictOp";     break;
	case BadGlyphSet:   name = "BadGlyphSet";   break;
	case BadGlyph:      name = "BadGlyph";      break;
	}
	static char buffer[256];
	if (!name) {
		XGetErrorText(dpy, ev->error_code, buffer, sizeof(buffer));
		name = buffer;
	}
	fprintf(stderr, "error %d: %s request %d minor %d serial %lu\n",
	        ev->error_code, name, ev->request_code, ev->minor_code, ev->serial);
	return 0;
}

static void
expose_root(Display *dpy, Window rootwin __attribute__((unused)),
            XRectangle *rects, int nrects)
{
	add_damage(dpy, XFixesCreateRegion(dpy, rects, nrects));
}

static void _X_COLD _X_NORETURN
usage(const char *program)
{
	fprintf(stderr, "%s v%s\n", program, PACKAGE_VERSION);
	fprintf(stderr, "usage: %s [options]\n%s\n", program,
	        "Options:\n"
	        "   -d display\n"
	        "      Specifies which display should be managed.\n"
	        "   -r radius\n"
	        "      Specifies the blur radius for client-side shadows.\n"
	        "   -o opacity\n"
	        "      Specifies the translucency for client-side shadows.\n"
	        "   -l left-offset\n"
	        "      Specifies the left offset for client-side shadows.\n"
	        "   -t top-offset\n"
	        "      Specifies the top offset for clinet-side shadows.\n"
	        "   -I fade-in-step\n"
	        "      Specifies the opacity change between steps while fading in.\n"
	        "   -O fade-out-step\n"
	        "      Specifies the opacity change between steps while fading out.\n"
	        "   -D fade-delta-time\n"
	        "      Specifies the time between steps in a fade in milliseconds.\n"
	        "   -c\n"
	        "      Draw client-side shadows with fuzzy edges.\n"
	        "   -f\n"
	        "      Fade windows in/out when opening/closing.\n"
	        "   -F\n"
	        "      Fade windows during opacity changes.\n"
	        "   -n\n"
	        "      Normal client-side compositing with transparency support\n"
	        "   -s\n"
	        "      Draw server-side shadows with sharp edges.\n"
	        "   -S\n"
	        "      Enable synchronous operation (for debugging).\n"
	        "   -Z zoom-factor\n"
	        "      Set zoom factor.\n"
	        "   -G\n"
	        "      Print the current zoom factor of the running instance and exit.\n"
	        "   -m value\n"
	        "      Minimum zoom factor.\n"
	        "   -M value\n"
	        "      Maximum zoom factor.\n"
	        "   -u ms\n"
	        "      Zoom animation duration in milliseconds.\n"
	        "   -b x1,y1,x2,y2\n"
	        "      Cubic bezier for the zoom animation curve.\n"
	        "   -k color\n"
	        "      Specifies the shadow color as #RRGGBB hex. (default #000000)\n");
	exit(1);
}

static Bool
register_cm(Display *dpy)
{
	char net_wm_cm[32];
	snprintf(net_wm_cm, sizeof(net_wm_cm), "_NET_WM_CM_S%d", scr);
	Atom a = XInternAtom(dpy, net_wm_cm, False);
	Window w = XGetSelectionOwner(dpy, a);
	if (w != None) {
		XTextProperty tp;
		char **strs;
		int count;
		Atom winNameAtom = XInternAtom(dpy, "_NET_WM_NAME", False);
		if (!XGetTextProperty(dpy, w, &tp, winNameAtom) &&
		    !XGetTextProperty(dpy, w, &tp, XA_WM_NAME)) {
			fprintf(stderr, "Another composite manager is already running (0x%lx)\n",
			        (unsigned long)w);
			return False;
		}
		if (XmbTextPropertyToTextList(dpy, &tp, &strs, &count) == Success) {
			fprintf(stderr, "Another composite manager is already running (%s)\n", strs[0]);
			XFreeStringList(strs);
		}
		XFree(tp.value);
		return False;
	}
	w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, 1, 1, 0, None, None);
	Xutf8SetWMProperties(dpy, w, "vcompmgr", "vcompmgr", NULL, 0, NULL, NULL, NULL);
	XSetSelectionOwner(dpy, a, w, 0);
	return True;
}

int
main(int argc, char **argv)
{
	Display *dpy;
	XEvent ev;
	Window root_return, parent_return;
	Window *children;
	unsigned int nchildren;
	XRenderPictureAttributes pa;
	XRectangle *expose_rects = NULL;
	int size_expose = 0;
	int n_expose = 0;
	struct pollfd ufd[3];
	pthread_t tid;
	int p;
	int composite_major, composite_minor;
	char *display = NULL;
	int o;
	const char *shadowcolor_arg = NULL;

	while ((o = getopt(argc, argv, "D:I:O:d:r:o:l:t:scnfFSZ:b:u:m:M:Gk:")) != -1) {
		switch (o) {
		case 'd': display = optarg; break;
		case 'D':
			fade_delta = atoi(optarg);
			if (fade_delta < 1) fade_delta = 10;
			break;
		case 'I':
			fade_in_step = atof(optarg);
			if (fade_in_step <= 0) fade_in_step = 0.01;
			break;
		case 'O':
			fade_out_step = atof(optarg);
			if (fade_out_step <= 0) fade_out_step = 0.01;
			break;
		case 's': compMode = CompServerShadows; break;
		case 'c': compMode = CompClientShadows; break;
		case 'n': compMode = CompSimple;         break;
		case 'f': fadeWindows = True;            break;
		case 'F': fadeTrans = True;              break;
		case 'S': synchronize = True;            break;
		case 'r': shadowRadius  = atoi(optarg);  break;
		case 'o': shadowOpacity = atof(optarg);  break;
		case 'l': shadowOffsetX = atoi(optarg);  break;
		case 't': shadowOffsetY = atoi(optarg);  break;
		case 'Z':
			zoom_flag = True;
			strncpy(zoom_opt_str, optarg, sizeof(zoom_opt_str) - 1);
			zoom_opt_str[sizeof(zoom_opt_str) - 1] = '\0';
			if      (optarg[0] == '+') zoom_factor = 1.0 + atof(optarg + 1);
			else if (optarg[0] == '-') zoom_factor = 1.0 - atof(optarg + 1);
			else                       zoom_factor = atof(optarg);
			if (zoom_factor <= 0.0) zoom_factor = 1.0;
			break;
		case 'b':
			if (sscanf(optarg, "%lf,%lf,%lf,%lf",
			           &zoom_bezier[0], &zoom_bezier[1],
			           &zoom_bezier[2], &zoom_bezier[3]) != 4)
				fprintf(stderr, "vcompmgr: -b: expected x1,y1,x2,y2\n");
			break;
		case 'u':
			zoom_duration = atof(optarg);
			if (zoom_duration < 0.0) zoom_duration = 0.0;
			break;
		case 'm':
			zoom_min = atof(optarg);
			if (zoom_min <= 0.0) zoom_min = 0.01;
			break;
		case 'M':
			zoom_max = atof(optarg);
			if (zoom_max <= 0.0) zoom_max = 5.0;
			break;
		case 'G':
			zoom_get_flag = True;
			break;
		case 'k':
			shadowcolor_arg = optarg;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (zoom_flag) {
		const char *ds = display ? display : getenv("DISPLAY");
		char msg[64];
		snprintf(msg, sizeof(msg), "zoom %s", zoom_opt_str);
		if (ds && ipc_send(ds, msg))
			exit(0);
		fprintf(stderr, "vcompmgr: no running instance found on %s\n",
		        ds ? ds : "(unknown display)");
		exit(1);
	}
	if (zoom_get_flag) {
		const char *ds = display ? display : getenv("DISPLAY");
		double z;
		if (ds && zoom_read_state(ds, &z)) {
			printf("%g\n", z);
			exit(0);
		}
		fprintf(stderr, "vcompmgr: no zoom state found on %s\n",
		        ds ? ds : "(unknown display)");
		exit(1);
	}

	if (shadowcolor_arg) {
		const char *ds = display ? display : getenv("DISPLAY");
		char msg[32];
		snprintf(msg, sizeof(msg), "shadowcolor %s", shadowcolor_arg);
		if (ds && ipc_send(ds, msg))
			exit(0);
	}

	dpy = XOpenDisplay(display);
	if (!dpy) {
		fprintf(stderr, "Can't open display\n");
		exit(1);
	}
	XSetErrorHandler(error);
	if (synchronize)
		XSynchronize(dpy, 1);
	scr  = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);

	if (!XRenderQueryExtension(dpy, &render_event, &render_error)) {
		fprintf(stderr, "No render extension\n");
		exit(1);
	}
	if (!XQueryExtension(dpy, COMPOSITE_NAME, &composite_opcode,
	                     &composite_event, &composite_error)) {
		fprintf(stderr, "No composite extension\n");
		exit(1);
	}
	XCompositeQueryVersion(dpy, &composite_major, &composite_minor);
#if HAS_NAME_WINDOW_PIXMAP
	if (composite_major > 0 || composite_minor >= 2)
		hasNamePixmap = True;
#endif
	if (!XDamageQueryExtension(dpy, &damage_event, &damage_error)) {
		fprintf(stderr, "No damage extension\n");
		exit(1);
	}
	if (!XFixesQueryExtension(dpy, &xfixes_event, &xfixes_error)) {
		fprintf(stderr, "No XFixes extension\n");
		exit(1);
	}
	if (!XShapeQueryExtension(dpy, &xshape_event, &xshape_error)) {
		fprintf(stderr, "No XShape extension\n");
		exit(1);
	}
	if (!register_cm(dpy))
		exit(1);

	opacityAtom        = XInternAtom(dpy, OPACITY_PROP, False);
	winTypeAtom        = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	winDesktopAtom     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
	winNormalAtom      = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
	pixmapAtom         = XInternAtom(dpy, "PIXMAP", False);
	backgroundProps[0] = XInternAtom(dpy, "_XROOTPMAP_ID", False);
	backgroundProps[1] = XInternAtom(dpy, "_XSETROOT_ID", False);
	backgroundProps[2] = None;
	for (size_t i = 0; i < N_SHADOW_EXCLUDE_TYPES; i++)
		shadow_exclude_type_atoms[i] = XInternAtom(dpy, shadow_exclude_types[i], False);
	for (size_t i = 0; i < N_ZOOM_EXCLUDE_TYPES; i++)
		zoom_exclude_type_atoms[i] = XInternAtom(dpy, zoom_exclude_types[i], False);

	pa.subwindow_mode = IncludeInferiors;

	if (compMode == CompClientShadows) {
		gaussianMap = make_gaussian_map(shadowRadius);
		presum_gaussian(gaussianMap);
	}

	root_width  = DisplayWidth(dpy, scr);
	root_height = DisplayHeight(dpy, scr);

	rootPicture = XRenderCreatePicture(dpy, root,
	                                   XRenderFindVisualFormat(dpy, DefaultVisual(dpy, scr)),
	                                   CPSubwindowMode, &pa);
	set_shadow_color(dpy, shadowcolor_arg);
	if (compMode == CompServerShadows)
		transBlackPicture = solid_picture(dpy, True, 0.3, 0, 0, 0);
	allDamage  = None;
	clipChanged = True;
	XGrabServer(dpy);
	XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
	XSelectInput(dpy, root,
	             SubstructureNotifyMask | ExposureMask |
	             StructureNotifyMask | PropertyChangeMask);
	XShapeSelectInput(dpy, root, ShapeNotifyMask);
	XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren);
	for (unsigned int i = 0; i < nchildren; i++)
		add_win(dpy, children[i], i ? children[i - 1] : None);
	XFree(children);
	XUngrabServer(dpy);
	ufd[0].fd     = ConnectionNumber(dpy);
	ufd[0].events = POLLIN;
	zoom_init_drm();
	zoom_socket_init(dpy);
	signal(SIGINT,  zoom_signal_handler);
	signal(SIGTERM, zoom_signal_handler);
	signal(SIGHUP,  zoom_signal_handler);
	signal(SIGSEGV, zoom_signal_handler);
	signal(SIGABRT, zoom_signal_handler);
	if (pipe(zoom_pipe) == 0) {
		fcntl(zoom_pipe[0], F_SETFL, O_NONBLOCK);
		fcntl(zoom_pipe[1], F_SETFL, O_NONBLOCK);
	}
	pthread_create(&tid, NULL, zoom_anim_thread, NULL);
	pthread_detach(tid);
	ufd[1].fd     = zoom_ipc_fd;
	ufd[1].events = POLLIN;
	ufd[2].fd     = zoom_pipe[0];
	ufd[2].events = POLLIN;
	paint_all(dpy, None);

	for (;;) {
		do {
			if (!QLength(dpy)) {
				int poll_ms = fade_timeout();
				if (allDamage) poll_ms = 0;
				if (poll(ufd, zoom_ipc_fd >= 0 ? 3 : 2, poll_ms) == 0) {
					break;
				}
				if (zoom_ipc_fd >= 0 && (ufd[1].revents & POLLIN))
					zoom_poll_ipc(dpy);
				if (zoom_pipe[0] >= 0 && (ufd[2].revents & POLLIN)) {
					char buf[64];
					while (read(zoom_pipe[0], buf, sizeof(buf)) > 0) {}
					pthread_mutex_lock(&zoom_mutex);
					double zf = zoom_factor;
					pthread_mutex_unlock(&zoom_mutex);
					run_fades(dpy);
					paint_all(dpy, None);
					if (zf != 1.0 && allDamage) {
						XFixesDestroyRegion(dpy, allDamage);
						allDamage = None;
					}
					zoom_present(dpy);
					XFlush(dpy);
				}
				if (!(ufd[0].revents & POLLIN) && !QLength(dpy))
					break;
			}
			XNextEvent(dpy, &ev);
			if ((ev.type & 0x7f) != KeymapNotify)
				discard_ignore(dpy, ev.xany.serial);
			switch (ev.type) {
				case CreateNotify:
					add_win(dpy, ev.xcreatewindow.window, 0);
					break;
				case ConfigureNotify:
					configure_win(dpy, &ev.xconfigure);
					break;
				case DestroyNotify:
					destroy_win(dpy, ev.xdestroywindow.window, True, True);
					break;
				case MapNotify:
					map_win(dpy, ev.xmap.window, ev.xmap.serial, True);
					break;
				case UnmapNotify:
					unmap_win(dpy, ev.xunmap.window, True);
					break;
				case ReparentNotify:
					if (ev.xreparent.parent == root)
						add_win(dpy, ev.xreparent.window, 0);
					else
						destroy_win(dpy, ev.xreparent.window, False, True);
					break;
				case CirculateNotify:
					circulate_win(dpy, &ev.xcirculate);
					break;
				case Expose:
					if (ev.xexpose.window == root) {
						int more = ev.xexpose.count + 1;
						if (n_expose == size_expose) {
							if (expose_rects) {
								XRectangle *old = expose_rects;
								expose_rects = reallocarray(old, size_expose + more,
								                            sizeof(XRectangle));
								if (!expose_rects) {
									expose_rects = old;
									expose_root(dpy, root, expose_rects, n_expose);
									n_expose = 0;
								} else {
									size_expose += more;
								}
							} else {
								expose_rects = reallocarray(NULL, more, sizeof(XRectangle));
								size_expose  = more;
							}
						}
						expose_rects[n_expose++] = (XRectangle){
						    ev.xexpose.x, ev.xexpose.y,
						    ev.xexpose.width, ev.xexpose.height};
						if (ev.xexpose.count == 0) {
							expose_root(dpy, root, expose_rects, n_expose);
							n_expose = 0;
						}
					}
					break;
				case PropertyNotify:
					for (p = 0; backgroundProps[p]; p++) {
						if (ev.xproperty.atom == backgroundProps[p]) {
							if (rootTile) {
								XClearArea(dpy, root, 0, 0, 0, 0, True);
								XRenderFreePicture(dpy, rootTile);
								rootTile = None;
								break;
							}
						}
					}
					if (ev.xproperty.atom == opacityAtom) {
						win *w = find_win(dpy, ev.xproperty.window);
						if (w) {
							if (fadeTrans && !find_fade(w)) {
								double start  = w->opacity * 1.0 / OPAQUE;
								double finish = get_opacity_percent(dpy, w, 1.0);
								double step   = (start > finish) ? fade_in_step : fade_out_step;
								set_fade(dpy, w, start, finish, step,
								         NULL, False, True, False);
							} else {
								w->opacity = get_opacity_prop(dpy, w, OPAQUE);
								determine_mode(dpy, w);
								invalidate_shadow(dpy, w);
							}
						}
					} else if (ev.xproperty.atom == XA_WM_CLASS) {
						win *w = find_win(dpy, ev.xproperty.window);
						if (w) {
							win_cache_class(dpy, w);
							w->shadow_excluded = win_shadow_excluded(w);
							w->zoom_excluded   = win_zoom_excluded(w);
							invalidate_shadow(dpy, w);
						}
					}
					break;
				default:
					if (ev.type == damage_event + XDamageNotify)
						damage_win(dpy, (XDamageNotifyEvent *)&ev);
					else if (ev.type == xshape_event + ShapeNotify)
						shape_win(dpy, (XShapeEvent *)&ev);
					break;
				}
		} while (QLength(dpy));

		if (allDamage) {
			pthread_mutex_lock(&zoom_mutex);
			double zf = zoom_factor;
			pthread_mutex_unlock(&zoom_mutex);
			run_fades(dpy);
			paint_all(dpy, allDamage);
			allDamage = None;
			if (zf == 1.0) {
				zoom_present(dpy);
				XFlush(dpy);
			}
			clipChanged = False;
		} else {
			run_fades(dpy);
		}
	}
}
