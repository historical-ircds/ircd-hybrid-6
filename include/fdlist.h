/*
 * fdlist.h
 *
 * $Id$
 */
#ifndef _IRCD_DOG3_FDLIST
#define _IRCD_DOG3_FDLIST

struct FDList {
  unsigned char entry [MAXCONNECTIONS+2];
};

typedef struct FDList fdlist;

void addto_fdlist(int a, fdlist *b);
void delfrom_fdlist( int a, fdlist *b);
void init_fdlist(fdlist *b);

#endif /* _IRCD_DOG3_FDLIST */
