#include "global.h"
RCSID("$Id: xbm.c,v 1.4 1999/05/10 23:03:47 mirar Exp $");

#include "config.h"

#include "interpret.h"
#include "svalue.h"
#include "pike_macros.h"
#include "object.h"
#include "program.h"
#include "array.h"
#include "error.h"
#include "constants.h"
#include "mapping.h"
#include "stralloc.h"
#include "multiset.h"
#include "pike_types.h"
#include "rusage.h"
#include "operators.h"
#include "fsort.h"
#include "callback.h"
#include "gc.h"
#include "backend.h"
#include "main.h"
#include "pike_memory.h"
#include "threads.h"
#include "time_stuff.h"
#include "version.h"
#include "encode.h"
#include "module_support.h"
#include "module.h"
#include "opcodes.h"
#include "cyclic.h"
#include "signal_handler.h"
#include "security.h"
#include "builtin_functions.h"

#include "image.h"
#include "colortable.h"

extern struct program *image_colortable_program;
extern struct program *image_program;

/*
**! module Image
**! submodule XBM
**!
*/


#define SWAP_S(x) ((unsigned short)((((x)&0x00ff)<<8) | ((x)&0xff00)>>8))
#define SWAP_L(x) (SWAP_S((x)>>16)|SWAP_S((x)&0xffff)<<16)

struct buffer
{
  unsigned int len;
  char *str;
};

int buf_search( struct buffer *b, unsigned char match )
{
  unsigned int off = 0;
  if(b->len <= 1)
    return 0;
  while(off < b->len)
  {
    if(b->str[off]  == match)
      break;
    off++;
  }
  off++;
  if(off >= b->len)
    return 0;
  b->str += off;
  b->len -= off;
  return 1;
}

static int buf_getc( struct buffer *fp )
{
  if(fp->len >= 1)
  {
    fp->len--;
    return (int)*((unsigned char *)fp->str++);
  }
  return '0';
}

static int hextoint( int what )
{
  if(what >= '0' && what <= '9')
    return what-'0';
  if(what >= 'a' && what <= 'f')
    return 10+what-'a';
  if(what >= 'A' && what <= 'F')
    return 10+what-'A';
  return 0;
}

static struct object *load_xbm( struct pike_string *data )
{
  int width, height;
  int x, y;
  struct buffer buff;
  struct buffer *b = &buff;
  rgb_group *dest;
  struct object *io;

  buff.str = data->str;
  buff.len = data->len;

  if(!buf_search( b, '#' ) || !buf_search( b, ' ' ) || !buf_search( b, ' ' ))
    error("This is not a XBM image!\n");
  width = atoi(b->str);
  if(width <= 0)
    error("This is not a XBM image!\n");
  if(!buf_search( b, '#' ) || !buf_search( b, ' ' ) || !buf_search( b, ' ' ))
    error("This is not a XBM image!\n");
  height = atoi(b->str);
  if(height <= 0)
    error("This is not a XBM image!\n");
  
  if(!buf_search( b, '{' ))
    error("This is not a XBM image!\n");


  push_int( width );
  push_int( height );
  io = clone_object( image_program, 2 );
  dest = ((struct image *)get_storage(io, image_program))->img;
  /* .. the code below asumes black if the read fails.. */
  for(y=0; y<height; y++)
  {
    int next_byte, cnt;
    for(x=0; x<width;)
    {
      if(buf_search( b, 'x' ))
      {
        next_byte = (hextoint(buf_getc( b ))*0x10) | hextoint(buf_getc( b ));
        for(cnt=0; cnt<8&&x<width; cnt++,x++)
        {
          if((next_byte&(1<<(x%8))))
            dest->r = dest->g = dest->b = 255;
          dest++;
        }
      }
    }
  }
  return io;
}

static struct pike_string *save_xbm( struct image *i, struct pike_string *name )
{
  dynamic_buffer buf;
  char size[32];
  int x, y, first=-1;

#define ccat( X )   low_my_binary_strcat( X, (sizeof(X)-sizeof("")), &buf );

#define cname()  do{                                          \
      if(name)                                                \
        low_my_binary_strcat( name->str, name->len, &buf );   \
      else                                                    \
        ccat( "image" );                                      \
   } while(0)                                                 \



