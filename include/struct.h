/* - Internet Relay Chat, include/struct.h
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
 *
 *
 * $Id$
 */
#ifndef INCLUDED_struct_h
#define INCLUDED_struct_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#if !defined(CONFIG_H_LEVEL_6)
#  error Incorrect config.h for this revision of ircd.
#endif
#ifndef INCLUDED_stdio_h
#include <stdio.h>
#define INCLUDED_stdio_h
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_netinet_in_h
#include <netinet/in.h>
#define INCLUDED_netinet_in_h
#endif

#ifdef ORATIMING
# ifndef INCLUDED_sys_time_h
#  include <sys/time.h>
#  define INCLUDED_sys_time_h
# endif
#endif

#ifdef USE_SYSLOG
# ifndef INCLUDED_syslog_h
#  include <syslog.h>
#  define INCLUDED_syslog_h
# endif
#endif

#ifndef INCLUDED_ircd_defs_h
# include "ircd_defs.h"
#endif

#ifndef INCLUDED_client_h
# include "client.h"
#endif

struct Channel;

typedef struct  ConfItem aConfItem;
typedef struct  Client  aClient;
typedef struct  User    anUser;
typedef struct  Server  aServer;
typedef struct  SLink   Link;
typedef struct  Mode    Mode;
typedef struct  Zdata   aZdata;

/*
** flags for bootup options (command line flags)
*/
#define BOOT_CONSOLE    1
#define BOOT_DEBUG      4
#define BOOT_TTY        16
#define BOOT_STDERR     128

/*
 * defined debugging levels
 */
#define DEBUG_FATAL  0
#define DEBUG_ERROR  1  /* report_error() and other errors that are found */
#define DEBUG_NOTICE 3
#define DEBUG_DNS    4  /* used by all DNS related routines - a *lot* */
#define DEBUG_INFO   5  /* general usful info */
#define DEBUG_NUM    6  /* numerics */
#define DEBUG_SEND   7  /* everything that is sent out */
#define DEBUG_DEBUG  8  /* anything to do with debugging, ie unimportant :) */
#define DEBUG_MALLOC 9  /* malloc/free calls */
#define DEBUG_LIST  10  /* debug list use */

/*
 * statistics structures
 */
struct  stats {
        unsigned int    is_cl;  /* number of client connections */
        unsigned int    is_sv;  /* number of server connections */
        unsigned int    is_ni;  /* connection but no idea who it was */
        unsigned short  is_cbs; /* bytes sent to clients */
        unsigned short  is_cbr; /* bytes received to clients */
        unsigned short  is_sbs; /* bytes sent to servers */
        unsigned short  is_sbr; /* bytes received to servers */
        unsigned long   is_cks; /* k-bytes sent to clients */
        unsigned long   is_ckr; /* k-bytes received to clients */
        unsigned long   is_sks; /* k-bytes sent to servers */
        unsigned long   is_skr; /* k-bytes received to servers */
        time_t          is_cti; /* time spent connected by clients */
        time_t          is_sti; /* time spent connected by servers */
        unsigned int    is_ac;  /* connections accepted */
        unsigned int    is_ref; /* accepts refused */
        unsigned int    is_unco; /* unknown commands */
        unsigned int    is_wrdi; /* command going in wrong direction */
        unsigned int    is_unpf; /* unknown prefix */
        unsigned int    is_empt; /* empty message */
        unsigned int    is_num; /* numeric message */
        unsigned int    is_kill; /* number of kills generated on collisions */
        unsigned int    is_fake; /* MODE 'fakes' */
        unsigned int    is_asuc; /* successful auth requests */
        unsigned int    is_abad; /* bad auth requests */
        unsigned int    is_udp; /* packets recv'd on udp port */
        unsigned int    is_loc; /* local connections made */
#ifdef FLUD
        unsigned int    is_flud;        /* users/channels flood protected */
#endif /* FLUD */
};


#ifdef BAN_INFO
/*
  Move BAN_INFO information out of the SLink struct
  its _only_ used for bans, no use wasting the memory for it
  in any other type of link. Keep in mind, doing this that
  it makes it slower as more Malloc's/Free's have to be done, 
  on the plus side bans are a smaller percentage of SLink usage.
  Over all, the th+hybrid coding team came to the conclusion
  it was worth the effort.

  - Dianora
*/
typedef struct Ban      /* also used for exceptions -orabidoo */
{
  char *banstr;
  char *who;
  time_t when;
} aBan;
#endif

/* general link structure used for chains */

struct SLink
{
  struct        SLink   *next;
  union
  {
    aClient   *cptr;
    struct Channel  *chptr;
    aConfItem *aconf;
#ifdef BAN_INFO
    aBan      *banptr;
#endif
    char      *cp;
  } value;
  int   flags;
};


/* used when sending to #mask or $mask */

#define MATCH_SERVER  1
#define MATCH_HOST    2

/* misc variable externs */

/* misc defines */

#define UTMP            "/etc/utmp"
#define COMMA           ","

#ifdef ORATIMING
/* Timing stuff (for performance measurements): compile with -DORATIMING
   and put a TMRESET where you want the counter of time spent set to 0,
   a TMPRINT where you want the accumulated results, and TMYES/TMNO pairs
   around the parts you want timed -orabidoo
*/
extern struct timeval tsdnow, tsdthen;
extern unsigned long tsdms;
#define TMRESET tsdms=0;
#define TMYES gettimeofday(&tsdthen, NULL);
#define TMNO gettimeofday(&tsdnow, NULL); if (tsdnow.tv_sec!=tsdthen.tv_sec) tsdms+=1000000*(tsdnow.tv_sec-tsdthen.tv_sec); tsdms+=tsdnow.tv_usec; tsdms-=tsdthen.tv_usec;
#define TMPRINT sendto_ops("Time spent: %ld ms", tsdms);
#else
#define TMRESET
#define TMYES
#define TMNO
#define TMPRINT
#endif

#ifdef FLUD
struct fludbot {
        struct Client   *fluder;
        int             count;
        time_t          first_msg, last_msg;
        struct fludbot  *next;
};
#endif /* FLUD */


#endif /* INCLUDED_struct_h */
