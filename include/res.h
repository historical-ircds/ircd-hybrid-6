/*
 * irc2.7.2/ircd/res.h (C)opyright 1992 Darren Reed.
 *
 * $Id$
 */
#ifndef	INCLUDED_res_h
#define INCLUDED_res_h

#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* time_t */
#define INCLUDED_sys_types_h
#endif

#ifndef INCLUDED_struct_h
#include "struct.h"
#endif

extern struct hostent* get_res(char *);
extern struct hostent* gethost_byaddr(char *, Link *);
extern struct hostent* gethost_byname(char *, Link *);
extern void            flush_cache(void);
extern int	       init_resolver(void);
extern time_t	       timeout_query_list(time_t);
extern time_t	       expire_cache(time_t);
extern void            del_queries(char *);
extern void	       add_local_domain (char *, int);

#endif /* INCLUDED_res_h */
