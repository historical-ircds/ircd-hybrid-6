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
#ifndef	INCLUDED_struct_h
#define INCLUDED_struct_h

#include "config.h"
#if !defined(CONFIG_H_LEVEL_6)
#  error Incorrect config.h for this revision of ircd.
#endif

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
/* #include <netdb.h> */
#if defined( HAVE_STDDEF_H )
# include <stddef.h>
#endif
#ifdef ORATIMING
#include <sys/time.h>
#endif

#ifdef USE_SYSLOG
# include <syslog.h>
# if defined( HAVE_SYS_SYSLOG_H )
#  include <sys/syslog.h>
# endif
#endif
#ifdef	pyr
#include <sys/time.h>
#endif

#include "zlib.h"

#define REPORT_DO_DNS	"NOTICE AUTH :*** Looking up your hostname...\n"
#define REPORT_FIN_DNS	"NOTICE AUTH :*** Found your hostname\n"
#define REPORT_FIN_DNSC	"NOTICE AUTH :*** Found your hostname, cached\n"
#define REPORT_FAIL_DNS	"NOTICE AUTH :*** Couldn't look up your hostname\n"
#define REPORT_DO_ID	"NOTICE AUTH :*** Checking Ident\n"
#define REPORT_FIN_ID	"NOTICE AUTH :*** Got Ident response\n"
#define REPORT_FAIL_ID	"NOTICE AUTH :*** No Ident response\n"

#define REPORT_DLINED   "NOTICE DLINE :*** You have been D-lined\n"

/*
 * We don't want to calculate these every time they are used :)
 */

#define R_do_dns (sizeof(REPORT_DO_DNS)-1)
#define R_fin_dns (sizeof(REPORT_FIN_DNS)-1)
#define R_fin_dnsc (sizeof(REPORT_FIN_DNSC)-1)
#define R_fail_dns (sizeof(REPORT_FAIL_DNS)-1)
#define R_do_id	(sizeof(REPORT_DO_ID)-1)
#define R_fin_id (sizeof(REPORT_FIN_ID)-1)
#define R_fail_id (sizeof(REPORT_FAIL_ID)-1)


/* sendheader from orabidoo TS4 */
#define sendheader(cptr, msg, len) do \
   { if (IsUnknown(cptr)) \
     send((cptr)->fd, (msg), (len), 0); } while(0)


#include "hash.h"

typedef	struct	ConfItem aConfItem;
typedef	struct 	Client	aClient;
typedef	struct	Channel	aChannel;
typedef	struct	User	anUser;
typedef	struct	Server	aServer;
typedef	struct	SLink	Link;
typedef	struct	SMode	Mode;
typedef	long	ts_val;
typedef struct  Zdata   aZdata;

typedef struct	MessageFileItem aMessageFile;

#include "class.h"
#include "dbuf.h"	/* THIS REALLY SHOULDN'T BE HERE!!! --msa */

#define	HOSTLEN		63	/* Length of hostname.  Updated to         */
				/* comply with RFC1123                     */

#define HOSTIPLEN	15	/* Length of dotted quad form of IP	   */
				/* - Dianora 				   */
#define	NICKLEN		9	/* Necessary to put 9 here instead of 10
				** if s_msg.c/m_nick has been corrected.
				** This preserves compatibility with old
				** servers --msa
				*/

#define MAX_DATE_STRING 32	/* maximum string length for a date string */

#define	USERLEN		10
#define	REALLEN	 	50
#define TOPICLEN 	120	/* old value 90, truncated on other servers */
#define	KILLLEN		90	
#define	CHANNELLEN	200
#define	PASSWDLEN 	20
#define	KEYLEN		23
#define IDLEN		12	/* this is the maximum length, not the actual
				   generated length; DO NOT CHANGE! */
#define	BUFSIZE		512		/* WARNING: *DONT* CHANGE THIS!!!! */
#define	MAXRECIPIENTS 	20
#define	MAXBANS		25	/* bans + exceptions together */
#define	MAXBANLENGTH	1024

#define OPERWALL_LEN    400		/* can be truncated on other servers */

#define MESSAGELINELEN	90

#define	USERHOST_REPLYLEN	(NICKLEN+HOSTLEN+USERLEN+5)

#define READBUF_SIZE    16384	/* used in s_bsd *AND* s_zip.c ! */

/*
** 'offsetof' is defined in ANSI-C. The following definition
** is not absolutely portable (I have been told), but so far
** it has worked on all machines I have needed it. The type
** should be size_t but...  --msa
*/
#ifndef offsetof
#define	offsetof(t,m) (int)((&((t *)0L)->m))
#endif

#define	elementsof(x) (sizeof(x)/sizeof(x[0]))

/*
** flags for bootup options (command line flags)
*/
#define	BOOT_CONSOLE	1
#define	BOOT_QUICK	2
#define	BOOT_DEBUG	4
#define	BOOT_INETD	8
#define	BOOT_TTY	16
#define	BOOT_OPER	32
#define	BOOT_AUTODIE	64
#define BOOT_STDERR	128
#define	STAT_LOG	-6	/* logfile for -x */
#define	STAT_MASTER	-5	/* Local ircd master before identification */
#define	STAT_CONNECTING	-4
#define	STAT_HANDSHAKE	-3
#define	STAT_ME		-2
#define	STAT_UNKNOWN	-1
#define	STAT_SERVER	0
#define	STAT_CLIENT	1

/*
 * status macros.
 */

#define	IsRegisteredUser(x)	((x)->status == STAT_CLIENT)
#define	IsRegistered(x)		((x)->status >= STAT_SERVER)
#define	IsConnecting(x)		((x)->status == STAT_CONNECTING)
#define	IsHandshake(x)		((x)->status == STAT_HANDSHAKE)
#define	IsMe(x)			((x)->status == STAT_ME)
#define	IsUnknown(x)		((x)->status == STAT_UNKNOWN || \
				 (x)->status == STAT_MASTER)
