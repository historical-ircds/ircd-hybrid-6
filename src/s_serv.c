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
 *
 *   $Id$
 */

#define CAPTAB
#include "struct.h"
#undef CAPTAB

#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include "h.h"
#include "s_bsd.h"
#include "dline_conf.h"
#include "mtrie_conf.h"
#include "fdlist.h"
#include "fileio.h"
#include "res.h"
#include "s_conf.h"
#include "class.h"
#include "send.h"

#ifndef __EMX__
#include <utmp.h> /* old slackware utmp.h defines BYTE_ORDER */
#endif /* __EMX__ */
#if defined(AIX) || defined(DYNIXPTX) || defined(SVR3)
#include <time.h>
#endif

#if defined( HAVE_STRING_H )
#include <string.h>
#else
/* older unices don't have strchr/strrchr .. help them out */
#include <strings.h>
#undef strchr
#define strchr index
#endif


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

#ifdef NEED_SPLITCODE
extern int server_was_split;		/* defined in channel.c */
extern time_t server_split_time;	/* defined in channel.c */

#ifdef SPLIT_PONG
extern int got_server_pong;		/* defined in channel.c */
#endif /* SPLIT_PONG */

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
extern void remove_empty_channels();	/* defined in channel.c */
#endif /* PRESERVE_CHANNEL_ON_SPLIT NO_JOIN_ON_SPLIT */

#endif /* NEED_SPLITCODE */

extern int cold_start;		/* defined in ircd.c */
extern fdlist serv_fdlist;
extern int rehashed;		/* defined in ircd.c */
extern int dline_in_progress;	/* defined in ircd.c */

int     max_connection_count = 1, max_client_count = 1;

extern SetOptionsType GlobalSetOptions; /* defined in ircd.c */

extern aConfItem *temporary_klines;	/* defined in s_conf.c */
extern aConfItem *find_special_conf(char *,int); /* defined in s_conf.c */

/* external functions */
#ifdef MAXBUFFERS
extern	void	reset_sock_opts();
#endif
extern char *smalldate(time_t);		/* defined in s_misc.c */
extern char *show_capabilities(aClient *); /* defined in s_misc.c */

#if defined(GLINES) || defined(SEPARATE_QUOTE_KLINES_BY_DATE)
extern char *small_file_date(time_t);	/* defined in s_misc.c */
#endif

extern char *oper_privs(aClient *,int);	/* defined in s_conf.c */
extern char *oper_flags(int);		/* defined in s_conf.c */
extern void outofmemory(void);		/* defined in list.c */
extern void s_die(void);		/* defined in ircd.c as VOIDSIG */
extern void show_opers(aClient *,char *);   /* defined in s_misc.c */
extern void show_servers(aClient *,char *); /* defined in s_misc.c */
extern void count_memory(aClient *,char *); /* defined in s_debug.c */

/* Local function prototypes */
static void set_autoconn(aClient *,char *,char *,int);
static void report_specials(aClient *,int,int);
extern void report_qlines(aClient *);
static int m_set_parser(char *);
int bad_tld(char *);

int send_motd(aClient *,aClient *,int, char **,aMessageFile *); 
int safe_write(aClient *,char *,char *,int,char *);

void read_motd(void);
#ifdef AMOTD
void read_amotd(void);
char amotd_last_changed_date[MAX_DATE_STRING];	/* enough room for date */
#endif
void read_help();
int read_message_file(char *,aMessageFile **);

char motd_last_changed_date[MAX_DATE_STRING];	/* enough room for date */

#ifdef OPER_MOTD
char oper_motd_last_changed_date[MAX_DATE_STRING]; /* enough room for date */
void read_oper_motd();
extern aMessageFile *opermotd;
#endif


