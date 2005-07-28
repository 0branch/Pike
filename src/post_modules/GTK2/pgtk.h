/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
|| $Id: pgtk.h,v 1.1 2005/07/28 15:24:58 nilsson Exp $
*/

/* Sort of unnessesary, and decreases code-size with 140Kb */
#define GTK_NO_CHECK_CASTS
/* Also sort of unessesary, most code is autogenerated anyway. */
#define NO_PIKE_SHORTHAND
#include "pgtk_config.h"
#include <program.h>
#include <pike_types.h>
#include <interpret.h>
#include <module_support.h>
#include <array.h>
#include <backend.h>
#include <stralloc.h>
#include <mapping.h>
#include <object.h>
#include <bignum.h>
#include <threads.h>
#include <builtin_functions.h>
#include <operators.h>

/*
#include <glib.h>
#include <glib-object.h>
*/
#include <gtk/gtk.h>
#if defined(HAVE_GNOME)
# include <gnome.h>
/*# include <libgnorba/gnorba.h> */
#endif

#ifdef __NT__
/* Sockets are unimplemented on NT */
#undef GTK_TYPE_SOCKET
#endif

#ifdef HAVE_GTKEXTRA_GTKEXTRA_H
# include <gtkextra/gtkextra.h>
#endif
#include "prototypes.h"
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

/*
#undef GTK_STYLE
#define GTK_STYLE(X) ((GtkStyle *)X)
*/

/*
#undef GTK_ACCEL_GROUP
#define GTK_ACCEL_GROUP(X) ((void *)X)
*/
#include "../../modules/Image/image.h"

struct object_wrapper {
  GObject *obj;
  int extra_int;
  void *extra_data;
};


struct signal_data {
  struct svalue cb;
  struct svalue args;
  int new_interface;
  int signal_id;
};

struct store_data {
  GType *types;
  int n_cols;
};

struct my_pixel {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char padding;
};

extern const char __pgtk_string_data[];
extern int pigtk_is_setup;
struct program *pgtk_type_to_program(GObject *widget);
void my_pop_n_elems(int n);
void my_ref_push_object(struct object *o);
void pgtk_return_this(int n);
void push_atom(GdkAtom a);

void pgtk_verify_setup();
void pgtk_verify_gnome_setup();
void pgtk_verify_inited();
void pgtk_verify_not_inited();

void push_Xpseudo32bitstring(void *f, int nelems);

int get_color_from_pikecolor(struct object *o, INT_TYPE *r, INT_TYPE *g, INT_TYPE *b);

int pgtk_signal_func_wrapper(struct signal_data *d, ...);

void pgtk_free_signal_data(struct signal_data *s, GClosure *gcl);
void pgtk_free_object(struct object *o);

void push_gdk_event(GdkEvent *e);

int pgtk_buttonfuncwrapper(GObject *obj, struct signal_data *d,  void *foo);
/*
int signal_func_wrapper(GtkObject *obj, struct signal_data *d,
                        int nparams, GValue *params);
*/

#define pgtk__init_this_object() pgtk__init_object(Pike_fp->current_object)
void pgtk__init_object(struct object *o);

void *get_pgdkobject(struct object *from, struct program *type);
#define get_gdkobject(X,Y) (void *)get_pgdkobject(X,pgdk_##Y##_program)


GObject *get_pgobject(struct object *from, struct program *type);
/*
#define get_pgtkobject(X,Y) get_pgobject(X,Y)
*/

#define get_gobject(from) get_pgobject(from,pg_object_program)
/*
#define get_gtkobject(from) get_pgobject(from,pg_object_program)
*/

void push_gobjectclass(void *obj, struct program *def);
/*
#define push_gtkobjectclass(X,Y) push_gobjectclass(X,Y)
*/
#define push_gobject(o) push_gobjectclass(o,pgtk_type_to_program((void *)o))
/*
#define push_gtkobject(o) push_gobjectclass(o,pgtk_type_to_program((void*)o))
*/

void pgtk_clear_obj_struct(struct object *o);
void pgtk_default__sprintf(int n, int a, int l);

void push_pgdkobject(void *obj, struct program *def);
#define push_gdkobject(X,Y) push_pgdkobject(X,pgdk_##Y##_program)


GdkImage *gdkimage_from_pikeimage(struct object *img, int fast, GdkImage *i);
struct object *pikeimage_from_gdkimage(GdkImage *img);