#define OUTPUT_BYTE(X) do{                                              \
      if(!++first)                                                      \
        sprintf( size, " 0x%02x", (X) );                                \
      else                                                              \
        sprintf( size, ",%s0x%02x", (first%12?" ":"\n "), (X) );        \
      (X)=0;                                                            \
      low_my_binary_strcat( size, strlen(size), &buf );                 \
  } while(0)


  initialize_buf(&buf);
  ccat( "#define ");  cname();  ccat( "_width " );
  sprintf( size, "%d\n", i->xsize );
  low_my_binary_strcat( size, strlen(size), &buf );

  ccat( "#define ");  cname();  ccat( "_height " );
  sprintf( size, "%d\n", i->ysize );
  low_my_binary_strcat( size, strlen(size), &buf );

  ccat( "static char " );  cname();  ccat( "_bits[] = {\n" );
  
  for(y=0; y<i->ysize; y++)
  {
    rgb_group *p = i->img+y*i->xsize;
    int next_byte = 0;
    for(x=0; x<i->xsize; x++)
    {
      if((p->r || p->g || p->b ))
        next_byte |= (1<<(x%8));
      if((x % 8) == 7)
        OUTPUT_BYTE( next_byte );
      p++;
    }
    if(i->xsize%8)
      OUTPUT_BYTE( next_byte );
  }
  ccat( "};\n" );
  return low_free_buf(&buf);
}



/*
**! method object decode(string data)
**! 	Decodes a XBM image. 
**!
**! note
**!	Throws upon error in data.
*/
static void image_xbm_decode( INT32 args )
{
  struct pike_string *data;
  struct object *o;
  get_all_args( "Image.XBM.decode", args, "%S", &data );
  o = load_xbm( data );
  pop_n_elems(args);
  push_object( o );
}


/*
**! method object _decode(string data)
**! method object _decode(string data, mapping options)
**! 	Decodes a XBM image to a mapping.
**!
**! 	<pre>
**!         Supported options:
**!        ([
**!           "fg":({fgcolor}),    // Foreground color. Default black
**!           "bg":({bgcolor}),    // Background color. Default white
**!           "invert":1,          // Invert the mask
**!         ])
**!	</pre>
**!
**! note
**!	Throws upon error in data.
*/


static struct pike_string *param_fg;
static struct pike_string *param_bg;
static struct pike_string *param_invert;
static void image_xbm__decode( INT32 args )
{
  struct array *fg = NULL;
  struct array *bg = NULL;
  int invert=0, ele;
  struct pike_string *data;
  struct object *i=NULL, *a;
  get_all_args( "Image.XBM.decode", args, "%S", &data );


  if (args>1)
  {
    if (sp[1-args].type!=T_MAPPING)
      error("Image.XBM._decode: illegal argument 2\n");
      
    push_svalue(sp+1-args);
    ref_push_string(param_fg); 
    f_index(2);
    if(!IS_ZERO(sp-1))
    {
      if(sp[-1].type != T_ARRAY || sp[-1].u.array->size != 3)
        error("Wrong type for foreground. Should be array(int(0..255))"
              " with 3 elements\n");
      for(ele=0; ele<3; ele++)
        if(sp[-1].u.array->item[ele].type != T_INT
           ||sp[-1].u.array->item[ele].u.integer < 0
           ||sp[-1].u.array->item[ele].u.integer > 255)
          error("Wrong type for foreground. Should be array(int(0..255))"
                " with 3 elements\n");
      fg = sp[-1].u.array;
    }
    sp--;

    push_svalue(sp+1-args);
    ref_push_string(param_bg);
    f_index(2);
    if(!IS_ZERO(sp-1))
    {
      if(sp[-1].type != T_ARRAY || sp[-1].u.array->size != 3)
        error("Wrong type for background. Should be array(int(0..255))"
              " with 3 elements\n");
      for(ele=0; ele<3; ele++)
        if(sp[-1].u.array->item[ele].type != T_INT
           ||sp[-1].u.array->item[ele].u.integer < 0
           ||sp[-1].u.array->item[ele].u.integer > 255)
          error("Wrong type for background. Should be array(int(0..255))"
                " with 3 elements\n");
      bg = sp[-1].u.array;
    }
    sp--;
    
    push_svalue(sp+1-args);
    ref_push_string(param_invert);
    f_index(2);
    invert = !IS_ZERO(sp-1);
    sp--;
  }

  a = load_xbm( data );

  if(!fg)
  {
    if(invert)
    {
      apply(a, "invert", 0);
      i = sp[-1].u.object;
      sp--;
    }
    else
    {
      i = a;
      a->refs++;
    }
  } else {
    if(!bg)
    {
      push_int(255);
      push_int(255);
      push_int(255);
      f_aggregate(3);
      bg = sp[-1].u.array;
      sp--;
    }
    if(invert)
    {
      struct array *tmp = fg;
      fg = bg;
      bg = fg;
    }
    apply(a, "xsize", 0);
    apply(a, "ysize", 0);
    push_int( bg->item[0].u.integer );
    push_int( bg->item[1].u.integer );
    push_int( bg->item[2].u.integer );
    i = clone_object( image_program, 5 );
    ref_push_object( i );
    push_int( fg->item[0].u.integer );
    push_int( fg->item[1].u.integer );
    push_int( fg->item[2].u.integer );

    apply( i, "paste_alpha_color", 4 );
  }
  
  pop_n_elems(args);
  push_constant_text( "alpha" );
  push_object( a );
    push_constant_text( "image" );
  if(i)
    push_object( i );
  else
    push_int( 0 );
  f_aggregate_mapping(4);
}