#ifdef PACE_WALLOPS
time_t last_used_wallops = 0L;
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

  if(IsAnOper(sptr))
     {
       if (hunt_server(cptr,sptr,":%s VERSION :%s",1,parc,parv)==HUNTED_ISME)
	 sendto_one(sptr, form_str(RPL_VERSION), me.name,
		    parv[0], version, serno, debugmode, me.name, serveropts);
     }
   else
     sendto_one(sptr, rpl_str(RPL_VERSION), me.name,
		parv[0], version, serno, debugmode, me.name, serveropts);

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
  aConfItem *aconf;
  char	*server;
  aClient	*acptr;
  char	*comment = (parc > 2 && parv[2]) ? parv[2] : cptr->name;

  if (!IsPrivileged(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
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
	  if (!irccmp(server, my_name_for_link(me.name, aconf)))
	    server = cptr->name;
	  break; /* WARNING is normal here */
          /* NOTREACHED */
	}
      /*
      ** The following allows wild cards in SQUIT. Only useful
      ** when the command is issued by an oper.
      */
      for (acptr = GlobalClientList; (acptr = next_client(acptr, server));
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
      **
      ** Closing the client's connection probably wouldn't do much
      ** good.. any oper out there should know that the proper way
      ** to disconnect is /QUIT :)
      **
      ** its still valid if its not a local client, its then
      ** a protocol error for sure -Dianora
      */
      if(MyClient(sptr))
        {
          sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
               me.name, parv[0], "SQUIT");
          return 0;
        }
      else
        {
          server = cptr->sockhost;
          acptr = cptr;
        }
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
      sendto_one(sptr, form_str(ERR_NOSUCHSERVER),
		 me.name, parv[0], server);
      return 0;
    }
  if (IsLocOp(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
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
  struct Capability *cap;
  char	*p;
  char *s;

  if ((!IsUnknown(cptr) && !IsHandshake(cptr)) || parc < 2)
    return 0;

  if(cptr->caps)
    return exit_client(cptr, cptr, cptr, "CAPAB received twice");
  else
    cptr->caps |= CAP_CAP;

  for (s=strtoken(&p, parv[1], " "); s; s=strtoken(&p, NULL, " "))
    {
      for (cap=captab; cap->name; cap++)
	{
	  if (!strcmp(cap->name, s))
	    {
	      cptr->caps |= cap->cap;
	      break;
	    }
	 }
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
  struct Capability *cap;
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
  int	i;
  char	info[REALLEN+1];
  /*  char *inpath; */
  char  *host;
  aClient *acptr, *bcptr;
  aConfItem *aconf;
  int	hop;
  char clean_host[(2*HOSTLEN)+1];

  info[0] = '\0';
  /*  inpath = get_client_name(cptr,FALSE); */
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
      sendto_one(cptr, form_str(ERR_UNKNOWNCOMMAND),
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

  if (MyConnect(cptr) && (AUTOCONN == 0))
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
		 /* inpath */ get_client_name(cptr,FALSE), host);
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
	  if (match(my_name_for_link(me.name, aconf), acptr->name))
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
  aChannel *chptr;
  aClient	*acptr;
  aConfItem	*aconf, *bconf;
  char	*inpath, *host, *encr;
  int	split;

  inpath = get_client_name(cptr,TRUE); /* "refresh" inpath with host */
  split = irccmp(cptr->name, cptr->sockhost);
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
  SetGotId(cptr);

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
      Debug((DEBUG_INFO, "Check Usernames [%s]vs[%s]",
	     aconf->user, cptr->username));
      if (!match(aconf->user, cptr->username))
	{
	  ircstp->is_ref++;
	  sendto_ops("Username mismatch [%s]v[%s] : %s",
		     aconf->user, cptr->username,
		     get_client_name(cptr, TRUE));
	  sendto_one(cptr, "ERROR :No Username Match");
	  return exit_client(cptr, cptr, cptr, "Bad User");
	}
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

  /* adds to fdlist */
  addto_fdlist(cptr->fd,&serv_fdlist);

#ifndef NO_PRIORITY
  /* this causes the server to be marked as "busy" */
  check_fdlists(timeofday);
#endif
 
  nextping = timeofday;
  /* ircd-hybrid-6 can do TS links, and  zipped links*/
  sendto_ops("Link with %s established: (%s) link",
	     inpath,show_capabilities(cptr));

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
	  match(my_name_for_link(me.name, aconf), cptr->name))
	continue;
      if (split)
	{
	  /*
	  sendto_one(acptr,":%s SERVER %s 2 :[%s] %s",
		   me.name, cptr->name,
		   cptr->sockhost, cptr->info);
		   */

	  /* DON'T give away the IP of the server here
	   * if its a hub especially.
	   */

	  sendto_one(acptr,":%s SERVER %s 2 :%s",
		   me.name, cptr->name,
		   cptr->info);
	}
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
	  if (match(my_name_for_link(me.name, aconf), acptr->name))
	    continue;
	  split = (MyConnect(acptr) &&
		   irccmp(acptr->name, acptr->sockhost));

	  /* DON'T give away the IP of the server here
	   * if its a hub especially.
	   */

	  if (split)
	    sendto_one(cptr, ":%s SERVER %s %d :%s",
		       acptr->serv->up, acptr->name,
		       acptr->hopcount+1,
		       acptr->info);
	    /*
	    sendto_one(cptr, ":%s SERVER %s %d :[%s] %s",
		       acptr->serv->up, acptr->name,
		       acptr->hopcount+1,
		       acptr->sockhost, acptr->info);
		       */
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
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
	/* don't send 0 user channels on rejoin (Mortiis)
	 */
	if(chptr->users != 0)
#endif
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

  /* Always send a PING after connect burst is done */
  sendto_one(cptr, "PING :%s", me.name);

#ifdef NEED_SPLITCODE
#ifdef SPLIT_PONG
  if (server_was_split)
    got_server_pong = NO;
#endif /* SPLIT_PONG */
#endif /* NEED_SPLITCODE */

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
  char *p;
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
	  /* safe enough to give this on a local connect only */
	  sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
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
  
  for (acptr = GlobalClientList, collapse(mask); acptr; acptr = acptr->next) 
    {
      if (!IsServer(acptr) && !IsMe(acptr))
	continue;
      if (!BadPtr(mask) && !match(mask, acptr->name))
	continue;
      if(IsAnOper(sptr))
	 sendto_one(sptr, form_str(RPL_LINKS),
		    me.name, parv[0], acptr->name, acptr->serv->up,
		    acptr->hopcount, (acptr->info[0] ? acptr->info :
				      "(Unknown Location)"));
      else
	{
	  if(acptr->info[0])
	    {
	      /* kludge, you didn't see this nor am I going to admit
               * that I coded this.
	       */
	      p = strchr(acptr->info,']');
	      if(p)
		p += 2;	/* skip the nasty [IP] part */
	      else
		p = acptr->info;
	    }
	  else
	    p = "(Unknown Location)";

	  sendto_one(sptr, form_str(RPL_LINKS),
		    me.name, parv[0], acptr->name, acptr->serv->up,
		    acptr->hopcount, p);
	}

    }
  
  sendto_one(sptr, form_str(RPL_ENDOFLINKS), me.name, parv[0],
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
  aConfItem *tmp;
  REPORT_STRUCT *p;
  char	*host, *pass, *user, *name;
  int   port;

  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    if (tmp->status & mask)
      {
	for (p = &report_array[0]; p->conf_type; p++)
	  if (p->conf_type == tmp->status)
	    break;
	if(p->conf_type == 0)return;

	GetPrintableaConfItem(tmp, &name, &host, &pass, &user, &port);

	if(mask & (CONF_CONNECT_SERVER|CONF_NOCONNECT_SERVER))
	  {
	    char c;

	    c = p->conf_char;
	    if(tmp->flags & CONF_FLAGS_ZIP_LINK)
	      c = 'c';

	    /* Don't allow non opers to see actual ips */
	    if(IsAnOper(sptr))
	      sendto_one(sptr, form_str(p->rpl_stats), me.name,
			 sptr->name, c,
			 host,
			 name,
			 port,
			 get_conf_class(tmp),
			 oper_flags((int)tmp->hold));
	    else
	      sendto_one(sptr, form_str(p->rpl_stats), me.name,
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
	      sendto_one(sptr, form_str(p->rpl_stats), me.name,
			 sptr->name,
			 p->conf_char,
			 user, host, name,
			 oper_privs((aClient *)NULL,port),
			 get_conf_class(tmp),
			 oper_flags((int)tmp->hold));
	    else
	      sendto_one(sptr, form_str(p->rpl_stats), me.name,
			 sptr->name, p->conf_char,
			 user, host, name,
			 "0",
			 get_conf_class(tmp), "");
	  }
	else
	  sendto_one(sptr, form_str(p->rpl_stats), me.name,
		     sptr->name, p->conf_char,
		     user, host, name, port,
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
  aConfItem *this_conf;
  aConfItem *aconf;
  char	*name, *host, *pass, *user;
  int port;

  if(flags & CONF_XLINE)
    this_conf = x_conf;
  else if(flags & CONF_ULINE)
    this_conf = u_conf;
  else return;

  for (aconf = this_conf; aconf; aconf = aconf->next)
    if (aconf->status & flags)
      {
	GetPrintableaConfItem(aconf, &name, &host, &pass, &user, &port);

	sendto_one(sptr, form_str(numeric),
		   me.name,
		   sptr->name,
		   user,
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
  int i;
  int	doall = 0, wilds = 0, valid_stats = 0;
  char	*name;
  static time_t last_used=0L;

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
        {
	  /* safe enough to give this on a local connect only */
	  if(MyClient(sptr))
	    sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
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
      if (!irccmp(name, me.name))
	doall = 2;
      else if (match(name, me.name))
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
      for (i = 0; i <= highest_fd; i++)
	{
	  if (!(acptr = local[i]))
	    continue;

          if (IsPerson(acptr) &&
              !IsAnOper(acptr) && !IsAnOper(sptr) &&
              (acptr != sptr))
            continue;
	  if (IsInvisible(acptr) && (doall || wilds) &&
	      !(MyConnect(sptr) && IsOper(sptr)) &&
	      !IsAnOper(acptr) && (acptr != sptr))
	    continue;
	  if (!doall && wilds && !match(name, acptr->name))
	    continue;
	  if (!(doall || wilds) && irccmp(name, acptr->name))
	    continue;

	  /* I've added a sanity test to the "timeofday - acptr->since"
	   * occasionally, acptr->since is larger than timeofday.
	   * The code in parse.c "randomly" increases the "since",
	   * which means acptr->since is larger then timeofday at times,
	   * this gives us very high odd number.. 
	   * So, I am going to return 0 for ->since if this happens.
	   * - Dianora
	   */
	  /* trust opers not on this server */
          /* if(IsAnOper(sptr)) */

	  /* Don't trust opers not on this server */
          if(MyClient(sptr) && IsAnOper(sptr))
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
		     IsServer(acptr) ? show_capabilities(acptr) : "-");
              }
            else
              {
                if(IsIPHidden(acptr) || IsServer(acptr))
                  sendto_one(sptr, Lformat, me.name,
                     RPL_STATSLINKINFO, parv[0],
                     get_client_name(acptr, HIDEME),
                     (int)DBufLength(&acptr->sendQ),
                     (int)acptr->sendM, (int)acptr->sendK,
                     (int)acptr->receiveM, (int)acptr->receiveK,
                     timeofday - acptr->firsttime,
                     (timeofday > acptr->since) ? (timeofday - acptr->since):0,
                     IsServer(acptr) ? show_capabilities(acptr) : "-");
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
                     IsServer(acptr) ? show_capabilities(acptr) : "-");
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
	  sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
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
/* sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]); */
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
      for (mptr = msgtab; mptr->cmd; mptr++)
	  sendto_one(sptr, form_str(RPL_STATSCOMMANDS),
		     me.name, parv[0], mptr->cmd,
		     mptr->count, mptr->bytes);
      valid_stats++;
      break;

    case 'o' : case 'O' :
      report_configured_links(sptr, CONF_OPS);
      valid_stats++;
      break;

    case 'p' : case 'P' :
      show_opers(sptr, parv[0]);
      valid_stats++;
      break;

    case 'Q' : case 'q' :
      if(!IsAnOper(sptr))
	sendto_one(sptr,":%s NOTICE %s :This server does not support Q lines",
		   me.name, parv[0]);
      else
	{
	  report_qlines(sptr);
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
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      valid_stats++;
      break;

    case 'T' : case 't' :
      if (!IsAnOper(sptr))
	{
	  sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
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
	time_t now;
	
	now = timeofday - me.since;
	sendto_one(sptr, form_str(RPL_STATSUPTIME), me.name, parv[0],
		   now/86400, (now/3600)%24, (now/60)%60, now%60);
	sendto_one(sptr, form_str(RPL_STATSCONN), me.name, parv[0],
		   max_connection_count, max_client_count);
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
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
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
  sendto_one(sptr, form_str(RPL_ENDOFSTATS), me.name, parv[0], stat);

  /* personally, I don't see why opers need to see stats requests
   * at all. They are just "noise" to an oper, and users can't do
   * any damage with stats requests now anyway. So, why show them?
   * -Dianora
   */

#ifdef STATS_NOTICE
  if (valid_stats)
    sendto_realops_lev(SPY_LEV, "STATS %c requested by %s (%s@%s) [%s]", stat,
		       sptr->name, sptr->user->username, sptr->user->host,
		       sptr->user->server);
#endif
  return 0;
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
  if (hunt_server(cptr,sptr,":%s USERS :%s",1,parc,parv) == HUNTED_ISME)
    {
      /* No one uses this any more... so lets remap it..   -Taner */
      sendto_one(sptr, form_str(RPL_LOCALUSERS), me.name, parv[0],
		 Count.local, Count.max_loc);
      sendto_one(sptr, form_str(RPL_GLOBALUSERS), me.name, parv[0],
		 Count.total, Count.max_tot);
    }
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
  char	*para;

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
  aMessageFile *helpfile_ptr;
  static time_t last_used=0L;

  if(!IsAnOper(sptr))
    {
      /* HELP is always local */
      if((last_used + PACE_WAIT) > NOW)
        {
	  /* safe enough to give this on a local connect only */
	  sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
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

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
        {
	  /* safe enough to give this on a local connect only */
	  if(MyClient(sptr))
	    sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
	  return 0;
        }
      else
        {
	  last_used = NOW;
        }
    }

  if (parc > 2)
    {
      if(hunt_server(cptr, sptr, ":%s LUSERS %s :%s", 2, parc, parv)
       != HUNTED_ISME)
        {
          return 0;
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

      for (acptr = GlobalClientList; acptr; acptr = acptr->next)
	{
/*	  if (parc>1)
	    {
	      if (!IsServer(acptr) && acptr->user)
	        {
		  if (!match(parv[1], acptr->user->server))
		  continue;
	        } 
	      else
	 	{
	          if (!match(parv[1], acptr->name))
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
    sendto_one(sptr, form_str(RPL_LUSERCLIENT), me.name, parv[0],
	       c_count, i_count, s_count);
#ifndef	SHOW_INVISIBLE_LUSERS
  else
    sendto_one(sptr,
	       ":%s %d %s :There are %d users on %d servers", me.name,
	       RPL_LUSERCLIENT, parv[0], c_count,
	       s_count);
#endif
  if (o_count)
    sendto_one(sptr, form_str(RPL_LUSEROP),
	       me.name, parv[0], o_count);
  if (u_count > 0)
    sendto_one(sptr, form_str(RPL_LUSERUNKNOWN),
	       me.name, parv[0], u_count);
  /* This should be ok */
  if (Count.chan > 0)
    sendto_one(sptr, form_str(RPL_LUSERCHANNELS),
	       me.name, parv[0], Count.chan);
  sendto_one(sptr, form_str(RPL_LUSERME),
	     me.name, parv[0], m_client, m_server);
  sendto_one(sptr, form_str(RPL_LOCALUSERS), me.name, parv[0],
	     Count.local, Count.max_loc);
  sendto_one(sptr, form_str(RPL_GLOBALUSERS), me.name, parv[0],
	     Count.total, Count.max_tot);

  sendto_one(sptr, form_str(RPL_STATSCONN), me.name, parv[0],
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
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
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
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
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

  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    if ((aconf->status == CONF_CONNECT_SERVER) &&
	match(parv[1], aconf->name))
      break;

  /* Checked first servernames, then try hostnames. */
  if (!aconf)
    {
      for (aconf = ConfigItemList; aconf; aconf = aconf->next)
	{
	  if ((aconf->status == CONF_CONNECT_SERVER) &&
	      match(parv[1], aconf->host))
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
      syslog(LOG_DEBUG, "CONNECT From %s : %s %s", parv[0], parv[1], parv[2] ? parv[2] : "");
#endif
    }

  aconf->port = port;
  switch (retval = connect_server(aconf, sptr, NULL))
    {
    case 0:
      sendto_one(sptr,
		 ":%s NOTICE %s :*** Connecting to %s[%s].%d",
		 me.name, parv[0], aconf->host, aconf->name, aconf->port);
      break;
    case -1:
      sendto_one(sptr, ":%s NOTICE %s :*** Couldn't connect to %s.%d",
		 me.name, parv[0], aconf->host,aconf->port);
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
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "WALLOPS");
      return 0;
    }

  if (!IsServer(sptr) && MyConnect(sptr) && !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  /* If its coming from a server, do the normal thing
     if its coming from an oper, send the wallops along
     and only send the wallops to our local opers (those who are +oz)
     -Dianora
  */

  if(!IsServer(sptr))	/* If source of message is not a server, i.e. oper */
    {

#ifdef PACE_WALLOPS
      if( MyClient(sptr) && ((last_used_wallops + WALLOPS_WAIT) > NOW) )
	{
	  sendto_one(sptr, ":%s NOTICE %s :Oh, one of those annoying opers who doesn't know how to use a channel",
		     me.name,parv[0]);
	  return 0;
	}
      last_used_wallops = NOW;
#endif

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
  char *message = NULL;
#ifdef SLAVE_SERVERS
  char *slave_oper;
  aClient *acptr;
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

	  parc--;
	  parv++;

	  if ((acptr = hash_find_client(slave_oper,(aClient *)NULL)))
	    {
	      if(!IsPerson(acptr))
		return 0;
	    }
	  else
	    return 0;

	  if(parv[1])
	    {
	      message = parv[1];
	      send_operwall(acptr, "SLOCOPS", message);
	    }
	  else
	    return 0;
#ifdef HUB
	  sendto_slaves(sptr,"LOCOPS",slave_oper,parc,parv);
#endif
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
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "LOCOPS");
      return 0;
    }

  if(MyConnect(sptr) && IsAnOper(sptr))
    {

#ifdef SLAVE_SERVERS
      sendto_slaves(NULL,"LOCOPS",sptr->name,parc,parv);
#endif
      send_operwall(sptr, "LOCOPS", message);
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  return(0);
}

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
	sendto_one(sptr, form_str(ERR_NOPRIVILEGES),
		   me.name, parv[0]);
      return 0;
    }
  if (BadPtr(message))
    {
      if (MyClient(sptr))
	sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
		   me.name, parv[0], "OPERWALL");
      return 0;
    }
  /*
  if (strlen(message) > TOPICLEN)
    message[TOPICLEN] = '\0';
    */

#ifdef PACE_WALLOPS
  if( MyClient(sptr) && ((last_used_wallops + WALLOPS_WAIT) > NOW) )
    {
      sendto_one(sptr, ":%s NOTICE %s :Oh, one of those annoying opers who doesn't know how to use a channel",
		 me.name,parv[0]); 
      return 0;
    }
  last_used_wallops = NOW;
#endif

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
    sendto_one(sptr, form_str(RPL_TIME), me.name,
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

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
	{
	  /* safe enough to give this on a local connect only */
	  if(MyClient(sptr))
	    sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
	  return 0;
	}
      else
	last_used = NOW;
    }

  if (hunt_server(cptr,sptr,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return 0;

  if (IsPerson(sptr))
    sendto_realops_lev(SPY_LEV, "ADMIN requested by %s (%s@%s) [%s]", sptr->name,
		       sptr->user->username, sptr->user->host, sptr->user->server);
  if ((aconf = find_admin()))
    {
      sendto_one(sptr, form_str(RPL_ADMINME),
		 me.name, parv[0], me.name);
      sendto_one(sptr, form_str(RPL_ADMINLOC1),
		 me.name, parv[0], aconf->host);
      sendto_one(sptr, form_str(RPL_ADMINLOC2),
		 me.name, parv[0], aconf->passwd);
      sendto_one(sptr, form_str(RPL_ADMINEMAIL),
		 me.name, parv[0], aconf->user);
    }
  else
    sendto_one(sptr, form_str(ERR_NOADMININFO),
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
 * m_set_parser - find the correct int. to return 
 * so we can switch() it.
 * KEY:  0 - MAX
 * 	 1 - AUTOCONN
 *	 2 - IDLETIME
 *	 3 - FLUDNUM
 *	 4 - FLUDTIME
 *	 5 - FLUDBLOCK
 *	 6 - DRONETIME
 *	 7 - DRONECOUNT
 *	 8 - SPLITDELAY
 *	 9 - SPLITNUM
 *      10 - SPLITUSERS
 *	11 - SPAMNUM
 *	12 - SPAMTIME
 * - rjp
 */

#define TOKEN_MAX 0
#define TOKEN_AUTOCONN 1
#define TOKEN_IDLETIME 2
#define TOKEN_FLUDNUM 3
#define TOKEN_FLUDTIME 4
#define TOKEN_FLUDBLOCK 5
#define TOKEN_DRONETIME 6
#define TOKEN_DRONECOUNT 7
#define TOKEN_SPLITDELAY 8
#define TOKEN_SPLITNUM 9
#define TOKEN_SPLITUSERS 10
#define TOKEN_SPAMNUM 11
#define TOKEN_SPAMTIME 12
#define TOKEN_BAD 13

static char *set_token_table[] = {
  "MAX",
  "AUTOCONN",
  "IDLETIME",
  "FLUDNUM",
  "FLUDTIME",
  "FLUDBLOCK",
  "DRONETIME",
  "DRONECOUNT",
  "SPLITDELAY",
  "SPLITNUM",
  "SPLITUSERS",
  "SPAMNUM",
  "SPAMTIME",
  NULL
};

static int m_set_parser(char *parsethis)
{
  int i;

  for( i = 0; set_token_table[i]; i++ )
    {
      if(!strcasecmp(set_token_table[i],parsethis))
	return i;
    }
  return TOKEN_BAD;
}

/*
 * m_set - set options while running
 */
int   m_set(aClient *cptr,
	    aClient *sptr,
	    int parc,
	    char *parv[])
{
  char *command;
  int cnum;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      command = parv[1];
      cnum = m_set_parser(command);
/* This strcasecmp crap is annoying.. a switch() would be better.. 
 * - rjp
 */
      switch(cnum)
	{
	case TOKEN_MAX:
	  if (parc > 2)
	    {
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
	      sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
			     parv[0], sptr->user->username, sptr->sockhost, MAXCLIENTS, Count.local);
	      return 0;
	    }
	  sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
		     me.name, parv[0], MAXCLIENTS, Count.local);
	  return 0;
	  break;

	case TOKEN_AUTOCONN:
	  if(parc > 3)
	    {
	      int newval = atoi(parv[3]);

	      if(!strcasecmp(parv[2],"ALL"))
		{
		  sendto_realops(
				 "%s has changed AUTOCONN ALL to %i",
				 parv[0], newval);
		  AUTOCONN = newval;
		}
	      else
		set_autoconn(sptr,parv[0],parv[2],newval);
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
			 me.name, parv[0], AUTOCONN);
	    }
	  return 0;
	  break;

#ifdef IDLE_CHECK
	  case TOKEN_IDLETIME:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);
		if(newval == 0)
		  {
		    sendto_realops("%s has disabled IDLE_CHECK",
				   parv[0]);
		    IDLETIME = 0;
		  }
		else
		  {
		    sendto_realops("%s has changed IDLETIME to %i",
				   parv[0], newval);
		    IDLETIME = (newval*60);
		  }
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
			   me.name, parv[0], IDLETIME/60);
	      }
	    return 0;
	    break;
#endif
#ifdef FLUD
	  case TOKEN_FLUDNUM:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);

		if(newval <= 0)
		  {
		    sendto_one(sptr, ":%s NOTICE %s :FLUDNUM must be > 0",
			       me.name, parv[0]);
		    return 0;
		  }       
		FLUDNUM = newval;
		sendto_realops("%s has changed FLUDNUM to %i",
			       parv[0], FLUDNUM);
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :FLUDNUM is currently %i",
			   me.name, parv[0], FLUDNUM);
	      }
	    return 0;
	    break;

	  case TOKEN_FLUDTIME:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);

		if(newval <= 0)
		  {
		    sendto_one(sptr, ":%s NOTICE %s :FLUDTIME must be > 0",
			       me.name, parv[0]);
		    return 0;
		  }       
		FLUDTIME = newval;
		sendto_realops("%s has changed FLUDTIME to %i",
			       parv[0], FLUDTIME);
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :FLUDTIME is currently %i",
			   me.name, parv[0], FLUDTIME);
	      }
	    return 0;       
	    break;

	  case TOKEN_FLUDBLOCK:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);

		if(newval < 0)
		  {
		    sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK must be >= 0",
			       me.name, parv[0]);
		    return 0;
		  }       
		FLUDBLOCK = newval;
		if(FLUDBLOCK == 0)
		  {
		    sendto_realops("%s has disabled flud detection/protection",
				   parv[0]);
		  }
		else
		  {
		    sendto_realops("%s has changed FLUDBLOCK to %i",
				   parv[0],FLUDBLOCK);
		  }
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK is currently %i",
			   me.name, parv[0], FLUDBLOCK);
	      }
	    return 0;       
	    break;
#endif
#ifdef ANTI_DRONE_FLOOD
	  case TOKEN_DRONETIME:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);

		if(newval < 0)
		  {
		    sendto_one(sptr, ":%s NOTICE %s :DRONETIME must be > 0",
			       me.name, parv[0]);
		    return 0;
		  }       
		DRONETIME = newval;
		if(DRONETIME == 0)
		  sendto_realops("%s has disabled the ANTI_DRONE_FLOOD code",
				 parv[0]);
		else
		  sendto_realops("%s has changed DRONETIME to %i",
				 parv[0], DRONETIME);
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :DRONETIME is currently %i",
			   me.name, parv[0], DRONETIME);
	      }
	    return 0;
	    break;

	case TOKEN_DRONECOUNT:
	  if(parc > 2)
	    {
	      int newval = atoi(parv[2]);

	      if(newval <= 0)
		{
		  sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT must be > 0",
			     me.name, parv[0]);
		  return 0;
		}       
	      DRONECOUNT = newval;
	      sendto_realops("%s has changed DRONECOUNT to %i",
			     parv[0], DRONECOUNT);
	    }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT is currently %i",
			 me.name, parv[0], DRONECOUNT);
	    }
	  return 0;
	  break;
