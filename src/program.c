/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/
/**/
#include "global.h"
RCSID("$Id: program.c,v 1.233 2001/08/02 23:10:40 hubbe Exp $");
#include "program.h"
#include "object.h"
#include "dynamic_buffer.h"
#include "pike_types.h"
#include "stralloc.h"
#include "las.h"
#include "language.h"
#include "lex.h"
#include "pike_macros.h"
#include "fsort.h"
#include "error.h"
#include "docode.h"
#include "interpret.h"
#include "hashtable.h"
#include "main.h"
#include "gc.h"
#include "threads.h"
#include "constants.h"
#include "operators.h"
#include "builtin_functions.h"
#include "stuff.h"
#include "mapping.h"
#include "cyclic.h"
#include "security.h"
#include "pike_types.h"

#include <errno.h>
#include <fcntl.h>


#undef ATTRIBUTE
#define ATTRIBUTE(X)


/* #define COMPILER_DEBUG */
/* #define PROGRAM_BUILD_DEBUG */

#ifdef COMPILER_DEBUG
#define CDFPRINTF(X)	fprintf X
#else /* !COMPILER_DEBUG */
#define CDFPRINTF(X)
#endif /* COMPILER_DEBUG */

/*
 * Define the size of the cache that is used for method lookup.
 */
#define FIND_FUNCTION_HASHSIZE 4711


#define STRUCT
#include "compilation.h"

#define DECLARE
#include "compilation.h"


struct pike_string *this_program_string=0;

char *lfun_names[] = {
  "__INIT",
  "create",
  "destroy",
  "`+",
  "`-",
  "`&",
  "`|",
  "`^",
  "`<<",
  "`>>",
  "`*",
  "`/",
  "`%",
  "`~",
  "`==",
  "`<",
  "`>",
  "__hash",
  "cast",
  "`!",
  "`[]",
  "`[]=",
  "`->",
  "`->=",
  "_sizeof",
  "_indices",
  "_values",
  "`()",
  "``+",
  "``-",
  "``&",
  "``|",
  "``^",
  "``<<",
  "``>>",
  "``*",
  "``/",
  "``%",
  "`+=",
  "_is_type",
  "_sprintf",
  "_equal",
};

/* mapping(string:type) */
static struct mapping *lfun_types;

static char *raw_lfun_types[] = {
  tFuncV(tNone,tVoid,tVoid),	/* "__INIT", */
  tFuncV(tNone,tZero,tVoid),	/* "create", */
  tFuncV(tNone,tVoid,tVoid),	/* "destroy", */
  tFuncV(tNone,tZero,tMix),	/* "`+", */
  tFuncV(tNone,tZero,tMix),	/* "`-", */
  tFuncV(tNone,tZero,tMix),	/* "`&", */
  tFuncV(tNone,tZero,tMix),	/* "`|", */
  tFuncV(tNone,tZero,tMix),	/* "`^", */
  tFuncV(tZero,tVoid,tMix),	/* "`<<", */
  tFuncV(tZero,tVoid,tMix),	/* "`>>", */
  tFuncV(tNone,tZero,tMix),	/* "`*", */
  tFuncV(tNone,tZero,tMix),	/* "`/", */
  tFuncV(tNone,tZero,tMix),	/* "`%", */
  tFuncV(tNone,tVoid,tMix),	/* "`~", */
  tFuncV(tMix,tVoid,tInt),	/* "`==", */
  tFuncV(tMix,tVoid,tInt),	/* "`<", */
  tFuncV(tMix,tVoid,tInt),	/* "`>", */
  tFuncV(tNone,tVoid,tInt),	/* "__hash", */
  tFuncV(tString,tVoid,tMix),	/* "cast", */
  tFuncV(tNone,tVoid,tInt),	/* "`!", */
  tFuncV(tZero,tVoid,tMix),	/* "`[]", */
  tFuncV(tZero tSetvar(0,tZero),tVoid,tVar(0)),	/* "`[]=", */
  tFuncV(tStr,tVoid,tMix),	/* "`->", */
  tFuncV(tStr tSetvar(0,tZero),tVoid,tVar(0)),	/* "`->=", */
  tFuncV(tNone,tVoid,tInt),	/* "_sizeof", */
  tFuncV(tNone,tVoid,tArray),	/* "_indices", */
  tFuncV(tNone,tVoid,tArray),	/* "_values", */
  tFuncV(tNone,tZero,tMix),	/* "`()", */
  tFuncV(tNone,tZero,tMix),	/* "``+", */
  tFuncV(tNone,tZero,tMix),	/* "``-", */
  tFuncV(tNone,tZero,tMix),	/* "``&", */
  tFuncV(tNone,tZero,tMix),	/* "``|", */
  tFuncV(tNone,tZero,tMix),	/* "``^", */
  tFuncV(tZero,tVoid,tMix),	/* "``<<", */
  tFuncV(tZero,tVoid,tMix),	/* "``>>", */
  tFuncV(tNone,tZero,tMix),	/* "``*", */
  tFuncV(tNone,tZero,tMix),	/* "``/", */
  tFuncV(tNone,tZero,tMix),	/* "``%", */
  tFuncV(tZero,tVoid,tMix),	/* "`+=", */
  tFuncV(tStr,tVoid,tInt),	/* "_is_type", */
  tFuncV(tInt tOr(tMap(tStr,tInt),tVoid),tVoid,tStr),	/* "_sprintf", */
  tFuncV(tMix,tVoid,tInt),	/* "_equal", */
};

struct program *first_program = 0;
static int current_program_id=0x10000;

struct program *new_program=0;
struct object *fake_object=0;
struct program *malloc_size_program=0;
struct object *error_handler=0;

int compiler_pass;
int compilation_depth;
long local_class_counter;
int catch_level;
struct compiler_frame *compiler_frame=0;
static INT32 last_line = 0;
static INT32 last_pc = 0;
static struct pike_string *last_file = 0;
dynamic_buffer used_modules;
INT32 num_used_modules;
static struct mapping *resolve_cache=0;
static struct mapping *module_index_cache=0;

int get_small_number(char **q);

/* So what if we don't have templates? / Hubbe */

