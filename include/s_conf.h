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
 * Revision 1.21  1999/07/18 17:27:02  db
 * - a few more header cleanups
 * - motd.c included channel.h, no need
 *
 * Revision 1.20  1999/07/18 07:00:24  tomh
 * add new file
 *
 * Revision 1.19  1999/07/17 03:23:15  db
 * - my bad.
 * - fixed prototype in s_conf.h
 * - fixed typo of password for passwd in s_conf.c
 *
 * Revision 1.18  1999/07/17 03:13:03  db
 * - corrected type casting problems, mainly const char *
 * - moved prototype for safe_write into s_conf.h
 *
 * Revision 1.17  1999/07/16 11:57:31  db
 * - more cleanups
 * - removed unused function in FLUD code
 *
 * Revision 1.16  1999/07/16 09:57:54  db
 * - even more cleanups. moved prototype from h.h to s_conf.h
 *
 * Revision 1.15  1999/07/16 09:36:00  db
 * - rename some function names to make function clearer
 * - moved prototypes into headers
 * - made some functions static
 * - added some needed comments
 *
 * Revision 1.14  1999/07/16 04:16:59  db
 * - optimized get_conf_name
 * - replaced char * with const char * for filename
 *
 * Revision 1.13  1999/07/15 22:26:43  db
 * - fixed core bug in m_kline.c, probably should add extra sanity test there
 *   REDUNDANT_KLINES was using aconf->name instead of aconf->user
 * - cleaning up conf file generation etc.
 *
 * Revision 1.12  1999/07/15 02:45:07  db
 * - added conf_connect_allowed()
 *
 * Revision 1.11  1999/07/15 02:34:18  db
 * - redid m_kline, moved conf file writing from m_kline into s_conf.c
 *   thus "hiding" the details of where the kline gets written..
 *   Temporarily removed Shadowfax's LOCKFILE code until this settles down.
 *
 * Revision 1.10  1999/07/13 01:42:58  db
 * - cleaned up conf file handling, handled by read_conf_files()
 *
 * Revision 1.9  1999/07/11 21:09:35  tomh
 * sockhost cleanup and a lot of other stuff
 *
 * Revision 1.8  1999/07/11 02:44:17  db
 * - redid motd handling completely. most of the motd handling is now
 *   done in motd.c
 *   motd handling includes, motd, oper motd, help file
 *
 * Revision 1.7  1999/07/09 06:55:45  tomh
 * Changed resolver code to use reference counting instead of blind hostent
 * removal. This will ensure that if a client resolved we will always get
 * it's hostent. Currently we are saving the hostent for the life of the client,
 * but it can be released once the access checks are finished so the resolver
 * cache stays reasonably sized.
 *
 * Revision 1.6  1999/07/08 23:04:06  db
 * - fixed goof in s_conf.h
 *
 * Revision 1.5  1999/07/08 22:46:22  db
 * - changes to centralize config.h ircd config files to one struct
 *
 * Revision 1.4  1999/07/04 09:00:48  tomh
 * more cleanup, only call delete_resolver_queries when there are outstanding requests
 *
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
#ifndef INCLUDED_ircd_defs_h
#include "ircd_defs.h"
#endif
#ifndef INCLUDED_motd_h
#include "motd.h"
#endif

struct Client;
struct SLink;
struct DNSReply;
struct hostent;

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

extern void 		read_conf_files(int cold);

extern struct DNSReply* conf_dns_lookup(struct ConfItem* aconf);
extern int              attach_conf(struct Client*, struct ConfItem *);
extern struct ConfItem* attach_confs(struct Client*, char *, int);
extern struct ConfItem* attach_confs_host(struct Client*, char *, int);
extern int              attach_Iline(struct Client* client, 
                                     struct hostent* hp,
                                     const char* sockname, 
                                     const char* username, char** reason);
extern struct ConfItem* find_me(void);
extern struct ConfItem* find_admin(void);
extern struct ConfItem* count_cnlines(struct SLink *);
extern void             det_confs_butmask (struct Client *, int);
extern int              detach_conf (struct Client *, struct ConfItem *);
extern struct ConfItem* det_confs_butone (struct Client *, struct ConfItem *);
extern struct ConfItem* find_conf (struct SLink *, char*, int);
extern struct ConfItem* find_conf_exact(const char* name, const char* user, 
                                        const char* host, int statmask);
extern struct ConfItem* find_conf_host (struct SLink *, char *, int);
extern struct ConfItem* find_conf_ip (struct SLink *, char *, char *, int);
extern struct ConfItem* find_conf_name (char *, int);
extern struct ConfItem* find_kill (struct Client *);
extern int conf_connect_allowed(struct in_addr addr);
extern char *oper_flags_as_string(int);
extern char *oper_privs_as_string(struct Client *, int);
extern int rehash_dump(struct Client *);
extern int find_q_line(char*, char*, char *);
extern struct ConfItem* find_special_conf(char *,int );
extern struct ConfItem* find_is_klined(char*, char *,unsigned long);
extern void   GetPrintableaConfItem(struct ConfItem *,
				    char **, char **, char **,
				    char **, int *);

typedef enum {
  CONF_TYPE,
  KLINE_TYPE,
  DLINE_TYPE
}KlineType;

extern void write_kline_or_dline_to_conf_and_notice_opers(
							  KlineType,
							  struct Client *,
							  struct Client *,
							  char *,
							  char *,
							  char *,
							  char *
							  );
extern const char *get_conf_name(KlineType);
extern int safe_write(struct Client *, const char *, int ,char *);

typedef struct
{
  char *dpath;		/* DPATH if set from command line */
  char *configfile;
  char *klinefile;
  char *dlinefile;

#ifdef GLINES
  char	*glinefile;
#endif

  MessageFile helpfile;
  MessageFile motd;
  MessageFile opermotd;
}ConfigFileEntryType;


#endif /* INCLUDED_s_conf_h */

