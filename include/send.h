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

#ifdef HAVE_STDARG_H

#define MyVaStart(a, p)      (va_start((a), (p)))

#else

#define MyVaStart(a, p)      (va_start((a)))

#endif /* HAVE_STDARG_H */

/* send.c prototypes */

extern  void send_operwall(aClient *,char *,char *);	/* defined in send.c */
extern  void sendto_channel_type_notice(aClient *, aChannel *, int, char *);
extern  int sendto_slaves(aClient *, char *, char *, int, char **);

#ifdef HAVE_STDARG_H

extern  void sendto_one(aClient *, char *, ...);
extern  void sendto_channel_butone(aClient *, aClient *, aChannel *, char *, ...);
extern  void sendto_channel_type(aClient *, aClient *, aChannel *, int, char *, ...);
extern  void sendto_serv_butone(aClient *, char *, ...);
extern  void sendto_common_channels(aClient *, char *, ...);
extern  void sendto_channel_butserv(aChannel *, aClient *, char *, ...);
extern  void sendto_match_servs(aChannel *, aClient *, char *, ...);
extern  void sendto_match_cap_servs(aChannel *, aClient *, int, char *, ...);
extern  void sendto_match_butone(aClient *, aClient *, char *, int, char *, ...);
extern  void sendto_ops_lev(int, char *, ...);
extern  void sendto_ops(char *, ...);
extern  void sendto_ops_butone(aClient *, aClient *, char *, ...);
extern  void sendto_wallops_butone(aClient *, aClient *, char *, ...);
extern  void sendto_realops(char *, ...);
extern  void sendto_realops_lev(int, char *, ...);
extern  void ts_warn(char *, ...);

extern  void sendto_prefix_one(aClient *, aClient *, char *, ...);

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
