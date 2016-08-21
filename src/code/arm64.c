/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#include "global.h"
#include "svalue.h"
#include "operators.h"
#include "bitvector.h"
#include "object.h"
#include "builtin_functions.h"
#include "bignum.h"

#define MACRO  ATTRIBUTE((unused)) static

/* ARM64 machine code backend
 *
 * naming conventions:
 *
 *  <op>_reg_reg(dst, a, b)
 *      execute a <op> b and store to dst
 *  <op>_reg_imm(dst, a, imm, rot)
 *      execute a <op> (ROT(imm, rot*2)) and store to dst
 *  arm64_<op>_reg_int(dst, a, v)
 *      execute a <op> v and store to dst
 *
 */

enum arm64_register {
    ARM_REG_R0,
    ARM_REG_R1,
    ARM_REG_R2,
    ARM_REG_R3,
    ARM_REG_R4,
    ARM_REG_R5,
    ARM_REG_R6,
    ARM_REG_R7,
    ARM_REG_R8,
    ARM_REG_R9,
    ARM_REG_R10,
    ARM_REG_R11,
    ARM_REG_R12,
    ARM_REG_R13,
    ARM_REG_R14,
    ARM_REG_R15,
    ARM_REG_R16 = 16,
    ARM_REG_IP0 = 16,
    ARM_REG_R17 = 17,
    ARM_REG_IP1 = 17,
    ARM_REG_R18 = 18,

    /* everything below is calee saved */
    ARM_REG_R19 = 19,
    ARM_REG_R20 = 20,
    ARM_REG_R21 = 21,
    ARM_REG_R22 = 22,
    ARM_REG_R23 = 23,
    ARM_REG_R24 = 24,
    ARM_REG_R25 = 25,
    ARM_REG_PIKE_LOCALS = 25,
    ARM_REG_R26 = 26,
    ARM_REG_PIKE_IP = 26,
    ARM_REG_R27 = 27,
    ARM_REG_PIKE_SP = 27,
    ARM_REG_R28 = 28,
    ARM_REG_PIKE_FP = 28,
    ARM_REG_R29 = 29,
    ARM_REG_FP = 29,
    ARM_REG_R30 = 30,
    ARM_REG_LR = 30,

    ARM_REG_SP = 31,  /* Depending on opcode */
    ARM_REG_ZERO = 31,

    ARM_REG_NONE = -1
};

unsigned INT32 RBIT(enum arm64_register reg) {
    return 1<<reg;
}

enum arm64_condition {
    ARM_COND_EQ = 0, /* equal */
    ARM_COND_Z  = 0, /* zero */
    ARM_COND_NE = 1, /* not equal */
    ARM_COND_NZ = 1, /* not zero */
    ARM_COND_CS = 2,
    ARM_COND_CC = 3,
    ARM_COND_MI = 4,
    ARM_COND_PL = 5,
    ARM_COND_VS = 6, /* overflow */
    ARM_COND_VC = 7, /* no overflow */
    /* unsigned comparison */
    ARM_COND_HI = 8,
    ARM_COND_LS = 9,
    /* signed comparison */
    ARM_COND_GE = 10,
    ARM_COND_LT = 11,
    ARM_COND_GT = 12,
    ARM_COND_LE = 13,
    /* unconditional */
    ARM_COND_AL = 14
};

enum arm64_shift_mode {
    ARM_SHIFT_LSL = 0 << 22,
    ARM_SHIFT_LSR = 1 << 22,
    ARM_SHIFT_ASR = 2 << 22,
    ARM_SHIFT_ROR = 3 << 22,
};

enum arith_instr {
    ARM_ARITH_ADD  = 0 << 29,
    ARM_ARITH_ADDS = 1 << 29,
    ARM_ARITH_SUB  = 2 << 29,
    ARM_ARITH_SUBS = 3 << 29,
};

enum logic_instr {
    ARM_LOGIC_AND  = 0 << 29,
    ARM_LOGIC_OR   = 1 << 29,
    ARM_LOGIC_EOR  = 2 << 29,
    ARM_LOGIC_ANDS = 3 << 29,
};

enum move_wide_instr {
    ARM_WIDE_MOVN = 0 << 29,
    ARM_WIDE_MOVZ = 2 << 29,
    ARM_WIDE_MOVK = 3 << 29,
};

enum intr_type {
ARM_INSTR_LOGIC_REG = 0x0a000000,
ARM_INSTR_ARITH_REG = 0x0b000000,
ARM_INSTR_PC_REL    = 0x10000000,
ARM_INSTR_ARITH_IMM = 0x11000000,
ARM_INSTR_LOGIC_IMM = 0x12000000,
ARM_INSTR_MOVE_WIDE = 0x12800000,
ARM_INSTR_UNCOND_BRANCH_IMM = 0x14000000,
ARM_INSTR_COND_SELECT       = 0x1a800000,
ARM_INSTR_SHIFT_REG         = 0x1ac02000,
ARM_INSTR_LOADSTORE_PAIR    = 0x28000000,
ARM_INSTR_COND_COMPARE      = 0x3a400000,
ARM_INSTR_COND_BRANCH_IMM   = 0x54000000,
ARM_INSTR_LOADSTORE_SINGLE  = 0xb8000000,
ARM_INSTR_UNCOND_BRANCH_REG = 0xd6000000,
};

#define OPCODE_FUN ATTRIBUTE((unused,warn_unused_result)) static PIKE_OPCODE_T


MACRO void arm64_flush_dirty_regs(void);
MACRO void arm64_call(void *ptr);
MACRO enum arm64_register ra_alloc_any(void);
MACRO void ra_free(enum arm64_register reg);

MACRO void break_my_arm(void) {
}

#ifdef ARM64_LOW_DEBUG
static unsigned INT32 stats_m[F_MAX_INSTR - F_OFFSET];
static unsigned INT32 stats_b[F_MAX_INSTR - F_OFFSET];

#define OPCODE0(X,Y,F) case X: return #X;
#define OPCODE1(X,Y,F) case X: return #X;
#define OPCODE2(X,Y,F) case X: return #X;
#define OPCODE0_TAIL(X,Y,F) case X: return #X;
#define OPCODE1_TAIL(X,Y,F) case X: return #X;
#define OPCODE2_TAIL(X,Y,F) case X: return #X;
#define OPCODE0_JUMP(X,Y,F) case X: return #X;
#define OPCODE1_JUMP(X,Y,F) case X: return #X;
#define OPCODE2_JUMP(X,Y,F) case X: return #X;
#define OPCODE0_TAILJUMP(X,Y,F) case X: return #X;
#define OPCODE1_TAILJUMP(X,Y,F) case X: return #X;
#define OPCODE2_TAILJUMP(X,Y,F) case X: return #X;
#define OPCODE0_PTRJUMP(X,Y,F) case X: return #X;
#define OPCODE1_PTRJUMP(X,Y,F) case X: return #X;
#define OPCODE2_PTRJUMP(X,Y,F) case X: return #X;
#define OPCODE0_TAILPTRJUMP(X,Y,F) case X: return #X;
#define OPCODE1_TAILPTRJUMP(X,Y,F) case X: return #X;
#define OPCODE2_TAILPTRJUMP(X,Y,F) case X: return #X;
#define OPCODE0_RETURN(X,Y,F) case X: return #X;
#define OPCODE1_RETURN(X,Y,F) case X: return #X;
#define OPCODE2_RETURN(X,Y,F) case X: return #X;
#define OPCODE0_TAILRETURN(X,Y,F) case X: return #X;
#define OPCODE1_TAILRETURN(X,Y,F) case X: return #X;
#define OPCODE2_TAILRETURN(X,Y,F) case X: return #X;
#define OPCODE0_BRANCH(X,Y,F) case X: return #X;
#define OPCODE1_BRANCH(X,Y,F) case X: return #X;
#define OPCODE2_BRANCH(X,Y,F) case X: return #X;
#define OPCODE0_TAILBRANCH(X,Y,F) case X: return #X;
#define OPCODE1_TAILBRANCH(X,Y,F) case X: return #X;
#define OPCODE2_TAILBRANCH(X,Y,F) case X: return #X;
#define OPCODE0_ALIAS(X,Y,F,A) case X: return #X;
#define OPCODE1_ALIAS(X,Y,F,A) case X: return #X;
#define OPCODE2_ALIAS(X,Y,F,A) case X: return #X;

const char* arm_get_opcode_name(PIKE_OPCODE_T code) {
    switch (code+F_OFFSET) {
#include "interpret_protos.h"
    default:
        return "<unknown>";
    }
}

#undef OPCODE0
#undef OPCODE1
#undef OPCODE2
#undef OPCODE0_TAIL
#undef OPCODE1_TAIL
#undef OPCODE2_TAIL
#undef OPCODE0_PTRJUMP
#undef OPCODE1_PTRJUMP
#undef OPCODE2_PTRJUMP
#undef OPCODE0_TAILPTRJUMP
#undef OPCODE1_TAILPTRJUMP
#undef OPCODE2_TAILPTRJUMP
#undef OPCODE0_RETURN
#undef OPCODE1_RETURN
#undef OPCODE2_RETURN
#undef OPCODE0_TAILRETURN
#undef OPCODE1_TAILRETURN
#undef OPCODE2_TAILRETURN
#undef OPCODE0_BRANCH
#undef OPCODE1_BRANCH
#undef OPCODE2_BRANCH
#undef OPCODE0_TAILBRANCH
#undef OPCODE1_TAILBRANCH
#undef OPCODE2_TAILBRANCH
#undef OPCODE0_JUMP
#undef OPCODE1_JUMP
#undef OPCODE2_JUMP
#undef OPCODE0_TAILJUMP
#undef OPCODE1_TAILJUMP
#undef OPCODE2_TAILJUMP
#undef OPCODE0_ALIAS
#undef OPCODE1_ALIAS
#undef OPCODE2_ALIAS

ATTRIBUTE((destructor))
MACRO void write_stats() {
    int i;
    FILE* file = fopen("/home/el/opcode.state", "a");

    for (i = 0; i < F_MAX_INSTR - F_OFFSET; i++) {
        if (!stats_m[i] && !stats_b[i]) continue;

        fprintf(file, "%s\t%u\t%u\n", arm_get_opcode_name(i), stats_b[i], stats_m[i] - stats_b[i]);
    }

    fclose(file);
}
#endif

MACRO void record_opcode(PIKE_OPCODE_T code, int bytecode) {
    code = code; /* prevent unused warning */
    bytecode = bytecode;
#ifdef ARM64_LOW_DEBUG
    if (bytecode) {
        stats_b[code - F_OFFSET] ++;
    } else {
        stats_m[code - F_OFFSET] ++;
    }
#endif
}

OPCODE_FUN set_64bit(unsigned INT32 instr) {
    return instr | (1<<31);
}

OPCODE_FUN set_rt_reg(unsigned INT32 instr, enum arm64_register r) {
    return instr | (r);
}

OPCODE_FUN set_rn_reg(unsigned INT32 instr, enum arm64_register r) {
    return instr | (r << 5);
}

OPCODE_FUN set_rm_reg(unsigned INT32 instr, enum arm64_register r) {
    return instr | (r << 16);
}

OPCODE_FUN set_arith_src_imm(unsigned INT32 instr, unsigned short imm, unsigned char shift) {
    return ARM_INSTR_ARITH_IMM | instr | (shift<<22) | (imm << 10);
}

OPCODE_FUN set_logic_src_imm(unsigned INT32 instr, unsigned char n, unsigned char immr, unsigned char imms) {
    return ARM_INSTR_LOGIC_IMM | instr | (n<<22) | (immr << 16) | (imms << 10);
}

/* Emulated... */
enum arm64_multiple_mode {
#define MODE(P, U, W)   (((P)<<2) | ((U)<<1) | ((W)))
    ARM_MULT_DB  = MODE(1, 0, 0),
    ARM_MULT_DA  = MODE(0, 0, 0),
    ARM_MULT_IB  = MODE(1, 1, 0),
    ARM_MULT_IA  = MODE(0, 1, 0),
    ARM_MULT_DBW = MODE(1, 0, 1),
    ARM_MULT_DAW = MODE(0, 0, 1),
    ARM_MULT_IBW = MODE(1, 1, 1),
    ARM_MULT_IAW = MODE(0, 1, 1)
#undef MODE
};

static void emulate_multiple(enum arm64_register addr, enum arm64_multiple_mode mode,
			     unsigned INT32 registers,  int sf, int l)
{
    /* Note: DA(W) and IB(W) probably use the wrong offset for the
       last/first pair, but they are not used anyway... */

    short offs = 0, step = ((mode & 2)? 2 : -2);
    INT32 instr = ARM_INSTR_LOADSTORE_PAIR | (sf<<31) | (l<<22);
    if (mode & 1) {
	/* Writeback */
	offs = step;
	instr |= (1<<23);
	if (mode & 4)
	    instr |= (1<<24);
    } else {
	instr |= (1<<24);
    }
    while (registers) {
	enum arm64_register rt, rt2;
	if (mode & 2) {
	    /* Ascending, lowest register first */
	    rt = ctz32(registers);
	    rt2 = ctz32(registers&~RBIT(rt));
	    if (rt2 >= 32)
		break;
	} else {
	    /* Descending, highest register first */
	    rt2 = 31-clz32(registers);
	    rt = 31-clz32(registers&~RBIT(rt2));
	    if (rt < 0)
		break;
	}
	registers &= ~(RBIT(rt)|RBIT(rt2));
	if ((mode&~2) == 4) {
	    /* No writeback, pre-inc/dec */
	    offs += step;
	}
	add_to_program(instr | ((offs&0x7f) << 15) | (rt2 << 10) | (addr << 5) | rt);
	if ((mode&~2) == 0) {
	    /* No writeback, post-inc/dec */
	    offs += step;
	}
    }
    if (registers) {
	/* FIXME? Could do a single load/store here... */
	Pike_fatal("Registers not in pairs");
    }
}

MACRO void store_multiple(enum arm64_register addr, enum arm64_multiple_mode mode,
                          unsigned INT32 registers) {
    emulate_multiple(addr, mode, registers, 1, 0);
}

MACRO void load_multiple(enum arm64_register addr, enum arm64_multiple_mode mode,
                         unsigned INT32 registers) {
    emulate_multiple(addr, mode, registers, 1, 1);
}

