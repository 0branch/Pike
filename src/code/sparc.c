/*
 * $Id: sparc.c,v 1.5 2001/07/20 22:45:19 grubba Exp $
 *
 * Machine code generator for sparc.
 *
 * Henrik Grubbström 20010720
 */

#define ADD_CALL(X, DELAY_OK) do {					\
    INT32 delta_;							\
    struct program *p_ = Pike_compiler->new_program;			\
    INT32 off_ = p_->num_program;					\
    /* noop		*/						\
    INT32 delay_ = 0x01000000;						\
									\
    if (DELAY_OK) {							\
      /* Move the previous opcode to the delay-slot. */			\
      delay_ = p_->program[--off_];					\
    } else {								\
      add_to_program(0); /* Placeholder... */				\
    }									\
    /* call X	*/							\
    delta_ = ((PIKE_OPCODE_T *)(X)) - (p_->program + off_);		\
    p_->program[off_] = 0x40000000 | (delta_ & 0x3fffffff);		\
    add_to_relocations(off_);						\
    add_to_program(delay_);						\
  } while(0)

static void low_ins_f_byte(unsigned int b, int delay_ok)
{
#ifdef PIKE_DEBUG
  if(store_linenumbers && b<F_MAX_OPCODE)
    ADD_COMPILED(b);
#endif /* PIKE_DEBUG */

  b-=F_OFFSET;
#ifdef PIKE_DEBUG
  if(b>255)
    Pike_error("Instruction too big %d\n",b);
#endif
    
  {
    static int last_prog_id=-1;
    static int last_num_linenumbers=-1;
    if(last_prog_id != Pike_compiler->new_program->id ||
       last_num_linenumbers != Pike_compiler->new_program->num_linenumbers)
    {
      last_prog_id=Pike_compiler->new_program->id;
      last_num_linenumbers = Pike_compiler->new_program->num_linenumbers;
      UPDATE_PC();
      delay_ok = 1;
    }
  }
  
  ADD_CALL(instrs[b].address, delay_ok);
}

void ins_f_byte(unsigned int opcode)
{
  low_ins_f_byte(opcode, 0);
}

void ins_f_byte_with_arg(unsigned int a,unsigned INT32 b)
{
  SET_REG(SPARC_REG_O0, b);
  low_ins_f_byte(a, 1);
  return;
}

void ins_f_byte_with_2_args(unsigned int a,
			    unsigned INT32 c,
			    unsigned INT32 b)
{
  SET_REG(SPARC_REG_O0, c);
  SET_REG(SPARC_REG_O1, b);
  low_ins_f_byte(a, 1);
  return;
}

#define addstr(s, l) low_my_binary_strcat((s), (l), buf)
#define adddata2(s,l) addstr((char *)(s),(l) * sizeof((s)[0]));

void sparc_encode_program(struct program *p, struct dynamic_buffer_s *buf)
{
  size_t prev = 0, rel;
  /* De-relocate the program... */
  for (rel = 0; rel < p->num_relocations; rel++) {
    size_t off = p->relocations[rel];
    INT32 opcode;
#ifdef PIKE_DEBUG
    if (off < prev) {
      fatal("Relocations in bad order!\n");
    }
#endif /* PIKE_DEBUG */
    adddata2(p->program + prev, off - prev);

#ifdef PIKE_DEBUG
    if ((p->program[off] & 0xc0000000) != 0x40000000) {
      fatal("Bad relocation!\n");
    }
#endif /* PIKE_DEBUG */
    /* Relocate to being relative to NULL */
    opcode = 0x40000000 |
      ((p->program[off] + (((INT32)(p->program)>>2))) & 0x3fffffff);
    adddata2(&opcode, 1);
    prev = off+1;
  }
  adddata2(p->program + prev, p->num_program - prev);
}

void sparc_decode_program(struct program *p)
{
  /* Relocate the program... */
  PIKE_OPCODE_T *prog = p->program;
  INT32 delta = ((INT32)p->program)>>2;
  size_t rel = p->num_relocations;
  while (rel--) {
#ifdef PIKE_DEBUG
    if ((prog[p->relocations[rel]] & 0xc0000000) != 0x40000000) {
      Pike_error("Bad relocation: %d, off:%d, opcode: 0x%08x\n",
		 rel, p->relocations[rel],
		 prog[p->relocations[rel]]);
    }
#endif /* PIKE_DEBUG */
    prog[p->relocations[rel]] = 0x40000000 |
      (((prog[p->relocations[rel]] & 0x3fffffff) - delta) &
       0x3fffffff);
  }
}