#endif
#ifdef NEED_SPLITCODE

	    case TOKEN_SPLITDELAY:
	      if(parc > 2)
		{
		  int newval = atoi(parv[2]);
		  
		  if(newval < 0)
		    {
		      sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY must be > 0",
				 me.name, parv[0]);
		      return 0;
		    }
		  /* sygma found it, the hard way */
		  if(newval > MAX_SERVER_SPLIT_RECOVERY_TIME)
		    {
		      sendto_one(sptr,
				 ":%s NOTICE %s :Cannot set SPLITDELAY over %d",
				 me.name, parv[0], MAX_SERVER_SPLIT_RECOVERY_TIME);
		      newval = MAX_SERVER_SPLIT_RECOVERY_TIME;
		    }
		  sendto_realops("%s has changed SPLITDELAY to %i",
				 parv[0], newval);
		  SPLITDELAY = (newval*60);
		  if(SPLITDELAY == 0)
		    {
		      cold_start = NO;
		      if (server_was_split)
			{
			  server_was_split = NO;
			  sendto_ops("split-mode deactived by manual override");
			}
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
		      remove_empty_channels();
#endif
#if defined(SPLIT_PONG)
		      got_server_pong = YES;
#endif
		    }
		}
	      else
		{
		  sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY is currently %i",
			     me.name,
			     parv[0],
			     SPLITDELAY/60);
		}
	  return 0;
	  break;

	case TOKEN_SPLITNUM:
	  if(parc > 2)
	    {
              int newval = atoi(parv[2]);

              if(newval < SPLIT_SMALLNET_SIZE)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITNUM must be >= %d",
                             me.name, parv[0],SPLIT_SMALLNET_SIZE);
                  return 0;
                }
              sendto_realops("%s has changed SPLITNUM to %i",
			     parv[0], newval);
	      SPLITNUM = newval;
            }
	  else
	    {
	      sendto_one(sptr, ":%s NOTICE %s :SPLITNUM is currently %i",
			 me.name,
			 parv[0],
			 SPLITNUM);
	    }
	  return 0;
	  break;

	  case TOKEN_SPLITUSERS:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);

		if(newval < 0)
		  {
		    sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS must be >= 0",
			       me.name, parv[0]);
		    return 0;
		  }
		sendto_realops("%s has changed SPLITUSERS to %i",
			       parv[0], newval);
		SPLITUSERS = newval;
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS is currently %i",
			   me.name,
			   parv[0],
			   SPLITUSERS);
	      }
	    return 0;
	    break;
