#include "global.h"
#include "image_machine.h"
#include <math.h>
#include <ctype.h>

#include "stralloc.h"
RCSID("$Id: pvr.c,v 1.13 2001/04/13 16:20:13 grubba Exp $");
#include "pike_macros.h"
#include "object.h"
#include "constants.h"
#include "interpret.h"
#include "svalue.h"
#include "threads.h"
#include "array.h"
#include "mapping.h"
#include "pike_error.h"
#include "operators.h"
#include "stralloc.h"
#include "builtin_functions.h"
#include "module_support.h"


#include "image.h"

#include "encodings.h"

/* MUST BE INCLUDED LAST */
#include "module_magic.h"

extern struct program *image_program;

/*
**! module Image
**! submodule PVR
**!
**! 	Handle encoding and decoding of PVR images.
**! 
**! 	PVR is the texture format of the NEC PowerVR system
**! 	It is a rather simple, uncompressed, truecolor
**!     format. 
*/

/*
**! method object decode(string data)
**! method object decode_alpha(string data)
**! method mapping decode_header(string data)
**! method mapping _decode(string data)
**!	decodes a PVR image
**!
**!	The <ref>decode_header</ref> and <ref>_decode</ref>
**!	has these elements:
**!
**!	<pre>
**!        "image":object            - image object    \- not decode_header
**!	   "alpha":object            - decoded alpha   /
**!	   
**!	   "type":"image/x-pvr"      - image type
**!	   "xsize":int               - horisontal size in pixels
**!	   "ysize":int               - vertical size in pixels
**!	   "attr":int		     - texture attributes
**!	   "global_index":int	     - global index (if present)
**!	</pre>
**!	  
**!
**! method string encode(object image)
**! method string encode(object image,mapping options)
**!	encode a PVR image
**!
**!	options is a mapping with optional values:
**!	<pre>
**!	   "alpha":object            - alpha channel
**!	   "global_index":int	     - global index
**!	</pre>
*/

#define MODE_ARGB1555 0x00
#define MODE_RGB565   0x01
#define MODE_ARGB4444 0x02
#define MODE_YUV422   0x03
#define MODE_BUMPMAP  0x04
#define MODE_RGB555   0x05
#define MODE_ARGB8888 0x06

#define MODE_TWIDDLE            0x0100
#define MODE_TWIDDLE_MIPMAP     0x0200
#define MODE_COMPRESSED         0x0300
#define MODE_COMPRESSED_MIPMAP  0x0400
#define MODE_CLUT4              0x0500
#define MODE_CLUT4_MIPMAP       0x0600
#define MODE_CLUT8              0x0700
#define MODE_CLUT8_MIPMAP       0x0800
#define MODE_RECTANGLE          0x0900
#define MODE_STRIDE             0x0b00
#define MODE_TWIDDLED_RECTANGLE 0x0d00

static INT32 twiddletab[1024];
static int twiddleinited=0;

static void init_twiddletab()
{
  int x;
  for(x=0; x<1024; x++)
    twiddletab[x] = (x&1)|((x&2)<<1)|((x&4)<<2)|((x&8)<<3)|((x&16)<<4)|
      ((x&32)<<5)|((x&64)<<6)|((x&128)<<7)|((x&256)<<8)|((x&512)<<9);
  twiddleinited=1;
}

static int pvr_check_alpha(struct image *alpha)
{
  int r=0;
  INT32 cnt;
  rgb_group *p;
  if(alpha==NULL)
    return 0;
  for(cnt=alpha->xsize*alpha->ysize, p=alpha->img; cnt--; p++)
    if(p->g < 16)
      r=1;
    else if(p->g < 240)
      return 2;
  return r;
}

static void pvr_encode_rect(INT32 attr, rgb_group *src, unsigned char *dst,
			    unsigned int h, unsigned int w)
{
  INT32 cnt = h * w;
  switch(attr&0xff) {
   case MODE_RGB565:
     while(cnt--) {
       unsigned int p =
	 ((src->r&0xf8)<<8)|((src->g&0xfc)<<3)|((src->b&0xf8)>>3);
       *dst++=p&0xff;
       *dst++=(p&0xff00)>>8;
       src++;
     }
     break;
  }
}