/*
**! method string encode(object image)
**! method string encode(object image, mapping options)
**! 	Encodes a XBM image. 
**!
**!     The <tt>options</tt> argument may be a mapping
**!	containing zero or more encoding options.
**!
**!	<pre>
**!	normal options:
**!	    "name":"xbm_image_name"
**!		The name of the XBM. Defaults to 'image'
**!	</pre>
*/

static struct pike_string *param_name;
void image_xbm_encode( INT32 args )
{
  struct image *img = NULL;
  struct pike_string *name = NULL, *buf;
  if (!args)
    error("Image.XBM.encode: too few arguments\n");
   
  if (sp[-args].type!=T_OBJECT ||
      !(img=(struct image*)
        get_storage(sp[-args].u.object,image_program)))
    error("Image.XBM.encode: illegal argument 1\n");
   
  if (!img->img)
    error("Image.XBM.encode: no image\n");

  if (args>1)
  {
    if (sp[1-args].type!=T_MAPPING)
      error("Image.XBM.encode: illegal argument 2\n");
      
    push_svalue(sp+1-args);
    ref_push_string(param_name); 
    f_index(2);
    if(sp[-1].type == T_STRING)
    {
      if(sp[-1].u.string->size_shift)
        error("The name of the image must be a normal non-wide string (sorry, not my fault)\n");
      name = sp[-1].u.string;
    }
    pop_stack();
  }

  buf = save_xbm( img, name );
  pop_n_elems(args);
  push_string( buf );
}


static struct program *image_encoding_xbm_program=NULL;
void init_image_xbm( )
{
  start_new_program();
  add_function( "_decode", image_xbm__decode, 
                "function(string,mapping|void:mapping(string:object))", 0);
  add_function( "decode", image_xbm_decode, 
                "function(string:object)", 0);
  add_function( "encode", image_xbm_encode,  
                "function(object,mapping|void:string)", 0); 
  image_encoding_xbm_program=end_program();

  push_object(clone_object(image_encoding_xbm_program,0));
  {
    struct pike_string *s=make_shared_string("XBM");
    add_constant(s,sp-1,0);
    free_string(s);
  }
  pop_stack();
  param_name=make_shared_string("name");
  param_fg=make_shared_string("fg");
  param_bg=make_shared_string("bg");
  param_invert=make_shared_string("invert");
}

void exit_image_xbm(void)
{
  if(image_encoding_xbm_program)
  {
    free_program(image_encoding_xbm_program);
    image_encoding_xbm_program=0;
    free_string(param_name);
    free_string(param_fg);
    free_string(param_bg);
    free_string(param_invert);
  }
}
