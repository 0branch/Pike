/*\
||| This file a part of Pike, and is copyright by Fredrik Hubinette
||| Pike is distributed as GPL (General Public License)
||| See the files COPYING and DISCLAIMER for more information.
\*/
#include "global.h"
RCSID("$Id: interpret.c,v 1.109 1998/11/20 18:57:42 hubbe Exp $");
#include "interpret.h"
#include "object.h"
#include "program.h"
#include "svalue.h"
#include "array.h"
#include "mapping.h"
#include "error.h"
#include "language.h"
#include "stralloc.h"
#include "constants.h"
#include "pike_macros.h"
#include "multiset.h"
#include "backend.h"
#include "operators.h"
#include "opcodes.h"
#include "main.h"
#include "lex.h"
#include "builtin_functions.h"
#include "signal_handler.h"
#include "gc.h"
#include "threads.h"
#include "callback.h"
#include "fd_control.h"

#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_MMAP
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef MAP_NORESERVE
#define USE_MMAP_FOR_STACK
#endif
#endif

/*
 * Define the default evaluator stack size, used for just about everything.
 */
#define EVALUATOR_STACK_SIZE	100000

#define TRACE_LEN (100 + t_flag * 10)


/* sp points to first unused value on stack
 * (much simpler than letting it point at the last used value.)
 */
struct svalue *sp;     /* Current position */
struct svalue *evaluator_stack; /* Start of stack */
int stack_size = EVALUATOR_STACK_SIZE;
int evaluator_stack_malloced = 0;
char *stack_top;

/* mark stack, used to store markers into the normal stack */
struct svalue **mark_sp; /* Current position */
struct svalue **mark_stack; /* Start of stack */
int mark_stack_malloced = 0;

#ifdef PROFILING
#ifdef HAVE_GETHRTIME
long long accounted_time =0;
long long time_base =0;
#endif
#endif

void push_sp_mark(void)
{
  if(mark_sp == mark_stack + stack_size)
    error("No more mark stack!\n");
  *mark_sp++=sp;
}
int pop_sp_mark(void)
{
#ifdef DEBUG
  if(mark_sp < mark_stack)
    fatal("Mark stack underflow!\n");
#endif
  return sp - *--mark_sp;
}

struct frame *fp; /* frame pointer */

#ifdef DEBUG
static void gc_check_stack_callback(struct callback *foo, void *bar, void *gazonk)
{
  struct frame *f;
  debug_gc_xmark_svalues(evaluator_stack,sp-evaluator_stack-1,"interpreter stack");

  for(f=fp;f;f=f->parent_frame)
  {
    if(f->context.parent)
      gc_external_mark(f->context.parent);
    gc_external_mark(f->current_object);
    gc_external_mark(f->context.prog);
  }

}
#endif

void init_interpreter(void)
{
#ifdef USE_MMAP_FOR_STACK
  static int fd = -1;


#ifndef MAP_VARIABLE
#define MAP_VARIABLE 0
#endif

#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0
#endif

#ifndef MAP_FAILED
#define MAP_FAILED -1
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0
  if(fd == -1)
  {
    while(1)
    {
      fd=open("/dev/zero",O_RDONLY);
      if(fd >= 0) break;
      if(errno != EINTR)
      {
	evaluator_stack=0;
	mark_stack=0;
	goto use_malloc;
      }
    }
    /* Don't keep this fd on exec() */
    set_close_on_exec(fd, 1);
  }
#endif

#define MMALLOC(X,Y) (Y *)mmap(0,X*sizeof(Y),PROT_READ|PROT_WRITE, MAP_NORESERVE | MAP_PRIVATE | MAP_ANONYMOUS, fd, 0)

  evaluator_stack_malloced=0;
  mark_stack_malloced=0;
  evaluator_stack=MMALLOC(stack_size,struct svalue);
  mark_stack=MMALLOC(stack_size, struct svalue *);
  if((char *)MAP_FAILED == (char *)evaluator_stack) evaluator_stack=0;
  if((char *)MAP_FAILED == (char *)mark_stack) mark_stack=0;
#else
  evaluator_stack=0;
  mark_stack=0;
#endif

use_malloc:
  if(!evaluator_stack)
  {
    evaluator_stack=(struct svalue *)xalloc(stack_size*sizeof(struct svalue));
    evaluator_stack_malloced=1;
  }

  if(!mark_stack)
  {
    mark_stack=(struct svalue **)xalloc(stack_size*sizeof(struct svalue *));
    mark_stack_malloced=1;
  }

  sp=evaluator_stack;
  mark_sp=mark_stack;
  fp=0;

#ifdef DEBUG
  {
    static struct callback *spcb;
    if(!spcb)
    {
      spcb=add_gc_callback(gc_check_stack_callback,0,0);
    }
  }
#endif
#ifdef PROFILING
#ifdef HAVE_GETHRTIME
  time_base = gethrtime();
  accounted_time =0;
#endif
#endif
}

void check_stack(INT32 size)
{
  if(sp - evaluator_stack + size >= stack_size)
    error("Stack overflow.\n");
}

void check_mark_stack(INT32 size)
{
  if(mark_sp - mark_stack + size >= stack_size)
    error("Mark stack overflow.\n");
}

void check_c_stack(INT32 size)
{
  long x=((char *)&size) + STACK_DIRECTION * size - stack_top ;
  x*=STACK_DIRECTION;
  if(x>0)
    error("C stack overflow.\n");
}


static int eval_instruction(unsigned char *pc);


/*
 * lvalues are stored in two svalues in one of these formats:
 * array[index]   : { array, index } 
 * mapping[index] : { mapping, index } 
 * multiset[index] : { multiset, index } 
 * object[index] : { object, index }
 * local variable : { svalue_pointer, nothing } 
 * global variable : { svalue_pointer/short_svalue_pointer, nothing } 
 */

void lvalue_to_svalue_no_free(struct svalue *to,struct svalue *lval)
{
  switch(lval->type)
  {
    case T_ARRAY_LVALUE:
    {
      INT32 e;
      struct array *a;
      ONERROR err;
      a=allocate_array(lval[1].u.array->size>>1);
      SET_ONERROR(err, do_free_array, a);
      for(e=0;e<a->size;e++)
	lvalue_to_svalue_no_free(a->item+e, lval[1].u.array->item+(e<<1));
      to->type = T_ARRAY;
      to->u.array=a;
      UNSET_ONERROR(err);
      break;
    }
      
    case T_LVALUE:
      assign_svalue_no_free(to, lval->u.lval);
      break;
      
    case T_SHORT_LVALUE:
      assign_from_short_svalue_no_free(to, lval->u.short_lval, lval->subtype);
      break;
      
    case T_OBJECT:
      object_index_no_free(to, lval->u.object, lval+1);
      break;
      
    case T_ARRAY:
      simple_array_index_no_free(to, lval->u.array, lval+1);
      break;
      
    case T_MAPPING:
      mapping_index_no_free(to, lval->u.mapping, lval+1);
      break;
      
    case T_MULTISET:
      to->type=T_INT;
      if(multiset_member(lval->u.multiset,lval+1))
      {
	to->u.integer=0;
	to->subtype=NUMBER_UNDEFINED;
      }else{
	to->u.integer=0;
	to->subtype=NUMBER_NUMBER;
      }
      break;
      
    default:
      if(IS_ZERO(lval))
	error("Indexing the NULL value.\n"); /* Per */
      else
	error("Indexing a basic type.\n");
  }
}

void assign_lvalue(struct svalue *lval,struct svalue *from)
{
  switch(lval->type)
  {
    case T_ARRAY_LVALUE:
    {
      INT32 e;
      if(from->type != T_ARRAY)
	error("Trying to assign combined lvalue from non-array.\n");

      if(from->u.array->size < (lval[1].u.array->size>>1))
	error("Not enough values for multiple assign.\n");

      for(e=0;e<from->u.array->size;e++)
	assign_lvalue(lval[1].u.array->item+(e<<1),from->u.array->item+e);
    }
    break;

  case T_LVALUE:
    assign_svalue(lval->u.lval,from);
    break;

  case T_SHORT_LVALUE:
    assign_to_short_svalue(lval->u.short_lval, lval->subtype, from);
    break;

  case T_OBJECT:
    object_set_index(lval->u.object, lval+1, from);
    break;

  case T_ARRAY:
    simple_set_index(lval->u.array, lval+1, from);
    break;

  case T_MAPPING:
    mapping_insert(lval->u.mapping, lval+1, from);
    break;

  case T_MULTISET:
    if(IS_ZERO(from))
      multiset_delete(lval->u.multiset, lval+1);
    else
      multiset_insert(lval->u.multiset, lval+1);
    break;
    
  default:
   if(IS_ZERO(lval))
     error("Indexing the NULL value.\n"); /* Per */
   else
     error("Indexing a basic type.\n");
  }
}

