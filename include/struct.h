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
# if defined( HAVE_SYS_SYSLOG_H )
#  ifndef INCLUDED_sys_syslog_h
#   include <sys/syslog.h>
#   define INCLUDED_sys_syslog_h
#  endif
# endif
#endif

#ifndef INCLUDED_ircd_defs_h
# include "ircd_defs.h"
#endif

#ifndef INCLUDED_zlib_h
# include "zlib.h"
# define INCLUDED_zlib_h
#endif

#ifndef INCLUDED_client_h
# include "client.h"
#endif

#define REPORT_DLINED  "NOTICE DLINE :*** You have been D-lined\r\n"

typedef struct  ConfItem aConfItem;
typedef	struct 	Client	aClient;
typedef	struct	Channel	aChannel;
typedef	struct	User	anUser;
typedef	struct	Server	aServer;
typedef	struct	SLink	Link;
typedef	struct	SMode	Mode;
typedef struct  Zdata   aZdata;

/*
** flags for bootup options (command line flags)
*/
#define	BOOT_CONSOLE	1
#define	BOOT_DEBUG	4
#define	BOOT_TTY	16
#define BOOT_STDERR	128

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
#define CONF_OPER_REHASH     64
#define CONF_OPER_DIE       128

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
  time_t channelts;
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

/* String manipulation macros */

/* strncopynt --> strncpyzt to avoid confusion, sematics changed
   N must be now the number of bytes in the array --msa */
#define	strncpyzt(x, y, N) \
  do { strncpy((x),(y),(N)); x[(N) - 1]='\0'; } while(0)

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
