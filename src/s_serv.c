/************************************************************************
 *   IRC - Internet Relay Chat, src/s_serv.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers. 
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

#ifndef lint
static  char sccsid[] = "@(#)s_serv.c	2.55 2/7/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";


static char *rcs_version = "$Id$";
#endif


#define CAPTAB
#include "struct.h"
#undef CAPTAB

#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#ifndef __EMX__
#include <utmp.h> /* old slackware utmp.h defines BYTE_ORDER */
#endif /* __EMX__ */
#include "nameser.h" /* and nameser.h checks to see if its defined */
#include "resolv.h"

#if defined(AIX) || defined(DYNIXPTX) || defined(SVR3)
#include <time.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include "h.h"
#if defined( HAVE_STRING_H )
#include <string.h>
#else
/* older unices don't have strchr/strrchr .. help them out */
#include <strings.h>
#undef strchr
#define strchr index
#endif
#include "dline_conf.h"
#include "mtrie_conf.h"


/* internal variables */
static	char	buf[BUFSIZE]; 

/* external variables */

/* LINKLIST */
extern aClient *local_cptr_list;
extern aClient *oper_cptr_list;
extern aClient *serv_cptr_list;

/* aConfItems */
/* conf uline link list root */
extern aConfItem *u_conf;
/* conf xline link list root */
extern aConfItem *x_conf;
/* conf qline link list root */
extern aConfItem *q_conf;

#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)
extern int server_was_split;
extern time_t server_split_time;
extern int server_split_recovery_time;
#endif

extern int lifesux;		/* defined in ircd.c */
extern int rehashed;		/* defined in ircd.c */
extern int dline_in_progress;	/* defined in ircd.c */
extern int autoconn;		/* defined in ircd.c */
extern int spare_fd;		/* defined in ircd.c */

#ifdef HIGHEST_CONNECTION
int     max_connection_count = 1, max_client_count = 1;
#endif

#ifdef ANTI_SPAMBOT_EXTRA
int spambot_privmsg_count = PRIVMSG_POSSIBLE_SPAMBOT_COUNT;
#endif

extern aConfItem *temporary_klines;	/* defined in s_conf.c */
#ifdef IDLE_CHECK
extern int idle_time;			/* defined in ircd.c */
#endif

/* external functions */
#ifdef MAXBUFFERS
extern	void	reset_sock_opts();
#endif
extern char *smalldate(time_t);		/* defined in s_misc.c */

#if defined(GLINES) || defined(SEPARATE_QUOTE_KLINES_BY_DATE)
extern char *small_file_date(time_t);	/* defined in s_misc.c */
#endif

#ifdef GLINES
extern void add_gline(aConfItem *);		/* defined in s_conf.c */
static void log_gline_request(
			      char *,char *,char *,char *,
			      char *,char *,char *);

void log_gline(aClient *,char *,GLINE_PENDING *,
		      char *,char *,char *,char *,char *,char *,char *);


extern void expire_pending_glines();	/* defined in s_conf.c */

extern int majority_gline(aClient *,
			  char *,char *,
			  char *,char *,
			  char *,char *,char *); /* defined in s_conf.c */
#endif

extern char *oper_privs(aClient *,int);	/* defined in s_conf.c */
extern void outofmemory(void);		/* defined in list.c */
extern void s_die(void);		/* defined in ircd.c as VOIDSIG */
extern int match(char *,char *);	/* defined in match.c */
extern void show_opers(aClient *,char *);   /* defined in s_misc.c */
extern void show_servers(aClient *,char *); /* defined in s_misc.c */
extern void count_memory(aClient *,char *); /* defined in s_debug.c */

/* Local function prototypes */
static int isnumber(char *);	/* return 0 if not, else return number */
static char *cluster(char *);
static void set_autoconn(aClient *,char *,char *,int);
static void report_specials(aClient *,int,int);
static int bad_tld(char *);
static void show_ports(aClient *,char *);

int send_motd(aClient *,aClient *,int, char **,aMessageFile *); 
static int safe_write(aClient *,char *,char *,int,char *);

void read_motd(void);
#ifdef AMOTD
void read_amotd(void);
#endif
void read_help();
int read_message_file(char *,aMessageFile **);

char motd_last_changed_date[MAX_DATE_STRING];	/* enough room for date */

#ifdef OPER_MOTD
char oper_motd_last_changed_date[MAX_DATE_STRING]; /* enough room for date */
void read_oper_motd();
extern aMessageFile *opermotd;
#endif


#ifdef UNKLINE
static int flush_write(aClient *,int,char *,int,char *);
static int remove_tkline_match(char *,char *);
#endif

#ifdef LOCKFILE
/* Shadowfax's lockfile code */
void do_pending_klines(void);

struct pkl {
        char *comment;          /* Kline Comment */
        char *kline;            /* Actual Kline */
        struct pkl *next;       /* Next Pending Kline */
} *pending_klines = NULL;

time_t	pending_kline_time = 0;
#endif


/*
** m_functions execute protocol messages on this server:
**
**	cptr	is always NON-NULL, pointing to a *LOCAL* client
**		structure (with an open socket connected!). This
**		identifies the physical socket where the message
**		originated (or which caused the m_function to be
**		executed--some m_functions may call others...).
**
**	sptr	is the source of the message, defined by the
**		prefix part of the message if present. If not
**		or prefix not found, then sptr==cptr.
**
**		(!IsServer(cptr)) => (cptr == sptr), because
**		prefixes are taken *only* from servers...
**
**		(IsServer(cptr))
**			(sptr == cptr) => the message didn't
**			have the prefix.
**
**			(sptr != cptr && IsServer(sptr) means
**			the prefix specified servername. (?)
**
**			(sptr != cptr && !IsServer(sptr) means
**			that message originated from a remote
**			user (not local).
**
**		combining
**
**		(!IsServer(sptr)) means that, sptr can safely
**		taken as defining the target structure of the
**		message in this server.
**
**	*Always* true (if 'parse' and others are working correct):
**
**	1)	sptr->from == cptr  (note: cptr->from == cptr)
**
**	2)	MyConnect(sptr) <=> sptr == cptr (e.g. sptr
**		*cannot* be a local connection, unless it's
**		actually cptr!). [MyConnect(x) should probably
**		be defined as (x == x->from) --msa ]
**
**	parc	number of variable parameter strings (if zero,
**		parv is allowed to be NULL)
**
**	parv	a NULL terminated list of parameter pointers,
**
**			parv[0], sender (prefix string), if not present
**				this points to an empty string.
**			parv[1]...parv[parc-1]
**				pointers to additional parameters
**			parv[parc] == NULL, *always*
**
**		note:	it is guaranteed that parv[0]..parv[parc-1] are all
**			non-NULL pointers.
*/

/*
** m_version
**	parv[0] = sender prefix
**	parv[1] = remote server
*/
int	m_version(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{
  extern char serveropts[];

#if 0 /* I don't see a point in pacing this */
  static time_t last_used=0L;

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
	return 0;
      else
	last_used = NOW;
    }
#endif /* 0 */

  if(IsAnOper(sptr))
     {
       if (hunt_server(cptr,sptr,":%s VERSION :%s",1,parc,parv)==HUNTED_ISME)
	 sendto_one(sptr, rpl_str(RPL_VERSION), me.name,
		    parv[0], version, debugmode, me.name, serveropts);
     }
   else
     sendto_one(sptr, rpl_str(RPL_VERSION), me.name,
		parv[0], version, debugmode, me.name, serveropts);

  return 0;
}