union anything *get_pointer_if_this_type(struct svalue *lval, TYPE_T t)
{
  switch(lval->type)
  {
    case T_ARRAY_LVALUE:
      return 0;
      
    case T_LVALUE:
      if(lval->u.lval->type == t) return & ( lval->u.lval->u );
      return 0;
      
    case T_SHORT_LVALUE:
      if(lval->subtype == t) return lval->u.short_lval;
      return 0;
      
    case T_OBJECT:
      return object_get_item_ptr(lval->u.object,lval+1,t);
      
    case T_ARRAY:
      return array_get_item_ptr(lval->u.array,lval+1,t);
      
    case T_MAPPING:
      return mapping_get_item_ptr(lval->u.mapping,lval+1,t);

    case T_MULTISET: return 0;
      
    default:
      if(IS_ZERO(lval))
	error("Indexing the NULL value.\n"); /* Per */
    else
      error("Indexing a basic type.\n");
      return 0;
  }
}

#ifdef DEBUG
void print_return_value(void)
{
  if(t_flag>3)
  {
    char *s;
	
    init_buf();
    describe_svalue(sp-1,0,0);
    s=simple_free_buf();
    if((long)strlen(s) > (long)TRACE_LEN)
    {
      s[TRACE_LEN]=0;
      s[TRACE_LEN-1]='.';
      s[TRACE_LEN-2]='.';
      s[TRACE_LEN-2]='.';
    }
    fprintf(stderr,"-    value: %s\n",s);
    free(s);
  }
}
#else
#define print_return_value()
#endif

struct callback_list evaluator_callbacks;

#ifdef DEBUG
static char trace_buffer[200];
#define GET_ARG() (backlog[backlogp].arg=(\
  instr=prefix,\
  prefix=0,\
  instr+=EXTRACT_UCHAR(pc++),\
  (t_flag>3 ? sprintf(trace_buffer,"-    Arg = %ld\n",(long)instr),write_to_stderr(trace_buffer,strlen(trace_buffer)) : 0),\
  instr))
#define GET_ARG2() (\
  instr=EXTRACT_UCHAR(pc++),\
  (t_flag>3 ? sprintf(trace_buffer,"-    Arg2= %ld\n",(long)instr),write_to_stderr(trace_buffer,strlen(trace_buffer)) : 0),\
  instr)


#else
#define GET_ARG() (instr=prefix,prefix=0,instr+EXTRACT_UCHAR(pc++))
#define GET_ARG2() EXTRACT_UCHAR(pc++)
#endif

#define CASE(X) case (X)-F_OFFSET:

#define DOJUMP() \
 do { int tmp; tmp=EXTRACT_INT(pc); pc+=tmp; if(tmp < 0) fast_check_threads_etc(6); }while(0)

#define COMPARISMENT(ID,EXPR) \
CASE(ID); \
instr=EXPR; \
pop_n_elems(2); \
push_int(instr); \
break

#define LOOP(ID, OP1, OP2, OP3, OP4)				\
CASE(ID)							\
{								\
  union anything *i=get_pointer_if_this_type(sp-2, T_INT);	\
  if(i)								\
  {								\
    OP1 ( i->integer );						\
    if(i->integer OP2 sp[-3].u.integer)				\
    {								\
      pc+=EXTRACT_INT(pc);					\
      fast_check_threads_etc(8);				\
    }else{							\
      pc+=sizeof(INT32);					\
    }								\
  }else{							\
    lvalue_to_svalue_no_free(sp-2,sp); sp++;			\
    push_int(1);						\
    OP3;							\
    assign_lvalue(sp-3,sp-1);					\
    if(OP4 ( sp-1, sp-4 ))					\
    {								\
      pc+=EXTRACT_INT(pc);					\
      fast_check_threads_etc(8);				\
    }else{							\
      pc+=sizeof(INT32);					\
    }								\
    pop_stack();						\
  }								\
  break;							\
}

#define CJUMP(X,Y) \
CASE(X); \
if(Y(sp-2,sp-1)) { \
  DOJUMP(); \
}else{ \
  pc+=sizeof(INT32); \
} \
pop_n_elems(2); \
break


/*
 * reset the stack machine.
 */
void reset_evaluator(void)
{
  fp=0;
  pop_n_elems(sp - evaluator_stack);
}

#ifdef DEBUG
#define BACKLOG 512
struct backlog
{
  INT32 instruction;
  INT32 arg;
  struct program *program;
  unsigned char *pc;
};

struct backlog backlog[BACKLOG];
int backlogp=BACKLOG-1;

void dump_backlog(void)
{
  int e;
  if(!d_flag || backlogp<0 || backlogp>=BACKLOG)
    return;

  e=backlogp;
  do
  {
    e++;
    if(e>=BACKLOG) e=0;

    if(backlog[e].program)
    {
      char *file;
      INT32 line;

      file=get_line(backlog[e].pc-1,backlog[e].program, &line);
      fprintf(stderr,"%s:%ld: %s(%ld)\n",
	      file,
	      (long)line,
	      low_get_f_name(backlog[e].instruction + F_OFFSET, backlog[e].program),
	      (long)backlog[e].arg);
    }
  }while(e!=backlogp);
}

#endif

static int o_catch(unsigned char *pc);