#define	IsServer(x)		((x)->status == STAT_SERVER)
#define	IsClient(x)		((x)->status == STAT_CLIENT)
#define	IsLog(x)		((x)->status == STAT_LOG)

#define	SetMaster(x)		((x)->status = STAT_MASTER)
#define	SetConnecting(x)	((x)->status = STAT_CONNECTING)
#define	SetHandshake(x)		((x)->status = STAT_HANDSHAKE)
#define	SetMe(x)		((x)->status = STAT_ME)
#define	SetUnknown(x)		((x)->status = STAT_UNKNOWN)
#define	SetServer(x)		((x)->status = STAT_SERVER)
#define	SetClient(x)		((x)->status = STAT_CLIENT)
#define	SetLog(x)		((x)->status = STAT_LOG)


#define	FLAGS_PINGSENT   0x0001	/* Unreplied ping sent */
#define	FLAGS_DEADSOCKET 0x0002	/* Local socket is dead--Exiting soon */
#define	FLAGS_KILLED     0x0004	/* Prevents "QUIT" from being sent for this */
#define	FLAGS_OPER       0x0008	/* Operator */
#define	FLAGS_LOCOP      0x0010 /* Local operator -- SRB */
#define	FLAGS_INVISIBLE  0x0020 /* makes user invisible */
#define	FLAGS_WALLOP     0x0040 /* send wallops to them */
#define	FLAGS_SERVNOTICE 0x0080 /* server notices such as kill */
#define	FLAGS_BLOCKED    0x0100	/* socket is in a blocked condition */
#define FLAGS_REJECT_HOLD 0x0200 /* client has been klined */
/* #define FLAGS_UNIX	 0x0200  Not used anymore, free for other use */
     				/* socket is in the unix domain, not inet */
#define	FLAGS_CLOSING    0x0400	/* set when closing to suppress errors */
#define	FLAGS_LISTEN     0x0800 /* used to mark clients which we listen() on */
#define	FLAGS_CHKACCESS  0x1000 /* ok to check clients access if set */
#define	FLAGS_DOINGDNS	 0x2000 /* client is waiting for a DNS response */
#define	FLAGS_AUTH	 0x4000 /* client is waiting on rfc931 response */
#define	FLAGS_WRAUTH	 0x8000	/* set if we havent writen to ident server */
#define	FLAGS_LOCAL	0x10000 /* set for local clients */
#define	FLAGS_GOTID	0x20000	/* successful ident lookup achieved */
#define	FLAGS_DOID	0x40000	/* I-lines say must use ident return */
#define	FLAGS_NONL	0x80000 /* No \n in buffer */
#define FLAGS_CCONN    0x100000 /* Client Connections */
#define FLAGS_REJ      0x200000 /* Bot Rejections */
#define FLAGS_SKILL    0x400000 /* Server Killed */
#define FLAGS_FULL     0x800000 /* Full messages */
#define FLAGS_NORMALEX 0x1000000 /* Client exited normally */
#define	FLAGS_NCHANGE  0x2000000 /* Nick change notice */
#define	FLAGS_SPY      0x4000000 /* see STATS / LINKS */
#define FLAGS_DEBUG    0x8000000 /* 'debugging' info */
#define FLAGS_SENDQEX  0x10000000 /* Sendq exceeded */
#define FLAGS_OPERWALL 0x20000000 /* Operwalls */
#define FLAGS_IPHASH   0x40000000 /* iphashed this client */

/* *sigh* overflow flags */
#define FLAGS2_RESTRICTED   0x0001      /* restricted client */
#define FLAGS2_PING_TIMEOUT 0x0002
#define FLAGS2_E_LINED	    0x0004	/* client is graced with E line */
#define FLAGS2_B_LINED	    0x0008	/* client is graced with B line */
#define FLAGS2_F_LINED	    0x0010	/* client is graced with F line */

/* oper priv flags */

#define FLAGS2_OPER_GLOBAL_KILL 0x0020	/* oper can global kill */
#define FLAGS2_OPER_REMOTE	0x0040	/* oper can do squits/connects */
#define FLAGS2_OPER_UNKLINE	0x0080	/* oper can use unkline */
#define FLAGS2_OPER_GLINE	0x0100	/* oper can use gline */
#define FLAGS2_OPER_N		0x0200	/* oper can umode n */
#define FLAGS2_OPER_K		0x0400	/* oper can kill/kline */
#define FLAGS2_OPER_FLAGS	(FLAGS2_OPER_GLOBAL_KILL | \
				 FLAGS2_OPER_REMOTE | \
				 FLAGS2_OPER_UNKLINE | \
				 FLAGS2_OPER_GLINE | \
				 FLAGS2_OPER_N | \
				 FLAGS2_OPER_K)
/* ZIP_LINKS */

#define FLAGS2_ZIP	0x1000	/* (server) link is zipped */
#define FLAGS2_ZIPFIRST	0x2000	/* start of zip (ignore any CR/LF) */
#define FLAGS2_CBURST	0x4000	/* connection burst being sent */

#define FLAGS2_DOINGLIST 	0x8000  /* client is doing a list */
#ifdef IDLE_CHECK
#define FLAGS2_IDLE_LINED   0x10000
#endif
#define FLAGS2_ALREADY_EXITED	0x20000 /* kludge grrrr */
#define FLAGS2_IP_SPOOFING	0x40000	/* client IP is spoofed */
#define FLAGS2_IP_HIDDEN	0x80000	/* client IP should be hidden
					   from non opers */