#ifdef PIKE_DEBUG
#define CHECK_FOO(NUMTYPE,TYPE,NAME)				\
  if(malloc_size_program-> PIKE_CONCAT(num_,NAME) < new_program-> PIKE_CONCAT(num_,NAME))	\
    fatal("new_program->num_" #NAME " is out of order\n");	\
  if(new_program->flags & PROGRAM_OPTIMIZED)			\
    fatal("Tried to reallocate fixed program.\n")

#else
#define CHECK_FOO(NUMTYPE,TYPE,NAME)
#endif

#define FOO(NUMTYPE,TYPE,NAME)						\
void PIKE_CONCAT(add_to_,NAME) (TYPE ARG) {				\
  CHECK_FOO(NUMTYPE,TYPE,NAME);						\
  if(malloc_size_program->PIKE_CONCAT(num_,NAME) ==			\
     new_program->PIKE_CONCAT(num_,NAME)) {				\
    void *tmp;								\
    malloc_size_program->PIKE_CONCAT(num_,NAME) *= 2;			\
    malloc_size_program->PIKE_CONCAT(num_,NAME)++;			\
    tmp=realloc((char *)new_program->NAME,				\
                sizeof(TYPE) *						\
		malloc_size_program->PIKE_CONCAT(num_,NAME));		\
    if(!tmp) fatal("Out of memory.\n");					\
    new_program->NAME=tmp;						\
  }									\
  new_program->NAME[new_program->PIKE_CONCAT(num_,NAME)++]=(ARG);	\
}

#include "program_areas.h"

#define FOO(NUMTYPE,TYPE,NAME)							\
void PIKE_CONCAT(low_add_to_,NAME) (struct program_state *state,		\
                                    TYPE ARG) {					\
  if(state->malloc_size_program->PIKE_CONCAT(num_,NAME) ==			\
     state->new_program->PIKE_CONCAT(num_,NAME)) {				\
    void *tmp;									\
    state->malloc_size_program->PIKE_CONCAT(num_,NAME) *= 2;			\
    state->malloc_size_program->PIKE_CONCAT(num_,NAME)++;			\
    tmp=realloc((char *)state->new_program->NAME,				\
                sizeof(TYPE) *							\
		state->malloc_size_program->PIKE_CONCAT(num_,NAME));		\
    if(!tmp) fatal("Out of memory.\n");						\
    state->new_program->NAME=tmp;						\
  }										\
  state->new_program->NAME[state->new_program->PIKE_CONCAT(num_,NAME)++]=(ARG);	\
}

#include "program_areas.h"


void ins_int(INT32 i, void (*func)(char tmp))
{
  int e;
  unsigned char *p = (unsigned char *)&i;
  for(e=0;e<(long)sizeof(i);e++) {
    func(p[e]);
  }
}

void ins_short(INT16 i, void (*func)(char tmp))
{
  int e;
  unsigned char *p = (unsigned char *)&i;
  for(e=0;e<(long)sizeof(i);e++) {
    func(p[e]);
  }
}

void use_module(struct svalue *s)
{
  if( (1<<s->type) & (BIT_MAPPING | BIT_OBJECT | BIT_PROGRAM))
  {
    num_used_modules++;
    assign_svalue_no_free((struct svalue *)
			  low_make_buf_space(sizeof(struct svalue),
					     &used_modules), s);
    if(module_index_cache)
    {
      free_mapping(module_index_cache);
      module_index_cache=0;
    }
  }else{
    yyerror("Module is neither mapping nor object");
  }
}

void unuse_modules(INT32 howmany)
{
  if(!howmany) return;
#ifdef PIKE_DEBUG
  if(howmany *sizeof(struct svalue) > used_modules.s.len)
    fatal("Unusing too many modules.\n");
#endif
  num_used_modules-=howmany;
  low_make_buf_space(-sizeof(struct svalue)*howmany, &used_modules);
  free_svalues((struct svalue *)low_make_buf_space(0, &used_modules),
	       howmany,
	       BIT_MAPPING | BIT_OBJECT | BIT_PROGRAM);
  if(module_index_cache)
  {
    free_mapping(module_index_cache);
    module_index_cache=0;
  }
}

int low_find_shared_string_identifier(struct pike_string *name,
				      struct program *prog);



static struct node_s *index_modules(struct pike_string *ident,
				    struct mapping **module_index_cache,
				    int num_used_modules,
				    struct svalue *modules)
{
  struct node_s *ret;
  JMP_BUF tmp;

  if(*module_index_cache)
  {
    struct svalue *tmp=low_mapping_string_lookup(*module_index_cache,ident);
    if(tmp)
    {
      if(!(IS_ZERO(tmp) && tmp->subtype==1))
	return mksvaluenode(tmp);
      return 0;
    }
  }

/*  fprintf(stderr,"index_module: %s\n",ident->str); */


  if(SETJMP(tmp))
  {
    ONERROR tmp;
    SET_ONERROR(tmp,exit_on_error,"Error in handle_error in master object!");
    assign_svalue_no_free(sp++, & throw_value);
    APPLY_MASTER("handle_error", 1);
    pop_stack();
    UNSET_ONERROR(tmp);
    yyerror("Couldn't index module.");
  }else{
    int e=num_used_modules;
    modules-=num_used_modules;
    while(--e>=0)
    {
      push_svalue(modules+e);
      ref_push_string(ident);
      f_index(2);

      if(!IS_UNDEFINED(sp-1))
      {
	UNSETJMP(tmp);
	if(!*module_index_cache)
	  *module_index_cache=allocate_mapping(10);
	mapping_string_insert(*module_index_cache, ident, sp-1);
	ret=mksvaluenode(sp-1);
	pop_stack();
	return ret;
      }
      pop_stack();
    }
  }
  UNSETJMP(tmp);

/*  fprintf(stderr,"***Undefined.\n"); */

  return 0;
}


struct node_s *find_module_identifier(struct pike_string *ident,
				      int see_inherit)
{
  struct node_s *ret;

  struct svalue *modules=(struct svalue *)
    (used_modules.s.str + used_modules.s.len);

  if((ret=index_modules(ident,
			&module_index_cache,
			num_used_modules,
			modules))) return ret;
  modules-=num_used_modules;

  {
    struct program_state *p=previous_program_state;
    int n;
    for(n=0;n<compilation_depth;n++,p=p->previous)
    {
      int i;
      if(see_inherit)
      {
	i=really_low_find_shared_string_identifier(ident,
						   p->new_program,
						   SEE_STATIC);
	if(i!=-1)
	{
	  struct identifier *id;
	  id=ID_FROM_INT(p->new_program, i);
	  return mkexternalnode(n, i, id);
	}
      }

      if((ret=index_modules(ident,
			    &p->module_index_cache,
			    p->num_used_modules,
			    modules))) return ret;
      modules-=p->num_used_modules;
#ifdef PIKE_DEBUG
      if( ((char *)modules ) < used_modules.s.str)
	fatal("Modules out of whack!\n");
#endif
    }
  }

  /* Handle this_program */
  if (ident == this_program_string) {
    struct svalue s;
    s.type=T_PROGRAM;
    s.u.program=new_program;
    return mkconstantsvaluenode(&s);
  }

  if(resolve_cache)
  {
    struct svalue *tmp=low_mapping_string_lookup(resolve_cache,ident);
    if(tmp)
    {
      if(!(IS_ZERO(tmp) && tmp->subtype==1))
	return mkconstantsvaluenode(tmp);

      return 0;
    }
  }

  if(!num_parse_error && get_master())
  {
    DECLARE_CYCLIC();
    node *ret=0;
    if(BEGIN_CYCLIC(ident, lex.current_file))
    {
      my_yyerror("Recursive module dependency in %s.",
		 ident->str);
    }else{
      int i;
      SET_CYCLIC_RET(1);

      ref_push_string(ident);
      ref_push_string(lex.current_file);

      if(error_handler && (i=find_identifier("resolv",error_handler->prog))!=-1)
      {
	safe_apply_low(error_handler, i, 2);
      }else{
	SAFE_APPLY_MASTER("resolv", 2);
      }

      if(throw_value.type == T_STRING)
      {
	if(compiler_pass==2)
	  my_yyerror("%s",throw_value.u.string->str);
      }
      else
      {
	if(!resolve_cache)
	  resolve_cache=dmalloc_touch(struct mapping *, allocate_mapping(10));
	mapping_string_insert(resolve_cache,ident,sp-1);

	if(!(IS_ZERO(sp-1) && sp[-1].subtype==1))
	{
	  ret=mkconstantsvaluenode(sp-1);
	}
      }
      pop_stack();
      END_CYCLIC();
    }
    if(ret) return ret;
  }

  return 0;
}

struct program *parent_compilation(int level)
{
  int n;
  struct program_state *p=previous_program_state;
  for(n=0;n<level;n++)
  {
    if(n>=compilation_depth) return 0;
    p=p->previous;
    if(!p) return 0;
  }
  return p->new_program;
}

#define ID_TO_PROGRAM_CACHE_SIZE 512
struct program *id_to_program_cache[ID_TO_PROGRAM_CACHE_SIZE];

struct program *id_to_program(INT32 id)
{
  struct program *p;
  INT32 h;
  if(!id) return 0;
  h=id & (ID_TO_PROGRAM_CACHE_SIZE-1);

  if((p=id_to_program_cache[h]))
    if(p->id==id)
      return p;

  if(id)
    {
      for(p=first_program;p;p=p->next)
	{
	  if(id==p->id)
	    {
	      id_to_program_cache[h]=p;
	      return p;
	    }
	}
    }
  return 0;
}

/* Here starts routines which are used to build new programs */

/* Re-allocate all the memory in the program in one chunk. because:
 * 1) The individual blocks are munch bigger than they need to be
 * 2) cuts down on malloc overhead (maybe)
 * 3) localizes memory access (decreases paging)
 */
void optimize_program(struct program *p)
{
  SIZE_T size=0;
  char *data;

  /* Already done (shouldn't happen, but who knows?) */
  if(p->flags & PROGRAM_OPTIMIZED) return;

#define FOO(NUMTYPE,TYPE,NAME) \
  size=DO_ALIGN(size, ALIGNOF(TYPE)); \
  size+=p->PIKE_CONCAT(num_,NAME)*sizeof(p->NAME[0]);
#include "program_areas.h"

  data=malloc(size);
  if(!data) return; /* We are out of memory, but we don't care! */

  size=0;

#define FOO(NUMTYPE,TYPE,NAME) \
  size=DO_ALIGN(size, ALIGNOF(TYPE)); \
  MEMCPY(data+size,p->NAME,p->PIKE_CONCAT(num_,NAME)*sizeof(p->NAME[0])); \
  dmfree((char *)p->NAME); \
  p->NAME=(TYPE *)(data+size); \
  size+=p->PIKE_CONCAT(num_,NAME)*sizeof(p->NAME[0]);
#include "program_areas.h"

  p->total_size=size + sizeof(struct program);

  p->flags |= PROGRAM_OPTIMIZED;
}

/* internal function to make the index-table */
int program_function_index_compare(const void *a,const void *b)
{
  return
    my_order_strcmp(ID_FROM_INT(new_program, *(unsigned short *)a)->name,
		    ID_FROM_INT(new_program, *(unsigned short *)b)->name);
}

void fixate_program(void)
{
  INT32 i,e,t;
  if(new_program->flags & PROGRAM_FIXED) return;
#ifdef PIKE_DEBUG
  if(new_program->flags & PROGRAM_OPTIMIZED)
    fatal("Cannot fixate optimized program\n");
#endif

  /* Ok, sort for binsearch */
  for(e=i=0;i<(int)new_program->num_identifier_references;i++)
  {
    struct reference *funp;
    struct identifier *fun;
    funp=new_program->identifier_references+i;
    if(funp->id_flags & (ID_HIDDEN|ID_STATIC)) continue;
    if(funp->id_flags & ID_INHERITED)
    {
      if(funp->id_flags & ID_PRIVATE) continue;
      fun=ID_FROM_PTR(new_program, funp);
/*	  if(fun->func.offset == -1) continue; * prototype */

      /* check for multiple definitions */
      for(t=i+1;t>=0 && t<(int)new_program->num_identifier_references;t++)
      {
	struct reference *funpb;
	struct identifier *funb;

	funpb=new_program->identifier_references+t;
	if(funpb->id_flags & (ID_HIDDEN|ID_STATIC)) continue;
	funb=ID_FROM_PTR(new_program,funpb);
	/* if(funb->func.offset == -1) continue; * prototype */
	if(fun->name==funb->name) t=-10;
      }
      if(t<0) continue;
    }
    add_to_identifier_index(i);
  }
  fsort((void *)new_program->identifier_index,
	new_program->num_identifier_index,
	sizeof(unsigned short),(fsortfun)program_function_index_compare);


  /* Yes, it is supposed to start at 1  /Hubbe */
  for(i=1;i<NUM_LFUNS;i++) {
    new_program->lfuns[i] = low_find_lfun(new_program, i);
  }

  new_program->flags |= PROGRAM_FIXED;

#ifdef DEBUG_MALLOC
  {
#define DBSTR(X) ((X)?(X)->str:"")
    int e,v;
    struct memory_map *m=dmalloc_alloc_mmap( DBSTR(lex.current_file),
					     lex.current_line);
    for(e=0;e<new_program->num_inherits;e++)
    {
      struct inherit *i=new_program->inherits+e;
      char *tmp;
      char buffer[50];

      for(v=0;v<i->prog->num_variable_index;v++)
      {
	int d=i->prog->variable_index[v];
	struct identifier *id=i->prog->identifiers+d;

	dmalloc_add_mmap_entry(m,
			       id->name->str,
			       OFFSETOF(object,storage) + i->storage_offset + id->func.offset,
			       sizeof_variable(id->run_time_type),
			       1, /* count */
			       0,0);
      }

      if(i->name)
      {
	tmp=i->name->str;
      }else if(!(tmp=dmalloc_find_name(i->prog))){
	/* Didn't find a given name, revert to ad-hoc method */
	INT32 line,pos;
	
	for(pos=0;pos<100;pos++)
	{
	  tmp=get_line(i->prog->program+pos, i->prog, &line);
	  if(tmp && line) break;
	  if(pos+1>=(long)i->prog->num_program) break;
	}
	if(!(tmp && line))
	{
	  sprintf(buffer,"inherit[%d]",e);
	  tmp=buffer;
	}
      }
      dmalloc_add_mmap_entry(m,
			     tmp,
			     OFFSETOF(object, storage) + i->storage_offset,
			     i->prog->storage_needed - i->prog->inherits[0].storage_offset,
			     1, /* count */
			     0,0);

    }
    dmalloc_set_mmap_template(new_program, m);
  }
#endif
}

struct program *low_allocate_program(void)
{
  struct program *p;
  p=ALLOC_STRUCT(program);
  MEMSET(p, 0, sizeof(struct program));
  p->alignment_needed=1;

  GC_ALLOC(p);
  p->refs=1;
  p->id=++current_program_id;

  if((p->next=first_program)) first_program->prev=p;
  first_program=p;
  GETTIMEOFDAY(& p->timestamp);
  INITIALIZE_PROT(p);
  return p;
}

/*
 * Start building a new program
 */
void low_start_new_program(struct program *p,
			   struct pike_string *name,
			   int flags,
			   int *idp)
{
  int e,id=0;
  struct svalue tmp;

#if 0
#ifdef SHARED_NODES
  if (!node_hash.table) {
    node_hash.table = malloc(sizeof(node *)*16411);
    if (!node_hash.table) {
      fatal("Out of memory!\n");
    }
    MEMSET(node_hash.table, 0, sizeof(node *)*16411);
    node_hash.size = 16411;
  }
#endif /* SHARED_NODES */
#endif /* 0 */

  /* We don't want to change thread, but we don't want to
   * wait for the other threads to complete.
   */
  low_init_threads_disable();

  compilation_depth++;

  CDFPRINTF((stderr, "th(%ld) low_start_new_program() pass=%d: compilation_depth:%d\n",
	     (long)th_self(),compilation_depth,compiler_pass));

  tmp.type=T_PROGRAM;
  if(!p)
  {
    p=low_allocate_program();
    if(name)
    {
      tmp.u.program=p;
      id=add_constant(name, &tmp, flags & ~ID_EXTERN);
    }
    e=1;
  }else{
    tmp.u.program=p;
    add_ref(p);
    if(name)
    {
      struct identifier *i;
      id=isidentifier(name);
      if (id < 0)
	fatal("Program constant disappeared in second pass.\n");
      i=ID_FROM_INT(new_program, id);
      free_string(i->type);
      i->type=get_type_of_svalue(&tmp);
    }
    e=2;
  }
  if(idp) *idp=id;

  init_type_stack();

#define PUSH
#include "compilation.h"

  compiler_pass=e;

  num_used_modules=0;

  if(p && (p->flags & PROGRAM_FINISHED))
  {
    yyerror("Pass2: Program already done");
    p=0;
  }

  malloc_size_program = ALLOC_STRUCT(program);
#ifdef PIKE_DEBUG
  fake_object=(struct object *)xalloc(sizeof(struct object) + 256*sizeof(struct svalue));
  /* Stipple to find illegal accesses */
  MEMSET(fake_object,0x55,sizeof(struct object) + 256*sizeof(struct svalue));
#else
  fake_object=ALLOC_STRUCT(object);
#endif
  GC_ALLOC(fake_object);

  fake_object->next=fake_object;
  fake_object->prev=fake_object;
  fake_object->refs=1;
  fake_object->parent=0;
  fake_object->parent_identifier=0;
  fake_object->prog=p;
  add_ref(p);

#ifdef PIKE_DEBUG
  fake_object->program_id=p->id;
#endif

#ifdef PIKE_SECURITY
  fake_object->prot=0;
#endif

  if(name)
  {
    if((fake_object->parent=previous_program_state->fake_object))
      add_ref(fake_object->parent);
    fake_object->parent_identifier=id;
  }

  new_program=p;

#ifdef PROGRAM_BUILD_DEBUG
  if (name) {
    fprintf (stderr, "%.*sstarting program %d (pass=%d): ",
	     compilation_depth, "                ", new_program->id, compiler_pass);
    push_string (name);
    print_svalue (stderr, --sp);
    putc ('\n', stderr);
  }
  else
    fprintf (stderr, "%.*sstarting program %d (pass=%d)\n",
	     compilation_depth, "                ", new_program->id, compiler_pass);
#endif

  if(new_program->program)
  {
#define FOO(NUMTYPE,TYPE,NAME) \
    malloc_size_program->PIKE_CONCAT(num_,NAME)=new_program->PIKE_CONCAT(num_,NAME);
#include "program_areas.h"


    {
      INT32 line=0, off=0;
      char *file=0;
      char *cnt=new_program->linenumbers;

      while(cnt < new_program->linenumbers + new_program->num_linenumbers)
      {
	if(*cnt == 127)
	{
	  file=cnt+1;
	  cnt=file+strlen(file)+1;
	}
	off+=get_small_number(&cnt);
	line+=get_small_number(&cnt);
      }
      last_line=line;
      last_pc=off;
      if(file)
      {
	if(last_file) free_string(last_file);
	last_file=make_shared_string(file);
      }
    }

  }else{
    static struct pike_string *s;
    struct inherit i;

#define START_SIZE 64
#define FOO(NUMTYPE,TYPE,NAME) \
    malloc_size_program->PIKE_CONCAT(num_,NAME)=START_SIZE; \
    new_program->NAME=(TYPE *)xalloc(sizeof(TYPE) * START_SIZE);
#include "program_areas.h"

    i.prog=new_program;
    i.identifier_level=0;
    i.storage_offset=0;
    i.inherit_level=0;
    i.parent=0;
    i.parent_identifier=-1;
    i.parent_offset=-18;
    i.name=0;
    add_to_inherits(i);
  }


  init_node=0;
  num_parse_error=0;

  push_compiler_frame(0);
  add_ref(compiler_frame->current_return_type=void_type_string);
}

void debug_start_new_program(PROGRAM_LINE_ARGS)
{
  CDFPRINTF((stderr,
	     "th(%ld) start_new_program(): threads_disabled:%d, compilation_depth:%d\n",
	     (long)th_self(),threads_disabled, compilation_depth));

  low_start_new_program(0,0,0,0);
#ifdef PIKE_DEBUG
  {
    struct pike_string *s=make_shared_string(file);
    store_linenumber(line,s);
    free_string(s);
    debug_malloc_name(new_program, file, line);
  }
#endif
}


void really_free_program(struct program *p)
{
  unsigned INT16 e;

  if(id_to_program_cache[p->id & (ID_TO_PROGRAM_CACHE_SIZE-1)]==p)
    id_to_program_cache[p->id & (ID_TO_PROGRAM_CACHE_SIZE-1)]=0;

  if(p->strings)
    for(e=0; e<p->num_strings; e++)
      if(p->strings[e])
	free_string(p->strings[e]);

  if(p->identifiers)
  {
    for(e=0; e<p->num_identifiers; e++)
    {
      if(p->identifiers[e].name)
	free_string(p->identifiers[e].name);
      if(p->identifiers[e].type)
	free_string(p->identifiers[e].type);
    }
  }

  if(p->constants)
  {
    for(e=0;e<p->num_constants;e++)
    {
      free_svalue(& p->constants[e].sval);
      if(p->constants[e].name) free_string(p->constants[e].name);
    }
  }

  if(p->inherits)
    for(e=0; e<p->num_inherits; e++)
    {
      if(p->inherits[e].name)
	free_string(p->inherits[e].name);
      if(e)
      {
	if(p->inherits[e].prog)
	  free_program(p->inherits[e].prog);
      }
      if(p->inherits[e].parent)
	free_object(p->inherits[e].parent);
    }

  if(p->prev)
    p->prev->next=p->next;
  else
    first_program=p->next;

  if(p->next)
    p->next->prev=p->prev;

  if(p->flags & PROGRAM_OPTIMIZED)
  {
    if(p->program)
      dmfree(p->program);
#define FOO(NUMTYPE,TYPE,NAME) p->NAME=0;
#include "program_areas.h"
  }else{
#define FOO(NUMTYPE,TYPE,NAME) \
    if(p->NAME) { dmfree((char *)p->NAME); p->NAME=0; }
#include "program_areas.h"
  }

  FREE_PROT(p);
  dmfree((char *)p);

  GC_FREE();
}

#ifdef PIKE_DEBUG
void dump_program_desc(struct program *p)
{
  int e,d,q;
/*  fprintf(stderr,"Program '%s':\n",p->name->str); */

  fprintf(stderr,"All inherits:\n");
  for(e=0;e<p->num_inherits;e++)
  {
    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"%3d:\n",e);

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"inherited program: %d\n",p->inherits[e].prog->id);

    if(p->inherits[e].name)
    {
      for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
      fprintf(stderr,"name  : %s\n",p->inherits[e].name->str);
    }

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"inherit_level: %d\n",p->inherits[e].inherit_level);

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"identifier_level: %d\n",p->inherits[e].identifier_level);

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"parent_identifier: %d\n",p->inherits[e].parent_identifier);

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"parent_offset: %d\n",p->inherits[e].parent_offset);

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"storage_offset: %d\n",p->inherits[e].storage_offset);

    for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"parent: %p\n",p->inherits[e].parent);

    if(p->inherits[e].parent &&
      p->inherits[e].parent->prog &&
      p->inherits[e].parent->prog->num_linenumbers>1)
    {
      for(d=0;d<p->inherits[e].inherit_level;d++) fprintf(stderr,"  ");
      fprintf(stderr,"parent: %s\n",p->inherits[e].parent->prog->linenumbers+1);
    }
  }

  fprintf(stderr,"All identifiers:\n");
  for(e=0;e<(int)p->num_identifier_references;e++)
  {
    fprintf(stderr,"%3d:",e);
    for(d=0;d<INHERIT_FROM_INT(p,e)->inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"%s;\n",ID_FROM_INT(p,e)->name->str);
  }
  fprintf(stderr,"All sorted identifiers:\n");
  for(q=0;q<(int)p->num_identifier_index;q++)
  {
    e=p->identifier_index[q];
    fprintf(stderr,"%3d (%3d):",e,q);
    for(d=0;d<INHERIT_FROM_INT(p,e)->inherit_level;d++) fprintf(stderr,"  ");
    fprintf(stderr,"%s;\n", ID_FROM_INT(p,e)->name->str);
  }
}
#endif