/*
** m_squit
**	parv[0] = sender prefix
**	parv[1] = server name
**	parv[2] = comment
*/
int	m_squit(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  Reg	aConfItem *aconf;
  char	*server;
  Reg	aClient	*acptr;
  char	*comment = (parc > 2 && parv[2]) ? parv[2] : cptr->name;

  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      server = parv[1];
      /*
      ** To accomodate host masking, a squit for a masked server
      ** name is expanded if the incoming mask is the same as
      ** the server name for that link to the name of link.
      */
      while ((*server == '*') && IsServer(cptr))
	{
	  aconf = cptr->serv->nline;
	  if (!aconf)
	    break;
	  if (!mycmp(server, my_name_for_link(me.name, aconf)))
	    server = cptr->name;
	  break; /* WARNING is normal here */
          /* NOTREACHED */
	}
      /*
      ** The following allows wild cards in SQUIT. Only useful
      ** when the command is issued by an oper.
      */
      for (acptr = client; (acptr = next_client(acptr, server));
	   acptr = acptr->next)
	if (IsServer(acptr) || IsMe(acptr))
	  break;
      if (acptr && IsMe(acptr))
	{
	  acptr = cptr;
	  server = cptr->sockhost;
	}
    }
  else
    {
      /*
      ** This is actually protocol error. But, well, closing
      ** the link is very proper answer to that...
      */
      server = cptr->sockhost;
      acptr = cptr;
    }

  /*
  ** SQUIT semantics is tricky, be careful...
  **
  ** The old (irc2.2PL1 and earlier) code just cleans away the
  ** server client from the links (because it is never true
  ** "cptr == acptr".
  **
  ** This logic here works the same way until "SQUIT host" hits
  ** the server having the target "host" as local link. Then it
  ** will do a real cleanup spewing SQUIT's and QUIT's to all
  ** directions, also to the link from which the orinal SQUIT
  ** came, generating one unnecessary "SQUIT host" back to that
  ** link.
  **
  ** One may think that this could be implemented like
  ** "hunt_server" (e.g. just pass on "SQUIT" without doing
  ** nothing until the server having the link as local is
  ** reached). Unfortunately this wouldn't work in the real life,
  ** because either target may be unreachable or may not comply
  ** with the request. In either case it would leave target in
  ** links--no command to clear it away. So, it's better just
  ** clean out while going forward, just to be sure.
  **
  ** ...of course, even better cleanout would be to QUIT/SQUIT
  ** dependant users/servers already on the way out, but
  ** currently there is not enough information about remote
  ** clients to do this...   --msa
  */
  if (!acptr)
    {
      sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
		 me.name, parv[0], server);
      return 0;
    }
  if (IsLocOp(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (MyClient(sptr) && !IsOperRemote(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no R flag",me.name,parv[0]);
      return 0;
    }

  /*
  **  Notify all opers, if my local link is remotely squitted
  */
  if (MyConnect(acptr) && !IsAnOper(cptr))
    {
      sendto_ops_butone(NULL, &me,
			":%s WALLOPS :Received SQUIT %s from %s (%s)",
			me.name, server, get_client_name(sptr,FALSE), comment);
#if defined(USE_SYSLOG) && defined(SYSLOG_SQUIT)
      syslog(LOG_DEBUG,"SQUIT From %s : %s (%s)",
	     parv[0], server, comment);
#endif
    }
  else if (MyConnect(acptr))
    sendto_ops("Received SQUIT %s from %s (%s)",
	       acptr->name, get_client_name(sptr,FALSE), comment);
  
  return exit_client(cptr, acptr, sptr, comment);
}

/*
** m_svinfo
**	parv[0] = sender prefix
**	parv[1] = TS_CURRENT for the server
**	parv[2] = TS_MIN for the server
**	parv[3] = server is standalone or connected to non-TS only
**	parv[4] = server's idea of UTC time
*/
int	m_svinfo(aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{
  time_t	deltat, tmptime,theirtime;

  if (!IsServer(sptr) || !MyConnect(sptr) || !DoesTS(sptr) || parc < 5)
    return 0;

  if (TS_CURRENT < atoi(parv[2]) || atoi(parv[1]) < TS_MIN)
    {
      /*
      ** a server with the wrong TS version connected; since we're
      ** TS_ONLY we can't fall back to the non-TS protocol so
      ** we drop the link  -orabidoo
      */
      sendto_ops("Link %s dropped, wrong TS protocol version (%s,%s)",
		 get_client_name(sptr, TRUE), parv[1], parv[2]);
      return exit_client(sptr, sptr, sptr, "Incompatible TS version");
    }

  tmptime = time(NULL);
  theirtime = atol(parv[4]);
  deltat = abs(theirtime - tmptime);

  if (deltat > TS_MAX_DELTA)
    {
      sendto_ops("Link %s dropped, excessive TS delta (my TS=%d, their TS=%d, delta=%d)",
		 get_client_name(sptr, TRUE), tmptime, theirtime,deltat);
      return exit_client(sptr, sptr, sptr, "Excessive TS delta");
    }

  if (deltat > TS_WARN_DELTA)
    { 
      sendto_realops("Link %s notable TS delta (my TS=%d, their TS=%d, delta=%d)",
                 get_client_name(sptr, TRUE), tmptime, theirtime, deltat);
    }

  return 0;
}

/*
** m_capab
**	parv[0] = sender prefix
**	parv[1] = space-separated list of capabilities
*/
int	m_capab(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  register	struct Capability *cap;
  char	*p;
  Reg	char *s;
  int   found = NO;

  if ((!IsUnknown(cptr) && !IsHandshake(cptr)) || parc < 2)
    return 0;

  sendto_realops("CAPAB received %s",parv[1]);

  for (s=strtoken(&p, parv[1], " "); s; s=strtoken(&p, NULL, " "))
    {
      for (cap=captab; cap->name; cap++)
	{
	  if (!strcmp(cap->name, s))
	    {
	      cptr->caps |= cap->cap;
	      found = YES;
	      break;
	    }
	 }

      if(!found)
	sendto_realops("CAPAB %s received from %s but not understood",
		       s,get_client_name(cptr,FALSE));

      found = NO;
    }
  
  return 0;
}

/*
** send the CAPAB line to a server  -orabidoo
*
* modified, always send all capabilities -Dianora
*/
void send_capabilities(aClient *cptr,int use_zip)
{
  register struct Capability *cap;
  char	msgbuf[BUFSIZE];

  msgbuf[0] = '\0';

  for (cap = captab; cap->name; cap++)
    {
      /* kludge to rhyme with sludge */

      if(use_zip)
	{
	  strcat(msgbuf, cap->name);
	  strcat(msgbuf, " ");
	}
      else
	{
	  if(cap->cap != CAP_ZIP)
	    {
	      strcat(msgbuf, cap->name);
	      strcat(msgbuf, " ");
	    }
	}
    }
  sendto_one(cptr, "CAPAB :%s", msgbuf);
}

/*
** m_server
**	parv[0] = sender prefix
**	parv[1] = servername
**	parv[2] = serverinfo/hopcount
**      parv[3] = serverinfo
*/
int	m_server(aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{
  Reg	int	i;
  char	info[REALLEN+1], *inpath, *host;
  aClient *acptr, *bcptr;
  aConfItem *aconf;
  int	hop;
  char clean_host[(2*HOSTLEN)+1];

  info[0] = '\0';
  inpath = get_client_name(cptr,FALSE);
  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(cptr,"ERROR :No servername");
      return 0;
    }
  hop = 0;
  host = parv[1];
  if (parc > 3 && atoi(parv[2]))
    {
      hop = atoi(parv[2]);
      strncpyzt(info, parv[3], REALLEN);
    }
  else if (parc > 2)
    {
      strncpyzt(info, parv[2], REALLEN);
      if ((parc > 3) && ((i = strlen(info)) < (REALLEN-2)))
	{
	  (void)strcat(info, " ");
	  (void)strncat(info, parv[3], REALLEN - i - 2);
	  info[REALLEN] = '\0';
	}
    }
  /*
  ** July 5, 1997
  ** Rewritten to throw away server cruft from users,
  ** combined the hostname validity test with
  ** cleanup of host name, so a cleaned up hostname
  ** can be returned as an error if necessary. - Dianora
  */
  /* yes, the if(strlen) below is really needed!! */
  if (strlen(host) > HOSTLEN)
    host[HOSTLEN] = '\0';

  if (IsPerson(cptr))
    {
      /*
      ** A local link that has been identified as a USER
      ** tries something fishy... ;-)
      */
      sendto_one(cptr, err_str(ERR_UNKNOWNCOMMAND),
		 me.name, parv[0], "SERVER");
      /*
	Just ignore it for fripps sake... - Dianora
	      
	sendto_ops("User %s trying to become a server %s",
	get_client_name(cptr, TRUE), host);
	*/
	    
      return 0;
    }
  else
    {
      /* Lets check for bogus names and clean them up
	 we don't bother cleaning up ones from users, becasuse
	 we will never see them any more - Dianora
	 */

      int bogus_server = 0;
      int found_dot = 0;
      char *s;
      char *d;

      s = host;
      d = clean_host;
      while(*s)
	{
	  if(*s < ' ') /* Is it a control character? */
	    {
	      bogus_server = 1;
	      *d++ = '^';
	      *d++ = (*s + 0x40); /* turn it into a printable */
	      s++;
	    }
	  else if(*s > '~')
	    {
	      bogus_server = 1;
	      *d++ = '.';
	      s++;
	    }
	  else
	    {
	      if( *s == '.' )
		found_dot = 1;
	      *d++ = *s++;   
	    }
	}
      *d = '\0';

      if( (!found_dot) || bogus_server )
	{
	  sendto_one(sptr,"ERROR :Bogus server name (%s)",
		     clean_host);
	  return exit_client(cptr, cptr, cptr, "Bogus server name");
	}
    }

  /* *WHEN* can it be that "cptr != sptr" ????? --msa */
  /* When SERVER command (like now) has prefix. -avalon */
  
  /* check to see this host even has an N line before bothering
  ** anyone about it. Its only a quick sanity test to stop
  ** the conference room and win95 ircd dorks. 
  ** Sure, it will be redundantly checked again in m_server_estab()
  ** *sigh* yes there will be wasted CPU as the conf list will
  ** be scanned twice. But how often will this happen?
  ** - Dianora
  *
  * This should (will be) be recoded to check the IP is valid as well, 
  * with a pointer to the valid N line conf kept for later, saving
  * an extra lookup.. *sigh* - Dianora
  */
  if (!IsServer(cptr))
    {
      if(find_conf_name(host,CONF_NOCONNECT_SERVER) == NULL)
	{
#ifdef WARN_NO_NLINE
	  sendto_realops("Link %s Server %s dropped, no N: line",
			 get_client_name(cptr, TRUE),clean_host);
#endif
	  return exit_client(cptr, cptr, cptr, "NO N line");
	}
    }

  if (MyConnect(cptr) && (autoconn == 0))
    {
      sendto_ops("WARNING AUTOCONN is 0, Closing %s",
		 get_client_name(cptr, TRUE));
      return exit_client(cptr, cptr, cptr, "AUTOCONNS off");
    }

  if ((acptr = find_name(host, NULL)))
    {
      /*
      ** This link is trying feed me a server that I already have
      ** access through another path -- multiple paths not accepted
      ** currently, kill this link immediately!!
      **
      ** Rather than KILL the link which introduced it, KILL the
      ** youngest of the two links. -avalon
      */
      char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */

      bcptr = (cptr->firsttime > acptr->from->firsttime) ? cptr : acptr->from;
      sendto_one(bcptr,"ERROR :Server %s already exists", host);
      if (bcptr == cptr)
      {
        sendto_ops("Link %s cancelled, server %s already exists",
		 get_client_name(bcptr, TRUE), host);
        return exit_client(bcptr, bcptr, &me, "Server Exists");
      }
      /*
      ** in this case, we are not dropping the link from
      ** which we got the SERVER message.  Thus we canNOT
      ** `return' yet! -krys
      */
      /*
      ** get_client_name() can return ptr to static buffer...can't use
      ** 2 times in same sendto_ops(), so we have to strcpy one =(
      **  - comstud
      */
      strcpy(nbuf, get_client_name(bcptr, TRUE));
      sendto_ops("Link %s cancelled, server %s reintroduced by %s",
		nbuf, host, get_client_name(cptr, TRUE));
      (void) exit_client(bcptr, bcptr, &me, "Server Exists");
    }

  /* The following if statement would be nice to remove
   * since user nicks never have '.' in them and servers
   * must always have '.' in them. There should never be a 
   * server/nick name collision, but it is possible a capricious
   * server admin could deliberately do something strange.
   *
   * -Dianora
   */

  if ((acptr = find_client(host, NULL)) && acptr != cptr)
    {
      /*
      ** Server trying to use the same name as a person. Would
      ** cause a fair bit of confusion. Enough to make it hellish
      ** for a while and servers to send stuff to the wrong place.
      */
      sendto_one(cptr,"ERROR :Nickname %s already exists!", host);
      sendto_ops("Link %s cancelled: Server/nick collision on %s",
		 inpath, host);
      return exit_client(cptr, cptr, cptr, "Nick as Server");
    }

  if (IsServer(cptr))
    {
      /*
      ** Server is informing about a new server behind
      ** this link. Create REMOTE server structure,
      ** add it to list and propagate word to my other
      ** server links...
      */
      if (parc == 1 || info[0] == '\0')
	{
	  sendto_one(cptr,
		     "ERROR :No server info specified for %s",
		     host);
	  return 0;
	}

      /*
      ** See if the newly found server is behind a guaranteed
      ** leaf (L-line). If so, close the link.
      */
      if ((aconf = find_conf_host(cptr->confs, host, CONF_LEAF)) &&
	  (!aconf->port || (hop > aconf->port)))
	{
	  sendto_ops("Leaf-only link %s->%s - Closing",
		     get_client_name(cptr,  TRUE),
		     aconf->host ? aconf->host : "*");
	  sendto_one(cptr, "ERROR :Leaf-only link, sorry.");
	  return exit_client(cptr, cptr, cptr, "Leaf Only");
	}
      /*
      **
      */
      if (!(aconf = find_conf_host(cptr->confs, host, CONF_HUB)) ||
	  (aconf->port && (hop > aconf->port)) )
	{
	  sendto_ops("Non-Hub link %s introduced %s(%s).",
		     get_client_name(cptr,  TRUE), host,
		     aconf ? (aconf->host ? aconf->host : "*") :
		     "!");
	  sendto_one(cptr, "ERROR :%s has no H: line for %s.",
		     get_client_name(cptr,  TRUE), host);
	  return exit_client(cptr, cptr, cptr,
			     "Too many servers");
	}
/*
 * Q: line code removed. Q: lines are not a useful feature on a modern net.
 */

      acptr = make_client(cptr);
      (void)make_server(acptr);
      acptr->hopcount = hop;
      strncpyzt(acptr->name, host, sizeof(acptr->name));
      strncpyzt(acptr->info, info, sizeof(acptr->info));
      acptr->serv->up = find_or_add(parv[0]);

      SetServer(acptr);

      Count.server++;

      add_client_to_list(acptr);
      (void)add_to_client_hash_table(acptr->name, acptr);
      acptr->servptr = sptr;
      add_client_to_llist(&(acptr->servptr->serv->servers), acptr);

      /*
      ** Old sendto_serv_but_one() call removed because we now
      ** need to send different names to different servers
      ** (domain name matching)
      */
      for(bcptr=serv_cptr_list;bcptr;bcptr=bcptr->next_server_client)
	{
	  if (bcptr == cptr)
	    continue;
	  if (!(aconf = bcptr->serv->nline))
	    {
	      sendto_ops("Lost N-line for %s on %s. Closing",
			 get_client_name(cptr, TRUE), host);
	      return exit_client(cptr, cptr, cptr,
				 "Lost N line");
	    }
	  if (matches(my_name_for_link(me.name, aconf),
		      acptr->name) == 0)
	    continue;

	  sendto_one(bcptr, ":%s SERVER %s %d :%s",
		     parv[0], acptr->name, hop+1, acptr->info);
	}
      return 0;
    }

  if (!IsUnknown(cptr) && !IsHandshake(cptr))
    return 0;
  /*
  ** A local link that is still in undefined state wants
  ** to be a SERVER. Check if this is allowed and change
  ** status accordingly...
  */

  /* 
  ** Reject a direct nonTS server connection if we're TS_ONLY -orabidoo
  */
  if (!DoesTS(cptr))
    {
      sendto_ops("Link %s dropped, non-TS server",
		 get_client_name(cptr, TRUE));
      return exit_client(cptr, cptr, cptr, "Non-TS server");
    }

  strncpyzt(cptr->name, host, sizeof(cptr->name));
  strncpyzt(cptr->info, info[0] ? info:me.name, sizeof(cptr->info));
  cptr->hopcount = hop;

  switch (check_server_init(cptr))
    {
    case 0 :
      return m_server_estab(cptr);
    case 1 :
      sendto_ops("Access check for %s in progress",
		 get_client_name(cptr,TRUE));
      return 1;
    default :
      ircstp->is_ref++;
      sendto_ops("Received unauthorized connection from %s.",
		 get_client_host(cptr));
      return exit_client(cptr, cptr, cptr, "No C/N conf lines");
    }

}

static void	sendnick_TS( aClient *cptr, aClient *acptr)
{
  static char ubuf[12];

  if (IsPerson(acptr))
    {
      send_umode(NULL, acptr, 0, SEND_UMODES, ubuf);
      if (!*ubuf)
	{ /* trivial optimization - Dianora */
	  
	  ubuf[0] = '+';
	  ubuf[1] = '\0';
	  /*	original was	strcpy(ubuf, "+"); */
	}

      sendto_one(cptr, "NICK %s %d %ld %s %s %s %s :%s", acptr->name, 
		 acptr->hopcount + 1, acptr->tsinfo, ubuf,
		 acptr->user->username, acptr->user->host,
		 acptr->user->server, acptr->info);
    }
}

int	m_server_estab(aClient *cptr)
{
  Reg	aChannel *chptr;
  Reg	aClient	*acptr;
  Reg	aConfItem	*aconf, *bconf;
  char	*inpath, *host, *s, *encr;
  int	split;

  inpath = get_client_name(cptr,TRUE); /* "refresh" inpath with host */
  split = mycmp(cptr->name, cptr->sockhost);
  host = cptr->name;

  if (!(aconf = find_conf(cptr->confs, host, CONF_NOCONNECT_SERVER)))
    {
      ircstp->is_ref++;
      sendto_one(cptr,
		 "ERROR :Access denied. No N line for server %s",
		 inpath);
      sendto_ops("Access denied. No N line for server %s", inpath);
      return exit_client(cptr, cptr, cptr, "No N line for server");
    }
  if (!(bconf = find_conf(cptr->confs, host, CONF_CONNECT_SERVER )))
    {
      ircstp->is_ref++;
      sendto_one(cptr, "ERROR :Only N (no C) field for server %s",
		 inpath);
      sendto_ops("Only N (no C) field for server %s",inpath);
      return exit_client(cptr, cptr, cptr, "No C line for server");
    }

#ifdef CRYPT_LINK_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL. Head it off at the pass... */
  if(*cptr->passwd && *aconf->passwd)
    {
      extern  char *crypt();
      encr = crypt(cptr->passwd, aconf->passwd);
    }
  else
    encr = "";
#else
  encr = cptr->passwd;
#endif  /* CRYPT_LINK_PASSWORD */
  if (*aconf->passwd && !StrEq(aconf->passwd, encr))
    {
      ircstp->is_ref++;
      sendto_one(cptr, "ERROR :No Access (passwd mismatch) %s",
		 inpath);
      sendto_ops("Access denied (passwd mismatch) %s", inpath);
      return exit_client(cptr, cptr, cptr, "Bad Password");
    }
  memset((void *)cptr->passwd, 0,sizeof(cptr->passwd));

  /* Its got identd , since its a server */
  cptr->flags |= FLAGS_GOTID;

#ifndef	HUB
  /* Its easy now, if there is a server in my link list
   * and I'm not a HUB, I can't grow the linklist more than 1
   *
   * -Dianora
   */
  if (serv_cptr_list)	
    {
      ircstp->is_ref++;
      sendto_one(cptr, "ERROR :I'm a leaf not a hub");
      return exit_client(cptr, cptr, cptr, "I'm a leaf");
    }
#endif
  if (IsUnknown(cptr))
    {
      if (bconf->passwd[0])
	sendto_one(cptr,"PASS %s :TS", bconf->passwd);
      /*
      ** Pass my info to the new server
      */

      send_capabilities(cptr,(bconf->flags & CONF_FLAGS_ZIP_LINK));
      sendto_one(cptr, "SERVER %s 1 :%s",
		 my_name_for_link(me.name, aconf), 
		 (me.info[0]) ? (me.info) : "IRCers United");
    }
  else
    {
      s = (char *)strchr(aconf->host, '@');
      if(s)
	*s = '\0'; /* should never be NULL -- wanna bet? -Dianora */
      Debug((DEBUG_INFO, "Check Usernames [%s]vs[%s]",
	     aconf->host, cptr->username));
      if (matches(aconf->host, cptr->username))
	{
	  if(s)
	    *s = '@';
	  ircstp->is_ref++;
	  sendto_ops("Username mismatch [%s]v[%s] : %s",
		     aconf->host, cptr->username,
		     get_client_name(cptr, TRUE));
	  sendto_one(cptr, "ERROR :No Username Match");
	  return exit_client(cptr, cptr, cptr, "Bad User");
	}
      if(s)
	*s = '@';
    }
  

#ifdef ZIP_LINKS
  if (IsCapable(cptr, CAP_ZIP) && (bconf->flags & CONF_FLAGS_ZIP_LINK))
    {
      if (zip_init(cptr) == -1)
	{
	  zip_free(cptr);
	  sendto_ops("Unable to setup compressed link for %s",
		      get_client_name(cptr, TRUE));
	  return exit_client(cptr, cptr, &me, "zip_init() failed");
	}
      cptr->flags2 |= (FLAGS2_ZIP|FLAGS2_ZIPFIRST);
    }
  else
    ClearCap(cptr, CAP_ZIP);
#endif /* ZIP_LINKS */

  sendto_one(cptr,"SVINFO %d %d 0 :%ld", TS_CURRENT, TS_MIN,(ts_val)timeofday);
  
  det_confs_butmask(cptr, CONF_LEAF|CONF_HUB|CONF_NOCONNECT_SERVER);
  /*
  ** *WARNING*
  ** 	In the following code in place of plain server's
  **	name we send what is returned by get_client_name
  **	which may add the "sockhost" after the name. It's
  **	*very* *important* that there is a SPACE between
  **	the name and sockhost (if present). The receiving
  **	server will start the information field from this
  **	first blank and thus puts the sockhost into info.
  **	...a bit tricky, but you have been warned, besides
  **	code is more neat this way...  --msa
  */
  SetServer(cptr);
  cptr->servptr = &me;
  add_client_to_llist(&(me.serv->servers), cptr);

  Count.server++;
  Count.myserver++;

#ifdef MAXBUFFERS
/*
 * let's try to bump up server sock_opts...
 * -Taner
 */
  reset_sock_opts(cptr->fd, 1);
#endif /* MAXBUFFERS */

  /* LINKLIST */
  /* add to server link list -Dianora */
  cptr->next_server_client = serv_cptr_list;
  serv_cptr_list = cptr;
 
  nextping = timeofday;
  /* ircd-hybrid-6 can do TS links, and  zipped links*/
  sendto_ops("Link with %s established: (TS%s) link",
	     inpath,(IsCapable(cptr, CAP_ZIP) ? ", zipped" : ""));

  (void)add_to_client_hash_table(cptr->name, cptr);
  /* doesnt duplicate cptr->serv if allocated this struct already */
  (void)make_server(cptr);
  cptr->serv->up = me.name;
  /* add it to scache */
  (void)find_or_add(cptr->name);
  
  cptr->serv->nline = aconf;
  cptr->flags2 |= FLAGS2_CBURST;

  /*
  ** Old sendto_serv_but_one() call removed because we now
  ** need to send different names to different servers
  ** (domain name matching) Send new server to other servers.
  */
  for(acptr=serv_cptr_list;acptr;acptr=acptr->next_server_client)
    {
      if (acptr == cptr)
	continue;

      if ((aconf = acptr->serv->nline) &&
	  !matches(my_name_for_link(me.name, aconf), cptr->name))
	continue;
      if (split)
	sendto_one(acptr,":%s SERVER %s 2 :[%s] %s",
		   me.name, cptr->name,
		   cptr->sockhost, cptr->info);
      else
	sendto_one(acptr,":%s SERVER %s 2 :%s",
		   me.name, cptr->name, cptr->info);
    }

  /*
  ** Pass on my client information to the new server
  **
  ** First, pass only servers (idea is that if the link gets
  ** cancelled beacause the server was already there,
  ** there are no NICK's to be cancelled...). Of course,
  ** if cancellation occurs, all this info is sent anyway,
  ** and I guess the link dies when a read is attempted...? --msa
  ** 
  ** Note: Link cancellation to occur at this point means
  ** that at least two servers from my fragment are building
  ** up connection this other fragment at the same time, it's
  ** a race condition, not the normal way of operation...
  **
  ** ALSO NOTE: using the get_client_name for server names--
  **	see previous *WARNING*!!! (Also, original inpath
  **	is destroyed...)
  */

  aconf = cptr->serv->nline;
  for (acptr = &me; acptr; acptr = acptr->prev)
    {
      /* acptr->from == acptr for acptr == cptr */
      if (acptr->from == cptr)
	continue;
      if (IsServer(acptr))
	{
	  if (matches(my_name_for_link(me.name, aconf),
		      acptr->name) == 0)
	    continue;
	  split = (MyConnect(acptr) &&
		   mycmp(acptr->name, acptr->sockhost));
	  if (split)
	    sendto_one(cptr, ":%s SERVER %s %d :[%s] %s",
		       acptr->serv->up, acptr->name,
		       acptr->hopcount+1,
		       acptr->sockhost, acptr->info);
	  else
	    sendto_one(cptr, ":%s SERVER %s %d :%s",
		       acptr->serv->up, acptr->name,
		       acptr->hopcount+1, acptr->info);
	}
    }
  
 
      /*
      ** Send it in the shortened format with the TS, if
      ** it's a TS server; walk the list of channels, sending
      ** all the nicks that haven't been sent yet for each
      ** channel, then send the channel itself -- it's less
      ** obvious than sending all nicks first, but on the
      ** receiving side memory will be allocated more nicely
      ** saving a few seconds in the handling of a split
      ** -orabidoo
      */

  {
    Link	*l;
    static	char nickissent = 1;
      
    nickissent = 3 - nickissent;
    /* flag used for each nick to check if we've sent it
       yet - must be different each time and !=0, so we
       alternate between 1 and 2 -orabidoo
       */
    for (chptr = channel; chptr; chptr = chptr->nextch)
      {
	for (l = chptr->members; l; l = l->next)
	  {
	    acptr = l->value.cptr;
	    if (acptr->nicksent != nickissent)
	      {
		acptr->nicksent = nickissent;
		if (acptr->from != cptr)
		  sendnick_TS(cptr, acptr);
	      }
	  }
	send_channel_modes(cptr, chptr);
      }
    /*
    ** also send out those that are not on any channel
    */
    for (acptr = &me; acptr; acptr = acptr->prev)
      if (acptr->nicksent != nickissent)
	{
	  acptr->nicksent = nickissent;
	  if (acptr->from != cptr)
	    sendnick_TS(cptr, acptr);
	}
    }

  cptr->flags2 &= ~FLAGS2_CBURST;

#ifdef  ZIP_LINKS
  /*
  ** some stats about the connect burst,
  ** they are slightly incorrect because of cptr->zip->outbuf.
  */
  if ((cptr->flags2 & FLAGS2_ZIP) && cptr->zip->out->total_in)
    sendto_ops("Connect burst to %s: %lu, compressed: %lu (%3.1f%%)",
		get_client_name(cptr, TRUE),
		cptr->zip->out->total_in,cptr->zip->out->total_out,
		(100.0*(float)cptr->zip->out->total_out) /
		(float)cptr->zip->out->total_in);
#endif /* ZIP_LINKS */

  return 0;
}

/*
** m_info
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_info(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  char **text = infotext;
  char outstr[241];
  static time_t last_used=0L;

  if (hunt_server(cptr,sptr,":%s INFO :%s",1,parc,parv) == HUNTED_ISME)
    {
      sendto_realops_lev(SPY_LEV, "info requested by %s (%s@%s) [%s]",
		         sptr->name, sptr->user->username, sptr->user->host,
                         sptr->user->server);
      if(!IsAnOper(sptr))
        {
          /* reject non local requests */
          if(!MyConnect(sptr))
            return 0;
          if((last_used + PACE_WAIT) > NOW)
            {
              return 0;
            }
          else
            {
              last_used = NOW;
            }
        }

      while (*text)
	sendto_one(sptr, rpl_str(RPL_INFO), me.name, parv[0], *text++);
      
      sendto_one(sptr, rpl_str(RPL_INFO), me.name, parv[0], "");

      if (IsAnOper(sptr) && MyConnect(sptr))
      {
#ifdef ANTI_NICK_FLOOD
	ircsprintf(outstr,
		   "ANTI_NICK_FLOOD=1 MAX_NICK_TIME=%d MAX_NICK_CHANGES=%d",
		   MAX_NICK_TIME,MAX_NICK_CHANGES);
	sendto_one(sptr, rpl_str(RPL_INFO), me.name, parv[0], outstr);
#else
	sendto_one(sptr, rpl_str(RPL_INFO), me.name, parv[0],
		   "ANTI_NICK_FLOOD=0");
#endif

#ifdef ANTI_SPAMBOT
#define OUT1	"ANTI_SPAMBOT=1"
#else
#define OUT1	"ANTI_SPAMBOT=0"
#endif
#ifdef ANTI_SPAMBOT_EXTRA
#define OUT2	" ANTI_SPAMBOT_EXTRA=1"
#else
#define OUT2	" ANTI_SPAMBOT_EXTRA=0"
#endif
#ifdef ANTI_SPAM_EXIT_MESSAGE
#define OUT3	" ANTI_SPAM_EXIT_MESSAGE=1"
#else
#define OUT3	" ANTI_SPAM_EXIT_MESSAGE=0"
#endif

	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef BAN_INFO
#define OUT1 "BAN_INFO=1"
#else
#define OUT1 "BAN_INFO=0"
#endif
#ifdef BOTCHECK
#define OUT2 " BOTCHECK=1"
#else
#define OUT2 " BOTCHECK=0"
#endif
#ifdef BOT_GCOS_WARN
#define OUT3 " BOT_GCOS_WARN=1"
#else
#define OUT3 " BOT_GCOS_WARN=0"
#endif
#ifdef B_LINES_OPER_ONLY
#define OUT4 " B_LINES_OPER_ONLY=1"
#else
#define OUT4 " B_LINES_OPER_ONLY=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef CLIENT_COUNT
#define OUT1 "CLIENT_COUNT=1"
#else
#define OUT1 "CLIENT_COUNT=0"
#endif
#ifdef CLIENT_FLOOD
#define OUT2 " CLIENT_FLOOD=1"
#else
#define OUT2 " CLIENT_FLOOD=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef CUSTOM_ERR
#define OUT1 "CUSTOM_ERR=1"
#else
#define OUT1 "CUSTOM_ERR=0"
#endif
#ifdef DEBUGMODE
#define OUT2 " DEBUGMODE=1"
#else
#define OUT2 " DEBUGMODE=0"
#endif
#ifdef DLINES_IN_KPATH
#define OUT3 " DLINES_IN_KPATH=1"
#else
#define OUT3 " DLINES_IN_KPATH=0"
#endif
#ifdef DNS_DEBUG
#define OUT4 " DNS_DEBUG=1"
#else
#define OUT4 " DNS_DEBUG=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4
#ifdef DO_IDENTD
#define OUT1 "DO_IDENTD=1"
#else
#define OUT1 "DO_IDENTD=0"
#endif
#ifdef EXTRA_BOT_NOTICES
#define OUT2 " EXTRA_BOT_NOTICES=1"
#else
#define OUT2 " EXTRA_BOT_NOTICES=0"
#endif
#ifdef E_LINES_OPER_ONLY
#define OUT3 " E_LINES_OPER_ONLY=1"
#else
#define OUT3 " E_LINES_OPER_ONLY=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef FAILED_OPER_NOTICE
#define OUT1 "FAILED_OPER_NOTICE=1"
#else
#define OUT1 "FAILED_OPER_NOTICE=0"
#endif
#ifdef FLUD
#define OUT2 " FLUD=1"
#else
#define OUT2 " FLUD=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef F_LINES_OPER_ONLY
#define OUT1 "F_LINES_OPER_ONLY=1"
#else
#define OUT1 "F_LINES_OPER_ONLY=0"
#endif
#ifdef GLINES
#define OUT2 " GLINES=1"
#else
#define OUT2 " GLINES=0"
#endif
#ifdef HIGHEST_CONNECTION
#define OUT3 " HIGHEST_CONNECTION=1"
#else
#define OUT3 " HIGHEST_CONNECTION=0"
#endif
#ifdef HUB
#define OUT4 " HUB=1"
#else
#define OUT4 " HUB=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 OUT4 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef IDENTD_COMPLAIN
#define OUT1 "IDENTD_COMPLAIN=1"
#else
#define OUT1 "IDENTD_COMPLAIN=0"
#endif
#ifdef IDLE_CHECK
#define OUT2 " IDLE_CHECK=1"
#else
#define OUT2 " IDLE_CHECK=0"
#endif
#ifdef IDLE_FROM_MSG
#define OUT3 " IDLE_FROM_MSG=1"
#else
#define OUT3 " IDLE_FROM_MSG=0"
#endif
#ifdef IGNORE_FIRST_CHAR
#define OUT4 " IGNORE_FIRST_CHAR=1"
#else
#define OUT4 " IGNORE_FIRST_CHAR=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef KLINE_WITH_REASON
#define OUT1 "KLINE_WITH_REASON=1"
#else
#define OUT1 "KLINE_WITH_REASON=0"
#endif
#ifdef KPATH
#define OUT2 " KPATH=1"
#else
#define OUT2 " KPATH=0"
#endif

        sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], OUT1 OUT2);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef LIMIT_UH
#define OUT1 "LIMIT_UH=1"
#else
#define OUT1 "LIMIT_UH=0"
#endif
#ifdef LITTLE_I_LINES
#define OUT2 " LITTLE_I_LINES=1"
#else
#define OUT2 " LITTLE_I_LINES=0"
#endif
#ifdef LOCKFILE
#define OUT3 " LOCKFILE=1"
#else
#define OUT3 " LOCKFILE=0"
#endif
#ifdef MAXBUFFERS
#define OUT4 " MAXBUFFERS=1"
#else
#define OUT4 " MAXBUFFERS=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 OUT4 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef NON_REDUNDANT_KLINES
#define OUT1 "NON_REDUNDANT_KLINES=1"
#else
#define OUT1 "NON_REDUNDANT_KLINES=0"
#endif
#ifdef NO_CHANOPS_WHEN_SPLIT
#define OUT2 " NO_CHANOPS_WHEN_SPLIT=1"
#else
#define OUT2 " NO_CHANOPS_WHEN_SPLIT=0"
#endif
#ifdef NO_DEFAULT_INVISIBLE
#define OUT3 " NO_DEFAULT_INVISIBLE=1"
#else
#define OUT3 " NO_DEFAULT_INVISIBLE=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], OUT1 OUT2 OUT3);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef NO_LOCAL_KLINE
#define OUT1 "NO_LOCAL_KLINE=1"
#else
#define OUT1 "NO_LOCAL_KLINE=0"
#endif
#ifdef NO_DEFAULT_INVISIBLE
#define OUT2 " NO_DEFAULT_INVISIBLE=1"
#else
#define OUT2 " NO_DEFAULT_INVISIBLE=0"
#endif
#ifdef NO_MIXED_CASE
#define OUT3 " NO_MIXED_CASE=1"
#else
#define OUT3 " NO_MIXED_CASE=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], OUT1 OUT2 OUT3 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef NO_OPER_FLOOD
#define OUT1 "NO_OPER_FLOOD=1"
#else
#define OUT1 "NO_OPER_FLOOD=0"
#endif
#ifdef NO_SPECIAL
#define OUT2 " NO_SPECIAL=1"
#else
#define OUT2 " NO_SPECIAL=0"
#endif
#ifdef OLD_Y_LIMIT
#define OUT3 " OLD_Y_LIMIT=1"
#else
#define OUT3 " OLD_Y_LIMIT=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef REJECT_HOLD
#define OUT1 "REJECT_HOLD=1 "
#else
#define OUT1 "REJECT_HOLD=0 "
#endif
#ifdef REJECT_IPHONE
#define OUT2 " REJECT_IPHONE=1"
#else
#define OUT2 " REJECT_IPHONE=0"
#endif
#ifdef RFC1035_ANAL
#define OUT3 " RFC1035_ANAL=1"
#else
#define OUT3 " RFC1035_ANAL=0"
#endif
#ifdef RK_NOTICES
#define OUT4 " RK_NOTICES=1"
#else
#define OUT4 " RK_NOTICES=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 OUT4 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef R_LINES
#define OUT1 "R_LINES=1"
#else
#define OUT1 "R_LINES=0"
#endif
#ifdef R_LINES_OFTEN
#define OUT2 " R_LINES_OFTEN=1"
#else
#define OUT2 " R_LINES_OFTEN=0"
#endif
#ifdef R_LINES_REHASH
#define OUT3 " R_LINES_REHASH=1"
#else
#define OUT3 " R_LINES_REHASH=0"
#endif
#ifdef REPORT_DLINE_TO_USER
#define OUT4 " REPORT_DLINE_TO_USER=1"
#else
#define OUT4 " REPORT_DLINE_TO_USER=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
#define OUT1 "SEPARATE_QUOTE_KLINES_BY_DATE=1"
#else
#define OUT1 "SEPARATE_QUOTE_KLINES_BY_DATE=0"
#endif
#ifdef SHORT_MOTD
#define OUT2 " SHORT_MOTD=1"
#else
#define OUT2 " SHORT_MOTD=0"
#endif
#ifdef SHOW_HEADERS
#define OUT3 " SHOW_HEADERS=1"
#else
#define OUT3 " SHOW_HEADERS=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], OUT1 OUT2 OUT3);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef SHOW_UH
#ifdef SHOW_INVISIBLE_LUSERS
#define OUT1 "SHOW_INVISIBLE_LUSERS=1"
#else
#define OUT1 "SHOW_INVISIBLE_LUSERS=0"
#endif
#define OUT2 " SHOW_UH=1"
#else
#define OUT2 " SHOW_UH=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef TIMED_KLINES
#define OUT1 "TIMED_KLINES=1"
#else
#define OUT1 "TIMED_KLINES=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef TOPIC_INFO
#define OUT1 "TOPIC_INFO=1"
#else
#define OUT1 "TOPIC_INFO=0"
#endif
#ifdef UNKLINE
#define OUT2 " UNKLINE=1"
#else
#define OUT2 " UNKLINE=0"
#endif
#ifdef USERNAMES_IN_TRACE
#define OUT3 " USERNAMES_IN_TRACE=1"
#else
#define OUT3 " USERNAMES_IN_TRACE=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef USE_FAST_FD_ISSET
#define OUT1 "USE_FAST_FD_ISSET=1"
#else
#define OUT1 "USE_FAST_FD_ISSET=0"
#endif
#ifdef USE_SYSLOG
#define OUT2 " USE_SYSLOG=1"
#else
#define OUT2 " USE_SYSLOG=0"
#endif
#ifdef USE_UH
#define OUT3 " USE_UH=1"
#else
#define OUT3 " USE_UH=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2 OUT3);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef WARN_NO_NLINE
#define OUT1 "WARN_NO_NLINE=1"
#else
#define OUT1 "WARN_NO_NLINE=0"
#endif
#ifdef WHOIS_NOTICE
#define OUT2 " WHOIS_NOTICE=1"
#else
#define OUT2 " WHOIS_NOTICE=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], OUT1 OUT2);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef FLUD
	ircsprintf(outstr,"FLUD_BLOCK=%d FLUD_NUM=%d FLUD_TIME=%d",
	  FLUD_BLOCK,FLUD_NUM,FLUD_TIME);
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], outstr);
#endif
#ifdef GLINES
	ircsprintf(outstr,"GLINE_TIME=%d",GLINE_TIME);
	sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], outstr);