/* for sendto_ops_lev */
#define CCONN_LEV	1
#define REJ_LEV		2
#define SKILL_LEV	3
#define FULL_LEV	4
#define SPY_LEV		5
#define DEBUG_LEV	6
#define NCHANGE_LEV	7

#define	SEND_UMODES	(FLAGS_INVISIBLE|FLAGS_OPER|FLAGS_WALLOP)
#define	ALL_UMODES	(SEND_UMODES|FLAGS_SERVNOTICE|FLAGS_CCONN|FLAGS_REJ|FLAGS_SKILL|FLAGS_FULL|FLAGS_SPY|FLAGS_NCHANGE|FLAGS_OPERWALL|FLAGS_DEBUG)

#ifndef OPER_UMODES
#define OPER_UMODES   (FLAGS_OPER|FLAGS_WALLOP|FLAGS_SERVNOTICE|FLAGS_SPY|FLAGS_OPERWALL|FLAGS_DEBUG)
#endif /* OPER_UMODES */
#ifndef LOCOP_UMODES
#define LOCOP_UMODES   (FLAGS_LOCOP|FLAGS_WALLOP|FLAGS_SERVNOTICE|FLAGS_SPY|FLAGS_DEBUG)
#endif /* LOCOP_UMODES */
#define	FLAGS_ID	(FLAGS_DOID|FLAGS_GOTID)

/*
 * flags macros.
 */
#define	IsOper(x)		((x)->flags & FLAGS_OPER)
#define	IsLocOp(x)		((x)->flags & FLAGS_LOCOP)
#define	IsInvisible(x)		((x)->flags & FLAGS_INVISIBLE)
#define	IsAnOper(x)		((x)->flags & (FLAGS_OPER|FLAGS_LOCOP))
#define	IsPerson(x)		((x)->user && IsClient(x))
#define	IsPrivileged(x)		(IsAnOper(x) || IsServer(x))
#define	SendWallops(x)		((x)->flags & FLAGS_WALLOP)
#define	SendServNotice(x)	((x)->flags & FLAGS_SERVNOTICE)
#define SendOperwall(x)		((x)->flags & FLAGS_OPERWALL)
#define SendCConnNotice(x)	((x)->flags & FLAGS_CCONN)
#define SendRejNotice(x)	((x)->flags & FLAGS_REJ)
#define SendSkillNotice(x)	((x)->flags & FLAGS_SKILL)
#define SendFullNotice(x)	((x)->flags & FLAGS_FULL)
#define SendSpyNotice(x)	((x)->flags & FLAGS_SPY)
#define SendDebugNotice(x)	((x)->flags & FLAGS_DEBUG)
#define SendNickChange(x)	((x)->flags & FLAGS_NCHANGE)
     /* #define	IsUnixSocket(x)		((x)->flags & FLAGS_UNIX) */
     /* No more FLAGS_UNIX */
#define	IsListening(x)		((x)->flags & FLAGS_LISTEN)
#define	DoAccess(x)		((x)->flags & FLAGS_CHKACCESS)
#define	IsLocal(x)		((x)->flags & FLAGS_LOCAL)
#define	IsDead(x)		((x)->flags & FLAGS_DEADSOCKET)
#define	SetOper(x)		((x)->flags |= FLAGS_OPER)
#define	SetLocOp(x)    		((x)->flags |= FLAGS_LOCOP)
#define	SetInvisible(x)		((x)->flags |= FLAGS_INVISIBLE)
#define	SetWallops(x)  		((x)->flags |= FLAGS_WALLOP)
/* #define	SetUnixSock(x)		((x)->flags |= FLAGS_UNIX) */
#define	SetDNS(x)		((x)->flags |= FLAGS_DOINGDNS)
#define SetStartAuth(x)		((x)->flags |= (FLAGS_WRAUTH|FLAGS_AUTH))
#define ClearStartAuth(x)	((x)->flags &= ~(FLAGS_WRAUTH|FLAGS_AUTH))
#define ClearWriteAuth(x)	((x)->flags &= ~FLAGS_WRAUTH)
#define IsWriteAuth(x)		((x)->flags & FLAGS_WRAUTH)
#define	DoingDNS(x)		((x)->flags & FLAGS_DOINGDNS)
#define	SetAccess(x)		((x)->flags |= FLAGS_CHKACCESS)
#define	DoingAuth(x)		((x)->flags & FLAGS_AUTH)
#define	NoNewLine(x)		((x)->flags & FLAGS_NONL)
#define	ClearOper(x)		((x)->flags &= ~FLAGS_OPER)
#define ClearLocOp(x)		((x)->flags &= ~FLAGS_LOCOP)
#define	ClearInvisible(x)	((x)->flags &= ~FLAGS_INVISIBLE)
#define	ClearWallops(x)		((x)->flags &= ~FLAGS_WALLOP)
#define	ClearDNS(x)		((x)->flags &= ~FLAGS_DOINGDNS)
#define	ClearAuth(x)		((x)->flags &= ~FLAGS_AUTH)
#define	ClearAccess(x)		((x)->flags &= ~FLAGS_CHKACCESS)
#define SetGotId(x)		((x)->flags |= FLAGS_GOTID)

#ifdef REJECT_HOLD
#define IsRejectHeld(x)	        ((x)->flags & FLAGS_REJECT_HOLD)
#define SetRejectHold(x)        ((x)->flags |= FLAGS_REJECT_HOLD)
#endif

#define SetIpHash		((x)->flags |= FLAGS_IPHASH)
#define ClearIpHash		((x)->flags &= ~FLAGS_IPHASH)
#define IsIpHash		((x)->flags & FLAGS_IPHASH)