static void toss_compilation_resources(void)
{
  if(fake_object)
  {
    free_program(fake_object->prog);
    fake_object->prog=0;
    free_object(fake_object);
    fake_object=0;
  }

  free_program(new_program);
  new_program=0;

  if(malloc_size_program)
    {
      dmfree((char *)malloc_size_program);
      malloc_size_program=0;
    }

  if(module_index_cache)
  {
    free_mapping(module_index_cache);
    module_index_cache=0;
  }

  while(compiler_frame)
    pop_compiler_frame();

  if(last_file)
  {
    free_string(last_file);
    last_file=0;
  }

  unuse_modules(num_used_modules);
}

int sizeof_variable(int run_time_type)
{
  switch(run_time_type)
  {
    case T_FUNCTION:
    case T_MIXED: return sizeof(struct svalue);
    case T_FLOAT: return sizeof(FLOAT_TYPE);
    case T_INT: return sizeof(INT_TYPE);
    default: return sizeof(char *);
  }
}

static int alignof_variable(int run_time_type)
{
  switch(run_time_type)
  {
    case T_FUNCTION:
    case T_MIXED: return ALIGNOF(struct svalue);
    case T_FLOAT: return ALIGNOF(FLOAT_TYPE);
    case T_INT: return ALIGNOF(INT_TYPE);
    default: return ALIGNOF(char *);
  }
}

#ifdef PIKE_DEBUG

void check_program(struct program *p)
{
  INT32 size;
  unsigned INT32 checksum, e;
  int variable_positions[1024];

  if(p->flags & PROGRAM_AVOID_CHECK) return;

  for(e=0;e<NELEM(variable_positions);e++)
    variable_positions[e]=-1;

  if(p->id > current_program_id)
    fatal("Program id is out of sync! (p->id=%d, current_program_id=%d)\n",p->id,current_program_id);

  if(p->refs <=0)
    fatal("Program has zero refs.\n");

  if(p->next && p->next->prev != p)
    fatal("Program ->next->prev != program.\n");

  if(p->prev)
  {
    if(p->prev->next != p)
      fatal("Program ->prev->next != program.\n");
  }else{
    if(first_program != p)
      fatal("Program ->prev == 0 but first_program != program.\n");
  }

  if(p->id > current_program_id || p->id <= 0)
    fatal("Program id is wrong.\n");

  if(p->storage_needed < 0)
    fatal("Program->storage_needed < 0.\n");

  if(p->num_identifier_index > p->num_identifier_references)
    fatal("Too many identifier index entries in program!\n");

  for(e=0;e<p->num_constants;e++)
  {
    check_svalue(& p->constants[e].sval);
    if(p->constants[e].name) check_string(p->constants[e].name);
  }

  for(e=0;e<p->num_strings;e++)
    check_string(p->strings[e]);

  for(e=0;e<p->num_inherits;e++)
  {
    if(!p->inherits[e].prog) 
    {
      /* This inherit is not yet initialized, ignore rest of tests.. */
      return;
    }

    if(p->inherits[e].storage_offset < 0)
      fatal("Inherit->storage_offset is wrong.\n");

    if(p->inherits[e].prog &&
       p->inherits[e].storage_offset + STORAGE_NEEDED(p->inherits[e].prog) >
       p->storage_needed)
      fatal("Not enough room allocated by inherit!\n");

    if(e)
    {
      if(p->inherits[e-1].storage_offset >
	 p->inherits[e].storage_offset)
	fatal("Overlapping inherits! (1)\n");

      if(p->inherits[e-1].prog &&
	 p->inherits[e-1].inherit_level >= p->inherits[e].inherit_level &&
	 ( p->inherits[e-1].storage_offset +
	   STORAGE_NEEDED(p->inherits[e-1].prog)) >
	 p->inherits[e].storage_offset)
	fatal("Overlapping inherits! (3)\n");
    }
  }


  if(p->flags & PROGRAM_FINISHED)
  for(e=0;e<p->num_identifiers;e++)
  {
    check_string(p->identifiers[e].name);
    check_string(p->identifiers[e].type);

    if(p->identifiers[e].identifier_flags & ~IDENTIFIER_MASK)
      fatal("Unknown flags in identifier flag field.\n");

    if(p->identifiers[e].run_time_type!=T_MIXED)
      check_type(p->identifiers[e].run_time_type);

    if(IDENTIFIER_IS_VARIABLE(p->identifiers[e].identifier_flags))
    {
      if( (p->identifiers[e].func.offset + OFFSETOF(object,storage)) &
	 (alignof_variable(p->identifiers[e].run_time_type)-1))
      {
	fatal("Variable %s offset is not properly aligned (%d).\n",p->identifiers[e].name->str,p->identifiers[e].func.offset);
      }
    }
  }

  for(e=0;e<p->num_identifier_references;e++)
  {
    struct identifier *i;
    if(p->identifier_references[e].inherit_offset > p->num_inherits)
      fatal("Inherit offset is wrong!\n");

    if(!p->inherits[p->identifier_references[e].inherit_offset].prog)
    {
      if(!(p->flags & PROGRAM_FINISHED))
	continue;

      fatal("p->inherit[%d].prog = NULL!\n",p->identifier_references[e].inherit_offset);
    }

    if(p->identifier_references[e].identifier_offset >
       p->inherits[p->identifier_references[e].inherit_offset].prog->num_identifiers)
      fatal("Identifier offset is wrong!\n");

    i=ID_FROM_INT(p, e);

    if( !(i->identifier_flags & (IDENTIFIER_FUNCTION | IDENTIFIER_CONSTANT)))
    {
      unsigned int q, size;
      /* Variable */
      int offset = INHERIT_FROM_INT(p, e)->storage_offset+i->func.offset;
      size=sizeof_variable(i->run_time_type);

      if((offset+size > (unsigned int)p->storage_needed) || offset<0)
	fatal("Variable outside storage! (%s)\n",i->name->str);

      for(q=0;q<size;q++)
      {
	if(offset+q >= NELEM(variable_positions)) break;

	if(variable_positions[offset+q] != -1)
	{
	  if(ID_FROM_INT(p,variable_positions[offset+q])->run_time_type !=
	     i->run_time_type)
	  {
	    fatal("Variable '%s' and '%s' overlap\n",
		  ID_FROM_INT(p,variable_positions[offset+q])->name->str,
		  i->name->str);
	  }
	}
	variable_positions[offset+q]=e;
      }
    }
  }

  for(e=0;e<p->num_identifier_index;e++)
  {
    if(p->identifier_index[e] > p->num_identifier_references)
      fatal("Program->identifier_indexes[%ld] is wrong\n",(long)e);
  }

}
#endif

struct program *end_first_pass(int finish)
{
  int e;
  struct program *prog;
  struct pike_string *s;

  MAKE_CONSTANT_SHARED_STRING(s,"__INIT");


  /* Collect references to inherited __INIT functions */
  for(e=new_program->num_inherits-1;e;e--)
  {
    int id;
    if(new_program->inherits[e].inherit_level!=1) continue;
    id=low_reference_inherited_identifier(0, e, s, SEE_STATIC);
    if(id!=-1)
    {
      init_node=mknode(F_COMMA_EXPR,
		       mkcastnode(void_type_string,
				  mkapplynode(mkidentifiernode(id),0)),
		       init_node);
    }
  }

  /*
   * Define the __INIT function, but only if there was any code
   * to initialize.
   */

  if(init_node)
  {
    union idptr tmp;
    e=dooptcode(s,
		mknode(F_COMMA_EXPR,
		       init_node,mknode(F_RETURN,mkintnode(0),0)),
		function_type_string,
		ID_STATIC);
    init_node=0;
  }else{
    e=-1;
  }
  new_program->lfuns[LFUN___INIT]=e;

  free_string(s);

  pop_compiler_frame(); /* Pop __INIT local variables */

  if(num_parse_error > 0)
  {
    prog=0;
  }else{
    prog=new_program;
    add_ref(prog);

#ifdef PIKE_DEBUG
    check_program(prog);
    if(l_flag)
      dump_program_desc(prog);
#endif

    new_program->flags |= PROGRAM_PASS_1_DONE;

    if(finish)
    {
      fixate_program();
      optimize_program(new_program);
      new_program->flags |= PROGRAM_FINISHED;
    }

  }

#ifdef PROGRAM_BUILD_DEBUG
  fprintf (stderr, "%.*sfinishing program %d (pass=%d)\n",
	   compilation_depth, "                ", new_program->id, compiler_pass);
#endif

  toss_compilation_resources();

  CDFPRINTF((stderr,
	     "th(%ld),end_first_pass(): compilation_depth:%d, compiler_pass:%d\n",
	     (long)th_self(), compilation_depth, compiler_pass));

  if(!compiler_frame && (compiler_pass==2 || !prog) && resolve_cache)
  {
    free_mapping(dmalloc_touch(struct mapping *, resolve_cache));
    resolve_cache=0;
  }

#ifdef SHARED_NODES
  /* free(node_hash.table); */
#endif /* SHARED_NODES */

#define POP
#include "compilation.h"

  exit_type_stack();

  compilation_depth--;

  exit_threads_disable(NULL);

  free_all_nodes();

  return prog;
}

/*
 * Finish this program, returning the newly built program
 */
struct program *debug_end_program(void)
{
  return end_first_pass(1);
}


/*
 * Allocate needed for this program in the object structure.
 * An offset to the data is returned.
 */
SIZE_T low_add_storage(SIZE_T size, SIZE_T alignment, int modulo_orig)
{
  long offset;
  int modulo;

  if(!size) return new_program->storage_needed;

#ifdef PIKE_DEBUG
  if(alignment <=0 || (alignment & (alignment-1)) || alignment > 256)
    fatal("Alignment must be 1,2,4,8,16,32,64,128 or 256 not %d\n",alignment);
#endif
  modulo=( modulo_orig+OFFSETOF(object,storage) ) % alignment;

  offset=DO_ALIGN(new_program->storage_needed-modulo,alignment)+modulo;

  if(!new_program->storage_needed) {
    /* Shouldn't new_program->storage_needed be set here?
     * Otherwise the debug code below ought to be trigged.
     * But since it isn't, I guess this is dead code?
     *	/grubba 1999-09-28
     *
     * No, the below offset represents the storage in the beginning
     * of obj->storage which is not used because of alignment constraints.
     * However, for historical reasons, prog->storage_offset needs to
     * contain this unused space as well. This means that the real
     * space used by all variables in an object is really:
     * o->prog->storage_needed - o->prog->inherits[0].storage_offset,
     * This can also be written as STORAGE_NEEDED(o->prog)
     * STORAGE_NEEDED() is defined in program.h.
     * /Hubbe 1999-09-29
     *
     * Oops, seems I read the test below the wrong way around.
     *	/grubba 1999-09-29
     */
    new_program->inherits[0].storage_offset=offset;
  }

  if(new_program->alignment_needed<alignment)
    new_program->alignment_needed=alignment;

#ifdef PIKE_DEBUG
  if(offset < new_program->storage_needed)
    fatal("add_storage failed horribly!\n");

  if( (offset + OFFSETOF(object,storage) - modulo_orig ) % alignment )
    fatal("add_storage failed horribly(2) %ld %ld %ld %ld!\n",
	  (long)offset,
	  (long)OFFSETOF(object,storage),
	  (long)modulo_orig,
	  (long)alignment
	  );

#endif

  new_program->storage_needed = offset + size;

  return (SIZE_T) offset;
}


/*
 * set a callback used to initialize clones of this program
 * the init function is called at clone time
 */
void set_init_callback(void (*init)(struct object *))
{
  new_program->init=init;
}

/*
 * set a callback used to de-initialize clones of this program
 * the exit function is called at destruct
 */
void set_exit_callback(void (*exit)(struct object *))
{
  new_program->exit=exit;
}

/*
 * This callback is called to allow the object to mark all internal
 * structures as 'used'.
 */
void set_gc_mark_callback(void (*m)(struct object *))
{
  new_program->gc_marked=m;
}

/*
 * Called for all objects and inherits in first pass of gc()
 */
void set_gc_check_callback(void (*m)(struct object *))
{
  new_program->gc_check_func=m;
}