OPCODE_FUN gen_mov_wide(enum arm64_register reg, unsigned short imm, unsigned char shift) {
    return ARM_INSTR_MOVE_WIDE | ARM_WIDE_MOVZ | (shift << 21) | (imm << 5) | reg;
}

OPCODE_FUN gen_mov_widen(enum arm64_register reg, unsigned short imm, unsigned char shift) {
    return ARM_INSTR_MOVE_WIDE | ARM_WIDE_MOVN | (shift << 21) | (imm << 5) | reg;
}

MACRO void mov_wide(enum arm64_register reg, unsigned short imm, unsigned char shift) {
    add_to_program(set_64bit(gen_mov_wide(reg, imm, shift)));
}

MACRO void mov_widen(enum arm64_register reg, unsigned short imm, unsigned char shift) {
    add_to_program(set_64bit(gen_mov_widen(reg, imm, shift)));
}

OPCODE_FUN gen_mov_top(enum arm64_register reg, unsigned short imm, unsigned char shift) {
    return ARM_INSTR_MOVE_WIDE | ARM_WIDE_MOVK | (shift << 21) | (imm << 5) | reg;
}

MACRO void mov_top(enum arm64_register reg, unsigned short imm, unsigned char shift) {
    add_to_program(set_64bit(gen_mov_top(reg, imm, shift)));
}

OPCODE_FUN gen_br_reg(enum arm64_register to) {
    unsigned INT32 instr = ARM_INSTR_UNCOND_BRANCH_REG;

    instr |= (to << 5);
    instr |= (31<<16);

    return instr;
}

MACRO void br_reg(enum arm64_register to) {
    add_to_program(gen_br_reg(to));
}

OPCODE_FUN gen_blr_reg(enum arm64_register to) {
    return gen_br_reg(to) | (1<<21);
}

MACRO void blr_reg(enum arm64_register to) {
    add_to_program(gen_blr_reg(to));
}

OPCODE_FUN gen_ret_reg(enum arm64_register to) {
    return gen_br_reg(to) | (1<<22);
}

MACRO void ret_reg(enum arm64_register to) {
    add_to_program(gen_ret_reg(to));
}

OPCODE_FUN gen_b_imm(INT32 dist) {
    unsigned INT32 instr = ARM_INSTR_UNCOND_BRANCH_IMM;

    instr |= dist & 0x3ffffff;

    return instr;
}

MACRO void b_imm(INT32 dist) {
    add_to_program(gen_b_imm(dist));
}

OPCODE_FUN gen_b_imm_cond(INT32 dist, enum arm64_condition cond) {
    unsigned INT32 instr = ARM_INSTR_COND_BRANCH_IMM;

    instr |= (dist & 0x7ffff) << 5;
    instr |= cond;

    return instr;
}

MACRO void b_imm_cond(INT32 dist, enum arm64_condition cond) {
    add_to_program(gen_b_imm_cond(dist, cond));
}

OPCODE_FUN gen_bl_imm(INT32 dist) {
    return gen_b_imm(dist) | (1<<31);
}

MACRO void bl_imm(INT32 dist) {
    add_to_program(gen_bl_imm(dist));
}

OPCODE_FUN gen_arith_reg_reg(enum arith_instr op, enum arm64_register dst,
			     enum arm64_register a, enum arm64_register b,
			     unsigned short imm) {
    unsigned INT32 instr = ARM_INSTR_ARITH_REG;

    instr |= op;
    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, a);
    instr = set_rm_reg(instr, b);
    instr |= (imm << 10);

    return instr;
}

OPCODE_FUN gen_logic_reg_reg(enum logic_instr op, enum arm64_register dst,
			     enum arm64_register a, enum arm64_register b,
			     unsigned short imm) {
    unsigned INT32 instr = ARM_INSTR_LOGIC_REG;

    instr |= op;
    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, a);
    instr = set_rm_reg(instr, b);
    instr |= (imm << 10);

    return instr;
}

OPCODE_FUN gen_cmp_reg_reg(enum arm64_register a, enum arm64_register b) {
    return gen_arith_reg_reg(ARM_ARITH_SUBS, ARM_REG_ZERO, a, b, 0);
}

MACRO void cmp_reg_reg(enum arm64_register a, enum arm64_register b) {
    add_to_program(set_64bit(gen_cmp_reg_reg(a, b)));
}

OPCODE_FUN gen_ccmp_reg_reg(enum arm64_register a, enum arm64_register b,
			    enum arm64_condition cond, unsigned char nzcv)
{
    unsigned INT32 instr = ARM_INSTR_COND_COMPARE;
    instr |= 1<<30; /* CCMP */
    instr = set_rn_reg(instr, a);
    instr = set_rm_reg(instr, b);
    instr |= cond << 12;
    instr |= nzcv;
    return instr;
}

OPCODE_FUN gen_csinc(enum arm64_register dst, enum arm64_register a, enum arm64_register b,
		     enum arm64_condition cond)
{
    unsigned INT32 instr = ARM_INSTR_COND_SELECT;
    instr |= 1<<10; /* CSINC */
    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, a);
    instr = set_rm_reg(instr, b);
    instr |= cond << 12;
    return instr;
}


/* returns 1 if v can be represented as a shifted imm, with imm and shift set */
MACRO int arm64_make_arith_imm(unsigned INT64 v, unsigned short *imm, unsigned char *shift) {
    if (!(v & ~0xfff)) {
        *imm = v;
        *shift = 0;
        return 1;
    } else if (!(v & ~0xfff000)) {
        *imm = v >> 12;
        *shift = 1;
        return 1;
    }

    return 0;
}

/* returns 1 if v can be represented as a bitmask imm, with n, immr and imms set */
MACRO int arm64_make_logic_imm(unsigned INT64 v, unsigned char *n, unsigned char *immr, unsigned char *imms, int sf) {
    int e = (sf? 64 : 32);
    if (!sf) {
        v = (v << 32) | (unsigned INT32)v;
    }
    if (v == 0 || (~v) == 0) {
	/* bitmask immediates can't represent all zeroes or all ones,
	   Need to use ARM_REG_ZERO for this case */
	return 0;
    }
    for (; e >= 2; e>>=1) {
	unsigned INT64 elt;
	if (e < 64) {
	    unsigned INT64 mask = (1UL<<e)-1;
	    elt = v & mask;
	    if (((v >> e)&mask) != elt)
		break;
	} else {
	    elt = v;
	}
	int rot = 0;
	int cnt = ctz64(elt);
	if (cnt) {
	    elt >>= cnt;
	    rot += e-cnt;
	}
	cnt = ctz64(~elt);
	elt >>= cnt;
	if (elt != 0) {
	    int cnt2 = ctz64(elt);
	    if ((int)(ctz64(~(elt >> cnt2))) + cnt2 < e-cnt)
		continue;
	    cnt2 = e-cnt-cnt2;
	    rot += cnt2;
	    cnt += cnt2;
	}
	rot &= (e-1);
	if (e == 64) {
	    *n = 1;
	    *immr = rot;
	    *imms = cnt-1;
	} else {
	    *n = 0;
	    *immr = rot;
	    *imms = (cnt-1)|(0x3f^(e|(e-1)));
	}
	return 1;
    }
    return 0;
}

MACRO void arm64_mov_int(enum arm64_register reg, unsigned INT64 v);

OPCODE_FUN gen_arith_reg_imm(enum arith_instr op, enum arm64_register dst,
			     enum arm64_register reg, unsigned short imm, unsigned char shift) {
    unsigned INT32 instr = set_arith_src_imm(op, imm, shift);

    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, reg);

    return instr;
}

OPCODE_FUN gen_logic_reg_imm(enum logic_instr op, enum arm64_register dst,
			     enum arm64_register reg, unsigned char n, unsigned char immr, unsigned char imms) {
    unsigned INT32 instr = set_logic_src_imm(op, n, immr, imms);

    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, reg);

    return instr;
}

OPCODE_FUN gen_cmp_reg_imm(enum arm64_register a, unsigned short imm, unsigned char shift) {
    return gen_arith_reg_imm(ARM_ARITH_SUBS, ARM_REG_ZERO, a, imm, shift);
}

OPCODE_FUN gen_shift_reg_reg(enum arm64_shift_mode mode, enum arm64_register dst,
                             enum arm64_register a, enum arm64_register b) {
    unsigned INT32 instr = ARM_INSTR_SHIFT_REG;
    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, a);
    instr = set_rm_reg(instr, b);
    instr |= mode >> 12;

    return instr;
}

MACRO void cmp_reg_imm(enum arm64_register a, unsigned short imm, unsigned char shift) {
    add_to_program(set_64bit(gen_cmp_reg_imm(a, imm, shift)));
}

OPCODE_FUN gen_store_reg_imm(enum arm64_register dst, enum arm64_register base, INT32 offset, int sf) {
    unsigned INT32 instr = ARM_INSTR_LOADSTORE_SINGLE | (sf << 30);

    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, base);

    if (offset >= 0 && !(offset & (sf? 7 : 3))) {
        offset >>= (sf? 3 : 2);
	instr |= (1<<24);
	instr |= offset << 10;
	assert(!(offset >> 12));
    } else {
	instr |= (offset & 0x1ff) << 12;
	assert((offset & 0x100)?
	       (offset | 0x1ff) == -1 :
	       (offset & ~0x1ff) == 0);
    }

    return instr;
}

MACRO void store32_reg_imm(enum arm64_register dst, enum arm64_register base, INT32 offset) {
    add_to_program(gen_store_reg_imm(dst, base, offset, 0));
}

MACRO void store64_reg_imm(enum arm64_register dst, enum arm64_register base, INT32 offset) {
    add_to_program(gen_store_reg_imm(dst, base, offset, 1));
}

OPCODE_FUN gen_load_reg_imm(enum arm64_register dst, enum arm64_register base, INT32 offset, int sf) {
    return gen_store_reg_imm(dst, base, offset, sf) | (1<<22);
}

MACRO void load32_reg_imm(enum arm64_register dst, enum arm64_register base, INT32 offset) {
    add_to_program(gen_load_reg_imm(dst, base, offset, 0));
}

MACRO void load64_reg_imm(enum arm64_register dst, enum arm64_register base, INT32 offset) {
    add_to_program(gen_load_reg_imm(dst, base, offset, 1));
}

OPCODE_FUN gen_store_reg_reg(enum arm64_register dst, enum arm64_register base, enum arm64_register index, int s, int sf) {
    unsigned INT32 instr = ARM_INSTR_LOADSTORE_SINGLE | (sf << 30);
    instr |= 1<<21;
    instr |= 1<<11;
    instr |= 3<<13; /* LSL */
    instr |= s<<12;
    instr = set_rt_reg(instr, dst);
    instr = set_rn_reg(instr, base);
    instr = set_rm_reg(instr, index);
    return instr;
}

MACRO void store32_reg_reg(enum arm64_register dst, enum arm64_register base, enum arm64_register index, int s) {
    add_to_program(gen_store_reg_reg(dst, base, index, s, 0));
}

MACRO void store64_reg_reg(enum arm64_register dst, enum arm64_register base, enum arm64_register index, int s) {
    add_to_program(gen_store_reg_reg(dst, base, index, s, 1));
}

OPCODE_FUN gen_load_reg_reg(enum arm64_register dst, enum arm64_register base, enum arm64_register index, int s, int sf) {
    return gen_store_reg_reg(dst, base, index, s, sf) | (1<<22);
}

MACRO void load32_reg_reg(enum arm64_register dst, enum arm64_register base, enum arm64_register index, int s) {
    add_to_program(gen_load_reg_reg(dst, base, index, s, 0));
}

MACRO void load64_reg_reg(enum arm64_register dst, enum arm64_register base, enum arm64_register index, int s) {
    add_to_program(gen_load_reg_reg(dst, base, index, s, 1));
}