#define SetDoId(x)		((x)->flags |= FLAGS_DOID)
#define IsGotId(x)		((x)->flags & FLAGS_GOTID)

/*
 * flags2 macros.
 */
#define IsRestricted(x)		((x)->flags2 & FLAGS2_RESTRICTED)
#define SetRestricted(x)	((x)->flags2 |= FLAGS2_RESTRICTED)
#define ClearDoingList(x)	((x)->flags2 &= ~FLAGS2_DOINGLIST)
#define SetDoingList(x)         ((x)->flags2 |= FLAGS2_DOINGLIST)
#define IsDoingList(x)		((x)->flags2 & FLAGS2_DOINGLIST)
#define IsElined(x)		((x)->flags2 & FLAGS2_E_LINED)
#define SetElined(x)		((x)->flags2 |= FLAGS2_E_LINED)
#define IsBlined(x)		((x)->flags2 & FLAGS2_B_LINED)
#define SetBlined(x)		((x)->flags2 |= FLAGS2_B_LINED)
#define IsFlined(x)		((x)->flags2 & FLAGS2_F_LINED)
#define SetFlined(x)		((x)->flags2 |= FLAGS2_F_LINED)
#define SetIPSpoof(x)		((x)->flags2 |= FLAGS2_IP_SPOOFING)
#define IsIPSpoof(x)		((x)->flags2 & FLAGS2_IP_SPOOFING)
#define SetIPHidden(x)		((x)->flags2 |= FLAGS2_IP_HIDDEN)
#define IsIPHidden(x)		((x)->flags2 & FLAGS2_IP_HIDDEN)

#ifdef IDLE_CHECK
#define SetIdlelined(x)		((x)->flags2 |= FLAGS2_IDLE_LINED)
#define IsIdlelined(x)		((x)->flags2 & FLAGS2_IDLE_LINED)
#endif

#define SetOperGlobalKill(x)	((x)->flags2 |= FLAGS2_OPER_GLOBAL_KILL)
#define IsOperGlobalKill(x)	((x)->flags2 & FLAGS2_OPER_GLOBAL_KILL)
#define SetOperRemote(x)	((x)->flags2 |= FLAGS2_OPER_REMOTE)
#define IsOperRemote(x)		((x)->flags2 & FLAGS2_OPER_REMOTE)
#define SetOperUnkline(x)	((x)->flags2 |= FLAGS2_OPER_UNKLINE)
#define IsSetOperUnkline(x)	((x)->flags2 & FLAGS2_OPER_UNKLINE)
#define SetOperGline(x)		((x)->flags2 |= FLAGS2_OPER_GLINE)
#define IsSetOperGline(x)	((x)->flags2 & FLAGS2_OPER_GLINE)
#define SetOperN(x)		((x)->flags2 |= FLAGS2_OPER_N)
#define IsSetOperN(x)		((x)->flags2 & FLAGS2_OPER_N)
#define SetOperK(x)		((x)->flags2 |= FLAGS2_OPER_K)
#define IsSetOperK(x)		((x)->flags2 & FLAGS2_OPER_K)

#define CBurst(x)		((x)->flags2 & FLAGS2_CBURST)

/*
 * defined debugging levels
 */
#define	DEBUG_FATAL  0
#define	DEBUG_ERROR  1	/* report_error() and other errors that are found */
#define	DEBUG_NOTICE 3
#define	DEBUG_DNS    4	/* used by all DNS related routines - a *lot* */
#define	DEBUG_INFO   5	/* general usful info */
#define	DEBUG_NUM    6	/* numerics */
#define	DEBUG_SEND   7	/* everything that is sent out */
#define	DEBUG_DEBUG  8	/* anything to do with debugging, ie unimportant :) */
#define	DEBUG_MALLOC 9	/* malloc/free calls */
#define	DEBUG_LIST  10	/* debug list use */


struct	Counter	{
	int	server;		/* servers */
	int	myserver;	/* my servers */
	int	oper;		/* Opers */
	int	chan;		/* Channels */
	int	local;		/* Local Clients */
	int	total;		/* total clients */
	int	invisi;		/* invisible clients */
	int	unknown;	/* unknown connections */
	int	max_loc;	/* MAX local clients */
	int	max_tot;	/* MAX global clients */
};

struct  MessageFileItem
{
  char	line[MESSAGELINELEN];
  struct MessageFileItem *next;
};

/*
  lets speed this up...
  also removed away information. *tough*
  - Dianora
 */
typedef struct Whowas
{
  int hashv;
  char name[NICKLEN+1];
  char username[USERLEN+1]; 
  char hostname[HOSTLEN+1];
  char *servername;
  char realname[REALLEN+1];
  time_t logoff;
  struct Client *online; /* Pointer to new nickname for chasing or NULL */
  struct Whowas *next;  /* for hash table... */
  struct Whowas *prev;  /* for hash table... */
  struct Whowas *cnext; /* for client struct linked list */
  struct Whowas *cprev; /* for client struct linked list */
}aWhowas;

struct	ConfItem
{
  unsigned int	status;	/* If CONF_ILLEGAL, delete when no clients */
  unsigned int flags;	
  int	clients;	/* Number of *LOCAL* clients using this */
  struct in_addr ipnum;	/* ip number of host field */
  unsigned long ip;	/* only used for I D lines etc. */
  unsigned long ip_mask;
  char	*name;		/* IRC name, nick, server name, or original u@h */
  char	*host;		/* host part of user@host */
  char	*passwd;
  char	*user;		/* user part of user@host */
  int	port;
  time_t hold;		/* Hold action until this time (calendar time) */
  aClass *class;	  /* Class of connection */
  struct ConfItem *next;
};


