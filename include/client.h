/* - Internet Relay Chat, include/client.h
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
#ifndef	INCLUDED_client_h
#define INCLUDED_client_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif
#if !defined(CONFIG_H_LEVEL_6)
#error Incorrect config.h for this revision of ircd.
#endif
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>       /* time_t */
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_netinet_in_h
#include <netinet/in.h>      /* in_addr */
#define INCLUDED_netinet_in_h
#endif
#ifndef INCLUDED_ircd_defs_h
# include "ircd_defs.h"
#endif
#ifndef INCLUDED_dbuf_h
#include "dbuf.h"
#endif

#define	HOSTLEN		63	/* Length of hostname.  Updated to         */
				/* comply with RFC1123                     */
#define HOSTIPLEN	15	/* Length of dotted quad form of IP	   */
				/* - Dianora 				   */
#define	NICKLEN		9	/* Necessary to put 9 here instead of 10
				 * if s_msg.c/m_nick has been corrected.
				 * This preserves compatibility with old
				 * servers --msa
				 */
#define	USERLEN		10
#define	REALLEN	 	50
#define	PASSWDLEN 	20
#define IDLEN		12	/* this is the maximum length, not the actual
				   generated length; DO NOT CHANGE! */
#define CLIENT_BUFSIZE 512      /* must be at least 512 bytes */

/*
 * status macros.
 */
#define	STAT_LOG	-6	/* logfile for -x */
#define	STAT_MASTER	-5	/* Local ircd master before identification */
#define	STAT_CONNECTING	-4
#define	STAT_HANDSHAKE	-3
#define	STAT_ME		-2
#define	STAT_UNKNOWN	-1
#define	STAT_SERVER	0
#define	STAT_CLIENT	1


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


#define	FLAGS_PINGSENT     0x0001 /* Unreplied ping sent */
#define	FLAGS_DEADSOCKET   0x0002 /* Local socket is dead--Exiting soon */
#define	FLAGS_KILLED       0x0004 /* Prevents "QUIT" from being sent for this */
#define	FLAGS_OPER         0x0008 /* Operator */
#define	FLAGS_LOCOP        0x0010 /* Local operator -- SRB */
#define	FLAGS_INVISIBLE    0x0020 /* makes user invisible */
#define	FLAGS_WALLOP       0x0040 /* send wallops to them */
#define	FLAGS_SERVNOTICE   0x0080 /* server notices such as kill */
#define	FLAGS_BLOCKED      0x0100 /* socket is in a blocked condition */
#define FLAGS_REJECT_HOLD  0x0200 /* client has been klined */
#define	FLAGS_CLOSING      0x0400 /* set when closing to suppress errors */
#define	FLAGS_LISTEN       0x0800 /* used to mark clients which we listen on */
#define	FLAGS_CHKACCESS    0x1000 /* ok to check clients access if set */
/* #define FLAGS_DOINGDNS  0x2000    UNUSED */
/* #define FLAGS_AUTH	   0x4000    UNUSED */
/* #define FLAGS_WRAUTH	   0x8000    UNUSED */
#define	FLAGS_LOCAL	  0x10000 /* set for local clients */
#define	FLAGS_GOTID	  0x20000 /* successful ident lookup achieved */
#define	FLAGS_NEEDID	  0x40000 /* I-lines say must use ident return */
#define	FLAGS_NONL	  0x80000 /* No \n in buffer */
#define FLAGS_CCONN      0x100000 /* Client Connections */
#define FLAGS_REJ        0x200000 /* Bot Rejections */
#define FLAGS_SKILL      0x400000 /* Server Killed */
#define FLAGS_FULL       0x800000 /* Full messages */
#define FLAGS_NORMALEX  0x1000000 /* Client exited normally */
#define	FLAGS_NCHANGE   0x2000000 /* Nick change notice */
#define	FLAGS_SPY       0x4000000 /* see STATS / LINKS */
#define FLAGS_DEBUG     0x8000000 /* 'debugging' info */
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
#define FLAGS2_OPER_DIE         0x0800  /* oper can die */
#define FLAGS2_OPER_REHASH      0x1000  /* oper can rehash */
#define FLAGS2_OPER_FLAGS	(FLAGS2_OPER_GLOBAL_KILL | \
				 FLAGS2_OPER_REMOTE | \
				 FLAGS2_OPER_UNKLINE | \
				 FLAGS2_OPER_GLINE | \
				 FLAGS2_OPER_N | \
				 FLAGS2_OPER_K | \
                                 FLAGS2_OPER_REHASH)
/* ZIP_LINKS */

#define FLAGS2_ZIP	     0x4000  /* (server) link is zipped */
#define FLAGS2_ZIPFIRST	     0x8000  /* start of zip (ignore any CR/LF) */
#define FLAGS2_CBURST	    0x10000  /* connection burst being sent */

