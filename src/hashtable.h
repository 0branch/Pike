/*
|| This file is part of Pike. For copyright information see COPYRIGHT.
|| Pike is distributed under GPL, LGPL and MPL. See the file COPYING
|| for more information.
*/

#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "global.h"

#define AVERAGE_HASH_LENGTH 16
#define NEW_HASHTABLE_SIZE 4

#ifndef STRUCT_HASH_ENTRY_DECLARED
#define STRUCT_HASH_ENTRY_DECLARED
#endif
struct hash_entry
{
  struct hash_entry *next;
  struct pike_string *s;
};

#ifndef STRUCT_HASH_TABLE_DECLARED
#define STRUCT_HASH_TABLE_DECLARED
#endif
struct hash_table
{
  INT32 mask;
  INT32 entries;
  struct hash_entry *htable[1];
};

/* Remap some of the symbols that might clash with other libraries.
 *
 * NB: hash_insert is known to clash with mariadb-connector-c 2.2.
 */
#define hash_lookup	pike_hash_lookup
#define hash_insert	pike_hash_insert
#define hash_unlink	pike_hash_unlink

/* Prototypes begin here */
struct hash_entry *hash_lookup(const struct hash_table *h,
                               const struct pike_string *s);
struct hash_table *hash_insert(struct hash_table *h, struct hash_entry *s);
struct hash_table *hash_unlink(struct hash_table *h, struct hash_entry *s);
void free_hashtable(struct hash_table *h,
		    void (*free_entry)(struct hash_entry *));
/* Prototypes end here */

#endif