static void pvr_encode_twiddled(INT32 attr, rgb_group *src, unsigned char *d,
				unsigned int sz)
{
  unsigned int x, y;
  switch(attr&0xff) {
   case MODE_RGB565:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned char *dst = d+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 unsigned int p =
	   ((src->r&0xf8)<<8)|((src->g&0xfc)<<3)|((src->b&0xf8)>>3);
	 *dst++=p&0xff;
	 *dst=(p&0xff00)>>8;
	 src++;
       }
     }
     break;
  }
}

static void pvr_encode_alpha_rect(INT32 attr, rgb_group *src, rgb_group *alpha,
				  unsigned char *dst,
				  unsigned int h, unsigned int w)
{
  INT32 cnt = h * w;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     while(cnt--) {
       unsigned int p =
	 ((src->r&0xf8)<<7)|((src->g&0xf8)<<2)|((src->b&0xf8)>>3);
       if(alpha->g&0x80)
	 p |= 0x8000;
       *dst++=p&0xff;
       *dst++=(p&0xff00)>>8;
       src++;
       alpha++;
     }
     break;
   case MODE_ARGB4444:
     while(cnt--) {
       unsigned int p =
	 ((alpha->g&0xf0)<<8)|
	 ((src->r&0xf0)<<4)|(src->g&0xf0)|((src->b&0xf0)>>4);
       *dst++=p&0xff;
       *dst++=(p&0xff00)>>8;
       src++;
       alpha++;
     }
     break;
  }
}

static void pvr_encode_alpha_twiddled(INT32 attr, rgb_group *src,
				      rgb_group *alpha, unsigned char *d,
				      unsigned int sz)
{
  unsigned int x, y;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned char *dst = d+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 unsigned int p =
	   ((src->r&0xf8)<<7)|((src->g&0xf8)<<2)|((src->b&0xf8)>>3);
	 if(alpha->g&0x80)
	   p |= 0x8000;
	 *dst++=p&0xff;
	 *dst++=(p&0xff00)>>8;
	 src++;
	 alpha++;
       }
     }
     break;
   case MODE_ARGB4444:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned char *dst = d+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 unsigned int p =
	   ((alpha->g&0xf0)<<8)|
	   ((src->r&0xf0)<<4)|(src->g&0xf0)|((src->b&0xf0)>>4);
	 *dst++=p&0xff;
	 *dst++=(p&0xff00)>>8;
	 src++;
	 alpha++;
       }
     }
     break;
  }
}