#endif
	ircsprintf(outstr,"HARD_FDLIMIT_=%d HYBRID_SOMAXCONN=%d",HARD_FDLIMIT_,HYBRID_SOMAXCONN);
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], outstr);
	ircsprintf(outstr,"PACE_WAIT=%d MAXIMUM_LINKS=%d",PACE_WAIT,MAXIMUM_LINKS);
	sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], outstr);
	ircsprintf(outstr,"MAX_NICK_CHANGES=%d MAX_NICK_TIME=%d",MAX_NICK_CHANGES,MAX_NICK_TIME);
        sendto_one(sptr, rpl_str(RPL_INFO),
                me.name, parv[0], outstr);
	ircsprintf(outstr,"NICKNAMEHISTORYLENGTH=%d NOISY_HTM=%d",NICKNAMEHISTORYLENGTH,NOISY_HTM);
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], outstr);
        ircsprintf(outstr,"TS_MAX_DELTA=%d TS_WARN_DELTA=%d",TS_MAX_DELTA,TS_WARN_DELTA);
	sendto_one(sptr, rpl_str(RPL_INFO),
		me.name, parv[0], outstr);
      }

      sendto_one(sptr,
		 ":%s %d %s :Birth Date: %s, compile # %s",
		 me.name, RPL_INFO, parv[0], creation, generation);
      sendto_one(sptr, ":%s %d %s :On-line since %s",
		 me.name, RPL_INFO, parv[0],
		 myctime(me.firsttime));
      sendto_one(sptr, rpl_str(RPL_ENDOFINFO), me.name, parv[0]);
    }

  return 0;
}


/*
** m_links
**	parv[0] = sender prefix
**	parv[1] = servername mask
** or
**	parv[0] = sender prefix
**	parv[1] = server to query 
**      parv[2] = servername mask
*/
int	m_links(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  char *mask;
  aClient *acptr;
  char clean_mask[(2*HOSTLEN)+1];
  char *s;
  char *d;
  int  n;
  static time_t last_used=0L;

  if (parc > 2)
    {
      if (hunt_server(cptr, sptr, ":%s LINKS %s :%s", 1, parc, parv)
	  != HUNTED_ISME)
	return 0;
      mask = parv[2];
    }
  else
    mask = parc < 2 ? NULL : parv[1];

  if(!IsAnOper(sptr))
    {
      /* reject non local requests */
      if(!MyConnect(sptr))
        return 0;

      if((last_used + PACE_WAIT) > NOW)
        {
          return 0;
        }
      else
        {
          last_used = NOW;
        }
    }

/*
 * *sigh* Before the kiddies find this new and exciting way of 
 * annoying opers, lets clean up what is sent to all opers
 * -Dianora
 */

  if(mask)	/* only necessary if there is a mask */
    {
      s = mask;
      d = clean_mask;
      n = (2*HOSTLEN) - 2;
      while(*s && n)
        {
          if(*s < ' ') /* Is it a control character? */
            {
              *d++ = '^';
              *d++ = (*s + 0x40); /* turn it into a printable */
              s++;
              n--;
            }
          else if(*s > '~')
            {
              *d++ = '.';
              s++;
              n--;
            }
          else
            {
              *d++ = *s++;
              n--;
            }
        }
      *d = '\0';
    }

  if (MyConnect(sptr))
    sendto_realops_lev(SPY_LEV,
		       "LINKS '%s' requested by %s (%s@%s) [%s]",
		       mask?clean_mask:"",
		       sptr->name, sptr->user->username,
		       sptr->user->host, sptr->user->server);
  
  for (acptr = client, (void)collapse(mask); acptr; acptr = acptr->next) 
    {
      if (!IsServer(acptr) && !IsMe(acptr))
	continue;
      if (!BadPtr(mask) && matches(mask, acptr->name))
	continue;
      sendto_one(sptr, rpl_str(RPL_LINKS),
		 me.name, parv[0], acptr->name, acptr->serv->up,
		 acptr->hopcount, (acptr->info[0] ? acptr->info :
				   "(Unknown Location)"));
    }
  
  sendto_one(sptr, rpl_str(RPL_ENDOFLINKS), me.name, parv[0],
	     BadPtr(mask) ? "*" : clean_mask);
  return 0;
}

/*
 * rewrote to use a struct -Dianora
 */

typedef struct
{
  int conf_type;
  int rpl_stats;
  int conf_char;
}REPORT_STRUCT;

static REPORT_STRUCT report_array[] = {
  { CONF_CONNECT_SERVER,    RPL_STATSCLINE, 'C'},
  { CONF_NOCONNECT_SERVER,  RPL_STATSNLINE, 'N'},
  { CONF_LEAF,		  RPL_STATSLLINE, 'L'},
  { CONF_OPERATOR,	  RPL_STATSOLINE, 'O'},
  { CONF_HUB,		  RPL_STATSHLINE, 'H'},
  { CONF_LOCOP,		  RPL_STATSOLINE, 'o'},
  { 0, 0, '\0' }
};



static	void	report_configured_links(aClient *sptr,int mask)
{
  static	char	null[] = "<NULL>";
  aConfItem *tmp;
  REPORT_STRUCT *p;
  int   port;
  char	*host, *pass, *name;

  for (tmp = conf; tmp; tmp = tmp->next)
    if (tmp->status & mask)
      {
	for (p = &report_array[0]; p->conf_type; p++)
	  if (p->conf_type == tmp->status)
	    break;
	if(p->conf_type == 0)return;

	host = BadPtr(tmp->host) ? null : tmp->host;
	pass = BadPtr(tmp->passwd) ? null : tmp->passwd;
	name = BadPtr(tmp->name) ? null : tmp->name;
	port = (int)tmp->port;

	if(mask & (CONF_CONNECT_SERVER|CONF_NOCONNECT_SERVER))
	  {
	    char c;

	    c = p->conf_char;
	    if(tmp->flags & CONF_FLAGS_ZIP_LINK)
	      c = 'c';

	    /* Don't allow non opers to see actual ips */
	    if(IsAnOper(sptr))
	      sendto_one(sptr, rpl_str(p->rpl_stats), me.name,
			 sptr->name, c,
			 host,
			 name,
			 port,
			 get_conf_class(tmp));
	    else
	      sendto_one(sptr, rpl_str(p->rpl_stats), me.name,
			 sptr->name, c,
			 "*@127.0.0.1",
			 name,
			 port,
			 get_conf_class(tmp));

	  }
	else if(mask & (CONF_OPERATOR|CONF_LOCOP))
	  {
	    /* Don't allow non opers to see oper privs */
	    if(IsAnOper(sptr))
	      sendto_one(sptr, rpl_str(p->rpl_stats), me.name,
			 sptr->name, p->conf_char, host, name,
			 oper_privs((aClient *)NULL,port),
			 get_conf_class(tmp));
	    else
	      sendto_one(sptr, rpl_str(p->rpl_stats), me.name,
			 sptr->name, p->conf_char, host, name,
			 "0",
			 get_conf_class(tmp));
	  }
	else
	  sendto_one(sptr, rpl_str(p->rpl_stats), me.name,
		     sptr->name, p->conf_char, host, name, port,
		     get_conf_class(tmp));
      }
  return;
}

/*
 * report_specials
 *
 * inputs	- aClient pointer to client to report to
 *		- int flags type of special aConfItem to report
 *		- int numeric for aConfItem to report
 * output	- none
 * side effects	-
 */

static	void	report_specials(aClient *sptr,int flags,int numeric)
{
  static	char	null[] = "<NULL>";
  aConfItem *this_conf;
  aConfItem *aconf;
  char	*pass, *host;

  if(flags & CONF_XLINE)
    this_conf = x_conf;
  else if(flags & CONF_ULINE)
    this_conf = u_conf;
  else if(flags & CONF_QUARANTINED_NICK)
    this_conf = q_conf;
  else return;

  for (aconf = this_conf; aconf; aconf = aconf->next)
    if (aconf->status & flags)
      {
	host = BadPtr(aconf->host) ? null : aconf->host;
	pass = BadPtr(aconf->passwd) ? null : aconf->passwd;

	sendto_one(sptr, rpl_str(numeric),
		   me.name,
		   sptr->name,
		   host,
		   pass);

      }
}

/*
** m_stats
**      parv[0] = sender prefix
**      parv[1] = statistics selector (defaults to Message frequency)
**      parv[2] = server name (current server defaulted, if omitted)
**
**      Currently supported are:
**              M = Message frequency (the old stat behaviour)
**              L = Local Link statistics
**              C = Report C and N configuration lines
*/
/*
** m_stats/stats_conf
**    Report N/C-configuration lines from this server. This could
**    report other configuration lines too, but converting the
**    status back to "char" is a bit akward--not worth the code
**    it needs...
**
**    Note:   The info is reported in the order the server uses
**            it--not reversed as in ircd.conf!
*/

int	m_stats(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  static	char	Lformat[]  = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
  struct	Message	*mptr;
  aClient	*acptr;
  char	stat = parc > 1 ? parv[1][0] : '\0';
  int	doall = 0, wilds = 0, valid_stats = 0;
  char	*name;
  static time_t last_used=0L;

  if (hunt_server(cptr,sptr,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return 0;

  if(!IsAnOper(sptr))
    {
      /* reject non local requests
       * but in this case allow stats p
       */
      if( !((stat == 'p') || (stat == 'P')) &&
	  !MyConnect(sptr))
	return 0;

      if((last_used + PACE_WAIT) > NOW)
        {
	  return 0;
        }
      else
        {
	  last_used = NOW;
        }
    }

  if (hunt_server(cptr,sptr,":%s STATS %s :%s",2,parc,parv)!=HUNTED_ISME)
    return 0;

  if (parc > 2)
    {
      name = parv[2];
      if (!mycmp(name, me.name))
	doall = 2;
      else if (matches(name, me.name) == 0)
	doall = 1;
      if (strchr(name, '*') || strchr(name, '?'))
	wilds = 1;
    }
  else
    name = me.name;


  switch (stat)
    {
    case 'L' : case 'l' :
      /*
       * send info about connections which match, or all if the
       * mask matches me.name.  Only restrictions are on those who
       * are invisible not being visible to 'foreigners' who use
       * a wild card based search to list it.
       */
      for(acptr=local_cptr_list;acptr;acptr=acptr->next_local_client)
	{
          if (IsPerson(acptr) &&
              !IsAnOper(acptr) && !IsAnOper(sptr) &&
              (acptr != sptr))
            continue;
	  if (IsInvisible(acptr) && (doall || wilds) &&
	      !(MyConnect(sptr) && IsOper(sptr)) &&
	      !IsAnOper(acptr) && (acptr != sptr))
	    continue;
	  if (!doall && wilds && matches(name, acptr->name))
	    continue;
	  if (!(doall || wilds) && mycmp(name, acptr->name))
	    continue;

	  /* I've added a sanity test to the "timeofday - acptr->since"
	   * occasionally, acptr->since is larger than timeofday.
	   * The code in parse.c "randomly" increases the "since",
	   * which means acptr->since is larger then timeofday at times,
	   * this gives us very high odd number.. 
	   * So, I am going to return 0 for ->since if this happens.
	   * - Dianora
	   */

          if(IsAnOper(sptr))
	    {
	      sendto_one(sptr, Lformat, me.name,
		     RPL_STATSLINKINFO, parv[0],
		     (isupper(stat)) ?
		     get_client_name(acptr, TRUE) :
		     get_client_name(acptr, FALSE),
		     (int)DBufLength(&acptr->sendQ),
		     (int)acptr->sendM, (int)acptr->sendK,
		     (int)acptr->receiveM, (int)acptr->receiveK,
		     timeofday - acptr->firsttime,
		     (timeofday > acptr->since) ? (timeofday - acptr->since):0,
		     IsServer(acptr) ? "TS" : "-");
              }
            else
              {
                if(IsAnOper(acptr))
                  sendto_one(sptr, Lformat, me.name,
                     RPL_STATSLINKINFO, parv[0],
                     get_client_name(acptr, HIDEME),
                     (int)DBufLength(&acptr->sendQ),
                     (int)acptr->sendM, (int)acptr->sendK,
                     (int)acptr->receiveM, (int)acptr->receiveK,
                     timeofday - acptr->firsttime,
                     (timeofday > acptr->since) ? (timeofday - acptr->since):0,
                     IsServer(acptr) ? "TS" : "-");
                 else
                  sendto_one(sptr, Lformat, me.name,
                     RPL_STATSLINKINFO, parv[0],
                     (isupper(stat)) ?
                     get_client_name(acptr, TRUE) :
                     get_client_name(acptr, FALSE),
                     (int)DBufLength(&acptr->sendQ),
                     (int)acptr->sendM, (int)acptr->sendK,
                     (int)acptr->receiveM, (int)acptr->receiveK,
                     timeofday - acptr->firsttime,
                     (timeofday > acptr->since) ? (timeofday - acptr->since):0,
                     IsServer(acptr) ? "TS" : "-");
              }
	}
      valid_stats++;
      break;
    case 'C' : case 'c' :
      report_configured_links(sptr, CONF_CONNECT_SERVER|CONF_NOCONNECT_SERVER);
      valid_stats++;
      break;

    case 'B' : case 'b' :
      sendto_one(sptr,":%s NOTICE %s Use stats I instead", me.name, parv[0]);
      break;

    case 'D': case 'd':
      if (!IsAnOper(sptr))
	{
	  sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	  break;
	}
      report_dlines(sptr);
      valid_stats++;
      break;

    case 'E' : case 'e' :
      sendto_one(sptr,":%s NOTICE %s Use stats I instead", me.name, parv[0]);
      break;

    case 'F' : case 'f' :
      sendto_one(sptr,":%s NOTICE %s Use stats I instead", me.name, parv[0]);
      break;

    case 'G': case 'g' :
#ifdef GLINES
      report_glines(sptr);
      valid_stats++;
#else
      sendto_one(sptr,":%s NOTICE %s :This server does not support G lines",
               me.name, parv[0]);
#endif
      break;

    case 'H' : case 'h' :
      report_configured_links(sptr, CONF_HUB|CONF_LEAF);
      valid_stats++;
      break;

    case 'I' : case 'i' :
      report_mtrie_conf_links(sptr, CONF_CLIENT);
      valid_stats++;
      break;

    case 'k' :
      report_temp_klines(sptr);
      valid_stats++;
      break;

    case 'K' :
/* sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]); */
      if(parc > 3)
	report_matching_host_klines(sptr,parv[3]);
      else
	if (IsAnOper(sptr))
	  report_mtrie_conf_links(sptr, CONF_KILL);
	else
	  report_matching_host_klines(sptr,sptr->sockhost);
      valid_stats++;
      break;

    case 'M' : case 'm' :
/* original behaviour was not to report the command, if the command hadn't
   been used. I'm going to always report the command instead -Dianora */
      for (mptr = msgtab; mptr->cmd; mptr++)
/*	if (mptr->count) */
	  sendto_one(sptr, rpl_str(RPL_STATSCOMMANDS),
		     me.name, parv[0], mptr->cmd,
		     mptr->count, mptr->bytes);
      valid_stats++;
      break;

    case 'o' : case 'O' :
      report_configured_links(sptr, CONF_OPS);
      valid_stats++;
      break;

    case 'p' :
      show_opers(sptr, parv[0]);
      valid_stats++;
      break;

    case 'P' :
      show_ports(sptr, parv[0]);
      break;

    case 'Q' : case 'q' :
      if(!IsAnOper(sptr))
	sendto_one(sptr,":%s NOTICE %s :This server does not support Q lines",
		   me.name, parv[0]);
      else
	{
	  report_specials(sptr,CONF_QUARANTINED_NICK,RPL_STATSQLINE);
	  valid_stats++;
	}
      break;

    case 'R' : case 'r' :
#ifdef DEBUGMODE
      send_usage(sptr,parv[0]);
#endif
      valid_stats++;
      break;

    case 'S' : case 's':
      if (IsAnOper(sptr))
	list_scache(cptr,sptr,parc,parv);
      else
	sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      valid_stats++;
      break;

    case 'T' : case 't' :
      if (!IsAnOper(sptr))
	{
	  sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	  break;
	}
      tstats(sptr, parv[0]);
      valid_stats++;
      break;

    case 'U' :
      report_specials(sptr,CONF_ULINE,RPL_STATSULINE);
      valid_stats++;
      break;

    case 'u' :
      {
	register time_t now;
	
	now = timeofday - me.since;
	sendto_one(sptr, rpl_str(RPL_STATSUPTIME), me.name, parv[0],
		   now/86400, (now/3600)%24, (now/60)%60, now%60);
#ifdef HIGHEST_CONNECTION
	sendto_one(sptr, rpl_str(RPL_STATSCONN), me.name, parv[0],
		   max_connection_count, max_client_count);
#endif
	valid_stats++;
	break;
      }

    case 'v' : case 'V' :
      show_servers(sptr, parv[0]);
      valid_stats++;
      break;

    case 'x' : case 'X' :
      if(IsAnOper(sptr))
	{
	  report_specials(sptr,CONF_XLINE,RPL_STATSXLINE);
	  valid_stats++;
	}
      break;;

    case 'Y' : case 'y' :
      report_classes(sptr);
      valid_stats++;
      break;

    case 'Z' : case 'z' :
      if (IsAnOper(sptr))
	{
	  count_memory(sptr, parv[0]);
	}
      else
	sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      valid_stats++;
      break;

    case '?':
      serv_info(sptr, parv[0]);
      valid_stats++;
      break;

    default :
      stat = '*';
      break;
    }
  sendto_one(sptr, rpl_str(RPL_ENDOFSTATS), me.name, parv[0], stat);

  /* personally, I don't see why opers need to see stats requests
   * at all. They are just "noise" to an oper, and users can't do
   * any damage with stats requests now anyway. So, why show them?
   * -Dianora
   */

#ifdef STATS_NOTICE
  if (valid_stats && stat != (char)0)
    sendto_realops_lev(SPY_LEV, "STATS %c requested by %s (%s@%s) [%s]", stat,
		       sptr->name, sptr->user->username, sptr->user->host,
		       sptr->user->server);
#endif
  return 0;
}

static void show_ports(aClient *cptr,char *name)
{
  register aConfItem *aconf;

  sendto_one(cptr,":%s NOTICE %s : %d",me.name,name,me.port);  

  for(aconf=conf;aconf;aconf=aconf->next)
    {
      if(aconf->status & CONF_LISTEN_PORT)
	sendto_one(cptr,":%s NOTICE %s : %d",me.name,name,aconf->port);
    }
}

/*
** m_users
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_users(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
#ifdef CLIENT_COUNT
  if (hunt_server(cptr,sptr,":%s USERS :%s",1,parc,parv) == HUNTED_ISME)
    {
      /* No one uses this any more... so lets remap it..   -Taner */
      sendto_one(sptr, rpl_str(RPL_LOCALUSERS), me.name, parv[0],
		 Count.local, Count.max_loc);
      sendto_one(sptr, rpl_str(RPL_GLOBALUSERS), me.name, parv[0],
		 Count.total, Count.max_tot);
    }
#endif /* CLIENT_COUNT */
  return 0;
}