#ifdef THIS
#undef THIS
#endif

#define THIS ((struct object_wrapper *)Pike_fp->current_storage)
#define THISO ((struct object_wrapper *)Pike_fp->current_storage)->obj


#define RETURN_THIS()  pgtk_return_this(args)

struct my_pixel pgtk_pixel_from_xpixel(unsigned int pix, GdkImage *i);
typedef void *Gdk_Atom;
GdkAtom get_gdkatom(struct object *o);
void pgtk_get_mapping_arg(struct mapping *map,
                          char *name, int type, int madd,
                          void *dest, long *mask, int len);

void pgtk_index_stack(char *with);
void pgtk_get_image_module();

void pgtk_encode_truecolor_masks(struct image *i,
                                 int bitspp,
                                 int pad,
                                 int byteorder,
                                 unsigned int red_mask,
                                 unsigned int green_mask,
                                 unsigned int blue_mask,
                                 unsigned char *buffer,
                                 int debuglen);


#if defined(PGTK_DEBUG) && defined(HAVE_GETHRTIME)
# define TIMER_INIT(X) do { long long cur,last,start; start = gethrtime(); last=start;fprintf(stderr, "%20s ... ",(X))
# define TIMER_END() cur=gethrtime();fprintf(stderr, "%4.1fms (%4.1fms)\n\n",(cur-last)/1000000.0,(cur-start)/1000000.0);} while(0);
# define PFTIME(X) cur=gethrtime();fprintf(stderr, "%4.1fms (%4.1fms)\n%20s ... ",(cur-last)/1000000.0,(cur-start)/1000000.0,(X));last=cur;
# define DEBUG_PF(X) printf X
#else
# define TIMER_INIT(X)
# define PFTIME(X)
# define TIMER_END()
# define DEBUG_PF(X)
#endif

void pgtk_push_gchar(const gchar *s);
gchar *pgtk_get_str(struct svalue *sv);
void pgtk_free_str(gchar *s);
# define PGTK_ISSTR(X) ((X)->type==PIKE_T_STRING)
# define PGTK_GETSTR(X)  pgtk_get_str(X)
# define PGTK_FREESTR(X) pgtk_free_str(X)
# define PGTK_PUSH_GCHAR(X) pgtk_push_gchar(X)
/*
#else
# define PGTK_ISSTR( X ) (((X)->type==PIKE_T_STRING)&&((X)->u.string->size_shift==0))
# define PGTK_GETSTR(X)  ((char*)((X)->u.string->str))
# define PGTK_FREESTR(X)  
# define PGTK_PUSH_GCHAR(X) push_text( X )
#endif
*/

/* Somewhat more complex than one could expect. Consider bignums. */
LONGEST pgtk_get_int(struct svalue *s);
int pgtk_is_int(struct svalue *s);

# define PGTK_ISINT(X)    (((X)->type==PIKE_T_INT) || pgtk_is_int(X))
# define PGTK_GETINT(X)   pgtk_get_int(X)
# define PGTK_PUSH_INT(X) push_int64((LONGEST)(X))

/* Can convert from int to float, and, if bignum is present, bignum to
 * float.
 */
double pgtk_get_float(struct svalue *s);
int pgtk_is_float(struct svalue *s);
#define PGTK_ISFLT(X) pgtk_is_float(X)
#define PGTK_GETFLT(X) pgtk_get_float(X)
int pgtk_last_event_time();

#define PSTR (char*)__pgtk_string_data
#define PGTK_CHECK_TYPE(ob,t) (g_type_is_a(G_TYPE_FROM_INSTANCE(ob),(t)))

#define setprop(x,y) g_object_set(THIS->obj,x,y,NULL)
#define getprop(x,y) g_object_get(THIS->obj,x,y,NULL)

void pgtk_set_property(GObject *g, char *prop, struct svalue *sv);
void pgtk__low_get_property(GObject *g, char *prop);
void pgtk_get_property(GObject *g, char *prop);
void pgtk_destroy_store_data(gpointer data);
void pgtk_set_gvalue(GValue *gv, GType gt, struct svalue *sv);
int pgtk_tree_sortable_callback(GtkTreeModel *model, GtkTreeIter *a,
				GtkTreeIter *b, struct signal_data *d);