static int eval_instruction(unsigned char *pc)
{
  unsigned INT32 accumulator=0,instr, prefix=0;
  while(1)
  {
    fp->pc = pc;
    instr=EXTRACT_UCHAR(pc++);

#ifdef DEBUG
    if(d_flag)
    {
#if defined(_REENTRANT) && !defined(__NT__)
      if(!mt_trylock(& interpreter_lock))
	fatal("Interpreter running unlocked!\n");
#endif
      sp[0].type=99; /* an invalid type */
      sp[1].type=99;
      sp[2].type=99;
      sp[3].type=99;
      
      if(sp<evaluator_stack || mark_sp < mark_stack || fp->locals>sp)
	fatal("Stack error (generic).\n");
      
      if(sp > evaluator_stack+stack_size)
	fatal("Stack error (overflow).\n");
      
      if(fp->fun>=0 && fp->current_object->prog &&
	 fp->locals+fp->num_locals > sp)
	fatal("Stack error (stupid!).\n");

      if(recoveries && sp-evaluator_stack < recoveries->sp)
	fatal("Stack error (underflow).\n");

      if(mark_sp > mark_stack && mark_sp[-1] > sp)
	fatal("Stack error (underflow?)\n");
      
      if(d_flag > 9) do_debug();

      backlogp++;
      if(backlogp >= BACKLOG) backlogp=0;

      if(backlog[backlogp].program)
	free_program(backlog[backlogp].program);

      backlog[backlogp].program=fp->context.prog;
      add_ref(fp->context.prog);
      backlog[backlogp].instruction=instr;
      backlog[backlogp].arg=0;
      backlog[backlogp].pc=pc;
    }

    if(t_flag > 2)
    {
      char *file, *f;
      INT32 linep;

      file=get_line(pc-1,fp->context.prog,&linep);
      while((f=STRCHR(file,'/'))) file=f+1;
      fprintf(stderr,"- %s:%4ld:(%lx): %-25s %4ld %4ld\n",
	      file,(long)linep,
	      (long)(pc-fp->context.prog->program-1),
	      get_f_name(instr + F_OFFSET),
	      (long)(sp-evaluator_stack),
	      (long)(mark_sp-mark_stack));
    }

    if(instr + F_OFFSET < F_MAX_OPCODE) 
      ADD_RUNNED(instr + F_OFFSET);
#endif

    switch(instr)
    {
      /* Support to allow large arguments */
      CASE(F_PREFIX_256); prefix+=256; break;
      CASE(F_PREFIX_512); prefix+=512; break;
      CASE(F_PREFIX_768); prefix+=768; break;
      CASE(F_PREFIX_1024); prefix+=1024; break;
      CASE(F_PREFIX_24BITX256);
      prefix+=EXTRACT_UCHAR(pc++)<<24;
      CASE(F_PREFIX_WORDX256);
      prefix+=EXTRACT_UCHAR(pc++)<<16;
      CASE(F_PREFIX_CHARX256);
      prefix+=EXTRACT_UCHAR(pc++)<<8;
      break;

      CASE(F_LDA); accumulator=GET_ARG(); break;

      /* Push number */
      CASE(F_CONST0); push_int(0); break;
      CASE(F_CONST1); push_int(1); break;
      CASE(F_CONST_1); push_int(-1); break;
      CASE(F_BIGNUM); push_int(0x7fffffff); break;
      CASE(F_NUMBER); push_int(GET_ARG()); break;
      CASE(F_NEG_NUMBER); push_int(-GET_ARG()); break;

      /* The rest of the basic 'push value' instructions */	

      CASE(F_MARK_AND_STRING); *(mark_sp++)=sp;
      CASE(F_STRING);
      copy_shared_string(sp->u.string,fp->context.prog->strings[GET_ARG()]);
      sp->type=T_STRING;
      sp->subtype=0;
      sp++;
      print_return_value();
      break;

      CASE(F_ARROW_STRING);
      copy_shared_string(sp->u.string,fp->context.prog->strings[GET_ARG()]);
      sp->type=T_STRING;
      sp->subtype=1; /* Magic */
      sp++;
      print_return_value();
      break;

      CASE(F_CONSTANT);
      assign_svalue_no_free(sp++,fp->context.prog->constants+GET_ARG());
      print_return_value();
      break;

      CASE(F_FLOAT);
      sp->type=T_FLOAT;
      MEMCPY((void *)&sp->u.float_number, pc, sizeof(FLOAT_TYPE));
      pc+=sizeof(FLOAT_TYPE);
      sp++;
      break;

      CASE(F_LFUN);
      sp->u.object=fp->current_object;
      add_ref(fp->current_object);
      sp->subtype=GET_ARG()+fp->context.identifier_level;
      sp->type=T_FUNCTION;
      sp++;
      print_return_value();
      break;

      /* The not so basic 'push value' instructions */
      CASE(F_GLOBAL);
      low_object_index_no_free(sp,
			       fp->current_object,
			       GET_ARG() + fp->context.identifier_level);
      sp++;
      print_return_value();
      break;

      CASE(F_EXTERNAL);
      {
	struct inherit *inherit;
	struct program *p;
	struct object *o;
	INT32 i,id=GET_ARG();

	inherit=&fp->context;
	
	o=fp->current_object;

	if(!o)
	  error("Current object is destructed\n");
	
	while(1)
	{
	  if(inherit->parent_offset)
	  {
#ifdef DEBUG
	    if(t_flag>4)
	    {
	      sprintf(trace_buffer,"-   Following o->parent (accumulator+=%d)\n",inherit->parent_offset-1);
	      write_to_stderr(trace_buffer,strlen(trace_buffer));
	    }
#endif

	    i=o->parent_identifier;
	    o=o->parent;
	    accumulator+=inherit->parent_offset-1;
	  }else{
#ifdef DEBUG
	    if(t_flag>4)
	    {
	      sprintf(trace_buffer,"-   Following inherit->parent (accumulator+=%d)\n",inherit->parent_offset-1);
	      write_to_stderr(trace_buffer,strlen(trace_buffer));
	    }
#endif
	    i=inherit->parent_identifier;
	    o=inherit->parent;
	  }
	  
	  if(!o)
	    error("Parent was lost during cloning.\n");
	  
	  if(!(p=o->prog))
	    error("Attempting to access variable in destructed object\n");
	  
	  inherit=INHERIT_FROM_INT(p, i);
	  
	  if(!accumulator) break;
	  --accumulator;
	}

	low_object_index_no_free(sp,
				 o,
				 id + inherit->identifier_level);
	sp++;
	print_return_value();
	break;
      }

      CASE(F_EXTERNAL_LVALUE);
      {
	struct inherit *inherit;
	struct program *p;
	struct object *o;
	INT32 i,id=GET_ARG();

	inherit=&fp->context;
	o=fp->current_object;
	
	if(!o)
	  error("Current object is destructed\n");

	while(1)
	{
	  if(inherit->parent_offset)
	  {
	    i=o->parent_identifier;
	    o=o->parent;
	    accumulator+=inherit->parent_offset-1;
	  }else{
	    i=inherit->parent_identifier;
	    o=inherit->parent;
	  }

	  if(!o)
	    error("Parent no longer exists\n");

	  if(!(p=o->prog))
	    error("Attempting to access variable in destructed object\n");

	  inherit=INHERIT_FROM_INT(p, i);

	  if(!accumulator) break;
	  accumulator--;
	}

	ref_push_object(o);
	sp->type=T_LVALUE;
	sp->u.integer=id + inherit->identifier_level;
	sp++;
	break;
      }


      CASE(F_MARK_AND_LOCAL); *(mark_sp++)=sp;
      CASE(F_LOCAL);
      assign_svalue_no_free(sp++,fp->locals+GET_ARG());
      print_return_value();
      break;

      CASE(F_2_LOCALS);
      assign_svalue_no_free(sp++,fp->locals+GET_ARG());
      print_return_value();
      assign_svalue_no_free(sp++,fp->locals+GET_ARG2());
      print_return_value();
      break;

      CASE(F_LOCAL_2_LOCAL);
      {
	int tmp=GET_ARG();
	assign_svalue(fp->locals+tmp, fp->locals+GET_ARG2());
	break;
      }

      CASE(F_LOCAL_2_GLOBAL);
      {
	INT32 tmp=GET_ARG() + fp->context.identifier_level;
	struct identifier *i;

	if(!fp->current_object->prog)
	  error("Cannot access global variables in destructed object.\n");

	i=ID_FROM_INT(fp->current_object->prog, tmp);
	if(!IDENTIFIER_IS_VARIABLE(i->identifier_flags))
	  error("Cannot assign functions or constants.\n");
	if(i->run_time_type == T_MIXED)
	{
	  assign_svalue((struct svalue *)GLOBAL_FROM_INT(tmp), fp->locals + GET_ARG2());
	}else{
	  assign_to_short_svalue((union anything *)GLOBAL_FROM_INT(tmp),
				 i->run_time_type,
				 fp->locals + GET_ARG2());
	}
	break;
      }

      CASE(F_GLOBAL_2_LOCAL);
      {
	INT32 tmp=GET_ARG() + fp->context.identifier_level;
	INT32 tmp2=GET_ARG2();
	free_svalue(fp->locals + tmp2);
	low_object_index_no_free(fp->locals + tmp2,
				 fp->current_object,
				 tmp);
	break;
      }

      CASE(F_LOCAL_LVALUE);
      sp[0].type=T_LVALUE;
      sp[0].u.lval=fp->locals+GET_ARG();
      sp[1].type=T_VOID;
      sp+=2;
      break;

      CASE(F_ARRAY_LVALUE);
      f_aggregate(GET_ARG()*2);
      sp[-1].u.array->flags |= ARRAY_LVALUE;
      sp[-1].u.array->type_field |= BIT_UNFINISHED | BIT_MIXED;
      sp[0]=sp[-1];
      sp[-1].type=T_ARRAY_LVALUE;
      sp++;
      break;

      CASE(F_CLEAR_2_LOCAL);
      instr=GET_ARG();
      free_svalues(fp->locals + instr, 2, -1);
      fp->locals[instr].type=T_INT;
      fp->locals[instr].subtype=0;
      fp->locals[instr].u.integer=0;
      fp->locals[instr+1].type=T_INT;
      fp->locals[instr+1].subtype=0;
      fp->locals[instr+1].u.integer=0;
      break;

      CASE(F_CLEAR_4_LOCAL);
      {
	int e;
	instr=GET_ARG();
	free_svalues(fp->locals + instr, 4, -1);
	for(e=0;e<4;e++)
	{
	  fp->locals[instr+e].type=T_INT;
	  fp->locals[instr+e].subtype=0;
	  fp->locals[instr+e].u.integer=0;
	}
	break;
      }

      CASE(F_CLEAR_LOCAL);
      instr=GET_ARG();
      free_svalue(fp->locals + instr);
      fp->locals[instr].type=T_INT;
      fp->locals[instr].subtype=0;
      fp->locals[instr].u.integer=0;
      break;

      CASE(F_INC_LOCAL);
      instr=GET_ARG();
      if(fp->locals[instr].type == T_INT)
      {
        fp->locals[instr].u.integer++;
	assign_svalue_no_free(sp++,fp->locals+instr);
      }else{
	assign_svalue_no_free(sp++,fp->locals+instr);
	push_int(1);
	f_add(2);
	assign_svalue(fp->locals+instr,sp-1);
      }
      break;

      CASE(F_POST_INC_LOCAL);
      instr=GET_ARG();
      assign_svalue_no_free(sp++,fp->locals+instr);
      goto inc_local_and_pop;

      CASE(F_INC_LOCAL_AND_POP);
      instr=GET_ARG();
    inc_local_and_pop:
      if(fp->locals[instr].type == T_INT)
      {
	fp->locals[instr].u.integer++;
      }else{
	assign_svalue_no_free(sp++,fp->locals+instr);
	push_int(1);
	f_add(2);
	assign_svalue(fp->locals+instr,sp-1);
	pop_stack();
      }
      break;

      CASE(F_DEC_LOCAL);
      instr=GET_ARG();
      if(fp->locals[instr].type == T_INT)
      {
	fp->locals[instr].u.integer--;
	assign_svalue_no_free(sp++,fp->locals+instr);
      }else{
	assign_svalue_no_free(sp++,fp->locals+instr);
	push_int(1);
	o_subtract();
	assign_svalue(fp->locals+instr,sp-1);
      }
      break;

      CASE(F_POST_DEC_LOCAL);
      instr=GET_ARG();
      assign_svalue_no_free(sp++,fp->locals+instr);
      goto dec_local_and_pop;
      /* fp->locals[instr].u.integer--; */
      break;

      CASE(F_DEC_LOCAL_AND_POP);
      instr=GET_ARG();
    dec_local_and_pop:
      if(fp->locals[instr].type == T_INT)
      {
	fp->locals[instr].u.integer--;
      }else{
	assign_svalue_no_free(sp++,fp->locals+instr);
	push_int(1);
	o_subtract();
	assign_svalue(fp->locals+instr,sp-1);
	pop_stack();
      }
      break;

      CASE(F_LTOSVAL);
      lvalue_to_svalue_no_free(sp,sp-2);
      sp++;
      break;

      CASE(F_LTOSVAL2);
      sp[0]=sp[-1];
      lvalue_to_svalue_no_free(sp-1,sp-3);

      /* this is so that foo+=bar (and similar things) will be faster, this
       * is done by freeing the old reference to foo after it has been pushed
       * on the stack. That way foo can have only 1 reference if we are lucky,
       * and then the low array/multiset/mapping manipulation routines can be
       * destructive if they like
       */
      if( (1 << sp[-1].type) & ( BIT_ARRAY | BIT_MULTISET | BIT_MAPPING | BIT_STRING ))
      {
	struct svalue s;
	s.type=T_INT;
	s.subtype=0;
	s.u.integer=0;
	assign_lvalue(sp-3,&s);
      }
      sp++;
      break;


      CASE(F_ADD_TO_AND_POP);
      sp[0]=sp[-1];
      lvalue_to_svalue_no_free(sp-1,sp-3);

      /* this is so that foo+=bar (and similar things) will be faster, this
       * is done by freeing the old reference to foo after it has been pushed
       * on the stack. That way foo can have only 1 reference if we are lucky,
       * and then the low array/multiset/mapping manipulation routines can be
       * destructive if they like
       */
      if( (1 << sp[-1].type) & ( BIT_ARRAY | BIT_MULTISET | BIT_MAPPING | BIT_STRING ))
      {
	struct svalue s;
	s.type=T_INT;
	s.subtype=0;
	s.u.integer=0;
	assign_lvalue(sp-3,&s);
      }
      sp++;
      f_add(2);
      assign_lvalue(sp-3,sp-1);
      pop_n_elems(3);
      break;

      CASE(F_GLOBAL_LVALUE);
      {
	struct identifier *i;
	INT32 tmp=GET_ARG() + fp->context.identifier_level;

	if(!fp->current_object->prog)
	  error("Cannot access global variables in destructed object.\n");

	i=ID_FROM_INT(fp->current_object->prog, tmp);

	if(!IDENTIFIER_IS_VARIABLE(i->identifier_flags))
	  error("Cannot re-assign functions or constants.\n");

	if(i->run_time_type == T_MIXED)
	{
	  sp[0].type=T_LVALUE;
	  sp[0].u.lval=(struct svalue *)GLOBAL_FROM_INT(tmp);
	}else{
	  sp[0].type=T_SHORT_LVALUE;
	  sp[0].u.short_lval= (union anything *)GLOBAL_FROM_INT(tmp);
	  sp[0].subtype=i->run_time_type;
	}
	sp[1].type=T_VOID;
	sp+=2;
	break;
      }
      
      CASE(F_INC);
      {
	union anything *u=get_pointer_if_this_type(sp-2, T_INT);
	if(u)
	{
	  instr=++ u->integer;
	  pop_n_elems(2);
	  push_int(u->integer);
	}else{
	  lvalue_to_svalue_no_free(sp, sp-2); sp++;
	  push_int(1);
	  f_add(2);
	  assign_lvalue(sp-3, sp-1);
	  assign_svalue(sp-3, sp-1);
	  pop_n_elems(2);
	}
	break;
      }

      CASE(F_DEC);
      {
	union anything *u=get_pointer_if_this_type(sp-2, T_INT);
	if(u)
	{
	  instr=-- u->integer;
	  pop_n_elems(2);
	  push_int(u->integer);
	}else{
	  lvalue_to_svalue_no_free(sp, sp-2); sp++;
	  push_int(1);
	  o_subtract();
	  assign_lvalue(sp-3, sp-1);
	  assign_svalue(sp-3, sp-1);
	  pop_n_elems(2);
	}
	break;
      }

      CASE(F_DEC_AND_POP);
      {
	union anything *u=get_pointer_if_this_type(sp-2, T_INT);
	if(u)
	{
	  instr=-- u->integer;
	  pop_n_elems(2);
	}else{
	  lvalue_to_svalue_no_free(sp, sp-2); sp++;
	  push_int(1);
	  o_subtract();
	  assign_lvalue(sp-3, sp-1);
	  pop_n_elems(3);
	}
	break;
      }

      CASE(F_INC_AND_POP);
      {
	union anything *u=get_pointer_if_this_type(sp-2, T_INT);
	if(u)
	{
	  instr=++ u->integer;
	  pop_n_elems(2);
	}else{
	  lvalue_to_svalue_no_free(sp, sp-2); sp++;
	  push_int(1);
	  f_add(2);
	  assign_lvalue(sp-3, sp-1);
	  pop_n_elems(3);
	}
	break;
      }

      CASE(F_POST_INC);
      {
	union anything *u=get_pointer_if_this_type(sp-2, T_INT);
	if(u)
	{
	  instr=u->integer ++;
	  pop_n_elems(2);
	  push_int(instr);
	}else{
	  lvalue_to_svalue_no_free(sp, sp-2); sp++;
	  assign_svalue_no_free(sp,sp-1); sp++;
	  push_int(1);
	  f_add(2);
	  assign_lvalue(sp-4, sp-1);
	  assign_svalue(sp-4, sp-2);
	  pop_n_elems(3);
	}
	break;
      }

      CASE(F_POST_DEC);
      {
	union anything *u=get_pointer_if_this_type(sp-2, T_INT);
	if(u)
	{
	  instr=u->integer --;
	  pop_n_elems(2);
	  push_int(instr);
	}else{
	  lvalue_to_svalue_no_free(sp, sp-2); sp++;
	  assign_svalue_no_free(sp,sp-1); sp++;
	  push_int(1);
	  o_subtract();
	  assign_lvalue(sp-4, sp-1);
	  assign_svalue(sp-4, sp-2);
	  pop_n_elems(3);
	}
	break;
      }

      CASE(F_ASSIGN);
      assign_lvalue(sp-3,sp-1);
      free_svalue(sp-3);
      free_svalue(sp-2);
      sp[-3]=sp[-1];
      sp-=2;
      break;

      CASE(F_ASSIGN_AND_POP);
      assign_lvalue(sp-3,sp-1);
      pop_n_elems(3);
      break;

    CASE(F_APPLY_ASSIGN_LOCAL);
      strict_apply_svalue(fp->context.prog->constants + GET_ARG(), sp - *--mark_sp );
      /* Fall through */

      CASE(F_ASSIGN_LOCAL);
      assign_svalue(fp->locals+GET_ARG(),sp-1);
      break;

    CASE(F_APPLY_ASSIGN_LOCAL_AND_POP);
      strict_apply_svalue(fp->context.prog->constants + GET_ARG(), sp - *--mark_sp );
      /* Fall through */

      CASE(F_ASSIGN_LOCAL_AND_POP);
      instr=GET_ARG();
      free_svalue(fp->locals+instr);
      fp->locals[instr]=sp[-1];
      sp--;
      break;

      CASE(F_ASSIGN_GLOBAL)
      {
	struct identifier *i;
	INT32 tmp=GET_ARG() + fp->context.identifier_level;
	if(!fp->current_object->prog)
	  error("Cannot access global variables in destructed object.\n");

	i=ID_FROM_INT(fp->current_object->prog, tmp);
	if(!IDENTIFIER_IS_VARIABLE(i->identifier_flags))
	  error("Cannot assign functions or constants.\n");
	if(i->run_time_type == T_MIXED)
	{
	  assign_svalue((struct svalue *)GLOBAL_FROM_INT(tmp), sp-1);
	}else{
	  assign_to_short_svalue((union anything *)GLOBAL_FROM_INT(tmp),
				 i->run_time_type,
				 sp-1);
	}
      }
      break;

      CASE(F_ASSIGN_GLOBAL_AND_POP)
      {
	struct identifier *i;
	INT32 tmp=GET_ARG() + fp->context.identifier_level;
	if(!fp->current_object->prog)
	  error("Cannot access global variables in destructed object.\n");

	i=ID_FROM_INT(fp->current_object->prog, tmp);
	if(!IDENTIFIER_IS_VARIABLE(i->identifier_flags))
	  error("Cannot assign functions or constants.\n");

	if(i->run_time_type == T_MIXED)
	{
	  struct svalue *s=(struct svalue *)GLOBAL_FROM_INT(tmp);
	  free_svalue(s);
	  sp--;
	  *s=*sp;
	}else{
	  assign_to_short_svalue((union anything *)GLOBAL_FROM_INT(tmp),
				 i->run_time_type,
				 sp-1);
	  pop_stack();
	}
      }
      break;

      /* Stack machine stuff */
      CASE(F_POP_VALUE); pop_stack(); break;
      CASE(F_POP_N_ELEMS); pop_n_elems(GET_ARG()); break;
      CASE(F_MARK2); *(mark_sp++)=sp;
      CASE(F_MARK); *(mark_sp++)=sp; break;
      CASE(F_MARK_X); *(mark_sp++)=sp-GET_ARG(); break;
      CASE(F_POP_MARK); --mark_sp; break;

      CASE(F_CLEAR_STRING_SUBTYPE);
      if(sp[-1].type==T_STRING) sp[-1].subtype=0;
      break;

      /* Jumps */
      CASE(F_BRANCH);
      DOJUMP();
      break;

      CASE(F_BRANCH_IF_NOT_LOCAL_ARROW);
      {
	struct svalue tmp;
	tmp.type=T_STRING;
	tmp.u.string=fp->context.prog->strings[GET_ARG()];
	tmp.subtype=1;
	sp->type=T_INT;	
	sp++;
	index_no_free(sp-1,fp->locals+GET_ARG2() , &tmp);
	print_return_value();
      }

      /* Fall through */

      CASE(F_BRANCH_WHEN_ZERO);
      if(!IS_ZERO(sp-1))
      {
	pc+=sizeof(INT32);
      }else{
	DOJUMP();
      }
      pop_stack();
      break;
      
      CASE(F_BRANCH_WHEN_NON_ZERO);
      if(IS_ZERO(sp-1))
      {
	pc+=sizeof(INT32);
      }else{
	DOJUMP();
      }
      pop_stack();
      break;

      CASE(F_BRANCH_IF_LOCAL);
      instr=GET_ARG();
      if(IS_ZERO(fp->locals + instr))
      {
	pc+=sizeof(INT32);
      }else{
	DOJUMP();
      }
      break;

      CASE(F_BRANCH_IF_NOT_LOCAL);
      instr=GET_ARG();
      if(!IS_ZERO(fp->locals + instr))
      {
	pc+=sizeof(INT32);
      }else{
	DOJUMP();
      }
      break;

      CJUMP(F_BRANCH_WHEN_EQ, is_eq);
      CJUMP(F_BRANCH_WHEN_NE,!is_eq);
      CJUMP(F_BRANCH_WHEN_LT, is_lt);
      CJUMP(F_BRANCH_WHEN_LE,!is_gt);
      CJUMP(F_BRANCH_WHEN_GT, is_gt);
      CJUMP(F_BRANCH_WHEN_GE,!is_lt);

      CASE(F_BRANCH_AND_POP_WHEN_ZERO);
      if(!IS_ZERO(sp-1))
      {
	pc+=sizeof(INT32);
      }else{
	DOJUMP();
	pop_stack();
      }
      break;

      CASE(F_BRANCH_AND_POP_WHEN_NON_ZERO);
      if(IS_ZERO(sp-1))
      {
	pc+=sizeof(INT32);
      }else{
	DOJUMP();
	pop_stack();
      }
      break;

      CASE(F_LAND);
      if(!IS_ZERO(sp-1))
      {
	pc+=sizeof(INT32);
	pop_stack();
      }else{
	DOJUMP();
      }
      break;

      CASE(F_LOR);
      if(IS_ZERO(sp-1))
      {
	pc+=sizeof(INT32);
	pop_stack();
      }else{
	DOJUMP();
      }
      break;

      CASE(F_EQ_OR);
      if(!is_eq(sp-2,sp-1))
      {
	pop_n_elems(2);
	pc+=sizeof(INT32);
      }else{
	pop_n_elems(2);
	push_int(1);
	DOJUMP();
      }
      break;

      CASE(F_EQ_AND);
      if(is_eq(sp-2,sp-1))
      {
	pop_n_elems(2);
	pc+=sizeof(INT32);
      }else{
	pop_n_elems(2);
	push_int(0);
	DOJUMP();
      }
      break;

      CASE(F_CATCH);
      if(o_catch(pc+sizeof(INT32)))
	return -1; /* There was a return inside the evaluated code */
      else
	pc+=EXTRACT_INT(pc);
      break;

      CASE(F_THROW_ZERO);
      push_int(0);
      f_throw(1);
      break;

      CASE(F_SWITCH)
      {
	INT32 tmp;
	tmp=switch_lookup(fp->context.prog->
			  constants[GET_ARG()].u.array,sp-1);
	pc=(unsigned char *)DO_ALIGN(pc,sizeof(INT32));
	pc+=(tmp>=0 ? 1+tmp*2 : 2*~tmp) * sizeof(INT32);
	if(*(INT32*)pc < 0) fast_check_threads_etc(7);
	pc+=*(INT32*)pc;
	pop_stack();
	break;
      }
      
      LOOP(F_INC_LOOP, ++, <, f_add(2), is_lt);
      LOOP(F_DEC_LOOP, --, >, o_subtract(), is_gt);
      LOOP(F_INC_NEQ_LOOP, ++, !=, f_add(2), !is_eq);
      LOOP(F_DEC_NEQ_LOOP, --, !=, o_subtract(), !is_eq);

      CASE(F_FOREACH) /* array, lvalue, X, i */
      {
	if(sp[-4].type != T_ARRAY)
	  PIKE_ERROR("foreach", "Bad argument 1.\n", sp-3, 1);
	if(sp[-1].u.integer < sp[-4].u.array->size)
	{
	  fast_check_threads_etc(10);
	  index_no_free(sp,sp-4,sp-1);
	  sp++;
	  assign_lvalue(sp-4, sp-1);
	  free_svalue(sp-1);
	  sp--;
	  pc+=EXTRACT_INT(pc);
	  sp[-1].u.integer++;
	}else{
	  pc+=sizeof(INT32);
	}
	break;
      }

      CASE(F_APPLY_AND_RETURN);
      {
	INT32 args=sp - *--mark_sp;
/*	fprintf(stderr,"%p >= %p\n",fp->expendible,sp-args); */
	if(fp->expendible >= sp-args)
	{
/*	  fprintf(stderr,"NOT EXPENDIBLE!\n"); */
	  MEMMOVE(sp-args+1,sp-args,args*sizeof(struct svalue));
	  sp++;
	  sp[-args-1].type=T_INT;
	}
	/* We sabotage the stack here */
	assign_svalue(sp-args-1,fp->context.prog->constants+GET_ARG());
	return args+1;
      }

      CASE(F_CALL_LFUN_AND_RETURN);
      {
	INT32 args=sp - *--mark_sp;
	if(fp->expendible >= sp-args)
	{
	  MEMMOVE(sp-args+1,sp-args,args*sizeof(struct svalue));
	  sp++;
	  sp[-args-1].type=T_INT;
	}else{
	  free_svalue(sp-args-1);
	}
	/* More stack sabotage */
	sp[-args-1].u.object=fp->current_object;
	sp[-args-1].subtype=GET_ARG()+fp->context.identifier_level;
	sp[-args-1].type=T_FUNCTION;
	add_ref(fp->current_object);

	return args+1;
      }

      CASE(F_RETURN_LOCAL);
      instr=GET_ARG();
      pop_n_elems(sp-1 - (fp->locals+instr));
      print_return_value();
      goto do_return;

      CASE(F_RETURN_1);
      push_int(1);
      goto do_return;

      CASE(F_RETURN_0);
      push_int(0);
      goto do_return;

      CASE(F_RETURN);
    do_return:
#if defined(DEBUG) && defined(GC2)
      if(d_flag>2) do_gc();
      if(d_flag>3) do_debug();
      check_threads_etc();
#endif

      /* fall through */

      CASE(F_DUMB_RETURN);
      return -1;

      CASE(F_NEGATE); 
      if(sp[-1].type == T_INT)
      {
	sp[-1].u.integer =- sp[-1].u.integer;
      }else if(sp[-1].type == T_FLOAT)
      {
	sp[-1].u.float_number =- sp[-1].u.float_number;
      }else{
	o_negate();
      }
      break;

      CASE(F_COMPL); o_compl(); break;

      CASE(F_NOT);
      switch(sp[-1].type)
      {
      case T_INT:
	sp[-1].u.integer =! sp[-1].u.integer;
	break;

      case T_FUNCTION:
      case T_OBJECT:
	if(IS_ZERO(sp-1))
	{
	  pop_stack();
	  push_int(1);
	}else{
	  pop_stack();
	  push_int(0);
	}
	break;

      default:
	free_svalue(sp-1);
	sp[-1].type=T_INT;
	sp[-1].u.integer=0;
      }
      break;

      CASE(F_LSH); o_lsh(); break;
      CASE(F_RSH); o_rsh(); break;

      COMPARISMENT(F_EQ, is_eq(sp-2,sp-1));
      COMPARISMENT(F_NE,!is_eq(sp-2,sp-1));
      COMPARISMENT(F_GT, is_gt(sp-2,sp-1));
      COMPARISMENT(F_GE,!is_lt(sp-2,sp-1));
      COMPARISMENT(F_LT, is_lt(sp-2,sp-1));
      COMPARISMENT(F_LE,!is_gt(sp-2,sp-1));

      CASE(F_ADD);      f_add(2);     break;
      CASE(F_SUBTRACT); o_subtract(); break;
      CASE(F_AND);      o_and();      break;
      CASE(F_OR);       o_or();       break;
      CASE(F_XOR);      o_xor();      break;
      CASE(F_MULTIPLY); o_multiply(); break;
      CASE(F_DIVIDE);   o_divide();   break;
      CASE(F_MOD);      o_mod();      break;

      CASE(F_ADD_INT); push_int(GET_ARG()); f_add(2); break;
      CASE(F_ADD_NEG_INT); push_int(-GET_ARG()); f_add(2); break;

      CASE(F_PUSH_ARRAY);
      if(sp[-1].type!=T_ARRAY)
	PIKE_ERROR("@", "Bad argument.\n", sp, 1);
      sp--;
      push_array_items(sp->u.array);
      break;

      CASE(F_LOCAL_LOCAL_INDEX);
      {
	struct svalue *s=fp->locals+GET_ARG();
	if(s->type == T_STRING) s->subtype=0;
	sp++->type=T_INT;
	index_no_free(sp-1,fp->locals+GET_ARG2(),s);
	break;
      }

      CASE(F_LOCAL_INDEX);
      {
	struct svalue tmp,*s=fp->locals+GET_ARG();
	if(s->type == T_STRING) s->subtype=0;
	index_no_free(&tmp,sp-1,s);
	free_svalue(sp-1);
	sp[-1]=tmp;
	break;
      }

      CASE(F_GLOBAL_LOCAL_INDEX);
      {
	struct svalue tmp,*s;
	low_object_index_no_free(sp,
				 fp->current_object,
				 GET_ARG() + fp->context.identifier_level);
	sp++;
	s=fp->locals+GET_ARG2();
	if(s->type == T_STRING) s->subtype=0;
	index_no_free(&tmp,sp-1,s);
	free_svalue(sp-1);
	sp[-1]=tmp;
	break;
      }

      CASE(F_LOCAL_ARROW);
      {
	struct svalue tmp;
	tmp.type=T_STRING;
	tmp.u.string=fp->context.prog->strings[GET_ARG()];
	tmp.subtype=1;
	sp->type=T_INT;	
	sp++;
	index_no_free(sp-1,fp->locals+GET_ARG2() , &tmp);
	print_return_value();
	break;
      }

      CASE(F_ARROW);
      {
	struct svalue tmp,tmp2;
	tmp.type=T_STRING;
	tmp.u.string=fp->context.prog->strings[GET_ARG()];
	tmp.subtype=1;
	index_no_free(&tmp2, sp-1, &tmp);
	free_svalue(sp-1);
	sp[-1]=tmp2;
	print_return_value();
	break;
      }

      CASE(F_STRING_INDEX);
      {
	struct svalue tmp,tmp2;
	tmp.type=T_STRING;
	tmp.u.string=fp->context.prog->strings[GET_ARG()];
	tmp.subtype=0;
	index_no_free(&tmp2, sp-1, &tmp);
	free_svalue(sp-1);
	sp[-1]=tmp2;
	print_return_value();
	break;
      }

      CASE(F_POS_INT_INDEX);
      push_int(GET_ARG());
      print_return_value();
      goto do_index;

      CASE(F_NEG_INT_INDEX);
      push_int(-GET_ARG());
      print_return_value();

      CASE(F_INDEX);
    do_index:
      {
	struct svalue s;
	index_no_free(&s,sp-2,sp-1);
	pop_n_elems(2);
	*sp=s;
	sp++;
      }
      print_return_value();
      break;

      CASE(F_CAST); f_cast(); break;

      CASE(F_RANGE); o_range(); break;
      CASE(F_COPY_VALUE);
      {
	struct svalue tmp;
	copy_svalues_recursively_no_free(&tmp,sp-1,1,0);
	free_svalue(sp-1);
	sp[-1]=tmp;
      }
      break;

      CASE(F_INDIRECT);
      {
	struct svalue s;
	lvalue_to_svalue_no_free(&s,sp-2);
	if(s.type != T_STRING)
	{
	  pop_n_elems(2);
	  *sp=s;
	  sp++;
	}else{
	  struct object *o;
	  o=low_clone(string_assignment_program);
	  ((struct string_assignment_storage *)o->storage)->lval[0]=sp[-2];
	  ((struct string_assignment_storage *)o->storage)->lval[1]=sp[-1];
	  ((struct string_assignment_storage *)o->storage)->s=s.u.string;
	  sp-=2;
	  push_object(o);
	}
      }
      print_return_value();
      break;
      

      CASE(F_SIZEOF);
      instr=pike_sizeof(sp-1);
      pop_stack();
      push_int(instr);
      break;

      CASE(F_SIZEOF_LOCAL);
      push_int(pike_sizeof(fp->locals+GET_ARG()));
      break;

      CASE(F_SSCANF); o_sscanf(GET_ARG()); break;

      CASE(F_CALL_LFUN);
      apply_low(fp->current_object,
		GET_ARG()+fp->context.identifier_level,
		sp - *--mark_sp);
      break;

      CASE(F_CALL_LFUN_AND_POP);
      apply_low(fp->current_object,
		GET_ARG()+fp->context.identifier_level,
		sp - *--mark_sp);
      pop_stack();
      break;

    CASE(F_MARK_APPLY);
      strict_apply_svalue(fp->context.prog->constants + GET_ARG(), 0);
      break;

    CASE(F_MARK_APPLY_POP);
      strict_apply_svalue(fp->context.prog->constants + GET_ARG(), 0);
      pop_stack();
      break;

    CASE(F_APPLY);
      strict_apply_svalue(fp->context.prog->constants + GET_ARG(), sp - *--mark_sp );
      break;

    CASE(F_APPLY_AND_POP);
      strict_apply_svalue(fp->context.prog->constants + GET_ARG(), sp - *--mark_sp );
      pop_stack();
      break;

    CASE(F_CALL_FUNCTION);
    mega_apply(APPLY_STACK,sp - *--mark_sp,0,0);
    break;

    CASE(F_CALL_FUNCTION_AND_RETURN);
    {
      INT32 args=sp - *--mark_sp;
      if(!args)
	PIKE_ERROR("`()", "Too few arguments.\n", sp, 0);
      switch(sp[-args].type)
      {
	case T_INT:
	  if (!sp[-args].u.integer) {
	    PIKE_ERROR("`()", "Attempt to call the NULL-value\n", sp, args);
	  }
	case T_STRING:
	case T_FLOAT:
	case T_MAPPING:
	case T_MULTISET:
	  PIKE_ERROR("`()", "Attempt to call a non-function value.\n", sp, args);
      }
      return args;
    }

    default:
      fatal("Strange instruction %ld\n",(long)instr);
    }

  }
}

