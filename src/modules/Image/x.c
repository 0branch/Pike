/* $Id: x.c,v 1.6 1997/04/03 07:00:46 mirar Exp $ */

/*
**! module Image
**! class image
*/

#include "global.h"

#include <math.h>
#include <ctype.h>

#include "stralloc.h"
#include "global.h"
RCSID("$Id: x.c,v 1.6 1997/04/03 07:00:46 mirar Exp $");
#include "types.h"
#include "pike_macros.h"
#include "object.h"
#include "constants.h"
#include "interpret.h"
#include "svalue.h"
#include "threads.h"
#include "array.h"
#include "error.h"
#include "port.h"

#include "image.h"
#include "builtin_functions.h"

struct program *image_program;
#define THIS ((struct image *)(fp->current_storage))
#define THISOBJ (fp->current_object)

/*
**! method string cast(string type)
**! returns the image data as a string ("rgbrgbrgb...")
**! see also: toppm, togif, tozbgr, to8bit, to8bit_rgbcube
**! known bugs: always casts to string...
*/

void image_cast(INT32 args)
{
  /* CHECK TYPE TO CAST TO HERE! FIXME FIXME FIXME! */
  pop_n_elems(args);
  push_string(make_shared_binary_string((char *)THIS->img,
					THIS->xsize*THIS->ysize
					*sizeof(rgb_group)));
}

/*
**! method string to8bit(array(array(int)) colors)
**! method string to8bit_fs(array(array(int)) colors)
**! method string to8bit_closest(array(array(int)) colors)
**!     Maps the image to the given colors and returns 
**!	the 8 bit data.
**!
**!	to8bit_fs uses floyd-steinberg dithering
**! returns the calculated string
**! see also: to8bit_rgbcube, tozbgr, map_fast, map_closest, select_colors, tobitmap
*/

void image_to8bit_closest(INT32 args)
{
  struct colortable *ct;
  struct pike_string *res = begin_shared_string((THIS->xsize*THIS->ysize));
  unsigned long i;
  rgb_group *s;
  unsigned char *d;

  if(!res) error("Out of memory\n");

  if (args!=1)
     error("Illegal number of arguments to image->to8bit(COLORTABLE);\n");
  if(sp[-args].type != T_ARRAY)
     error("Wrong type to image->to8bit(COLORTABLE);\n");

  ct=colortable_from_array(sp[-args].u.array,"image->to8bit()\n");

  i=THIS->xsize*THIS->ysize;
  s=THIS->img;
  d=(unsigned char *)res->str;

  THREADS_ALLOW();
  while (i--)
  {
    *d=ct->index[colortable_rgb_nearest(ct,*s)];
    d++; *s++;
  }
  THREADS_DISALLOW();

  colortable_free(ct);

  pop_n_elems(args);
  push_string(end_shared_string(res));
}

void image_to8bit(INT32 args)
{
  struct colortable *ct;
  struct pike_string *res = begin_shared_string((THIS->xsize*THIS->ysize));
  unsigned long i;
  rgb_group *s;
  unsigned char *d;

  if(!res) error("Out of memory\n");

  if (args!=1)
     error("Illegal number of arguments to image->to8bit(COLORTABLE);\n");
  if(sp[-args].type != T_ARRAY)
     error("Wrong type to image->to8bit(COLORTABLE);\n");

  ct=colortable_from_array(sp[-args].u.array,"image->to8bit()\n");

  i=THIS->xsize*THIS->ysize;
  s=THIS->img;
  d=(unsigned char *)res->str;

  THREADS_ALLOW();
  while (i--)
  {
    *d=ct->index[colortable_rgb(ct,*s)];
    d++; *s++;
  }
  THREADS_DISALLOW();

  colortable_free(ct);

  pop_n_elems(args);
  push_string(end_shared_string(res));
}