#define FLAGS2_DOINGLIST    0x20000  /* client is doing a list */
#ifdef IDLE_CHECK
#define FLAGS2_IDLE_LINED   0x40000
#endif
#define FLAGS2_ALREADY_EXITED	0x80000         /* kludge grrrr */
#define FLAGS2_IP_SPOOFING	0x100000	/* client IP is spoofed */
#define FLAGS2_IP_HIDDEN	0x200000	/* client IP should be hidden
					           from non opers */


#define	SEND_UMODES  (FLAGS_INVISIBLE | FLAGS_OPER | FLAGS_WALLOP)
#define	ALL_UMODES   (SEND_UMODES | FLAGS_SERVNOTICE | FLAGS_CCONN | \
                      FLAGS_REJ | FLAGS_SKILL | FLAGS_FULL | FLAGS_SPY | \
                      FLAGS_NCHANGE | FLAGS_OPERWALL | FLAGS_DEBUG)

#ifndef OPER_UMODES
#define OPER_UMODES  (FLAGS_OPER | FLAGS_WALLOP | FLAGS_SERVNOTICE | \
                      FLAGS_SPY | FLAGS_OPERWALL | FLAGS_DEBUG)
#endif /* OPER_UMODES */
#ifndef LOCOP_UMODES
#define LOCOP_UMODES (FLAGS_LOCOP | FLAGS_WALLOP | FLAGS_SERVNOTICE | \
                      FLAGS_SPY | FLAGS_DEBUG)
#endif /* LOCOP_UMODES */
#define	FLAGS_ID     (FLAGS_NEEDID | FLAGS_GOTID)

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
#define	IsListening(x)		((x)->flags & FLAGS_LISTEN)
#define	DoAccess(x)		((x)->flags & FLAGS_CHKACCESS)
#define	IsLocal(x)		((x)->flags & FLAGS_LOCAL)
#define	IsDead(x)		((x)->flags & FLAGS_DEADSOCKET)
#define	SetOper(x)		((x)->flags |= FLAGS_OPER)
#define	SetLocOp(x)    		((x)->flags |= FLAGS_LOCOP)
#define	SetInvisible(x)		((x)->flags |= FLAGS_INVISIBLE)
#define	SetWallops(x)  		((x)->flags |= FLAGS_WALLOP)
#define	SetAccess(x)		((x)->flags |= FLAGS_CHKACCESS)
#define	NoNewLine(x)		((x)->flags & FLAGS_NONL)
#define	ClearOper(x)		((x)->flags &= ~FLAGS_OPER)
#define ClearLocOp(x)		((x)->flags &= ~FLAGS_LOCOP)
#define	ClearInvisible(x)	((x)->flags &= ~FLAGS_INVISIBLE)
#define	ClearWallops(x)		((x)->flags &= ~FLAGS_WALLOP)
#define	ClearAccess(x)		((x)->flags &= ~FLAGS_CHKACCESS)

#ifdef REJECT_HOLD
#define IsRejectHeld(x)	        ((x)->flags & FLAGS_REJECT_HOLD)
#define SetRejectHold(x)        ((x)->flags |= FLAGS_REJECT_HOLD)
#endif

#define SetIpHash		((x)->flags |= FLAGS_IPHASH)
#define ClearIpHash		((x)->flags &= ~FLAGS_IPHASH)
#define IsIpHash		((x)->flags & FLAGS_IPHASH)

#define SetNeedId(x)		((x)->flags |= FLAGS_NEEDID)
#define IsNeedId(x)             (((x)->flags & FLAGS_NEEDID) != 0)

#define SetGotId(x)		((x)->flags |= FLAGS_GOTID)
#define IsGotId(x)		(((x)->flags & FLAGS_GOTID) != 0)

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
#define SetOperDie(x)           ((x)->flags2 |= FLAGS2_OPER_DIE)
#define IsOperDie(x)            ((x)->flags2 & FLAGS2_OPER_DIE)
#define SetOperRehash(x)        ((x)->flags2 |= FLAGS2_OPER_REHASH)
#define IsOperRehash(x)         ((x)->flags2 &= FLAGS2_OPER_REHASH)
#define CBurst(x)		((x)->flags2 & FLAGS2_CBURST)

/*
 * pre declare structs
 */
struct SLink;
struct ConfItem;
struct Whowas;
struct fludbot;
struct Zdata;
struct Client;

/*
 * Client structures
 */
struct User
{
  struct User*   next;		/* chain of anUser structures */
  struct SLink*  channel;	/* chain of channel pointer blocks */
  struct SLink*  invited;	/* chain of invite pointer blocks */
  char*          away;		/* pointer to away message */
  time_t         last;
  int	         refcnt;	/* Number of times this block is referenced */
  int	         joined;	/* number of channels joined */
  char	         username[USERLEN + 1];
  char	         host[HOSTLEN + 1];
  char	         id[IDLEN + 1];	/* for future use *hint* */
  char*          server;	/* pointer to scached server name */
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

struct Server
{
  struct User*     user;	/* who activated this connection */
  char*            up;		/* Pointer to scache name */
  char	           by[NICKLEN + 1];
  struct ConfItem* nline;	/* N-line pointer for this server */
  struct Client*   servers;	/* Servers on this server */
  struct Client*   users;	/* Users on this server */
};

struct Client
{
  struct Client*    next;
  struct Client*    prev;
  struct Client*    hnext;
  struct Client*    idhnext;

/* QS */

