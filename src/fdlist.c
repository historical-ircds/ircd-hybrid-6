/* 
 *
 * fdlist.c   maintain lists of certain important fds 
 *
 *
 * $Id$
 */
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "h.h"
#include "config.h"
#include "fdlist.h"


void addto_fdlist(int fd,fdlist *listp)
{
  if ( (unsigned int)fd > MAXCONNECTIONS )
    return;
  else
    listp->entry[fd] = 1;
}

void delfrom_fdlist(int fd,fdlist *listp)
{
  if ( (unsigned int)fd > MAXCONNECTIONS )
    return;
  else
    listp->entry[fd] = 0;
}

void init_fdlist(fdlist *listp)
{
  (void)memset((void *)listp,0,sizeof(fdlist));
}
  