static void trace_return_value(void)
{
  char *s;
  
  init_buf();
  my_strcat("Return: ");
  describe_svalue(sp-1,0,0);
  s=simple_free_buf();
  if((long)strlen(s) > (long)TRACE_LEN)
  {
    s[TRACE_LEN]=0;
    s[TRACE_LEN-1]='.';
    s[TRACE_LEN-2]='.';
    s[TRACE_LEN-2]='.';
  }
  fprintf(stderr,"%-*s%s\n",4,"-",s);
  free(s);
}

static void do_trace_call(INT32 args)
{
  char *file,*s;
  INT32 linep,e;
  my_strcat("(");
  for(e=0;e<args;e++)
  {
    if(e) my_strcat(",");
    describe_svalue(sp-args+e,0,0);
  }
  my_strcat(")"); 
  s=simple_free_buf();
  if((long)strlen(s) > (long)TRACE_LEN)
  {
    s[TRACE_LEN]=0;
    s[TRACE_LEN-1]='.';
    s[TRACE_LEN-2]='.';
    s[TRACE_LEN-2]='.';
  }
  if(fp && fp->pc)
  {
    char *f;
    file=get_line(fp->pc,fp->context.prog,&linep);
    while((f=STRCHR(file,'/'))) file=f+1;
  }else{
    linep=0;
    file="-";
  }
  fprintf(stderr,"- %s:%4ld: %s\n",file,(long)linep,s);
  free(s);
}