/*
** Note: At least at protocol level ERROR has only one parameter,
** although this is called internally from other functions
** --msa
**
**	parv[0] = sender prefix
**	parv[*] = parameters
*/
int	m_error(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  Reg	char	*para;

  para = (parc > 1 && *parv[1] != '\0') ? parv[1] : "<>";
  
  Debug((DEBUG_ERROR,"Received ERROR message from %s: %s",
	 sptr->name, para));
  /*
  ** Ignore error messages generated by normal user clients
  ** (because ill-behaving user clients would flood opers
  ** screen otherwise). Pass ERROR's from other sources to
  ** the local operator...
  */
  if (IsPerson(cptr) || IsUnknown(cptr))
    return 0;
  if (cptr == sptr)
    sendto_ops("ERROR :from %s -- %s",
	       get_client_name(cptr, FALSE), para);
  else
    sendto_ops("ERROR :from %s via %s -- %s", sptr->name,
	       get_client_name(cptr,FALSE), para);
  return 0;
}

/*
** m_help
**	parv[0] = sender prefix
*/
int	m_help(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  int i;
  register aMessageFile *helpfile_ptr;
  static time_t last_used=0L;

  if(!IsAnOper(sptr))
    {
      /* HELP is always local */
      if((last_used + PACE_WAIT) > NOW)
        {
	  return 0;
        }
      else
        {
	  last_used = NOW;
        }
    }

  if ( !IsAnOper(sptr) || (helpfile == (aMessageFile *)NULL))
    {
      for (i = 0; msgtab[i].cmd; i++)
	sendto_one(sptr,":%s NOTICE %s :%s",
		   me.name, parv[0], msgtab[i].cmd);
      return 0;
    }

  for(helpfile_ptr = helpfile; helpfile_ptr; helpfile_ptr = helpfile_ptr->next)
    {
      sendto_one(sptr,
		 ":%s NOTICE %s :%s",
		 me.name, parv[0], helpfile_ptr->line);
    }

  return 0;
}

/*
 * parv[0] = sender
 * parv[1] = host/server mask.
 * parv[2] = server to query
 * 
 * 199970918 JRL hacked to ignore parv[1] completely and require parc > 3
 * to cause a force
 */
int	 m_lusers(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{
  static time_t last_used=0L;

  if (parc > 2)
    {
      if(hunt_server(cptr, sptr, ":%s LUSERS %s :%s", 2, parc, parv)
       != HUNTED_ISME)
        {
          return 0;
        }
    }

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
        {
	  return 0;
        }
      else
        {
	  last_used = NOW;
        }
    }

  return(show_lusers(cptr,sptr,parc,parv));
}

int show_lusers(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
#define LUSERS_CACHE_TIME 180
  static long last_time=0;
  static int	s_count = 0, c_count = 0, u_count = 0, i_count = 0;
  static int	o_count = 0, m_client = 0, m_server = 0;
  int forced;
  aClient *acptr;

/*  forced = (parc >= 2); */
  forced = (IsAnOper(sptr) && (parc > 3));

/* (void)collapse(parv[1]); */

  Count.unknown = 0;
  m_server = Count.myserver;
  m_client = Count.local;
  i_count  = Count.invisi;
  u_count  = Count.unknown;
  c_count  = Count.total-Count.invisi;
  s_count  = Count.server;
  o_count  = Count.oper;
  if (forced || (timeofday > last_time+LUSERS_CACHE_TIME))
    {
      last_time = timeofday;
      /* only recount if more than a second has passed since last request */
      /* use LUSERS_CACHE_TIME instead... */
      s_count = 0; c_count = 0; u_count = 0; i_count = 0;
      o_count = 0; m_client = 0; m_server = 0;

      for (acptr = client; acptr; acptr = acptr->next)
	{
/*	  if (parc>1)
	    {
	      if (!IsServer(acptr) && acptr->user)
	        {
		  if (matches(parv[1], acptr->user->server)!=0)
		  continue;
	        } 
	      else
	 	{
	          if (matches(parv[1], acptr->name)!=0)
		  continue;
		}
	    }
*/
	  switch (acptr->status)
	    {
	    case STAT_SERVER:
	      if (MyConnect(acptr))
		m_server++;
	    case STAT_ME:
	      s_count++;
	      break;
	    case STAT_CLIENT:
	      if (IsOper(acptr))
		o_count++;
#ifdef	SHOW_INVISIBLE_LUSERS
	      if (MyConnect(acptr))
		m_client++;
	      if (!IsInvisible(acptr))
		c_count++;
	      else
		i_count++;
#else
	      if (MyConnect(acptr))
		{
		  if (IsInvisible(acptr))
		    {
		      if (IsAnOper(sptr))
			m_client++;
		    }
		  else
		    m_client++;
		}
	      if (!IsInvisible(acptr))
		c_count++;
	      else
		i_count++;
#endif
	      break;
	    default:
	      u_count++;
	      break;
	    }
	}
      /*
       * We only want to reassign the global counts if the recount
       * time has expired, and NOT when it was forced, since someone
       * may supply a mask which will only count part of the userbase
       *	-Taner
       */
      if (!forced)
	{
	  if (m_server != Count.myserver)
	    {
	      sendto_realops_lev(DEBUG_LEV, 
				 "Local server count off by %d",
				 Count.myserver - m_server);
	      Count.myserver = m_server;
	    }
	  if (s_count != Count.server)
	    {
	      sendto_realops_lev(DEBUG_LEV,
				 "Server count off by %d",
				 Count.server - s_count);
	      Count.server = s_count;
	    }
	  if (i_count != Count.invisi)
	    {
	      sendto_realops_lev(DEBUG_LEV,
				 "Invisible client count off by %d",
				 Count.invisi - i_count);
	      Count.invisi = i_count;
	    }
	  if ((c_count+i_count) != Count.total)
	    {
	      sendto_realops_lev(DEBUG_LEV, "Total client count off by %d",
				 Count.total - (c_count+i_count));
	      Count.total = c_count+i_count;
	    }
	  if (m_client != Count.local)
	    {
	      sendto_realops_lev(DEBUG_LEV,
				 "Local client count off by %d",
				 Count.local - m_client);
	      Count.local = m_client;
	    }
	  if (o_count != Count.oper)
	    {
	      sendto_realops_lev(DEBUG_LEV,
				 "Oper count off by %d", Count.oper - o_count);
	      Count.oper = o_count;
	    }
	  Count.unknown = u_count;
	} /* Complain & reset loop */
    } /* Recount loop */
  
#ifndef	SHOW_INVISIBLE_LUSERS
  if (IsAnOper(sptr) && i_count)
#endif
    sendto_one(sptr, rpl_str(RPL_LUSERCLIENT), me.name, parv[0],
	       c_count, i_count, s_count);
#ifndef	SHOW_INVISIBLE_LUSERS
  else
    sendto_one(sptr,
	       ":%s %d %s :There are %d users on %d servers", me.name,
	       RPL_LUSERCLIENT, parv[0], c_count,
	       s_count);
#endif
  if (o_count)
    sendto_one(sptr, rpl_str(RPL_LUSEROP),
	       me.name, parv[0], o_count);
  if (u_count > 0)
    sendto_one(sptr, rpl_str(RPL_LUSERUNKNOWN),
	       me.name, parv[0], u_count);
  /* This should be ok */
  if (Count.chan > 0)
    sendto_one(sptr, rpl_str(RPL_LUSERCHANNELS),
	       me.name, parv[0], Count.chan);
  sendto_one(sptr, rpl_str(RPL_LUSERME),
	     me.name, parv[0], m_client, m_server);
#ifdef CLIENT_COUNT
  sendto_one(sptr, rpl_str(RPL_LOCALUSERS), me.name, parv[0],
	     Count.local, Count.max_loc);
  sendto_one(sptr, rpl_str(RPL_GLOBALUSERS), me.name, parv[0],
	     Count.total, Count.max_tot);
#else
#ifdef HIGHEST_CONNECTION
  sendto_one(sptr, rpl_str(RPL_STATSCONN), me.name, parv[0],
	     max_connection_count, max_client_count);
  if (m_client > max_client_count)
    max_client_count = m_client;
  if ((m_client + m_server) > max_connection_count)
    {
      max_connection_count = m_client + m_server;
      if (max_connection_count % 10 == 0)
	sendto_ops(
		   "New highest connections: %d (%d clients)",
		   max_connection_count, max_client_count);
    }
#endif /* HIGHEST_CONNECTION */
#endif /* CLIENT_COUNT */
  return 0;
}

  
/***********************************************************************
 * m_connect() - Added by Jto 11 Feb 1989
 ***********************************************************************/

/*
** m_connect
**	parv[0] = sender prefix
**	parv[1] = servername
**	parv[2] = port number
**	parv[3] = remote server
*/
int	m_connect(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{
  int	port, tmpport, retval;
  aConfItem *aconf;
  aClient *acptr;

  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return -1;
    }

  if (IsLocOp(sptr) && parc > 3)	/* Only allow LocOps to make */
    return 0;		/* local CONNECTS --SRB      */

  if (MyConnect(sptr) && !IsOperRemote(sptr) && parc > 3)
    {
      sendto_one(sptr,":%s NOTICE %s :You have no R flag",me.name,parv[0]);
      return 0;
    }

  if (hunt_server(cptr,sptr,":%s CONNECT %s %s :%s",
		  3,parc,parv) != HUNTED_ISME)
    return 0;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "CONNECT");
      return -1;
    }

  if ((acptr = find_server(parv[1], NULL)))
    {
      sendto_one(sptr, ":%s NOTICE %s :Connect: Server %s %s %s.",
		 me.name, parv[0], parv[1], "already exists from",
		 acptr->from->name);
      return 0;
    }

  for (aconf = conf; aconf; aconf = aconf->next)
    if ((aconf->status == CONF_CONNECT_SERVER) &&
	matches(parv[1], aconf->name) == 0)
      break;
  /* Checked first servernames, then try hostnames. */
  /* *sigh* AVALON YOU DOLT! */
  if (!aconf)
    {
      char *host_to_match;

      for (aconf = conf; aconf; aconf = aconf->next)
	{
	  host_to_match = strchr(aconf->host, '@');
	  if(host_to_match)
	    host_to_match++;
	  else
	    host_to_match = aconf->host;

	  if ((aconf->status == CONF_CONNECT_SERVER) &&
	      (matches(parv[1], aconf->host) == 0 ||
	       matches(parv[1], host_to_match) == 0))
	    {
	      break;
	    }
	}
    }

  if (!aconf)
    {
      sendto_one(sptr,
		 "NOTICE %s :Connect: Host %s not listed in ircd.conf",
		 parv[0], parv[1]);
      return 0;
    }
  /*
  ** Get port number from user, if given. If not specified,
  ** use the default form configuration structure. If missing
  ** from there, then use the precompiled default.
  */
  tmpport = port = aconf->port;
  if (parc > 2 && !BadPtr(parv[2]))
    {
      if ((port = atoi(parv[2])) <= 0)
	{
	  sendto_one(sptr,
		     "NOTICE %s :Connect: Illegal port number",
		     parv[0]);
	  return 0;
	}
    }
  else if (port <= 0 && (port = PORTNUM) <= 0)
    {
      sendto_one(sptr, ":%s NOTICE %s :Connect: missing port number",
		 me.name, parv[0]);
      return 0;
    }
  /*
  ** Notify all operators about remote connect requests
  */
  if (!IsAnOper(cptr))
    {
      sendto_ops_butone(NULL, &me,
			":%s WALLOPS :Remote CONNECT %s %s from %s",
			me.name, parv[1], parv[2] ? parv[2] : "",
			get_client_name(sptr,FALSE));
#if defined(USE_SYSLOG) && defined(SYSLOG_CONNECT)
      syslog(LOG_DEBUG, "CONNECT From %s : %s %d", parv[0], parv[1], parv[2] ? parv[2] : "");
#endif
    }
  aconf->port = port;
  switch (retval = connect_server(aconf, sptr, NULL))
    {
    case 0:
      sendto_one(sptr,
		 ":%s NOTICE %s :*** Connecting to %s[%s].",
		 me.name, parv[0], aconf->host, aconf->name);
      break;
    case -1:
      sendto_one(sptr, ":%s NOTICE %s :*** Couldn't connect to %s.",
		 me.name, parv[0], aconf->host);
      break;
    case -2:
      sendto_one(sptr, ":%s NOTICE %s :*** Host %s is unknown.",
		 me.name, parv[0], aconf->host);
      break;
    default:
      sendto_one(sptr,
		 ":%s NOTICE %s :*** Connection to %s failed: %s",
		 me.name, parv[0], aconf->host, strerror(retval));
    }
  aconf->port = tmpport;
  return 0;
}


 
/*
** m_wallops (write to *all* opers currently online)
**      parv[0] = sender prefix
**      parv[1] = message text
*/
int     m_wallops(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{ 
  char    *message;

  message = parc > 1 ? parv[1] : NULL;
  
  if (BadPtr(message))
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "WALLOPS");
      return 0;
    }

  if (!IsServer(sptr) && MyConnect(sptr) && !IsAnOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  /* If its coming from a server, do the normal thing
     if its coming from an oper, send the wallops along
     and only send the wallops to our local opers (those who are +oz)
     -Dianora
  */

  if(!IsServer(sptr))	/* If source of message is not a server, i.e. oper */
    {
      send_operwall(sptr, "WALLOPS", message);
      sendto_serv_butone( IsServer(cptr) ? cptr : NULL,
			  ":%s WALLOPS :%s", parv[0], message);
    }
  else			/* its a server wallops */
    sendto_wallops_butone(IsServer(cptr) ? cptr : NULL, sptr,
			    ":%s WALLOPS :%s", parv[0], message);
  return 0;
}

/*
** m_locops (write to *all* local opers currently online)
**      parv[0] = sender prefix
**      parv[1] = message text
*/
int     m_locops(aClient *cptr,
                  aClient *sptr,
                  int parc,
                  char *parv[])
{
  char *message;
#ifdef SLAVE_SERVERS
  char *slave_oper;
  char *nick;
  char *p;
  register aClient *acptr;
#endif

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      if(!find_special_conf(sptr->name,CONF_ULINE))
	{
	  sendto_realops("received Unauthorized locops from %s",sptr->name);
	  return 0;
	}

      if(parc > 2)
	{
	  slave_oper = parv[1];
	  nick = slave_oper;
	  p = strchr(nick,'!');
	  if(p)
	    *p = '\0';
	  else
	    return 0;
	  if ((acptr = hash_find_client(nick,(aClient *)NULL)))
	    {
	      if(!IsPerson(acptr))
		return 0;
	    }
	  else
	    return 0;

	  if(parv[2])
	    {
	      message = parv[2];
	      send_operwall(acptr, "SLOCOPS", message);
	    }
	  else
	    return 0;
	}
    }
  else
    {
      message = parc > 1 ? parv[1] : NULL;
    }
#else
  if(IsServer(sptr))
    return 0;
  message = parc > 1 ? parv[1] : NULL;
#endif

  if (BadPtr(message))
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "LOCOPS");
      return 0;
    }


  if(MyConnect(sptr) && IsAnOper(sptr))
    {
#ifdef SLAVE_SERVERS
      sendto_slaves("LOCOPS",cptr,sptr,
		    sptr->name,sptr->user->username,
		    sptr->user->host,parc,parv);
#endif
      send_operwall(sptr, "LOCOPS", message);
    }
  else
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  return(0);
}

/* raped from csr30 */

int     m_operwall(aClient *cptr,
		   aClient *sptr,
		   int parc,
		   char *parv[])
{
  char *message = parc > 1 ? parv[1] : NULL;


  if (check_registered_user(sptr))
    return 0;
  if (!IsAnOper(sptr) || IsServer(sptr))
    {
      if (MyClient(sptr) && !IsServer(sptr))
	sendto_one(sptr, err_str(ERR_NOPRIVILEGES),
		   me.name, parv[0]);
      return 0;
    }
  if (BadPtr(message))
    {
      if (MyClient(sptr))
	sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "OPERWALL");
      return 0;
    }
  /*
  if (strlen(message) > TOPICLEN)
    message[TOPICLEN] = '\0';
    */
  sendto_serv_butone(IsServer(cptr) ? cptr : NULL, ":%s OPERWALL :%s",
		     parv[0], message);
  send_operwall(sptr, "OPERWALL", message);
  return 0;
}


