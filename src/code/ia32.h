/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
|| $Id: ia32.h,v 1.21 2003/07/31 14:28:51 tomas Exp $
*/

/* #define ALIGN_PIKE_JUMPS 8 */

#define LOW_GET_JUMP()	EXTRACT_INT(PROG_COUNTER)
#define LOW_SKIPJUMP()	(SET_PROG_COUNTER(PROG_COUNTER + sizeof(INT32)))


#ifdef _M_IX86

#define DEF_PROG_COUNTER void *ia32_pc; \
                         _asm { _asm mov ia32_pc,ebp }
#define PROG_COUNTER  (((unsigned char **)ia32_pc)[1])

#else  /* _M_IX86 */

#define PROG_COUNTER (((unsigned char **)__builtin_frame_address(0))[1])

#endif


#define ins_pointer(PTR)	ins_int((PTR), (void (*)(char))add_to_program)
#define read_pointer(OFF)	read_int(OFF)
#define upd_pointer(OFF, PTR)	upd_int(OFF, PTR)
#define ins_align(ALIGN)	do {				\
    while(Pike_compiler->new_program->num_program % (ALIGN)) {	\
      add_to_program(0);					\
    }								\
  } while(0)
#define ins_byte(VAL)		add_to_program(VAL)
#define ins_data(VAL)		ins_int((VAL), (void (*)(char))add_to_program)

void ia32_update_pc(void);

#define UPDATE_PC() ia32_update_pc()

extern ptrdiff_t ia32_prev_stored_pc;

#define ADJUST_PIKE_PC(pc) do {						\
    ia32_prev_stored_pc = pc;						\
    DO_IF_DEBUG(							\
      if (a_flag >= 60)							\
	fprintf (stderr, "pc %d  adjusted\n", ia32_prev_stored_pc);	\
    );									\
  } while (0)


#define READ_INCR_BYTE(PC)	EXTRACT_UCHAR((PC)++)


/* We know that x86 handles unaligned memory access, we might
 * as well use it.
 */
#define RELOCATE_program(P, NEW)	do {				 \
    PIKE_OPCODE_T *op_ = NEW;						 \
    struct program *p_ = P;						 \
    size_t rel_ = p_->num_relocations;					 \
    INT32 delta_ = p_->program - op_;					 \
    if(delta_) {							 \
      while (rel_--) {							 \
        *((INT32 *)(op_ + p_->relocations[rel_]))+=delta_;		 \
      }									 \
    }									 \
  } while(0)


struct dynamic_buffer_s;
struct program;
void ia32_encode_program(struct program *p, struct dynamic_buffer_s *buf);
void ia32_decode_program(struct program *p);

#define ENCODE_PROGRAM(P, BUF)	ia32_encode_program(P, BUF)
#define DECODE_PROGRAM(P)	ia32_decode_program(p)

INT32 ins_f_jump(unsigned int b);
void update_f_jump(INT32 offset, INT32 to_offset);
INT32 read_f_jump(INT32 offset);

#define INS_F_JUMP ins_f_jump
#define UPDATE_F_JUMP update_f_jump
#define READ_F_JUMP read_f_jump

void ia32_flush_code_generator(void);
#define FLUSH_CODE_GENERATOR_STATE ia32_flush_code_generator


#ifdef _M_IX86

#define CALL_MACHINE_CODE(pc)                                   \
  /* This code does not clobber %eax, %ebx, %ecx & %edx, but    \
   * the code jumped to does.                                   \
   */                                                           \
  __asm {                                                       \
    __asm sub esp,12                                            \
    __asm inc ebx /* dummy: forces the compiler to save ebx */  \
    __asm jmp pc                                                \
  }

#define EXIT_MACHINE_CODE()                                     \
  __asm { __asm add esp,12 }

#else  /* _M_IX86 */

#define CALL_MACHINE_CODE(pc)						\
  /* This code does not clobber %eax, %ebx, %ecx & %edx, but		\
   * the code jumped to does.						\
   */									\
  __asm__ __volatile__( "	sub $12,%%esp\n"			\
			"	jmp *%0"				\
			: "=m" (pc)					\
			:						\
			: "cc", "memory", "eax", "ebx", "ecx", "edx" )

#define EXIT_MACHINE_CODE()						\
  __asm__ __volatile__( "add $12,%%esp\n" : : )

#endif /* _M_IX86 */