void mega_apply(enum apply_type type, INT32 args, void *arg1, void *arg2)
{
  struct object *o;
  int fun, tailrecurse=-1;
  struct svalue *save_sp=sp-args;
#ifdef PROFILING
#ifdef HAVE_GETHRTIME
  long long children_base = accounted_time;
  long long start_time = gethrtime() - time_base;
  unsigned INT32 self_time_base;
  if(start_time < 0)
  {
    fatal("gethrtime() shrunk\n start_time=%ld\n gethrtime()\n time_base=%ld\n",
	  (long)(start_time/100000), 
	  (long)(gethrtime()/100000), 
	  (long)(time_base/100000));
  }
#endif
#endif

  switch(type)
  {
  case APPLY_STACK:
  apply_stack:
    if(!args)
      PIKE_ERROR("`()", "Too few arguments.\n", sp, 0);
    args--;
    if(sp-save_sp-args > (args<<2) + 32)
    {
      /* The test above assures these two areas
       * are not overlapping
       */
      assign_svalues(save_sp, sp-args-1, args+1, BIT_MIXED);
      pop_n_elems(sp-save_sp-args-1);
    }
    arg1=(void *)(sp-args-1);

  case APPLY_SVALUE:
  apply_svalue:
  {
    struct svalue *s=(struct svalue *)arg1;
    switch(s->type)
    {
    case T_INT:
      if (!s->u.integer) {
	PIKE_ERROR("0", "Attempt to call the NULL-value\n", sp, args);
      } else {
	error("Attempt to call the value %d\n", s->u.integer);
      }

    case T_STRING:
      if (s->u.string->len > 20) {
	error("Attempt to call the string \"%20s\"...\n", s->u.string->str);
      } else {
	error("Attempt to call the string \"%s\"\n", s->u.string->str);
      }
    case T_MAPPING:
      error("Attempt to call a mapping\n");
    default:
      error("Call to non-function value type:%s.\n",
	    get_name_of_type(s->type));
      
    case T_FUNCTION:
      if(s->subtype == FUNCTION_BUILTIN)
      {
#ifdef DEBUG
	if(t_flag>1)
	{
	  init_buf();
	  describe_svalue(s,0,0);
	  do_trace_call(args);
	}
#endif
	(*(s->u.efun->function))(args);
	break;
      }else{
	o=s->u.object;
	fun=s->subtype;
	goto apply_low;
      }
      break;

    case T_ARRAY:
#ifdef DEBUG
      if(t_flag>1)
      {
	init_buf();
	describe_svalue(s,0,0);
	do_trace_call(args);
      }
#endif
      apply_array(s->u.array,args);
      break;

    case T_PROGRAM:
#ifdef DEBUG
      if(t_flag>1)
      {
	init_buf();
	describe_svalue(s,0,0);
	do_trace_call(args);
      }
#endif
      push_object(clone_object(s->u.program,args));
      break;

    case T_OBJECT:
      o=s->u.object;
      fun=LFUN_CALL;
      goto call_lfun;
    }
    break;
  }

  call_lfun:
#ifdef DEBUG
    if(fun < 0 || fun >= NUM_LFUNS)
      fatal("Apply lfun on illegal value!\n");
#endif
    if(!o->prog)
      PIKE_ERROR("destructed object", "Apply on destructed object.\n", sp, args);
    fun=FIND_LFUN(o->prog,fun);
    goto apply_low;
  

  case APPLY_LOW:
    o=(struct object *)arg1;
    fun=(long)arg2;

  apply_low:
    {
      struct program *p;
      struct reference *ref;
      struct frame new_frame;
      struct identifier *function;
      
      if(fun<0)
      {
	pop_n_elems(sp-save_sp);
	push_int(0);
	return;
      }
      
      check_stack(256);
      check_mark_stack(256);
      check_c_stack(8192);

#ifdef DEBUG
      if(d_flag>2) do_debug();
#endif

      p=o->prog;
      if(!p)
	PIKE_ERROR("destructed object->function",
	      "Cannot call functions in destructed objects.\n", sp, args);
#ifdef DEBUG
      if(fun>=(int)p->num_identifier_references)
      {
	fprintf(stderr,"Function index out of range. %d >= %d\n",fun,(int)p->num_identifier_references);
	fprintf(stderr,"########Program is:\n");
	describe(p);
	fprintf(stderr,"########Object is:\n");
	describe(o);
	fatal("Function index out of range.\n");
      }
#endif

      ref = p->identifier_references + fun;
#ifdef DEBUG
      if(ref->inherit_offset>=p->num_inherits)
	fatal("Inherit offset out of range in program.\n");
#endif

      /* init a new evaluation frame */
      new_frame.parent_frame = fp;
      new_frame.current_object = o;
      new_frame.context = p->inherits[ ref->inherit_offset ];

      function = new_frame.context.prog->identifiers + ref->identifier_offset;

#ifdef PROFILING
      function->num_calls++;
#endif
  
      new_frame.locals = sp - args;
      new_frame.expendible = new_frame.locals;
      new_frame.args = args;
      new_frame.fun = fun;
      new_frame.current_storage = o->storage+new_frame.context.storage_offset;
      new_frame.pc = 0;
      
      add_ref(new_frame.current_object);
      add_ref(new_frame.context.prog);
      if(new_frame.context.parent) add_ref(new_frame.context.parent);
      
      if(t_flag)
      {
	char buf[50];
	init_buf();
	sprintf(buf,"%lx->",(long)o);
	my_strcat(buf);
	my_strcat(function->name->str);
	do_trace_call(args);
      }
      
      fp = &new_frame;
      
      if(function->func.offset == -1)
	PIKE_ERROR(function->name->str, "Calling undefined function.\n", sp, args);
      
      tailrecurse=-1;

#ifdef PROFILING
#ifdef HAVE_GETHRTIME
      self_time_base=function->total_time;
#endif
#endif

      switch(function->identifier_flags & (IDENTIFIER_FUNCTION | IDENTIFIER_CONSTANT))
      {
      case IDENTIFIER_C_FUNCTION:
	fp->num_args=args;
	new_frame.num_locals=args;
	check_threads_etc();
	(*function->func.c_fun)(args);
	break;
	
      case IDENTIFIER_CONSTANT:
      {
	struct svalue *s=fp->context.prog->constants+function->func.offset;
	if(s->type == T_PROGRAM)
	{
	  struct object *tmp;
	  check_threads_etc();
	  tmp=parent_clone_object(s->u.program,
				  o,
				  fun,
				  args);
	  push_object(tmp);
	  break;
	}
	/* Fall through */
      }
      
      case 0:
      {
	if(sp-save_sp-args<=0)
	{
	  /* Create an extra svalue for tail recursion style call */
	  sp++;
	  MEMMOVE(sp-args,sp-args-1,sizeof(struct svalue)*args);
	  sp[-args-1].type=T_INT;
	}
	low_object_index_no_free(sp-args-1,o,fun);
	tailrecurse=args+1;
	break;
      }

      case IDENTIFIER_PIKE_FUNCTION:
      {
	int num_args;
	int num_locals;
	unsigned char *pc;
	pc=new_frame.context.prog->program + function->func.offset;
	
	num_locals=EXTRACT_UCHAR(pc++);
	num_args=EXTRACT_UCHAR(pc++);
	
	/* adjust arguments on stack */
	if(args < num_args) /* push zeros */
	{
	  clear_svalues(sp, num_args-args);
	  sp += num_args-args;
	  args += num_args-args;
	}
	
	if(function->identifier_flags & IDENTIFIER_VARARGS)
	{
	  f_aggregate(args - num_args); /* make array */
	  args = num_args+1;
	}else{
	  if(args > num_args)
	  {
	    /* pop excessive */
	    pop_n_elems(args - num_args);
	    args=num_args;
	  }
	}
	
	clear_svalues(sp, num_locals - args);
	sp += num_locals - args;
#ifdef DEBUG
	if(num_locals < num_args)
	  fatal("Wrong number of arguments or locals in function def.\n");
#endif
	new_frame.num_locals=num_locals;
	new_frame.num_args=num_args;

	check_threads_etc();

	{
	  struct svalue **save_mark_sp=mark_sp;
	  tailrecurse=eval_instruction(pc);
#ifdef DEBUG
	  if(mark_sp < save_mark_sp)
	    fatal("Popped below save_mark_sp!\n");
#endif
	  mark_sp=save_mark_sp;
	}
#ifdef DEBUG
	if(sp<evaluator_stack)
	  fatal("Stack error (also simple).\n");
#endif
	break;
      }
      
      }
#ifdef PROFILING
#ifdef HAVE_GETHRTIME
      {
	long long time_passed, time_in_children, self_time;
	time_in_children=  accounted_time - children_base;
	time_passed = gethrtime() - time_base - start_time;
	self_time=time_passed - time_in_children;
	accounted_time+=self_time;
#ifdef DEBUG
	if(self_time < 0 || children_base <0 || accounted_time <0)
	  fatal("Time is negative\n  self_time=%ld\n  time_passed=%ld\n  time_in_children=%ld\n  children_base=%ld\n  accounted_time=%ld!\n  time_base=%ld\n  start_time=%ld\n",
		(long)(self_time/100000),
		(long)(time_passed/100000),
		(long)(time_in_children/100000),
		(long)(children_base/100000),
		(long)(accounted_time/100000),
		(long)(time_base/100000),
		(long)(start_time/100000)
		);
#endif
	function->total_time=self_time_base + (INT32)(time_passed /1000);
	function->self_time+=(INT32)( self_time /1000);
      }
#endif
#endif

#if 0
      if(sp - new_frame.locals > 1)
      {
	pop_n_elems(sp - new_frame.locals -1);
      }else if(sp - new_frame.locals < 1){
#ifdef DEBUG
	if(sp - new_frame.locals<0) fatal("Frame underflow.\n");
#endif
	sp->u.integer = 0;
	sp->subtype=NUMBER_NUMBER;
	sp->type = T_INT;
	sp++;
      }
#endif
      
      if(new_frame.context.parent) free_object(new_frame.context.parent);
      free_object(new_frame.current_object);
      free_program(new_frame.context.prog);
      
      fp = new_frame.parent_frame;
      
      if(tailrecurse>=0)
      {
	args=tailrecurse;
	goto apply_stack;
      }

    }
  }

  if(save_sp+1 < sp)
  {
    assign_svalue(save_sp,sp-1);
    pop_n_elems(sp-save_sp-1);
  }

  if(save_sp+1 > sp)
  {
    if(type != APPLY_SVALUE)
      push_int(0);
  }else{
    if(t_flag>1) trace_return_value();
  }
}