/*
** m_time
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_time(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  if (hunt_server(cptr,sptr,":%s TIME :%s",1,parc,parv) == HUNTED_ISME)
    sendto_one(sptr, rpl_str(RPL_TIME), me.name,
	       parv[0], me.name, date((long)0));
  return 0;
}

/*
** m_admin
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_admin(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  aConfItem *aconf;
  static time_t last_used=0L;

  if (hunt_server(cptr,sptr,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return 0;

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
	return 0;
      else
	last_used = NOW;
    }

  if (IsPerson(sptr))
    sendto_realops_lev(SPY_LEV, "ADMIN requested by %s (%s@%s) [%s]", sptr->name,
		       sptr->user->username, sptr->user->host, sptr->user->server);
  if ((aconf = find_admin()))
    {
      sendto_one(sptr, rpl_str(RPL_ADMINME),
		 me.name, parv[0], me.name);
      sendto_one(sptr, rpl_str(RPL_ADMINLOC1),
		 me.name, parv[0], aconf->host);
      sendto_one(sptr, rpl_str(RPL_ADMINLOC2),
		 me.name, parv[0], aconf->passwd);
      sendto_one(sptr, rpl_str(RPL_ADMINEMAIL),
		 me.name, parv[0], aconf->name);
    }
  else
    sendto_one(sptr, err_str(ERR_NOADMININFO),
	       me.name, parv[0], me.name);
  return 0;
}

/* Shadowfax's server side, anti flood code */

#ifdef FLUD
extern int flud_num;
extern int flud_time;
extern int flud_block;
#endif

#ifdef ANTI_SPAMBOT
extern int spam_num;
extern int spam_time;
#endif


/*
 * m_set - set options while running
 */
int   m_set(aClient *cptr,
	    aClient *sptr,
	    int parc,
	    char *parv[])
{
  char *command;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      command = parv[1];
      if (!strncasecmp(command,"MAX",3))
	{
          if (parc > 2) {
	    int new_value = atoi(parv[2]);
	    if (new_value > MASTER_MAX)
	      {
                sendto_one(sptr,
			   ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
			   me.name, parv[0], MASTER_MAX);
                return 0;
              }
	    if (new_value < 32)
	      {
                sendto_one(sptr, ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
                      me.name, parv[0], MAXCLIENTS, highest_fd);
                return 0;
              }
	    MAXCLIENTS = new_value;
	    sendto_one(sptr, ":%s NOTICE %s :NEW MAXCLIENTS = %d (Current = %d)",
		       me.name, parv[0], MAXCLIENTS, Count.local);
	    sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
			   parv[0], sptr->user->username, sptr->sockhost, MAXCLIENTS, Count.local);
	    return 0;
          }
          sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
		     me.name, parv[0], MAXCLIENTS, Count.local);
          return 0;
        }
      else if(!strncasecmp(command,"AUTOCONN",8))
	{
	  if(parc > 3)
	    {
	      int newval = atoi(parv[3]);

	      if(!strcasecmp(parv[2],"ALL"))
		 {
		   sendto_ops(
			      "%s has changed AUTOCONN ALL to %i",
			      parv[0], newval);
		   sendto_one(sptr,
			      ":%s NOTICE %s :AUTOCONN ALL is now set to %i",
			 me.name, parv[0], newval);
		   autoconn = newval;
		   return 0;
		 }
	      else
		set_autoconn(sptr,parv[0],parv[2],newval);
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
			 me.name, parv[0], autoconn);
	      return 0;
	    }
	}
#ifdef IDLE_CHECK
      else if(!strncasecmp(command, "IDLETIME", 8))
	{
	  if(parc > 2)
	    {
	      int newval = atoi(parv[2]);

	      if((newval*60) < MIN_IDLETIME)
		{
		  sendto_one(sptr, ":%s NOTICE %s :IDLETIME must be >= %d",
			     me.name, parv[0],MIN_IDLETIME/60);
		  return 0;
		}       
	      sendto_ops("%s has changed IDLETIME to %i", parv[0], newval);
	      sendto_one(sptr, ":%s NOTICE %s :IDLETIME is now set to %i",
			 me.name, parv[0], newval);
	      idle_time = (newval*60);
	      return 0;       
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
			 me.name, parv[0], idle_time/60);
	      return 0;
	    }
	}
#endif
#ifdef FLUD
      else if(!strncasecmp(command, "FLUDNUM",7))
	{
	  if(parc > 2)
	    {
	      int newval = atoi(parv[2]);

	      if(newval <= 0)
		{
		  sendto_one(sptr, ":%s NOTICE %s :flud NUM must be > 0",
			     me.name, parv[0]);
		  return 0;
		}       
	      flud_num = newval;
	      sendto_ops("%s has changed flud NUM to %i", parv[0], flud_num);
	      sendto_one(sptr, ":%s NOTICE %s :flud NUM is now set to %i",
			 me.name, parv[0], flud_num);
	      return 0;       
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :flud NUM is currently %i",
			 me.name, parv[0], flud_num);
	      return 0;
	    }
	}
      else if(!strncasecmp(command,"FLUDTIME",8))
	{
	  if(parc > 2)
	    {
	      int newval = atoi(parv[2]);

	      if(newval <= 0)
		{
		  sendto_one(sptr, ":%s NOTICE %s :flud TIME must be > 0",
			     me.name, parv[0]);
		  return 0;
		}       
	      flud_time = newval;
	      sendto_ops("%s has changed flud TIME to %i", parv[0], flud_time);
	      sendto_one(sptr, ":%s NOTICE %s :flud TIME is now set to %i",
			 me.name, parv[0], flud_num);
	      return 0;       
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :flud TIME is currently %i",
			 me.name, parv[0], flud_time);
	      return 0;
	    }
	}
      else if(!strncasecmp(command,"FLUDBLOCK",9))
	{
	  if(parc > 2)
	    {
	      int newval = atoi(parv[2]);

	      if(newval < 0)
		{
		  sendto_one(sptr, ":%s NOTICE %s :flud BLOCK must be >= 0",
			     me.name, parv[0]);
		  return 0;
		}       
	      flud_block = newval;
	      if(flud_block == 0)
		{
		  sendto_ops("%s has disabled flud detection/protection",
			     parv[0]);
		  sendto_one(sptr, ":%s NOTICE %s :flud detection disabled",
			 me.name, parv[0]);
		}
	      else
		{
		  sendto_ops("%s has changed flud BLOCK to %i",
			     parv[0],flud_block);
		  sendto_one(sptr, ":%s NOTICE %s :flud BLOCK is now set to %i",
			 me.name, parv[0], flud_block);
		}
	      return 0;       
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :flud BLOCK is currently %i",
			 me.name, parv[0], flud_block);
	      return 0;
	    }
	}
#endif
#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)
      else if(!strncasecmp(command, "SPLITDELAY",10))
	{
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval < 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :split delay must be >= 0",
                             me.name, parv[0]);
                  return 0;
                }
	      if(newval > MAX_SERVER_SPLIT_RECOVERY_TIME)
		{
		  sendto_one(sptr, ":%s NOTICE %s :Cannot set SPLIT RECOVERY TIME over %d", parv[0], MAX_SERVER_SPLIT_RECOVERY_TIME);
		  newval = MAX_SERVER_SPLIT_RECOVERY_TIME;
		}
              sendto_ops("%s has changed spam SERVER SPLIT RECOVERY TIME  to %i", parv[0], newval);
              sendto_one(sptr, ":%s NOTICE %s :SERVER SPLIT RECOVERY TIME is now set to %i",
                         me.name, parv[0], newval );
	      server_split_recovery_time = (newval*60);
              return 0;
            }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :SERVER SPLIT RECOVERY TIME is currently %i",
			 me.name,
			 parv[0],
			 server_split_recovery_time/60);
	    }
	}
#endif
#ifdef ANTI_SPAMBOT
/*
int spam_time = MIN_JOIN_LEAVE_TIME;
int spam_num = MAX_JOIN_LEAVE_COUNT;
*/
      else if(!strncasecmp(command, "SPAMNUM",7))
        {
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
                             me.name, parv[0]);
                  return 0;
                }
              if(newval < MIN_SPAM_NUM)
		spam_num = MIN_SPAM_NUM;
	      else
		spam_num = newval;
              sendto_ops("%s has changed SPAMNUM to %i", parv[0], spam_num);
              sendto_one(sptr, ":%s NOTICE %s :spam NUM is now set to %i",
                         me.name, parv[0], spam_num);
              return 0;
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
                         me.name, parv[0], spam_num);
              return 0;
            }
        }
      else if(!strncasecmp(command, "SPAMTIME",8))
        {
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPAMTIME must be > 0",
                             me.name, parv[0]);
                  return 0;
                }
              if(newval < MIN_SPAM_TIME)
		spam_time = MIN_SPAM_TIME;
	      else
		spam_time = newval;
              sendto_ops("%s has changed SPAMTIME to %i", parv[0], spam_time);
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is now set to %i",
                         me.name, parv[0], spam_time);
              return 0;
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
                         me.name, parv[0], spam_time);
              return 0;
            }
        }
#endif
#ifdef ANTI_SPAMBOT_EXTRA
      else if(!strncasecmp(command, "SPAMMSGS",8))
        {
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :spam message count must be > 0",
                             me.name, parv[0]);
                  return 0;
                }
              if(newval < PRIVMSG_POSSIBLE_SPAMBOT_COUNT)
		spambot_privmsg_count = PRIVMSG_POSSIBLE_SPAMBOT_COUNT;
	      else
		spambot_privmsg_count = newval;
              sendto_ops("%s has changed spam message count to %i",
			 parv[0], spambot_privmsg_count);
              sendto_one(sptr, ":%s NOTICE %s :SPAMMSGS is now set to %i",
                         me.name, parv[0], spambot_privmsg_count);
              return 0;
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMMSGS is currently %i",
                         me.name, parv[0], spambot_privmsg_count);
              return 0;
            }
        }
      }
#endif
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :Options: MAX AUTOCONN",
		 me.name, parv[0]);
#ifdef FLUD
      sendto_one(sptr, ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK",
		 me.name, parv[0]);
#endif
#ifdef ANTI_SPAMBOT
      sendto_one(sptr, ":%s NOTICE %s :Options: SPAMNUM, SPAMTIME",
		 me.name, parv[0]);
#endif
#ifdef ANTI_SPAMBOT_EXTRA
      sendto_one(sptr, ":%s NOTICE %s :Options: SPAMMSGS",
		 me.name, parv[0]);
#endif
#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)
	sendto_one(sptr, ":%s NOTICE %s :Options: SPLITDELAY",
		me.name, parv[0]);
#endif
#ifdef IDLE_CHECK
	sendto_one(sptr, ":%s NOTICE %s :Options: IDLETIME",
		   me.name, parv[0]);
#endif
    }
  return 0;
}

/*
 * m_htm - high traffic mode info
 */
int   m_htm(aClient *cptr,
	    aClient *sptr,
	    int parc,
	    char *parv[])
{
#define LOADCFREQ 5
  char *command;

  extern int lifesux, LRV, LCF, noisy_htm;  /* in ircd.c */
  extern float currlife;
  
  if (!MyClient(sptr) || !IsOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr,
	":%s NOTICE %s :HTM is %s(%d), %s. Max rate = %dk/s. Current = %.1fk/s",
          me.name, parv[0], lifesux ? "ON" : "OFF", lifesux,
	  noisy_htm ? "NOISY" : "QUIET",
	  LRV, currlife);
  if (parc > 1)
    {
      command = parv[1];
      if (!strcasecmp(command,"TO"))
	{
	  if (parc > 2)
	    {
	      int new_value = atoi(parv[2]);
	      if (new_value < 10)
		{
		  sendto_one(sptr, ":%s NOTICE %s :\002Cannot set LRV < 10!\002",
			     me.name, parv[0]);
                }
	      else
		LRV = new_value;
	      sendto_one(sptr, ":%s NOTICE %s :NEW Max rate = %dk/s. Current = %.1fk/s",
			 me.name, parv[0], LRV, currlife);
	      sendto_realops("%s!%s@%s set new HTM rate to %dk/s (%.1fk/s current)",
			     parv[0], sptr->user->username, sptr->sockhost,
			     LRV, currlife);
	    }
	  else 
	    sendto_one(sptr, ":%s NOTICE %s :LRV command needs an integer parameter",me.name, parv[0]);
        }
      else
	{
          if (!strcasecmp(command,"ON"))
	    {
              lifesux = 1;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now ON.", me.name, parv[0]);
              sendto_ops("Entering high-traffic mode: Forced by %s!%s@%s",
			 parv[0], sptr->user->username, sptr->sockhost);
	      LCF = 30;	/* 30s */
	    }
	  else if (!strcasecmp(command,"OFF"))
	    {
              lifesux = 0;
	      LCF = LOADCFREQ;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now OFF.", me.name, parv[0]);
              sendto_ops("Resuming standard operation: Forced by %s!%s@%s",
			 parv[0], sptr->user->username, sptr->sockhost);
            }
          else if (!strcasecmp(command,"QUIET"))
            {
	      sendto_ops("HTM is now QUIET");
              noisy_htm = NO;
            }
          else if (!strcasecmp(command,"NOISY"))
            {
	      sendto_ops("HTM is now NOISY");
              noisy_htm = YES;
            }
	  else
	    sendto_one(sptr,
		       ":%s NOTICE %s :Commands are:HTM [ON] [OFF] [TO int] [QUIET] [NOISY]",
		       me.name, parv[0]);
        }
    }
  return 0;
}

/*
 * cluster()
 *
 * input	- pointer to a hostname
 * output	- pointer to a static of the hostname masked
 *		  for use in a kline.
 * side effects	- NONE
 *
 * reworked a tad -Dianora
 */

static char *cluster(char *hostname)
{
  static char result[HOSTLEN+1];	/* result to return */
  char        temphost[HOSTLEN+1];	/* work place */
  char	      *ipp;		/* used to find if host is ip # only */
  char	      *host_mask;	/* used to find host mask portion to '*' */
  char	      *zap_point=(char *)NULL;	/* used to zap last nnn portion of an ip # */
  char 	      *tld;		/* Top Level Domain */
  int	      is_ip_number;	/* flag if its an ip # */	      
  int	      number_of_dots;	/* count number of dots for both ip# and
				   domain klines */
  if (!hostname)
    return (char *) NULL;	/* EEK! */

  /* If a '@' is found in the hostname, this is bogus
   * and must have been introduced by server that doesn't
   * check for bogus domains (dns spoof) very well. *sigh* just return it...
   * I could also legitimately return (char *)NULL as above.
   *
   * -Dianora
   */

  if(strchr(hostname,'@'))	
    {
      strncpyzt(result, hostname, HOSTLEN);      
      return(result);
    }

  strncpyzt(temphost, hostname, HOSTLEN);

  is_ip_number = YES;	/* assume its an IP# */
  ipp = temphost;
  number_of_dots = 0;

  while (*ipp)
    {
      if( *ipp == '.' )
	{
	  number_of_dots++;

	  if(number_of_dots == 3)
	    zap_point = ipp;
	  ipp++;
	}
      else if(!isdigit(*ipp))
	{
	  is_ip_number = NO;
	  break;
	}
      ipp++;
    }

  if(is_ip_number && (number_of_dots == 3))
    {
      zap_point++;
      *zap_point++ = '*';		/* turn 111.222.333.444 into */
      *zap_point = '\0';		/*      111.222.333.*	     */
      strncpy(result, temphost, HOSTLEN);
      return(result);
    }
  else
    {
      tld = strrchr(temphost, '.');
      if(tld)
	{
	  number_of_dots = 2;
	  if(tld[3])			 /* its at least a 3 letter tld
					    i.e. ".com" tld[3] = 'm' not 
					    '\0' */
				         /* 4 letter tld's are coming */
	    number_of_dots = 1;

	  if(tld != temphost)		/* in these days of dns spoofers ...*/
	    host_mask = tld - 1;	/* Look for host portion to '*' */
	  else
	    host_mask = tld;		/* degenerate case hostname is
					   '.com' etc. */

	  while(host_mask != temphost)
	    {
	      if(*host_mask == '.')
		number_of_dots--;
	      if(number_of_dots == 0)
		{
		  result[0] = '*';
		  strncpy(result+1,host_mask,HOSTLEN-1);
		  return(result);
		}
	      host_mask--;
	    }
	  result[0] = '*';			/* foo.com => *foo.com */
	  strncpy(result+1,temphost,HOSTLEN);
	}
      else	/* no tld found oops. just return it as is */
	{
	  strncpy(result, temphost, HOSTLEN);
	  return(result);
	}
    }

  return (result);
}

#ifdef GLINES

/*
 * m_gline()
 *
 * inputs	- The usual for a m_ function
 * output	-
 * side effects	-
 *
 * Place a G line if 3 opers agree on the identical user@host
 * 
 */

int     m_gline(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  char buffer[512];
  char *oper_name;		/* nick of oper requesting GLINE */
  char *oper_username;		/* username of oper requesting GLINE */
  char *oper_host;		/* hostname of oper requesting GLINE */
  char *oper_server;		/* server of oper requesting GLINE */
  char *user, *host;		/* user and host of GLINE "victim" */
  char *reason;			/* reason for "victims" demise */
  char *current_date;
  char tempuser[USERLEN+2];
  char temphost[HOSTLEN+1];
  aConfItem *aconf;

  if(!IsServer(sptr)) /* allow remote opers to apply g lines */
    {
      /* Only globals can apply Glines */
      if (!IsOper(sptr))
	{
	  sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	  return 0;
	}

      if (!IsSetOperGline(sptr))
	{
	  sendto_one(sptr,":%s NOTICE %s :You have no G flag",me.name,parv[0]);
	  return 0;
	}

      if ( parc < 3 )
	{
	  sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		     me.name, parv[0], "GLINE");
	  return 0;
	}
      
      if ( (host = strchr(parv[1], '@')) || *parv[1] == '*' )
	{
	  /* Explicit user@host mask given */
	  
	  if(host)			/* Found user@host */
	    {
	      user = parv[1];	/* here is user part */
	      *(host++) = '\0';	/* and now here is host */
	    }
	  else
	    {
	      user = "*";		/* no @ found, assume its *@somehost */
	      host = parv[1];
	    }

	  if (!*host)		/* duh. no host found, assume its '*' host */
	    host = "*";

	  strncpyzt(tempuser, user, USERLEN+2);	/* allow for '*' */
	  strncpyzt(temphost, host, HOSTLEN);
	  user = tempuser;
	  host = temphost;
	}
      else
	{
          sendto_one(sptr, ":%s NOTICE %s :Can't G-Line a nick use user@host",
		     me.name,
		     parv[0]);
	  return 0;
	}

      if(strchr(parv[2], ':'))
	{
          sendto_one(sptr,
		     ":%s NOTICE %s :Invalid character ':' in comment",
		     me.name, parv[2]);
	  return 0;
	}

      reason = parv[2];

      if (!matches(user, "akjhfkahfasfjd") &&
                !matches(host, "ldksjfl.kss...kdjfd.jfklsjf"))
	{
	  if(MyClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :Can't G-Line *@*", me.name,
		       parv[0]);

	  return 0;
	}
      
      if (bad_tld(host))
	{
	  if(MyClient(sptr))
	    sendto_one(sptr, ":%s NOTICE %s :Can't G-Line *@%s",
		       me.name,
		       parv[0],
		       host);
	  return 0;
	}

      if(sptr->name)
	oper_name = sptr->name;
      else 
	return 0;
      
      if(sptr->user && sptr->user->username)
	oper_username = sptr->user->username;
      else
	return 0;

      if(sptr->user && sptr->user->host)
	oper_host = sptr->user->host;
      else
	return 0;

      if(sptr->user && sptr->user->server)
	oper_server = sptr->user->server;
      else
	return 0;

      sendto_serv_butone(NULL, ":%s GLINE %s %s %s %s %s %s :%s",
			 me.name,
			 oper_name,
			 oper_username,
			 oper_host,
			 oper_server,
			 user,
			 host,
			 reason);
    }
  else
    {
      if(!IsServer(sptr))
        return(0);
      
      oper_name = parv[1];
      oper_username = parv[2];
      oper_host = parv[3];
      oper_server = parv[4];
      user = parv[5];
      host = parv[6];
      reason = parv[7];

      sendto_serv_butone(sptr, ":%s GLINE %s %s %s %s %s %s :%s",
                         sptr->name,
			 oper_name,oper_username,oper_host,oper_server,
                         user,
                         host,
                         reason);
    }
   log_gline_request(oper_name,oper_username,oper_host,oper_server,
		     user,host,reason);

   sendto_realops("%s!%s@%s on %s is requesting gline for [%s@%s] [%s]",
		  oper_name,
		  oper_username,
		  oper_host,
		  oper_server,
		  user,
		  host,
		  reason);

  /* If at least 3 opers agree this user should be G lined then do it */
  if(majority_gline(sptr,
		    oper_name,
		    oper_username,
		    oper_host,
		    oper_server,
		    user,
		    host,
		    reason))
    {
      current_date = smalldate((time_t) 0);
	  
      aconf = make_conf();
      aconf->status = CONF_KILL;
      DupString(aconf->host, host);

      (void)ircsprintf(buffer, "%s (%s)",reason,current_date);
      
      DupString(aconf->passwd, buffer);
      DupString(aconf->name, user);
      aconf->hold = timeofday + GLINE_TIME;
      add_gline(aconf);
      
      sendto_realops("%s!%s@%s on %s has triggered gline for [%s@%s] [%s]",
		     oper_name,
		     oper_username,
		     oper_host,
		     oper_server,
		     user,
		     host,
		     reason);
      
      rehashed = YES;
      dline_in_progress = NO;
      nextping = timeofday;

      return 0;
    }
  
  return 0;
}