#define	CONF_ILLEGAL		0x80000000
#define	CONF_MATCH		0x40000000
#define CONF_QUARANTINED_NICK	0x0001
#define	CONF_CLIENT		0x0002
#define	CONF_CONNECT_SERVER	0x0004
#define	CONF_NOCONNECT_SERVER	0x0008
#define	CONF_LOCOP		0x0010
#define	CONF_OPERATOR		0x0020
#define	CONF_ME			0x0040
#define	CONF_KILL		0x0080
#define	CONF_ADMIN		0x0100
#ifdef 	R_LINES
#define	CONF_RESTRICT		0x0200
#endif
#define	CONF_CLASS		0x0400
#define	CONF_LEAF		0x0800
#define	CONF_LISTEN_PORT	0x1000
#define	CONF_HUB		0x2000
#define CONF_ELINE		0x4000
#define CONF_FLINE		0x8000
#define	CONF_BLINE		0x10000
#define	CONF_DLINE		0x20000
#define CONF_XLINE		0x40000
#define CONF_ULINE		0x80000

#define	CONF_OPS		(CONF_OPERATOR | CONF_LOCOP)
#define	CONF_SERVER_MASK	(CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER)
#define	CONF_CLIENT_MASK	(CONF_CLIENT | CONF_OPS | \
				 CONF_SERVER_MASK)

#define	IsIllegal(x)	((x)->status & CONF_ILLEGAL)

/* aConfItem->flags */

#define CONF_FLAGS_LIMIT_IP		0x0001
#define CONF_FLAGS_NO_TILDE		0x0002
#define CONF_FLAGS_NEED_IDENTD		0x0004
#define CONF_FLAGS_PASS_IDENTD		0x0008
#define CONF_FLAGS_NOMATCH_IP		0x0010
#define CONF_FLAGS_E_LINED		0x0020
#define CONF_FLAGS_B_LINED		0x0040
#define CONF_FLAGS_F_LINED		0x0080

#ifdef IDLE_CHECK
#define CONF_FLAGS_IDLE_LINED		0x0100
#endif

#define CONF_FLAGS_DO_IDENTD		0x0200
#define CONF_FLAGS_ALLOW_AUTO_CONN	0x0400
#define CONF_FLAGS_ZIP_LINK		0x0800
#define CONF_FLAGS_SPOOF_IP		0x1000

#ifdef LITTLE_I_LINES
#define CONF_FLAGS_LITTLE_I_LINE	0x8000
#endif

#ifdef ZIP_LINKS
/* the minimum amount of data needed to trigger compression */
#define ZIP_MINIMUM     4096

/* the maximum amount of data to be compressed (can actually be a bit more) */
#define ZIP_MAXIMUM     8192    /* WARNING: *DON'T* CHANGE THIS!!!! */

struct Zdata {
        z_stream        *in;            /* input zip stream data */
        z_stream        *out;           /* output zip stream data */
        char            inbuf[ZIP_MAXIMUM]; /* incoming zipped buffer */
        char            outbuf[ZIP_MAXIMUM]; /* outgoing (unzipped) buffer */
        int             incount;        /* size of inbuf content */
        int             outcount;       /* size of outbuf content */
};

#endif /* ZIP_LINKS */

/* Macros for aConfItem */

#define IsLimitIp(x)            ((x)->flags & CONF_FLAGS_LIMIT_IP)
#define IsNoTilde(x)            ((x)->flags & CONF_FLAGS_NO_TILDE)
#define IsNeedIdentd(x)         ((x)->flags & CONF_FLAGS_NEED_IDENTD)
#define IsPassIdentd(x)         ((x)->flags & CONF_FLAGS_PASS_IDENTD)
#define IsNoMatchIp(x)          ((x)->flags & CONF_FLAGS_NOMATCH_IP)
#define IsConfElined(x)		((x)->flags & CONF_FLAGS_E_LINED)
#define IsConfBlined(x)		((x)->flags & CONF_FLAGS_B_LINED)
#define IsConfFlined(x)		((x)->flags & CONF_FLAGS_F_LINED)

#ifdef IDLE_CHECK
#define IsConfIdlelined(x)	((x)->flags & CONF_FLAGS_IDLE_LINED)
#endif

#define IsConfDoIdentd(x)	((x)->flags & CONF_FLAGS_DO_IDENTD)
#define IsConfDoSpoofIp(x)	((x)->flags & CONF_FLAGS_SPOOF_IP)
#ifdef LITTLE_I_LINES
#define IsConfLittleI(x)	((x)->flags & CONF_FLAGS_LITTLE_I_LINE)
#endif

/* port definitions for Opers */

#define CONF_OPER_GLOBAL_KILL 1
#define CONF_OPER_REMOTE      2
#define CONF_OPER_UNKLINE     4
#define CONF_OPER_GLINE	      8
#define CONF_OPER_N	     16
#define CONF_OPER_K	     32

/*
 * Client structures
 */
struct	User
{
  struct User *next;	/* chain of anUser structures */
  Link	*channel;	/* chain of channel pointer blocks */
  Link	*invited;	/* chain of invite pointer blocks */
  char	*away;		/* pointer to away message */
  time_t last;
  int	refcnt;		/* Number of times this block is referenced */
  int	joined;		/* number of channels joined */
  char	username[USERLEN+1];
  char	host[HOSTLEN+1];
  char	id[IDLEN+1];	/* for future use *hint* */
  char	*server;	/* pointer to scached server name */
  /*
  ** In a perfect world the 'server' name
  ** should not be needed, a pointer to the
  ** client describing the server is enough.
  ** Unfortunately, in reality, server may
  ** not yet be in links while USER is
  ** introduced... --msa
  */
  /* with auto-removal of dependent links, this may no longer be the
  ** case, but it's already fixed by the scache anyway  -orabidoo
  */
};