#endif
#ifdef ANTI_SPAMBOT
	  case TOKEN_SPAMNUM:
	    if(parc > 2)
	      {
		int newval = atoi(parv[2]);

		if(newval < 0)
		  {
		    sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
			       me.name, parv[0]);
		    return 0;
		  }
		if(newval == 0)
		  {
		    sendto_realops("%s has disabled ANTI_SPAMBOT",
				   parv[0]);
		    return 0;
		  }

		if(newval < MIN_SPAM_NUM)
		  SPAMNUM = MIN_SPAM_NUM;
		else
		  SPAMNUM = newval;
		sendto_realops("%s has changed SPAMNUM to %i",
			       parv[0], SPAMNUM);
	      }
	    else
	      {
		sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
			   me.name, parv[0], SPAMNUM);
	      }

	    return 0;
	    break;

	case TOKEN_SPAMTIME:
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
		SPAMTIME = MIN_SPAM_TIME;
	      else
		SPAMTIME = newval;
              sendto_realops("%s has changed SPAMTIME to %i",
			     parv[0], SPAMTIME);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
                         me.name, parv[0], SPAMTIME);
            }
	  return 0;
	  break;
#endif
	default:
	case TOKEN_BAD:
	  break;
	}
    }
  sendto_one(sptr, ":%s NOTICE %s :Options: MAX AUTOCONN",
	     me.name, parv[0]);