#define GEN_ARITH_OP(name, NAME)                                                                         \
OPCODE_FUN gen_ ## name ## _reg_imm(enum arm64_register dst, enum arm64_register reg,                    \
                                    unsigned short imm, unsigned char shift) {                           \
    return gen_arith_reg_imm(ARM_ARITH_ ## NAME, dst, reg, imm, shift);                                  \
}                                                                                                        \
OPCODE_FUN gen_ ## name ## _reg_reg(enum arm64_register dst, enum arm64_register a,                      \
                                    enum arm64_register b) {                                             \
    return gen_arith_reg_reg(ARM_ARITH_ ## NAME, dst, a, b, 0);                                          \
}                                                                                                        \
                                                                                                         \
MACRO void name ## 32_reg_imm(enum arm64_register dst, enum arm64_register reg, unsigned short imm,      \
                                      unsigned char shift) {                                             \
    add_to_program(gen_ ## name ## _reg_imm(dst, reg, imm, shift));                                      \
}                                                                                                        \
MACRO void name ## 32_reg_reg(enum arm64_register dst, enum arm64_register a, enum arm64_register b) {   \
    add_to_program(gen_ ## name ## _reg_reg(dst, a, b));                                                 \
}                                                                                                        \
MACRO void arm64_ ## name ## 32_reg_int(enum arm64_register dst, enum arm64_register a, unsigned INT32 v) {\
    unsigned short imm;                                                                                  \
    unsigned char shift;                                                                                 \
                                                                                                         \
    if (arm64_make_arith_imm(v, &imm, &shift)) {                                                         \
        name ## 32_reg_imm(dst, a, imm, shift);                                                          \
    } else {                                                                                             \
        enum arm64_register tmp = ra_alloc_any();                                                        \
        arm64_mov_int(tmp, v);                                                                           \
        name ## 32_reg_reg(dst, a, tmp);                                                                 \
        ra_free(tmp);                                                                                    \
    }                                                                                                    \
}                                                                                                        \
MACRO void name ## 64_reg_imm(enum arm64_register dst, enum arm64_register reg, unsigned short imm,      \
                                      unsigned char shift) {                                             \
    add_to_program(set_64bit(gen_ ## name ## _reg_imm(dst, reg, imm, shift)));                           \
}                                                                                                        \
MACRO void name ## 64_reg_reg(enum arm64_register dst, enum arm64_register a, enum arm64_register b) {   \
    add_to_program(set_64bit(gen_ ## name ## _reg_reg(dst, a, b)));                                      \
}                                                                                                        \
MACRO void arm64_ ## name ## 64_reg_int(enum arm64_register dst, enum arm64_register a, unsigned INT64 v) {\
    unsigned short imm;                                                                                  \
    unsigned char shift;                                                                                 \
                                                                                                         \
    if (arm64_make_arith_imm(v, &imm, &shift)) {                                                         \
        name ## 64_reg_imm(dst, a, imm, shift);                                                          \
    } else {                                                                                             \
        enum arm64_register tmp = ra_alloc_any();                                                        \
        arm64_mov_int(tmp, v);                                                                           \
        name ## 64_reg_reg(dst, a, tmp);                                                                 \
        ra_free(tmp);                                                                                    \
    }                                                                                                    \
}                                                                                                        \

#define GEN_LOGIC_OP(name, NAME)                                                                         \
OPCODE_FUN gen_ ## name ## _reg_imm(enum arm64_register dst, enum arm64_register reg,                    \
                                    unsigned char n, unsigned char immr, unsigned char imms) {           \
    return gen_logic_reg_imm(ARM_LOGIC_ ## NAME, dst, reg, n, immr, imms);                               \
}                                                                                                        \
OPCODE_FUN gen_ ## name ## _reg_reg(enum arm64_register dst, enum arm64_register a,                      \
                                    enum arm64_register b) {                                             \
    return gen_logic_reg_reg(ARM_LOGIC_ ## NAME, dst, a, b, 0);                                          \
}                                                                                                        \
                                                                                                         \
MACRO void name ## 32_reg_imm(enum arm64_register dst, enum arm64_register reg, unsigned char n,         \
                                      unsigned char immr, unsigned char imms) {                          \
    add_to_program(gen_ ## name ## _reg_imm(dst, reg, n, immr, imms));                                   \
}                                                                                                        \
MACRO void name ## 32_reg_reg(enum arm64_register dst, enum arm64_register a, enum arm64_register b) {   \
    add_to_program(gen_ ## name ## _reg_reg(dst, a, b));                                                 \
}                                                                                                        \
MACRO void arm64_ ## name ## 32_reg_int(enum arm64_register dst, enum arm64_register a, unsigned INT32 v) {\
    unsigned char n, immr, imms;                                                                         \
                                                                                                         \
    if (arm64_make_logic_imm(v, &n, &immr, &imms, 0)) {                                                  \
        name ## 32_reg_imm(dst, a, n, immr, imms);                                                       \
    } else {                                                                                             \
        enum arm64_register tmp = ra_alloc_any();                                                        \
        arm64_mov_int(tmp, v);                                                                           \
        name ## 32_reg_reg(dst, a, tmp);                                                                 \
        ra_free(tmp);                                                                                    \
    }                                                                                                    \
}                                                                                                        \
MACRO void name ## 64_reg_imm(enum arm64_register dst, enum arm64_register reg, unsigned char n,         \
                                      unsigned char immr, unsigned char imms) {                          \
    add_to_program(set_64bit(gen_ ## name ## _reg_imm(dst, reg, n, immr, imms)));                        \
}                                                                                                        \
MACRO void name ## 64_reg_reg(enum arm64_register dst, enum arm64_register a, enum arm64_register b) {   \
    add_to_program(set_64bit(gen_ ## name ## _reg_reg(dst, a, b)));                                      \
}                                                                                                        \
MACRO void arm64_ ## name ## 64_reg_int(enum arm64_register dst, enum arm64_register a, unsigned INT64 v) {\
    unsigned char n, immr, imms;                                                                         \
                                                                                                         \
    if (arm64_make_logic_imm(v, &n, &immr, &imms, 1)) {                                                  \
        name ## 64_reg_imm(dst, a, n, immr, imms);                                                       \
    } else {                                                                                             \
        enum arm64_register tmp = ra_alloc_any();                                                        \
        arm64_mov_int(tmp, v);                                                                           \
        name ## 64_reg_reg(dst, a, tmp);                                                                 \
        ra_free(tmp);                                                                                    \
    }                                                                                                    \
}                                                                                                        \

#define GEN_SHIFT_OP(name, NAME)                                                                         \
OPCODE_FUN gen_ ## name ## _reg_reg(enum arm64_register dst, enum arm64_register a,                      \
                                    enum arm64_register b) {                                             \
    return gen_shift_reg_reg(ARM_SHIFT_ ## NAME, dst, a, b);                                             \
}                                                                                                        \
                                                                                                         \
MACRO void name ## 32_reg_reg(enum arm64_register dst, enum arm64_register a, enum arm64_register b) {   \
    add_to_program(gen_ ## name ## _reg_reg(dst, a, b));                                                 \
}                                                                                                        \
MACRO void name ## 64_reg_reg(enum arm64_register dst, enum arm64_register a, enum arm64_register b) {   \
    add_to_program(set_64bit(gen_ ## name ## _reg_reg(dst, a, b)));                                      \
}                                                                                                        \

GEN_ARITH_OP(add, ADD)
GEN_ARITH_OP(adds, ADDS)
GEN_ARITH_OP(sub, SUB)
GEN_ARITH_OP(subs, SUBS)
GEN_LOGIC_OP(and, AND)
GEN_LOGIC_OP(or, OR)
GEN_LOGIC_OP(eor, EOR)
GEN_LOGIC_OP(ands, ANDS)

GEN_SHIFT_OP(lsl, LSL)


OPCODE_FUN gen_mov_reg(enum arm64_register dst, enum arm64_register src) {
    return set_64bit(gen_logic_reg_reg(ARM_LOGIC_OR, dst, ARM_REG_ZERO, src, 0));
}

MACRO void mov_reg(enum arm64_register dst, enum arm64_register src) {
    add_to_program(gen_mov_reg(dst, src));
}

OPCODE_FUN gen_mov_imm(enum arm64_register dst, unsigned char n, unsigned char immr, unsigned char imms) {
    return gen_logic_reg_imm(ARM_LOGIC_OR, dst, ARM_REG_ZERO, n, immr, imms);
}

MACRO void mov_imm(enum arm64_register dst, unsigned char n, unsigned char immr, unsigned char imms, int sf) {
    unsigned INT32 instr = gen_mov_imm(dst, n, immr, imms);
    add_to_program(sf? set_64bit(instr) : instr);
}

MACRO int arm64_move_wide_preferred(unsigned char n, unsigned char immr, unsigned char imms, int sf)
{
  int width = (sf? 64 : 32);
  if (sf && !n)
    return 0;
  if (!sf && (n || (imms&32)))
    return 0;
  if (imms < 16)
    return ((-immr) & 15) <= (15 - imms);
  if (imms >= width-15)
    return (immr & 15) <= (imms - (width-15));
  return 0;
}

MACRO void arm64_mov_int(enum arm64_register reg, unsigned INT64 v) {
    unsigned char n, immr, imms;
    int sf = (((unsigned INT32)v) != v);
    if (arm64_make_logic_imm(v, &n, &immr, &imms, sf) &&
	!arm64_move_wide_preferred(n, immr, imms, sf)) {
        mov_imm(reg, n, immr, imms, sf);
    } else {
        int i;
	int pos = 0, neg = 0;
	unsigned INT64 v2;
	for (i = 0, v2 = v; i < 4; i++) {
	    if (!(v2 & 0xffff)) pos++;
	    if (!((~v2) & 0xffff)) neg++;
	    v2 >>= 16;
	}
	if (pos > neg)
	    neg = 0;
	i = 0;
	if (pos < 4 && neg < 4)
	    for (; i < 3; i++) {
	        if (((neg? ~v:v)&0xffff) != 0)
		    break;
		v >>= 16;
	    }
	if (neg) {
	    mov_widen(reg, ~v, i);
	} else {
	    mov_wide(reg, v, i);
	}
	for (++i; i<4; i++) {
	    v >>= 16;
	    if ((v & 0xffff) != (neg? 0xffff : 0)) {
	        mov_top(reg, v, i);
	    }
	}
    }
}

OPCODE_FUN gen_adr_imm(enum arm64_register dst, INT32 offs)
{
    assert ((offs < 0? (offs|0x1fffff) == -1 : (offs&~0x1fffff) == 0));
    return set_rt_reg(ARM_INSTR_PC_REL | ((offs & 3) << 29) | ((offs & 0x1ffffc) << 3), dst);
}

MACRO void adr_imm(enum arm64_register dst, INT32 offs)
{
    add_to_program(gen_adr_imm(dst, offs));
}

/*
 * TODO: work with labels
 */
MACRO void arm64_rel_cond_jmp(enum arm64_register reg, enum arm64_register b, enum arm64_condition cond, int jmp) {
    cmp_reg_reg(reg, b);
    b_imm_cond(jmp, cond);
}


static void arm64_adr_imm_at(unsigned INT32 offset, enum arm64_register dst,
			     INT32 offs)
{
    upd_pointer(offset, gen_adr_imm(dst, offs));
}

/*
 * "High" level interface
 */

#define FLAG_SP_LOADED  1
#define FLAG_FP_LOADED  2
#define FLAG_LOCALS_LOADED 4

struct location_list_entry {
    unsigned INT32 location;
    struct location_list_entry *next;
};

struct compiler_state {
    /* currently unused and dirt registers */
    unsigned INT32 free, dirt;
    /* address into which the initial stack push has to be
     * generated
     */
    unsigned INT32 push_addr;
    unsigned INT32 flags;
} compiler_state;


static struct location_list_entry* add_to_list(struct location_list_entry *list, unsigned INT32 location) {
    struct location_list_entry *e = xalloc(sizeof(struct location_list_entry));

    e->location = location;
    e->next = list;

    return e;
}

static void free_list(struct location_list_entry *list) {
    while (list) {
        struct location_list_entry * next = list->next;
        free(list);
        list = next;
    }
}

struct label {
    struct location_list_entry *list;
    unsigned INT32 loc;
};

static void label_init(struct label *l) {
    l->list = NULL;
    l->loc = (unsigned INT32)-1;
}

MACRO INT32 label_dist(struct label *l) {
    if (l->loc != (unsigned INT32)-1)
        return l->loc - PIKE_PC;
    l->list = add_to_list(l->list, PIKE_PC);
    return 0;
}

MACRO void label_generate(struct label *l) {
    unsigned INT32 loc = PIKE_PC;

    assert(l->loc == (unsigned INT32)-1);

    l->loc = loc;

    struct location_list_entry *e = l->list;

    while (e) {
        unsigned INT32 instr = read_pointer(e->location);
	if ((instr & 0x7c000000) == ARM_INSTR_UNCOND_BRANCH_IMM) {
	    if (instr & 0x3ffffff) {
	        Pike_fatal("Setting label distance twice in %x\n", instr);
	    }
	    upd_pointer(e->location, instr|(loc - e->location));
	} else if ((instr & 0xfe000000) == ARM_INSTR_COND_BRANCH_IMM) {
	    if (instr & 0xffffe0) {
	        Pike_fatal("Setting label distance twice in %x\n", instr);
	    }
	    upd_pointer(e->location, instr|((loc - e->location) << 5));
	}
        e = e->next;
    }

    free_list(l->list);
}

MACRO void ra_init(void) {
    /* all register r0 through r24 are unused
     *
     */
    compiler_state.free = RBIT(0)|RBIT(1)|RBIT(2)|RBIT(3)|RBIT(4)|RBIT(5)|
      RBIT(6)|RBIT(7)|RBIT(8)|RBIT(9)|RBIT(10)|RBIT(11)|RBIT(12)|RBIT(13)|
      RBIT(14)|RBIT(15)|RBIT(19);
    compiler_state.dirt = 0;
    compiler_state.push_addr = -1;
    compiler_state.flags = 0;
    // FIXME: not quite the place
    instrs[F_CATCH - F_OFFSET].address = inter_return_opcode_F_CATCH;
}

MACRO enum arm64_register ra_alloc(enum arm64_register reg) {
    unsigned INT32 rbit = RBIT(reg);

    if (!(rbit & compiler_state.free))
        Pike_fatal("Register %d is already in use.\n", reg);

    compiler_state.free ^= rbit;
    compiler_state.dirt |= rbit;

    return reg;
}

MACRO enum arm64_register ra_alloc_persistent(void) {
    unsigned INT32 free = compiler_state.free;

    /* we dont want 0-18 */
    free &= 0xfffc0000;

    if (!free)
        Pike_fatal("No register left: %x\n", compiler_state.free);

    return ra_alloc(ctz32(free));
}

MACRO enum arm64_register ra_alloc_any(void) {
    if (!compiler_state.free)
        Pike_fatal("No register left.\n");

    return ra_alloc(ctz32(compiler_state.free));
}

MACRO void ra_free(enum arm64_register reg) {
    unsigned INT32 rbit = RBIT(reg);

    if (rbit & compiler_state.free)
        Pike_fatal("Register %d is not in use.\n", reg);

    compiler_state.free |= rbit;
}

MACRO int ra_is_dirty(enum arm64_register reg) {
    unsigned INT32 rbit = RBIT(reg);

    return !!(rbit & compiler_state.dirt);
}

MACRO int ra_is_free(enum arm64_register reg) {
    unsigned INT32 rbit = RBIT(reg);

    return !!(rbit & compiler_state.free);
}

MACRO void add_push(void) {
    unsigned INT32 registers;

    /* NOTE: number of registers always needs to be even */
    registers = RBIT(ARM_REG_PIKE_SP)
                |RBIT(ARM_REG_PIKE_IP)
                |RBIT(ARM_REG_PIKE_FP)
                |RBIT(ARM_REG_PIKE_LOCALS)
                |RBIT(ARM_REG_LR)
                |RBIT(19);

    store_multiple(ARM_REG_SP, ARM_MULT_DBW, registers);
}

MACRO void add_pop(void) {
    unsigned INT32 registers;

    /* NOTE: number of registers always needs to be even */
    registers = RBIT(ARM_REG_PIKE_SP)
                |RBIT(ARM_REG_PIKE_IP)
                |RBIT(ARM_REG_PIKE_FP)
                |RBIT(ARM_REG_PIKE_LOCALS)
                |RBIT(ARM_REG_LR)
                |RBIT(19);

    load_multiple(ARM_REG_SP, ARM_MULT_IAW, registers);
}

/* corresponds to ENTRY_PROLOGUE_SIZE */
MACRO void arm64_prologue(void) {
    add_push();
    mov_reg(ARM_REG_PIKE_IP, ARM_REG_R0);
}

#define EPILOGUE_SIZE 4
MACRO void arm64_epilogue(void) {
    add_pop();
    ret_reg(ARM_REG_LR);
}

MACRO void arm64_call(void *ptr) {
    unsigned INT64 v = (char*)ptr - (char*)NULL;
    enum arm64_register tmp = ra_alloc_any();

    arm64_mov_int(tmp, v);
    blr_reg(tmp);
    ra_free(tmp);
}

MACRO void arm64_ins_branch_check_threads_etc(int a) {
}

void arm64_flush_instruction_cache(void *addr, size_t len) {
    __builtin___clear_cache(addr, (char*)addr+len);
}

MACRO void arm64_low_load_sp_reg(void) {
    INT32 offset = OFFSETOF(Pike_interpreter_struct, stack_pointer);

    load64_reg_imm(ARM_REG_PIKE_SP, ARM_REG_PIKE_IP, offset);
}

static void arm64_load_sp_reg(void) {
    if (!(compiler_state.flags & FLAG_SP_LOADED)) {

        compiler_state.flags |= FLAG_SP_LOADED;

        arm64_low_load_sp_reg();
    }
}

static void arm64_store_sp_reg(void) {
    INT32 offset = OFFSETOF(Pike_interpreter_struct, stack_pointer);
    assert(compiler_state.flags & FLAG_SP_LOADED);
    store64_reg_imm(ARM_REG_PIKE_SP, ARM_REG_PIKE_IP, offset);
}

static void arm64_load_fp_reg(void) {
    if (!(compiler_state.flags & FLAG_FP_LOADED)) {
        INT32 offset = OFFSETOF(Pike_interpreter_struct, frame_pointer);
        /* load Pike_interpreter_pointer->frame_pointer into ARM_REG_PIKE_FP */
        load64_reg_imm(ARM_REG_PIKE_FP, ARM_REG_PIKE_IP, offset);
        compiler_state.flags |= FLAG_FP_LOADED;
        compiler_state.flags &= ~FLAG_LOCALS_LOADED;
    }
}

MACRO void arm64_load_locals_reg(void) {
    arm64_load_fp_reg();

    if (!(compiler_state.flags & FLAG_LOCALS_LOADED)) {
        INT32 offset = OFFSETOF(pike_frame, locals);

        load64_reg_imm(ARM_REG_PIKE_LOCALS, ARM_REG_PIKE_FP, offset);

        compiler_state.flags |= FLAG_LOCALS_LOADED;
    }
}

MACRO void arm64_change_sp_reg(INT32 offset) {
    assert(compiler_state.flags & FLAG_SP_LOADED);
    offset *= sizeof(struct svalue);
    if (offset < 0) {
        offset = -offset;
        arm64_sub64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, offset);
    } else {
        arm64_add64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, offset);
    }
}

MACRO void arm64_change_sp(INT32 offset) {
    arm64_load_sp_reg();
    arm64_change_sp_reg(offset);
    arm64_store_sp_reg();
}

static void arm64_flush_dirty_regs(void) {
    arm64_store_sp_reg();
}

MACRO void arm64_call_efun(void (*fun)(int), int args) {
    arm64_mov_int(ra_alloc(ARM_REG_R0), args);
    arm64_call(fun);
    ra_free(ARM_REG_R0);
    if (args != 1 && compiler_state.flags & FLAG_SP_LOADED) {
        arm64_change_sp_reg(-(args-1));
    }
}

MACRO void arm64_assign_int_reg(enum arm64_register dst, enum arm64_register value, int subtype) {
    unsigned INT32 combined = TYPE_SUBTYPE(PIKE_T_INT, subtype);
    enum arm64_register tmp1 = ra_alloc_any(), tmp2 = ra_alloc_any();

    assert(tmp1 < tmp2);

    arm64_mov_int(tmp1, combined);
    mov_reg(tmp2, value);

    store_multiple(dst, ARM_MULT_IA, RBIT(tmp1)|RBIT(tmp2));

    ra_free(tmp1);
    ra_free(tmp2);
}

MACRO void arm64_push_int_reg(enum arm64_register value, int subtype) {
    unsigned INT32 combined = TYPE_SUBTYPE(PIKE_T_INT, subtype);
    enum arm64_register tmp1 = ra_alloc_any(), tmp2 = ra_alloc_any();

    assert(tmp1 < tmp2);

    arm64_load_sp_reg();

    arm64_mov_int(tmp1, combined);
    mov_reg(tmp2, value);

    store_multiple(ARM_REG_PIKE_SP, ARM_MULT_IAW, RBIT(tmp1)|RBIT(tmp2));

    ra_free(tmp1);
    ra_free(tmp2);

    arm64_store_sp_reg();
}

MACRO void arm64_push_int(unsigned INT64 value, int subtype) {
    unsigned INT32 combined = TYPE_SUBTYPE(PIKE_T_INT, subtype);
    enum arm64_register tmp1 = ra_alloc_any(), tmp2 = ra_alloc_any();

    assert(tmp1 < tmp2);

    arm64_load_sp_reg();

    arm64_mov_int(tmp1, combined);
    arm64_mov_int(tmp2, value);

    store_multiple(ARM_REG_PIKE_SP, ARM_MULT_IAW, RBIT(tmp1)|RBIT(tmp2));

    ra_free(tmp1);
    ra_free(tmp2);

    arm64_store_sp_reg();
}

MACRO void arm64_push_ptr_type(enum arm64_register treg, enum arm64_register vreg) {
    assert(treg < vreg);

    arm64_load_sp_reg();
    store_multiple(ARM_REG_PIKE_SP, ARM_MULT_IAW, RBIT(treg)|RBIT(vreg));
    arm64_store_sp_reg();

    /* add reference */
    load32_reg_imm(treg, vreg, 0);
    arm64_add32_reg_int(treg, treg, 1);
    store32_reg_imm(treg, vreg, 0);
}

MACRO void arm64_move_svaluep_nofree(enum arm64_register dst, enum arm64_register from) {
    enum arm64_register treg = ra_alloc_any(),
                        vreg = ra_alloc_any();

    assert(treg < vreg);

    load_multiple(from, ARM_MULT_IA, RBIT(treg)|RBIT(vreg));
    store_multiple(dst, ARM_MULT_IA, RBIT(treg)|RBIT(vreg));

    ra_free(treg);
    ra_free(vreg);
}

MACRO void arm64_assign_svaluep_nofree(enum arm64_register dst, enum arm64_register from) {
    struct label end;
    enum arm64_register treg = ra_alloc_any(),
                        vreg = ra_alloc_any();

    assert(treg < vreg);

    load_multiple(from, ARM_MULT_IA, RBIT(treg)|RBIT(vreg));
    store_multiple(dst, ARM_MULT_IA, RBIT(treg)|RBIT(vreg));

    label_init(&end);
    arm64_ands32_reg_int(treg, treg, TYPE_SUBTYPE(MIN_REF_TYPE, 0));
    b_imm_cond(label_dist(&end), ARM_COND_Z);

    load32_reg_imm(treg, vreg, OFFSETOF(pike_string, refs));
    arm64_add32_reg_int(treg, treg, 1);
    store32_reg_imm(treg, vreg, OFFSETOF(pike_string, refs));
    label_generate(&end);

    ra_free(treg);
    ra_free(vreg);
}

MACRO void arm64_push_svaluep_off(enum arm64_register src, INT32 offset) {
    struct label end;
    enum arm64_register tmp1 = ra_alloc_any(),
                        tmp2 = ra_alloc_any();

    assert(tmp1 < tmp2);
    assert(sizeof(struct svalue) == 16);

    if (offset) {
        arm64_add64_reg_int(tmp1, src, offset*sizeof(struct svalue));

        load_multiple(tmp1, ARM_MULT_IA, RBIT(tmp1)|RBIT(tmp2));
    } else {
        load_multiple(src, ARM_MULT_IA, RBIT(tmp1)|RBIT(tmp2));
    }

    arm64_load_sp_reg();

    store_multiple(ARM_REG_PIKE_SP, ARM_MULT_IAW, RBIT(tmp1)|RBIT(tmp2));

    arm64_store_sp_reg();

    label_init(&end);
    arm64_ands32_reg_int(tmp1, tmp1, TYPE_SUBTYPE(MIN_REF_TYPE, 0));
    b_imm_cond(label_dist(&end), ARM_COND_Z);

    load32_reg_imm(tmp1, tmp2, OFFSETOF(pike_string, refs));
    arm64_add32_reg_int(tmp1, tmp1, 1);
    store32_reg_imm(tmp1, tmp2, OFFSETOF(pike_string, refs));
    label_generate(&end);

    ra_free(tmp1);
    ra_free(tmp2);
}

/* the returned condition will be true if both types are type_subtype */
MACRO enum arm64_condition arm64_eq_types(enum arm64_register type1, enum arm64_register type2,
                                          unsigned INT32 type_subtype) {
    unsigned short imm;
    unsigned char shift;
    int ok = arm64_make_arith_imm(type_subtype, &imm, &shift);

    assert(ok);

    add_to_program(gen_cmp_reg_imm(type1, imm, shift));
    add_to_program(gen_ccmp_reg_reg(type1, type2, ARM_COND_EQ, 0));

    return ARM_COND_EQ;
}

/* the returned condition will be true if unless both types are type_subtype */
MACRO enum arm64_condition arm64_ne_types(enum arm64_register type1, enum arm64_register type2,
                                          unsigned INT32 type_subtype) {
    unsigned short imm;
    unsigned char shift;
    int ok = arm64_make_arith_imm(type_subtype, &imm, &shift);

    assert(ok);

    add_to_program(gen_cmp_reg_imm(type1, imm, shift));
    add_to_program(gen_ccmp_reg_reg(type1, type2, ARM_COND_EQ, 0));

    return ARM_COND_NE;
}

MACRO void arm64_jump_real_cmp(struct label *l, enum arm64_register type1, enum arm64_register type2) {
    unsigned INT32 mask = BIT_INT|BIT_STRING|BIT_PROGRAM|BIT_MAPPING|BIT_ARRAY|BIT_MULTISET|BIT_TYPE;
    enum arm64_register reg = ra_alloc_any(),
                        one = ra_alloc_any(),
                        mask_reg = ra_alloc_any();

    arm64_mov_int(reg, mask);
    arm64_mov_int(one, 1);

    lsl32_reg_reg(mask_reg, one, type1);
    ands32_reg_reg(mask_reg, reg, mask_reg);
    b_imm_cond(label_dist(l), ARM_COND_Z);

    lsl32_reg_reg(mask_reg, one, type2);
    ands32_reg_reg(mask_reg, reg, mask_reg);
    b_imm_cond(label_dist(l), ARM_COND_Z);

    ra_free(reg);
    ra_free(one);
    ra_free(mask_reg);
}

void arm64_flush_codegen_state(void) {
    //fprintf(stderr, "flushing codegen state.\n");
    compiler_state.flags = 0;
}

void arm64_start_function(int UNUSED(no_pc)) {
    ra_init();
}

void arm64_end_function(int UNUSED(no_pc)) {
}

/* Store $pc into Pike_fp->pc */
void arm64_update_pc(void) {
    unsigned INT32 v = PIKE_PC;
    INT32 offset;
    enum arm64_register tmp = ra_alloc_any();

    arm64_load_fp_reg();
    v = 4*(PIKE_PC - v);
    adr_imm(tmp, -v);
    store64_reg_imm(tmp, ARM_REG_PIKE_FP, OFFSETOF(pike_frame, pc));
    ra_free(tmp);
}

void arm64_ins_entry(void) {
    arm64_prologue();
    arm64_flush_codegen_state();
}

static void arm64_ins_maybe_exit(void) {
    struct label noreturn;

    label_init(&noreturn);

    adds64_reg_imm(ARM_REG_ZERO, ARM_REG_R0, 1, 0);
    b_imm_cond(label_dist(&noreturn), ARM_COND_NZ);
    arm64_epilogue();
    label_generate(&noreturn);
}

MACRO void arm64_maybe_update_pc() {
  {
    static int last_prog_id=-1;
    static size_t last_num_linenumbers=(size_t)~0;
    if(last_prog_id != Pike_compiler->new_program->id ||
       last_num_linenumbers != Pike_compiler->new_program->num_linenumbers)
    {
      unsigned INT32 tmp = PIKE_PC;
      last_prog_id=Pike_compiler->new_program->id;
      last_num_linenumbers = Pike_compiler->new_program->num_linenumbers;

      UPDATE_PC();
    }
  }
}

static void low_ins_call(void *addr) {
  arm64_maybe_update_pc();
  arm64_call(addr);
}

static void arm64_call_c_function(void * addr) {
    compiler_state.flags &= ~FLAG_SP_LOADED;
    compiler_state.flags &= ~FLAG_FP_LOADED;
    arm64_call(addr);
}

static void arm64_call_c_opcode(unsigned int opcode) {
  void *addr = instrs[opcode-F_OFFSET].address;
  int flags = instrs[opcode-F_OFFSET].flags;

  record_opcode(opcode, 1);

  if (flags & I_UPDATE_SP) {
    compiler_state.flags &= ~FLAG_SP_LOADED;
  }
  if (flags & I_UPDATE_M_SP) {}
  if (flags & I_UPDATE_FP) {
    compiler_state.flags &= ~FLAG_FP_LOADED;
  }

  low_ins_call(addr);
}

MACRO void arm64_free_svalue_off(enum arm64_register src, int off, int guaranteed) {
    unsigned INT32 combined = TYPE_SUBTYPE(MIN_REF_TYPE, 0);
    unsigned char imm, rot;
    struct label end;
    enum arm64_register reg = ra_alloc(ARM_REG_R0);
    enum arm64_register tmp = ra_alloc_any();

    guaranteed = guaranteed;

    off *= sizeof(struct svalue);

    label_init(&end);

    load32_reg_imm(reg, src, off);

    arm64_ands32_reg_int(reg, reg, combined);

    b_imm_cond(label_dist(&end), ARM_COND_Z);

    load64_reg_imm(tmp, src, off+OFFSETOF(svalue, u));

    load32_reg_imm(reg, tmp, OFFSETOF(pike_string, refs));
    subs32_reg_imm(reg, reg, 1, 0);
    store32_reg_imm(reg, tmp, OFFSETOF(pike_string, refs));

    b_imm_cond(label_dist(&end), ARM_COND_NZ);

    if (off > 0) {
        arm64_add64_reg_int(reg, src, off);
    } else if (off < 0 ) {
        arm64_sub64_reg_int(reg, src, -off);
    } else {
        mov_reg(reg, src);
    }

    arm64_call(really_free_svalue);

    label_generate(&end);
    ra_free(reg);
    ra_free(tmp);
}


static void arm64_free_svalue(enum arm64_register reg, int guaranteed_ref) {
    arm64_free_svalue_off(reg, 0, guaranteed_ref);
}

static void arm64_mark(enum arm64_register base, int offset) {

  enum arm64_register tmp = ra_alloc_any();

  load64_reg_imm(tmp, ARM_REG_PIKE_IP, OFFSETOF(Pike_interpreter_struct, mark_stack_pointer));

  if (offset) {
    enum arm64_register tmp2 = ra_alloc_any();

    offset *= sizeof(struct svalue);

    if (offset > 0) {
      arm64_add64_reg_int(tmp2, base, offset);
    } else {
      arm64_sub64_reg_int(tmp2, base, -offset);
    }
    store64_reg_imm(tmp2, tmp, 0);

    ra_free(tmp2);
  } else {
    store64_reg_imm(base, tmp, 0);
  }

  add64_reg_imm(tmp, tmp, sizeof(void*), 0);
  store64_reg_imm(tmp, ARM_REG_PIKE_IP, OFFSETOF(Pike_interpreter_struct, mark_stack_pointer));

  ra_free(tmp);
}

MACRO void arm64_pop_mark(enum arm64_register dst) {
  enum arm64_register tmp = ra_alloc_any();

  load64_reg_imm(tmp, ARM_REG_PIKE_IP, OFFSETOF(Pike_interpreter_struct, mark_stack_pointer));

  arm64_sub64_reg_int(tmp, tmp, sizeof(void*));

  load64_reg_imm(dst, tmp, 0);

  store64_reg_imm(tmp, ARM_REG_PIKE_IP, OFFSETOF(Pike_interpreter_struct, mark_stack_pointer));

  ra_free(tmp);
}

#ifdef ARM64_PIKE_DEBUG
MACRO void arm_green_on(void) {
    if (Pike_interpreter.trace_level > 2)
        fprintf(stderr, "\33[032m");
}

MACRO void arm_green_off(void) {
    if (Pike_interpreter.trace_level > 2)
        fprintf(stderr, "\33[0m");
}

MACRO void arm64_debug_instr_prologue_0(PIKE_INSTR_T instr) {
  arm64_call(arm_green_on);
  arm64_maybe_update_pc();
  arm64_mov_int(ra_alloc(ARM_REG_R0), instr-F_OFFSET);
  arm64_call(simple_debug_instr_prologue_0);
  arm64_call(arm_green_off);
  ra_free(ARM_REG_R0);
}

MACRO void arm64_debug_instr_prologue_1(PIKE_INSTR_T instr, INT32 arg1) {
  arm64_call(arm_green_on);
  arm64_maybe_update_pc();
  arm64_mov_int(ra_alloc(ARM_REG_R0), instr-F_OFFSET);
  arm64_mov_int(ra_alloc(ARM_REG_R1), arg1);
  arm64_call(simple_debug_instr_prologue_1);
  arm64_call(arm_green_off);
  ra_free(ARM_REG_R0);
  ra_free(ARM_REG_R1);
}

MACRO void arm64_debug_instr_prologue_2(PIKE_INSTR_T instr, INT32 arg1, INT32 arg2) {
  arm64_call(arm_green_on);
  arm64_maybe_update_pc();
  arm64_mov_int(ra_alloc(ARM_REG_R0), instr-F_OFFSET);
  arm64_mov_int(ra_alloc(ARM_REG_R1), arg1);
  arm64_mov_int(ra_alloc(ARM_REG_R2), arg2);
  arm64_call(simple_debug_instr_prologue_2);
  arm64_call(arm_green_off);
  ra_free(ARM_REG_R0);
  ra_free(ARM_REG_R1);
  ra_free(ARM_REG_R2);
}
#else
#define arm64_debug_instr_prologue_1(a, b)  do {} while(0)
#define arm64_debug_instr_prologue_2(a, b, c)  do {} while(0)
#define arm64_debug_instr_prologue_0(a) do { } while(0)
#endif

static void low_ins_f_byte(unsigned int opcode)
{
  int flags;
  INT32 rel_addr = rel_addr;

  assert(opcode-F_OFFSET<=255);

  flags = instrs[opcode-F_OFFSET].flags;

  switch (opcode) {
  case F_UNDEFINED:
      arm64_debug_instr_prologue_0(opcode);
      arm64_push_int(0, NUMBER_UNDEFINED);
      return;
  case F_CONST_1:
      arm64_debug_instr_prologue_0(opcode);
      arm64_push_int(-1, NUMBER_NUMBER);
      return;
  case F_CONST1:
      arm64_debug_instr_prologue_0(opcode);
      arm64_push_int(1, NUMBER_NUMBER);
      return;
  case F_CONST0:
      arm64_debug_instr_prologue_0(opcode);
      arm64_push_int(0, NUMBER_NUMBER);
      return;
  case F_CATCH:
      {
          rel_addr = PIKE_PC;
          adr_imm(ra_alloc(ARM_REG_R0), 0);
      }
      break;
  case F_POP_VALUE:
      arm64_change_sp(-1);
      arm64_free_svalue(ARM_REG_PIKE_SP, 0);
      return;
  case F_POP_TO_MARK: /* this opcode sucks noodles, introduce F_POP_TO_LOCAL(num) */
      {
          struct label done, loop;
          enum arm64_register reg = ra_alloc_persistent();

          label_init(&done);
          label_init(&loop);

          arm64_load_sp_reg();

          arm64_pop_mark(reg);
          cmp_reg_reg(ARM_REG_PIKE_SP, reg);
          /* jump if pike_sp <= reg */
          b_imm_cond(label_dist(&done), ARM_COND_LS);

          label_generate(&loop);

          arm64_sub64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, 1*sizeof(struct svalue));
          arm64_free_svalue(ARM_REG_PIKE_SP, 0);

          cmp_reg_reg(ARM_REG_PIKE_SP, reg);
          /* jump if pike_sp > reg */
          b_imm_cond(label_dist(&loop), ARM_COND_HI);

          arm64_store_sp_reg();

          label_generate(&done);
          ra_free(reg);
      }
      return;
  case F_EQ:
  case F_NE:
  case F_GT:
  case F_GE:
  case F_LT:
  case F_LE:
      {
          struct label real_cmp, real_pop, end;
          enum arm64_register reg, type1, type2, tmp;
          enum arm64_condition cond;
          int (*cmp)(const struct svalue *a, const struct svalue *b);
          int swap = 0, negate = 0;

          label_init(&real_cmp);
          label_init(&real_pop);
          label_init(&end);

          type1 = ra_alloc_any();
          type2 = ra_alloc_any();

          arm64_load_sp_reg();

          load32_reg_imm(type1, ARM_REG_PIKE_SP, -2*(INT32)sizeof(struct svalue));
          load32_reg_imm(type2, ARM_REG_PIKE_SP, -1*(INT32)sizeof(struct svalue));

          switch (opcode) {
          case F_NE:
              negate = 1;
              /* FALL THROUGH */
          case F_EQ:
              cmp = is_eq;

              arm64_jump_real_cmp(&real_cmp, type1, type2);

              reg = ra_alloc_persistent();
              tmp = ra_alloc_any();

              load64_reg_imm(tmp, ARM_REG_PIKE_SP, -2*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));
              load64_reg_imm(reg, ARM_REG_PIKE_SP, -1*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));

              /* TODO: make those shorter */
	      cmp_reg_reg(reg, tmp);
              if (opcode == F_EQ) {
		  add_to_program(set_64bit(gen_csinc(reg, ARM_REG_ZERO, ARM_REG_ZERO, ARM_COND_NE)));
              } else {
                  add_to_program(set_64bit(gen_csinc(reg, ARM_REG_ZERO, ARM_REG_ZERO, ARM_COND_EQ)));
              }

              /* jump to real pop, if not both integers */
              cond = arm64_ne_types(type1, type2, TYPE_SUBTYPE(PIKE_T_INT, NUMBER_NUMBER));
              b_imm_cond(label_dist(&real_pop), cond);

              break;
          case F_GT:
          case F_GE:
          case F_LE:
          case F_LT:
              cond = arm64_ne_types(type1, type2, TYPE_SUBTYPE(PIKE_T_INT, NUMBER_NUMBER));
              b_imm_cond(label_dist(&real_cmp), cond);

              reg = ra_alloc_persistent();
              tmp = ra_alloc_any();

              load64_reg_imm(tmp, ARM_REG_PIKE_SP, -2*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));
              load64_reg_imm(reg, ARM_REG_PIKE_SP, -1*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));

              cmp_reg_reg(tmp, reg);

              switch (opcode) {
              case F_GT:
                  swap = 1;
                  cmp = is_lt;
                  add_to_program(set_64bit(gen_csinc(reg, ARM_REG_ZERO, ARM_REG_ZERO, ARM_COND_LE)));
                  break;
              case F_GE:
                  cmp = is_le;
                  swap = 1;
                  add_to_program(set_64bit(gen_csinc(reg, ARM_REG_ZERO, ARM_REG_ZERO, ARM_COND_LT)));
                  break;
              case F_LT:
                  cmp = is_lt;
                  add_to_program(set_64bit(gen_csinc(reg, ARM_REG_ZERO, ARM_REG_ZERO, ARM_COND_GE)));
                  break;
              case F_LE:
                  cmp = is_le;
                  add_to_program(set_64bit(gen_csinc(reg, ARM_REG_ZERO, ARM_REG_ZERO, ARM_COND_GT)));
                  break;
              }
              break;
          }

          ra_free(type1);
          ra_free(type2);
          ra_free(tmp);
          // SIMPLE POP INT:

          arm64_sub64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, sizeof(struct svalue));
          store64_reg_imm(reg, ARM_REG_PIKE_SP, -1*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));

          arm64_store_sp_reg();

          b_imm(label_dist(&end));
          // COMPLEX CMP:
          label_generate(&real_cmp);

          ra_alloc(ARM_REG_R0);
          ra_alloc(ARM_REG_R1);

          if (swap) {
              arm64_sub64_reg_int(ARM_REG_R1, ARM_REG_PIKE_SP, 2*sizeof(struct svalue));
              arm64_sub64_reg_int(ARM_REG_R0, ARM_REG_PIKE_SP, 1*sizeof(struct svalue));
          } else {
              arm64_sub64_reg_int(ARM_REG_R0, ARM_REG_PIKE_SP, 2*sizeof(struct svalue));
              arm64_sub64_reg_int(ARM_REG_R1, ARM_REG_PIKE_SP, 1*sizeof(struct svalue));
          }

          arm64_call(cmp);

          if (negate) {
              arm64_eor32_reg_int(reg, ARM_REG_R0, 1);
          } else {
              mov_reg(reg, ARM_REG_R0);
          }

          ra_free(ARM_REG_R0);
          ra_free(ARM_REG_R1);

          // COMPLEX POP:
          label_generate(&real_pop);

          arm64_free_svalue_off(ARM_REG_PIKE_SP, -1, 0);
          arm64_free_svalue_off(ARM_REG_PIKE_SP, -2, 0);
          /* the order of the free and pop is important, because really_free_svalue should not
           * use that region of the stack we are trying to free */
          arm64_sub64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, 2*sizeof(struct svalue));

          arm64_push_int_reg(reg, NUMBER_NUMBER);

          // END:
          label_generate(&end);

          ra_free(reg);
      }
      return;
  case F_MARK:
      arm64_load_sp_reg();
      arm64_mark(ARM_REG_PIKE_SP, 0);
      return;
  case F_MARK2:
      ins_f_byte(F_MARK);
      ins_f_byte(F_MARK);
      return;
  case F_MARK_AND_CONST0:
      ins_f_byte(F_MARK);
      ins_f_byte(F_CONST0);
      return;
  case F_MARK_AND_CONST1:
      ins_f_byte(F_MARK);
      ins_f_byte(F_CONST1);
      return;
  case F_MAKE_ITERATOR:
      arm64_debug_instr_prologue_0(opcode);
      arm64_call_efun(f_get_iterator, 1);
      return;
  case F_ADD:
      arm64_debug_instr_prologue_0(opcode);
      arm64_call_efun(f_add, 2);
      return;
  case F_ADD_INTS:
      {
          struct label end, slow;
          enum arm64_register reg1, reg2;
          enum arm64_condition cond;

          label_init(&end);
          label_init(&slow);

          arm64_debug_instr_prologue_0(opcode);

          reg1 = ra_alloc_any();
          reg2 = ra_alloc_any();

          arm64_load_sp_reg();

          load32_reg_imm(reg1, ARM_REG_PIKE_SP, -2*(INT32)sizeof(struct svalue));
          load32_reg_imm(reg2, ARM_REG_PIKE_SP, -1*(INT32)sizeof(struct svalue));

          cond = arm64_ne_types(reg1, reg2, TYPE_SUBTYPE(PIKE_T_INT, NUMBER_NUMBER));
          b_imm_cond(label_dist(&slow), cond);

          load64_reg_imm(reg1, ARM_REG_PIKE_SP, -2*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));
          load64_reg_imm(reg2, ARM_REG_PIKE_SP, -1*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u));

          adds64_reg_reg(reg1, reg1, reg2);

	  b_imm_cond(label_dist(&slow), ARM_COND_VS);

	  store64_reg_imm(reg1, ARM_REG_PIKE_SP, -2*(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u)),

          ra_free(reg1);
          ra_free(reg2);

          b_imm(label_dist(&end));
          label_generate(&slow);
          ra_alloc(ARM_REG_R0);
          arm64_mov_int(ARM_REG_R0, 2);
          arm64_call(f_add);
          ra_free(ARM_REG_R0);
          label_generate(&end);
          arm64_sub64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, sizeof(struct svalue));
          arm64_store_sp_reg();

          return;
      }
  }

  arm64_call_c_opcode(opcode);


  if (opcode == F_CATCH) ra_free(ARM_REG_R0);

  if (flags & I_RETURN) {
      enum arm64_register kludge_reg = ARM_REG_NONE;

      if (opcode == F_RETURN_IF_TRUE) {
          kludge_reg = ra_alloc_persistent();
          /* 4 == 4*(JUMP_EPILOGUE_SIZE) */
          adr_imm(kludge_reg, 4);
      }

      arm64_ins_maybe_exit();

      if (opcode == F_RETURN_IF_TRUE) {
          arm64_rel_cond_jmp(ARM_REG_R0, kludge_reg, ARM_COND_EQ, 2);
          br_reg(ARM_REG_R0);
          ra_free(kludge_reg);
          return;
      }
  }
  if (flags & I_JUMP) {
    /* This is the code that JUMP_EPILOGUE_SIZE compensates for. */
    br_reg(ARM_REG_R0);

    compiler_state.flags &= ~FLAG_FP_LOADED;

    if (opcode == F_CATCH) {
        arm64_adr_imm_at(rel_addr, ARM_REG_R0, 4*(PIKE_PC - rel_addr));
    }
  }
}

