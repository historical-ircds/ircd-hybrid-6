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

struct DNSQuery {
  void* vptr;               /* pointer used by callback to identify request */
  void (*callback)(void* vptr, struct hostent* hp);  /* callback to call */
};

extern void get_res(void);
extern struct hostent* gethost_byname(const char* name, 
                                      const struct DNSQuery* req);
extern struct hostent* gethost_byaddr(const char* name, 
                                      const struct DNSQuery* req);
extern void            flush_cache(void);
extern int	       init_resolver(void);
extern time_t	       timeout_query_list(time_t);
extern time_t	       expire_cache(time_t);
extern void            del_queries(const void* vptr);
extern void            restart_resolver(void);

/*
 * add_local_domain - append local domain suffix to hostnames that 
 * don't contain a dot '.'
 * name - string to append to
 * len  - total length of the buffer
 * name is modified only if there is enough space in the buffer to hold
 * the suffix
 */
extern void add_local_domain(char* name, int len);

#endif /* INCLUDED_res_h */