int low_reference_inherited_identifier(struct program_state *q,
				       int e,
				       struct pike_string *name,
				       int flags)
{
  struct program *np=q?q->new_program:new_program;
  struct reference funp;
  struct program *p;
  int i,d;

  p=np->inherits[e].prog;
  i=find_shared_string_identifier(name,p);
  if(i==-1)
  {
    i=really_low_find_shared_string_identifier(name,p, flags);
    if(i==-1) return -1;
  }

  if(p->identifier_references[i].id_flags & ID_HIDDEN)
    return -1;

  if(p->identifier_references[i].id_flags & ID_PRIVATE)
    if(!(flags & SEE_PRIVATE))
      return -1;

  funp=p->identifier_references[i];
  funp.inherit_offset+=e;
  funp.id_flags|=ID_HIDDEN;

  for(d=0;d<(int)np->num_identifier_references;d++)
  {
    struct reference *fp;
    fp=np->identifier_references+d;

    if(!MEMCMP((char *)fp,(char *)&funp,sizeof funp)) return d;
  }

  if(q)
    low_add_to_identifier_references(q,funp);
  else
    add_to_identifier_references(funp);
  return np->num_identifier_references -1;
}

node *reference_inherited_identifier(struct pike_string *super_name,
				   struct pike_string *function_name)
{
  int n,e,id;
  struct program_state *state=previous_program_state;

  struct program *p;


#ifdef PIKE_DEBUG
  if(function_name!=debug_findstring(function_name))
    fatal("reference_inherited_function on nonshared string.\n");
#endif

  p=new_program;

  for(e=p->num_inherits-1;e>0;e--)
  {
    if(p->inherits[e].inherit_level!=1) continue;
    if(!p->inherits[e].name) continue;

    if(super_name)
      if(super_name != p->inherits[e].name)
	continue;

    id=low_reference_inherited_identifier(0,
					  e,
					  function_name,
					  SEE_STATIC);

    if(id!=-1)
      return mkidentifiernode(id);

    if(ISCONSTSTR(function_name,"`->") ||
       ISCONSTSTR(function_name,"`[]"))
    {
      return mknode(F_MAGIC_INDEX,mknewintnode(e),mknewintnode(0));
    }

    if(ISCONSTSTR(function_name,"`->=") ||
       ISCONSTSTR(function_name,"`[]="))
    {
      return mknode(F_MAGIC_SET_INDEX,mknewintnode(e),mknewintnode(0));
    }
  }


  for(n=0;n<compilation_depth;n++,state=state->previous)
  {
    struct program *p=state->new_program;

    for(e=p->num_inherits-1;e>0;e--)
    {
      if(p->inherits[e].inherit_level!=1) continue;
      if(!p->inherits[e].name) continue;

      if(super_name)
	if(super_name != p->inherits[e].name)
	  continue;

      id=low_reference_inherited_identifier(state,e,function_name,SEE_STATIC);

      if(id!=-1)
	return mkexternalnode(n,id,ID_FROM_INT(state->new_program, id));

      if(ISCONSTSTR(function_name,"`->") ||
	 ISCONSTSTR(function_name,"`[]"))
      {
	return mknode(F_MAGIC_INDEX,
		      mknewintnode(e),mknewintnode(n+1));
      }

      if(ISCONSTSTR(function_name,"`->=") ||
	 ISCONSTSTR(function_name,"`[]="))
      {
	return mknode(F_MAGIC_SET_INDEX,
		      mknewintnode(e),mknewintnode(n+1));
      }
    }
  }

  return 0;
}

void rename_last_inherit(struct pike_string *n)
{
  if(new_program->inherits[new_program->num_inherits].name)
    free_string(new_program->inherits[new_program->num_inherits].name);
  copy_shared_string(new_program->inherits[new_program->num_inherits].name,
		     n);
}

/*
 * make this program inherit another program
 */
/*
 * make this program inherit another program
 */
void low_inherit(struct program *p,
		 struct object *parent,
		 int parent_identifier,
		 int parent_offset,
		 INT32 flags,
		 struct pike_string *name)
{
  int e;
  ptrdiff_t inherit_offset, storage_offset;
  struct inherit inherit;
  struct pike_string *s;

  if(!p)
  {
    yyerror("Illegal program pointer.");
    return;
  }

  if(p->flags & PROGRAM_USES_PARENT)
  {
    if(!parent && !parent_offset)
    {
      yyerror("Parent pointer lost, cannot inherit!");
      /* We inherit it anyway, to avoid causing more errors */
    }

#if 0
    /* FIXME: we don't really need to set thsi flag on ALL
     * previous compilations, but I'm too lazy to figure out
     * exactly how deep down we need to go...
     */
    for(e=0;e<compilation_depth;e++,state=state->previous)
      state->new_program->flags |= PROGRAM_USES_PARENT;
#endif
  }

 /* parent offset was increased by 42 for above test.. */
  if(parent_offset)
    parent_offset-=42;


  if(!(p->flags & (PROGRAM_FINISHED | PROGRAM_PASS_1_DONE)))
  {
    yyerror("Cannot inherit program which is not fully compiled yet.");
    return;
  }

  inherit_offset = new_program->num_inherits;

  /* alignment magic */
  storage_offset=p->inherits[0].storage_offset % p->alignment_needed;
  storage_offset=low_add_storage(STORAGE_NEEDED(p),
				 p->alignment_needed,
				 storage_offset);

  /* Without this, the inherit becomes skewed */
  storage_offset-=p->inherits[0].storage_offset;

  for(e=0; e<(int)p->num_inherits; e++)
  {
    inherit=p->inherits[e];
    add_ref(inherit.prog);
    inherit.identifier_level += new_program->num_identifier_references;
    inherit.storage_offset += storage_offset;
    inherit.inherit_level ++;


    if(!e)
    {
      if(parent)
      {
	if(parent->next == parent)
	{
	  struct object *o;
	  inherit.parent_offset=0;
	  for(o=fake_object;o!=parent;o=o->parent)
	  {
#ifdef PIKE_DEBUG
	    if(!o) fatal("low_inherit with odd fake_object as parent!\n");
#endif
	    inherit.parent_offset++;
	  }
	}else{
	  inherit.parent=parent;
	  inherit.parent_identifier=parent_identifier;
	  inherit.parent_offset=-17;
	}
      }else{
	inherit.parent_offset=parent_offset;
	inherit.parent_identifier=parent_identifier;
      }
    }else{
      if(!inherit.parent)
      {
	if(parent && parent->next != parent && inherit.parent_offset)
	{
	  /* Fake object */
	  struct object *par=parent;
	  int e,pid=parent_identifier;

	  for(e=1;e<inherit.parent_offset;e++)
	  {
	    struct inherit *in;
	    if(!par->prog)
	    {
	      par=0;
	      pid=-1;
	      break;
	    }

	    in=INHERIT_FROM_INT(par->prog, pid);
	    switch(in->parent_offset)
	    {
	      default:
	      {
		struct external_variable_context tmp;
		struct inherit *in2=in;
		while(in2->identifier_level >= in->identifier_level) in2--;
		tmp.o=par;
		tmp.inherit=in2;
		tmp.parent_identifier=pid;
		find_external_context(&tmp, in->parent_offset);
		par = tmp.o;
		pid = tmp.parent_identifier;
	      }
	      break;

	      case -17:
		pid = in->parent_identifier;
		par = in->parent;
		break;

	      case -18:
		pid = par->parent_identifier;
		par = par->parent;
	    }
	  }

	  inherit.parent=par;
	  inherit.parent_offset=-17;
	}
      }
    }
    if(inherit.parent) add_ref(inherit.parent);

    if(name)
    {
      if(e==0)
      {
	copy_shared_string(inherit.name,name);
      }
      else if(inherit.name)
      {
	struct pike_string *s;
	s=begin_shared_string(inherit.name->len + name->len + 2);
	MEMCPY(s->str,name->str,name->len);
	MEMCPY(s->str+name->len,"::",2);
	MEMCPY(s->str+name->len+2,inherit.name->str,inherit.name->len);
	inherit.name=end_shared_string(s);
      }
      else
      {
	inherit.name=0;
      }
    }else{
      inherit.name=0;
    }
    add_to_inherits(inherit);
  }

  for (e=0; e < (int)p->num_identifier_references; e++)
  {
    struct reference fun;
    struct pike_string *name;

    fun = p->identifier_references[e]; /* Make a copy */

    name=ID_FROM_PTR(p,&fun)->name;
    fun.inherit_offset += inherit_offset;

    if (fun.id_flags & ID_NOMASK)
    {
      int n;
      n = isidentifier(name);
      if (n != -1 && ID_FROM_INT(new_program,n)->func.offset != -1)
	my_yyerror("Illegal to redefine 'nomask' function/variable \"%s\"",name->str);
    }

    if(fun.id_flags & ID_PRIVATE) fun.id_flags|=ID_HIDDEN;

    if (fun.id_flags & ID_PUBLIC)
      fun.id_flags |= flags & ~ID_PRIVATE;
    else
      fun.id_flags |= flags;

    fun.id_flags |= ID_INHERITED;
    add_to_identifier_references(fun);
  }
}

void do_inherit(struct svalue *s,
		INT32 flags,
		struct pike_string *name)
{
  struct program *p=program_from_svalue(s);
  low_inherit(p,
	      s->type == T_FUNCTION ? s->u.object : 0,
	      s->type == T_FUNCTION ? s->subtype : -1,
	      0,
	      flags,
	      name);
}

void compiler_do_inherit(node *n,
			 INT32 flags,
			 struct pike_string *name)
{
  struct program *p;
  struct identifier *i;
  INT32 numid, offset;

  if(!n)
  {
    yyerror("Unable to inherit");
    return;
  }
  switch(n->token)
  {
    case F_IDENTIFIER:
      p=new_program;
      offset=0;
      numid=n->u.id.number;
      goto continue_inherit;

    case F_EXTERNAL:
      p=parent_compilation(n->u.integer.a);
      offset=n->u.integer.a+1;
      numid=n->u.integer.b;

      if(!p)
      {
	yyerror("Failed to resolv external constant.\n");
	return;
      }


  continue_inherit:
      i=ID_FROM_INT(p, numid);

      if(IDENTIFIER_IS_CONSTANT(i->identifier_flags))
      {
	struct svalue *s=&PROG_FROM_INT(p, numid)->
	  constants[i->func.offset].sval;
	if(s->type != T_PROGRAM)
	{
	  do_inherit(s,flags,name);
	  return;
	}else{
	  p=s->u.program;
	}
      }else{
	yyerror("Inherit identifier is not a constant program");
	return;
      }

      low_inherit(p,
		  0,
		  numid,
		  offset+42,
		  flags,
		  name);
      break;

    default:
      resolv_class(n);
      do_inherit(sp-1, flags, name);
      pop_stack();
  }
}


void simple_do_inherit(struct pike_string *s,
		       INT32 flags,
		       struct pike_string *name)
{
  reference_shared_string(s);
  push_string(s);
  ref_push_string(lex.current_file);
  SAFE_APPLY_MASTER("handle_inherit", 2);

  if(sp[-1].type != T_PROGRAM)
  {
    my_yyerror("Couldn't find file to inherit %s",s->str);
    pop_stack();
    return;
  }

  if(name)
  {
    free_string(s);
    s=name;
  }
  do_inherit(sp-1, flags, s);
  free_string(s);
  pop_stack();
}

/*
 * Return the index of the identifier found, otherwise -1.
 */
int isidentifier(struct pike_string *s)
{
  INT32 e;
  for(e=new_program->num_identifier_references-1;e>=0;e--)
  {
    if(new_program->identifier_references[e].id_flags & ID_HIDDEN) continue;

    if(ID_FROM_INT(new_program, e)->name == s)
      return e;
  }
  return -1;
}

/* argument must be a shared string */
int low_define_variable(struct pike_string *name,
			struct pike_string *type,
			INT32 flags,
			INT32 offset,
			INT32 run_time_type)
{
  int n;

  struct identifier dummy;
  struct reference ref;

#ifdef PIKE_DEBUG
  if(new_program->flags & (PROGRAM_FIXED | PROGRAM_OPTIMIZED))
    fatal("Attempting to add variable to fixed program\n");

  if(compiler_pass==2)
    fatal("Internal error: Not allowed to add more identifiers during second compiler pass.\n"
	  "Added identifier: \"%s\"\n", name->str);
#endif

  copy_shared_string(dummy.name, name);
  copy_shared_string(dummy.type, type);
  dummy.identifier_flags = 0;
  dummy.run_time_type=run_time_type;
  dummy.func.offset=offset - new_program->inherits[0].storage_offset;
#ifdef PROFILING
  dummy.self_time=0;
  dummy.num_calls=0;
  dummy.total_time=0;
#endif

  ref.id_flags=flags;
  ref.identifier_offset=new_program->num_identifiers;
  ref.inherit_offset=0;

  add_to_variable_index(ref.identifier_offset);

  add_to_identifiers(dummy);

  n=new_program->num_identifier_references;
  add_to_identifier_references(ref);

  return n;
}

int map_variable(char *name,
		 char *type,
		 INT32 flags,
		 INT32 offset,
		 INT32 run_time_type)
{
  int ret;
  struct pike_string *n,*t;

#ifdef PROGRAM_BUILD_DEBUG
  fprintf (stderr, "%.*sdefining variable (pass=%d): %s %s\n",
	   compilation_depth, "                ", compiler_pass, type, name);
#endif

  n=make_shared_string(name);
  t=parse_type(type);
  ret=low_define_variable(n,t,flags,offset,run_time_type);
  free_string(n);
  free_string(t);
  return ret;
}

