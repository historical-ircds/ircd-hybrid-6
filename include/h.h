/************************************************************************
 *   IRC - Internet Relay Chat, include/h.h
 *   Copyright (C) 1992 Darren Reed
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * "h.h". - Headers file.
 *
 * Most of the externs and prototypes thrown in here to 'cleanup' things.
 * -avalon
 *
 * $Id$
 *
 */
#ifndef INCLUDED_h_h
#define INCLUDED_h_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_fdlist_h
#include "fdlist.h"
#endif

struct Client;
struct Class;
struct Channel;
struct ConfItem;
struct User;
struct stats;
struct SLink;
struct Message;
struct Server;

/* 
 * GLOBAL - global variables
 */

#ifdef  GLINES
extern struct ConfItem* find_gkill(struct Client* client);
extern struct ConfItem* find_is_glined(const char* host, const char* name);
extern  void    flush_glines(void);             
extern  void    report_glines(struct Client *); 
#endif

/* ircd.c */
extern void     report_error_on_tty(const char* message);
extern  int     debuglevel;
extern  int     highest_fd;
extern  int     debuglevel;
extern  int     debugtty;
extern  char*   debugmode;
extern  time_t  check_fdlists (time_t);
extern int    lifesux;
extern struct Counter Count;
extern time_t NOW;
extern time_t nextconnect;
extern time_t nextping;
extern time_t timeofday;
extern struct Client* GlobalClientList;
extern struct Client  me;
extern struct Client* local[];
extern struct Channel* channel;
extern struct stats* ircstp;
extern int    bootopt;

/* scache.c */
extern void        clear_scache_hash_table(void);
extern const char* find_or_add(const char* name);
extern void        count_scache(int *,unsigned long *);
extern void        list_scache(struct Client *, struct Client *,int, char **);

extern void     dummy(int signo);

extern  char    *form_str (int);
extern  void    get_my_name (struct Client *, char *, int);

/* s_bsd.c */
extern  int     send_queued(struct Client*);

/* s_serv.c */
extern  void    send_capabilities(struct Client *,int);

/* bsd.c */
extern  int     deliver_it (struct Client *, char *, int);

/* s_numeric.c */
extern  int     do_numeric (int, struct Client *, struct Client *, int, char **);

/* packet.c */
extern  int     dopacket (struct Client *, char *, int);

#endif /* INCLUDED_h_h */