/* Put catch outside of eval_instruction, so
 * the setjmp won't affect the optimization of
 * eval_instruction
 */
static int o_catch(unsigned char *pc)
{
  JMP_BUF tmp;
  struct svalue *expendible=fp->expendible;
  if(SETJMP(tmp))
  {
    *sp=throw_value;
    throw_value.type=T_INT;
    sp++;
    UNSETJMP(tmp);
    fp->expendible=expendible;
    return 0;
  }else{
    int x;
    fp->expendible=fp->locals + fp->num_locals;
    x=eval_instruction(pc);
    fp->expendible=expendible;
    if(x!=-1) mega_apply(APPLY_STACK, x, 0,0);
    UNSETJMP(tmp);
    return 1;
  }
}

void f_call_function(INT32 args)
{
  mega_apply(APPLY_STACK,args,0,0);
}

int apply_low_safe_and_stupid(struct object *o, INT32 offset)
{
  JMP_BUF tmp;
  struct frame new_frame;
  int ret;

  new_frame.parent_frame = fp;
  new_frame.current_object = o;
  new_frame.context=o->prog->inherits[0];
  new_frame.locals = evaluator_stack;
  new_frame.expendible=new_frame.locals;
  new_frame.args = 0;
  new_frame.num_args=0;
  new_frame.num_locals=0;
  new_frame.fun = -1;
  new_frame.pc = 0;
  new_frame.current_storage=o->storage;
  new_frame.context.parent=0;
  fp = & new_frame;

  add_ref(new_frame.current_object);
  add_ref(new_frame.context.prog);

  if(SETJMP(tmp))
  {
    ret=1;
  }else{
    int tmp=eval_instruction(o->prog->program + offset);
    if(tmp!=-1) mega_apply(APPLY_STACK, tmp, 0,0);
    
#ifdef DEBUG
    if(sp<evaluator_stack)
      fatal("Stack error (simple).\n");
#endif
    ret=0;
  }
  UNSETJMP(tmp);

  free_object(new_frame.current_object);
  free_program(new_frame.context.prog);

  fp=new_frame.parent_frame;
  return ret;
}

