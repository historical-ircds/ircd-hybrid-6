#ifndef INCLUDED_s_conf_h
#define INCLUDED_s_conf_h
/************************************************************************
 *   IRC - Internet Relay Chat, include/s_conf.h
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 */

/*
 * $Id$
 *
 * $Log$
 * Revision 1.3  1999/07/03 20:24:20  tomh
 * clean up class macros, includes
 *
 * Revision 1.2  1999/07/03 08:13:09  tomh
 * cleanup dependencies
 *
 * Revision 1.1  1999/06/23 00:28:37  tomh
 * added fileio module, changed file read/write code to use fileio, removed dgets, new header s_conf.h, new module files fileio.h fileio.c
 *
 */
#ifndef INCLUDED_config_h
#include "config.h"             /* defines */
#endif
#ifndef INCLUDED_fileio_h
#include "fileio.h"             /* FBFILE */
#endif
#ifndef INCLUDED_netinet_in_h
#include <netinet/in.h>         /* in_addr */
#define INCLUDED_netinet_in_h
#endif

struct Client;
struct SLink;

struct ConfItem
{
  struct ConfItem* next;     /* list node pointer */
  unsigned int     status;   /* If CONF_ILLEGAL, delete when no clients */
  unsigned int     flags;
  int              clients;  /* Number of *LOCAL* clients using this */
  struct in_addr   ipnum;    /* ip number of host field */
  unsigned long    ip;       /* only used for I D lines etc. */
  unsigned long    ip_mask;
  char*            name;     /* IRC name, nick, server name, or original u@h */
  char*            host;     /* host part of user@host */
  char*            passwd;
  char*            user;     /* user part of user@host */
  int              port;
  time_t           hold;     /* Hold action until this time (calendar time) */
  struct Class*    c_class;     /* Class of connection */
  int              dns_pending; /* 1 if dns query pending, 0 otherwise */
};


#define CONF_ILLEGAL            0x80000000
#define CONF_MATCH              0x40000000
#define CONF_QUARANTINED_NICK   0x0001
#define CONF_CLIENT             0x0002
#define CONF_CONNECT_SERVER     0x0004
#define CONF_NOCONNECT_SERVER   0x0008
#define CONF_LOCOP              0x0010
#define CONF_OPERATOR           0x0020
#define CONF_ME                 0x0040
#define CONF_KILL               0x0080
#define CONF_ADMIN              0x0100
#ifdef  R_LINES
#define CONF_RESTRICT           0x0200
#endif
#define CONF_CLASS              0x0400
#define CONF_LEAF               0x0800
#define CONF_LISTEN_PORT        0x1000
#define CONF_HUB                0x2000
#define CONF_ELINE              0x4000
#define CONF_FLINE              0x8000
#define CONF_BLINE              0x10000
#define CONF_DLINE              0x20000
#define CONF_XLINE              0x40000
#define CONF_ULINE              0x80000

#define CONF_OPS                (CONF_OPERATOR | CONF_LOCOP)
#define CONF_SERVER_MASK        (CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER)
#define CONF_CLIENT_MASK        (CONF_CLIENT | CONF_OPS | CONF_SERVER_MASK)

#define IsIllegal(x)    ((x)->status & CONF_ILLEGAL)

/* aConfItem->flags */

#define CONF_FLAGS_LIMIT_IP             0x0001
#define CONF_FLAGS_NO_TILDE             0x0002
#define CONF_FLAGS_NEED_IDENTD          0x0004
#define CONF_FLAGS_PASS_IDENTD          0x0008
#define CONF_FLAGS_NOMATCH_IP           0x0010
#define CONF_FLAGS_E_LINED              0x0020
#define CONF_FLAGS_B_LINED              0x0040
#define CONF_FLAGS_F_LINED              0x0080

#ifdef IDLE_CHECK
#define CONF_FLAGS_IDLE_LINED           0x0100
#endif

#define CONF_FLAGS_DO_IDENTD            0x0200
#define CONF_FLAGS_ALLOW_AUTO_CONN      0x0400
#define CONF_FLAGS_ZIP_LINK             0x0800
#define CONF_FLAGS_SPOOF_IP             0x1000

#ifdef LITTLE_I_LINES
#define CONF_FLAGS_LITTLE_I_LINE        0x8000
#endif

extern struct ConfItem* ConfigItemList;   /* GLOBAL - conf list head */


extern struct ConfItem* make_conf(void);
extern void             free_conf(struct ConfItem*);

extern FBFILE*          openconf(char* filename);
extern void             initconf(int, FBFILE*, int);
extern struct hostent*  conf_dns_lookup(struct ConfItem* aconf);
extern int              attach_conf(struct Client*, struct ConfItem *);
extern struct ConfItem* attach_confs(struct Client*, char *, int);
extern struct ConfItem* attach_confs_host(struct Client*, char *, int);
extern int              attach_Iline(struct Client *, struct hostent *,
                                     char *, char *,char **);
extern struct ConfItem* find_me (void);
extern struct ConfItem* find_admin (void);
extern struct ConfItem* count_cnlines (struct SLink *);
extern void             det_confs_butmask (struct Client *, int);
extern int              detach_conf (struct Client *, struct ConfItem *);
extern struct ConfItem* det_confs_butone (struct Client *, struct ConfItem *);
extern struct ConfItem* find_conf (struct SLink *, char*, int);
extern struct ConfItem* find_conf_exact (char *, char *, char *, int);
extern struct ConfItem* find_conf_host (struct SLink *, char *, int);
extern struct ConfItem* find_conf_ip (struct SLink *, char *, char *, int);
extern struct ConfItem* find_conf_name (char *, int);
extern struct ConfItem* find_kill (struct Client *);


#endif /* INCLUDED_s_conf_h */