/*
 * log_gline_request()
 *
 */
static void log_gline_request(
		      char *oper_nick,
		      char *oper_user,
		      char *oper_host,
		      char *oper_server,
		      char *user,
		      char *host,
		      char *reason)
{
  char buffer[512];
  char filenamebuf[1024];
  static  char    timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;
  int out;

  (void)sprintf(filenamebuf, "%s.%s", glinefile, small_file_date((time_t)0));
  if ((out = open(filenamebuf, O_RDWR|O_APPEND|O_CREAT,0644))==-1)
    {
      sendto_realops("*** Problem opening %s",filenamebuf);
      return;
    }

  tmptr = localtime(&NOW);
  strftime(timebuffer, MAX_DATE_STRING, "%y/%m/%d %H:%M:%S", tmptr);

  (void)ircsprintf(buffer,
		   "#Gline for %s@%s [%s] requested by %s!%s@%s on %s at %s\n",
		   user,host,reason,
		   oper_nick,oper_user,oper_host,oper_server,
		   timebuffer);

  if (write(out,buffer,strlen(buffer)) <= 0)
    {
      sendto_realops("*** Problem writing to %s",filenamebuf);
      (void)close(out);
      return;
    }
  return;
}

/*
 * log_gline()
 *
 */
void log_gline(
		      aClient *sptr,
		      char *parv0,
		      GLINE_PENDING *gline_pending_ptr,
		      char *oper_nick,
		      char *oper_user,
		      char *oper_host,
		      char *oper_server,
		      char *user,
		      char *host,
		      char *reason)
{
  char buffer[512];
  char filenamebuf[1024];
  static  char    timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;
  int out;

  (void)sprintf(filenamebuf, "%s.%s", glinefile, small_file_date((time_t) 0));
  if ((out = open(filenamebuf, O_RDWR|O_APPEND|O_CREAT,0644))==-1)
    {
      return;
    }

  tmptr = localtime(&NOW);
  strftime(timebuffer, MAX_DATE_STRING, "%y/%m/%d %H:%M:%S", tmptr);

  (void)ircsprintf(buffer,"#Gline for %s@%s %s added by the following\n",
		   user,host,timebuffer);

  if (safe_write(sptr,parv0,filenamebuf,out,buffer))
    return;

  (void)ircsprintf(buffer, "#%s!%s@%s on %s [%s]\n",
		   gline_pending_ptr->oper_nick1,
		   gline_pending_ptr->oper_user1,
		   gline_pending_ptr->oper_host1,
		   gline_pending_ptr->oper_server1,
		   (gline_pending_ptr->reason1)?
		   (gline_pending_ptr->reason1):"No reason");

  if (safe_write(sptr,parv0,filenamebuf,out,buffer))
    return;

  (void)ircsprintf(buffer, "#%s!%s@%s on %s [%s]\n",
		   gline_pending_ptr->oper_nick2,
		   gline_pending_ptr->oper_user2,
		   gline_pending_ptr->oper_host2,
		   gline_pending_ptr->oper_server2,
		   (gline_pending_ptr->reason2)?
		   (gline_pending_ptr->reason2):"No reason");

  if (safe_write(sptr,parv0,filenamebuf,out,buffer))
    return;

  (void)ircsprintf(buffer, "#%s!%s@%s on %s [%s]\n",
		   oper_nick,
		   oper_user,
		   oper_host,
		   oper_server,
		   (reason)?reason:"No reason");

  if (safe_write(sptr,parv0,filenamebuf,out,buffer))
    return;

  (void)ircsprintf(buffer, "K:%s:%s:%s\n",
	host,user,reason);
  if (safe_write(sptr,parv0,filenamebuf,out,buffer))
    return;

  (void)close(out);
}


#endif /* GLINES */

/*
 * m_kline()
 *
 * re-worked a tad ... -Dianora
 *
 * Added comstuds SEPARATE_QUOTE_KLINES_BY_DATE code
 * -Dianora
 */

int     m_kline(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
#if defined (LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
  struct pkl *k;
#else
  int out;
#endif
  char buffer[512];
  char *p;
  char cidr_form_host[64];

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  char filenamebuf[1024];
#endif
  char *filename;		/* filename to use for kline */

  char *user, *host;
  char *reason;
  char *current_date;
  int  ip_kline = NO;
  aClient *acptr;
  char tempuser[USERLEN+2];
  char temphost[HOSTLEN+1];
  aConfItem *aconf;
  int temporary_kline_time=0;	/* -Dianora */
  time_t temporary_kline_time_seconds=0;
  char *argv;
#ifdef SLAVE_SERVERS
  char *slave_oper;
#endif

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      sendto_realops("received kline from %s",
		     sptr->name,parv[0]);

      if(parc < 2)	/* pick up actual oper who placed kline */
	return 0;

      slave_oper = parv[1];	/* make it look like normal local kline */
      parc--;
      parv++;

      if ( parc < 2 )
	return 0;
      if(!find_special_conf(sptr->name,CONF_ULINE))
	{
	  sendto_realops("received Unauthorized kline from %s",sptr->name);
	  return 0;
	}
    }
  else
#endif
    {
#ifdef NO_LOCAL_KLINE
      if (!MyClient(sptr) || !IsOper(sptr))
	{
	  sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	  return 0;
	}
#else
      if (!MyClient(sptr) || !IsAnOper(sptr))
	{
	  sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	  return 0;
	}
#endif

      if(!IsSetOperK(sptr))
	{
	  sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
	  return 0;
	}

      if ( parc < 2 )
	{
	  sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		     me.name, parv[0], "KLINE");
	  return 0;
	}

#ifdef SLAVE_SERVERS
      if(MyConnect(sptr) && IsAnOper(sptr))
	{
	  sendto_slaves("KLINE",cptr,sptr,
			sptr->name,sptr->user->username,
			sptr->user->host,parc,parv);
	}
#endif
    }

  argv = parv[1];

  if( (temporary_kline_time = isnumber(argv)) )
    {
      if(parc < 3)
	{
#ifdef SLAVE_SERVERS
	  if(!IsServer(sptr))
#endif	  
	     sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
			me.name, parv[0], "KLINE");
	  return 0;
	}
      if(temporary_kline_time > (24*60))
        temporary_kline_time = (24*60); /* Max it at 24 hours */
      temporary_kline_time_seconds = (time_t)temporary_kline_time * (time_t)60;
	/* turn it into minutes */
      argv = parv[2];
      parc--;
    }

  if ( (host = strchr(argv, '@')) || *argv == '*' )
    {
      /* Explicit user@host mask given */

      if(host)			/* Found user@host */
	{
	  user = argv;	/* here is user part */
	  *(host++) = '\0';	/* and now here is host */
	}
      else
	{
	  user = "*";		/* no @ found, assume its *@somehost */
	  host = argv;
	}

      if (!*host)		/* duh. no host found, assume its '*' host */
	host = "*";
      strncpyzt(tempuser, user, USERLEN+2); /* allow for '*' in front */
      strncpyzt(temphost, host, HOSTLEN);
      user = tempuser;
      host = temphost;
    }
  else
    {
      /* Try to find user@host mask from nick */

      if (!(acptr = find_chasing(sptr, argv, NULL)))
	return 0;

      if(!acptr->user)
	return 0;

      if (IsServer(acptr))
	{
#ifdef SLAVE_SERVERS
	  if(!IsServer(sptr))
#endif
	    sendto_one(sptr,
              ":%s NOTICE %s :Can't KLINE a server, use @'s where appropriate",
		       me.name, parv[0]);
	  return 0;
	}

      if(IsElined(acptr))
	{
#ifdef SLAVE_SERVERS
	  if(!IsServer(sptr))
#endif
	    sendto_one(sptr,
		       ":%s NOTICE %s :%s is E-lined",me.name,parv[0],
		       acptr->name);
	  return 0;
	}

      /* turn the "user" bit into "*user", blow away '~'
	 if found in original user name (non-idented) */

      tempuser[0] = '*';
      if (*acptr->user->username == '~')
	strcpy(tempuser+1, (char *)acptr->user->username+1);
      else
	strcpy(tempuser+1, acptr->user->username);
      user = tempuser;
      host = cluster(acptr->user->host);
    }

  if(temporary_kline_time)
    argv = parv[3];
  else
    argv = parv[2];

  if (parc > 2)	
    {
      if(strchr(argv, ':'))
	{
#ifdef SLAVE_SERVERS
	  if(!IsServer(sptr))
#endif
	    sendto_one(sptr,
		       ":%s NOTICE %s :Invalid character ':' in comment",
		       me.name, parv[0]);
	  return 0;
	}

      if(strchr(argv, '#'))
        {
#ifdef SLAVE_SERVERS
	  if(!IsServer(sptr))
#endif
	    sendto_one(sptr,
		       ":%s NOTICE %s :Invalid character '#' in comment",
		       me.name, parv[0]);
          return 0;
        }

      if(*argv)
	reason = argv;
      else
	reason = "No reason";
    }
  else
    reason = "No reason";

  if (!matches(user, "akjhfkahfasfjd") &&
                !matches(host, "ldksjfl.kss...kdjfd.jfklsjf"))
    {
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
	sendto_one(sptr, ":%s NOTICE %s :Can't K-Line *@*",
		   me.name,
		   parv[0]);
      return 0;
    }
      
  if (bad_tld(host))
    {
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
	sendto_one(sptr, ":%s NOTICE %s :Can't K-Line *@%s",
		   me.name,
		   parv[0],
		   host);
      return 0;
    }

  /* 
  ** At this point, I know the user and the host to place the k-line on
  ** I also know whether its supposed to be a temporary kline or not
  ** I also know the reason field is clean
  ** Now what I want to do, is find out if its a kline of the form
  **
  ** /quote kline *@192.168.0.* i.e. it should be turned into a d-line instead
  **
  */

  /*
   * what to do if host is a legal ip, and its a temporary kline ?
   */

  if(host_is_legal_ip(host))
     {
       ip_kline = YES;
       strncpy(cidr_form_host,host,32);
       p = strchr(cidr_form_host,'*');
       if(p)
	 {
	   *p++ = '0';
	   *p++ = '/';
	   *p++ = '2';
	   *p++ = '4';
	   *p++ = '\0';
	 }
       host = cidr_form_host;
    }

#ifdef NON_REDUNDANT_KLINES
  if( (aconf = find_is_klined(host,user,0L)) )
     {
       char *reason;

       reason = aconf->passwd ? aconf->passwd : "<No Reason>";
#ifdef SLAVE_SERVERS
       if(!IsServer(sptr))
#endif
	 sendto_one(sptr,
		    ":%s NOTICE %s :[%s@%s] already K-lined by [%s@%s] - %s",
		    me.name,
		    parv[0],
		    user,host,
		    aconf->name,aconf->host,reason);
       return 0;
     }
#endif

  current_date = smalldate((time_t) 0);

  aconf = make_conf();
  aconf->status = CONF_KILL;
  DupString(aconf->host, host);

  if(temporary_kline_time)
    (void)ircsprintf(buffer, "Temporary K-line %d min. for %s (%s)",
       temporary_kline_time,reason,current_date);
  else
    (void)ircsprintf(buffer, "%s (%s)",reason,current_date);

  DupString(aconf->passwd, buffer );
  DupString(aconf->name, user);
  aconf->port = 0;

  if(temporary_kline_time)
    {
      aconf->hold = timeofday + temporary_kline_time_seconds;
      add_temp_kline(aconf);
      rehashed = YES;
      dline_in_progress = NO;
      nextping = timeofday;
      sendto_realops("%s added temporary %d min. K-Line for [%s@%s] [%s]",
                 parv[0], temporary_kline_time, user, host, reason);
      return 0;
    }

  Class(aconf) = find_class(0);

/* when can aconf->host ever be a NULL pointer though?
 * or for that matter, when is aconf->status ever not going
 * to be CONF_KILL at this point? 
 * -Dianora
 */

  if(ip_kline)
    add_ip_Kline(aconf);
  else
    add_mtrie_conf_entry(aconf,CONF_KILL);

/* comstud's SEPARATE_QUOTE_KLINES_BY_DATE code */
/* Note, that if SEPARATE_QUOTE_KLINES_BY_DATE is defined,
 *  it doesn't make sense to have LOCKFILE on the kline file
 */

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  timebuffer = small_file_date(NOW);

  (void)sprintf(filenamebuf, "%s.%s", klinefile, timebuffer);
  filename = filenamebuf;
       
#ifdef SLAVE_SERVERS
  if(!IsServer(sptr))
#endif
    sendto_one(sptr, ":%s NOTICE %s :Added K-Line [%s@%s] to %s",
	       me.name, parv[0], user, host, filenamebuf);
#else
  filename = klinefile;

#ifdef KPATH
#ifdef SLAVE_SERVERS
  if(!IsServer(sptr))
#endif
    sendto_one(sptr, ":%s NOTICE %s :Added K-Line [%s@%s] to server klinefile",
	       me.name, parv[0], user, host);
#else
#ifdef SLAVE_SERVERS
  if(!IsServer(sptr))
#endif
    sendto_one(sptr, ":%s NOTICE %s :Added K-Line [%s@%s] to server configfile",
	       me.name, parv[0], user, host);
#endif
#endif

  rehashed = YES;
  dline_in_progress = NO;
  nextping = timeofday;
  sendto_realops("%s added K-Line for [%s@%s] [%s]",
		 parv[0], user, host, reason);

#if defined(LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
/* MDP - Careful, don't cut & paste that ^O */

  if((k = (struct pkl *)malloc(sizeof(struct pkl))) == NULL)
    {
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
	sendto_one(sptr, ":%s NOTICE %s :Problem allocating memory",
		   me.name, parv[0]);
      return(0);
    }

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      (void)ircsprintf(buffer, "#%s K'd: %s@%s:%s\n",
		       slave_oper, user, host, reason);
    }
  else
#endif
    {
      (void)ircsprintf(buffer, "#%s!%s@%s K'd: %s@%s:%s\n",
		       sptr->name, sptr->user->username,
		       sptr->user->host, user, host,
		       reason);
    }
  
  if((k->comment = strdup(buffer)) == NULL)
    {
      free(k);
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
	sendto_one(sptr, ":%s NOTICE %s :Problem allocating memory",
		   me.name, parv[0]);
      return(0);
    }

  (void)ircsprintf(buffer, "K:%s:%s (%s):%s\n",
		   host,
		   reason,
		   current_date,
		   user);

  if((k->kline = strdup(buffer)) == NULL)
    {
      free(k->comment);
      free(k); 
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
	sendto_one(sptr, ":%s NOTICE %s :Problem allocating memory",
		   me.name, parv[0]);
      return(0);
    }       
  k->next = pending_klines;
  pending_klines = k; 
 
  do_pending_klines();
  return(0);

#else /* LOCKFILE - MDP and not SEPARATE_KLINES_BY_DATE */

  if ((out = open(filename, O_RDWR|O_APPEND|O_CREAT,0644))==-1)
    {
#ifdef SLAVE_SERVERS
      if(!IsServer(sptr))
#endif
	sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
		   me.name, parv[0], filename);
      return 0;
    }

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  fchmod(out, 0660);
#endif

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      (void)ircsprintf(buffer, "#%s K'd: %s@%s:%s\n",
		       slave_oper, user, host, reason);
    }
  else
#endif
    {
      (void)ircsprintf(buffer, "#%s!%s@%s K'd: %s@%s:%s\n",
		       sptr->name, sptr->user->username,
		       sptr->user->host, user, host,
		       reason);
    }

  if (safe_write(sptr,parv[0],filename,out,buffer))
    return 0;

  if (safe_write(sptr,parv[0],filename,out,buffer))
    return 0;

  (void)ircsprintf(buffer, "K:%s:%s (%s):%s\n",
		   host,
		   reason,
		   current_date,
		   user);

  if (safe_write(sptr,parv[0],filename,out,buffer))
    return 0;

  (void)close(out);

#ifdef USE_SYSLOG
  syslog(LOG_NOTICE, "%s added K-Line for [%s@%s] [%s]",
	 parv[0],
	 user,
	 host,
	 reason);
#endif

  return 0;
#endif /* #ifdef LOCKFILE */
}


/*
 * isnumber()
 * 
 * inputs	- pointer to ascii string in
 * output	- 0 if not an integer number, else the number
 * side effects	- none
*/

static int isnumber(char *p)
{
  int result = 0;

  while(*p)
    {
      if(isdigit(*p))
        {
          result *= 10;
          result += ((*p) & 0xF);
          p++;
        }
      else
        return(0);
    }
  /* in the degenerate case where oper does a /quote kline 0 user@host :reason 
     i.e. they specifically use 0, I am going to return 1 instead
     as a return value of non-zero is used to flag it as a temporary kline
  */

  if(result == 0)
    result = 1;
  return(result);
}