void safe_apply_low(struct object *o,int fun,int args)
{
  JMP_BUF recovery;

  sp-=args;
  free_svalue(& throw_value);
  throw_value.type=T_INT;
  if(SETJMP(recovery))
  {
    if(throw_value.type == T_ARRAY)
    {
      ONERROR tmp;
      SET_ONERROR(tmp,exit_on_error,"Error in handle_error in master object!");
      assign_svalue_no_free(sp++, & throw_value);
      APPLY_MASTER("handle_error", 1);
      pop_stack();
      UNSET_ONERROR(tmp);
    }
      
    sp->u.integer = 0;
    sp->subtype=NUMBER_NUMBER;
    sp->type = T_INT;
    sp++;
  }else{
    INT32 expected_stack = sp - evaluator_stack + 1;
    sp+=args;
    apply_low(o,fun,args);
    if(sp - evaluator_stack > expected_stack)
      pop_n_elems(sp - evaluator_stack - expected_stack);
    if(sp - evaluator_stack < expected_stack)
    {
      sp->u.integer = 0;
      sp->subtype=NUMBER_NUMBER;
      sp->type = T_INT;
      sp++;
    }
  }
  UNSETJMP(recovery);
}


void safe_apply(struct object *o, char *fun ,INT32 args)
{
#ifdef DEBUG
  if(!o->prog) fatal("Apply safe on destructed object.\n");
#endif
  safe_apply_low(o, find_identifier(fun, o->prog), args);
}