void image_pvr_f_encode(INT32 args)
{
  struct object *imgo;
  struct mapping *optm = NULL;
  struct image *alpha = NULL, *img;
  INT32 gbix=0, sz, attr=0;
  int has_gbix=0, twiddle=0;
  struct pike_string *res;
  unsigned char *dst;

  get_all_args("Image.PVR.encode", args, (args>1 && !IS_ZERO(&sp[1-args])?
					  "%o%m":"%o"), &imgo, &optm);

  if((img=(struct image*)get_storage(imgo, image_program))==NULL)
    Pike_error("Image.PVR.encode: illegal argument 1\n");

  if(optm != NULL) {
    struct svalue *s;
    if((s = simple_mapping_string_lookup(optm, "alpha"))!=NULL && !IS_ZERO(s))
      if(s->type != T_OBJECT ||
	 (alpha=(struct image*)get_storage(s->u.object, image_program))==NULL)
	Pike_error("Image.PVR.encode: option (arg 2) \"alpha\" has illegal type\n");
    if((s = simple_mapping_string_lookup(optm, "global_index"))!=NULL &&
       !IS_UNDEFINED(s)) {
      if(s->type == T_INT) {
	gbix = s->u.integer;
	has_gbix=1;
      }
      else
	Pike_error("Image.PVR.encode: option (arg 2) \"global_index\" has illegal type\n");
    }
  }

  if (!img->img)
    Pike_error("Image.PVR.encode: no image\n");
  if (alpha && !alpha->img)
    Pike_error("Image.PVR.encode: no alpha image\n");

  if (alpha && (alpha->xsize != img->xsize || alpha->ysize != img->ysize))
    Pike_error("Image.PVR.encode: alpha and image size differ\n");

  res = begin_shared_string(8+(sz=8+2*img->xsize*img->ysize)+(has_gbix? 12:0));
  dst = STR0(res);

  switch(pvr_check_alpha(alpha)) {
   case 0:
     alpha = NULL;
     attr = MODE_RGB565;
     break;
   case 1:
     attr = MODE_ARGB1555;
     break;
   case 2:
     attr = MODE_ARGB4444;
     break;
  }

  if(img->xsize == img->ysize && img->xsize>=8 && img->ysize<=1024 &&
     !(img->xsize&(img->xsize-1))) {
    attr |= MODE_TWIDDLE;
    twiddle = 1;
  } else
    attr |= MODE_RECTANGLE;

  if(has_gbix) {
    *dst++ = 'G';
    *dst++ = 'B';
    *dst++ = 'I';
    *dst++ = 'X';
    *dst++ = 4;
    *dst++ = 0;
    *dst++ = 0;
    *dst++ = 0;
    *dst++ = (gbix&0x000000ff);
    *dst++ = (gbix&0x0000ff00)>>8;
    *dst++ = (gbix&0x00ff0000)>>16;
    *dst++ = (gbix&0xff000000)>>24;
  }

  *dst++ = 'P';
  *dst++ = 'V';
  *dst++ = 'R';
  *dst++ = 'T';
  *dst++ = (sz&0x000000ff);
  *dst++ = (sz&0x0000ff00)>>8;
  *dst++ = (sz&0x00ff0000)>>16;
  *dst++ = (sz&0xff000000)>>24;
  *dst++ = (attr&0x000000ff);
  *dst++ = (attr&0x0000ff00)>>8;
  *dst++ = (attr&0x00ff0000)>>16;
  *dst++ = (attr&0xff000000)>>24;
  *dst++ = (img->xsize&0x00ff);
  *dst++ = (img->xsize&0xff00)>>8;
  *dst++ = (img->ysize&0x00ff);
  *dst++ = (img->ysize&0xff00)>>8;

  if(twiddle && !twiddleinited)
    init_twiddletab();

  if(alpha != NULL)
    if(twiddle)
      pvr_encode_alpha_twiddled(attr, img->img, alpha->img, dst, img->xsize);
    else
      pvr_encode_alpha_rect(attr, img->img, alpha->img, dst,
			    img->ysize, img->xsize);
  else
    if(twiddle)
      pvr_encode_twiddled(attr, img->img, dst, img->xsize);
    else
      pvr_encode_rect(attr, img->img, dst, img->ysize, img->xsize);

  pop_n_elems(args);
  push_string(end_shared_string(res));
}

static void pvr_decode_rect(INT32 attr, unsigned char *src, rgb_group *dst,
			    INT32 stride, unsigned int h, unsigned int w)
{
  INT32 cnt = h * w;
  switch(attr&0xff) {
   case MODE_ARGB1555:
   case MODE_RGB555:
     while(cnt--) {
       unsigned int p = src[0]|(src[1]<<8);
       dst->r = ((p&0x7c00)>>7)|((p&0x7000)>>12);
       dst->g = ((p&0x03e0)>>2)|((p&0x0380)>>7);
       dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
       src+=2;
       dst++;
     }
     break;
   case MODE_RGB565:
     while(cnt--) {
       unsigned int p = src[0]|(src[1]<<8);
       dst->r = ((p&0xf800)>>8)|((p&0xe000)>>13);
       dst->g = ((p&0x07e0)>>3)|((p&0x0600)>>9);
       dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
       src+=2;
       dst++;
     }
     break;
   case MODE_ARGB4444:
     while(cnt--) {
       unsigned int p = src[0]|(src[1]<<8);
       dst->r = ((p&0x0f00)>>4)|((p&0x0f00)>>8);
       dst->g = (p&0x00f0)|((p&0x00f0)>>4);
       dst->b = ((p&0x000f)<<4)|(p&0x000f);
       src+=2;
       dst++;
     }
     break;
  }
}

static void pvr_decode_twiddled(INT32 attr, unsigned char *s, rgb_group *dst,
				INT32 stride, unsigned int sz)
{
  unsigned int x, y;
  unsigned char *src;
  switch(attr&0xff) {
   case MODE_ARGB1555:
   case MODE_RGB555:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned int p;
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst->r = ((p&0x7c00)>>7)|((p&0x7000)>>12);
	 dst->g = ((p&0x03e0)>>2)|((p&0x0380)>>7);
	 dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
	 dst++;
       }
       dst += stride;
     }
     break;
   case MODE_RGB565:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned int p;
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst->r = ((p&0xf800)>>8)|((p&0xe000)>>13);
	 dst->g = ((p&0x07e0)>>3)|((p&0x0600)>>9);
	 dst->b = ((p&0x001f)<<3)|((p&0x001c)>>2);
	 dst++;
       }
       dst += stride;
     }
     break;
   case MODE_ARGB4444:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 unsigned int p;
	 src = s+(((twiddletab[x]<<1)|twiddletab[y])<<1);
	 p = src[0]|(src[1]<<8);
	 dst->r = ((p&0x0f00)>>4)|((p&0x0f00)>>8);
	 dst->g = (p&0x00f0)|((p&0x00f0)>>4);
	 dst->b = ((p&0x000f)<<4)|(p&0x000f);
	 dst++;
       }
       dst += stride;
     }
     break;
  }
}