void ins_f_byte(unsigned int opcode)
{
  record_opcode(opcode, 0);

  low_ins_f_byte(opcode);
}

void ins_f_byte_with_arg(unsigned int opcode, INT32 arg1)
{
  struct label done;
  label_init(&done);

  record_opcode(opcode, 0);

  switch (opcode) {
  case F_NUMBER:
      arm64_debug_instr_prologue_1(opcode, arg1);
      arm64_push_int(arg1, NUMBER_NUMBER);
      return;
  case F_NEG_NUMBER:
      arm64_debug_instr_prologue_1(opcode, arg1);
      arm64_push_int(-(INT64)arg1, 0);
      return;
  case F_SUBTRACT_INT:
    {
      struct label fallback;
      enum arm64_register tmp;
      arm64_debug_instr_prologue_1(opcode, arg1);

      tmp = ra_alloc_any();
      label_init(&fallback);
      arm64_load_sp_reg();
      load32_reg_imm(tmp, ARM_REG_PIKE_SP, -(INT32)sizeof(struct svalue)+0);
      cmp_reg_imm(tmp, 0, 0);
      b_imm_cond(label_dist(&fallback), ARM_COND_NE);
      load64_reg_imm(tmp, ARM_REG_PIKE_SP, -(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u.integer));
      arm64_subs64_reg_int(tmp, tmp, (INT64)arg1);
      b_imm_cond(label_dist(&fallback), ARM_COND_VS);
      store64_reg_imm(tmp, ARM_REG_PIKE_SP, -(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u.integer));
      ra_free(tmp);
      b_imm(label_dist(&done));
      label_generate(&fallback);
      break;
    }
  case F_ADD_INT:
    {
      struct label fallback;
      unsigned char imm, rot;
      enum arm64_register tmp;
      arm64_debug_instr_prologue_1(opcode, arg1);

      tmp = ra_alloc_any();
      label_init(&fallback);
      arm64_load_sp_reg();
      load32_reg_imm(tmp, ARM_REG_PIKE_SP, -(INT32)sizeof(struct svalue)+0);
      cmp_reg_imm(tmp, 0, 0);
      b_imm_cond(label_dist(&fallback), ARM_COND_NE);
      load64_reg_imm(tmp, ARM_REG_PIKE_SP, -(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u.integer));
      arm64_adds64_reg_int(tmp, tmp, (INT64)arg1);
      b_imm_cond(label_dist(&fallback), ARM_COND_VS);
      store64_reg_imm(tmp, ARM_REG_PIKE_SP, -(INT32)sizeof(struct svalue)+(INT32)OFFSETOF(svalue, u.integer));
      ra_free(tmp);
      b_imm(label_dist(&done));
      label_generate(&fallback);
      break;
    }
  case F_MARK_AND_CONST0:
      ins_f_byte(F_MARK);
      ins_f_byte(F_CONST0);
      return;
  case F_MARK_AND_CONST1:
      ins_f_byte(F_MARK);
      ins_f_byte(F_CONST1);
      return;
  case F_MARK_AND_STRING:
      ins_f_byte(F_MARK);
      ins_f_byte_with_arg(F_STRING, arg1);
      return;
  case F_MARK_AND_GLOBAL:
      ins_f_byte(F_MARK);
      ins_f_byte_with_arg(F_GLOBAL, arg1);
      return;
  case F_MARK_AND_LOCAL:
      ins_f_byte(F_MARK);
      ins_f_byte_with_arg(F_LOCAL, arg1);
      return;
  case F_LOCAL:
      arm64_debug_instr_prologue_1(opcode, arg1);
      arm64_load_locals_reg();
      arm64_push_svaluep_off(ARM_REG_PIKE_LOCALS, arg1);
      return;
  case F_MARK_AT:
      arm64_debug_instr_prologue_1(opcode, arg1);
      arm64_load_locals_reg();
      arm64_mark(ARM_REG_PIKE_LOCALS, arg1);
      return;
  case F_STRING:
      {
          enum arm64_register treg = ra_alloc_any(),
                              vreg = ra_alloc_any();

          arm64_load_fp_reg();

          load64_reg_imm(vreg, ARM_REG_PIKE_FP, OFFSETOF(pike_frame, context));
          load64_reg_imm(vreg, vreg, OFFSETOF(inherit, prog));
          load64_reg_imm(vreg, vreg, OFFSETOF(program, strings));
	  if (arg1 < 4096)
	      load64_reg_imm(vreg, vreg, arg1*sizeof(struct pike_string*));
	  else {
	      arm64_mov_int(treg, arg1*(sizeof(struct pike_string*)/sizeof(INT64)));
	      load64_reg_reg(vreg, vreg, treg, 1);
	  }

          arm64_mov_int(treg, TYPE_SUBTYPE(PIKE_T_STRING, 0));

          arm64_push_ptr_type(treg, vreg);

          ra_free(treg);
          ra_free(vreg);
          return;
      }
  case F_ASSIGN_LOCAL_AND_POP:
  case F_ASSIGN_LOCAL:
      {
          enum arm64_register tmp;
          arm64_debug_instr_prologue_1(opcode, arg1);
          arm64_load_locals_reg();

          tmp = ra_alloc_persistent();
          arm64_add64_reg_int(tmp, ARM_REG_PIKE_LOCALS, (INT64)(arg1 * sizeof(struct svalue)));

          arm64_free_svalue_off(tmp, 0, 0);

          arm64_load_sp_reg();
          arm64_sub64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, sizeof(struct svalue));

          if (opcode == F_ASSIGN_LOCAL_AND_POP) {
              arm64_store_sp_reg();
              arm64_move_svaluep_nofree(tmp, ARM_REG_PIKE_SP);
          } else {
              arm64_assign_svaluep_nofree(tmp, ARM_REG_PIKE_SP);
              arm64_add64_reg_int(ARM_REG_PIKE_SP, ARM_REG_PIKE_SP, sizeof(struct svalue));
          }

          ra_free(tmp);
          return;
      }
  case F_PROTECT_STACK:
      {
          arm64_load_fp_reg();
          arm64_load_locals_reg();

          if (arg1) {
              enum arm64_register reg = ra_alloc_any();

              arm64_add64_reg_int(reg, ARM_REG_PIKE_LOCALS, sizeof(struct svalue)*(INT64)arg1);
              store64_reg_imm(reg, ARM_REG_PIKE_FP, OFFSETOF(pike_frame, expendible));

              ra_free(reg);
          } else {
              store64_reg_imm(ARM_REG_PIKE_LOCALS, ARM_REG_PIKE_FP, OFFSETOF(pike_frame, expendible));
          }
          return;
      }
  }
  arm64_mov_int(ra_alloc(ARM_REG_R0), arg1);
  low_ins_f_byte(opcode);
  ra_free(ARM_REG_R0);
  label_generate(&done);
  return;
}

