/*
 * send.h
 * Copyright (C) 1999 Patrick Alken
 *
 * $Id$
 */

/*
 * MyVstart() removes the need for code duplication
 * in the sendto* functions by initializing the
 * variadic arguements appropriately depending on
 * whether we're using stdarg.h or varargs.h.
 * -wnder
 */
#ifndef INCLUDED_send_h
#define INCLUDED_send_h
#ifndef INCLUDED_config_h
#include "config.h"       /* HAVE_STDARG_H */
#endif

/*
 * struct decls
 */
struct Client;
struct Channel;

#ifdef HAVE_STDARG_H

#define MyVaStart(a, p)      (va_start((a), (p)))

#else

#define MyVaStart(a, p)      (va_start((a)))

#endif /* HAVE_STDARG_H */

/* send.c prototypes */

extern  void send_operwall(struct Client *,char *,char *); /* send.c */
extern  void sendto_channel_type_notice(struct Client *, 
                                        struct Channel *, int, char *);
extern  int sendto_slaves(struct Client *, char *, char *, int, char **);

#ifdef HAVE_STDARG_H

extern  void sendto_one(struct Client *, char *, ...);
extern  void sendto_channel_butone(struct Client *, struct Client *, 
                                   struct Channel *, const char *, ...);
extern  void sendto_channel_type(struct Client *, struct Client *, 
                                 struct Channel *, int, const char *, ...);
extern  void sendto_serv_butone(struct Client *, const char *, ...);
extern  void sendto_common_channels(struct Client *, const char *, ...);
extern  void sendto_channel_butserv(struct Channel *, struct Client *, 
                                    const char *, ...);
extern  void sendto_match_servs(struct Channel *, struct Client *, 
                                const char *, ...);
extern  void sendto_match_cap_servs(struct Channel *, struct Client *, 
                                    int, const char *, ...);
extern  void sendto_match_butone(struct Client *, struct Client *, 
                                 char *, int, const char *, ...);
extern  void sendto_ops_lev(int, const char *, ...);
extern  void sendto_ops(const char *, ...);
extern  void sendto_ops_butone(struct Client *, struct Client *, 
                               const char *, ...);
extern  void sendto_wallops_butone(struct Client *, struct Client *, 
                                   const char *, ...);
extern  void sendto_realops(const char *, ...);
extern  void sendto_realops_lev(int, const char *, ...);
extern  void ts_warn(const char *, ...);

extern  void sendto_prefix_one(struct Client *, struct Client *, 
                               const char *, ...);

#else

extern  void sendto_one();
extern  void sendto_channel_butone();
extern  void sendto_channel_type();
extern  void sendto_serv_butone();
extern  void sendto_common_channels();
extern  void sendto_channel_butserv();
extern  void sendto_match_servs();
extern  void sendto_match_cap_servs();
extern  void sendto_match_butone();
extern  void sendto_ops_lev();
extern  void sendto_ops();
extern  void sendto_ops_butone();
extern  void sendto_wallops_butone();
extern  void sendto_realops();
extern  void sendto_realops_lev();
extern  void ts_warn();

extern  void sendto_prefix_one();

#endif /* HAVE_STDARG_H */
#endif /* INCLUDED_send_h */
