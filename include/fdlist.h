#ifndef _IRCD_DOG3_FDLIST
#define _IRCD_DOG3_FDLIST

typedef struct fdstruct {
  unsigned char entry [MAXCONNECTIONS+2];
} fdlist;

void addto_fdlist( int a, fdlist *b);
void delfrom_fdlist( int a, fdlist *b);
void init_fdlist(fdlist *b);

#endif /* _IRCD_DOG3_FDLIST */