void ins_f_byte_with_2_args(unsigned int opcode, INT32 arg1, INT32 arg2)
{
  record_opcode(opcode, 0);

  switch (opcode) {
  case F_MARK_AND_EXTERNAL:
      ins_f_byte(F_MARK);
      ins_f_byte_with_2_args(F_EXTERNAL, arg1, arg2);
      return;
  case F_INIT_FRAME: {
          enum arm64_register tmp;
          arm64_debug_instr_prologue_2(opcode, arg1, arg2);
          arm64_load_fp_reg();

          tmp = ra_alloc_any();
          arm64_mov_int(tmp, arg2|(arg1<<16));

          assert(OFFSETOF(pike_frame, num_locals) % 4 == 0);
          assert(OFFSETOF(pike_frame, num_locals) + 2 == OFFSETOF(pike_frame, num_args));

          store32_reg_imm(tmp, ARM_REG_PIKE_FP, OFFSETOF(pike_frame, num_locals));
          ra_free(tmp);
          return;
      }
  case F_FILL_STACK:
      {
          enum arm64_register reg, treg, vreg;
          struct label skip, loop;
          label_init(&skip);
          label_init(&loop);

          arm64_load_sp_reg();
          arm64_load_locals_reg();

          reg = ra_alloc_any();
          treg = ra_alloc_any();
          vreg = ra_alloc_any();

          assert(treg < vreg);

          arm64_add64_reg_int(reg, ARM_REG_PIKE_LOCALS, (INT64)arg1*sizeof(struct svalue));

          cmp_reg_reg(reg, ARM_REG_PIKE_SP);
          /* jump if pike_sp >= reg */
          b_imm_cond(label_dist(&skip), ARM_COND_LS);
          arm64_mov_int(treg, TYPE_SUBTYPE(PIKE_T_INT, arg2 ? NUMBER_UNDEFINED : NUMBER_NUMBER));
          arm64_mov_int(vreg, 0);

          label_generate(&loop);
          store_multiple(ARM_REG_PIKE_SP, ARM_MULT_IAW, RBIT(treg)|RBIT(vreg));
          cmp_reg_reg(reg, ARM_REG_PIKE_SP);
          /* jump if pike_sp < reg */
          b_imm_cond(label_dist(&loop), ARM_COND_HI);

          arm64_store_sp_reg();
          label_generate(&skip);

          ra_free(reg);
          ra_free(treg);
          ra_free(vreg);

          return;
      }
  case F_2_LOCALS:
      ins_f_byte_with_arg(F_LOCAL, arg1);
      ins_f_byte_with_arg(F_LOCAL, arg2);
      return;
  }
  arm64_mov_int(ra_alloc(ARM_REG_R0), arg1);
  arm64_mov_int(ra_alloc(ARM_REG_R1), arg2);
  low_ins_f_byte(opcode);
  ra_free(ARM_REG_R0);
  ra_free(ARM_REG_R1);
  return;
}