#ifdef UNKLINE
/*
** m_unkline
** Added Aug 31, 1997 
** common (Keith Fralick) fralick@gate.net
**
**      parv[0] = sender
**      parv[1] = address to remove
*
*
* re-worked and cleanedup for use in hybrid-5 
* -Dianora
*
* Added comstuds SEPARATE_QUOTE_KLINES_BY_DATE
*
*/
int m_unkline (aClient *cptr,aClient *sptr,int parc,char *parv[])
{
  int   in, out;
  int	pairme = NO;
  char	buf[256], buff[256];
  char	temppath[256];

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  char timebuffer[MAX_DATE_STRING];
  char filenamebuf[1024];
  struct tm *tmptr;
#endif
  char  *filename;		/* filename to use for unkline */

  char	*user,*host;
  char  *p;
  int   nread;
  int   error_on_write = NO;
  struct stat oldfilestat;
  mode_t oldumask;

  ircsprintf(temppath, "%s.tmp", klinefile);
  
  if (check_registered(sptr))
    {
      return -1;
    }

#ifdef NO_LOCAL_KLINE
  if(!IsOper(sptr))
#else
  if (!IsAnOper(sptr))  
#endif
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, 
		 parv[0]);
      return 0;
    }

  if (!IsSetOperUnkline(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no U flag",me.name,parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "UNKLINE");
      return 0;
    }

  if ( (host = strchr(parv[1], '@')) || *parv[1] == '*' )
    {
      /* Explicit user@host mask given */

      if(host)			/* Found user@host */
	{
	  user = parv[1];	/* here is user part */
	  *(host++) = '\0';	/* and now here is host */
	}
      else
	{
	  user = "*";		/* no @ found, assume its *@somehost */
	  host = parv[1];
	}
    }
  else
    {
      sendto_one(sptr, ":%s NOTICE %s :Invalid parameters",
		 me.name, parv[0]);
      return 0;
    }

  if( (user[0] == '*') && (user[1] == '\0')
      && (host[0] == '*') && (host[1] == '\0') )
    {
      sendto_one(sptr, ":%s NOTICE %s :Cannot UNK-Line everyone",
		 me.name, parv[0]);
      return 0;
    }

  if(remove_tkline_match(host,user))
    {
      sendto_one(sptr, ":%s NOTICE %s :Un-klined [%s@%s] from temporary k-lines",
		 me.name, parv[0],user, host);
      sendto_ops("%s has removed the temporary K-Line for: [%s@%s]",
		 parv[0], user, host );

#ifdef USE_SYSLOG
      syslog(LOG_NOTICE, "%s removed temporary K-Line for [%s@%s]",
	     parv[0],
	     user,
	     host);
#endif
      return 0;
    }

#if defined(LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
  if(lock_kline_file() < 0 )
    {
      sendto_one(sptr,":%s NOTICE %s :%s is locked try again in a few minutes",
	me.name,parv[0],klinefile);
      return -1;
    }
#endif

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  tmptr = localtime(&NOW);
  strftime(timebuffer, MAX_DATE_STRING, "%y%m%d", tmptr);
  (void)sprintf(filenamebuf, "%s.%s", klinefile, timebuffer);
  filename = filenamebuf;
#else
  filename = klinefile;
#endif			

  if( (in = open(filename, O_RDONLY)) == -1)
    {
      sendto_one(sptr, ":%s NOTICE %s :Cannot open %s",
	me.name,parv[0],filename);
#if defined(LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
      (void)unlink(LOCKFILE);
#endif
      return 0;
    }
  if(fstat(in, &oldfilestat) < 0)	/* Save the old file mode */
    oldfilestat.st_mode = 0644;

  oldumask = umask(0);			/* ircd is normally too paranoid */
  if( (out = open(temppath, O_WRONLY|O_CREAT, oldfilestat.st_mode)) == -1)
    {
      sendto_one(sptr, ":%s NOTICE %s :Cannot open %s",
        me.name,parv[0],temppath);
      (void)close(in);
#if defined (LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
      (void)unlink(LOCKFILE);
#endif
      umask(oldumask);			/* Restore the old umask */
      return 0;
    }
    umask(oldumask);			/* Restore the old umask */

/*
#Dianora!db@ts2-11.ottawa.net K'd: foo@bar:No reason
K:bar:No reason (1997/08/30 14.56):foo
*/

  while((nread = dgets(in, buf, sizeof(buf)) ) > 0) 
    {
      buf[nread] = '\0';

      if((buf[1] == ':') && ((buf[0] == 'k') || (buf[0] == 'K')))
	{
	  /* its a K: line */
	  char *found_host;
	  char *found_user;
	  char *found_comment;

	  strcpy(buff,buf);

	  p = strchr(buff,'\n');
	  if(p)
	    *p = '\0';
	  p = strchr(buff,'\r');
	  if(p)
	    *p = '\0';

	  found_host = buff + 2;	/* point past the K: */
	  p = strchr(found_host,':');
	  if(p == (char *)NULL)
	    {
	      sendto_one(sptr, ":%s NOTICE %s :K-Line file corrupted",
			 me.name, parv[0]);
	      sendto_one(sptr, ":%s NOTICE %s :Couldn't find host",
			 me.name, parv[0]);
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
	      continue;		/* This K line is corrupted ignore */
	    }
	  *p = '\0';
	  p++;
 
	  found_comment = p;
	  p = strchr(found_comment,':');
	  if(p == (char *)NULL)
	    {
	      sendto_one(sptr, ":%s NOTICE %s :K-Line file corrupted",
			 me.name, parv[0]);
	      sendto_one(sptr, ":%s NOTICE %s :Couldn't find comment",
			 me.name, parv[0]);
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
	      continue;		/* This K line is corrupted ignore */
	    }
          *p = '\0';
          p++;
	  found_user = p;

/*
 * Ok, if its not an exact match on either the user or the host
 * then, write the K: line out, and I add it back to the K line
 * tree
 */
	  if(strcasecmp(host,found_host) || strcasecmp(user,found_user))
            {
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
            }
          else
	    pairme++;

	}				
      else if(buf[0] == '#')
	{
	  char *userathost;
	  char *found_user;
	  char *found_host;

	  strcpy(buff,buf);
/*
#Dianora!db@ts2-11.ottawa.net K'd: foo@bar:No reason
K:bar:No reason (1997/08/30 14.56):foo

If its a comment coment line, i.e.
#ignore this line
Then just ignore the line
*/
	  p = strchr(buff,':');
	  if(p == (char *)NULL)
	    {
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
	      continue;
	    }
	  *p = '\0';
	  p++;

	  userathost = p;
	  p = strchr(userathost,':');

	  if(p == (char *)NULL)
	    {
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
	      continue;
	    }
          *p = '\0';

	  while(*userathost == ' ')
	    userathost++;

	  found_user = userathost;
	  p = strchr(found_user,'@');
	  if(p == (char *)NULL)
	    {
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
	      continue;
	    }
	  *p = '\0';
	  found_host = p;
	  found_host++;

	  if( (strcasecmp(found_host,host)) || (strcasecmp(found_user,user)) )
	    {
              if(!error_on_write)
                error_on_write = flush_write(sptr,
		  out,buf,strlen(buf),temppath);
            }
	}
      else	/* its the ircd.conf file, and not a K line or comment */
        {
	  if(!error_on_write)
            error_on_write = flush_write(sptr,
              out,buf,strlen(buf),temppath);
        }
    }

  (void)close(in);

/* The result of the rename should be checked too... oh well */
/* If there was an error on a write above, then its been reported
 * and I am not going to trash the original kline /conf file
 * -Dianora
 */
  if( (!error_on_write) && (close(out) >= 0) )
    {
      (void)rename(temppath, filename);
      rehash(cptr,sptr,0);
    }
  else
    {
      sendto_one(sptr,":%s NOTICE %s :Couldn't write temp kline file, aborted",
        me.name,parv[0]);
#if defined (LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
      (void)unlink(LOCKFILE);
#endif
      return -1;
    }

#if defined (LOCKFILE) && !defined(SEPARATE_QUOTE_KLINES_BY_DATE)
  (void)unlink(LOCKFILE);
#endif
	
  if(!pairme)
    {
      sendto_one(sptr, ":%s NOTICE %s :No K-Line for %s@%s",
		 me.name, parv[0],user,host);
      return 0;
    }
  sendto_one(sptr, ":%s NOTICE %s :K-Line for [%s@%s] is removed", 
	     me.name, parv[0], user,host);
  sendto_ops("%s has removed the K-Line for: [%s@%s]",
	     parv[0], user, host);

#ifdef USE_SYSLOG
      syslog(LOG_NOTICE, "%s removed K-Line for [%s@%s]",
	     parv[0],
	     user,
	     host);
#endif
  return 0; 
}

/*
 * flush_write()
 *
 * inputs	- pointer to client structure of oper requesting unkline
 *              - out is the file descriptor
 *		- buf is the buffer to write
 *		- ntowrite is the expected number of character to be written
 *		- temppath is the temporary file name to be written
 * output	- YES for error on write
 *		- NO for success
 * side effects	- if successful, the buf is written to output file
 *                if a write failure happesn, and the file pointed to
 *		  by temppath, if its non NULL, is removed.
 *
 * The idea here is, to be as robust as possible when writing to the 
 * kline file.
 *
 * -Dianora
 */

static int flush_write(aClient *sptr,
  int out,char *buf,int ntowrite,char *temppath)
{
  int nwritten;
  int error_on_write = NO;

  nwritten = write(out,buf,ntowrite);
  if(nwritten != ntowrite)
    {
      sendto_one(sptr,":%s NOTICE %s :Unable to write to %s",
        me.name, sptr->name, temppath );
      error_on_write = YES;
      (void)close(out);
      if(temppath != (char *)NULL)
        (void)unlink(temppath);
    }
  return(error_on_write);
}

/*
** remove_tkline_match()
*
* un-kline a temporary k-line. 
*
*/
static int remove_tkline_match(char *host,char *user)
{
  aConfItem *kill_list_ptr;
  aConfItem *last_kill_ptr=(aConfItem *)NULL;

  if(!temporary_klines)
    return NO;

  kill_list_ptr = temporary_klines;

  while(kill_list_ptr)
    {
      if( !strcasecmp(kill_list_ptr->host,host)
	  && !strcasecmp(kill_list_ptr->name,user))	/* match */
	{
	  if(last_kill_ptr)
	    last_kill_ptr->next = kill_list_ptr->next;
	  else
	    temporary_klines = kill_list_ptr->next;
	  free_conf(kill_list_ptr);
	  return YES;
	}
      else
	{
	  last_kill_ptr = kill_list_ptr;
	  kill_list_ptr = kill_list_ptr->next;
	}
    }
  return NO;
}
#endif

/*
 * re-worked a tad
 * added Rodders dated KLINE code
 * -Dianora
 *
 * BUGS:	There is a concern here with LOCKFILE handling
 * the LOCKFILE code only knows how to talk to the kline file.
 * Having an extra define allowing D lines to go into ircd.conf or
 * the kline file complicates life. LOCKFILE code should still respect
 * any lock placed.. The fix I believe is that we are going to have
 * to also pass along in the struct pkl struct, which file the entry
 * is to go into... or.. just remove the DLINES_IN_KPATH option.
 * -Dianora
 */

int     m_dline(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  char *host, *reason;
  char *p;
  int number_of_dots=0;
  int number_of_chars=0;
  aClient *acptr;
  char cidr_form_host[64];

#ifdef NO_LOCAL_KLINE
  if (!MyClient(sptr) || !IsOper(sptr))
#else
  if (!MyClient(sptr) || !IsAnOper(sptr))
#endif
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if(!IsSetOperK(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
      return 0;
    }

  if ( parc < 2 )
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "KLINE");
      return 0;
    }

  host = parv[1];

  p = host;
  while(*p)
    {
      if(*p == '.')
	{
	  number_of_dots++;
	  p++;
	  number_of_chars++;
	}
      else if(*p == '/')	/* I'm going to gamble
				   that it is a legal ddd.ddd.ddd.ddd/mask */
	break;
      else if(*p == '*')
	{
	  if(number_of_dots != 3)
	    {
	      sendto_one(sptr, ":%s NOTICE %s :D-line ddd.ddd.ddd.*",
				 me.name, parv[0]);
              return 0;
            }
          else
           {
	     *(p+1) = '\0';
	     /* I know its a dline old style of form nnn.nnn.nnn.*
	      * convert to cidr style here *sigh* -Dianora
	      */
	     strncpy(cidr_form_host,host,32);
	     p = strchr(cidr_form_host,'*');
	     if(p)
	       {
		 *p++ = '0';
		 *p++ = '/';
		 *p++ = '2';
		 *p++ = '4';
		 *p++ = '\0';
		 number_of_chars += 4;
	       }
	     host = cidr_form_host;
	     break;
           }
	}
      else
	{
	  if(!isdigit(*p))
	    {
	      if (!(acptr = find_chasing(sptr, parv[1], NULL)))
		return 0;

	      if(!acptr->user)
		return 0;

	      if (IsServer(acptr))
		{
		  sendto_one(sptr,
			     ":%s NOTICE %s :Can't DLINE a server silly",
			     me.name, parv[0]);
		  return 0;
		}
	      
	      if(!MyConnect(acptr))
		{
		  sendto_one(sptr,
			  ":%s NOTICE :%s :Can't DLINE nick on another server",
			  me.name, parv[0]);
		  return 0;
		}

	      host = cluster(acptr->hostip);
	      strncpy(cidr_form_host,host,32);
	      p = strchr(cidr_form_host,'*');
	      if(p)
		{
		  *p++ = '0';
		  *p++ = '/';
		  *p++ = '2';
		  *p++ = '4';
		  *p++ = '\0';
		}
	     host = cidr_form_host;
	     break;
	      break;
	    }
	  p++;
	  number_of_chars++;
	  /* ddd.ddd.ddd.ddd == 15 ddd.ddd.ddd.000/24 == 18 */
	  if(number_of_chars > 18)
	    {
	      if (!(acptr = find_chasing(sptr, parv[1], NULL)))
		return 0;
	      break;
	    }
	}
    }

  if (parc > 2)	/* host :reason */
    {
      if(strchr(parv[2], ':'))
	{
	  sendto_one(sptr,
		     ":%s NOTICE %s :Invalid character ':' in comment",
		     me.name, parv[0]);
	  return 0;
	}

      if(strchr(parv[2], '#'))
        {
          sendto_one(sptr,
                     ":%s NOTICE %s :Invalid character '#' in comment",
                     me.name, parv[0]);
          return 0;
        }

      if(*parv[2])
	reason = parv[2];
      else
	reason = "No reason";
    }
  else
    reason = "No reason";


  /*
  ** At this point, I know the host to place the d-line on
  ** I also know the reason field is clean
  **
  */

  return place_dline(sptr,parv[0],host,reason); 
}

/*
** place_dline()
** actual code that places the d-line
*/

int     place_dline(aClient *sptr,
		    char *parv0,
		    char *host,
		    char *reason)
{
#if defined(LOCKFILE) && defined(DLINES_IN_KPATH)
  struct pkl *k;
#else
  int out;
#endif
  char buffer[1024];
  char *current_date;
  unsigned long ip_host;
  unsigned long ip_host_network_order;
  unsigned long ip_mask;
  aConfItem *aconf;

  ip_host = host_name_to_ip(host,&ip_mask); /* returns in =host= order */
  ip_host_network_order = ntohl(ip_host);   /* get it in network order 
					     * its used only for 
					     * find_host_in_dline_hash
					     */

  if((ip_mask & 0xFFFFFF00) ^ 0xFFFFFF00)
    {
      if(ip_mask != 0xFFFFFFFF)
	{
	  sendto_one(sptr, ":%s NOTICE %s :Can't use a mask less than 24 with dline",
		     me.name,
		     parv0);
	  return 0;
	}
    }

#ifdef NON_REDUNDANT_KLINES
  if( aconf = match_Dline(ip_host_network_order)) 
     {
       char *reason;
       reason = aconf->passwd ? aconf->passwd : "<No Reason>";
       if(aconf->status & CONF_ELINE)
	 sendto_one(sptr, ":%s NOTICE %s :[%s] is (E)d-lined by [%s] - %s",
		    me.name,
		    parv0,
		    host,
		    aconf->host,reason);
	 else
	   sendto_one(sptr, ":%s NOTICE %s :[%s] already D-lined by [%s] - %s",
		      me.name,
		      parv0,
		      host,
		      aconf->host,reason);
      return 0;
       
     }
#endif

  current_date = smalldate((time_t) 0);

  (void)ircsprintf(buffer, "%s (%s)",reason,current_date);

  aconf = make_conf();
  aconf->status = CONF_DLINE;
  DupString(aconf->host,host);
  DupString(aconf->passwd,buffer);

  add_Dline(aconf);
  sendto_realops("%s added D-Line for [%s] [%s]",
		 parv0, host, reason);

#ifdef DLINES_IN_KPATH
  sendto_one(sptr, ":%s NOTICE %s :Added D-Line [%s] to server klinefile",
	     me.name, parv0, host);
#else
  sendto_one(sptr, ":%s NOTICE %s :Added D-Line [%s] to server configfile",
          me.name, parv0, host);
#endif

  /*
  ** I moved the following 2 lines up here
  ** because we still want the server to
  ** hunt for 'targetted' clients even if
  ** there are problems adding the D-line
  ** to the appropriate file. -ThemBones
  */
  rehashed = YES;
  dline_in_progress = YES;
  nextping = timeofday;

/* MDP - Careful, don't cut & paste that ^O */
#if defined(LOCKFILE) && defined(DLINES_IN_KPATH)

  if((k = (struct pkl *)MyMalloc(sizeof(struct pkl))) == NULL)
    {
      sendto_one(sptr, ":%s NOTICE %s :Problem allocating memory",
		 me.name, parv0);
      return(0);
    }

  (void)ircsprintf(buffer, "#%s!%s@%s D'd: %s:%s (%s)\n",
		   sptr->name, sptr->user->username,
		   sptr->user->host, host,
		   reason, current_date);

  if((k->comment = strdup(buffer)) == NULL)
    {
      free(k);
      sendto_one(sptr, ":%s NOTICE %s :Problem allocating memory",
		 me.name, parv0);
      return(0);
    }

  (void)ircsprintf(buffer, "D:%s:%s (%s)\n",
		   host,
		   reason,
		   current_date);

  if((k->kline = strdup(buffer)) == NULL)
    {
      free(k->comment);
      free(k); 
      sendto_one(sptr, ":%s NOTICE %s :Problem allocating memory",
		 me.name, parv0);
      return(0);
    }       
  k->next = pending_klines;
  pending_klines = k; 
 
  do_pending_klines();
  return(0);

#else /* LOCKFILE - MDP */

  if ((out = open(dlinefile, O_RDWR|O_APPEND|O_CREAT,0644))==-1)
    {
      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
		 me.name, parv0, dlinefile);
      return 0;
    }

  (void)ircsprintf(buffer, "#%s!%s@%s D'd: %s:%s (%s)\n",
		   sptr->name, sptr->user->username,
		   sptr->user->host, host,
		   reason, current_date);

  if (safe_write(sptr,parv0,dlinefile,out,buffer))
    return 0;

  (void)ircsprintf(buffer, "D:%s:%s (%s)\n",
		   host,
		   reason,
		   current_date);

  if (safe_write(sptr,parv0,dlinefile,out,buffer))
    return 0;

  (void)close(out);
  return 0;
#endif
}


