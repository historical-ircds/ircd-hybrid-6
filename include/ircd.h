#ifndef INCLUDED_ircd_h
#define INCLUDED_ircd_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;

extern void     report_error_on_tty(const char* message);
extern  int     debuglevel;
extern  int     debugtty;
extern  char*   debugmode;
extern  time_t  check_fdlists (time_t);
extern struct Counter Count;
extern time_t NOW;
extern time_t nextconnect;
extern time_t nextping;
extern time_t timeofday;
extern struct Client* GlobalClientList;
extern struct Client  me;
extern struct Client* local[];
extern int    bootopt;
extern int    cold_start;

#endif