/* argument must be a shared string */
int define_variable(struct pike_string *name,
		    struct pike_string *type,
		    INT32 flags)
{
  int n, run_time_type;

#ifdef PIKE_DEBUG
  if(name!=debug_findstring(name))
    fatal("define_variable on nonshared string.\n");
#endif

#ifdef PROGRAM_BUILD_DEBUG
  {
    struct pike_string *d = describe_type (type);
    fprintf (stderr, "%.*sdefining variable (pass=%d): %s ",
	     compilation_depth, "                ", compiler_pass, d->str);
    free_string (d);
    push_string (name);
    print_svalue (stderr, --sp);
    putc ('\n', stderr);
  }
#endif

  if(type == void_type_string)
    yyerror("Variables can't be of type void");

  n = isidentifier(name);

  if(new_program->flags & PROGRAM_PASS_1_DONE)
  {
    if(n==-1)
      yyerror("Pass2: Variable disappeared!");
    else {
      /* Don't mess with inherited variables... */
      if(!IDENTIFIERP(n)->inherit_offset) {
	struct identifier *id;
	id=ID_FROM_INT(new_program,n);
	free_string(id->type);
	copy_shared_string(id->type, type);
      }
      return n;
    }
  }

#ifdef PIKE_DEBUG
  if(new_program->flags & (PROGRAM_FIXED | PROGRAM_OPTIMIZED))
    fatal("Attempting to add variable to fixed program\n");
#endif

  if(n != -1)
  {
    /* not inherited */
    if(new_program->identifier_references[n].inherit_offset == 0)
    {
      if (!((IDENTIFIERP(n)->id_flags | flags) & ID_EXTERN)) {
	my_yyerror("Identifier '%s' defined twice.",name->str);
	return n;
      }
      if (flags & ID_EXTERN) {
	/* FIXME: Check type */
	return n;
      }
    }

    if (!(IDENTIFIERP(n)->id_flags & ID_EXTERN)) {
      if (IDENTIFIERP(n)->id_flags & ID_NOMASK)
	my_yyerror("Illegal to redefine 'nomask/final' "
		   "variable/functions \"%s\"", name->str);

      if(!(IDENTIFIERP(n)->id_flags & ID_INLINE) || compiler_pass!=1)
      {
 	if(!match_types(ID_FROM_INT(new_program, n)->type, type))
 	  my_yyerror("Illegal to redefine inherited variable "
 		     "with different type.");

	

	if(!IDENTIFIER_IS_VARIABLE(ID_FROM_INT(new_program, n)->
				   identifier_flags))
	{
	  my_yyerror("Illegal to redefine inherited variable "
		     "with different type.");
	}

	IDENTIFIERP(n)->id_flags = flags & ~ID_EXTERN;
	return n;
      }
    }
  }

  run_time_type=compile_type_to_runtime_type(type);

  switch(run_time_type)
  {
#ifdef AUTO_BIGNUM
#if 0
    case T_OBJECT:
      /* This is to allow room for integers in variables declared as
       * 'object', however, this could be changed in the future to only
       * make room for integers if the variable was declared as
       * 'object(Gmp.mpz)'                                     /Hubbe
       */
#endif
    case T_INT:
#endif
    case T_FUNCTION:
    case T_PROGRAM:
      run_time_type = T_MIXED;
  }

  n=low_define_variable(name,type,flags,
			low_add_storage(sizeof_variable(run_time_type),
					alignof_variable(run_time_type),0),
			run_time_type);


  return n;
}

int simple_add_variable(char *name,
			char *type,
			INT32 flags)
{
  INT32 ret;
  struct pike_string *name_s, *type_s;
  name_s=make_shared_string(name);
  type_s=parse_type(type);

  ret=define_variable(name_s, type_s, flags);
  free_string(name_s);
  free_string(type_s);
  return ret;
}

int add_constant(struct pike_string *name,
		 struct svalue *c,
		 INT32 flags)
{
  int n;
  struct identifier dummy;
  struct reference ref;

#ifdef PROGRAM_BUILD_DEBUG
  {
    if (c) {
      struct pike_string *t = get_type_of_svalue (c);
      struct pike_string *d = describe_type (t);
      fprintf (stderr, "%.*sdefining constant (pass=%d): %s ",
	       compilation_depth, "                ", compiler_pass, d->str);
      free_string (t);
      free_string (d);
    }
    else
      fprintf (stderr, "%.*sdeclaring constant (pass=%d): ",
	       compilation_depth, "                ", compiler_pass);
    push_string (name);
    print_svalue (stderr, --sp);
    putc ('\n', stderr);
  }
#endif

#ifdef PIKE_DEBUG
  if(name!=debug_findstring(name))
    fatal("define_constant on nonshared string.\n");
#endif

  n = isidentifier(name);

  /*
   * FIXME:
   * No constants should be allowed to contain
   * any fake objects. (Fake objects can be
   * detected thus: fake_ob->next == fake_ob
   *
   * I would prefer to find a way to detect
   * this without recursing through arrays,
   * mapping etc. etc.
   *
   * /Hubbe
   */

  if(new_program->flags & PROGRAM_PASS_1_DONE)
  {
    if(n==-1)
    {
      yyerror("Pass2: Constant disappeared!");
    }else{
      struct identifier *id;
      id=ID_FROM_INT(new_program,n);
      if(id->func.offset>=0)
      {
	struct pike_string *s;
	struct svalue *c=&PROG_FROM_INT(new_program,n)->
	  constants[id->func.offset].sval;
	s=get_type_of_svalue(c);
	free_string(id->type);
	id->type=s;
      }
      else {
#ifdef PIKE_DEBUG
	if (!c) fatal("Can't declare constant during second compiler pass\n");
#endif
	free_string(id->type);
	id->type = get_type_of_svalue(c);
	id->run_time_type = c->type;
	id->func.offset = store_constant(c, 0, 0);
      }
      return n;
    }
  }

#ifdef PIKE_DEBUG
  if(new_program->flags & (PROGRAM_FIXED | PROGRAM_OPTIMIZED))
    fatal("Attempting to add constant to fixed program\n");

  if(compiler_pass==2)
    fatal("Internal error: Not allowed to add more identifiers during second compiler pass.\n");
#endif

  copy_shared_string(dummy.name, name);
  dummy.identifier_flags = IDENTIFIER_CONSTANT;

  if (c) {
    dummy.type = get_type_of_svalue(c);
    dummy.run_time_type=c->type;
    dummy.func.offset=store_constant(c, 0, 0);
  }
  else {
    reference_shared_string(mixed_type_string);
    dummy.type = mixed_type_string;
    dummy.run_time_type=T_MIXED;
    dummy.func.offset=-1;
  }

  ref.id_flags=flags;
  ref.identifier_offset=new_program->num_identifiers;
  ref.inherit_offset=0;

#ifdef PROFILING
  dummy.self_time=0;
  dummy.num_calls=0;
  dummy.total_time=0;
#endif

  add_to_identifiers(dummy);

  if(n != -1)
  {
    if(IDENTIFIERP(n)->id_flags & ID_NOMASK)
      my_yyerror("Illegal to redefine 'nomask' identifier \"%s\"", name->str);

    /* not inherited */
    if(new_program->identifier_references[n].inherit_offset == 0)
    {
      my_yyerror("Identifier '%s' defined twice.",name->str);
      return n;
    }

    if(!(IDENTIFIERP(n)->id_flags & ID_INLINE))
    {
      /* override */
      new_program->identifier_references[n]=ref;

      return n;
    }
  }
  n=new_program->num_identifier_references;
  add_to_identifier_references(ref);

  return n;
}

int simple_add_constant(char *name,
			struct svalue *c,
			INT32 flags)
{
  INT32 ret;
  struct pike_string *id;
  id=make_shared_string(name);
  ret=add_constant(id, c, flags);
  free_string(id);
  return ret;
}

int add_integer_constant(char *name,
			 INT32 i,
			 INT32 flags)
{
  struct svalue tmp;
  tmp.u.integer=i;
  tmp.type=T_INT;
  tmp.subtype=NUMBER_NUMBER;
  return simple_add_constant(name, &tmp, flags);
}

int quick_add_integer_constant(char *name,
			       int name_length,
			       INT32 i,
			       INT32 flags)
{
  struct svalue tmp;
  struct pike_string *id;
  INT32 ret;

  tmp.u.integer=i;
  tmp.type=T_INT;
  tmp.subtype=NUMBER_NUMBER;
  id=make_shared_binary_string(name,name_length);
  ret=add_constant(id, &tmp, flags);
  free_string(id);
  return ret;
}

int add_float_constant(char *name,
			 double f,
			 INT32 flags)
{
  struct svalue tmp;
  tmp.type=T_FLOAT;
  tmp.u.float_number=f;
  tmp.subtype=0;
  return simple_add_constant(name, &tmp, flags);
}

int add_string_constant(char *name,
			char *str,
			INT32 flags)
{
  INT32 ret;
  struct svalue tmp;
  tmp.type=T_STRING;
  tmp.subtype=0;
  tmp.u.string=make_shared_string(str);
  ret=simple_add_constant(name, &tmp, flags);
  free_svalue(&tmp);
  return ret;
}

int add_program_constant(char *name,
			 struct program *p,
			 INT32 flags)
{
  INT32 ret;
  struct svalue tmp;
  tmp.type=T_PROGRAM;
  tmp.subtype=0;
  tmp.u.program=p;
  ret=simple_add_constant(name, &tmp, flags);
  return ret;
}

int add_object_constant(char *name,
			struct object *o,
			INT32 flags)
{
  INT32 ret;
  struct svalue tmp;
  tmp.type=T_OBJECT;
  tmp.subtype=0;
  tmp.u.object=o;
  ret=simple_add_constant(name, &tmp, flags);
  return ret;
}

int add_function_constant(char *name, void (*cfun)(INT32), char * type, INT16 flags)
{
  struct svalue s;
  struct pike_string *n;
  INT32 ret;

  s.type=T_FUNCTION;
  s.subtype=FUNCTION_BUILTIN;
  s.u.efun=make_callable(cfun, name, type, flags, 0, 0);
  ret=simple_add_constant(name, &s, 0);
  free_svalue(&s);
  return ret;
}


int debug_end_class(char *name, int namelen, INT32 flags)
{
  INT32 ret;
  struct svalue tmp;
  struct pike_string *id;

  tmp.type=T_PROGRAM;
  tmp.subtype=0;
  tmp.u.program=end_program();
  if(!tmp.u.program)
    fatal("Failed to initialize class '%s'\n",name);

  id=make_shared_binary_string(name,namelen);
  ret=add_constant(id, &tmp, flags);
  free_string(id);
  free_svalue(&tmp);
  return ret;
}

/*
 * define a new function
 * if func isn't given, it is supposed to be a prototype.
 */
INT32 define_function(struct pike_string *name,
		      struct pike_string *type,
		      INT16 flags,
		      INT8 function_flags,
		      union idptr *func)
{
  struct identifier *funp,fun;
  struct reference ref;
  struct svalue *lfun_type;
  INT32 i;

#ifdef PROGRAM_BUILD_DEBUG
  {
    struct pike_string *d = describe_type (type);
    fprintf (stderr, "%.*sdefining function (pass=%d): %s ",
	     compilation_depth, "                ", compiler_pass, d->str);
    free_string (d);
    push_string (name);
    print_svalue (stderr, --sp);
    putc ('\n', stderr);
  }
#endif

#ifdef PROFILING
  fun.self_time=0;
  fun.num_calls=0;
  fun.total_time=0;
#endif

  /* If this is an lfun, match against the predefined type. */
  if ((lfun_type = low_mapping_string_lookup(lfun_types, name))) {
#ifdef PIKE_DEBUG
    if (lfun_type->type != T_TYPE) {
      fatal("Bad entry in lfun_types for key \"%s\"\n", name->str);
    }
#endif /* PIKE_DEBUG */
    if (!pike_types_le(type, lfun_type->u.string)) {
      if (!match_types(type, lfun_type->u.string)) {
	yytype_error("Function type mismatch", lfun_type->u.string, type, 0);
      } else if (lex.pragmas & ID_STRICT_TYPES) {
	yytype_error("Function type mismatch", lfun_type->u.string, type,
		     YYTE_IS_WARNING);
      }
    }
  }

  if(function_flags & IDENTIFIER_C_FUNCTION)
    new_program->flags |= PROGRAM_HAS_C_METHODS;

  i=isidentifier(name);

  if(i >= 0)
  {
    /* already defined */

    funp=ID_FROM_INT(new_program, i);
    ref=new_program->identifier_references[i];

    if(ref.inherit_offset == 0) /* not inherited */
    {

      if( !( IDENTIFIER_IS_FUNCTION(funp->identifier_flags) &&
	     ( (!func || func->offset == -1) || (funp->func.offset == -1))))
      {
	my_yyerror("Identifier '%s' defined twice.",name->str);
	return i;
      }

      /* match types against earlier prototype or vice versa */
      if(!match_types(type, funp->type))
      {
	my_yyerror("Prototype doesn't match for function %s.",name->str);
      }
    }

    /* We modify the old definition if it is in this program */

    if(ref.inherit_offset==0)
    {
      if(func)
	funp->func = *func;
      else
	funp->func.offset = -1;

      funp->identifier_flags=function_flags;

      free_string(funp->type);
      copy_shared_string(funp->type, type);
    }else{

      if((ref.id_flags & ID_NOMASK)
#if 0
	 && !(funp->func.offset == -1)
#endif
	)
      {
	my_yyerror("Illegal to redefine 'nomask' function %s.",name->str);
      }


      if(ref.id_flags & ID_INLINE)
      {
	goto make_a_new_def;
      }

      /* Otherwise we make a new definition */
      copy_shared_string(fun.name, name);
      copy_shared_string(fun.type, type);

      fun.run_time_type=T_FUNCTION;

      fun.identifier_flags=function_flags;
      if(function_flags & IDENTIFIER_C_FUNCTION)
	new_program->flags |= PROGRAM_HAS_C_METHODS;

      if(func)
	fun.func = *func;
      else
	fun.func.offset = -1;

      ref.identifier_offset=new_program->num_identifiers;
      add_to_identifiers(fun);
    }

    ref.inherit_offset = 0;
    ref.id_flags = flags;
#if 0
    new_program->identifier_references[i]=ref;
#else
    {
      int z;
      /* This loop could possibly be optimized by looping over
       * each inherit and looking up 'name' in each inherit
       * and then see if should be overwritten
       * /Hubbe
       */

      for(z=0;z<new_program->num_identifier_references;z++)
      {
	/* Do zapp hidden identifiers */
	if(new_program->identifier_references[z].id_flags & ID_HIDDEN)
	  continue;

	/* Do not zapp inline ('local') identifiers */
	if(new_program->identifier_references[z].inherit_offset &&
	   (new_program->identifier_references[z].id_flags & ID_INLINE))
          continue;

	/* Do not zapp functions with the wrong name... */
	if(ID_FROM_INT(new_program, z)->name != name)
	  continue;

	new_program->identifier_references[z]=ref;
      }

#ifdef PIKE_DEBUG
      if(MEMCMP(new_program->identifier_references+i, &ref,sizeof(ref)))
	fatal("New function overloading algorithm failed!\n");
#endif

    }
#endif
    return i;
  }
make_a_new_def:


#ifdef PIKE_DEBUG
  if(compiler_pass==2)
    fatal("Internal error: Not allowed to add more identifiers during second compiler pass.\n");
#endif