int arm64_low_ins_f_jump(unsigned int opcode, int backward_jump) {
    INT32 ret;

    arm64_maybe_update_pc();

    arm64_call_c_opcode(opcode);

    /* Do we need to reload the stack pointer? */
    arm64_load_sp_reg();

    cmp_reg_imm(ARM_REG_R0, 0, 0);

    if (backward_jump) {
        struct label skip;
        label_init(&skip);

        b_imm_cond(label_dist(&skip), ARM_COND_EQ);

        ret = PIKE_PC;
        b_imm(0);

        label_generate(&skip);
    } else {
        ret = PIKE_PC;
        b_imm_cond(0, ARM_COND_NE);
    }

    return ret;
}

int arm64_ins_f_jump(unsigned int opcode, int backward_jump) {
    INT32 ret;

    if (!(instrs[opcode - F_OFFSET].flags & I_BRANCH))
        return -1;

    record_opcode(opcode, 0);

    switch (opcode) {
    case F_QUICK_BRANCH_WHEN_ZERO:
    case F_QUICK_BRANCH_WHEN_NON_ZERO:
        {
            enum arm64_register tmp = ra_alloc_any();
            arm64_change_sp(-1);
            load64_reg_imm(tmp, ARM_REG_PIKE_SP, 8);
            cmp_reg_imm(tmp, 0, 0);
            ret = PIKE_PC;
            if (opcode == F_QUICK_BRANCH_WHEN_ZERO)
                b_imm_cond(0, ARM_COND_Z);
            else
                b_imm_cond(0, ARM_COND_NZ);
            ra_free(tmp);
            return ret;
        }
    case F_BRANCH:
        ret = PIKE_PC;
        b_imm(0);
        return ret;

    case F_BRANCH_WHEN_NE:
    case F_BRANCH_WHEN_EQ:
    case F_BRANCH_WHEN_LT:
    case F_BRANCH_WHEN_LE:
    case F_BRANCH_WHEN_GT:
    case F_BRANCH_WHEN_GE:
        {
            ra_alloc(ARM_REG_R0);
            ra_alloc(ARM_REG_R1);

            arm64_load_sp_reg();

            arm64_sub64_reg_int(ARM_REG_R0, ARM_REG_PIKE_SP, 2*sizeof(struct svalue));
            arm64_sub64_reg_int(ARM_REG_R1, ARM_REG_PIKE_SP, 1*sizeof(struct svalue));

            switch (opcode) {
            case F_BRANCH_WHEN_NE:
                arm64_call(is_eq);
                cmp_reg_imm(ARM_REG_R0, 0, 0);
                break;
            case F_BRANCH_WHEN_EQ:
                arm64_call(is_eq);
                cmp_reg_imm(ARM_REG_R0, 1, 0);
                break;
            case F_BRANCH_WHEN_LT:
                arm64_call(is_lt);
                cmp_reg_imm(ARM_REG_R0, 1, 0);
                break;
            case F_BRANCH_WHEN_LE:
                arm64_call(is_le);
                cmp_reg_imm(ARM_REG_R0, 1, 0);
                break;
            case F_BRANCH_WHEN_GT:
                arm64_call(is_le);
                cmp_reg_imm(ARM_REG_R0, 0, 0);
                break;
            case F_BRANCH_WHEN_GE:
                arm64_call(is_lt);
                cmp_reg_imm(ARM_REG_R0, 0, 0);
                break;
            }

            ret = PIKE_PC;
            b_imm_cond(0, ARM_COND_EQ);

            ra_free(ARM_REG_R0);
            ra_free(ARM_REG_R1);
            return ret;
        }
    }

    return arm64_low_ins_f_jump(opcode, backward_jump);
}