#ifdef FLUD
  sendto_one(sptr, ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK",
	     me.name, parv[0]);
#endif
#ifdef ANTI_DRONE_FLOOD
  sendto_one(sptr, ":%s NOTICE %s :Options: DRONETIME, DRONECOUNT",
	     me.name, parv[0]);
#endif
#ifdef ANTI_SPAMBOT
  sendto_one(sptr, ":%s NOTICE %s :Options: SPAMNUM, SPAMTIME",
	     me.name, parv[0]);
#endif
#ifdef NEED_SPLITCODE
  sendto_one(sptr, ":%s NOTICE %s :Options: SPLITNUM SPLITUSERS SPLITDELAY",
	       me.name, parv[0]);
#endif
#ifdef IDLE_CHECK
  sendto_one(sptr, ":%s NOTICE %s :Options: IDLETIME",
	     me.name, parv[0]);
#endif
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

  extern int LRV, LCF;  /* in ircd.c */
  extern float currlife;
  
  if (!MyClient(sptr) || !IsOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr,
	":%s NOTICE %s :HTM is %s(%d), %s. Max rate = %dk/s. Current = %.1fk/s",
          me.name, parv[0], LIFESUX ? "ON" : "OFF", LIFESUX,
	  NOISYHTM ? "NOISY" : "QUIET",
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
              LIFESUX = 1;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now ON.", me.name, parv[0]);
              sendto_ops("Entering high-traffic mode: Forced by %s!%s@%s",
			 parv[0], sptr->user->username, sptr->sockhost);
	      LCF = 30;	/* 30s */
	    }
	  else if (!strcasecmp(command,"OFF"))
	    {
              LIFESUX = 0;
	      LCF = LOADCFREQ;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now OFF.", me.name, parv[0]);
              sendto_ops("Resuming standard operation: Forced by %s!%s@%s",
			 parv[0], sptr->user->username, sptr->sockhost);
            }
          else if (!strcasecmp(command,"QUIET"))
            {
	      sendto_ops("HTM is now QUIET");
              NOISYHTM = NO;
            }
          else if (!strcasecmp(command,"NOISY"))
            {
	      sendto_ops("HTM is now NOISY");
              NOISYHTM = YES;
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
** m_rehash
**
*/
int	m_rehash(aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{
  int found = NO;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( !IsOperRehash(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s: You have no H flag", me.name, parv[0]);
      return 0;
    }

  if(parc > 1)
    {
      if(irccmp(parv[1],"DNS") == 0)
	{
	  sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "DNS");
#ifdef CUSTOM_ERR
          sendto_ops("%s is rehashing DNS while whistling innocently",
#else
          sendto_ops("%s is rehashing DNS",
#endif
                 parv[0]);
	  restart_resolver();	/* re-read /etc/resolv.conf AGAIN?
				   and close/re-open res socket */
	  found = YES;
	}
      else if(irccmp(parv[1],"TKLINES") == 0)
	{
	  sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "temp klines");
	  flush_temp_klines();
#ifdef CUSTOM_ERR
	  sendto_ops("%s is clearing temp klines while whistling innocently",
#else
	  sendto_ops("%s is clearing temp klines",
#endif
		 parv[0]);
	  found = YES;
	}
#ifdef GLINES
      else if(irccmp(parv[1],"GLINES") == 0)
	{
	  sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "g-lines");
	  flush_glines();
#ifdef CUSTOM_ERR
	  sendto_ops("%s is clearing G-lines while whistling innocently",
#else
	  sendto_ops("%s is clearing G-lines",
#endif
		 parv[0]);
	  found = YES;
	}
#endif
      else if(irccmp(parv[1],"GC") == 0)
	{
	  sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "garbage collecting");
	  block_garbage_collect();
#ifdef CUSTOM_ERR
	  sendto_ops("%s is garbage collecting while whistling innocently",
#else
	  sendto_ops("%s is garbage collecting",
#endif
		 parv[0]);
	  found = YES;
	}
      else if(irccmp(parv[1],"MOTD") == 0)
        {
	  sendto_ops("%s is forcing re-reading of MOTD file",parv[0]);
          read_motd();
#ifdef AMOTD
	  read_amotd();
#endif
	  found = YES;
        }
#ifdef OPER_MOTD
      else if(irccmp(parv[1],"OMOTD") == 0)
        {
	  sendto_ops("%s is forcing re-reading of OPER MOTD file",parv[0]);
	  read_oper_motd();
	  found = YES;
        }
#endif
      else if(irccmp(parv[1],"HELP") == 0)
        {
	  sendto_ops("%s is forcing re-reading of oper help file",parv[0]);
          read_help();
	  found = YES;
        }
      else if(irccmp(parv[1],"dump") == 0)
	{
	  sendto_ops("%s is dumping conf file",parv[0]);
	  rehash_dump(sptr,parv[0]);
	  found = YES;
	}
      else if(irccmp(parv[1],"dlines") == 0)
	{
	  sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], configfile);
          /* this does a full rehash right now, so report it as such */
#ifdef CUSTOM_ERR
	  sendto_ops("%s is rehashing dlines from server config file while whistling innocently",
#else
	  sendto_ops("%s is rehashing dlines from server config file",
#endif
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
#define OUT "rehash one of :DNS TKLINES GLINES GC MOTD OMOTD DUMP"
#else
#define OUT "rehash one of :DNS TKLINES GC MOTD OMOTD DUMP"
#endif
	  sendto_one(sptr,":%s NOTICE %s : " OUT,me.name,sptr->name);
	  return(0);
	}
    }
  else
    {
      sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], configfile);
#ifdef CUSTOM_ERR
      sendto_ops("%s is rehashing server config file while whistling innocently",
#else
      sendto_ops("%s is rehashing server config file",
#endif
		 parv[0]);
#ifdef USE_SYSLOG
      syslog(LOG_NOTICE, "REHASH From %s\n", get_client_name(sptr, FALSE));
#endif
      return rehash(cptr, sptr, (parc > 1) ? ((*parv[1] == 'q')?2:0) : 0);
    }
  return 0; /* shouldn't ever get here */
}

/*
** m_restart
**
*/
int	m_restart(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{
  char buf[BUFSIZE]; 
  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( !IsOperDie(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s: You have no D flag", me.name, parv[0]);
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
  int	i;
  aClient	*acptr = NULL;
  aClass	*cltmp;
  char	*tname;
  int	doall, link_s[MAXCONNECTIONS], link_u[MAXCONNECTIONS];
  int	cnt = 0, wilds, dow;
#ifdef PACE_TRACE
  static time_t last_trace=0L;
  static time_t last_used=0L;
#endif
  static time_t now;

  now = time(NULL);  
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
	
	ac2ptr = next_client_double(GlobalClientList, tname);
	if (ac2ptr)
	  sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
		     version, debugmode, tname, ac2ptr->from->name);
	else
	  sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
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
#if PACE_TRACE
      if((last_used + PACE_WAIT) > NOW)
        {
	  /* safe enough to give this on a local connect only */
	  if(MyClient(sptr))
	    sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
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
          sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
                     parv[0], parv[1]);
          return 0;
        }
    }

  if(MyClient(sptr))
    sendto_realops_lev(SPY_LEV, "trace requested by %s (%s@%s) [%s]",
		       sptr->name, sptr->user->username, sptr->user->host,
		       sptr->user->server);


  doall = (parv[1] && (parc > 1)) ? match(tname, me.name): TRUE;
  wilds = !parv[1] || strchr(tname, '*') || strchr(tname, '?');
  dow = wilds || doall;
  
  if(!IsAnOper(sptr) || !dow) /* non-oper traces must be full nicks */
                              /* lets also do this for opers tracing nicks */
    {
      char      *name;
      char 	*ip;
      int       class;

      acptr = hash_find_client(tname,(aClient *)NULL);
      if(!acptr || !IsPerson(acptr)) 
        {
	  /* this should only be reached if the matching
	     target is this server */
	  sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
		     parv[0], tname);
          return 0;
        }
      name = get_client_name(acptr,FALSE);
      ip = inetntoa((char *)&acptr->ip);

      class = get_client_class(acptr);

      if (IsAnOper(acptr))
        {
          sendto_one(sptr, form_str(RPL_TRACEOPERATOR),
                     me.name, parv[0], class,
		     name, 
		     IsAnOper(sptr)?ip:"127.0.0.1",
		     now - acptr->lasttime,
		     (acptr->user)?(now - acptr->user->last):0);
        }
      else
        {
          sendto_one(sptr,form_str(RPL_TRACEUSER),
                     me.name, parv[0], class,
		     name, 
		     IsIPHidden(acptr)?"127.0.0.1":ip,
		     now - acptr->lasttime,
		     (acptr->user)?(now - acptr->user->last):0);
        }
      sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
		 parv[0], tname);
      return 0;
    }

  if (dow && LIFESUX && !IsOper(sptr))
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }

  memset((void *)link_s,0,sizeof(link_s));
  memset((void *)link_u,0,sizeof(link_u));

  /*
   * Count up all the servers and clients in a downlink.
   */
  if (doall)
   {
    for (acptr = GlobalClientList; acptr; acptr = acptr->next)
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
      char      *ip;
      int	class;
      
      if (!(acptr = local[i])) /* Local Connection? */
	continue;
      if (IsInvisible(acptr) && dow &&
	  !(MyConnect(sptr) && IsAnOper(sptr)) &&
	  !IsAnOper(acptr) && (acptr != sptr))
	continue;
      if (!doall && wilds && !match(tname, acptr->name))
	continue;
      if (!dow && irccmp(tname, acptr->name))
	continue;
      name = get_client_name(acptr,FALSE);
      ip = inetntoa((char *)&acptr->ip);

      class = get_client_class(acptr);
      
      switch(acptr->status)
	{
	case STAT_CONNECTING:
	  sendto_one(sptr, form_str(RPL_TRACECONNECTING), me.name,
		     parv[0], class, name);
	  cnt++;
	  break;
	case STAT_HANDSHAKE:
	  sendto_one(sptr, form_str(RPL_TRACEHANDSHAKE), me.name,
		     parv[0], class, name);
	  cnt++;
	  break;
	case STAT_ME:
	  break;
	case STAT_UNKNOWN:
/* added time -Taner */
	  sendto_one(sptr, form_str(RPL_TRACEUNKNOWN),
		     me.name, parv[0], class, name, ip,
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
			   form_str(RPL_TRACEOPERATOR),
			   me.name,
			   parv[0], class,
			   name, IsAnOper(sptr)?ip:"127.0.0.1",
			   now - acptr->lasttime,
			   (acptr->user)?(now - acptr->user->last):0);
	      else
		sendto_one(sptr,form_str(RPL_TRACEUSER),
			   me.name, parv[0], class,
			   name,
			   IsIPHidden(acptr)?"127.0.0.1":ip,
			   now - acptr->lasttime,
			   (acptr->user)?(now - acptr->user->last):0);
	      cnt++;
	    }
	  break;
	case STAT_SERVER:
	  if (acptr->serv->user)
	    sendto_one(sptr, form_str(RPL_TRACESERVER),
		       me.name, parv[0], class, link_s[i],
		       link_u[i], name, acptr->serv->by,
		       acptr->serv->user->username,
		       acptr->serv->user->host, now - acptr->lasttime);
	  else
	    sendto_one(sptr, form_str(RPL_TRACESERVER),
		       me.name, parv[0], class, link_s[i],
		       link_u[i], name, *(acptr->serv->by) ?
		       acptr->serv->by : "*", "*",
		       me.name, now - acptr->lasttime);
	  cnt++;
	  break;
	case STAT_LOG:
	  sendto_one(sptr, form_str(RPL_TRACELOG), me.name,
		     parv[0], LOGFILE, acptr->port);
	  cnt++;
	  break;
	default: /* ...we actually shouldn't come here... --msa */
	  sendto_one(sptr, form_str(RPL_TRACENEWTYPE), me.name,
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
	  sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
		     parv[0],tname);
	  return 0;
	}
      /* let the user have some idea that its at the end of the
       * trace
       */
      sendto_one(sptr, form_str(RPL_TRACESERVER),
		 me.name, parv[0], 0, link_s[me.fd],
		 link_u[me.fd], me.name, "*", "*", me.name);
      sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
		 parv[0],tname);
      return 0;
    }
  for (cltmp = ClassList; doall && cltmp; cltmp = cltmp->next)
    if (Links(cltmp) > 0)
      sendto_one(sptr, form_str(RPL_TRACECLASS), me.name,
		 parv[0], ClassType(cltmp), Links(cltmp));
  sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name, parv[0],tname);
  return 0;
}