static void pvr_decode_alpha_rect(INT32 attr, unsigned char *src,
				  rgb_group *dst, INT32 stride,
				  unsigned int h, unsigned int w)
{
  INT32 cnt = h * w;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     while(cnt--) {
       if(src[1]&0x80)
	 dst->r = dst->g = dst->b = 0xff;
       else
	 dst->r = dst->g = dst->b = 0;
       src+=2;
       dst++;
     }
     break;
   case MODE_ARGB4444:
     while(cnt--) {
       int a = src[1]&0xf0;
       a |= a>>4;
       dst->r = dst->g = dst->b = a;
       src+=2;
       dst++;
     }
     break;
  }
}

static void pvr_decode_alpha_twiddled(INT32 attr, unsigned char *s,
				      rgb_group *dst, INT32 stride,
				      unsigned int sz)
{
  unsigned int x, y;
  unsigned char *src;
  switch(attr&0xff) {
   case MODE_ARGB1555:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 if(s[(((twiddletab[x]<<1)|twiddletab[y])<<1)+1]&0x80)
	   dst->r = dst->g = dst->b = 0xff;
	 else
	   dst->r = dst->g = dst->b = 0;
	 dst++;
       }
       dst += stride;
     }
     break;
   case MODE_ARGB4444:
     for(y=0; y<sz; y++) {
       for(x=0; x<sz; x++) {
	 int a = s[(((twiddletab[x]<<1)|twiddletab[y])<<1)+1]&0xf0;
	 a |= a>>4;
	 dst->r = dst->g = dst->b = a;
	 dst++;
       }
       dst += stride;
     }
     break;
  }
}