  /* define a new function */

  copy_shared_string(fun.name, name);
  copy_shared_string(fun.type, type);

  fun.identifier_flags=function_flags;
  fun.run_time_type=T_FUNCTION;

  if(func)
    fun.func = *func;
  else
    fun.func.offset = -1;

  i=new_program->num_identifiers;

  add_to_identifiers(fun);

  ref.id_flags = flags;
  ref.identifier_offset = i;
  ref.inherit_offset = 0;

  i=new_program->num_identifier_references;
  add_to_identifier_references(ref);

  return i;
}

int really_low_find_shared_string_identifier(struct pike_string *name,
					     struct program *prog,
					     int flags)
{
  struct reference *funp;
  struct identifier *fun;
  int i,t;

#if 0
  CDFPRINTF((stderr,"th(%ld) Trying to find %s flags=%d\n",
	     (long)th_self(),name->str, flags));
#endif

#ifdef PIKE_DEBUG
  if (!prog) {
    fatal("really_low_find_shared_string_identifier(\"%s\", NULL, %d)\n"
	  "prog is NULL!\n", name->str, flags);
  }
#endif /* PIKE_DEBUG */

  for(i=0;i<(int)prog->num_identifier_references;i++)
  {
    funp = prog->identifier_references + i;
    if(funp->id_flags & ID_HIDDEN) continue;
    if(funp->id_flags & ID_STATIC)
      if(!(flags & SEE_STATIC))
	continue;
    fun = ID_FROM_PTR(prog, funp);
    /* if(fun->func.offset == -1) continue; * Prototype */
    if(!is_same_string(fun->name,name)) continue;
    if(funp->id_flags & ID_INHERITED)
    {
      if(funp->id_flags & ID_PRIVATE) continue;
      for(t=0; t>=0 && t<(int)prog->num_identifier_references; t++)
      {
	struct reference *funpb;
	struct identifier *funb;

	if(t==i) continue;
	funpb=prog->identifier_references+t;
	if(funpb->id_flags & ID_HIDDEN) continue;
	if(funpb->id_flags & ID_STATIC)
	  if(!(flags & SEE_STATIC))
	    continue;
	if((funpb->id_flags & ID_INHERITED) && t<i) continue;
	funb=ID_FROM_PTR(prog,funpb);
	/* if(funb->func.offset == -1) continue; * prototype */
	if(fun->name==funb->name) t=-10;
      }
      if(t < 0) continue;
    }
    return i;
  }
  return -1;
}

int low_find_lfun(struct program *p, int lfun)
{
  struct pike_string *lfun_name = findstring(lfun_names[lfun]);
  unsigned int flags = 0;
  if (!lfun_name) return -1;
  if ((1 <= lfun) && (lfun < 3)) {
    /* create() and destroy() are used even if they are static. */
    flags = SEE_STATIC;
  }
  return
    really_low_find_shared_string_identifier(lfun_name,
					     dmalloc_touch(struct program *,
							   p),
					     flags);
}

/*
 * lookup the number of a function in a program given the name in
 * a shared_string
 */
int low_find_shared_string_identifier(struct pike_string *name,
				      struct program *prog)
{
  int max,min,tst;
  struct identifier *fun;

  if(prog->flags & PROGRAM_FIXED)
  {
    unsigned short *funindex = prog->identifier_index;

#ifdef PIKE_DEBUG
    if(!funindex)
      fatal("No funindex in fixed program\n");
#endif

    max = prog->num_identifier_index;
    min = 0;
    while(max != min)
    {
      tst=(max + min) >> 1;
      fun = ID_FROM_INT(prog, funindex[tst]);
      if(is_same_string(fun->name,name)) return funindex[tst];
      if(my_order_strcmp(fun->name, name) > 0)
	max=tst;
      else
	min=tst+1;
    }
  }else{
    return really_low_find_shared_string_identifier(name,prog,0);
  }
  return -1;
}

#ifdef FIND_FUNCTION_HASHSIZE
#if FIND_FUNCTION_HASHSIZE == 0
#undef FIND_FUNCTION_HASHSIZE
#endif
#endif

#ifdef FIND_FUNCTION_HASHSIZE
struct ff_hash
{
  struct pike_string *name;
  int id;
  int fun;
};

static struct ff_hash cache[FIND_FUNCTION_HASHSIZE];
#endif

int find_shared_string_identifier(struct pike_string *name,
				  struct program *prog)
{
#ifdef PIKE_DEBUG
  if (!prog) {
    fatal("find_shared_string_identifier(): No program!\n"
	  "Identifier: %s%s%s\n",
	  name?"\"":"", name?name->str:"NULL", name?"\"":"");
  }
#endif /* PIKE_DEBUG */
#ifdef FIND_FUNCTION_HASHSIZE
  if(prog -> flags & PROGRAM_FIXED)
  {
    unsigned int hashval;
    hashval=my_hash_string(name);
    hashval+=prog->id;
    hashval^=(unsigned long)prog;
    hashval-=name->str[0];
    hashval%=FIND_FUNCTION_HASHSIZE;
    if(is_same_string(cache[hashval].name,name) &&
       cache[hashval].id==prog->id)
      return cache[hashval].fun;

    if(cache[hashval].name) free_string(cache[hashval].name);
    copy_shared_string(cache[hashval].name,name);
    cache[hashval].id=prog->id;
    return cache[hashval].fun=low_find_shared_string_identifier(name,prog);
  }
#endif /* FIND_FUNCTION_HASHSIZE */

  return low_find_shared_string_identifier(name,prog);
}

int find_identifier(char *name,struct program *prog)
{
  struct pike_string *n;
  if(!prog) {
    if (strlen(name) < 1024) {
      error("Lookup of identifier %s in destructed object.\n", name);
    } else {
      error("Lookup of long identifier in destructed object.\n");
    }
  }
  n=findstring(name);
  if(!n) return -1;
  return find_shared_string_identifier(n,prog);
}

int store_prog_string(struct pike_string *str)
{
  unsigned int i;

  for (i=0;i<new_program->num_strings;i++)
    if (new_program->strings[i] == str)
      return i;

  reference_shared_string(str);
  add_to_strings(str);
  return i;
}

int store_constant(struct svalue *foo,
		   int equal,
		   struct pike_string *constant_name)
{
  struct program_constant tmp;
  unsigned int e;
  JMP_BUF tmp2;

  if(SETJMP(tmp2))
  {
    ONERROR tmp;
    struct svalue zero;

    SET_ONERROR(tmp,exit_on_error,"Error in store_constant in compiler!");
    assign_svalue_no_free(sp++, & throw_value);
    APPLY_MASTER("handle_error", 1);
    pop_stack();
    UNSET_ONERROR(tmp);

    yyerror("Couldn't store constant.");

    zero.type = T_INT;
    zero.subtype = NUMBER_NUMBER;
    zero.u.integer=0;

    UNSETJMP(tmp2);
    return store_constant(&zero, equal, constant_name);
  }else{
    for(e=0;e<new_program->num_constants;e++)
    {
      struct program_constant *c= new_program->constants+e;
      if((equal ? is_equal(& c->sval,foo) : is_eq(& c->sval,foo)) &&
	 c->name == constant_name)
      {
	UNSETJMP(tmp2);
	return e;
      }
    }
    assign_svalue_no_free(&tmp.sval,foo);
    if((tmp.name=constant_name)) add_ref(constant_name);

    add_to_constants(tmp);

    UNSETJMP(tmp2);
    return e;
  }
}

/*
 * program examination functions available from Pike.
 */

struct array *program_indices(struct program *p)
{
  int e;
  int n = 0;
  struct array *res;
  for (e = p->num_identifier_references; e--; ) {
    struct identifier *id;
    if (p->identifier_references[e].id_flags & ID_HIDDEN) {
      continue;
    }
    id = ID_FROM_INT(p, e);
    if (IDENTIFIER_IS_CONSTANT(id->identifier_flags)) {
      ref_push_string(ID_FROM_INT(p, e)->name);
      n++;
    }
  }
  f_aggregate(n);
  res = sp[-1].u.array;
  add_ref(res);
  pop_stack();
  return(res);
}

struct array *program_values(struct program *p)
{
  int e;
  int n = 0;
  struct array *res;
  for(e = p->num_identifier_references; e--; ) {
    struct identifier *id;
    if (p->identifier_references[e].id_flags & ID_HIDDEN) {
      continue;
    }
    id = ID_FROM_INT(p, e);
    if (IDENTIFIER_IS_CONSTANT(id->identifier_flags)) {
      struct program *p2 = PROG_FROM_INT(p, e);
      push_svalue( & p2->constants[id->func.offset].sval);
      n++;
    }
  }
  f_aggregate(n);
  res = sp[-1].u.array;
  add_ref(res);
  pop_stack();
  return(res);
}

void program_index_no_free(struct svalue *to, struct program *p,
			   struct svalue *ind)
{
  int e;
  struct pike_string *s;

  if (ind->type != T_STRING) {
    error("Can't index a program with a %s (expected string)\n",
	  get_name_of_type(ind->type));
  }
  s = ind->u.string;
  e=find_shared_string_identifier(s, p);
  if(e!=-1)
  {
    struct identifier *id;
    id=ID_FROM_INT(p, e);
    if (IDENTIFIER_IS_CONSTANT(id->identifier_flags)) {
      struct program *p2 = PROG_FROM_INT(p, e);
      assign_svalue_no_free(to, ( & p2->constants[id->func.offset].sval));
      return;
    } else {
      if (s->len < 1024) {
	error("Index \"%s\" is not constant.\n", s->str);
      } else {
	error("Index is not constant.\n");
      }
    }
  }

#if 1
  to->type=T_INT;
  to->subtype=NUMBER_UNDEFINED;
  to->u.integer=0;
#else
  if (s->len < 1024) {
    error("No such index \"%s\".\n", s->str);
  } else {
    error("No such index.\n");
  }
#endif
}

/*
 * Line number support routines, now also tells what file we are in
 */

int get_small_number(char **q)
{
  /* This is a workaround for buggy cc & Tru64 */
  int ret;
  ret=*(signed char *)*q;
  (*q)++;
  switch(ret)
  {
  case -127:
    ret=EXTRACT_WORD((unsigned char*)*q);
    *q+=2;
    return ret;

  case -128:
    ret=EXTRACT_INT((unsigned char*)*q);
    *q+=4;
    return ret;

  default:
    return ret;
  }
}

void start_line_numbering(void)
{
  if(last_file)
  {
    free_string(last_file);
    last_file=0;
  }
  last_pc=last_line=0;
}

static void insert_small_number(INT32 a)
{
  if(a>-127 && a<127)
  {
    add_to_linenumbers(a);
  }else if(a>=-32768 && a<32768){
    add_to_linenumbers(-127);
    ins_short(a, add_to_linenumbers);
  }else{
    add_to_linenumbers(-128);
    ins_int(a, add_to_linenumbers);
  }
}

void store_linenumber(INT32 current_line, struct pike_string *current_file)
{
/*  if(!store_linenumbers)  fatal("Fnord.\n"); */
#ifdef PIKE_DEBUG
  if(d_flag)
  {
    INT32 line=0, off=0;
    char *file=0;
    char *cnt=new_program->linenumbers;

    while(cnt < new_program->linenumbers + new_program->num_linenumbers)
    {
      if(*cnt == 127)
      {
	file=cnt+1;
	cnt=file+strlen(file)+1;
      }
      off+=get_small_number(&cnt);
      line+=get_small_number(&cnt);
    }

    if(last_line != line ||
       last_pc != off ||
       (last_file && file && strcmp(last_file->str, file)))
    {
      fatal("Line numbering out of whack\n"
	    "    (line: %d ?= %d)!\n"
	    "    (  pc: %d ?= %d)!\n"
	    "    (file: %s ?= %s)!\n",
	    last_line, line,
	    last_pc, off,
	    last_file?last_file->str:"N/A",
	    file?file:"N/A");
    }
  }
#endif
  if(last_line!=current_line || last_file != current_file)
  {
    if(last_file != current_file)
    {
      char *tmp;
      if(last_file) free_string(last_file);
      add_to_linenumbers(127);
      for(tmp=current_file->str; *tmp; tmp++)
	add_to_linenumbers(*tmp);
      add_to_linenumbers(0);
      copy_shared_string(last_file, current_file);
    }
    insert_small_number(PC-last_pc);
    insert_small_number(current_line-last_line);
    last_line=current_line;
    last_pc=PC;
  }
}

/*
 * return the file in which we were executing.
 * pc should be the program counter, prog the current
 * program, and line will be initialized to the line
 * in that file.
 */
char *get_line(unsigned char *pc,struct program *prog,INT32 *linep)
{
  static char *file, *cnt;
  static INT32 off,line,pid;
  INT32 offset;

  if (prog == 0) return "Unkown program";
  offset = pc - prog->program;

  if(prog == new_program)
  {
    linep[0]=0;
    return "Optimizer";
  }

#if 0
  if(prog->id != pid || offset < off)
#endif
  {
    cnt=prog->linenumbers;
    off=line=0;
    file="Line not found";
    pid=prog->id;
  }

  if (offset > (INT32)prog->num_program || offset<0)
    return file;

  while(cnt < prog->linenumbers + prog->num_linenumbers)
  {
    int oline;
    if(*cnt == 127)
    {
      file=cnt+1;
      cnt=file+strlen(file)+1;
    }
    off+=get_small_number(&cnt);
    oline=line;
    line+=get_small_number(&cnt);
    if(off > offset)
    {
      linep[0]=oline;
      return file;
    }
  }
  linep[0]=line;
  return file;
}

void my_yyerror(char *fmt,...)  ATTRIBUTE((format(printf,1,2)))
{
  va_list args;
  char buf[8192];

  va_start(args,fmt);

#ifdef HAVE_VSNPRINTF
  vsnprintf(buf, 8190, fmt, args);
#else /* !HAVE_VSNPRINTF */
  VSPRINTF(buf, fmt, args);
#endif /* HAVE_VSNPRINTF */

  if((long)strlen(buf) >= (long)sizeof(buf))
    fatal("Buffer overflow in my_yyerror.\n");

  yyerror(buf);
  va_end(args);
}