int arm64_ins_f_jump_with_arg(unsigned int opcode, INT32 arg1, int backward_jump) {
    int instr;

    if (!(instrs[opcode - F_OFFSET].flags & I_BRANCH))
        return -1;

    record_opcode(opcode, 0);

    arm64_mov_int(ra_alloc(ARM_REG_R0), (INT64)arg1);

    instr = arm64_low_ins_f_jump(opcode, backward_jump);

    ra_free(ARM_REG_R0);

    return instr;
}

int arm64_ins_f_jump_with_2_args(unsigned int opcode, INT32 arg1, INT32 arg2, int backward_jump) {
    int instr;

    if (!(instrs[opcode - F_OFFSET].flags & I_BRANCH))
        return -1;

    record_opcode(opcode, 0);

    arm64_mov_int(ra_alloc(ARM_REG_R0), (INT64)arg1);
    arm64_mov_int(ra_alloc(ARM_REG_R1), (INT64)arg2);

    instr = arm64_low_ins_f_jump(opcode, backward_jump);

    ra_free(ARM_REG_R0);
    ra_free(ARM_REG_R1);

    return instr;
}

void arm64_update_f_jump(INT32 offset, INT32 to_offset) {
    PIKE_OPCODE_T instr = read_pointer(offset);

    to_offset -= offset;
    if (!(instr & 0x60000000)) {
      /* Unconditional branch: 26 bit offset */
      assert((instr & 0x7c000000) == ARM_INSTR_UNCOND_BRANCH_IMM);
      instr &= ~0x03ffffff;
      assert (to_offset >= -0x02000000 && to_offset < 0x02000000);
      instr |= to_offset & 0x03ffffff;
    } else {
      /* Conditional branch: 19 bit offset */
      assert((instr & 0xff000010) == ARM_INSTR_COND_BRANCH_IMM);
      instr &= ~0x00ffffe0;
      assert (to_offset >= -0x00040000 && to_offset < 0x00040000);
      instr |= (to_offset & 0x0007ffff) << 5;
    }

    upd_pointer(offset, instr);
}

int arm64_read_f_jump(INT32 offset) {
    PIKE_OPCODE_T instr = read_pointer(offset);

    if (!(instr & 0x60000000)) {
      /* Unconditional branch: 26 bit offset */
      assert((instr & 0x7c000000) == ARM_INSTR_UNCOND_BRANCH_IMM);
      return (((INT32)(instr << 6)) >> 6) + offset;
    } else {
      /* Conditional branch: 19 bit offset */
      assert((instr & 0xff000010) == ARM_INSTR_COND_BRANCH_IMM);
      return (((INT32)(instr << 8)) >> 13) + offset;
    }
}

#ifdef PIKE_DEBUG

static const char *regname(PIKE_OPCODE_T reg, int sf, int sp)
{
    static const char * const regname_str[2][33] = {
      {"w0","w1","w2","w3","w4","w5","w6","w7",
       "w8","w9","w10","w11","w12","w13","w14","w15",
       "w16","w19","w18","w19","w20","w21","w22","w23",
       "w24","w25","w26","w27","w28","w29","w30","wzr","wsp"},
      {"x0","x1","x2","x3","x4","x5","x6","x7",
       "x8","x9","x10","x11","x12","x13","x14","x15",
       "x16","x19","x18","x19","x20","x21","x22","x23",
       "x24","x25","x26","x27","x28","x29","x30","xzr","sp"}
    };
    unsigned r = (reg & 31);
    if (r == 31 && sp)
        r++;
    return regname_str[sf][r];
}

static const char *extendname(PIKE_OPCODE_T mode, int sf)
{
    static const char * const extendname_str[8] = {
      "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx"
    };
    unsigned m = (mode & 7);
    unsigned lsl = (sf? 3 : 2);
    return (m == lsl? "lsl" : extendname_str[m]);
}

static int decode_bit_masks(unsigned char n, unsigned char imms, unsigned char immr,
			    int immediate, unsigned long *wmask, unsigned long *tmask)
{
    unsigned long welem, telem;
    unsigned char d, esize = 1<<(31-clz32((n<<6)|(imms^0x3f)));
    if (esize < 2)
        return 0;
    if (immediate && (imms&(esize-1)) == esize-1)
        return 0;
    imms &= esize-1;
    immr &= esize-1;
    d = (imms - immr)&(esize-1);
    welem = (1UL<<(imms+1))-1;
    telem = (1UL<<(d+1))-1;
    if (immr) {
        welem = (welem >> immr) | (welem << (esize-immr));
	if (esize < 64)
	    welem &= (1UL<<esize)-1;
    }
    while (esize < 64) {
        welem |= welem << esize;
	telem |= telem << esize;
	esize <<= 1;
    }
    if (wmask)
        *wmask = welem;
    if (tmask)
        *tmask = telem;
    return 1;
}