void img_pvr_decode(INT32 args,int header_only)
{
   struct pike_string *str;
   unsigned char *s;
   int n = 0;
   ptrdiff_t len;
   INT32 attr;
   unsigned int h, w, x;

   get_all_args("Image.PVR._decode", args, "%S", &str);
   s = (unsigned char *)str->str;
   len = str->len;
   pop_n_elems(args-1);

   if(len >= 12 && !strncmp((char *)s, "GBIX", 4)) {
     INT32 l = s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
     if(l>=4 && l<=len-8) {
       push_text("global_index");
       push_int(s[8]|(s[9]<<8)|(s[10]<<16)|(s[11]<<24));
       n++;
       len -= l+8;
       s += l+8;
     }
   }

   if(len < 16 || strncmp((char *)s, "PVRT", 4))
     Pike_error("not a PVR texture\n");
   else {
     INT32 l = s[4]|(s[5]<<8)|(s[6]<<16)|(s[7]<<24);
     if(l+8>len)
       Pike_error("file is truncated\n");
     else if(l<8)
       Pike_error("invalid PVRT chunk length\n");
     len = l+8;
   }

   push_text("type");
   push_text("image/x-pvr");
   n++;

   attr = s[8]|(s[9]<<8)|(s[10]<<16)|(s[11]<<24);
   w = s[12]|(s[13]<<8);
   h = s[14]|(s[15]<<8);

   s += 16;
   len -= 16;

   push_text("attr");
   push_int(attr);
   n++;
   push_text("xsize");
   push_int(w);
   n++;   
   push_text("ysize");
   push_int(h);
   n++;   

   if(!header_only) {
     int twiddle=0, hasalpha=0, bpp=0;
     struct object *o;
     struct image *img;
     INT32 mipmap=0;

     switch(attr&0xff00) {
      case MODE_TWIDDLE_MIPMAP:
	mipmap = 1;
      case MODE_TWIDDLE:
	twiddle = 1;
	if(w != h || w<8 || w>1024 || (w&(w-1)))
	  Pike_error("invalid size for twiddle texture\n");
      case MODE_RECTANGLE:
      case MODE_STRIDE:
	break;
      case MODE_TWIDDLED_RECTANGLE:
	twiddle = 1;
	if((w<h && (w<8 || w>1024 || (w&(w-1)) || h%w)) ||
	   (h>=w && (h<8 || h>1024 || (h&(h-1)) || w%h)))
	  Pike_error("invalid size for twiddle rectangle texture\n");
	break;
      case MODE_COMPRESSED:
      case MODE_COMPRESSED_MIPMAP:
	Pike_error("compressed PVRs not supported\n");
      case MODE_CLUT4:
      case MODE_CLUT4_MIPMAP:
      case MODE_CLUT8:
      case MODE_CLUT8_MIPMAP:
   	Pike_error("palette PVRs not supported\n");
      default:
	Pike_error("unknown PVR format\n");
     }

     switch(attr&0xff) {
      case MODE_ARGB1555:
      case MODE_ARGB4444:
	hasalpha=1;
      case MODE_RGB565:
      case MODE_RGB555:
	bpp=2; break;
      case MODE_YUV422:
	Pike_error("YUV mode not supported\n");
      case MODE_ARGB8888:
	Pike_error("ARGB8888 mode not supported\n");
      case MODE_BUMPMAP:
	Pike_error("bumpmap mode not supported\n");
      default:
	Pike_error("unknown PVR color mode\n");
     }

     if(mipmap) /* Just skip everything except the largest version */
       for(x=w; x>>=1;)
	 mipmap += x*x;

     if(len < (INT32)(bpp*(h*w+mipmap)))
       Pike_error("short PVRT chunk\n");

     s += bpp*mipmap;

     push_text("image");
     push_int(w);
     push_int(h);
     o=clone_object(image_program,2);
     img=(struct image*)get_storage(o,image_program);
     push_object(o);
     n++;

     if(twiddle && !twiddleinited)
       init_twiddletab();
     
     if(twiddle)
       if(h<w)
	 for(x=0; x<w; x+=h)
	   pvr_decode_twiddled(attr, s+bpp*h*x, img->img+x, w-h, h);
       else
	 for(x=0; x<h; x+=w)
	   pvr_decode_twiddled(attr, s+bpp*w*x, img->img+w*x, 0, w);
     else
       pvr_decode_rect(attr, s, img->img, 0, h, w);

     if(hasalpha) {

       push_text("alpha");
       push_int(w);
       push_int(h);
       o=clone_object(image_program,2);
       img=(struct image*)get_storage(o,image_program);
       push_object(o);
       n++;
       
       if(twiddle)
	 if(h<w)
	   for(x=0; x<w; x+=h)
	     pvr_decode_alpha_twiddled(attr, s+bpp*h*x, img->img+x, w-h, h);
	 else
	   for(x=0; x<h; x+=w)
	     pvr_decode_alpha_twiddled(attr, s+bpp*w*x, img->img+w*x, 0, w);
       else
	 pvr_decode_alpha_rect(attr, s, img->img, 0, h, w);
     }

   }

   f_aggregate_mapping(2*n);

#ifdef PVR_DEBUG
   print_svalue(stderr, sp-1);
#endif

   stack_swap();
   pop_stack();
}

static void image_pvr_f_decode(INT32 args)
{
   img_pvr_decode(args,0);
   push_string(make_shared_string("image"));
   f_index(2);
}

static void image_pvr_f_decode_alpha(INT32 args)
{
   img_pvr_decode(args,0);
   push_string(make_shared_string("alpha"));
   f_index(2);
}

void image_pvr_f_decode_header(INT32 args)
{
   img_pvr_decode(args,1);
}

void image_pvr_f__decode(INT32 args)
{
   img_pvr_decode(args,0);
}

void init_image_pvr()
{
  ADD_FUNCTION( "decode",  image_pvr_f_decode,  tFunc(tStr,tObj), 0);
  ADD_FUNCTION( "decode_alpha",  image_pvr_f_decode_alpha,  tFunc(tStr,tObj), 0);
  ADD_FUNCTION( "_decode", image_pvr_f__decode, tFunc(tStr,tMapping), 0);
  ADD_FUNCTION( "encode",  image_pvr_f_encode,  tFunc(tObj tOr(tVoid,tMapping),tStr), 0);
  ADD_FUNCTION( "decode_header", image_pvr_f_decode_header, tFunc(tStr,tMapping), 0);
}

void exit_image_pvr()
{
}