#ifdef LTRACE
/*
** m_ltrace - LimitedTRACE... like m_trace() but doesn't return TRACEUSER, TRACEUNKNOWN, or TRACECLASS
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_ltrace(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  int	i;
  aClient	*acptr = NULL;
  char	*tname;
  int	doall, link_s[MAXCONNECTIONS], link_u[MAXCONNECTIONS];
  int	cnt = 0, wilds, dow;
  static time_t now;
  
  if (check_registered(sptr))
    return 0;

  if (parc > 2)
    if (hunt_server(cptr, sptr, ":%s LTRACE %s :%s",
		    2, parc, parv))
      return 0;
  
  if (parc > 1)
    tname = parv[1];
  else
    tname = me.name;

  switch (hunt_server(cptr, sptr, ":%s LTRACE :%s", 1, parc, parv))
    {
    case HUNTED_PASS: /* note: gets here only if parv[1] exists */
      {
	aClient	*ac2ptr;
	
	ac2ptr = next_client(GlobalClientList, tname);
	if (ac2ptr)
	  sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
		     version, debugmode, tname, ac2ptr->from->name);
        else
	  sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
		     version, debugmode, tname, "ac2ptr_is_NULL!!");
	return 0;
      }
    case HUNTED_ISME:
      break;
    default:
      return 0;
    }


  if(MyClient(sptr))
    sendto_realops_lev(SPY_LEV, "ltrace requested by %s (%s@%s) [%s]",
		       sptr->name, sptr->user->username, sptr->user->host,
		       sptr->user->server);


  doall = (parv[1] && (parc > 1)) ? match(tname, me.name): TRUE;
  wilds = !parv[1] || index(tname, '*') || index(tname, '?');
  dow = wilds || doall;
  
  for (i = 0; i < MAXCONNECTIONS; i++)
    link_s[i] = 0, link_u[i] = 0;
                        
  if (dow && LIFESUX && !IsOper(sptr))
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }

  /*
   * Count up all the servers and clients in a downlink.
   */
  if (doall) {
    for (acptr = GlobalClientList; acptr; acptr = acptr->next) {
      if (IsServer(acptr)) 
        ++link_s[acptr->from->fd];
    }
  }

  /* report all direct connections */
  now = time(NULL);
  for (i = 0; i <= highest_fd; i++)
    {
      char	*name;
      char 	*ip;
      int	class;
      
      if (!(acptr = local[i])) /* Local Connection? */
	continue;
      if (IsInvisible(acptr) && dow &&
	  !(MyConnect(sptr) && IsAnOper(sptr)) &&
	  !IsAnOper(acptr) && (acptr != sptr))
	continue;
      if (!doall && wilds && !match(tname, acptr->name))
	continue;
      if (!dow && irccmp(tname, acptr->name))
	continue;
      name = get_client_name(acptr,FALSE);
      ip = inetntoa((char *)&acptr->ip);

      class = get_client_class(acptr);
      
      switch(acptr->status)
	{
	case STAT_HANDSHAKE:
	  sendto_one(sptr, form_str(RPL_TRACEHANDSHAKE), me.name,
		     parv[0], class, name);
	  cnt++;
	  break;
	case STAT_ME:
	  break;
	case STAT_CLIENT:
	  /* Well, most servers don't have a LOT of OPERs... let's show them too */
          if ((IsAnOper(sptr) &&
              (MyClient(sptr) || !(dow && IsInvisible(acptr))))
              || !dow || IsAnOper(acptr))
	    {
	      if (IsAnOper(acptr))
		sendto_one(sptr,
			   form_str(RPL_TRACEOPERATOR),
			   me.name, parv[0], class,
			   name, 
			   IsAnOper(sptr)?ip:"127.0.0.1", 
			   now - acptr->lasttime,
			   (acptr->user)?(now - acptr->user->last):0);
	      cnt++;
	    }
	  break;
	case STAT_SERVER:
	  if (acptr->serv->user)
	    sendto_one(sptr, form_str(RPL_TRACESERVER),
		       me.name, parv[0], class, link_s[i],
		       link_u[i], name, acptr->serv->by,
		       acptr->serv->user->username,
		       acptr->serv->user->host, now - acptr->lasttime);
	  else
	    sendto_one(sptr, form_str(RPL_TRACESERVER),
		       me.name, parv[0], class, link_s[i],
		       link_u[i], name, *(acptr->serv->by) ?
		       acptr->serv->by : "*", "*",
		       me.name, now - acptr->lasttime);
	  cnt++;
	  break;
	case STAT_LOG:
	  sendto_one(sptr, form_str(RPL_TRACELOG), me.name,
		     parv[0], LOGFILE, acptr->port);
	  cnt++;
	  break;
	default: /* ...we actually shouldn't come here... --msa */
	  sendto_one(sptr, form_str(RPL_TRACENEWTYPE), me.name,
		     parv[0], name);
	  cnt++;
	  break;
	}
    }
  /*
   * Add these lines to summarize the above which can get rather long
   * and messy when done remotely - Avalon
   */
  if (!IsAnOper(sptr) || !cnt)
    {
      if (cnt)
	  return 0;
      /* let the user have some idea that its at the end of the
       * trace
       */
      sendto_one(sptr, form_str(RPL_TRACESERVER),
		 me.name, parv[0], 0, link_s[me.fd],
		 link_u[me.fd], me.name, "*", "*", me.name);
      return 0;
    }
  return 0;
}
#endif /* LTRACE */

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

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
	{
	  /* safe enough to give this on a local connect only */
	  if(MyClient(sptr))
	    sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
	  return 0;
	}
      else
	last_used = NOW;
    }

  if (hunt_server(cptr, sptr, ":%s MOTD :%s", 1,parc,parv)!=HUNTED_ISME)
    return 0;

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
  aMessageFile *temp;

  if (motd == (aMessageFile *)NULL)
    {
      sendto_one(sptr, form_str(ERR_NOMOTD), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr, form_str(RPL_MOTDSTART), me.name, parv[0], me.name);

  sendto_one(sptr,":%s NOTICE %s :%s",me.name,parv[0],motd_last_changed_date);

  temp = motd;
  while(temp)
    {
      sendto_one(sptr,
		 form_str(RPL_MOTD),
		 me.name, parv[0], temp->line);
      temp = temp->next;
    }
  sendto_one(sptr, form_str(RPL_ENDOFMOTD), me.name, parv[0]);
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
  aMessageFile *temp;
  
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
  struct tm *motd_tm;
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
  aMessageFile *mptr;
  aMessageFile	*temp, *last;
  char		buffer[MESSAGELINELEN], *tmp;
  FBFILE* file;
  
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
  if ((file = fbopen(filename, "r")) == 0)
    return(-1);
  last = (aMessageFile *)NULL;

  while (fbgets(buffer, MESSAGELINELEN, file))
    {
      if ((tmp = (char *)strchr(buffer, '\n')))
	*tmp = '\0';
      temp = (aMessageFile*) MyMalloc(sizeof(aMessageFile));

      strncpyzt(temp->line, buffer, MESSAGELINELEN);
      temp->next = (aMessageFile *)NULL;
      if (!*ptr)
	*ptr = temp;
      else
	last->next = temp;
      last = temp;
    }
  fbclose(file);
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
  aClient	*acptr;
  int	i;
  int	closed = 0;

  if (!MyOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  for (i = highest_fd; i; i--)
    {
      if (!(acptr = local[i]))
	continue;
      if (!IsUnknown(acptr) && !IsConnecting(acptr) &&
	  !IsHandshake(acptr))
	continue;
      sendto_one(sptr, form_str(RPL_CLOSING), me.name, parv[0],
		 get_client_name(acptr, TRUE), acptr->status);
      (void)exit_client(acptr, acptr, acptr, "Oper Closing");
      closed++;
    }
  sendto_one(sptr, form_str(RPL_CLOSEEND), me.name, parv[0], closed);
  return 0;
}

int	m_die(aClient *cptr,
	      aClient *sptr,
	      int parc,
	      char *parv[])
{
  aClient	*acptr;
  int	i;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( !IsOperDie(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s: You have no D flag", me.name, parv[0]);
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


/*
 * set_autoconn
 *
 * inputs	- aClient pointer to oper requesting change
 *		-
 * output	- none
 * side effects	-
 */

static void set_autoconn(aClient *sptr,char *parv0,char *name,int newval)
{
  aConfItem *aconf;

  if((aconf= find_conf_name(name,CONF_CONNECT_SERVER)))
    {
      if(newval)
	aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
      else
	aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

      sendto_ops(
		 "%s has changed AUTOCONN for %s to %i",
		 parv0, name, newval);
      sendto_one(sptr,
		 ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
		 me.name, parv0, name, newval);
    }
  else
    {
      sendto_one(sptr,
		 ":%s NOTICE %s :Can't find %s",
		 me.name, parv0, name);
    }
}