#if defined(OPER_REHASH) || defined(LOCOP_REHASH)
/*
** m_rehash
**
*/
int	m_rehash(aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{
  char  sparemsg[80];
  int found = NO;

#ifndef	LOCOP_REHASH
  if (!MyClient(sptr) || !IsOper(sptr))
#else
# ifdef	OPER_REHASH
  if (!MyClient(sptr) || !IsAnOper(sptr))
# else
  if (!MyClient(sptr) || !IsLocOp(sptr))
# endif
#endif
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if(parc > 1)
    {
      if(mycmp(parv[1],"DNS") == 0)
	{
	  sendto_one(sptr, rpl_str(RPL_REHASHING), me.name, parv[0], "DNS");
          sendto_ops("%s is rehashing DNS while whistling innocently",
                 parv[0]);
	  flush_cache();	/* flush the dns cache */
	  close(spare_fd);
	  res_init();		/* re-read /etc/resolv.conf */
	  spare_fd = open("/dev/null",O_RDONLY,0);
          if ((spare_fd < 0) || (spare_fd > 256))
            {
              ircsprintf(sparemsg,"invalid spare_fd %d",spare_fd);
              restart(sparemsg);
            }
/* 	  return 0; 
	  restart_resolver(); */	/* re-read /etc/resolv.conf AGAIN?
					   and close/re-open res socket */
	  found = YES;
	}
      else if(mycmp(parv[1],"TKLINES") == 0)
	{
	  sendto_one(sptr, rpl_str(RPL_REHASHING), me.name, parv[0], "temp klines");
	  flush_temp_klines();
	  sendto_ops("%s is clearing temp klines while whistling innocently",
		 parv[0]);
	  found = YES;
	}
#ifdef GLINES
      else if(mycmp(parv[1],"GLINES") == 0)
	{
	  sendto_one(sptr, rpl_str(RPL_REHASHING), me.name, parv[0], "g-lines");
	  flush_glines();
	  sendto_ops("%s is clearing G-lines while whistling innocently",
		 parv[0]);
	  found = YES;
	}
#endif
      else if(mycmp(parv[1],"GC") == 0)
	{
	  sendto_one(sptr, rpl_str(RPL_REHASHING), me.name, parv[0], "garbage collecting");
	  block_garbage_collect();
	  sendto_ops("%s is garbage collecting while whistling innocently",
		 parv[0]);
	  found = YES;
	}
      else if(mycmp(parv[1],"MOTD") == 0)
        {
	  sendto_ops("%s is forcing re-reading of MOTD file",parv[0]);
          read_motd();
#ifdef AMOTD
	  read_amotd();
#endif
	  found = YES;
        }
#ifdef OPER_MOTD
      else if(mycmp(parv[1],"OMOTD") == 0)
        {
	  sendto_ops("%s is forcing re-reading of OPER MOTD file",parv[0]);
	  read_oper_motd();
	  found = YES;
        }
#endif
      else if(mycmp(parv[1],"HELP") == 0)
        {
	  sendto_ops("%s is forcing re-reading of oper help file",parv[0]);
          read_help();
	  found = YES;
        }
      else if(mycmp(parv[1],"dump") == 0)
	{
	  sendto_ops("%s is dumping conf file",parv[0]);
	  rehash_dump(sptr,parv[0]);
	  found = YES;
	}
      else if(mycmp(parv[1],"dlines") == 0)
	{
	  sendto_one(sptr, rpl_str(RPL_REHASHING), me.name, parv[0], configfile);
	  sendto_ops("%s is rehashing Server config file while whistling innocently",
		     parv[0]);
#ifdef USE_SYSLOG
	  syslog(LOG_NOTICE, "REHASH From %s\n", get_client_name(sptr, FALSE));
#endif
	  dline_in_progress = 1;
	  return rehash(cptr, sptr, (parc > 1) ? ((*parv[1] == 'q')?2:0) : 0);
	}
      if(found)
	{
#ifdef USE_SYSLOG
	  syslog(LOG_NOTICE, "REHASH %s From %s\n",
		 parv[1],
		 get_client_name(sptr, FALSE));
#endif
	  return 0;
	}
      else
	{
#undef OUT

#ifdef GLINES
#define OUT "rehash one of :DNS TKLINES GLINES GC MOTD DUMP"
#else
#define OUT "rehash one of :DNS TKLINES GC MOTD DUMP"
#endif
	  sendto_one(sptr,":%s NOTICE %s : " OUT,me.name,sptr->name);
	  return(0);
	}
    }
  else
    {
      sendto_one(sptr, rpl_str(RPL_REHASHING), me.name, parv[0], configfile);
      sendto_ops("%s is rehashing Server config file while whistling innocently",
		 parv[0]);
#ifdef USE_SYSLOG
      syslog(LOG_NOTICE, "REHASH From %s\n", get_client_name(sptr, FALSE));
#endif
      return rehash(cptr, sptr, (parc > 1) ? ((*parv[1] == 'q')?2:0) : 0);
    }
  return 0; /* shouldn't ever get here */
}
#endif

#if defined(OPER_RESTART) || defined(LOCOP_RESTART)
/*
** m_restart
**
*/
int	m_restart(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{
#ifndef	LOCOP_RESTART
  if (!MyClient(sptr) || !IsOper(sptr))
#else
# ifdef	OPER_RESTART
  if (!MyClient(sptr) || !IsAnOper(sptr))
# else
  if (!MyClient(sptr) || !IsLocOp(sptr))
# endif
#endif
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
#ifdef USE_SYSLOG
  syslog(LOG_WARNING, "Server RESTART by %s\n",
	 get_client_name(sptr,FALSE));
#endif
  sprintf(buf, "Server RESTART by %s", get_client_name(sptr, TRUE));
  restart(buf);
  return 0; /*NOT REACHED*/
}
#endif

/*
** m_trace
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_trace(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  Reg	int	i;
  Reg	aClient	*acptr = NULL;
  aClass	*cltmp;
  char	*tname;
  int	doall, link_s[MAXCONNECTIONS], link_u[MAXCONNECTIONS];
  int	cnt = 0, wilds, dow;
  static time_t last_trace=0L;
  static time_t last_used=0L;

  if (parc > 2)
    if (hunt_server(cptr, sptr, ":%s TRACE %s :%s",
		    2, parc, parv))
      return 0;
  
  if (parc > 1)
    tname = parv[1];
  else
    {
      tname = me.name;
    }

  switch (hunt_server(cptr, sptr, ":%s TRACE :%s", 1, parc, parv))
    {
    case HUNTED_PASS: /* note: gets here only if parv[1] exists */
      {
	aClient	*ac2ptr;
	
	ac2ptr = next_client_double(client, tname);
	if (ac2ptr)
	  sendto_one(sptr, rpl_str(RPL_TRACELINK), me.name, parv[0],
		     version, debugmode, tname, ac2ptr->from->name);
	else
	  sendto_one(sptr, rpl_str(RPL_TRACELINK), me.name, parv[0],
		     version, debugmode, tname, "ac2ptr_is_NULL!!");
	return 0;
      }
    case HUNTED_ISME:
      break;
    default:
      return 0;
    }

  if(!IsAnOper(sptr))
    {
      /* pacing for /trace is problemmatical */
#if 0
      if((last_used + PACE_WAIT) > NOW)
        {
          return 0;
        }
      else
        {
          last_used = NOW;
        }
#endif

      if (parv[1] && !index(parv[1],'.') && (index(parv[1], '*')
          || index(parv[1], '?'))) /* bzzzt, no wildcard nicks for nonopers */
        {
          sendto_one(sptr, rpl_str(RPL_ENDOFTRACE),me.name,
                     parv[0], parv[1]);
          return 0;
        }
    }

  sendto_realops_lev(SPY_LEV, "trace requested by %s (%s@%s) [%s]",
		     sptr->name, sptr->user->username, sptr->user->host,
		     sptr->user->server);


  doall = (parv[1] && (parc > 1)) ? !matches(tname, me.name): TRUE;
  wilds = !parv[1] || strchr(tname, '*') || strchr(tname, '?');
  dow = wilds || doall;
  
  if(!IsAnOper(sptr) || !dow) /* non-oper traces must be full nicks */
                              /* lets also do this for opers tracing nicks */
    {
      char      *name;
      int       class;

      acptr = hash_find_client(tname,(aClient *)NULL);
      if(!acptr || !IsPerson(acptr)) 
        {
	  /* this should only be reached if the matching
	     target is this server */
	  sendto_one(sptr, rpl_str(RPL_ENDOFTRACE),me.name,
		     parv[0], tname);
          return 0;
        }
      name = get_client_name(acptr,FALSE);
      class = get_client_class(acptr);
      if (IsAnOper(acptr))
        {
          sendto_one(sptr, rpl_str(RPL_TRACEOPERATOR),
                     me.name, parv[0], class, name);
        }
      else
        {
          sendto_one(sptr,rpl_str(RPL_TRACEUSER),
                     me.name, parv[0], class, name);
        }
      sendto_one(sptr, rpl_str(RPL_ENDOFTRACE),me.name,
		 parv[0], tname);
      return 0;
    }

  if (dow && lifesux && !IsOper(sptr))
    {
      sendto_one(sptr,rpl_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }

  memset((void *)link_s,0,sizeof(link_s));
  memset((void *)link_u,0,sizeof(link_u));

  /*
   * Count up all the servers and clients in a downlink.
   */
  if (doall)
   {
    for (acptr = client; acptr; acptr = acptr->next)
#ifdef	SHOW_INVISIBLE_LUSERS
      if (IsPerson(acptr))
	link_u[acptr->from->fd]++;
#else
      if (IsPerson(acptr) &&
        (!IsInvisible(acptr) || IsAnOper(sptr)))
        {
          link_u[acptr->from->fd]++;
        }
#endif
   }
  else
    {
      if (IsServer(acptr))
      {
        link_s[acptr->from->fd]++;
      }
    }

  /* report all direct connections */
	
  for (i = 0; i <= highest_fd; i++)
    {
      char	*name;
      int	class;
      
      if (!(acptr = local[i])) /* Local Connection? */
	continue;
      if (IsInvisible(acptr) && dow &&
	  !(MyConnect(sptr) && IsAnOper(sptr)) &&
	  !IsAnOper(acptr) && (acptr != sptr))
	continue;
      if (!doall && wilds && matches(tname, acptr->name))
	continue;
      if (!dow && mycmp(tname, acptr->name))
	continue;
      name = get_client_name(acptr,FALSE);
      class = get_client_class(acptr);
      
      switch(acptr->status)
	{
	case STAT_CONNECTING:
	  sendto_one(sptr, rpl_str(RPL_TRACECONNECTING), me.name,
		     parv[0], class, name);
	  cnt++;
	  break;
	case STAT_HANDSHAKE:
	  sendto_one(sptr, rpl_str(RPL_TRACEHANDSHAKE), me.name,
		     parv[0], class, name);
	  cnt++;
	  break;
	case STAT_ME:
	  break;
	case STAT_UNKNOWN:
/* added time -Taner */
	  sendto_one(sptr, rpl_str(RPL_TRACEUNKNOWN),
		     me.name, parv[0], class, name,
		     acptr->firsttime ? timeofday - acptr->firsttime : -1);
	  cnt++;
	  break;
	case STAT_CLIENT:
	  /* Only opers see users if there is a wildcard
	   * but anyone can see all the opers.
	   */
	  if ((IsAnOper(sptr) &&
	      (MyClient(sptr) || !(dow && IsInvisible(acptr))))
	      || !dow || IsAnOper(acptr))
	    {
	      if (IsAnOper(acptr))
		sendto_one(sptr,
			   rpl_str(RPL_TRACEOPERATOR),
			   me.name,
			   parv[0], class, name);
	      else
		sendto_one(sptr,rpl_str(RPL_TRACEUSER),
			   me.name, parv[0], class, name,
			   (acptr->user)?(timeofday - acptr->user->last):0);
	      cnt++;
	    }
	  break;
	case STAT_SERVER:
	  if (acptr->serv->user)
	    sendto_one(sptr, rpl_str(RPL_TRACESERVER),
		       me.name, parv[0], class, link_s[i],
		       link_u[i], name, acptr->serv->by,
		       acptr->serv->user->username,
		       acptr->serv->user->host);
	  else
	    sendto_one(sptr, rpl_str(RPL_TRACESERVER),
		       me.name, parv[0], class, link_s[i],
		       link_u[i], name, *(acptr->serv->by) ?
		       acptr->serv->by : "*", "*", me.name);
	  cnt++;
	  break;
	case STAT_LOG:
	  sendto_one(sptr, rpl_str(RPL_TRACELOG), me.name,
		     parv[0], LOGFILE, acptr->port);
	  cnt++;
	  break;
	default: /* ...we actually shouldn't come here... --msa */
	  sendto_one(sptr, rpl_str(RPL_TRACENEWTYPE), me.name,
		     parv[0], name);
	  cnt++;
	  break;
	}
    }
  /*
   * Add these lines to summarize the above which can get rather long
   * and messy when done remotely - Avalon
   */
  if (!SendWallops(sptr) || !cnt)
    {
      if (cnt)
	{
	  sendto_one(sptr, rpl_str(RPL_ENDOFTRACE),me.name,
		     parv[0],tname);
	  return 0;
	}
      /* let the user have some idea that its at the end of the
       * trace
       */
      sendto_one(sptr, rpl_str(RPL_TRACESERVER),
		 me.name, parv[0], 0, link_s[me.fd],
		 link_u[me.fd], me.name, "*", "*", me.name);
      sendto_one(sptr, rpl_str(RPL_ENDOFTRACE),me.name,
		 parv[0],tname);
      return 0;
    }
  for (cltmp = FirstClass(); doall && cltmp; cltmp = NextClass(cltmp))
    if (Links(cltmp) > 0)
      sendto_one(sptr, rpl_str(RPL_TRACECLASS), me.name,
		 parv[0], Class(cltmp), Links(cltmp));
  sendto_one(sptr, rpl_str(RPL_ENDOFTRACE),me.name, parv[0],tname);
  return 0;
}

/*
** m_motd
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_motd(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aMessageFile *motd_ptr=motd;
  static time_t last_used=0L;

  if (hunt_server(cptr, sptr, ":%s MOTD :%s", 1,parc,parv)!=HUNTED_ISME)
    return 0;

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
	return 0;
      else
	last_used = NOW;
    }

  /* Yes, its a kludge, but it works -Dianora */
#ifdef AMOTD
  if(IsLocal(sptr) && (parc > 1) && (*parv[1] == 'A'))
    {
      motd_ptr = amotd;
      parc--;
    }
#endif

  sendto_realops_lev(SPY_LEV, "motd requested by %s (%s@%s) [%s]",
		     sptr->name, sptr->user->username, sptr->user->host,
		     sptr->user->server);

  return(send_motd(cptr,sptr,parc,parv,motd_ptr));
}

/*
** send_motd
**	parv[0] = sender prefix
**	parv[1] = servername
**
** This function split off so a server notice could be generated on a
** user requested motd, but not on each connecting client.
** -Dianora
*/

int send_motd(aClient *cptr,
	  aClient *sptr,
	  int parc,
	  char *parv[],
	      aMessageFile *motd)
{
  register aMessageFile *temp;

  if (motd == (aMessageFile *)NULL)
    {
      sendto_one(sptr, err_str(ERR_NOMOTD), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr, rpl_str(RPL_MOTDSTART), me.name, parv[0], me.name);

  sendto_one(sptr,":%s NOTICE %s :%s",me.name,parv[0],motd_last_changed_date);

  temp = motd;
  while(temp)
    {
      sendto_one(sptr,
		 rpl_str(RPL_MOTD),
		 me.name, parv[0], temp->line);
      temp = temp->next;
    }
  sendto_one(sptr, rpl_str(RPL_ENDOFMOTD), me.name, parv[0]);
  return 0;
}

#ifdef OPER_MOTD
/*
** send_oper_motd
**	parv[0] = sender prefix
**	parv[1] = servername
**
** This function split off so a server notice could be generated on a
** user requested motd, but not on each connecting client.
** -Dianora
*/

int send_oper_motd(aClient *cptr,
	  aClient *sptr,
	  int parc,
	  char *parv[],
	      aMessageFile *motd)
{
  register aMessageFile *temp;
  
  if (opermotd == (aMessageFile *)NULL)
    {
      sendto_one(sptr, ":%s NOTICE %s :No OPER MOTD", me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr,":%s NOTICE %s :Start of OPER MOTD",
	     me.name,parv[0]);

  sendto_one(sptr,":%s NOTICE %s :%s",me.name,parv[0],
	     oper_motd_last_changed_date);

  temp = motd;
  while(temp)
    {
      sendto_one(sptr,":%s NOTICE %s :%s", me.name, parv[0], temp->line);
      temp = temp->next;
    }
  return 0;
}
#endif

/*
 * read_motd()
 *
 */

void read_motd()
{
  struct stat sb;
  struct tm *motd_tm;

  if(read_message_file(MOTD,&motd) < 0)
    return;

  stat(MOTD, &sb);
  motd_tm = localtime(&sb.st_mtime);

  if (motd_tm)
    (void)sprintf(motd_last_changed_date,
		  "%d/%d/%d %02d:%02d",
		  motd_tm->tm_mday,
		  motd_tm->tm_mon + 1,
		  1900 + motd_tm->tm_year,
		  motd_tm->tm_hour,
		  motd_tm->tm_min);
}

#ifdef OPER_MOTD
void read_oper_motd()
{
  struct stat	sb;
  struct tm *motd_tm;

  if(read_message_file(OMOTD,&opermotd) < 0)
    return;

  stat(OMOTD, &sb);
  motd_tm = localtime(&sb.st_mtime);

  if (motd_tm)
    (void)sprintf(oper_motd_last_changed_date,
		  "%d/%d/%d %02d:%02d",
		  motd_tm->tm_mday,
		  motd_tm->tm_mon + 1,
		  1900 + motd_tm->tm_year,
		  motd_tm->tm_hour,
		  motd_tm->tm_min);
}
#endif

#ifdef AMOTD
/*
 * read_amotd()
 *
 */

void read_amotd()
{
  struct stat	sb;

  if(read_message_file(AMOTD,&amotd) < 0)
    return;

  stat(AMOTD, &sb);
  motd_tm = localtime(&sb.st_mtime);

  if (motd_tm)
    (void)sprintf(amotd_last_changed_date,
		  "%d/%d/%d %d:%02d",
		  motd_tm->tm_mday,
		  motd_tm->tm_mon + 1,
		  1900 + motd_tm->tm_year,
		  motd_tm->tm_hour,
		  motd_tm->tm_min);
}
#endif

/*
 * read_motd() - From CoMSTuD, added Aug 29, 1996
 * modified by -Dianora
 */
int read_message_file(char *filename,aMessageFile **ptr)
{
  register aMessageFile *mptr;
  register aMessageFile	*temp, *last;
  char		buffer[MESSAGELINELEN], *tmp;
  int		fd;
  
  mptr = *ptr;

  /*
   * Clear out the old MOTD
   */
  while (mptr)
    {
      temp = mptr->next;
      MyFree(mptr);
      mptr = temp;
    }
  *ptr = ((aMessageFile *)NULL);
  fd = open(filename, O_RDONLY);
  if (fd == -1)
    return(-1);
  last = (aMessageFile *)NULL;

  while (dgets(fd, buffer, MESSAGELINELEN-1) > 0)
    {
      if ((tmp = (char *)strchr(buffer, '\n')))
	*tmp = '\0';
      if ((tmp = (char *)strchr(buffer, '\r')))
	*tmp = '\0';
      temp = (aMessageFile*) MyMalloc(sizeof(aMessageFile));

      strncpyzt(temp->line, buffer,MESSAGELINELEN);
      temp->next = (aMessageFile *)NULL;
      if (!*ptr)
	*ptr = temp;
      else
	last->next = temp;
      last = temp;
    }
  close(fd);
  return(0);
}

/*
 * read_help() - modified from from CoMSTuD's read_motd
 * added Aug 29, 1996 modifed  Aug 31 1997 - Dianora
 *
 * Use the same idea for the oper helpfile
 */
void	read_help()
{
  (void)read_message_file(HELPFILE,&helpfile);
}

/*
** m_close - added by Darren Reed Jul 13 1992.
*/
int	m_close(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  Reg	aClient	*acptr;
  Reg	int	i;
  int	closed = 0;

  if (!MyOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  for (i = highest_fd; i; i--)
    {
      if (!(acptr = local[i]))
	continue;
      if (!IsUnknown(acptr) && !IsConnecting(acptr) &&
	  !IsHandshake(acptr))
	continue;
      sendto_one(sptr, rpl_str(RPL_CLOSING), me.name, parv[0],
		 get_client_name(acptr, TRUE), acptr->status);
      (void)exit_client(acptr, acptr, acptr, "Oper Closing");
      closed++;
    }
  sendto_one(sptr, rpl_str(RPL_CLOSEEND), me.name, parv[0], closed);
  return 0;
}

#if defined(OPER_DIE) || defined(LOCOP_DIE)
int	m_die(aClient *cptr,
	      aClient *sptr,
	      int parc,
	      char *parv[])
{
  Reg	aClient	*acptr;
  Reg	int	i;

#ifndef	LOCOP_DIE
  if (!MyClient(sptr) || !IsOper(sptr))
#else
# ifdef	OPER_DIE
  if (!MyClient(sptr) || !IsAnOper(sptr))
# else
  if (!MyClient(sptr) || !IsLocOp(sptr))
# endif
#endif
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if(parc < 2)
    {
      sendto_one(sptr,":%s NOTICE %s :Need server name /die %s",
		 me.name,sptr->name,me.name);
      return 0;
    }
  else
    {
      if(strcasecmp(parv[1],me.name))
	{
	  sendto_one(sptr,":%s NOTICE %s :Mismatch on /die %s",
		     me.name,sptr->name,me.name);
	  return 0;
	}
    }

  for (i = 0; i <= highest_fd; i++)
    {
      if (!(acptr = local[i]))
	continue;
      if (IsClient(acptr))
	{
	  if(IsAnOper(acptr))
	    sendto_one(acptr,
		       ":%s NOTICE %s :Server Terminating. %s",
		       me.name, acptr->name,
		       get_client_name(sptr, TRUE));
	  else
	    sendto_one(acptr,
		       ":%s NOTICE %s :Server Terminating. %s",
		       me.name, acptr->name,
		       get_client_name(sptr, HIDEME));
	}
      else if (IsServer(acptr))
	sendto_one(acptr, ":%s ERROR :Terminated by %s",
		   me.name, get_client_name(sptr, HIDEME));
    }
  (void)s_die();
  return 0;
}
#endif

/* Shadowfax's LOCKFILE code */
#ifdef LOCKFILE

int lock_kline_file()
{
  int fd;

  /* Create Lockfile */
  if((fd = open(LOCKFILE, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0)
    {
      sendto_realops("%s is locked, klines pending",klinefile);
      pending_kline_time = time(NULL);
      return(-1);
    }
  (void)close(fd);
  return(1);
}

void do_pending_klines()
{
  int fd;
  char s[20];
  struct pkl *k, *ok;

  if(!pending_klines)
    return;
                        
  /* Create Lockfile */
  if((fd = open(LOCKFILE, O_WRONLY|O_CREAT|O_EXCL, 0644)) < 0)
    {
      sendto_realops("%s is locked, klines pending",klinefile);
      pending_kline_time = time(NULL);
      return;
    }
  (void)ircsprintf(s, "%d\n", getpid());
  (void)write(fd, s, strlen(s));
  close(fd);
  
  /* Open klinefile */   
  if ((fd = open(klinefile, O_WRONLY|O_APPEND|O_CREAT,0644))==-1)
    {
      sendto_realops("Pending klines cannot be written, cannot open %s",
                 klinefile);
      unlink(LOCKFILE);
      return; 
    }

  /* Add the Pending Klines */
  k = pending_klines;
  while(k)
    {
      write(fd, k->comment, strlen(k->comment));
      write(fd, k->kline, strlen(k->kline));
      free(k->comment);
      free(k->kline);
      ok = k;
      k = k->next;
      free(ok);
    }
  pending_klines = NULL;
  pending_kline_time = 0;
        
  close(fd);

  /* Delete the Lockfile */
  unlink(LOCKFILE);
}
#endif             

static int safe_write(aClient *sptr,
		      char *parv0,char *filename, int out,char *buffer)
{
  if (write(out,buffer,strlen(buffer)) <= 0)
    {
      sendto_realops("*** Problem writing to %s",filename);
      (void)close(out);
      return (-1);
    }
  return(0);
}


/*
 * set_autoconn
 *
 * inputs	- aClient pointer to oper requesting change
 *		-
 * output	- none
 * side effects	-
 */

static void set_autoconn(aClient *sptr,char *parv0,char *hostname,int newval)
{
  aConfItem *aconf;

  if((aconf= find_conf_name(hostname,CONF_CONNECT_SERVER)))
    {
      if(newval)
	aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
      else
	aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

      sendto_ops(
		 "%s has changed AUTOCONN for %s to %i",
		 parv0, hostname, newval);
      sendto_one(sptr,
		 ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
		 me.name, parv0, hostname, newval);
    }
  else
    {
      sendto_one(sptr,
		 ":%s NOTICE %s :Can't find %s",
		 me.name, parv0,hostname);
    }
}


/*
 * bad_tld
 *
 * input	- hostname to k-line
 * output	- YES if not valid
 * side effects	- NONE
 * checks to see if its a kline of the form blah@*.com,*.net,*.org,*.ca etc.
 * if so, return YES
 */

static int bad_tld(char *host)
{
  char *tld;
  int is_bad_tld;

  is_bad_tld = NO;

  tld = strrchr(host, '.');
  if(tld)
    {
      if(tld == host)
	is_bad_tld = YES;
      tld--;
      if(tld == host)
	if( (*tld == '.') || (*tld == '*'))
	  is_bad_tld = YES;
      tld--;
      if(tld != host)
	{
	  if((*tld == '*') || (*tld == '?'))
	    is_bad_tld = YES;
	}
    }
  else
    is_bad_tld = YES;

  return(is_bad_tld);
}