struct	Server
{
  anUser *user;		/* who activated this connection */
  char	*up;		/* Pointer to scache name */
  char	by[NICKLEN+1];
  aConfItem *nline;	/* N-line pointer for this server */
  aClient *servers;	/* Servers on this server */
  aClient *users;	/* Users on this server */
};

struct Client
{
  struct	Client *next,*prev, *hnext, *idhnext;

/* QS */

  struct	Client *lnext;	/* Used for Server->servers/users */
  struct	Client *lprev;	/* Used for Server->servers/users */

/* LINKLIST */
  /* N.B. next_local_client, and previous_local_client
   * duplicate the link list referenced to by struct Server -> users
   * someday, we'll rationalize this... -Dianora
   */

  struct        Client *next_local_client;      /* keep track of these */
  struct	Client *previous_local_client;

  struct        Client *next_server_client;
  struct        Client *next_oper_client;

  anUser	*user;		/* ...defined, if this is a User */
  aServer	*serv;		/* ...defined, if this is a server */
  struct Client *servptr;	/* Points to server this Client is on */

  aWhowas 	*whowas;	/* Pointers to whowas structs */
  time_t	lasttime;	/* ...should be only LOCAL clients? --msa */
  time_t	firsttime;	/* time client was created */
  time_t	since;		/* last time we parsed something */
  ts_val	tsinfo;		/* TS on the nick, SVINFO on servers */
  long		flags;		/* client flags */
  long		flags2;		/* ugh. overflow */
  aClient	*from;		/* == self, if Local Client, *NEVER* NULL! */
  int	fd;			/* >= 0, for local clients */
  int	hopcount;		/* number of servers to this 0 = local */
  short	status;			/* Client type */
  char	nicksent;
  char	name[HOSTLEN+1]; /* Unique name of the client, nick or host */
  char	username[USERLEN+1]; /* username here now for auth stuff */
  char	info[REALLEN+1]; /* Free form additional client information */
#ifdef FLUD
  Link	*fludees;
#endif
  /*
  ** The following fields are allocated only for local clients
  ** (directly connected to *this* server with a socket.
  ** The first of them *MUST* be the "count"--it is the field
  ** to which the allocation is tied to! *Never* refer to
  ** these fields, if (from != self).
  */
  int	count;		/* Amount of data in buffer */
#ifdef FLUD
  time_t fludblock;
  struct fludbot *fluders;
#endif
#ifdef ANTI_SPAMBOT
  time_t last_join_time;   /* when this client last joined a channel */
  time_t last_leave_time;  /* when this client last left a channel */
  int	 join_leave_count; /* count of JOIN/LEAVE in less than 
			      MIN_JOIN_LEAVE_TIME seconds */
  int    oper_warn_count_down; /* warn opers of this possible spambot
				  every time this gets to 0 */
#endif
#ifdef ANTI_DRONE_FLOOD
  time_t first_received_message_time;
  int	 received_number_of_privmsgs;
  int	drone_noticed;
#endif
  char	buffer[BUFSIZE]; /* Incoming message buffer */
#ifdef ZIP_LINKS
  aZdata *zip;		/* zip data */
#endif
  short	lastsq;		/* # of 2k blocks when sendqueued called last*/
  dbuf	sendQ;		/* Outgoing message queue--if socket full */
  dbuf	recvQ;		/* Hold for data incoming yet to be parsed */
  long	sendM;		/* Statistics: protocol messages send */
  long	sendK;		/* Statistics: total k-bytes send */
  long	receiveM;	/* Statistics: protocol messages received */
  long	receiveK;	/* Statistics: total k-bytes received */
  u_short	sendB;		/* counters to count upto 1-k lots of bytes */
  u_short	receiveB;	/* sent and received. */
  long		lastrecvM;	/* to check for activity --Mika */
  int		priority;
  aClient	*acpt;		/* listening client which we accepted from */
  Link		*confs;		/* Configuration record associated */
  int		authfd;		/* fd for rfc931 authentication */
  struct	in_addr	ip;	/* keep real ip# too */
  unsigned short	port;	/* and the remote port# too :-) */
  struct	hostent	*hostp;
#ifdef	pyr
  struct	timeval	lw;
#endif
#ifdef ANTI_NICK_FLOOD
  time_t	last_nick_change;
  int		number_of_nick_changes;
#endif
  time_t	last_knock;	/* don't allow knock to flood */
  char	sockhost[HOSTLEN+1]; /* This is the host name from the socket
			     ** and after which the connection was
			     ** accepted.
			     */
  char	passwd[PASSWDLEN+1];
  int	caps;			/* capabilities bit-field */
};

#define	CLIENT_LOCAL_SIZE sizeof(aClient)
#define	CLIENT_REMOTE_SIZE offsetof(aClient,count)

/*
 * statistics structures
 */
struct	stats {
	unsigned int	is_cl;	/* number of client connections */
	unsigned int	is_sv;	/* number of server connections */
	unsigned int	is_ni;	/* connection but no idea who it was */
	unsigned short	is_cbs;	/* bytes sent to clients */
	unsigned short	is_cbr;	/* bytes received to clients */
	unsigned short	is_sbs;	/* bytes sent to servers */
	unsigned short	is_sbr;	/* bytes received to servers */
	unsigned long	is_cks;	/* k-bytes sent to clients */
	unsigned long	is_ckr;	/* k-bytes received to clients */
	unsigned long	is_sks;	/* k-bytes sent to servers */
	unsigned long	is_skr;	/* k-bytes received to servers */
	time_t 		is_cti;	/* time spent connected by clients */
	time_t		is_sti;	/* time spent connected by servers */
	unsigned int	is_ac;	/* connections accepted */
	unsigned int	is_ref;	/* accepts refused */
	unsigned int	is_unco; /* unknown commands */
	unsigned int	is_wrdi; /* command going in wrong direction */
	unsigned int	is_unpf; /* unknown prefix */
	unsigned int	is_empt; /* empty message */
	unsigned int	is_num;	/* numeric message */
	unsigned int	is_kill; /* number of kills generated on collisions */
	unsigned int	is_fake; /* MODE 'fakes' */
	unsigned int	is_asuc; /* successful auth requests */
	unsigned int	is_abad; /* bad auth requests */
	unsigned int	is_udp;	/* packets recv'd on udp port */
	unsigned int	is_loc;	/* local connections made */
#ifdef FLUD
	unsigned int	is_flud;	/* users/channels flood protected */
#endif /* FLUD */
};

