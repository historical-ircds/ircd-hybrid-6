#ifndef INCLUDED_s_misc_h
#define INCLUDED_s_misc_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;
struct ConfItem;

extern  void    serv_info (struct Client *, char *);
extern  void    initstats (void);
extern  void    tstats  (struct Client *, char *);
extern  char*   date(time_t);
extern  const   char* my_name_for_link(const char* name,struct ConfItem* conf);
extern  struct  stats* ircstp;
extern  char    *show_capabilities (struct Client *);
extern  char    *smalldate(time_t);
extern  char    *small_file_date(time_t);

#endif