void arm64_disassemble_code(PIKE_OPCODE_T *addr, size_t bytes) {
    size_t i;
    size_t opcodes = bytes / sizeof(PIKE_OPCODE_T);
    static const char * const condname[16] = {
      "eq","ne","cs","cc","mi","pl","vs","vc","hi","ls","ge","lt","gt","le","al","nv"
    };
    static const char * const shiftname[4] = {"lsl", "lsr", "asr", "ror"};
    static const char * const logname[8] = {
      "and", "bic", "orr", "orn", "eor", "eon", "ands", "bics"
    };

    for (i = 0; i < opcodes; i++) {
        PIKE_OPCODE_T instr = addr[i];
        fprintf(stderr, "  %p\t", addr+i);

	if (((instr >> 26) & 7) == 4) {

	    /* Data processing - immediate */

	    int sf = (instr >> 31) & 1;
	    switch ((instr >> 24) & 3) {
	    case 0:
	        /* PC-rel. addressing */
	        fprintf(stderr, "%s\t%s,%p\n", (((instr>>31)&1)==0? "adr":"adrp"),
			regname(instr, 1, 0), ((char *)(addr+i))+
			(((unsigned long)(((instr>>3)&0xffffff)|((instr>>29)&3)))
			 <<(((instr>>31)&1)? 12:0)));
		continue;
	    case 1:
	        /* Add/subtract (immediate) */
	        if (((instr>>23)&1) == 0) {
		    fprintf(stderr, "%s%s\t%s,%s,#%d",
			    (((instr>>30)&1)==0? "add":"sub"),
			    (((instr>>29)&1)==0?"":"s"),
			    regname(instr, sf, !((instr>29)&1)),
			    regname(instr>>5, sf, 1),
			    (int)((instr>>10)&0xfff));
		    if (((instr>>22)&1) == 1) {
		        fprintf(stderr, ",lsl #12");
		    }
		    fprintf(stderr, "\n");
		    continue;
		}
		break;
	    case 2:
	        if (((instr>>23) & 1) == 0) {
		    /* Logical (immediate) */
		    unsigned char n = (instr>>22)&1;
		    unsigned char imms = (instr>>10)&63;
		    unsigned char immr = (instr>>16)&63;
		    unsigned char opc = (instr>>29)&3;
		    unsigned long imm;
		    if (!decode_bit_masks(n, imms, immr, 1, &imm, NULL))
		        break;
		    if (sf || !n) {
		        if (opc == 3 && ((instr>>5)&31) == 31) {
			    fprintf(stderr, "tst\t%s", regname(instr, sf, 0));
		        } else if (opc == 1 && ((instr>>5)&31) == 31 &&
			    !arm64_move_wide_preferred(n, immr, imms, sf)) {
			    fprintf(stderr, "mov\t%s", regname(instr, sf, 1));
			} else {
			    fprintf(stderr, "%s\t%s,%s", logname[opc<<1],
				    regname(instr, sf, opc != 3),
				    regname(instr>>5, sf, 0));
			}
		    }
		    if (!sf)
		        imm = (unsigned INT32)imm;
		    fprintf(stderr, ",#0x%lx\n", imm);
		    continue;
		} else {
		    /* Move wide (immediate) */
		    unsigned char opc = (instr>>29)&3;
		    unsigned char hw = (instr>>21)&3;
		    static const char * const movname[4] = {"movn", "", "movz", "movk"};
		    if (opc == 1 || (hw > 1 && !sf))
		        break;
		    if (opc != 3 && !(((instr>>5)&0xffff)==0 && hw!=0) &&
			(opc != 0 || sf || ((instr>>5)&0xffff)!=0xffff)) {
		        unsigned long imm = ((unsigned long)((instr>>5)&0xffff))<<(hw<<4);
			if (opc == 0)
			    imm = ~imm;
			if (!sf)
			    imm = (unsigned INT32)imm;
		        fprintf(stderr, "mov\t%s,#0x%lx\n", regname(instr, sf, 0), imm);
		        continue;
		    }
		    fprintf(stderr, "%s\t%s,#0x%x", movname[opc], regname(instr, sf, 0),
			    (unsigned)((instr>>5)&0xffff));
		    if (hw)
		        fprintf(stderr, ",lsl #%u", (unsigned)(hw<<4));
		    fprintf(stderr, "\n");
		    continue;
		}
		break;
	    case 3:
	        if (((instr>>23)&1) == 0) {
		    if (((instr>>29)&3) < 3 && ((int)((instr>>22)&1)) == sf) {
		        /* Bitfield */
		        static const char * const bfname[3] = {"sbfm","bfm","ubfm"};
		        fprintf(stderr, "%s\t%s,%s,#%d,#%d\n", bfname[(instr>>29)&3],
				regname(instr, sf, 0), regname(instr>>5, sf, 0),
				(int)((instr>>16)&63), (int)((instr>>10)&63));
			continue;
		    }
		} else {
		    if (((instr>>29)&3) == 0 && ((int)((instr>>22)&1)) == sf &&
			((instr>>21)&1) == 0 && (sf || ((instr>>15)&1)==0)) {
		        /* Extract */
		        if (((instr>>5)&31) == ((instr>>16)&31))
			    fprintf(stderr, "ror\t%s,%s", regname(instr, sf, 0),
				    regname(instr>>5, sf, 0));
			else
			    fprintf(stderr, "extr\t%s,%s,%s", regname(instr, sf, 0),
				    regname(instr>>5, sf, 0), regname(instr>>16, sf, 0));
		        fprintf(stderr, ",#%d\n", (int)((instr>>10)&63));
			continue;
		    }
		}
		break;
	    }

	} else if (((instr >> 26) & 7) == 5) {

	    /* Branch, exception generation and system instructions */

	    if (((instr >> 29) & 3) == 0) {
	        /* Unconditional branch (immediate) */
	        fprintf(stderr, "%s\t%p\n", (((instr>>31)&1)? "bl":"b"),
			addr+i+(((INT32)(instr<<6))>>6));
		continue;
	    } else if (((instr >> 29) & 3) == 1) {
	        if (((instr >> 25) & 1) == 0) {
		    /* Compare & branch (immediate) */
		    fprintf(stderr, "%s\t%s,%p\n",
			    (((instr>>24)&1)==0? "cbz":"cbnz"),
			    regname(instr, ((instr>>31)&1), 0),
			    addr+i+(((INT32)(instr<<8))>>13));
		    continue;
		} else {
		    /* Test & branch (immediate) */
		    fprintf(stderr, "%s\t%s,#%d,%p\n",
			    (((instr>>24)&1)==0? "tbz":"tbnz"),
			    regname(instr, ((instr>>31)&1), 0),
			    (int)(((instr>>26)&32)|((instr>>19)&31)),
			    addr+i+(((INT32)(instr<<13))>>18));
		    continue;
		}
	    } else if (((instr >> 29) & 3) == 2) {
	        if (((instr >> 24) & 0xff) == 0x54 && ((instr>>4)&1) == 0) {
		    /* Conditional branch (immediate) */
		    fprintf(stderr, "b.%s\t%p\n", condname[(instr&15)],
			  addr+i+(((INT32)(instr<<8))>>13));
		    continue;
		} else if (((instr >> 25) & 0x7f) == 0x6b &&
			   ((instr >> 10) & 0x7ff) == 0x7c0 &&
			   (instr & 31) == 0) {
		    /* Unconditional branch (register) */
		    unsigned opcode = (instr >> 21) & 15;
		    if (opcode < 3) {
		        static const char * const brname[3] = {"br","blr","ret"};
			fprintf(stderr, "%s", brname[opcode]);
			if (opcode < 2 || ((instr>>5)&31) != 30)
			    fprintf(stderr, "\t%s", regname(instr>>5, 1, 0));
			fprintf(stderr, "\n");
			continue;
		    } else if ((opcode & ~1) == 4 && ((instr >> 5)&31) == 31) {
		        fprintf(stderr, "%s\n", ((opcode&1)? "drps":"eret"));
		        continue;
		    }
		} else if (((instr >> 24) & 0xff) == 0xd4) {
		    /* Exception generation */
		    if (((instr>>2)&7) == 0 &&
			( ((instr&3)==0 && (((instr>>21)&7)==1 || ((instr>>21)&7)==2)) ||
			  ((instr&3)!=0 && (((instr>>21)&7)==0 || ((instr>>21)&7)==5)) )) {
		        static const char * const excname[8] = {
			  "svc","hvc","smc","brk","hlt","dcps1","dcps2","dcps3"
			};
			int opc = (instr>>21)&7;
		        fprintf(stderr, "%s", excname[opc+((instr&3)? (instr&3)-1:2)]);
			if (opc != 5 || ((instr>>5)&0xffff)!=0)
			    fprintf(stderr,"\t#0x%x", (unsigned)((instr>>5)&0xffff));
			fprintf(stderr, "\n");
		        continue;
		    }
		} else if (((instr >> 22) & 0x3ff) == 0x354) {
		    /* System */
		    char sregname[16];
		    sprintf(sregname, "s%d_%d_c%d_c%d_%d", (int)((instr>>19)&3),
			    (int)((instr>>16)&7), (int)((instr>>12)&15),
			    (int)((instr>>8)&15), (int)((instr>>5)&7));
		    if (((instr >> 21)&1) == 0)
		        fprintf(stderr, "msr\t%s,%s\n", sregname, regname(instr, 1, 0));
		    else
		        fprintf(stderr, "mrs\t%s,%s\n", regname(instr, 1, 0), sregname);
		    continue;
		}
	    }

	} else if (((instr >> 25) & 5) == 4) {

	    /* Loads and stores */

	    if (((instr>>28) & 3) == 0) {
	        if (((instr >> 24) & 7) == 0) {
		    /* Load/store exclusive */
		    if (((instr>>15)&0x101) != 0x100 &&
			(((instr>>21)&1)==0 ||
			 ( ((instr>>31)&1)==1 && ((instr>>23)&1)==0 )) &&
			(((instr>>21)&1)==1 || ((instr>>10)&31)==31) &&
			(((instr>>22)&3)==0 || ((instr>>16)&31)==31)) {
		        int sf = (((instr>>30)&3) == 3);
		        fprintf(stderr, "%s%s%s%s%s\t",
				(((instr>>22)&1)? "ld":"st"),
				(((instr>>15)&1)? (((instr>>22)&1)? "a":"l") : ""),
				(((instr>>23)&1)? "":"x"),
				(((instr>>21)&1)? "p":"r"),
				(((instr>>31)&1)? "": (((instr>>30)&1)? "h":"b")));
			if (((instr>>22)&3)==0)
			    fprintf(stderr, "%s,", regname(instr>>16, 0, 0));
			fprintf(stderr, "%s,", regname(instr, sf, 0));
			if (((instr>>21)&1)==1)
			    fprintf(stderr, "%s,", regname(instr>>10, sf, 0));
			fprintf(stderr, "[%s]\n", regname(instr>>5, 1, 1));
			continue;
		    }
		} else {
		    /* AdvSIMD load/store */
		    /* NYI */
		}
	    } else if (((instr>>28) & 3) == 1) {
	        if (((instr>>24) & 1) == 0) {
		    /* Load register (literal) */
		    if (((instr>>26) & 1) == 1) {
		        /* SIMD & FP */
		        /* NYI */
		    } else {
		        if (((instr>>30) & 3) == 3) {
			    fprintf(stderr, "prfm\t#%d", (int)(instr&31));
			} else {
			    fprintf(stderr, "ldr%s\t%s", (((instr>>30)&3)==2? "sw":""),
				    regname(instr, (((instr>>30)&3)!=0), 0));
			}
		        fprintf(stderr, ",%p\n", addr+i+(((INT32)(instr<<8))>>13));
			continue;
		    }
		}
	    } else if (((instr>>28) & 3) == 2) {
	        /* Load/store register pair */
	        if (((instr>>26) & 1) == 1) {
		    /* SIMD & FP */
		    /* NYI */
		} else if (((instr>>30)&3) != 3 &&
			   (((instr>>30)&3) != 1 ||
			    (((instr>>22)&1) == 1 &&
			     ((instr>>23)&3) != 0))) {
		    int imm = ((int)(INT32)(instr<<10))>>25;
		    if (((instr>>30)&3) == 2)
		        imm <<= 3;
		    else
		        imm <<= 2;
		    fprintf(stderr, "%s%sp%s\t%s,%s,[%s",
			    (((instr>>22)&1)? "ld":"st"),
			    (((instr>>23)&3)? "":"n"),
			    (((instr>>30)&3)==1? "sw":""),
			    regname(instr, ((instr>>30)&3)>0, 0),
			    regname(instr>>10, ((instr>>30)&3)>0, 0),
			    regname(instr>>5, 1, 1));
		    if (((instr>>23)&3) == 1)
		        fprintf(stderr, "],#%d\n", imm);
		    else if (((instr>>23)&3) == 3)
		        fprintf(stderr, ",#%d]!\n", imm);
		    else if (imm)
		        fprintf(stderr, ",#%d]\n", imm);
		    else
		        fprintf(stderr, "]\n");
		    continue;
		}
	    } else {
	        if (((instr>>24) & 1) == 1 ||
		    ((instr>>21) & 1) == 0 ||
		    ((instr>>10) & 3) == 2) {
		    /* Load/store register */
		    if (((instr>>26)&1) == 1) {
		        /* SIMD & FP */
		        /* NYI */
		    } else {
		        if (((instr>>31)&1) == 0 ||
			    ((instr>>23)&1) == 0 ||
			    (((instr>>22)&3) == 2 &&
			     (((instr>>30)&1) == 0 || ((instr>>24)&1) == 1 ||
			      ((instr>>21)&1) == 1 || ((instr>>10)&3) == 0))) {
			    const char *mod="";
			    if (((instr>>24) & 1) == 0 && ((instr>>21) & 1) == 0 &&
				((instr>>10) & 1) == 0)
			        mod = (((instr>>11) & 1) == 0? "u":"t");
			    if (((instr>>22)&0x303) == 0x302) {
			        fprintf(stderr, "prf%sm\t#%d,", mod, (int)(instr&31));
			    } else {
			        fprintf(stderr, "%s%sr%s%s\t%s,",
					(((instr>>22)&3)==0? "st":"ld"), mod,
					(((instr>>23)&1)==0? "":"s"),
					(((instr>>30)&3)==0? "b":
					 (((instr>>30)&3)==1? "h":
					  (((instr>>23)&1)==1? "w":""))),
					regname(instr, ((instr>>30)&3)==3 || ((instr>>22)&3)==2, 0));
			    }
			    fprintf(stderr, "[%s", regname(instr>>5, 1, 1));
			    if (((instr>>24) & 1) == 1 ||
				((instr>>21) & 1) == 0) {
			        int imm;
				if (((instr>>24) & 1) == 1) {
				    imm = ((instr >> 10) & 0xfff) << ((instr >> 30)&3);
				} else {
				    imm = ((int)(INT32)(instr << 11)) >> 23;
				}
				if (((instr>>24) & 1) == 1 || ((instr>>10)&1) == 0) {
				    if (imm)
				        fprintf(stderr, ",#%d", imm);
				    fprintf(stderr, "]\n");
				} else if (((instr>>11)&1) == 0) {
				    fprintf(stderr, "],#%d\n", imm);
				} else {
				    fprintf(stderr, ",#%d]!\n", imm);
				}
			    } else {
			        fprintf(stderr, ",%s", regname(instr>>16, ((instr>>13)&3) == 3, 0));
				if (((instr >> 12) & 1) == 1 || ((instr>>13)&7) != 3) {
				    fprintf(stderr, ",%s", extendname(instr>>13, 1));
				    if(((instr >> 12) & 1) == 1 || ((instr>>13)&7) == 3) {
				        fprintf(stderr, " #%d", (((instr >> 12) & 1) == 1?
								 (int)((instr>>30)&3) : 0));
				    }
				}
				fprintf(stderr, "]\n");
			    }
			    continue;
			}
		    }
		}
	    }

	} else if (((instr >> 25) & 7) == 5) {

	    /* Data processing - register */

	    int sf = (instr >> 31) & 1;
	    if (((instr >> 24) & 1) == 1) {
	        if (((instr >> 28) & 1) == 1) {
		    /* Data processing (3 source) */
		    if (((instr >> 29) & 3) == 0)
		        switch ((instr >> 21) & 7) {
			case 0:
			    fprintf(stderr, "m%s\t%s,%s,%s,%s\n",
				    (((instr>>15)&1)==0? "add":"sub"),
				    regname(instr, sf, 0), regname(instr>>5, sf, 0),
				    regname(instr>>16, sf, 0), regname(instr>>10, sf, 0));
			    continue;
			case 1:
			case 5:
			    if (sf) {
			        fprintf(stderr, "%sm%sl\t%s,%s,%s,%s\n",
					(((instr>>23)&1)==0? "s":"u"),
					(((instr>>15)&1)==0? "add":"sub"),
					regname(instr, 1, 0), regname(instr>>5, 0, 0),
					regname(instr>>16, 0, 0), regname(instr>>10, 1, 0));
				continue;
			    }
			    break;
			case 2:
			case 6:
			    if (sf && ((instr>>15)&1) == 0) {
			        fprintf(stderr, "%smulh\t%s,%s,%s\n",
					(((instr>>23)&1)==0? "s":"u"),
					regname(instr, 1, 0), regname(instr>>5, 1, 0),
					regname(instr>>16, 1, 0));
				continue;
			    }
			}
	        } else {
		    if ((((instr>>21)&1) == 0 && ((instr>>22)&3)!=3) ||
			(((instr>>22)&3) == 0 && ((instr>>10)&7)<=4)) {
		        /* Add/subtract (shifted/extended register) */
		        fprintf(stderr, "%s%s\t",
				(((instr>>30)&1)==0? "add":"sub"),
				(((instr>>29)&1)==0?"":"s"));
			if (((instr>>21)&1) == 1) {
			    fprintf(stderr, "%s,%s,%s",
				    regname(instr, sf, 1), regname(instr>>5, sf, 1),
				    regname(instr>>16, sf&&((instr>>13)&3)==3, 0));
			    if (((instr>>10)&7) != 0 || ((instr>>13)&7) != (unsigned)2+sf) {
			        fprintf(stderr, ",%s", extendname(instr>>13, sf));
				if (((instr>>10)&7) != 0 || ((instr>>13)&7) == (unsigned)2+sf) {
				    fprintf(stderr, " #%d", (int)((instr>>10)&7));
				}
			    }
			} else {
			    fprintf(stderr, "%s,%s,%s",
				    regname(instr, sf, 0), regname(instr>>5, sf, 0),
				    regname(instr>>16, sf, 0));
			    if (((instr>>10)&63) != 0) {
			        fprintf(stderr, ",%s #%d",
					shiftname[(instr>>22)&3], (int)((instr>>10)&63));
			    }
			}
			fprintf(stderr, "\n");
			continue;
		    }
		}
	    } else if (((instr >> 28) & 1) == 0) {
	        /* Logical (shifted register) */
	        if (((instr>>29)&3) == 1 && ((instr>>5)&31)==31 &&
		    (((instr>>21)&1) == 1 || !((instr>>10)&0x303f)))
		    fprintf(stderr, "%s\t%s,%s",
			    (((instr>>21)&1)? "mvn":"mov"),
			    regname(instr, sf, 0), regname(instr>>16, sf, 0));
		else
		    fprintf(stderr, "%s\t%s,%s,%s",
			    logname[((instr>>28)&6)|((instr>>21)&1)],
			    regname(instr, sf, 0), regname(instr>>5, sf, 0),
			    regname(instr>>16, sf, 0));
		if (((instr>>10)&63) != 0) {
		  fprintf(stderr, ",%s #%d",
			  shiftname[(instr>>22)&3], (int)((instr>>10)&63));
		}
		fprintf(stderr, "\n");
	        continue;
	    } else switch ((instr >> 21) & 7) {
	    case 0:
	        if (((instr >> 10) & 63) == 0) {
		    /* Add/subtract with carry */
		    fprintf(stderr, "%s%s\t%s,%s,%s\n",
			    (((instr>>30)&1)==0? "adc":"sbc"),
			    (((instr>>29)&1)==0?"":"s"),
			    regname(instr, sf, 0), regname(instr>>5, sf, 0),
			    regname(instr>>16, sf, 0));
		    continue;
		}
	        break;
	    case 2:
	        if (((instr>>29)&1) == 1 && ((instr>>10)&1) == 0 &&
		    ((instr>>4)&1) == 0) {
		    /* Conditional compare */
		    fprintf(stderr, "%s\t%s",
			    (((instr>>30)&1)==0? "ccmn":"ccmp"),
			    regname(instr>>5, sf, 0));
		    if (((instr>>11)&1) == 1) {
		        fprintf(stderr, ",#%d", (int)((instr>>16)&15));
		    } else {
		        fprintf(stderr, ",%s", regname(instr>>16, sf, 0));
		    }
		    fprintf(stderr, ",#%d,%s\n", (int)(instr&15), condname[(instr>>12)&15]);
		    continue;
		}
	        break;
	    case 4:
	        if (((instr>>29)&1) == 0 && ((instr>>11)&1) == 0) {
		    /* Conditional select */
		    static const char * const cselname[4] = {"csel", "csinc", "csinv", "csneg"};
		    fprintf(stderr, "%s\t%s,%s,%s,%s\n",
			    cselname[((instr>>29)&2)|((instr>>10)&1)],
			    regname(instr, sf, 0), regname(instr>>5, sf, 0),
			    regname(instr>>16, sf, 0), condname[(instr>>12)&15]);
		    continue;
		}
		break;
	    case 6:
	        if (((instr>>29)&1) == 0) {
		    if (((instr>>30)&1) == 1) {
		        /* Data-processing (1 source) */
		        unsigned opcode = (instr>>10)&0x7ff;
			if (opcode < 6 && (sf || opcode != 3)) {
			    static const char * const dpro1name[6] = {
			      "rbit","rev16","rev32","rev","clz","cls"
			    };
			    if (opcode == 2 && !sf)
			        opcode++;
			    fprintf(stderr, "%s\t%s,%s\n", dpro1name[opcode],
				    regname(instr, sf, 0), regname(instr>>5, sf, 0));
			    continue;
			}
		    } else {
		        /* Data-processing (2 source) */
		        unsigned opcode = (instr>>10)&63;
			if (opcode < 24 &&
			    ((sf? 0x880fcU : 0x770f0cU) & (1U<<opcode)) != 0) {
			    static const char * const dpro2name[14] = {
			      "crc32b","crc32h","crc32w","crc32x","crc32cb","crc32ch","crc32cw","crc32cx",
			      "lslv","lsrv","asrv","rorv","udiv","sdiv"
			    };
			    if (opcode < 8)
			        opcode += 10;
			    fprintf(stderr, "%s\t%s,%s,%s\n", dpro2name[opcode&15],
				    regname(instr, sf && opcode < 16, 0),
				    regname(instr>>5, sf && opcode < 16, 0),
				    regname(instr>>16, sf, 0));
			    continue;
			}
		    }
		}
		break;
	    }

	} else if (((instr >> 25) & 7) == 7) {

	    /* Data processing - SIMD and floating point */

	    /* NYI */

	}

        fprintf(stderr, "%x\n", instr);
    }
}

#endif