void apply_lfun(struct object *o, int fun, int args)
{
#ifdef DEBUG
  if(fun < 0 || fun >= NUM_LFUNS)
    fatal("Apply lfun on illegal value!\n");
#endif
  if(!o->prog)
    PIKE_ERROR("destructed object", "Apply on destructed object.\n", sp, args);

  apply_low(o, (int)FIND_LFUN(o->prog,fun), args);
}

void apply_shared(struct object *o,
		  struct pike_string *fun,
		  int args)
{
  apply_low(o, find_shared_string_identifier(fun, o->prog), args);
}

void apply(struct object *o, char *fun, int args)
{
  apply_low(o, find_identifier(fun, o->prog), args);
}


void apply_svalue(struct svalue *s, INT32 args)
{
  if(s->type==T_INT)
  {
    pop_n_elems(args);
    push_int(0);
  }else{
    INT32 expected_stack=sp-args+1 - evaluator_stack;

    strict_apply_svalue(s,args);
    if(sp > (expected_stack + evaluator_stack))
    {
      pop_n_elems(sp-(expected_stack + evaluator_stack));
    }
    else if(sp < (expected_stack + evaluator_stack))
    {
      push_int(0);
    }
#ifdef DEBUG
    if(sp < (expected_stack + evaluator_stack))
      fatal("Stack underflow!\n");
#endif
  }
}

#ifdef DEBUG
void slow_check_stack(void)
{
  struct svalue *s,**m;
  struct frame *f;

  debug_check_stack();

  if(sp > &(evaluator_stack[stack_size]))
    fatal("Stack overflow.\n");

  if(mark_sp > &(mark_stack[stack_size]))
    fatal("Mark stack overflow.\n");

  if(mark_sp < mark_stack)
    fatal("Mark stack underflow.\n");

  for(s=evaluator_stack;s<sp;s++) check_svalue(s);

  s=evaluator_stack;
  for(m=mark_stack;m<mark_sp;m++)
  {
    if(*m < s)
      fatal("Mark stack failiure.\n");

    s=*m;
  }

  if(s > &(evaluator_stack[stack_size]))
    fatal("Mark stack exceeds svalue stack\n");

  for(f=fp;f;f=f->parent_frame)
  {
    if(f->locals)
    {
      if(f->locals < evaluator_stack ||
	f->locals > &(evaluator_stack[stack_size]))
      fatal("Local variable pointer points to Finsp�ng.\n");

      if(f->args < 0 || f->args > stack_size)
	fatal("FEL FEL FEL! HELP!! (corrupted frame)\n");
    }
  }
}
#endif

void cleanup_interpret(void)
{
#ifdef DEBUG
  int e;
#endif

  while(fp)
  {
    free_object(fp->current_object);
    free_program(fp->context.prog);
    
    fp = fp->parent_frame;
  }

#ifdef DEBUG
  for(e=0;e<BACKLOG;e++)
  {
    if(backlog[e].program)
    {
      free_program(backlog[e].program);
      backlog[e].program=0;
    }
  }
#endif
  reset_evaluator();

#ifdef USE_MMAP_FOR_STACK
  if(!evaluator_stack_malloced)
  {
    munmap((char *)evaluator_stack, stack_size*sizeof(struct svalue));
    evaluator_stack=0;
  }
  if(!mark_stack_malloced)
  {
    munmap((char *)mark_stack, stack_size*sizeof(struct svalue *));
    mark_stack=0;
  }
#endif

  if(evaluator_stack) free((char *)evaluator_stack);
  if(mark_stack) free((char *)mark_stack);

  mark_stack=0;
  evaluator_stack=0;
  mark_stack_malloced=0;
  evaluator_stack_malloced=0;
}