struct program *compile(struct pike_string *prog,
			struct object *handler)
{
#ifdef PIKE_DEBUG
  ONERROR tmp;
#endif
  struct program *p;
  struct lex save_lex;
  int save_depth=compilation_depth;
  int saved_threads_disabled;
  struct object *saved_handler = error_handler;
  dynamic_buffer used_modules_save = used_modules;
  INT32 num_used_modules_save = num_used_modules;
  extern void yyparse(void);
  struct mapping *resolve_cache_save = resolve_cache;
  resolve_cache = 0;

  CDFPRINTF((stderr, "th(%ld) compile() starting compilation_depth=%d\n",
	     (long)th_self(),compilation_depth));

  low_init_threads_disable();
  saved_threads_disabled = threads_disabled;

#ifdef PIKE_DEBUG
  SET_ONERROR(tmp, fatal_on_error,"Compiler exited with longjump!\n");
#endif

  error_handler = handler;
  
  if(error_handler)
  {
    /* FIXME: support '#Pike 0.6' here */
    safe_apply(error_handler,"get_default_module",0);
    if(IS_ZERO(sp-1))
    {
      pop_stack();
      ref_push_mapping(get_builtin_constants());
    }
  }else{
    ref_push_mapping(get_builtin_constants());
  }

  num_used_modules=0;

  save_lex=lex;

  lex.end = prog->str + (prog->len << prog->size_shift);

  switch(prog->size_shift) {
  case 0:
    lex.current_lexer = yylex0;
    break;
  case 1:
    lex.current_lexer = yylex1;
    break;
  case 2:
    lex.current_lexer = yylex2;
    break;
  default:
    fatal("Program has bad shift %d!\n", prog->size_shift);
    break;
  }

  lex.current_line=1;
  lex.current_file=make_shared_string("-");
  if (runtime_options & RUNTIME_STRICT_TYPES) {
    lex.pragmas = ID_STRICT_TYPES;
  } else {
    lex.pragmas = 0;
  }

  low_start_new_program(0,0,0,0);

  initialize_buf(&used_modules);
  use_module(sp-1);

  if(lex.current_file)
  {
    store_linenumber(lex.current_line, lex.current_file);

#ifdef DEBUG_MALLOC
    if(strcmp(lex.current_file->str,"-") || lex.current_line!=1)
      debug_malloc_name(new_program, lex.current_file->str, lex.current_line);
#endif
  }
  compilation_depth=0;

/*  start_line_numbering(); */

  compiler_pass=1;
  lex.pos=prog->str;

  CDFPRINTF((stderr, "compile(): First pass\n"));

  yyparse();  /* Parse da program */

  p=end_first_pass(0);

#ifdef PIKE_DEBUG
  if (compilation_depth != -1) {
    fprintf(stderr, "compile(): compilation_depth is %d at end of pass 1.\n",
	    compilation_depth);
  }
#endif /* PIKE_DEBUG */

  if(p)
  {
    low_start_new_program(p,0,0,0);
    free_program(p);
    p=0;
    compiler_pass=2;
    lex.pos=prog->str;

    use_module(sp-1);

    CDFPRINTF((stderr, "compile(): Second pass\n"));

    yyparse();  /* Parse da program again */
    p=end_program();

#ifdef PIKE_DEBUG
    if (compilation_depth != -1) {
      fprintf(stderr, "compile(): compilation_depth is %d at end of pass 2.\n",
	      compilation_depth);
    }
#endif /* PIKE_DEBUG */
  }

#ifdef PIKE_DEBUG
  if (threads_disabled != saved_threads_disabled) {
    fatal("compile(): threads_disabled:%d saved_threads_disabled:%d\n",
	  threads_disabled, saved_threads_disabled);
  }
#endif /* PIKE_DEBUG */
/*  threads_disabled = saved_threads_disabled + 1;   /Hubbe: UGGA! */

  free_string(lex.current_file);
  lex=save_lex;

/*  unuse_modules(1); */
#ifdef PIKE_DEBUG
  if(num_used_modules)
    fatal("Failed to pop modules properly.\n");
#endif

  toss_buffer(&used_modules);
  compilation_depth=save_depth;
  used_modules = used_modules_save;
  num_used_modules = num_used_modules_save ;
  error_handler = saved_handler;
#ifdef PIKE_DEBUG
  if (resolve_cache) fatal("resolve_cache not freed at end of compilation.\n");
#endif
  resolve_cache = resolve_cache_save;

  CDFPRINTF((stderr,
	     "th(%ld) compile() Leave: threads_disabled:%d, compilation_depth:%d\n",
	     (long)th_self(),threads_disabled, compilation_depth));

  exit_threads_disable(NULL);

#ifdef PIKE_DEBUG
  UNSET_ONERROR(tmp);
#endif
  pop_stack(); /* pop the 'default' module */

  if(!p) error("Compilation failed.\n");
  return p;
}

int pike_add_function(char *name,void (*cfun)(INT32),char *type,INT16 flags)
{
  int ret;
  struct pike_string *name_tmp,*type_tmp;
  union idptr tmp;

  name_tmp=make_shared_string(name);
  type_tmp=parse_type(type);

  if(cfun)
  {
    tmp.c_fun=cfun;
    ret=define_function(name_tmp,
			type_tmp,
			flags,
			IDENTIFIER_C_FUNCTION,
			&tmp);
  }else{
    ret=define_function(name_tmp,
			type_tmp,
			flags,
			IDENTIFIER_C_FUNCTION,
			0);
  }
  free_string(name_tmp);
  free_string(type_tmp);
  return ret;
}

int quick_add_function(char *name,
		       int name_length,
		       void (*cfun)(INT32),
		       char *type,
		       int type_length,
		       INT16 flags,
		       int opt_flags)
{
  int ret;
  struct pike_string *name_tmp,*type_tmp;
  union idptr tmp;
/*  fprintf(stderr,"ADD_FUNC: %s\n",name); */
  name_tmp=make_shared_binary_string(name,name_length);
  type_tmp=make_shared_binary_string(type,type_length);

  if(cfun)
  {
    tmp.c_fun=cfun;
    ret=define_function(name_tmp,
			type_tmp,
			flags,
			IDENTIFIER_C_FUNCTION,
			&tmp);
  }else{
    ret=define_function(name_tmp,
			type_tmp,
			flags,
			IDENTIFIER_C_FUNCTION,
			0);
  }
  free_string(name_tmp);
  free_string(type_tmp);
  return ret;
}

#ifdef PIKE_DEBUG
void check_all_programs(void)
{
  struct program *p;
  for(p=first_program;p;p=p->next)
    check_program(p);

#ifdef FIND_FUNCTION_HASHSIZE
  {
    unsigned long e;
    for(e=0;e<FIND_FUNCTION_HASHSIZE;e++)
    {
      if(cache[e].name)
      {
	check_string(cache[e].name);
	if(cache[e].id<0 || cache[e].id > current_program_id)
	  fatal("Error in find_function_cache[%ld].id\n",(long)e);

	if(cache[e].fun < -1 || cache[e].fun > 65536)
	  fatal("Error in find_function_cache[%ld].fun\n",(long)e);
      }
    }
  }
#endif

}
#endif

#undef THIS
#define THIS ((struct pike_trampoline *)(CURRENT_STORAGE))
struct program *pike_trampoline_program=0;

static void apply_trampoline(INT32 args)
{
  error("Internal error: Trampoline magic failed!\n");
}

static void init_trampoline(struct object *o)
{
  THIS->frame=0;
}

static void exit_trampoline(struct object *o)
{
  if(THIS->frame)
  {
    free_pike_frame(THIS->frame);
    THIS->frame=0;
  }
}

static void gc_check_frame(struct pike_frame *f)
{
  if(!f) return;
  if(!debug_gc_check(f,T_UNKNOWN,f) && f->malloced_locals)
  {
    if(f->current_object) gc_check(f->current_object);
    if(f->context.prog)   gc_check(f->context.prog);
    if(f->context.parent) gc_check(f->context.parent);
    gc_check_svalues(f->locals,f->num_locals);
    if(f->scope)          gc_check_frame(f->scope);
  }
}

static void gc_check_trampoline(struct object *o)
{
  gc_check_frame(THIS->frame);
}

static void gc_mark_frame(struct pike_frame *f)
{
  if(!f) return;
  if(gc_mark(f))
  {
    if(f->current_object) gc_mark_object_as_referenced(f->current_object);
    if(f->context.prog)   gc_mark_program_as_referenced(f->context.prog);
    if(f->context.parent) gc_mark_object_as_referenced(f->context.parent);
    if(f->malloced_locals)gc_mark_svalues(f->locals,f->num_locals);
    if(f->scope)          gc_mark_frame(f->scope);
  }
}

static void gc_mark_trampoline(struct object *o)
{
  gc_mark_frame(THIS->frame);
}


void init_program(void)
{
  int i;
  struct svalue key;
  struct svalue val;

  MAKE_CONSTANT_SHARED_STRING(this_program_string,"this_program");

  lfun_types = allocate_mapping(NUM_LFUNS);
  key.type = T_STRING;
  val.type = T_TYPE;
  for (i=0; i < NUM_LFUNS; i++) {
    key.u.string = make_shared_string(lfun_names[i]);
    val.u.string = make_pike_type(raw_lfun_types[i]);
    mapping_insert(lfun_types, &key, &val);
    free_string(val.u.string);
    free_string(key.u.string);
  }
  start_new_program();
  ADD_STORAGE(struct pike_trampoline);
  add_function("`()",apply_trampoline,"function(mixed...:mixed)",0);
  set_init_callback(init_trampoline);
  set_exit_callback(exit_trampoline);
  set_gc_check_callback(gc_check_trampoline);
  set_gc_mark_callback(gc_mark_trampoline);
  pike_trampoline_program=end_program();
}

void cleanup_program(void)
{
  int e;

  free_string(this_program_string);

  free_mapping(lfun_types);
#ifdef FIND_FUNCTION_HASHSIZE
  for(e=0;e<FIND_FUNCTION_HASHSIZE;e++)
  {
    if(cache[e].name)
    {
      free_string(cache[e].name);
      cache[e].name=0;
    }
  }
#endif

#ifdef DO_PIKE_CLEANUP
  if(resolve_cache)
  {
    free_mapping(dmalloc_touch (struct mapping *, resolve_cache));
    resolve_cache=0;
  }

  if(pike_trampoline_program)
  {
    free_program(pike_trampoline_program);
    pike_trampoline_program=0;
  }
#endif
}

#ifdef GC2

void gc_mark_program_as_referenced(struct program *p)
{
  if(gc_mark(p))
  {
    int e;
    for(e=0;e<p->num_constants;e++)
      gc_mark_svalues(& p->constants[e].sval, 1);

    for(e=0;e<p->num_inherits;e++)
    {
      if(p->inherits[e].parent)
	gc_mark_object_as_referenced(p->inherits[e].parent);

      if(e && p->inherits[e].prog)
	gc_mark_program_as_referenced(p->inherits[e].prog);
    }

  }
}

void gc_check_all_programs(void)
{
  struct program *p;
  for(p=first_program;p;p=p->next)
  {
    int e;

    debug_malloc_touch(p);

    for(e=0;e<p->num_constants;e++) {
      debug_gc_check_svalues(& p->constants[e].sval, 1, T_PROGRAM, p);
    }

    for(e=0;e<p->num_inherits;e++)
    {
      if(p->inherits[e].parent)
      {
#ifdef PIKE_DEBUG
	if(debug_gc_check(p->inherits[e].parent,T_PROGRAM,p)==-2)
	  fprintf(stderr,"(program at 0x%lx -> inherit[%d].parent)\n",
		  (long)p,
		  e);
#else
	debug_gc_check(p->inherits[e].parent, T_PROGRAM, p);
#endif
      }

      if(d_flag && p->inherits[e].name)
	debug_gc_check(p->inherits[e].name, T_PROGRAM, p);

      if(e && p->inherits[e].prog)
	debug_gc_check(p->inherits[e].prog, T_PROGRAM, p);
    }

#ifdef PIKE_DEBUG
    if(d_flag)
    {
      int e;
      for(e=0;e<(int)p->num_strings;e++)
	debug_gc_check(p->strings[e], T_PROGRAM, p);

      for(e=0;e<(int)p->num_identifiers;e++)
      {
	debug_gc_check(p->identifiers[e].name, T_PROGRAM, p);
	debug_gc_check(p->identifiers[e].type, T_PROGRAM, p);
      }
    }
#endif
  }
}

void gc_mark_all_programs(void)
{
  struct program *p;
  for(p=first_program;p;p=p->next)
    if(gc_is_referenced(p))
      gc_mark_program_as_referenced(p);
}

void gc_free_all_unreferenced_programs(void)
{
  struct program *p,*next;

  for(p=first_program;p;p=next)
  {
    if(gc_do_free(p))
    {
      int e;
      add_ref(p);
      for(e=0;e<p->num_constants;e++)
      {
	free_svalue(& p->constants[e].sval);
	p->constants[e].sval.type=T_INT;
	DO_IF_DMALLOC(p->constants[e].sval.u.refs=(void *)-1);
      }

      for(e=0;e<p->num_inherits;e++)
      {
	if(p->inherits[e].parent)
	{
	  free_object(p->inherits[e].parent);
	  p->inherits[e].parent=0;
	}
      }

      /* FIXME: Is there anything else that needs to be freed here? */
      SET_NEXT_AND_FREE(p, free_program);
    }else{
#ifdef PIKE_DEBUG
      int e,tmp=0;
      for(e=0;e<p->num_constants;e++)
      {
	if(p->constants[e].sval.type == T_PROGRAM && p->constants[e].sval.u.program == p)
	  tmp++;
      }
      if(tmp >= p->refs)
	fatal("garbage collector failed to free program!!!\n");
#endif      
      next=p->next;
    }
  }
}

#endif /* GC2 */


void count_memory_in_programs(INT32 *num_, INT32 *size_)
{
  INT32 size=0, num=0;
  struct program *p;
  for(p=first_program;p;p=p->next)
  {
    num++;
    size+=p->total_size;
  }
  *num_=num;
  *size_=size;
}