void image_to8bit_fs(INT32 args)
{
   struct colortable *ct;
   INT32 i,j,xs;
   rgb_group *s;
   struct object *o;
   int *res,w;
   unsigned char *d;
   rgbl_group *errb;
   struct pike_string *sres = begin_shared_string((THIS->xsize*THIS->ysize));
   
   if (!THIS->img) error("no image\n");
   if (args<1
       || sp[-args].type!=T_ARRAY)
      error("illegal argument to image->map_fs()\n");


   res=(int*)xalloc(sizeof(int)*THIS->xsize);
   errb=(rgbl_group*)xalloc(sizeof(rgbl_group)*THIS->xsize);
      
   ct=colortable_from_array(sp[-args].u.array,"image->map_closest()\n");
   pop_n_elems(args);

   for (i=0; i<THIS->xsize; i++)
      errb[i].r=(rand()%(FS_SCALE*2+1))-FS_SCALE,
      errb[i].g=(rand()%(FS_SCALE*2+1))-FS_SCALE,
      errb[i].b=(rand()%(FS_SCALE*2+1))-FS_SCALE;

   i=THIS->ysize;
   s=THIS->img;
   d=(unsigned char *)sres->str;
   w=0;
   xs=THIS->xsize;
   THREADS_ALLOW();
   while (i--)
   {
      image_floyd_steinberg(s,xs,errb,w=!w,res,ct,1);
      for (j=0; j<THIS->xsize; j++)
	 *(d++)=ct->index[res[j]];
      s+=xs;
   }
   THREADS_DISALLOW();

   free(errb);
   free(res);
   colortable_free(ct);
   push_string(end_shared_string(sres));
}


/*
**! method string tozbgr(array(array(int)) colors)
**! returns the image data as a string ("zbgrzbgr...", where z is zero...)
**! see also: cast, to8bit, to8bit_rgbcube, tobitmap
*/

void image_tozbgr(INT32 args)
{
   unsigned char *d;
   rgb_group *s;
   unsigned long i;
   struct pike_string *sres = begin_shared_string(THIS->xsize*THIS->ysize*4);
   
   if (!THIS->img) error("no image\n");
   pop_n_elems(args);

   i=THIS->ysize*THIS->xsize;
   s=THIS->img;
   d=(unsigned char *)sres->str;

   THREADS_ALLOW();
   while (i--)
   {
      *(d++)=0;
      *(d++)=s->b;
      *(d++)=s->g;
      *(d++)=s->r;
      s++;
   }
   THREADS_DISALLOW();

   push_string(end_shared_string(sres));
}

/*
**! method string to8bit_rgbcube(int red,int green,int blue)
**! method string to8bit_rgbcube(int red,int green,int blue,string map)
**! method string to8bit_rgbcube_rdither(int red,int green,int blue)
**! method string to8bit_rgbcube_rdither(int red,int green,int blue,string map)
**!     Maps the image into a colorcube with the given 
**!	dimensions. Red is least significant, blue is most.
**!
**!	The "rdither" type of method uses a random dither algoritm.
**! arg int red
**! arg int green
**! arg int blue
**!	The sides of the colorcube. Not the number of bits!
**! arg string map
**!	Map this position in the colorcube to another value,
**!	ie: say we have position red=1,green=2,blue=3 in a colorcube of
**!	6󬝲, we have the index 1+2*6+3*6*6=121. If the 
**!	map-string contains '�' in position 121, the resulting
**!	byte is '�' or 229.
**! returns the calculated string
**! see also: tozbgr, to8bit, tobitmap
*/

void image_to8bit_rgbcube(INT32 args)
/*

  ->to8bit_rgbcube(int red,int green,int blue,
                [string map])
  
  gives r+red*g+red*green*b       
 */
{
  struct colortable *ct;
  struct pike_string *res = begin_shared_string((THIS->xsize*THIS->ysize));
  unsigned long i;
  rgb_group *s;
  unsigned char *d;
  unsigned char *map=NULL;

  int red,green,blue,redgreen,redgreenblue,hred,hgreen,hblue;
  
  if(!res) error("Out of memory\n");

  if (!THIS->img) 
     error("No image\n");

  if (args<3) 
     error("Too few arguments to image->to8bit_rgbcube()\n");
  
  if (sp[-args].type!=T_INT
      || sp[1-args].type!=T_INT
      || sp[2-args].type!=T_INT) 
     error("Illegal argument(s) to image->to8bit_rgbcube()\n");

  red=sp[-args].u.integer; 	hred=red/2;
  green=sp[1-args].u.integer;	hgreen=green/2;
  blue=sp[2-args].u.integer; 	hblue=blue/2;
  redgreen=red*green;
  redgreenblue=red*green*blue;

  if (args>3)
     if (sp[3-args].type!=T_STRING)
	error("Illegal argument 4 to image->to8bit_rgbcube()"
	      " (expected string or no argument)\n");
     else if (sp[3-args].u.string->len<red*green*blue)
	error("map string is not long enough to image->to8bit_rgbcube()\n");
     else
	map=(unsigned char *)sp[3-args].u.string->str;

  i=THIS->xsize*THIS->ysize;
  s=THIS->img;
  d=(unsigned char *)res->str;

  THREADS_ALLOW();
  if (!map)
     while (i--)
     {
	*(d++)=
	   (unsigned char)( ((s->r*red+hred)>>8)+
			    ((s->g*green+hgreen)>>8)*red+
			    ((s->b*blue+hblue)>>8)*redgreen );
	s++;
     }
  else
     while (i--)
     {
	*(d++)=
	   map[ ((s->r*red+hred)>>8)+
	        ((s->g*green+hgreen)>>8)*red+
	        ((s->b*blue+hblue)>>8)*redgreen ];
	s++;
     }
  THREADS_DISALLOW();

  pop_n_elems(args);
  push_string(end_shared_string(res));
}

