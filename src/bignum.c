#include "global.h"

#ifdef AUTO_BIGNUM

#include "interpret.h"
#include "program.h"
#include "object.h"
#include "svalue.h"
#include "error.h"


struct svalue auto_bignum_program = { T_INT };

static void resolve_auto_bignum_program(void)
{
  if(auto_bignum_program.type == T_INT)
  {
    push_text("Gmp.mpz");
    SAFE_APPLY_MASTER("resolv", 1);
    
    if(sp[-1].type != T_FUNCTION)
      error("Failed to resolv Gmp.mpz!\n");
    
    auto_bignum_program=sp[-1];
    sp--;
  }
}

void exit_auto_bignum(void)
{
  free_svalue(&auto_bignum_program);
  auto_bignum_program.type=T_INT;
}

void convert_stack_top_to_bignum(void)
{
  resolve_auto_bignum_program();
  apply_svalue(&auto_bignum_program, 1);

  if(sp[-1].type != T_OBJECT)
    error("Gmp.mpz conversion failed.\n");
}


struct object *make_bignum_object(void)
{
  convert_stack_top_to_bignum();
  return  (--sp)->u.object;
}

struct object *bignum_from_svalue(struct svalue *s)
{
  push_svalue(s);
  convert_stack_top_to_bignum();
  return  (--sp)->u.object;
}

void convert_svalue_to_bignum(struct svalue *s)
{
  push_svalue(s);
  convert_stack_top_to_bignum();
  free_svalue(s);
  *s=sp[-1];
  sp--;
}

#endif /* AUTO_BIGNUM */