  struct Client*    lnext;	/* Used for Server->servers/users */
  struct Client*    lprev;	/* Used for Server->servers/users */

/* LINKLIST */
  /* N.B. next_local_client, and previous_local_client
   * duplicate the link list referenced to by struct Server -> users
   * someday, we'll rationalize this... -Dianora
   */

  struct Client*    next_local_client;      /* keep track of these */
  struct Client*    previous_local_client;

  struct Client*    next_server_client;
  struct Client*    next_oper_client;

  struct User*      user;	/* ...defined, if this is a User */
  struct Server*    serv;	/* ...defined, if this is a server */
  struct Client*    servptr;	/* Points to server this Client is on */
  struct Client*    from;	/* == self, if Local Client, *NEVER* NULL! */

  struct Whowas*    whowas;	/* Pointers to whowas structs */
  time_t	    lasttime;	/* ...should be only LOCAL clients? --msa */
  time_t	    firsttime;	/* time client was created */
  time_t	    since;	/* last time we parsed something */
  time_t	    tsinfo;	/* TS on the nick, SVINFO on servers */
  unsigned int	    flags;	/* client flags */
  unsigned int      flags2;	/* ugh. overflow */
  int	            fd;  	/* >= 0, for local clients */
  int	            hopcount;	/* number of servers to this 0 = local */
  /* XXX - alignment? */
  short	            status;	/* Client type */
  char	            nicksent;
  char	            name[HOSTLEN + 1]; /* Unique name of the client, 
                                          nick or host */
  char	            username[USERLEN + 1]; /* username for auth stuff */
  char	            info[REALLEN + 1]; /* Free form additional client info */
#ifdef FLUD
  struct SLink*     fludees;
#endif
  /*
  ** The following fields are allocated only for local clients
  ** (directly connected to *this* server with a socket.
  ** The first of them *MUST* be the "count"--it is the field
  ** to which the allocation is tied to! *Never* refer to
  ** these fields, if (from != self).
  */
  int	            count;	 /* Amount of data in buffer */
#ifdef FLUD
  time_t            fludblock;
  struct fludbot*   fluders;
#endif
#ifdef ANTI_SPAMBOT
  time_t            last_join_time;   /* when this client last 
                                         joined a channel */
  time_t            last_leave_time;  /* when this client last 
                                       * left a channel */
  int	            join_leave_count; /* count of JOIN/LEAVE in less than 
			                 MIN_JOIN_LEAVE_TIME seconds */
  int               oper_warn_count_down; /* warn opers of this possible 
                                          spambot every time this gets to 0 */
#endif
#ifdef ANTI_DRONE_FLOOD
  time_t            first_received_message_time;
  int	            received_number_of_privmsgs;
  int	            drone_noticed;
#endif
  char	buffer[CLIENT_BUFSIZE]; /* Incoming message buffer */
#ifdef ZIP_LINKS
  struct Zdata*     zip;	/* zip data */
#endif
  short	            lastsq;	/* # of 2k blocks when sendqueued called last*/
  dbuf	            sendQ;	/* Outgoing message queue--if socket full */
  dbuf	            recvQ;	/* Hold for data incoming yet to be parsed */
  unsigned int      sendM;	/* Statistics: protocol messages send */
  unsigned int      sendK;	/* Statistics: total k-bytes send */
  unsigned int      receiveM;	/* Statistics: protocol messages received */
  unsigned int      receiveK;	/* Statistics: total k-bytes received */
  unsigned short    sendB;	/* counters to count upto 1-k lots of bytes */
  unsigned short    receiveB;	/* sent and received. */
  unsigned int	    lastrecvM;	/* to check for activity --Mika */
  int		    priority;
  struct Client*    acpt;	/* listening client which we accepted from */
  struct SLink*     confs;	/* Configuration record associated */
  struct in_addr    ip;		/* keep real ip# too */
  unsigned short    port;	/* and the remote port# too :-) */
  struct hostent*   hostp;
#ifdef ANTI_NICK_FLOOD
  time_t	    last_nick_change;
  int		    number_of_nick_changes;
#endif
  time_t	    last_knock;	/* don't allow knock to flood */
  char	            sockhost[HOSTLEN + 1]; /* This is the host name from the 
                                              socket or the resolved name */
  char	            passwd[PASSWDLEN + 1];
  int	            caps;	/* capabilities bit-field */
};

/*
 * 'offsetof' is defined in ANSI-C. The following definition
 * is not absolutely portable (I have been told), but so far
 * it has worked on all machines I have needed it. The type
 * should be size_t but...  --msa
 */
#ifndef offsetof
#define	offsetof(t, m) (size_t)((&((t *)0L)->m))
#endif

#define	CLIENT_LOCAL_SIZE sizeof(struct Client)
#define	CLIENT_REMOTE_SIZE offsetof(struct Client, count)

#endif /* INCLUDED_client_h */