void push_compiler_frame(int lexical_scope)
{
  struct compiler_frame *f;
  f=ALLOC_STRUCT(compiler_frame);
  f->lexical_scope=lexical_scope;
  f->current_type=0;
  f->current_return_type=0;
  f->current_number_of_locals=0;
  f->max_number_of_locals=0;
  f->min_number_of_locals=0;
  f->previous=compiler_frame;
  compiler_frame=f;
}

void low_pop_local_variables(int level)
{
  while(compiler_frame->current_number_of_locals > level)
  {
    int e;
    e=--(compiler_frame->current_number_of_locals);
    free_string(compiler_frame->variable[e].name);
    free_string(compiler_frame->variable[e].type);
    if(compiler_frame->variable[e].def)
      free_node(compiler_frame->variable[e].def);
  }
}


void pop_local_variables(int level)
{
#if 1
  /* We need to save the variables Kuppo (but not their names) */
  if(level < compiler_frame->min_number_of_locals)
  {
    for(;level<compiler_frame->min_number_of_locals;level++)
    {
      free_string(compiler_frame->variable[level].name);
      MAKE_CONSTANT_SHARED_STRING(compiler_frame->variable[level].name,"");
    }
  }
#endif
  low_pop_local_variables(level);
}



void pop_compiler_frame(void)
{
  struct compiler_frame *f;
  int e;
  f=compiler_frame;
#ifdef PIKE_DEBUG
  if(!f)
    fatal("Popping out of compiler frames\n");
#endif

  low_pop_local_variables(0);
  if(f->current_type)
    free_string(f->current_type);

  if(f->current_return_type)
    free_string(f->current_return_type);

  compiler_frame=f->previous;
  dmfree((char *)f);
}


#define GET_STORAGE_CACHE_SIZE 1024
static struct get_storage_cache
{
  INT32 oid, pid, offset;
} get_storage_cache[GET_STORAGE_CACHE_SIZE];

int low_get_storage(struct program *o, struct program *p)
{
  INT32 oid,pid, offset;
  unsigned INT32 hval;
  if(!o) return 0;
  oid=o->id;
  pid=p->id;
  hval=oid*9248339 + pid;
  hval%=GET_STORAGE_CACHE_SIZE;
#ifdef PIKE_DEBUG
  if(hval>GET_STORAGE_CACHE_SIZE)
    fatal("hval>GET_STORAGE_CACHE_SIZE");
#endif
  if(get_storage_cache[hval].oid == oid &&
     get_storage_cache[hval].pid == pid)
  {
    offset=get_storage_cache[hval].offset;
  }else{
    INT32 e;
    offset=-1;
    for(e=0;e<o->num_inherits;e++)
    {
      if(o->inherits[e].prog==p)
      {
	offset=o->inherits[e].storage_offset;
	break;
      }
    }

    get_storage_cache[hval].oid=oid;
    get_storage_cache[hval].pid=pid;
    get_storage_cache[hval].offset=offset;
  }

  return offset;
}

char *get_storage(struct object *o, struct program *p)
{
  int offset;

#ifdef _REENTRANT
#ifndef __NT__
  if(d_flag)
    if(!mt_trylock(& interpreter_lock))
      fatal("get_storage running unlocked!\n");
#endif
#endif

  offset= low_get_storage(o->prog, p);
  if(offset == -1) return 0;
  return o->storage + offset;
}

struct program *low_program_from_function(struct program *p,
					  INT32 i)
{
  struct svalue *f;
  struct identifier *id=ID_FROM_INT(p, i);
  if(!IDENTIFIER_IS_CONSTANT(id->identifier_flags)) return 0;
  if(id->func.offset==-1) return 0;
  f=& PROG_FROM_INT(p,i)->constants[id->func.offset].sval;
  if(f->type!=T_PROGRAM) return 0;
  return f->u.program;
}

struct program *program_from_function(struct svalue *f)
{
  struct identifier *id;
  if(f->type != T_FUNCTION) return 0;
  if(f->subtype == FUNCTION_BUILTIN) return 0;
  if(!f->u.object->prog) return 0;
  return low_program_from_function(f->u.object->prog, f->subtype);
}

struct program *program_from_svalue(struct svalue *s)
{
  switch(s->type)
  {
    case T_OBJECT:
    {
      struct program *p = s->u.object->prog;
      int call_fun;

      if (!p) return 0;

      if ((call_fun = FIND_LFUN(p, LFUN_CALL)) >= 0) {
	/* Get the program from the return type. */
	struct identifier *id = ID_FROM_INT(p, call_fun);
	/* FIXME: do it. */
	/* fprintf(stderr, "Object type has `()().\n"); */
	return 0;
      }
      push_svalue(s);
      f_object_program(1);
      p=program_from_svalue(sp-1);
      pop_stack();
      return p; /* We trust that there is a reference somewhere... */
    }

  case T_FUNCTION:
    return program_from_function(s);
  case T_PROGRAM:
    return s->u.program;
  default:
    return 0;
  }
}

#define FIND_CHILD_HASHSIZE 5003
struct find_child_cache_s
{
  INT32 pid,cid,id;
};

static struct find_child_cache_s find_child_cache[FIND_CHILD_HASHSIZE];

int find_child(struct program *parent, struct program *child)
{
  unsigned INT32 h=(parent->id  * 9248339 + child->id);
  h= h % FIND_CHILD_HASHSIZE;
#ifdef PIKE_DEBUG
  if(h>=FIND_CHILD_HASHSIZE)
    fatal("find_child failed to hash within boundaries.\n");
#endif
  if(find_child_cache[h].pid == parent->id &&
     find_child_cache[h].cid == child->id)
  {
    return find_child_cache[h].id;
  }else{
    INT32 i;
    for(i=0;i<parent->num_identifier_references;i++)
    {
      if(low_program_from_function(parent, i)==child)
      {
	find_child_cache[h].pid=parent->id;
	find_child_cache[h].cid=child->id;
	find_child_cache[h].id=i;
	return i;
      }
    }
  }
  return -1;
}

void yywarning(char *fmt, ...) ATTRIBUTE((format(printf,1,2)))
{
  char buf[4711];
  va_list args;

  /* If we have parse errors we might get erroneous warnings,
   * so don't print them.
   * This has the additional benefit of making it easier to
   * visually locate the actual error message.
   */
  if (num_parse_error) return;

  va_start(args,fmt);
  VSPRINTF(buf, fmt, args);
  va_end(args);

  if(strlen(buf)>sizeof(buf))
    fatal("Buffer overfloat in yywarning!\n");

  if ((error_handler && error_handler->prog) || get_master()) {
    ref_push_string(lex.current_file);
    push_int(lex.current_line);
    push_text(buf);

    if (error_handler && error_handler->prog) {
      safe_apply(error_handler, "compile_warning", 3);
    } else {
      SAFE_APPLY_MASTER("compile_warning",3);
    }
    pop_stack();
  }
}



/* returns 1 if a implements b */
static int low_implements(struct program *a, struct program *b)
{
  int e;
  struct pike_string *s=findstring("__INIT");
  for(e=0;e<b->num_identifier_references;e++)
  {
    struct identifier *bid;
    int i;
    if (b->identifier_references[e].id_flags & (ID_STATIC|ID_HIDDEN))
      continue;		/* Skip static & hidden */
    bid = ID_FROM_INT(b,e);
    if(s == bid->name) continue;	/* Skip __INIT */
    i = find_shared_string_identifier(bid->name,a);
    if (i == -1) {
      if (b->identifier_references[e].id_flags & (ID_OPTIONAL))
	continue;		/* It's ok... */
#if 0
      fprintf(stderr, "Missing identifier \"%s\"\n", bid->name->str);
#endif /* 0 */
      return 0;
    }

    if(!match_types(ID_FROM_INT(a,i)->type, bid->type)) {
#if 0
      fprintf(stderr, "Identifier \"%s\" is incompatible.\n",
	      bid->name->str);
#endif /* 0 */
      return 0;
    }
  }
  return 1;
}

#define IMPLEMENTS_CACHE_SIZE 4711
struct implements_cache_s { INT32 aid, bid, ret; };
static struct implements_cache_s implements_cache[IMPLEMENTS_CACHE_SIZE];

/* returns 1 if a implements b, but faster */
int implements(struct program *a, struct program *b)
{
  unsigned long hval;
  if(!a || !b) return -1;
  if(a==b) return 1;

  hval = a->id*9248339 + b->id;
  hval %= IMPLEMENTS_CACHE_SIZE;
#ifdef PIKE_DEBUG
  if(hval >= IMPLEMENTS_CACHE_SIZE)
    fatal("Implements_cache failed!\n");
#endif
  if(implements_cache[hval].aid==a->id && implements_cache[hval].bid==b->id)
  {
    return implements_cache[hval].ret;
  }
  /* Do it the tedious way */
  implements_cache[hval].aid=a->id;
  implements_cache[hval].bid=b->id;
  implements_cache[hval].ret=low_implements(a,b);
  return implements_cache[hval].ret;
}

/* Returns 1 if a is compatible with b */
static int low_is_compatible(struct program *a, struct program *b)
{
  int e;
  struct pike_string *s=findstring("__INIT");

  /* Optimize the loop somewhat */
  if (a->num_identifier_references < b->num_identifier_references) {
    struct program *tmp = a;
    a = b;
    b = tmp;
  }

  for(e=0;e<b->num_identifier_references;e++)
  {
    struct identifier *bid;
    int i;
    if (b->identifier_references[e].id_flags & (ID_STATIC|ID_HIDDEN))
      continue;		/* Skip static & hidden */

    /* FIXME: What if they aren't static & hidden in a? */

    bid = ID_FROM_INT(b,e);
    if(s == bid->name) continue;	/* Skip __INIT */
    i = find_shared_string_identifier(bid->name,a);
    if (i == -1) {
      continue;		/* It's ok... */
    }

    if(!match_types(ID_FROM_INT(a,i)->type, bid->type)) {
#if 0
      fprintf(stderr, "Identifier \"%s\" is incompatible.\n",
	      bid->name->str);
#endif /* 0 */
      return 0;
    }
  }
  return 1;
}

static struct implements_cache_s is_compatible_cache[IMPLEMENTS_CACHE_SIZE];
/* Returns 1 if a is compatible with b
 * ie it's possible to write a hypothetical c that implements both.
 */
int is_compatible(struct program *a, struct program *b)
{
  unsigned long hval;
  unsigned long rhval;
  int aid, bid;
  if(!a || !b) return -1;
  if(a==b) return 1;

  /* Order the id's so we don't need double entries in the cache. */
  aid = a->id;
  bid = b->id;
  if (aid > bid) {
    int tmp = aid;
    aid = bid;
    bid = tmp;
  }

  hval = aid*9248339 + bid;
  hval %= IMPLEMENTS_CACHE_SIZE;
#ifdef PIKE_DEBUG
  if(hval >= IMPLEMENTS_CACHE_SIZE)
    fatal("Implements_cache failed!\n");
#endif
  if(is_compatible_cache[hval].aid==aid &&
     is_compatible_cache[hval].bid==bid)
  {
    return is_compatible_cache[hval].ret;
  }
  if(implements_cache[hval].aid==aid &&
     implements_cache[hval].bid==bid &&
     implements_cache[hval].ret)
  {
    /* a implements b */
    return 1;
  }
  rhval = bid*9248339 + aid;
  rhval %= IMPLEMENTS_CACHE_SIZE;
#ifdef PIKE_DEBUG
  if(rhval >= IMPLEMENTS_CACHE_SIZE)
    fatal("Implements_cache failed!\n");
#endif
  if(implements_cache[rhval].aid==bid &&
     implements_cache[rhval].bid==aid &&
     implements_cache[rhval].ret)
  {
    /* b implements a */
    return 1;
  }
  /* Do it the tedious way */
  is_compatible_cache[hval].aid=aid;
  is_compatible_cache[hval].bid=bid;
  is_compatible_cache[hval].ret=low_is_compatible(a,b);
  return is_compatible_cache[hval].ret;
}

/* returns 1 if a implements b */
int yyexplain_not_implements(struct program *a, struct program *b, int flags)
{
  int e;
  struct pike_string *s=findstring("__INIT");
  for(e=0;e<b->num_identifier_references;e++)
  {
    struct identifier *bid;
    int i;
    if (b->identifier_references[e].id_flags & (ID_STATIC|ID_HIDDEN))
      continue;		/* Skip static & hidden */
    bid = ID_FROM_INT(b,e);
    if(s == bid->name) continue;	/* Skip __INIT */
    i = find_shared_string_identifier(bid->name,a);
    if (i == -1) {
      if (b->identifier_references[e].id_flags & (ID_OPTIONAL))
	continue;		/* It's ok... */
      if(flags & YYTE_IS_WARNING)
	yywarning("Missing identifier \"%s\".", bid->name->str);
      else
	my_yyerror("Missing identifier \"%s\".", bid->name->str);
      return 0;
    }

    if(!match_types(ID_FROM_INT(a,i)->type, bid->type)) {
      struct pike_string *s1,*s2;
      my_yyerror("Type of identifier dentifier \"%s\" does not match.", bid->name->str);
      s1=describe_type(ID_FROM_INT(a,i)->type);
      s2=describe_type(bid->type);
      if(flags & YYTE_IS_WARNING)
      {
	yywarning("Expected: %s",s1->str);
	yywarning("Got     : %s",s2->str);
      }else{
	my_yyerror("Expected: %s",s1->str);
	my_yyerror("Got     : %s",s2->str);
      }

      free_string(s1);
      free_string(s2);
      return 0;
    }
  }
  return 1;
}

void *parent_storage(int depth)
{
  struct external_variable_context loc;
  struct program *p;


  loc.o=Pike_fp->current_object;
  p=loc.o->prog;
  if(!p)
  {
    /* magic fallback */
    p=get_program_for_object_being_destructed(loc.o);
    if(!p)
    {
      error("Cannot access parent of destructed object.\n");
    }
  }

  if(Pike_fp->fun == -1)
    error("Cannot access parent storage!\n");

  loc.parent_identifier=Pike_fp->fun;
  loc.inherit=INHERIT_FROM_INT(p, Pike_fp->fun);
  
  find_external_context(&loc, depth);

  return loc.o->storage + loc.inherit->storage_offset;
}