/* mode structure for channels */

struct	SMode
{
  unsigned int	mode;
  int	limit;
  char	key[KEYLEN+1];
};

/* Message table structure */

struct	Message
{
  char	*cmd;
  int	(* func)();
  unsigned int	count;			/* number of times command used */
  int	parameters;
  char	flags;
  /* bit 0 set means that this command is allowed to be used
   * only on the average of once per 2 seconds -SRB */

  /* I could have defined other bit maps to above instead of the next two
     flags that I added. so sue me. -Dianora */

  char    allow_unregistered_use;	/* flag if this command can be
					   used if unregistered */

  char    reset_idle;			/* flag if this command causes
					   idle time to be reset */
  unsigned long	bytes;
};

typedef struct msg_tree
{
  char *final;
  struct Message *msg;
  struct msg_tree *pointers[26];
} MESSAGE_TREE;

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
typedef struct Ban	/* also used for exceptions -orabidoo */
{
  char *banstr;
  char *who;
  time_t when;
} aBan;
#endif

/* general link structure used for chains */

struct SLink
{
  struct	SLink	*next;
  union
  {
    aClient   *cptr;
    aChannel  *chptr;
    aConfItem *aconf;
#ifdef BAN_INFO
    aBan      *banptr;
#endif
    char      *cp;
  } value;
  int	flags;
};


/* this should eliminate a lot of ifdef's in the main code... -orabidoo */
#ifdef BAN_INFO
#  define BANSTR(l)  ((l)->value.banptr->banstr)
#else
#  define BANSTR(l)  ((l)->value.cp)
#endif

/* channel structure */

struct Channel
{
  struct	Channel *nextch, *prevch, *hnextch;
  Mode	mode;
  char	topic[TOPICLEN+1];
#ifdef TOPIC_INFO
  char    topic_nick[NICKLEN+1];
  time_t  topic_time;
#endif
  int	users;
  Link	*members;
  Link	*invites;
  Link	*banlist;
  Link  *exceptlist;
  ts_val channelts;
  int locally_created;	/* used only to flag a locally created channel */
  int keep_their_modes;	/* used only on mode after sjoin */
#ifdef FLUD
  time_t fludblock;
  struct fludbot *fluders;
#endif
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
  struct Channel *last_empty_channel, *next_empty_channel;
#endif
  char	chname[1];
};

#define	TS_CURRENT	3	/* current TS protocol version */
#define	TS_MIN		1	/* minimum supported TS protocol version */
#define	TS_DOESTS	0x20000000
#define	DoesTS(x)	((x)->tsinfo == TS_DOESTS)

#define CAP_CAP		0x00000001	/* received a CAP to begin with */
#define CAP_QS          0x00000002	/* Can handle quit storm removal */
#define CAP_ZIP         0x00000004	/* Can do server compresion */
#define CAP_EX		0x00000008	/* Can do channel +e exemptions */
#define CAP_CHW		0x00000010	/* Can do channel wall @# */

#define DoesCAP(x)      ((x)->caps)

struct  Capability
{
  char	*name;
  int	cap;
};

/*
 * Capability macros.
 */
#define IsCapable(x, cap)       ((x)->caps & (cap))
#define SetCapable(x, cap)      ((x)->caps |= (cap))
#define ClearCap(x, cap)        ((x)->caps &= ~(cap))

/*
** list of recognized server capabilities.  "TS" is not on the list
** because all servers that we talk to already do TS, and the kludged
** extra argument to "PASS" takes care of checking that.  -orabidoo
*/

#ifdef CAPTAB
struct Capability captab[] = 
{
/*  name        cap     */  

#ifdef ZIP_LINKS
  { "ZIP",      CAP_ZIP },
#endif
  { "QS",       CAP_QS },
  { "EX",       CAP_EX },
  { "CHW",	CAP_CHW },
  { (char*)0,   0 }
};
#else
extern struct Capability captab[];
#endif

/*
** Channel Related macros follow
*/

/* Channel related flags */

#define	CHFL_CHANOP     0x0001 /* Channel operator */
#define	CHFL_VOICE      0x0002 /* the power to speak */
#define	CHFL_DEOPPED	0x0004 /* deopped by us, modes need to be bounced */
#define	CHFL_BAN	0x0008 /* ban channel flag */
#define CHFL_EXCEPTION  0x0010 /* exception to ban channel flag */

/* Channel Visibility macros */

#define	MODE_CHANOP	CHFL_CHANOP
#define	MODE_VOICE	CHFL_VOICE
#define	MODE_DEOPPED	CHFL_DEOPPED
#define	MODE_PRIVATE	0x0008
#define	MODE_SECRET	0x0010
#define	MODE_MODERATED  0x0020
#define	MODE_TOPICLIMIT 0x0040
#define	MODE_INVITEONLY 0x0080
#define	MODE_NOPRIVMSGS 0x0100
#define	MODE_KEY	0x0200
#define	MODE_BAN	0x0400
#define MODE_EXCEPTION  0x0800