void image_to8bit_rgbcube_rdither(INT32 args)
/*
  ->to8bit_rgbcube_rdither(int red,int green,int blue,
                [string map])
  
  gives r+red*g+red*green*b       
 */
{
  struct colortable *ct;
  struct pike_string *res = begin_shared_string((THIS->xsize*THIS->ysize));
  unsigned long i;
  rgb_group *s;
  unsigned char *d;
  unsigned char *map=NULL;

  int red,green,blue,redgreen,redgreenblue,rmax,gmax,bmax;
  
  if(!res) error("Out of memory\n");

  if (!THIS->img) 
     error("No image\n");

  if (args<3) 
     error("Too few arguments to image->to8bit_rgbcube()\n");
  
  if (sp[-args].type!=T_INT
      || sp[1-args].type!=T_INT
      || sp[2-args].type!=T_INT) 
     error("Illegal argument(s) to image->to8bit_rgbcube()\n");

  red=sp[-args].u.integer; 	rmax=red*255;
  green=sp[1-args].u.integer;	gmax=green*255;
  blue=sp[2-args].u.integer; 	bmax=blue*255;
  redgreen=red*green;
  redgreenblue=red*green*blue;

  if (args>3)
     if (sp[3-args].type!=T_STRING)
	error("Illegal argument 4 to image->to8bit_rgbcube()"
	      " (expected string or no argument)\n");
     else if (sp[3-args].u.string->len<red*green*blue)
	error("map string is not long enough to image->to8bit_rgbcube()\n");
     else
	map=(unsigned char *)sp[3-args].u.string->str;

  i=THIS->xsize*THIS->ysize;
  s=THIS->img;
  d=(unsigned char *)res->str;

  THREADS_ALLOW();
  if (!map)
     while (i--)
     {
       unsigned int tal = my_rand();

       int r=(s->r*red)+(tal&255);
       int g=(s->g*green)+((tal>>8)&255);
       int b=(s->b*blue+((tal>>16)&255));
       if(r>rmax) r=rmax; if(g>gmax) g=gmax; if(b>bmax) b=bmax;
       *(d++)= (unsigned char)((r>>8)+(g>>8)*red+(b>>8)*redgreen);
       s++;
     }
  else
     while (i--)
     {
       unsigned int tal = my_rand();

       int r=(s->r*red)+(tal&255);
       int g=(s->g*green)+((tal>>8)&255);
       int b=(s->b*blue+((tal>>16)&255));
       if(r>rmax) r=rmax; if(g>gmax) g=gmax; if(b>bmax) b=bmax;
       *(d++)= map[ (r>>8)+(g>>8)*red+(b>>8)*redgreen ];
       s++;
     }
  THREADS_DISALLOW();

  pop_n_elems(args);
  push_string(end_shared_string(res));
}

/*
**! method string tobitmap();
**!     Maps the image to a bitmap.
**!
**!	Bit 0 is the leftmost pixel, and the rows are aligned to 
**!	bytes.
**!
**!	Any pixel value other then black results in a set bit.
**! returns the calculated string
**! see also: tozbgr, to8bit, to8bit_rgbcube, cast
*/

void image_tobitmap(INT32 args)
{
   int xs;
   int i,j,left,bit,dbits;
   struct pike_string *res;
   unsigned char *d;
   rgb_group *s;

   pop_n_elems(args);
   if (!THIS->img) error("No image.\n");

   xs=(THIS->xsize+7)>>3;

   res=begin_shared_string(xs*THIS->ysize);
   d=(unsigned char *)res->str;

   s=THIS->img;

   j=THIS->ysize;
   while (j--)
   {
      i=THIS->xsize;
      while (i)
      {
	 left=8;
	 bit=1;
	 dbits=0;
	 while (left-- && i)
	 {
	    if (s->r||s->g||s->b) dbits|=bit;
	    bit<<=1;
	    s++;
	    i--;
	 }
	 *(d++)=(unsigned char)dbits;
      }
   }

   push_string(end_shared_string(res));
}

