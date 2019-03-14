/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#ifndef STUFF_H
#define STUFF_H

#include "global.h"

/* Prototypes begin here */
PMOD_EXPORT int my_log2(size_t x) ATTRIBUTE((const));
PMOD_EXPORT int count_bits(unsigned INT32 x) ATTRIBUTE((const));
PMOD_EXPORT double my_strtod(char *nptr, char **endptr);
PMOD_EXPORT unsigned INT32 my_sqrt(unsigned INT32 n) ATTRIBUTE((const));
/* unsigned long find_good_hash_size(unsigned long x) ATTRIBUTE((const)); */
unsigned INT32 find_next_power(unsigned INT32 x) ATTRIBUTE((const));
/* Prototypes end here */

extern const INT32 hashprimes[32];

#endif