#define	MODE_LIMIT	0x1000	/* was 0x0800 */
#define	MODE_FLAGS	0x1fff	/* was 0x0fff */

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
#define MODE_SPLIT	0x1000
#endif

#ifdef JUPE_CHANNEL
#define MODE_JUPED	0x2000
#endif

/*
 * mode flags which take another parameter (With PARAmeterS)
 */
#define	MODE_WPARAS (MODE_CHANOP|MODE_VOICE|MODE_BAN|\
		     MODE_EXCEPTION|MODE_KEY|MODE_LIMIT)
/*
 * Undefined here, these are used in conjunction with the above modes in
 * the source.
#define	MODE_QUERY	0x10000000
#define	MODE_DEL       0x40000000
#define	MODE_ADD       0x80000000
 */

#define	HoldChannel(x)		(!(x))
/* name invisible */
#define	SecretChannel(x)	((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define	HiddenChannel(x)	((x) && ((x)->mode.mode & MODE_PRIVATE))
/* channel visible */
#define	ShowChannel(v,c)	(PubChannel(c) || IsMember((v),(c)))
#define	PubChannel(x)		((!x) || ((x)->mode.mode &\
				 (MODE_PRIVATE | MODE_SECRET)) == 0)

/* #define	IsMember(user,chan) (find_user_link((chan)->members,user) ? 1 : 0)
*/
#define IsMember(blah,chan) ((blah && blah->user && \
		find_channel_link((blah->user)->channel, chan)) ? 1 : 0)

#define	IsChannelName(name) ((name) && (*(name) == '#' || *(name) == '&'))

/* Misc macros */

#define	BadPtr(x) (!(x) || (*(x) == '\0'))

#define	isvalid(c) (((c) >= 'A' && (c) <= '~') || isdigit(c) || (c) == '-')

#define	MyConnect(x)			((x)->fd >= 0)
#define	MyClient(x)			(MyConnect(x) && IsClient(x))
#define	MyOper(x)			(MyConnect(x) && IsOper(x))

/* String manipulation macros */

/* strncopynt --> strncpyzt to avoid confusion, sematics changed
   N must be now the number of bytes in the array --msa */
#define	strncpyzt(x, y, N) do{(void)strncpy(x,y,N);x[N-1]='\0';}while(0)
#define	StrEq(x,y)	(!strcmp((x),(y)))

/* used in SetMode() in channel.c and m_umode() in s_msg.c */

#define	MODE_NULL      0
#define	MODE_QUERY     0x10000000
#define	MODE_ADD       0x40000000
#define	MODE_DEL       0x20000000

/* return values for hunt_server() */

#define	HUNTED_NOSUCH	(-1)	/* if the hunted server is not found */
#define	HUNTED_ISME	0	/* if this server should execute the command */
#define	HUNTED_PASS	1	/* if message passed onwards successfully */

/* used when sending to #mask or $mask */

#define	MATCH_SERVER  1
#define	MATCH_HOST    2

/* misc variable externs */

extern	char	*version, *serno, *infotext[];
extern	char	*generation, *creation;

/* misc defines */

#define	FLUSH_BUFFER	-2
#define GO_ON		-3	/* for m_nick/m_client's benefit */
#define	UTMP		"/etc/utmp"
#define	COMMA		","

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

/* allow DEFAULT_SERVER_SPLIT_RECOVERY_TIME minutes after server rejoins
 * the network before allowing chanops new channels,
 *  but allow it to be set to a maximum of MAX_SERVER_SPLIT_RECOVERY_TIME 
 */

#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT_SIMPLE)
#define MAX_SERVER_SPLIT_RECOVERY_TIME 30
#ifndef DEFAULT_SERVER_SPLIT_RECOVERY_TIME
#define DEFAULT_SERVER_SPLIT_RECOVERY_TIME 15
#endif /* DEFAULT_SERVER_SPLIT_RECOVERY_TIME */
#endif

typedef struct
{
  int maxclients;	/* max clients allowed */
  int autoconn;		/* autoconn enabled for all servers? */
  int noisy_htm;        /* noisy htm or not ? */
  int lifesux;

#ifdef IDLE_CHECK
  int idletime;
#endif

#ifdef FLUD
  int fludnum;
  int fludtime;
  int fludblock;
#endif

#ifdef ANTI_DRONE_FLOOD
  int dronetime;
  int dronecount;
#endif

#ifdef NEED_SPLITCODE
  time_t server_split_recovery_time;
  int split_smallnet_size;
  int split_smallnet_users;
#endif

#ifdef ANTI_SPAMBOT
  int spam_num;
  int spam_time;
#endif

}SetOptionsType;

#ifdef FLUD
struct fludbot {
        struct Client   *fluder;
        int             count;
        time_t          first_msg, last_msg;
        struct fludbot  *next;
};
#endif /* FLUD */

#ifdef GLINES
typedef struct gline_pending
{
  char oper_nick1[NICKLEN+1];
  char oper_user1[USERLEN+1];
  char oper_host1[HOSTLEN+1];
  char *oper_server1;		/* point to scache */
  char *reason1;
  time_t time_request1;

  char oper_nick2[NICKLEN+1];
  char oper_user2[USERLEN+1];
  char oper_host2[HOSTLEN+1];
  char *oper_server2;		/* point to scache */
  char *reason2;
  time_t time_request2;
  
  time_t last_gline_time;	/* for expiring entry */
  char user[USERLEN+1];
  char host[HOSTLEN+1];

  struct gline_pending *next;
}GLINE_PENDING;

/* how long a pending G line can be around
 *   10 minutes should be plenty
 */

#define GLINE_PENDING_EXPIRE 600
#endif

#endif /* INCLUDED_struct_h */
