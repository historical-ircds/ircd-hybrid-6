/*
 * fdlist.h
 *
 * $Id$
 */
#ifndef _IRCD_DOG3_FDLIST
#define _IRCD_DOG3_FDLIST
#ifndef INCLUDED_config_h
#include "config.h"       /* MAXCONNECTIONS */
#endif

struct FDList {
  unsigned char entry[MAXCONNECTIONS + 2];
};

extern struct FDList serv_fdlist;
extern struct FDList busycli_fdlist;
extern struct FDList default_fdlist;
extern struct FDList oper_fdlist;

typedef struct FDList fdlist;

void addto_fdlist(int a, struct FDList* b);
void delfrom_fdlist( int a, struct FDList* b);
void init_fdlist(struct FDList* b);

#endif /* _IRCD_DOG3_FDLIST */

