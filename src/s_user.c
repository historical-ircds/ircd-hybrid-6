/************************************************************************
 *   IRC - Internet Relay Chat, src/s_user.c
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

/*
 * #define CANONIZE if you want canonize()
 * its no longer used in channel.c, and is commented out in whowas.c
 */

#ifndef lint
static  char sccsid[] = "@(#)s_user.c	2.68 07 Nov 1993 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";

static char *rcs_version="$Id$";

#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <sys/stat.h>
#ifndef __EMX__
#include <utmp.h>
#endif /* __EMX__ */
#include <fcntl.h>
#include "h.h"
#ifdef FLUD
#include "blalloc.h"
#endif /* FLUD */

#if defined( HAVE_STRING_H)
#include <string.h>
#else
#include <strings.h>
#endif

static int do_user (char *, aClient *, aClient*, char *, char *, char *,
                     char *);

int    botwarn (char *, char *, char *, char *);

extern char motd_last_changed_date[];
extern int send_motd(aClient *,aClient *,int, char **,aMessageFile *);

#ifdef ANTI_SPAMBOT_EXTRA
extern int spambot_privmsg_count;
#endif

#ifdef OPER_MOTD
extern aMessageFile *opermotd;	/* defined in s_serv.c */
extern int send_oper_motd(aClient *,aClient *,int, char **,aMessageFile *);
#endif

/* LINKLIST */ 
extern aClient *local_cptr_list;
extern aClient *oper_cptr_list;
extern aClient *serv_cptr_list;
 
extern void outofmemory(void);         /* defined in list.c */

#ifdef MAXBUFFERS
extern void reset_sock_opts();
#endif
extern int lifesux;

static char buf[BUFSIZE], buf2[BUFSIZE];
static int user_modes[]		= { FLAGS_OPER, 'o',
				FLAGS_LOCOP, 'O',
				FLAGS_INVISIBLE, 'i',
				FLAGS_WALLOP, 'w',
				FLAGS_SERVNOTICE, 's',
				FLAGS_OPERWALL,'z',
				FLAGS_CCONN, 'c',
				FLAGS_REJ, 'r',
				FLAGS_SKILL, 'k',
				FLAGS_FULL, 'f',
				FLAGS_SPY, 'y',
				FLAGS_DEBUG, 'd',
				FLAGS_NCHANGE, 'n',
				0, 0 };

/* internally defined functions */
int botreject(char *);
unsigned long my_rand(void);	/* provided by orabidoo */

/* externally defined functions */
extern Link *find_channel_link(Link *,aChannel *);	/* defined in list.c */
extern char *oper_privs(aClient *,int);		 /* defined in s_conf.c */
extern aConfItem *find_special_conf(char *,int); /* defined in s_conf.c */

#ifdef FLUD
int flud_num = FLUD_NUM;
int flud_time = FLUD_TIME;
int flud_block = FLUD_BLOCK;
extern BlockHeap *free_fludbots;
extern BlockHeap *free_Links;

void announce_fluder(aClient *,aClient *,aChannel *,int );
struct fludbot *remove_fluder_reference(struct fludbot **,aClient *);
Link *remove_fludee_reference(Link **,void *);
int check_for_ctcp(char *);
int check_for_fludblock(aClient *,aClient *,aChannel *,int);
int check_for_flud(aClient *,aClient *,aChannel *,int);
void free_fluders(aClient *,aChannel *);
void free_fludees(aClient *);
#endif

#ifdef ANTI_SPAMBOT
int spam_time = MIN_JOIN_LEAVE_TIME;
int spam_num = MAX_JOIN_LEAVE_COUNT;
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
** next_client
**	Local function to find the next matching client. The search
**	can be continued from the specified client entry. Normal
**	usage loop is:
**
**	for (x = client; x = next_client(x,mask); x = x->next)
**		HandleMatchingClient;
**	      
*/
aClient *next_client(aClient *next,	/* First client to check */
		     char *ch)		/* search string (may include wilds) */
{
  Reg	aClient	*tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return ((aClient *) NULL);

  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (!matches(ch,next->name)) break;
    }
  return next;
}


/* this slow version needs to be used for hostmasks *sigh * */

aClient *next_client_double(aClient *next,	/* First client to check */
			    char *ch)	/* search string (may include wilds) */
{
  Reg	aClient	*tmp = next;

  next = find_client(ch, tmp);
  if (tmp && tmp->prev == next)
    return NULL;
  if (next != tmp)
    return next;
  for ( ; next; next = next->next)
    {
      if (!matches(ch,next->name) || !matches(next->name,ch))
	break;
    }
  return next;
}

/*
** hunt_server
**
**	Do the basic thing in delivering the message (command)
**	across the relays to the specific server (server) for
**	actions.
**
**	Note:	The command is a format string and *MUST* be
**		of prefixed style (e.g. ":%s COMMAND %s ...").
**		Command can have only max 8 parameters.
**
**	server	parv[server] is the parameter identifying the
**		target server.
**
**	*WARNING*
**		parv[server] is replaced with the pointer to the
**		real servername from the matched client (I'm lazy
**		now --msa).
**
**	returns: (see #defines)
*/
int	hunt_server(aClient *cptr,
		    aClient *sptr,
		    char *command,
		    int server,
		    int parc,
		    char *parv[])
{
  aClient *acptr;
  int wilds;

  /*
  ** Assume it's me, if no server
  */
  if (parc <= server || BadPtr(parv[server]) ||
      matches(me.name, parv[server]) == 0 ||
      matches(parv[server], me.name) == 0)
    return (HUNTED_ISME);
  /*
  ** These are to pickup matches that would cause the following
  ** message to go in the wrong direction while doing quick fast
  ** non-matching lookups.
  */
  if ((acptr = find_client(parv[server], NULL)))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;
  if (!acptr && (acptr = find_server(parv[server], NULL)))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;

  (void)collapse(parv[server]);
  wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

  /*
   * Again, if there are no wild cards involved in the server
   * name, use the hash lookup
   * - Dianora
   */

  if (!acptr)
    {
      if(!wilds)
	{
	  acptr = find_name(parv[server],(aClient *)NULL);
	  if( !acptr || !IsRegistered(acptr) || !IsServer(acptr) )
	    {
	      sendto_one(sptr, err_str(ERR_NOSUCHSERVER), me.name,
			 parv[0], parv[server]);
	      return(HUNTED_NOSUCH);
	    }
	}
      else
	{
	  for (acptr = client;
	       (acptr = next_client(acptr, parv[server]));
	       acptr = acptr->next)
	    {
	      if (acptr->from == sptr->from && !MyConnect(acptr))
		continue;
	      /*
	       * Fix to prevent looping in case the parameter for
	       * some reason happens to match someone from the from
	       * link --jto
	       */
	      if (IsRegistered(acptr) && (acptr != cptr))
		break;
	    }
	}
    }

  if (acptr)
    {
      if (IsMe(acptr) || MyClient(acptr))
	return HUNTED_ISME;
      if (matches(acptr->name, parv[server]))
	parv[server] = acptr->name;
      sendto_one(acptr, command, parv[0],
		 parv[1], parv[2], parv[3], parv[4],
		 parv[5], parv[6], parv[7], parv[8]);
      return(HUNTED_PASS);
    } 
  sendto_one(sptr, err_str(ERR_NOSUCHSERVER), me.name,
	     parv[0], parv[server]);
  return(HUNTED_NOSUCH);
}

/*
** 'do_nick_name' ensures that the given parameter (nick) is
** really a proper string for a nickname (note, the 'nick'
** may be modified in the process...)
**
**	RETURNS the length of the final NICKNAME (0, if
**	nickname is illegal)
**
**  Nickname characters are in range
**	'A'..'}', '_', '-', '0'..'9'
**  anything outside the above set will terminate nickname.
**  In addition, the first character cannot be '-'
**  or a Digit.
**
**  Note:
**	'~'-character should be allowed, but
**	a change should be global, some confusion would
**	result if only few servers allowed it...
*/

static	int do_nick_name(char *nick)
{
  Reg char *ch;

  if (*nick == '-' || isdigit(*nick)) /* first character in [0..9-] */
    return 0;
  
  for (ch = nick; *ch && (ch - nick) < NICKLEN; ch++)
    if (!isvalid(*ch) || isspace(*ch))
      break;

  *ch = '\0';

  return (ch - nick);
}


#ifdef CANONIZE
/*
** canonize
**
** reduce a string of duplicate list entries to contain only the unique
** items.  Unavoidably O(n^2).
*/
char	*canonize(char *buffer)
{
  static	char	cbuf[BUFSIZ];
  register char	*s, *t, *cp = cbuf;
  register int	l = 0;
  char	*p = NULL, *p2;

  *cp = '\0';

  for (s = strtoken(&p, buffer, ","); s; s = strtoken(&p, NULL, ","))
    {
      if (l)
	{
	  for (p2 = NULL, t = strtoken(&p2, cbuf, ","); t;
	       t = strtoken(&p2, NULL, ","))
	    if (!mycmp(s, t))
	      break;
	    else if (p2)
	      p2[-1] = ',';
	}
      else
	t = NULL;

      if (!t)
	{
	  if (l)
	    *(cp-1) = ',';
	  else
	    l = 1;
	  (void)strcpy(cp, s);
	  if (p)
	    cp += (p - s);
	}
      else if (p2)
	p2[-1] = ',';
    }
  return cbuf;
}
#endif

/*
** register_user
**	This function is called when both NICK and USER messages
**	have been accepted for the client, in whatever order. Only
**	after this, is the USER message propagated.
**
**	NICK's must be propagated at once when received, although
**	it would be better to delay them too until full info is
**	available. Doing it is not so simple though, would have
**	to implement the following:
**
**	(actually it has been implemented already for a while) -orabidoo
**
**	1) user telnets in and gives only "NICK foobar" and waits
**	2) another user far away logs in normally with the nick
**	   "foobar" (quite legal, as this server didn't propagate
**	   it).
**	3) now this server gets nick "foobar" from outside, but
**	   has already the same defined locally. Current server
**	   would just issue "KILL foobar" to clean out dups. But,
**	   this is not fair. It should actually request another
**	   nick from local user or kill him/her...
*/

static	int	register_user(aClient *cptr,
			      aClient *sptr,
			      char *nick,
			      char *username)
{
  Reg	aConfItem *aconf;
  char	*parv[3];
  static  char ubuf[12];
  char	*p;
  short	oldstatus = sptr->status;
  anUser *user = sptr->user;
  int 	i, dots;
  int   bad_dns;	/* flag a bad dns name */
  char *reason;
#ifdef BOTCHECK
  int	isbot;
  char	bottemp[HOSTLEN + 1];
#endif
#ifdef EXTRA_BOT_NOTICES
  char	botgecos[127];
#endif
#ifdef ANTI_SPAMBOT
  char  spamchar = '\0';
#endif
  char tmpstr2[512];

  user->last = timeofday;
  parv[0] = sptr->name;
  parv[1] = parv[2] = NULL;

  /* pointed out by Mortiis, never be too careful */
  if(strlen(username) > USERLEN)
    username[USERLEN] = '\0';

  reason = (char *)NULL;

  if (MyConnect(sptr))
    {
      p = inetntoa((char *)&sptr->ip);
      strncpyzt(sptr->hostip,p,HOSTIPLEN+1);
      
      if ( (i = check_client(sptr,username,&reason)) )
	/*
	 * -2 is a socket error, already reported.
	 */
       {
	if (i != -2)
	  {
	    if (i == -4)
	      {
		ircstp->is_ref++;
		return exit_client(cptr, sptr, &me,
				   "Too many connections from your hostname");
	      }
	    else if (i == -3)
	      sendto_realops_lev(FULL_LEV, "%s for %s.",
				 "I-line is full", get_client_host(sptr));
            else if (i == -5)
	      {
		if(sptr->user)
		  {
		    if (sptr->flags & FLAGS_DOID &&
			!(sptr->flags & FLAGS_GOTID))
		      {
			*user->username = '~';
			(void)strncpy(&user->username[1], username, USERLEN);
			user->username[USERLEN] = '\0';
		      }
		    else
		      (void)strncpy(user->username, username, USERLEN);
		  }
		  
		/* Ok... if we are using REJECT_HOLD, I'm not going to dump
		 * the client immediately, but just mark the client for exit
		 * at some future time, .. this marking also disables reads/
		 * writes from the client. i.e. the client is "hanging" onto
		 * an fd without actually being able to do anything with it
		 * I still send the usual messages about the k line, but its
		 * not exited immediately.
		 * My concern would be, someone attempting to deny service
		 * attack would load up a bunch of clients all on the same
		 * user@host expecting to use up fd's
		 * - Dianora
		 */

		reason = (reason) ? reason : "K-lined";
#ifdef REJECT_HOLD
		SetRejectHold(cptr);
#ifdef KLINE_WITH_REASON
		sendto_one(sptr, ":%s NOTICE %s :*** K-lined for %s",
			   me.name,cptr->name,reason);
#else
		sendto_one(sptr, ":%s NOTICE %s :*** K-lined",
			   me.name,cptr->name);
#endif
		return(0);
#else
		ircstp->is_ref++;

#ifdef KLINE_WITH_REASON
		return exit_client(cptr, sptr, &me, reason);
#else
		return exit_client(cptr, sptr, &me, "K-lined");
#endif
#endif
#ifdef RK_NOTICES
		if(sptr->user)
		  sendto_realops("K-lined %s@%s. for %s",sptr->user->username,
			       sptr->sockhost,reason);
#endif
	      }
	    else
              sendto_realops_lev(CCONN_LEV, "%s from %s [%s].",
				 "Unauthorized client connection",
				 get_client_host(sptr),
				 sptr->hostip);
#ifdef USE_SYSLOG
	    syslog(LOG_INFO,"%s from %s.",i == -3 ? "Too many connections" :
		   "Unauthorized client connection", get_client_host(sptr));
#endif
	    ircstp->is_ref++;
	    return exit_client(cptr, sptr, &me, i == -3 ?
			       "No more connections allowed in your connection class" :
			       "You are not authorized to use this server");
	    
	  }
	else
	  {
	    return exit_client(cptr, sptr, &me, "Socket Error");
	  }
       }

#ifdef ANTI_SPAMBOT
      /* This appears to be broken */
      /* Check for single char in user->host -ThemBones */
      if (*(user->host + 1) == '\0')
         spamchar = *user->host;
#endif

#ifdef BOTCHECK
      /*
       * We need to save this now, before we clobber it
       */
      strncpyzt(bottemp, user->host, HOSTLEN);
#endif
      strncpyzt(user->host, sptr->sockhost, HOSTLEN);

      dots = 0;
      p = user->host;
      bad_dns = NO;
      while(*p)
	{
	  if (!isalnum(*p))
	    {
#ifdef RFC1035_ANAL
	      if ((*p != '-') && (*p != '.'))
#else
		if ((*p != '-') && (*p != '.') && (*p != '_') && (*p != '/'))
#endif /* RFC1035_ANAL */
		    bad_dns = YES;
	    } 
	  if( *p == '.' )
	    dots++;
	  p++;
	}
      
      /*
       * Check that the hostname has AT LEAST ONE dot (.)
       * in it. If not, drop the client (spoofed host)
       * -ThemBones
       */
      if (!dots)
	{
	  sendto_realops(
			 "Invalid hostname for %s, dumping user %s",
			 sptr->hostip, sptr->name);
	  return exit_client(cptr, sptr, &me, "Invalid hostname");
	}

      if(bad_dns)
	{
	  sendto_one(sptr, ":%s NOTICE %s :*** Notice -- You have a bad character in your hostname",
		     me.name,cptr->name);
	  strcpy(user->host,sptr->hostip);
	  strcpy(sptr->sockhost,sptr->hostip);
	}

      aconf = sptr->confs->value.aconf;
      if ((sptr->flags & FLAGS_DOID) && !(sptr->flags & FLAGS_GOTID))
	{
	  /* because username may point to user->username */
	  char	temp[USERLEN+1];
	  
	  strncpyzt(temp, username, USERLEN+1);
	  *user->username = '~';
	  (void)strncpy(&user->username[1], temp, USERLEN);
	  user->username[USERLEN] = '\0';

#ifdef IDENTD_COMPLAIN
/* tell them to install identd -Taner */
	  sendto_one(sptr, ":%s NOTICE %s :*** Notice -- It seems that you don't have identd installed on your host.",
		     me.name,cptr->name);
/* end identd hack */
#endif
	  if(aconf && IsNeedIdentd(aconf))
	    {
	      ircstp->is_ref++;
	      sendto_one(sptr, ":%s NOTICE %s :*** Notice -- You need to install identd to use this server",
			 me.name, cptr->name);
	      return exit_client(cptr, sptr, &me, "Install identd");
	    }
	  if(aconf && IsNoTilde(aconf))
	    {
	      strncpyzt(user->username, username, USERLEN+1);
	    }
	}
#ifndef FOLLOW_IDENT_RFC
      else if (sptr->flags & FLAGS_GOTID && *sptr->username != '-')
	strncpyzt(user->username, sptr->username, USERLEN+1);
#endif
      else
	strncpyzt(user->username, username, USERLEN+1);

      if (!BadPtr(aconf->passwd) &&
	!StrEq(sptr->passwd, aconf->passwd))
	{
	  ircstp->is_ref++;
	  sendto_one(sptr, err_str(ERR_PASSWDMISMATCH),
		     me.name, parv[0]);
	  return exit_client(cptr, sptr, &me, "Bad Password");
	}
      memset((void *)sptr->passwd,0, sizeof(sptr->passwd));

      /* If this user is being spoofed, tell them so */
      if(IsConfDoSpoofIp(aconf))
	{
	  sendto_one(sptr,
	      ":%s NOTICE %s :*** Spoofing your IP. congrats.",
		     me.name,parv[0]);
	}

      /* If this user is in the exception class, Set it "E lined" */
      if(IsConfElined(aconf))
	{
	  SetElined(sptr);
	  sendto_one(sptr,
	      ":%s NOTICE %s :*** You are exempt from K/D/G lines. congrats.",
		     me.name,parv[0]);
	}

      /* If this user can run bots set it "B lined" */
      if(IsConfBlined(aconf))
	{
	  SetBlined(sptr);
	  sendto_one(sptr,
	      ":%s NOTICE %s :*** You can run bots here. congrats.",
		     me.name,parv[0]);
	}

      /* If this user is exempt from user limits set it F lined" */
      if(IsConfFlined(aconf))
	{
	  SetFlined(sptr);
	  sendto_one(sptr,
	      ":%s NOTICE %s :*** You are exempt from user limits. congrats.",
		     me.name,parv[0]);
	}
#ifdef IDLE_CHECK
      /* If this user is exempt from idle time outs */
      if(IsConfIdlelined(aconf))
	{
	  SetIdlelined(sptr);
	  sendto_one(sptr,
	      ":%s NOTICE %s :*** You are exempt from idle limits. congrats.",
		     me.name,parv[0]);
	}
#endif

#ifdef GLINES
      if ( (aconf=find_gkill(sptr)) )
	{
	  char *reason;
	  reason = aconf->passwd ? aconf->passwd : "G-lined";

#ifdef RK_NOTICES
	  sendto_realops("G-lined %s@%s. for %s",sptr->user->username,
			 sptr->sockhost,reason);
#endif

	  /* Ok...read note about REJECT_HOLD above -Dianora
	   */

#ifdef REJECT_HOLD
	  SetRejectHold(cptr);
#ifdef KLINE_WITH_REASON
	  sendto_one(sptr, ":%s NOTICE %s :*** G-lined for %s",
		     me.name,cptr->name,reason);
#else
	  sendto_one(sptr, ":%s NOTICE %s :*** G-lined",
		     me.name,cptr->name);
#endif
	  return(0);
#else
	  ircstp->is_ref++;

#ifdef KLINE_WITH_REASON
	  return exit_client(cptr, sptr, &me, reason);
#else
	  return exit_client(cptr, sptr, &me, "G-lined");
#endif
#endif
	}
#endif	/* GLINES */

#ifdef R_LINES
      if (find_restrict(sptr))
	{
#ifdef RK_NOTICES
	  sendto_realops("R-lined %s@%s.",sptr->user->username,
			 sptr->sockhost);
#endif
	  ircstp->is_ref++;
	  return exit_client(cptr, sptr, &me , "R-lined");
	}
#endif

#ifdef BOTCHECK
      if (IsBlined(cptr))
	isbot = botwarn(bottemp, sptr->name, 
			sptr->user->username, sptr->sockhost);
      else
	isbot = botreject(bottemp);
#endif

      /* Limit clients */
      /*
       * We want to be able to have servers and F-line clients
       * connect, so save room for "buffer" connections.
       * Smaller servers may want to decrease this, and it should
       * probably be just a percentage of the MAXCLIENTS...
       *   -Taner
       */
      /* Except "F:" clients */
      if ( (
#ifdef BOTCHECK
	  !isbot &&
#endif /* BOTCHECK */
          ((Count.local + 1) >= (MAXCLIENTS+MAX_BUFFER))) ||
            (((Count.local +1) >= (MAXCLIENTS - 5)) && !(IsFlined(sptr))))
/*
	  (sptr->fd >= (MAXCLIENTS+MAX_BUFFER))) ||
             ((sptr->fd >= (MAXCLIENTS - 5)) && !(IsFlined(sptr))))
*/

    {
      sendto_realops_lev(FULL_LEV, "Too many clients, rejecting %s[%s].",
			 nick, sptr->sockhost);
      ircstp->is_ref++;
      return exit_client(cptr, sptr, &me,
			 "Sorry, server is full - try later");
    }


      /* USER * xdfdx xdfjx :* */
      /* anti clone.c catcher, it will have to be updated as they catch on
      * *sigh* -Dianora
      */

  if(!strcmp(user->host,"xdfdx"))
    {
      sendto_realops_lev(REJ_LEV,"Rejecting clone.c Bot: %s",
			 get_client_name(sptr,FALSE));
      ircstp->is_ref++;
      return exit_client(cptr, sptr, sptr, "clone.c detected, rejected.");
    }

#ifdef ANTI_SPAMBOT
      /* It appears, this is catching normal clients */
  /* Reject single char user-given user->host's */
  if (spamchar == 'x')
  {
    sendto_realops_lev(REJ_LEV,"Rejecting possible Spambot: %s (Single char user-given userhost: %c)",
		       get_client_name(sptr,FALSE), spamchar);
    ircstp->is_ref++;
    return exit_client(cptr, sptr, sptr, "Spambot detected, rejected.");
  }
#endif

#ifdef BOTCHECK
  /* Check bot type... */
  switch (isbot) {
  case 0:       break;  /* it's ok */
  case 1:       /* eggdrop */
    sendto_realops_lev(REJ_LEV,"Rejecting eggdrop: %s",
		       get_client_name(sptr,FALSE));
    ircstp->is_ref++;
    return exit_client(cptr, sptr, sptr, "Eggdrop bot detected, rejected.  If you are using a VMS client, please ftp the latest version from ubvmsa.cc.buffalo.edu:[.maslib.utilities.irc] to avoid this problem.");
    break;
  case 2:       /* vlad/com bot */
    sendto_realops_lev(REJ_LEV,"Rejecting vlad/com/joh bot: %s",
		       get_client_name(sptr,FALSE));
    ircstp->is_ref++;
    return exit_client(cptr, sptr, sptr, "Vlad/Com/joh bot detected, rejected.");
    break;
  case 3:	/* SpamBot */
    sendto_realops_lev(REJ_LEV,"Rejecting Spambot: %s",
		       get_client_name(sptr,FALSE));
    ircstp->is_ref++;
    return exit_client(cptr, sptr, sptr, "Spambot detected, rejected.");
    break;
  case 4:       /* annoy/ojnkbot */
    sendto_realops_lev(REJ_LEV,"Rejecting annoy/ojnkbot: %s",
		       get_client_name(sptr,FALSE));
    ircstp->is_ref++;
    return exit_client(cptr, sptr, sptr, "Annoy/OJNKbot detected, rejected.");
    break;
  default:      /* huh !? */
    sendto_realops_lev(REJ_LEV,"Rejecting bot: %s",
		       get_client_name(sptr,FALSE));
    ircstp->is_ref++;
    return exit_client(cptr, sptr, sptr, "Bot detected, rejected.");
    break;
  }
#endif
/* End of botcheck */

  if (oldstatus == STAT_MASTER && MyConnect(sptr))
    (void)m_oper(&me, sptr, 1, parv);
#if defined(NO_MIXED_CASE) || defined(NO_SPECIAL)
  {
    register char *tmpstr;
    u_char c, cc;
    register int lower, upper, special;
		
    lower = upper = special = cc = 0;

/* check for "@" in identd reply -Taner */
    if ((strchr(user->username,'@') != NULL) || (strchr(username,'@') != NULL))
      {
	sendto_realops_lev(REJ_LEV,
			   "Illegal \"@\" in username: %s (%s)",
			   get_client_name(sptr,FALSE),username);
	ircstp->is_ref++;
	(void)ircsprintf(tmpstr2,"Invalid username [%s] - '@' is not allowed!",
		   username);
	return exit_client(cptr, sptr, sptr , tmpstr2);
      }

    /*
     * First check user->username...
     */
#ifdef IGNORE_FIRST_CHAR
    tmpstr = (user->username[0] == '~' ? &user->username[2] :
	      &user->username[1]);
    /*
     * Ok, we don't want to TOTALLY ignore the first character.
     * We should at least check it for control characters, etc
     * - ThemBones
     */
    cc = (user->username[0] == '~' ? user->username[1] :
	  user->username[0]);
    if ((!isalnum(cc) && !strchr(" -_.",cc)) || (cc > 127))
	  special++;
#else
    tmpstr = (user->username[0] == '~' ? &user->username[1] :
	      user->username);
#endif /* IGNORE_FIRST_CHAR */

    while(*tmpstr)
      {
	c = *(tmpstr++);
	if (islower(c))
	  {
	    lower++;
	    continue;
	  }
	if (isupper(c))
	  {
	    upper++;
	    continue;
	  }
	if ((!isalnum(c) && !strchr(" -_.", c)) || (c > 127))
	  special++;
      }
#ifdef NO_MIXED_CASE
    if (lower && upper)
      {
	sendto_realops_lev(REJ_LEV,"Invalid username: %s (%s@%s)",
			   nick, user->username, user->host);
	ircstp->is_ref++;
	(void)ircsprintf(tmpstr2, "Invalid username [%s]", user->username);
	return exit_client(cptr, sptr, &me, tmpstr2);
      }
#endif /* NO_MIXED_CASE */
#ifdef NO_SPECIAL
	if (special)
	  {
	    sendto_realops_lev(REJ_LEV,"Invalid username: %s (%s@%s)",
			       nick, user->username, user->host);
	    ircstp->is_ref++;
	    (void)ircsprintf(tmpstr2, "Invalid username [%s]",
		       user->username);
	    return exit_client(cptr, sptr, &me, tmpstr2);
	  }
#endif /* NO_SPECIAL */
	    
	    /*
	     * Ok, now check the username they provided, if different
	     */
	    lower = upper = special = cc = 0;
	    
	    if (strcmp(user->username, username))
	      {
	      
#ifdef IGNORE_FIRST_CHAR
                tmpstr = (username[0] == '~' ? &username[2] : &username[1]);
		/* Ok, we don't want to TOTALLY ignore the first character.
		 * We should at least check it for control charcters, etc
		 * -ThemBones
		 */
		cc = (username[0] == '~' ? username[1] : username[0]);
		
		if ((!isalnum(cc) && !strchr(" -_.",cc)) || (cc > 127))
		  special++;
#else
                tmpstr = (username[0] == '~' ? &username[1] : username);
#endif /* IGNORE_FIRST_CHAR */
                while(*tmpstr)
                  {
                    c = *(tmpstr++);
                    if (islower(c))
		      {
			lower++;
			continue;
		      }
                    if (isupper(c))
		      {
			upper++;
			continue;
		      }
                    if ((!isalnum(c) && !strchr(" -_.", c)) || (c > 127))
                      special++;
                  }
#ifdef NO_MIXED_CASE
                if (lower && upper)
                  {
                    sendto_realops_lev(REJ_LEV,"Invalid username: %s (%s@%s)",
				       nick, username, user->host);
                    ircstp->is_ref++;
		    (void)ircsprintf(tmpstr2, "Invalid username [%s]",
			       username);
                    return exit_client(cptr, sptr, &me, tmpstr2);
                  }
#endif /* NO_MIXED_CASE */
#ifdef NO_SPECIAL
                if (special)
                  {
                    sendto_realops_lev(REJ_LEV,"Invalid username: %s (%s@%s)",
				       nick, username, user->host);
                    ircstp->is_ref++;
		    (void)ircsprintf(tmpstr2, "Invalid username [%s]",
			       username);
                    return exit_client(cptr, sptr, &me, tmpstr2);
		  }
#endif /* NO_SPECIAL */
		 } /* usernames different */
  }
#endif /* NO_MIXED_CASE || NO_SPECIAL */

  /* 
   * Absolutely always reject any '*' '!' '?' in an user name
   * reject any odd control characters names.
   */

  {
    unsigned char *p;
    p = (unsigned char *) user->username;

    while(*p)
      {
	if( (*p > 127) || (*p <= ' ') || 
	    (*p == '*') || (*p == '?') || (*p == '!'))
	  {
	    sendto_realops_lev(REJ_LEV,"Invalid username: %s (%s@%s)",
			       nick, user->username, user->host);
	    ircstp->is_ref++;
	    (void)ircsprintf(tmpstr2, "Invalid username [%s]",
			     user->username);
	    return exit_client(cptr, sptr, &me, tmpstr2);
	  }

	p++;
      }
    /* 
     * reject single character usernames which aren't alphabetic 
     * i.e. reject jokers who have '-@somehost' or '.@somehost'
     *
     * -Dianora
     */

    if((user->username[1] == '\0') && !isalpha(user->username[0]))
      {
	sendto_realops_lev(REJ_LEV,"Invalid username: %s (%s@%s)",
			   nick, user->username, user->host);
	ircstp->is_ref++;
	(void)ircsprintf(tmpstr2, "Invalid username [%s]",
			 user->username);
	return exit_client(cptr, sptr, &me, tmpstr2);
      }
  }

  if(!IsAnOper(sptr) && (aconf = find_special_conf(sptr->info,CONF_XLINE)) )
    {
      char *reason;
      if(aconf->passwd)
	reason = aconf->passwd;
      else
	reason = "NONE";

      if(aconf->port)
	{
	  ircstp->is_ref++;
	  sendto_realops_lev(REJ_LEV,"X-line Rejecting [%s] [%s], user %s",
			     sptr->info,
			     reason,
			     get_client_name(cptr, FALSE));
      
	  return exit_client(cptr, sptr, &me, "Bad user info");
	}
      else
	sendto_realops_lev(REJ_LEV,"X-line Warning [%s] [%s], user %s",
			   sptr->info,
			   reason,
			   get_client_name(cptr, FALSE));
      
    }

  sendto_realops_lev(CCONN_LEV,
		     "Client connecting: %s (%s@%s) [%s] {%d}",
		     nick,
		     user->username,
		     user->host,
		     sptr->hostip,
		     get_client_class(sptr));

  if ((++Count.local) > Count.max_loc)
    {
      Count.max_loc = Count.local;
      if (!(Count.max_loc % 10))
	sendto_ops("New Max Local Clients: %d",
		   Count.max_loc);
    }
   }
  else
    strncpyzt(user->username, username, USERLEN+1);

  SetClient(sptr);

  sptr->servptr = find_server(user->server, NULL);
  if (!sptr->servptr)
    {
      sendto_ops("Ghost killed: %s on invalid server %s",
		 sptr->name, sptr->user->server);
      sendto_one(cptr,":%s KILL %s: %s (Ghosted, %s doesn't exist)",
		 me.name, sptr->name, me.name, user->server);
      sptr->flags |= FLAGS_KILLED;
      return exit_client(NULL, sptr, &me, "Ghost");
    }
  add_client_to_llist(&(sptr->servptr->serv->users), sptr);

/* Increment our total user count here */
  if (++Count.total > Count.max_tot)
    Count.max_tot = Count.total;

  if (MyConnect(sptr))
    {
#ifdef MAXBUFFERS
/* Let's try changing the socket options for the client here...
 * -Taner
 */
      reset_sock_opts(sptr->fd, 0);
      /* End sock_opt hack */
#endif
      sendto_one(sptr, rpl_str(RPL_WELCOME), me.name, nick, nick);
      /* This is a duplicate of the NOTICE but see below...*/
      sendto_one(sptr, rpl_str(RPL_YOURHOST), me.name, nick,
		 get_client_name(sptr->acpt, FALSE), version);
      
      /*
      ** Don't mess with this one - IRCII needs it! -Avalon
      */
      sendto_one(sptr,
		 "NOTICE %s :*** Your host is %s, running version %s",
		 nick, get_client_name(sptr->acpt, FALSE), version);
      
      sendto_one(sptr, rpl_str(RPL_CREATED),me.name,nick,creation);
      sendto_one(sptr, rpl_str(RPL_MYINFO), me.name, parv[0],
		 me.name, version);
      (void)show_lusers(sptr, sptr, 1, parv);
      
      sendto_one(sptr,"NOTICE %s :*** Notice -- motd was last changed at %s",
		 nick, motd_last_changed_date);
#ifdef SHORT_MOTD
      sendto_one(sptr,
		 "NOTICE %s :*** Notice -- Please read the motd if you haven't read it",
		 nick);
      
      sendto_one(sptr, rpl_str(RPL_MOTDSTART),
		 me.name, parv[0], me.name);
      
      sendto_one(sptr,
		 rpl_str(RPL_MOTD),
		 me.name, parv[0],
		 "*** This is the short motd ***"
		 );

      sendto_one(sptr, rpl_str(RPL_ENDOFMOTD),
		 me.name, parv[0]);
#else
      (void)send_motd(sptr, sptr, 1, parv,motd);
#endif
#ifdef LITTLE_I_LINES
      if(sptr->confs && sptr->confs->value.aconf &&
	 (sptr->confs->value.aconf->flags
	  & CONF_FLAGS_LITTLE_I_LINE))
	{
	  SetRestricted(sptr);
	  sendto_one(sptr,"NOTICE %s :*** Notice -- You are in a restricted access mode",nick);
	  sendto_one(sptr,"NOTICE %s :*** Notice -- You can not chanop others",nick);
	}
#endif

      nextping = timeofday;

#if defined(EXTRA_BOT_NOTICES) && defined(BOT_GCOS_WARN)
      sprintf(botgecos, "/msg %s hello", nick);
      if ((strcasecmp(sptr->info, botgecos)==0)
	  || (!matches("/msg * hello", sptr->info)))
	{
	  sendto_realops_lev(REJ_LEV,
			     "EggDrop signon alarm activated: %s [%s@%s] : [gecos: %s]",
			     nick, sptr->user->username,
			     sptr->user->host, sptr->info);
	}
      
      sprintf(botgecos, "/msg %s help", nick);
      if ((strcasecmp(sptr->info, botgecos)==0) ||
	  (!matches("/msg * help", sptr->info)))
	{
	  sendto_realops_lev(REJ_LEV,
			     "Generic bot signon alarm activated: %s [%s@%s] : [gecos: %s]",
			     nick, sptr->user->username,
			     sptr->user->host, sptr->info);
	}
#endif /* EXTRA_BOT_NOTICES && BOT_GCOS_WARN */
    }
  else if (IsServer(cptr))
    {
      aClient *acptr;
      if ((acptr = find_server(user->server, NULL)) &&
	  acptr->from != sptr->from)
	{
	  sendto_realops_lev(DEBUG_LEV, 
			     "Bad User [%s] :%s USER %s@%s %s, != %s[%s]",
			     cptr->name, nick, user->username,
			     user->host, user->server,
			     acptr->name, acptr->from->name);
	  sendto_one(cptr,
		     ":%s KILL %s :%s (%s != %s[%s] USER from wrong direction)",
		     me.name, sptr->name, me.name, user->server,
		     acptr->from->name, acptr->from->sockhost);
	  sptr->flags |= FLAGS_KILLED;
	  return exit_client(sptr, sptr, &me,
			     "USER server wrong direction");
	  
	}
      /*
       * Super GhostDetect:
       *	If we can't find the server the user is supposed to be on,
       * then simply blow the user away.	-Taner
       */
      if (!acptr)
	{
	  sendto_one(cptr,
		     ":%s KILL %s :%s GHOST (no server %s on the net)",
		     me.name,
			  sptr->name, me.name, user->server);
	  sendto_realops("No server %s for user %s[%s@%s] from %s",
			       user->server,
			  sptr->name, user->username,
			  user->host, sptr->from->name);
	  sptr->flags |= FLAGS_KILLED;
	  return exit_client(sptr, sptr, &me, "Ghosted Client");
	}
    }

  send_umode(NULL, sptr, 0, SEND_UMODES, ubuf);
  if (!*ubuf)
    {
      ubuf[0] = '+';
      ubuf[1] = '\0';
    }
  
  /* LINKLIST 
   * add to local client link list -Dianora
   * I really want to move this add to link list
   * inside the if (MyConnect(sptr)) up above
   * but I also want to make sure its really good and registered
   * local client
   *
   * double link list only for clients, traversing
   * a small link list for opers/servers isn't a big deal
   * but it is for clients -Dianora
   */

  if (MyConnect(sptr))
    {
      if(local_cptr_list)
	local_cptr_list->previous_local_client = sptr;
      sptr->previous_local_client = (aClient *)NULL;
      sptr->next_local_client = local_cptr_list;
      local_cptr_list = sptr;
    }
  
  sendto_serv_butone(cptr, "NICK %s %d %ld %s %s %s %s :%s",
		     nick, sptr->hopcount+1, sptr->tsinfo, ubuf,
		     user->username, user->host, user->server,
		     sptr->info);
  return 0;
}

/*
** m_nick
**	parv[0] = sender prefix
**	parv[1] = nickname
**	parv[2]	= optional hopcount when new user; TS when nick change
**	parv[3] = optional TS
**	parv[4] = optional umode
**	parv[5] = optional username
**	parv[6] = optional hostname
**	parv[7]	= optional server
**	parv[8]	= optional ircname
*/
int	m_nick(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aClient *acptr;
  char	nick[NICKLEN+2], *s;
  ts_val newts = 0;
  int	sameuser = 0, fromTS = 0;

  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NONICKNAMEGIVEN),
		 me.name, parv[0]);
      return 0;
    }

  if (!IsServer(sptr) && IsServer(cptr) && parc > 2)
    newts = atol(parv[2]);
  else if (IsServer(sptr) && parc > 3)
    newts = atol(parv[3]);
  else parc = 2;

  /*
   * parc == 2 on a normal client sign on (local) and a normal
   *      client nick change
   * parc == 4 on a normal server-to-server client nick change
   *      notice
   * parc == 9 on a normal TS style server-to-server NICK
   *      introduction
   */
  if ((parc > 4) && (parc < 9))
    {
      /*
       * We got the wrong number of params. Someone is trying
       * to trick us. Kill it. -ThemBones
       * As discussed with ThemBones, not much point to this code now
       * sending a whack of global kills would also be more annoying
       * then its worth, just note the problem, and continue
       * -Dianora
       */
      sendto_realops("BAD NICK: %s[%s@%s] on %s (from %s)", parv[1],
		     (parc >= 6) ? parv[5] : "-",
		     (parc >= 7) ? parv[6] : "-",
		     (parc >= 8) ? parv[7] : "-", parv[0]);
      
     }

  if ((parc >= 7) && (!strchr(parv[6], '.')))
    {
      /*
       * Ok, we got the right number of params, but there
       * isn't a single dot in the hostname, which is suspicious.
       * Don't fret about it just kill it. - ThemBones
       */
      sendto_realops("BAD HOSTNAME: %s[%s@%s] on %s (from %s)",
		     parv[0], parv[5], parv[6], parv[7], parv[0]);
    }

  fromTS = (parc > 6);
  
  if (MyClient(sptr) && (s = (char *)strchr(parv[1], '~')))
    *s = '\0';
  strncpyzt(nick, parv[1], NICKLEN+1);

  if(MyClient(sptr) &&
     !IsAnOper(sptr) && find_special_conf(nick,CONF_QUARANTINED_NICK)) 
    {
      sendto_realops_lev(REJ_LEV,
			 "Quarantined nick [%s], dumping user %s",
			 nick,get_client_name(cptr, FALSE));

      return exit_client(cptr, sptr, &me, "quarantined nick");
    }

  /*
   * if do_nick_name() returns a null name OR if the server sent a nick
   * name and do_nick_name() changed it in some way (due to rules of nick
   * creation) then reject it. If from a server and we reject it,
   * and KILL it. -avalon 4/4/92
   */
  if (do_nick_name(nick) == 0 ||
      (IsServer(cptr) && strcmp(nick, parv[1])))
    {
      sendto_one(sptr, err_str(ERR_ERRONEUSNICKNAME),
		 me.name, parv[0], parv[1]);
      
      if (IsServer(cptr))
	{
	  ircstp->is_kill++;
	  sendto_realops_lev(DEBUG_LEV, "Bad Nick: %s From: %s %s",
			     parv[1], parv[0],
			     get_client_name(cptr, FALSE));
	  sendto_one(cptr, ":%s KILL %s :%s (%s <- %s[%s])",
		     me.name, parv[1], me.name, parv[1],
		     nick, cptr->name);
	  if (sptr != cptr) /* bad nick change */
	    {
	      sendto_serv_butone(cptr,
				 ":%s KILL %s :%s (%s <- %s!%s@%s)",
				 me.name, parv[0], me.name,
				 get_client_name(cptr, FALSE),
				 parv[0],
				 sptr->user ? sptr->username : "",
				 sptr->user ? sptr->user->server :
				 cptr->name);
	      sptr->flags |= FLAGS_KILLED;
	      return exit_client(cptr,sptr,&me,"BadNick");
	    }
	}
      return 0;
    }

  /*
  ** Check against nick name collisions.
  **
  ** Put this 'if' here so that the nesting goes nicely on the screen :)
  ** We check against server name list before determining if the nickname
  ** is present in the nicklist (due to the way the below for loop is
  ** constructed). -avalon
  */
  if ((acptr = find_server(nick, NULL)))
    if (MyConnect(sptr))
      {
	sendto_one(sptr, err_str(ERR_NICKNAMEINUSE), me.name,
		   BadPtr(parv[0]) ? "*" : parv[0], nick);
	return 0; /* NICK message ignored */
      }
  /*
  ** acptr already has result from previous find_server()
  */
  /*
    
    Well. unless we have a capricious server on the net,
    a nick can never bet the same as a server name - Dianora
    */

  if (acptr)
    {
      /*
      ** We have a nickname trying to use the same name as
      ** a server. Send out a nick collision KILL to remove
      ** the nickname. As long as only a KILL is sent out,
      ** there is no danger of the server being disconnected.
      ** Ultimate way to jupiter a nick ? >;-). -avalon
      */
      sendto_ops("Nick collision on %s(%s <- %s)",
		 sptr->name, acptr->from->name,
		 get_client_name(cptr, FALSE));
      ircstp->is_kill++;
      sendto_one(cptr, ":%s KILL %s :%s (%s <- %s)",
		 me.name, sptr->name, me.name, acptr->from->name,
		 /* NOTE: Cannot use get_client_name
		 ** twice here, it returns static
		 ** string pointer--the other info
		 ** would be lost
		 */
		 get_client_name(cptr, FALSE));
      sptr->flags |= FLAGS_KILLED;
      return exit_client(cptr, sptr, &me, "Nick/Server collision");
    }
  

  if (!(acptr = find_client(nick, NULL)))
    return(nickkilldone(cptr,sptr,parc,parv,newts,nick));  /* No collisions,
						       * all clear...
						       */

  /*
  ** If acptr == sptr, then we have a client doing a nick
  ** change between *equivalent* nicknames as far as server
  ** is concerned (user is changing the case of his/her
  ** nickname or somesuch)
  */
  if (acptr == sptr)
   {
    if (strcmp(acptr->name, nick) != 0)
      /*
      ** Allows change of case in his/her nick
      */
      return(nickkilldone(cptr,sptr,parc,parv,newts,nick)); /* -- go and process change */
    else
      {
        /*
        ** This is just ':old NICK old' type thing.
        ** Just forget the whole thing here. There is
        ** no point forwarding it to anywhere,
        ** especially since servers prior to this
        ** version would treat it as nick collision.
        */
        return 0; /* NICK Message ignored */
      }
   }
  /*
  ** Note: From this point forward it can be assumed that
  ** acptr != sptr (point to different client structures).
  */
  /*
  ** If the older one is "non-person", the new entry is just
  ** allowed to overwrite it. Just silently drop non-person,
  ** and proceed with the nick. This should take care of the
  ** "dormant nick" way of generating collisions...
  */
  if (IsUnknown(acptr)) 
   {
    if (MyConnect(acptr))
      {
	exit_client(NULL, acptr, &me, "Overridden");
	return(nickkilldone(cptr,sptr,parc,parv,newts,nick));
      }
    else
      {
	if (fromTS && !(acptr->user))
          {
	    sendto_ops("Nick Collision on %s(%s(NOUSER) <- %s!%s@%s)(TS:%s)",
		   acptr->name, acptr->from->name, parv[1], parv[5], parv[6],
		   cptr->name);
	    sendto_serv_butone(NULL, /* all servers */
			   ":%s KILL %s :%s (%s(NOUSER) <- %s!%s@%s)(TS:%s)", me.name,
			   acptr->name, me.name, acptr->from->name, parv[1], parv[5],
			   parv[6], cptr->name);
	    acptr->flags |= FLAGS_KILLED;
	    /* Having no USER struct should be ok... */
	    return exit_client(cptr, acptr, &me,
			   "Got TS NICK before Non-TS USER");
        }
      }
   }
  /*
  ** Decide, we really have a nick collision and deal with it
  */
  if (!IsServer(cptr))
    {
      /*
      ** NICK is coming from local client connection. Just
      ** send error reply and ignore the command.
      */
      sendto_one(sptr, err_str(ERR_NICKNAMEINUSE),
		 /* parv[0] is empty when connecting */
		 me.name, BadPtr(parv[0]) ? "*" : parv[0], nick);
      return 0; /* NICK message ignored */
    }
  /*
  ** NICK was coming from a server connection. Means that the same
  ** nick is registered for different users by different server.
  ** This is either a race condition (two users coming online about
  ** same time, or net reconnecting) or just two net fragments becoming
  ** joined and having same nicks in use. We cannot have TWO users with
  ** same nick--purge this NICK from the system with a KILL... >;)
  */
  /*
  ** This seemingly obscure test (sptr == cptr) differentiates
  ** between "NICK new" (TRUE) and ":old NICK new" (FALSE) forms.
  */
  /* 
  ** Changed to something reasonable like IsServer(sptr)
  ** (true if "NICK new", false if ":old NICK new") -orabidoo
  */

  if (IsServer(sptr))
    {
      /*
      ** A new NICK being introduced by a neighbouring
      ** server (e.g. message type "NICK new" received)
      */
      if (!newts || !acptr->tsinfo
	  || (newts == acptr->tsinfo))
	{
	  sendto_ops("Nick collision on %s(%s <- %s)(both killed)",
		     acptr->name, acptr->from->name,
		     get_client_name(cptr, FALSE));
	  ircstp->is_kill++;
	  sendto_one(acptr, err_str(ERR_NICKCOLLISION),
		     me.name, acptr->name, acptr->name);
	  sendto_serv_butone(NULL, /* all servers */
			     ":%s KILL %s :%s (%s <- %s)",
			     me.name, acptr->name, me.name,
			     acptr->from->name,
			     /* NOTE: Cannot use get_client_name twice
			     ** here, it returns static string pointer:
			     ** the other info would be lost
			     */
			     get_client_name(cptr, FALSE));
	  acptr->flags |= FLAGS_KILLED;
	  return exit_client(cptr, acptr, &me, "Nick collision");
	}
      else
	{
	  sameuser =  fromTS && (acptr->user) &&
	    mycmp(acptr->user->username, parv[5]) == 0 &&
	    mycmp(acptr->user->host, parv[6]) == 0;
	  if ((sameuser && newts < acptr->tsinfo) ||
	      (!sameuser && newts > acptr->tsinfo))
	    return 0;
	  else
	    {
	      if (sameuser)
		sendto_ops("Nick collision on %s(%s <- %s)(older killed)",
			   acptr->name, acptr->from->name,
			   get_client_name(cptr, FALSE));
	      else
		sendto_ops("Nick collision on %s(%s <- %s)(newer killed)",
			   acptr->name, acptr->from->name,
			   get_client_name(cptr, FALSE));
	      
	      ircstp->is_kill++;
	      sendto_one(acptr, err_str(ERR_NICKCOLLISION),
			 me.name, acptr->name, acptr->name);
	      sendto_serv_butone(sptr, /* all servers but sptr */
				 ":%s KILL %s :%s (%s <- %s)",
				 me.name, acptr->name, me.name,
				 acptr->from->name,
				 get_client_name(cptr, FALSE));
	      acptr->flags |= FLAGS_KILLED;
	      (void)exit_client(cptr, acptr, &me, "Nick collision");
	      return nickkilldone(cptr,sptr,parc,parv,newts,nick);
	    }
	}
    }
  /*
  ** A NICK change has collided (e.g. message type
  ** ":old NICK new". This requires more complex cleanout.
  ** Both clients must be purged from this server, the "new"
  ** must be killed from the incoming connection, and "old" must
  ** be purged from all outgoing connections.
  */
  if ( !newts || !acptr->tsinfo || (newts == acptr->tsinfo) ||
      !sptr->user)
    {
      sendto_ops("Nick change collision from %s to %s(%s <- %s)(both killed)",
		 sptr->name, acptr->name, acptr->from->name,
		 get_client_name(cptr, FALSE));
      ircstp->is_kill++;
      sendto_one(acptr, err_str(ERR_NICKCOLLISION),
		 me.name, acptr->name, acptr->name);
      sendto_serv_butone(NULL, /* KILL old from outgoing servers */
			 ":%s KILL %s :%s (%s(%s) <- %s)",
			 me.name, sptr->name, me.name, acptr->from->name,
			 acptr->name, get_client_name(cptr, FALSE));
      ircstp->is_kill++;
      sendto_serv_butone(NULL, /* Kill new from incoming link */
			 ":%s KILL %s :%s (%s <- %s(%s))",
			 me.name, acptr->name, me.name, acptr->from->name,
			 get_client_name(cptr, FALSE), sptr->name);
      acptr->flags |= FLAGS_KILLED;
      (void)exit_client(NULL, acptr, &me, "Nick collision(new)");
      sptr->flags |= FLAGS_KILLED;
      return exit_client(cptr, sptr, &me, "Nick collision(old)");
    }
  else
    {
      sameuser = mycmp(acptr->user->username,
		       sptr->user->username) == 0 &&
	mycmp(acptr->user->host, sptr->user->host) == 0;
      if ((sameuser && newts < acptr->tsinfo) ||
	  (!sameuser && newts > acptr->tsinfo))
	{
	  if (sameuser)
	    sendto_ops("Nick change collision from %s to %s(%s <- %s)(older killed)",
		       sptr->name, acptr->name, acptr->from->name,
		       get_client_name(cptr, FALSE));
	  else
	    sendto_ops("Nick change collision from %s to %s(%s <- %s)(newer killed)",
		       sptr->name, acptr->name, acptr->from->name,
		       get_client_name(cptr, FALSE));
	  ircstp->is_kill++;
	  sendto_serv_butone(cptr, /* KILL old from outgoing servers */
			     ":%s KILL %s :%s (%s(%s) <- %s)",
			     me.name, sptr->name, me.name, acptr->from->name,
			     acptr->name, get_client_name(cptr, FALSE));
	  sptr->flags |= FLAGS_KILLED;
	  if (sameuser)
	    return exit_client(cptr, sptr, &me, "Nick collision(old)");
	  else
	    return exit_client(cptr, sptr, &me, "Nick collision(new)");
	}
      else
	{
	  if (sameuser)
	    sendto_ops("Nick collision on %s(%s <- %s)(older killed)",
		       acptr->name, acptr->from->name,
		       get_client_name(cptr, FALSE));
	  else
	    sendto_ops("Nick collision on %s(%s <- %s)(newer killed)",
		       acptr->name, acptr->from->name,
		       get_client_name(cptr, FALSE));
	  
	  ircstp->is_kill++;
	  sendto_one(acptr, err_str(ERR_NICKCOLLISION),
		     me.name, acptr->name, acptr->name);
	  sendto_serv_butone(sptr, /* all servers but sptr */
			     ":%s KILL %s :%s (%s <- %s)",
			     me.name, acptr->name, me.name,
			     acptr->from->name,
			     get_client_name(cptr, FALSE));
	  acptr->flags |= FLAGS_KILLED;
	  (void)exit_client(cptr, acptr, &me, "Nick collision");
	  /* goto nickkilldone; */
	}
    }
  return(nickkilldone(cptr,sptr,parc,parv,newts,nick));
}


int nickkilldone(aClient *cptr, aClient *sptr, int parc,
		 char *parv[], ts_val newts,char *nick)
{

  if (IsServer(sptr))
    {
      /* A server introducing a new client, change source */
      
      sptr = make_client(cptr);
      add_client_to_list(sptr);		/* double linked list */
      if (parc > 2)
	sptr->hopcount = atoi(parv[2]);
      if (newts)
	sptr->tsinfo = newts;
      else
	{
	  newts = sptr->tsinfo = (ts_val)timeofday;
	  ts_warn("Remote nick %s introduced without a TS", nick);
	}
      /* copy the nick in place */
      (void)strcpy(sptr->name, nick);
      (void)add_to_client_hash_table(nick, sptr);
      if (parc > 8)
	{
	  Reg	int *s, flag;
	  Reg	char *m;
	  
	  /*
	  ** parse the usermodes -orabidoo
	  */
	  m = &parv[4][1];
	  while (*m)
	    {
	      for (s = user_modes; (flag = *s); s += 2)
		if (*m == *(s+1))
		  {
		    if (flag == FLAGS_INVISIBLE)
		      Count.invisi++;
		    if ((flag == FLAGS_OPER) || (flag == FLAGS_LOCOP))
		      {
			Count.oper++;
		      }
		    sptr->flags |= flag&SEND_UMODES;
		    break;
		  }
	      m++;
	    }
	  
	  return do_user(nick, cptr, sptr, parv[5], parv[6],
			 parv[7], parv[8]);
	}
    }
  else if (sptr->name[0])
    {
      /*
      ** Client just changing his/her nick. If he/she is
      ** on a channel, send note of change to all clients
      ** on that channel. Propagate notice to other servers.
      */
      if (mycmp(parv[0], nick))
	sptr->tsinfo = newts ? newts : (ts_val)timeofday;

      if(MyConnect(sptr) && IsRegisteredUser(sptr))
	{     
#ifdef ANTI_NICK_FLOOD

          if( (sptr->last_nick_change + MAX_NICK_TIME) < NOW)
	    sptr->number_of_nick_changes = 0;
          sptr->last_nick_change = NOW;
            sptr->number_of_nick_changes++;

          if(sptr->number_of_nick_changes <= MAX_NICK_CHANGES)
	    {
#endif
	      sendto_realops_lev(NCHANGE_LEV,
				 "Nick change: From %s to %s [%s@%s]",
				 parv[0], nick, sptr->user->username,
				 sptr->user->host);

	      sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
	      if (sptr->user)
		{
		  add_history(sptr,1);
	      
		  sendto_serv_butone(cptr, ":%s NICK %s :%ld",
				     parv[0], nick, sptr->tsinfo);
		}
#ifdef ANTI_NICK_FLOOD
	    }
	  else
	    {
	      sendto_one(sptr,
			 ":%s NOTICE %s :*** Notice -- Too many nick changes wait %d seconds before trying to change it again.",
			 me.name,
			 sptr->name,
			 MAX_NICK_TIME);
	      return 0;
	    }
#endif
	}
      else
	{
	  sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
	  if (sptr->user)
	    {
	      add_history(sptr,1);
	      
	      sendto_serv_butone(cptr, ":%s NICK %s :%ld",
				 parv[0], nick, sptr->tsinfo);
	    }
	}
    }
  else
    {
      /* Client setting NICK the first time */
      

      /* This had to be copied here to avoid problems.. */
      (void)strcpy(sptr->name, nick);
      sptr->tsinfo = timeofday;
      if (sptr->user)
	{
	  /*
	  ** USER already received, now we have NICK.
	  ** *NOTE* For servers "NICK" *must* precede the
	  ** user message (giving USER before NICK is possible
	  ** only for local client connection!). register_user
	  ** may reject the client and call exit_client for it
	  ** --must test this and exit m_nick too!!!
	  */
	    if (register_user(cptr, sptr, nick, sptr->user->username)
		== FLUSH_BUFFER)
	      return FLUSH_BUFFER;
	}
    }

  /*
  **  Finally set new nick name.
  */
  if (sptr->name[0])
    (void)del_from_client_hash_table(sptr->name, sptr);
  (void)strcpy(sptr->name, nick);
  (void)add_to_client_hash_table(nick, sptr);

  return 0;
}

/* Code provided by orabidoo */
/* a random number generator loosely based on RC5;
   assumes ints are at least 32 bit */
 
unsigned long my_rand() {
  static unsigned long s = 0, t = 0, k = 12345678;
  int i;
 
  if (s == 0 && t == 0) {
    s = (unsigned long)getpid();
    t = (unsigned long)time(NULL);
  }
  for (i=0; i<12; i++) {
    s = (((s^t) << (t&31)) | ((s^t) >> (31 - (t&31)))) + k;
    k += s + t;
    t = (((t^s) << (s&31)) | ((t^s) >> (31 - (s&31)))) + k;
    k += s + t;
  }
  return s;
}


/*
** m_message (used in m_private() and m_notice())
** the general function to deliver MSG's between users/channels
**
**	parv[0] = sender prefix
**	parv[1] = receiver list
**	parv[2] = message text
**
** massive cleanup
** rev argv 6/91
**
*/

static	int	m_message(aClient *cptr,
			  aClient *sptr,
			  int parc,
			  char *parv[],
			  int notice)
{
  Reg	aClient	*acptr;
  Reg	char	*s;
  aChannel *chptr;
  char	*nick, *server, *p, *cmd, *host;
  int type=0;

  cmd = notice ? MSG_NOTICE : MSG_PRIVATE;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NORECIPIENT),
		 me.name, parv[0], cmd);
      return -1;
    }

  if (parc < 3 || *parv[2] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
      return -1;
    }

  if (MyConnect(sptr))
    {
#ifdef ANTI_SPAMBOT
#ifndef ANTI_SPAMBOT_WARN_ONLY
      /* if its a spambot, just ignore it */
      if(sptr->join_leave_count >= MAX_JOIN_LEAVE_COUNT)
	return 0;
#endif
#endif
      /* As Mortiis points out, if there is only one target,
       * the call to canonize is silly
       */
      /*      parv[1] = canonize(parv[1]); */
    }
  /* 
   * If the target contains a , it will barf tough.
   */

  /*  nick = strtoken(&p, parv[1], ",");*/
  nick = parv[1];

#ifdef EXTRA_BOT_NOTICES
  if (MyConnect(sptr)) /* don't check for remote bots */
    {
      /* ComBot */
      if (strstr(nick, "blehdhfsddd"))
	sendto_realops_lev(REJ_LEV,
			   "ComBot alarm activated: %s [%s@%s] : [/msg %s]",
			   parv[0], sptr->user->username, 
			   sptr->user->host, nick);
      
      /* EggDrop finder by desynched */
      if (!matches("h?4x0r?", nick))
	sendto_realops_lev(REJ_LEV,
			   "EggDrop alarm #2 activated: %s [%s@%s] : [/msg %s]",
			   parv[0], sptr->user->username,
			   sptr->user->host, nick);

      /* HOFBot finder by desynched */
      if (strstr(nick,"blahb1ah"))
	sendto_realops_lev(REJ_LEV, "HOFBot alarm activated: %s [%s@%s] : [/msg %s]",
			   parv[0], sptr->user->username, sptr->user->host, nick);

      /* Stealth finder by desynched */
      if (strstr(nick,"uthfgse"))
	sendto_realops_lev(REJ_LEV, "Stealth Eggdrop alarm activated: %s [%s@%s] : [/msg %s]",
			   parv[0], sptr->user->username, sptr->user->host, nick);
    }
#endif
    
  /*
  ** nickname addressed?
  */
  if ((acptr = find_person(nick, NULL)))
    {
#ifdef	IDLE_CHECK
      /* reset idle time for message only if target exists */
      if(MyClient(sptr) && sptr->user)
	sptr->user->last = timeofday;
#endif
#ifdef ANTI_SPAMBOT_EXTRA
      if(MyConnect(sptr) && !IsElined(sptr) &&
	 (acptr != sptr->last_client_messaged))
	{
	  sptr->person_privmsgs++;
	  sptr->last_client_messaged = acptr;
	}
#endif
#ifdef FLUD
      if(!notice && MyFludConnect(acptr))
	if(check_for_ctcp(parv[2]))
	  if(check_for_flud(sptr, acptr, NULL, 1))
	    return 0;
#endif
      if (!notice && MyConnect(sptr) &&
	  acptr->user && acptr->user->away)
	sendto_one(sptr, rpl_str(RPL_AWAY), me.name,
		   parv[0], acptr->name,
		   acptr->user->away);
      sendto_prefix_one(acptr, sptr, ":%s %s %s :%s",
			parv[0], cmd, nick, parv[2]);

#ifdef	IDLE_CHECK
      /* reset idle time for message only if its not to self */
      if (sptr != acptr)
	{
	  if(sptr->user)
	    sptr->user->last = timeofday;
	}
#endif

#ifdef        EXTRA_BOT_NOTICES
      if (sptr == acptr)      /* msging self */
	{
	  if (strstr(parv[2], "\001AWAKE\001"))
	    sendto_realops_lev(REJ_LEV, "EggDrop alarm activated: %s [%s@%s] : [/ctcp %s AWAKE]",
			       parv[0], acptr->user->username, acptr->user->host, parv[0]);
	  if (strstr(parv[2], "-=Ping Check=-"))
	    sendto_realops_lev(REJ_LEV,"JohBot alarm activated: %s [%s@%s] : [/msg %s %s]",
			       parv[0], acptr->user->username, acptr->user->host, parv[0], parv[2]);
	  if (strstr(parv[2], "\001PRVMSG blah\001"))
	    sendto_realops_lev(REJ_LEV, "vlad 2.1+tb alarm activated: %s [%s@%s] : [/msg %s %s]",
			       parv[0], acptr->user->username, acptr->user->host, parv[0], parv[2]);
	}
#endif
#ifdef ANTI_SPAMBOT_EXTRA
      if( MyConnect(sptr) && spambot_privmsg_count &&
	  ((sptr->person_privmsgs - sptr->channel_privmsgs)
	   > spambot_privmsg_count) )
	{
	  sendto_realops_lev(REJ_LEV,
"Possible spambot %s [%s@%s] : privmsgs to clients %d privmsgs to channels %d",
			 sptr->name, sptr->user->username,
			 sptr->user->host,
			 sptr->person_privmsgs,sptr->channel_privmsgs);
	  /* and report it if happens again */
	  sptr->person_privmsgs = 0;
	  sptr->channel_privmsgs = 0;
	}
#endif
      return 0;
    }

  /*
  ** @# type of channel msg?
  */

  if(*nick == '@')
    type = MODE_CHANOP;
  else if(*nick == '+')
    type = MODE_CHANOP|MODE_VOICE;

  if(type)
    {
      /* Strip if using DALnet chanop/voice prefix.  -- David-R */
      if (*(nick+1) == '@' || *(nick+1) == '+')
	{
	  nick++;
	  *nick = '@';
	  type = MODE_CHANOP|MODE_VOICE;
	}

      /* suggested by Mortiis */
      if(!*nick)	/* if its a '\0' dump it, there is no recipient */
	{
	  sendto_one(sptr, err_str(ERR_NORECIPIENT),
		     me.name, parv[0], cmd);
	  return -1;
	}

      if (!IsPerson(sptr))	/* This means, servers can't send messages */
	return -1;

      /* At this point, nick+1 should be a channel name i.e. #foo or &foo
       * if the channel is found, fine, if not report an error
       */

      if (chptr = find_channel(nick+1, NullChn))
	{
#ifdef	IDLE_CHECK
	  /* reset idle time for message only if target exists */
	  if(MyClient(sptr) && sptr->user)
	    sptr->user->last = timeofday;
#endif
#ifdef ANTI_SPAMBOT_EXTRA
	  if(MyConnect(sptr) && !IsElined(sptr))
	    sptr->channel_privmsgs++;
#endif
#ifdef FLUD
	  if(!notice)
	    if(check_for_ctcp(parv[2]))
	      check_for_flud(sptr, NULL, chptr, 1);
#endif /* FLUD */

	  if (!is_chan_op(sptr,chptr))
	    {
	      if (!notice)
		{
		  sendto_one(sptr, err_str(ERR_CANNOTSENDTOCHAN),
			     me.name, parv[0], nick);
		}
	      return -1;
	    }
	  else
	    {
	      sendto_channel_type(cptr, sptr, chptr, type,
				  ":%s %s %s :%s",
				  parv[0], cmd, nick,
				  parv[2]);
	    }
#ifdef ANTI_SPAMBOT_EXTRA
	  if( MyConnect(sptr) && spambot_privmsg_count &&
	      ((sptr->person_privmsgs - sptr->channel_privmsgs)
	       > spambot_privmsg_count) )
	    {
	      sendto_realops_lev(REJ_LEV,
"Possible spambot %s [%s@%s] : privmsgs to clients %d privmsgs to channels %d",
			     sptr->name, sptr->user->username,
			     sptr->user->host,
			     sptr->person_privmsgs,sptr->channel_privmsgs);
	      /* and report it if happens again */
	      sptr->person_privmsgs = 0;
	      sptr->channel_privmsgs = 0;
	    }
	}
      else
	{
	  sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		     me.name, parv[0], nick);
	  return -1;
	}
#endif
      return 0;
    }

  /*
  ** plain old channel msg ?
  */
  if (IsPerson(sptr) && (chptr = find_channel(nick, NullChn)))
    {
#ifdef	IDLE_CHECK
      /* reset idle time for message only if target exists */
      if(MyClient(sptr) && sptr->user)
	sptr->user->last = timeofday;
#endif
#ifdef ANTI_SPAMBOT_EXTRA
      if(MyConnect(sptr) && !IsElined(sptr))
	sptr->channel_privmsgs++;
#endif
#ifdef FLUD
      if(!notice)
	if(check_for_ctcp(parv[2]))
	    check_for_flud(sptr, NULL, chptr, 1);
#endif /* FLUD */

      if (can_send(sptr, chptr) == 0)
	sendto_channel_butone(cptr, sptr, chptr,
			      ":%s %s %s :%s",
			      parv[0], cmd, nick,
			      parv[2]);
      else if (!notice)
	sendto_one(sptr, err_str(ERR_CANNOTSENDTOCHAN),
		   me.name, parv[0], nick);
#ifdef ANTI_SPAMBOT_EXTRA
      if( MyConnect(sptr) && spambot_privmsg_count &&
	  ((sptr->person_privmsgs - sptr->channel_privmsgs)
	   > spambot_privmsg_count) )
	{
	  sendto_realops_lev(REJ_LEV,
"Possible spambot %s [%s@%s] : privmsgs to clients %d privmsgs to channels %d",
			 sptr->name, sptr->user->username,
			 sptr->user->host,
			 sptr->person_privmsgs,sptr->channel_privmsgs);
	  /* and report it if happens again */
	  sptr->person_privmsgs = 0;
	  sptr->channel_privmsgs = 0;
	}
#endif
      return 0;
    }

  /* Everything below here should be reserved for opers 
   * as pointed out by Mortiis, user%host.name@server.name 
   * syntax could be used to flood without FLUD protection
   * its also a delightful way for non-opers to find users who
   * have changed nicks -Dianora
   */

  if(!IsAnOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		 me.name, parv[0], nick);
      return -1;
    }
	
  /*
  ** the following two cases allow masks in NOTICEs
  ** (for OPERs only)
  **
  ** Armin, 8Jun90 (gruner@informatik.tu-muenchen.de)
  */
  if ((*nick == '$' || *nick == '#'))
    {
      if (!(s = (char *)strrchr(nick, '.')))
	{
	  sendto_one(sptr, err_str(ERR_NOTOPLEVEL),
		     me.name, parv[0], nick);
	  return 0;
	}
      while (*++s)
	if (*s == '.' || *s == '*' || *s == '?')
	  break;
      if (*s == '*' || *s == '?')
	{
	  sendto_one(sptr, err_str(ERR_WILDTOPLEVEL),
		     me.name, parv[0], nick);
	  return 0;
	}
      sendto_match_butone(IsServer(cptr) ? cptr : NULL, 
			  sptr, nick + 1,
			  (*nick == '#') ? MATCH_HOST :
			  MATCH_SERVER,
			  ":%s %s %s :%s", parv[0],
			  cmd, nick, parv[2]);
      return 0;
    }
	
  /*
  ** user[%host]@server addressed?
  */
  if ((server = (char *)strchr(nick, '@')) &&
      (acptr = find_server(server + 1, NULL)))
    {
      int count = 0;
	
      /*
      ** Not destined for a user on me :-(
      */
      if (!IsMe(acptr))
	{
	  sendto_one(acptr,":%s %s %s :%s", parv[0],
		     cmd, nick, parv[2]);
	  return 0;
	}
      *server = '\0';
	
      if ((host = (char *)strchr(nick, '%')))
	*host++ = '\0';

      /*
      ** Look for users which match the destination host
      ** (no host == wildcard) and if one and one only is
      ** found connected to me, deliver message!
      */
      acptr = find_userhost(nick, host, NULL, &count);
      if (server)
	*server = '@';
      if (host)
	*--host = '%';
      if (acptr)
	{
	  if (count == 1)
	    sendto_prefix_one(acptr, sptr,
			      ":%s %s %s :%s",
			      parv[0], cmd,
			      nick, parv[2]);
	  else if (!notice)
	    sendto_one(sptr,
		       err_str(ERR_TOOMANYTARGETS),
		       me.name, parv[0], nick);
	}
      if (acptr)
	  return 0;
    }
  sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name,
	     parv[0], nick);

  return 0;
}

/*
** m_private
**	parv[0] = sender prefix
**	parv[1] = receiver list
**	parv[2] = message text
*/

int	m_private(aClient *cptr,
		  aClient *sptr,
		  int parc,
		  char *parv[])
{
  return m_message(cptr, sptr, parc, parv, 0);
}

/*
** m_notice
**	parv[0] = sender prefix
**	parv[1] = receiver list
**	parv[2] = notice text
*/

int	m_notice(aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{
  return m_message(cptr, sptr, parc, parv, 1);
}

static	void	do_who(aClient *sptr,
		       aClient *acptr,
		       aChannel *repchan,
		       Link *lp)
{
  char	status[5];
  /* Using a pointer will compile faster than an index */
  char *p = status;

  if (acptr->user->away)
    *p++ = 'G';
  else
    *p++ = 'H';
  if (IsAnOper(acptr))
    *p++ = '*';
  if ((repchan != NULL) && (lp == NULL))
    lp = find_user_link(repchan->members, acptr);
  if (lp != NULL)
    {
      if (lp->flags & CHFL_CHANOP)
	*p++ = '@';
      else if (lp->flags & CHFL_VOICE)
	*p++ = '+';
    }
  *p = '\0';
  sendto_one(sptr, rpl_str(RPL_WHOREPLY), me.name, sptr->name,
	     (repchan) ? (repchan->chname) : "*", acptr->user->username,
	     acptr->user->host, acptr->user->server, acptr->name,
	     status, acptr->hopcount, acptr->info);
}

/*
** m_who
**	parv[0] = sender prefix
**	parv[1] = nickname mask list
**	parv[2] = additional selection flag, only 'o' for now.
*/
int	m_who(aClient *cptr,
	      aClient *sptr,
	      int parc,
	      char *parv[])
{
  Reg	aClient *acptr;
  Reg	char	*mask = parc > 1 ? parv[1] : NULL;
  Reg	Link	*lp;
  aChannel *chptr;
  aChannel *mychannel = NULL;
  char	*channame = NULL;
  int	oper = parc > 2 ? (*parv[2] == 'o' ): 0; /* Show OPERS only */
  int	member;
  int	maxmatches = 500;

  mychannel = NullChn;
  if (sptr->user)
    if ((lp = sptr->user->channel))
      mychannel = lp->value.chptr;

  /* Allow use of m_who without registering */
  /* Not anymore...- Comstud */
  /* taken care of in parse.c now - Dianora */
     /*  if (check_registered_user(sptr))
    return 0;
    */

  /*
  **  Following code is some ugly hacking to preserve the
  **  functions of the old implementation. (Also, people
  **  will complain when they try to use masks like "12tes*"
  **  and get people on channel 12 ;) --msa
  */
  if (mask)
    (void)collapse(mask);
  if (!mask || (*mask == (char) 0))
    {
      sendto_one(sptr, rpl_str(RPL_ENDOFWHO), me.name, parv[0],
		 BadPtr(mask) ?  "*" : mask);
      return 0;
    }
  else if ((*(mask+1) == (char) 0) && (*mask == '*'))
    {
      if (!mychannel)
	{
	  sendto_one(sptr, rpl_str(RPL_ENDOFWHO), me.name, parv[0],
		     BadPtr(mask) ?  "*" : mask);
	  return 0;
	}
      channame = mychannel->chname;
    }
  else
    channame = mask;

  if (IsChannelName(channame))
    {
      /*
       * List all users on a given channel
       */
      chptr = find_channel(channame, NULL);
      if (chptr)
	{
	  member = IsMember(sptr, chptr);
	  if (member || !SecretChannel(chptr))
	    for (lp = chptr->members; lp; lp = lp->next)
	      {
		if (oper && !IsAnOper(lp->value.cptr))
		  continue;
		if (IsInvisible(lp->value.cptr) && !member)
		  continue;
		do_who(sptr, lp->value.cptr, chptr, lp);
	      }
	}
    }
  else if (mask && 
	   ((acptr = find_client(mask, NULL)) != NULL) &&
	   IsPerson(acptr) && (!oper || IsAnOper(acptr)))
    {
      int isinvis = 0;
      aChannel *ch2ptr = NULL;

      isinvis = IsInvisible(acptr);
      for (lp = acptr->user->channel; lp; lp = lp->next)
	{
	  chptr = lp->value.chptr;
	  member = IsMember(sptr, chptr);
	  if (isinvis && !member)
	    continue;
	  if (member || (!isinvis && PubChannel(chptr)))
	    {
	      ch2ptr = chptr;
	      break;
	    }
	}
      do_who(sptr, acptr, ch2ptr, NULL);
    }
  else for (acptr = client; acptr; acptr = acptr->next)
    {
      aChannel *ch2ptr = NULL;
      int	showperson, isinvis;

      if (!IsPerson(acptr))
	continue;
      if (oper && !IsAnOper(acptr))
	continue;
      
      showperson = 0;
      /*
       * Show user if they are on the same channel, or not
       * invisible and on a non secret channel (if any).
       * Do this before brute force match on all relevant fields
       * since these are less cpu intensive (I hope :-) and should
       * provide better/more shortcuts - avalon
       */
      isinvis = IsInvisible(acptr);
      for (lp = acptr->user->channel; lp; lp = lp->next)
	{
	  chptr = lp->value.chptr;
	  member = IsMember(sptr, chptr);
	  if (isinvis && !member)
	    continue;
	  if (member || (!isinvis && PubChannel(chptr)))
	    {
	      ch2ptr = chptr;
	      showperson = 1;
	      break;
	    }
	  if (HiddenChannel(chptr) && !SecretChannel(chptr) &&
	      !isinvis)
	    showperson = 1;
	}
      if (!acptr->user->channel && !isinvis)
	showperson = 1;
      if (showperson &&
	  (!mask ||
	   !matches(mask, acptr->name) ||
	   !matches(mask, acptr->user->username) ||
	   !matches(mask, acptr->user->host) ||
	   !matches(mask, acptr->user->server) ||
	   !matches(mask, acptr->info)))
	{
	  do_who(sptr, acptr, ch2ptr, NULL);
	  if (!--maxmatches)
	    {
	      sendto_one(sptr, rpl_str(RPL_ENDOFWHO), me.name, parv[0],
			 BadPtr(mask) ?  "*" : mask);
	      return 0;
	    }
	}
    }

  sendto_one(sptr, rpl_str(RPL_ENDOFWHO), me.name, parv[0],
	     BadPtr(mask) ?  "*" : mask);
  return 0;
}

/*
** m_whois
**	parv[0] = sender prefix
**	parv[1] = nickname masklist
*/
int	m_whois(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  static anUser UnknownUser =
  {
    NULL,	/* next */
    NULL,   	/* channel */
    NULL,   	/* invited */
    NULL,	/* away */
    0,		/* last */
    1,      	/* refcount */
    0,		/* joined */
    "<Unknown>",	/* user */
    "<Unknown>",	/* host */
    "<Unknown>"		/* server */
  };
  Reg	Link	*lp;
  Reg	anUser	*user;
  aClient *acptr, *a2cptr;
  aChannel *chptr;
  char	*nick, *tmp, *name;
  char	*p = NULL;
  int	found, len, mlen;
  static time_t last_used=0L;

  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NONICKNAMEGIVEN),
		 me.name, parv[0]);
      return 0;
    }

  if(parc > 2)
    {
      if (hunt_server(cptr,sptr,":%s WHOIS %s :%s", 1,parc,parv) !=
	  HUNTED_ISME)
	return 0;
      parv[1] = parv[2];
    }

  if(!IsAnOper(sptr) && !MyConnect(sptr)) /* pace non local requests */
    {
      if((last_used + WHOIS_WAIT) > NOW)
        {
	  /* Unfortunately, returning anything to a non local
	   * request =might= increase sendq to be usable in a split hack
	   * Sorry gang ;-( - Dianora
	   */
          return 0;
        }
      else
        {
          last_used = NOW;
        }
    }

  /* Multiple whois from remote hosts, can be used
   * to flood a server off. One could argue that multiple whois on
   * local server could remain. Lets think about that, for now
   * removing it totally. 
   * -Dianora 
   */

  /*  for (tmp = parv[1]; (nick = strtoken(&p, tmp, ",")); tmp = NULL) */
  nick = parv[1];
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';

    {
      int	invis, showperson, member, wilds;
      found = 0;
      (void)collapse(nick);
      wilds = (strchr(nick, '?') || strchr(nick, '*'));
      /*
      ** We're no longer allowing remote users to generate
      ** requests with wildcards.
      */
      if (!MyConnect(sptr) && wilds)
	return 0;
      /*	continue; */

      /* If the nick doesn't have any wild cards in it,
       * then just pick it up from the hash table
       * - Dianora 
       */

      if(!wilds)
	{
	  acptr = hash_find_client(nick,(aClient *)NULL);
	  if(!acptr)
	    {
	      sendto_one(sptr, err_str(ERR_NOSUCHNICK),
			 me.name, parv[0], nick);
	      return 0;
	      /*	      continue; */
	    }
	  if(!IsPerson(acptr))
	    {
	      sendto_one(sptr, rpl_str(RPL_ENDOFWHOIS),
			 me.name, parv[0], parv[1]);
	      return 0;
	    }
	    /*	    continue; */

          user = acptr->user ? acptr->user : &UnknownUser;
	  name = (!*acptr->name) ? "?" : acptr->name;
	  invis = IsInvisible(acptr);
	  member = (user->channel) ? 1 : 0;

	  a2cptr = find_server(user->server, NULL);
	  
	  sendto_one(sptr, rpl_str(RPL_WHOISUSER), me.name,
		     parv[0], name,
		     user->username, user->host, acptr->info);

	  mlen = strlen(me.name) + strlen(parv[0]) + 6 +
	    strlen(name);
	  for (len = 0, *buf = '\0', lp = user->channel; lp;
	       lp = lp->next)
	    {
	      chptr = lp->value.chptr;
	      if (ShowChannel(sptr, chptr))
		{
		  if (len + strlen(chptr->chname)
		      > (size_t) BUFSIZE - 4 - mlen)
		    {
		      sendto_one(sptr,
				 ":%s %d %s %s :%s",
				 me.name,
				 RPL_WHOISCHANNELS,
				 parv[0], name, buf);
		      *buf = '\0';
		      len = 0;
		    }
		  if (is_chan_op(acptr, chptr))
		    *(buf + len++) = '@';
		  else if (has_voice(acptr, chptr))
		    *(buf + len++) = '+';
		  if (len)
		    *(buf + len) = '\0';
		  (void)strcpy(buf + len, chptr->chname);
		  len += strlen(chptr->chname);
		  (void)strcat(buf + len, " ");
		  len++;
		}
	    }
	  if (buf[0] != '\0')
	    sendto_one(sptr, rpl_str(RPL_WHOISCHANNELS),
		       me.name, parv[0], name, buf);
	  
	  sendto_one(sptr, rpl_str(RPL_WHOISSERVER),
		     me.name, parv[0], name, user->server,
		     a2cptr?a2cptr->info:"*Not On This Net*");

	  if (user->away)
	    sendto_one(sptr, rpl_str(RPL_AWAY), me.name,
		       parv[0], name, user->away);

	  if (IsAnOper(acptr))
	    sendto_one(sptr, rpl_str(RPL_WHOISOPERATOR),
		       me.name, parv[0], name);
#ifdef WHOIS_NOTICE
	  if ((MyOper(acptr)) && ((acptr)->flags & FLAGS_SPY) &&
	      (MyConnect(sptr)) && (IsPerson(sptr)) && (acptr != sptr))
	    sendto_one(acptr,
		       ":%s NOTICE %s :*** Notice -- %s (%s@%s) is doing a /whois on you.",
		       me.name, acptr->name, parv[0], sptr->user->username,
		       sptr->user->host);
#endif /* #ifdef WHOIS_NOTICE */


	  if (acptr->user && MyConnect(acptr))
	    sendto_one(sptr, rpl_str(RPL_WHOISIDLE),
		       me.name, parv[0], name,
		       timeofday - user->last,
		       acptr->firsttime);
	  sendto_one(sptr, rpl_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);
	  return 0;
	  /*	  continue; */
	}

      /* wild is true so here we go */

      for (acptr = client; (acptr = next_client(acptr, nick));
	   acptr = acptr->next)
	{
	  if (IsServer(acptr))
	    continue;
	  /*
	   * I'm always last :-) and acptr->next == NULL!!
	   */
	  if (IsMe(acptr))
	    break;
	  /*
	   * 'Rules' established for sending a WHOIS reply:
	   *
	   *
	   * - if wildcards are being used dont send a reply if
	   *   the querier isnt any common channels and the
	   *   client in question is invisible and wildcards are
	   *   in use (allow exact matches only);
	   *
	   * - only send replies about common or public channels
	   *   the target user(s) are on;
	   */

/* If its an unregistered client, ignore it, it can
   be "seen" on a /trace anyway  -Dianora */

          if(!IsRegistered(acptr))
	    continue;

          user = acptr->user ? acptr->user : &UnknownUser;
	  name = (!*acptr->name) ? "?" : acptr->name;
	  invis = IsInvisible(acptr);
	  member = (user->channel) ? 1 : 0;
	  showperson = (wilds && !invis && !member) || !wilds;
	  for (lp = user->channel; lp; lp = lp->next)
	    {
	      chptr = lp->value.chptr;
	      member = IsMember(sptr, chptr);
	      if (invis && !member)
		continue;
	      if (member || (!invis && PubChannel(chptr)))
		{
		  showperson = 1;
		  break;
		}
	      if (!invis && HiddenChannel(chptr) &&
		  !SecretChannel(chptr))
		showperson = 1;
	    }
	  if (!showperson)
	    continue;
	  
	  a2cptr = find_server(user->server, NULL);
	  
	  sendto_one(sptr, rpl_str(RPL_WHOISUSER), me.name,
		     parv[0], name,
		     user->username, user->host, acptr->info);
	  found = 1;
	  mlen = strlen(me.name) + strlen(parv[0]) + 6 +
	    strlen(name);
	  for (len = 0, *buf = '\0', lp = user->channel; lp;
	       lp = lp->next)
	    {
	      chptr = lp->value.chptr;
	      if (ShowChannel(sptr, chptr))
		{
		  if (len + strlen(chptr->chname)
		      > (size_t) BUFSIZE - 4 - mlen)
		    {
		      sendto_one(sptr,
				 ":%s %d %s %s :%s",
				 me.name,
				 RPL_WHOISCHANNELS,
				 parv[0], name, buf);
		      *buf = '\0';
		      len = 0;
		    }
		  if (is_chan_op(acptr, chptr))
		    *(buf + len++) = '@';
		  else if (has_voice(acptr, chptr))
		    *(buf + len++) = '+';
		  if (len)
		    *(buf + len) = '\0';
		  (void)strcpy(buf + len, chptr->chname);
		  len += strlen(chptr->chname);
		  (void)strcat(buf + len, " ");
		  len++;
		}
	    }
	  if (buf[0] != '\0')
	    sendto_one(sptr, rpl_str(RPL_WHOISCHANNELS),
		       me.name, parv[0], name, buf);
	  
	  sendto_one(sptr, rpl_str(RPL_WHOISSERVER),
		     me.name, parv[0], name, user->server,
		     a2cptr?a2cptr->info:"*Not On This Net*");

	  if (user->away)
	    sendto_one(sptr, rpl_str(RPL_AWAY), me.name,
		       parv[0], name, user->away);

	  if (IsAnOper(acptr))
	    sendto_one(sptr, rpl_str(RPL_WHOISOPERATOR),
		       me.name, parv[0], name);
#ifdef WHOIS_NOTICE
	  if ((MyOper(acptr)) && ((acptr)->flags & FLAGS_SPY) &&
	      (MyConnect(sptr)) && (IsPerson(sptr)) && (acptr != sptr))
	    sendto_one(acptr,
		       ":%s NOTICE %s :*** Notice -- %s (%s@%s) is doing a /whois on you.",
		       me.name, acptr->name, parv[0], sptr->user->username,
		       sptr->user->host);
#endif /* #ifdef WHOIS_NOTICE */


	  if (acptr->user && MyConnect(acptr))
	    sendto_one(sptr, rpl_str(RPL_WHOISIDLE),
		       me.name, parv[0], name,
		       timeofday - user->last,
		       acptr->firsttime);
	}
      if (!found)
	sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		   me.name, parv[0], nick);
      /*
      if (p)
	p[-1] = ',';
	*/
    }
  sendto_one(sptr, rpl_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);
  
  return 0;
}


/*
** m_user
**	parv[0] = sender prefix
**	parv[1] = username (login name, account)
**	parv[2] = client host name (used only from other servers)
**	parv[3] = server host name (used only from other servers)
**	parv[4] = users real name info
*/
int	m_user(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
#define	UFLAGS	(FLAGS_INVISIBLE|FLAGS_WALLOP|FLAGS_SERVNOTICE)
  char	*username, *host, *server, *realname;
 
  if (parc > 2 && (username = (char *)strchr(parv[1],'@')))
    *username = '\0'; 
  if (parc < 5 || *parv[1] == '\0' || *parv[2] == '\0' ||
      *parv[3] == '\0' || *parv[4] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "USER");
      if (IsServer(cptr))
	sendto_realops("bad USER param count for %s from %s",
		       parv[0], get_client_name(cptr, FALSE));
      else
	return 0;
    }

  /* Copy parameters into better documenting variables */

  username = (parc < 2 || BadPtr(parv[1])) ? "<bad-boy>" : parv[1];
  host     = (parc < 3 || BadPtr(parv[2])) ? "<nohost>" : parv[2];
  server   = (parc < 4 || BadPtr(parv[3])) ? "<noserver>" : parv[3];
  realname = (parc < 5 || BadPtr(parv[4])) ? "<bad-realname>" : parv[4];
  
  return do_user(parv[0], cptr, sptr, username, host, server, realname);
}


/*
** do_user
*/
static int do_user(char *nick,
		aClient *cptr,
		aClient *sptr,
		char *username,
		char *host,
		char *server,
		char *realname)
{
  anUser	*user;

  long oflags;

  user = make_user(sptr);
  oflags = sptr->flags;
/* changed the goto into if-else...   -Taner */
/* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ GOOD FOR YOU Taner!!! - Dianora */

  if (!MyConnect(sptr))
    {
      user->server = find_or_add(server);
      strncpyzt(user->host, host, sizeof(user->host));
    }
  else
    {
      if (!IsUnknown(sptr))
	{
	  sendto_one(sptr, err_str(ERR_ALREADYREGISTRED),
		     me.name, nick);
	  return 0;
	}
#ifndef	NO_DEFAULT_INVISIBLE
      sptr->flags |= FLAGS_INVISIBLE;
#endif
      sptr->flags |= (UFLAGS & atoi(host));
      if (!(oflags & FLAGS_INVISIBLE) && IsInvisible(sptr))
	Count.invisi++;
      strncpyzt(user->host, host, sizeof(user->host));
      user->server = me.name;
    }
  strncpyzt(sptr->info, realname, sizeof(sptr->info));

  if (sptr->name[0]) /* NICK already received, now I have USER... */
    return register_user(cptr, sptr, sptr->name, username);
  else
    strncpyzt(sptr->user->username, username, USERLEN+1);
  return 0;
}

/*
** m_quit
**	parv[0] = sender prefix
**	parv[1] = comment
*/
int	m_quit(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  register char *comment = (parc > 1 && parv[1]) ? parv[1] : cptr->name;

/*  What is this doing here?
 *   -Taner
 */
/*
  if (MyClient(sptr))
  if (!strncmp("Local Kill", comment, 10) ||
  !strncmp(comment, "Killed", 6))
  comment = parv[0];
*/
  sptr->flags |= FLAGS_NORMALEX;
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

#if defined(ANTI_SPAM_EXIT_MESSAGE) || defined(ANTI_SPAMBOT_EXTRA)
  if(!IsServer(sptr))
    {
#ifdef ANTI_SPAMBOT_EXTRA
      /* catch the "one shot" spambots, they appear
       * to message a client, then exit. We can't do anything
       * to them since they are now exiting, but at least
       * we know about them now
       * -Dianora
       */
      if( MyConnect(sptr))
	{
	  if((sptr->person_privmsgs > 4) && !sptr->channel_privmsgs)
	    {
	      sendto_realops_lev(REJ_LEV,
"Possible spambot exiting %s [%s@%s] [%s] : privmsgs to clients %d, privmsgs to channels %d",
			     sptr->name, sptr->user->username,
			     sptr->user->host, 
			     comment,
			     sptr->person_privmsgs,sptr->channel_privmsgs);
	    }

	  /* If its a client that has joined at least one channel
	   * but not messaged anyone or a channel at all.. it might
	   * be trying to exit with a spam message.
	   */

	  if(sptr->last_join_time && !IsElined(sptr) &&
	     !(sptr->person_privmsgs | sptr->channel_privmsgs))
	    {
	      sendto_realops_lev(REJ_LEV,
				 "Possible spambot exiting %s [%s@%s] [%s]",
				 sptr->name, sptr->user->username,
				 sptr->user->host,
				 comment);
	    }
	}
#endif
#ifdef ANTI_SPAM_EXIT_MESSAGE
      if(MyConnect(sptr) && 
	 (sptr->firsttime + ANTI_SPAM_EXIT_MESSAGE_TIME) > NOW)
	comment = "Client Quit";
#endif
    }
#endif
  return IsServer(sptr) ? 0 : exit_client(cptr, sptr, sptr, comment);
}

/*
** m_kill
**	parv[0] = sender prefix
**	parv[1] = kill victim
**	parv[2] = kill path
*/
int	m_kill(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aClient *acptr;
  char	*inpath = get_client_name(cptr,FALSE);
  char	*user, *path, *killer;
  int	chasing = 0;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "KILL");
      return 0;
    }

  user = parv[1];
  path = parv[2]; /* Either defined or NULL (parc >= 2!!) */

#ifdef	OPER_KILL
  if (!IsPrivileged(cptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
#else
  if (!IsServer(cptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
#endif
  if (IsAnOper(sptr))
    {
      if(MyClient(sptr) && !IsSetOperK(sptr))
	{
	  sendto_one(sptr,":%s NOTICE %s :You have no K flag",me.name,parv[0]);
	  return 0;
	}
    }

  if (IsAnOper(cptr))
    {
      if (!BadPtr(path))
	/*  {
	    sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
	    me.name, parv[0], "KILL");
	    return 0;
	    fix this damn kill parameter nonsense... Mika   }*/
	if (strlen(path) > (size_t) KILLLEN)
	  path[KILLLEN] = '\0';
    }

  if (!(acptr = find_client(user, NULL)))
    {
      /*
      ** If the user has recently changed nick, we automaticly
      ** rewrite the KILL for this new nickname--this keeps
      ** servers in synch when nick change and kill collide
      */
      if (!(acptr = get_history(user, (long)KILLCHASETIMELIMIT)))
	{
	  sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		     me.name, parv[0], user);
	  return 0;
	}
      sendto_one(sptr,":%s NOTICE %s :KILL changed from %s to %s",
		 me.name, parv[0], user, acptr->name);
      chasing = 1;
    }
  if (!MyConnect(acptr) && IsLocOp(cptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  if (IsServer(acptr) || IsMe(acptr))
    {
      sendto_one(sptr, err_str(ERR_CANTKILLSERVER),
		 me.name, parv[0]);
      return 0;
    }

#ifdef	LOCAL_KILL_ONLY
  if (MyOper(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, ":%s NOTICE %s :Nick %s isnt on your server",
		 me.name, parv[0], acptr->name);
      return 0;
    }
#else
  if (MyOper(sptr) && !MyConnect(acptr) && (!IsOperGlobalKill(sptr)))
    {
      sendto_one(sptr, ":%s NOTICE %s :Nick %s isnt on your server",
		 me.name, parv[0], acptr->name);
      return 0;
    }
#endif
  if (!IsServer(cptr))
    {
      /*
      ** The kill originates from this server, initialize path.
      ** (In which case the 'path' may contain user suplied
      ** explanation ...or some nasty comment, sigh... >;-)
      **
      **	...!operhost!oper
      **	...!operhost!oper (comment)
      */
      inpath = cptr->sockhost;
      if (!BadPtr(path))
	{
	  (void)ircsprintf(buf, "%s%s (%s)",
			   cptr->name, IsOper(sptr) ? "" : "(L)", path);
	  path = buf;
	}
      else
	path = cptr->name;
    }
  else if (BadPtr(path))
    path = "*no-path*"; /* Bogus server sending??? */
  /*
  ** Notify all *local* opers about the KILL (this includes the one
  ** originating the kill, if from this server--the special numeric
  ** reply message is not generated anymore).
  **
  ** Note: "acptr->name" is used instead of "user" because we may
  **	 have changed the target because of the nickname change.
  */
  if (IsLocOp(sptr) && !MyConnect(acptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  if (IsAnOper(sptr)) /* send it normally */
    sendto_ops("Received KILL message for %s. From %s Path: %s!%s",
	       acptr->name, parv[0], inpath, path);
  else
    sendto_ops_lev(SKILL_LEV,"Received KILL message for %s. From %s Path: %s!%s",
		   acptr->name, parv[0], inpath, path);

#if defined(USE_SYSLOG) && defined(SYSLOG_KILL)
  if (IsOper(sptr))
    syslog(LOG_INFO,"KILL From %s For %s Path %s!%s",
			parv[0], acptr->name, inpath, path);
#endif
  /*
  ** And pass on the message to other servers. Note, that if KILL
  ** was changed, the message has to be sent to all links, also
  ** back.
  ** Suicide kills are NOT passed on --SRB
  */
  if (!MyConnect(acptr) || !MyConnect(sptr) || !IsAnOper(sptr))
    {
      sendto_serv_butone(cptr, ":%s KILL %s :%s!%s",
			 parv[0], acptr->name, inpath, path);
      if (chasing && IsServer(cptr))
	sendto_one(cptr, ":%s KILL %s :%s!%s",
		   me.name, acptr->name, inpath, path);
      acptr->flags |= FLAGS_KILLED;
    }

  /*
  ** Tell the victim she/he has been zapped, but *only* if
  ** the victim is on current server--no sense in sending the
  ** notification chasing the above kill, it won't get far
  ** anyway (as this user don't exist there any more either)
  */
  if (MyConnect(acptr))
    sendto_prefix_one(acptr, sptr,":%s KILL %s :%s!%s",
		      parv[0], acptr->name, inpath, path);
  /*
  ** Set FLAGS_KILLED. This prevents exit_one_client from sending
  ** the unnecessary QUIT for this. (This flag should never be
  ** set in any other place)
  */
  if (MyConnect(acptr) && MyConnect(sptr) && IsAnOper(sptr))
    (void)ircsprintf(buf2, "Local kill by %s (%s)", sptr->name,
		     BadPtr(parv[2]) ? sptr->name : parv[2]);
  else
    {
      if ((killer = strchr(path, ' ')))
	{
	  while (*killer && *killer != '!')
	    killer--;
	  if (!*killer)
	    killer = path;
	  else
	    killer++;
	}
      else
	killer = path;
      (void)ircsprintf(buf2, "Killed (%s)", killer);
    }
  return exit_client(cptr, acptr, sptr, buf2);
}

/***********************************************************************
 * m_away() - Added 14 Dec 1988 by jto. 
 *            Not currently really working, I don't like this
 *            call at all...
 *
 *            ...trying to make it work. I don't like it either,
 *	      but perhaps it's worth the load it causes to net.
 *	      This requires flooding of the whole net like NICK,
 *	      USER, MODE, etc messages...  --msa
 ***********************************************************************/

/*
** m_away
**	parv[0] = sender prefix
**	parv[1] = away message
*/
int	m_away(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  Reg	char	*away, *awy2 = parv[1];

  /* make sure the user exists */
  if (!(sptr->user))
    {
      sendto_realops_lev(DEBUG_LEV, "Got AWAY from nil user, from %s (%s)\n",cptr->name,sptr->name);
      return 0;
    }

  away = sptr->user->away;

  if (parc < 2 || !*awy2)
    {
      /* Marking as not away */
      
      if (away)
	{
	  MyFree(away);
	  sptr->user->away = NULL;
	}
/* some lamers scripts continually do a /away, hence making a lot of
   unnecessary traffic. *sigh* so... as comstud has done, I've
   commented out this sendto_serv_butone() call -Dianora */
/*      sendto_serv_butone(cptr, ":%s AWAY", parv[0]); */
      if (MyConnect(sptr))
	sendto_one(sptr, rpl_str(RPL_UNAWAY),
		   me.name, parv[0]);
      return 0;
    }

  /* Marking as away */
  
  if (strlen(awy2) > (size_t) TOPICLEN)
    awy2[TOPICLEN] = '\0';
/* some lamers scripts continually do a /away, hence making a lot of
   unnecessary traffic. *sigh* so... as comstud has done, I've
   commented out this sendto_serv_butone() call -Dianora */
/*  sendto_serv_butone(cptr, ":%s AWAY :%s", parv[0], awy2); */

  /* don't use realloc() -Dianora */

  if (away)
    MyFree(away);

  away = (char *)MyMalloc(strlen(awy2)+1);
  strcpy(away,awy2);

  sptr->user->away = away;

  if (MyConnect(sptr))
    sendto_one(sptr, rpl_str(RPL_NOWAWAY), me.name, parv[0]);
  return 0;
}

/*
** m_ping
**	parv[0] = sender prefix
**	parv[1] = origin
**	parv[2] = destination
*/
int	m_ping(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aClient *acptr;
  char	*origin, *destination;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NOORIGIN), me.name, parv[0]);
      return 0;
    }
  origin = parv[1];
  destination = parv[2]; /* Will get NULL or pointer (parc >= 2!!) */

  acptr = find_client(origin, NULL);
  if (!acptr)
    acptr = find_server(origin, NULL);
  if (acptr && acptr != sptr)
    origin = cptr->name;
  if (!BadPtr(destination) && mycmp(destination, me.name) != 0)
    {
      if ((acptr = find_server(destination, NULL)))
	sendto_one(acptr,":%s PING %s :%s", parv[0],
		   origin, destination);
      else
	{
	  sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
		     me.name, parv[0], destination);
	  return 0;
	}
    }
  else
    sendto_one(sptr,":%s PONG %s :%s", me.name,
	       (destination) ? destination : me.name, origin);
  return 0;
}


/*
** m_pong
**	parv[0] = sender prefix
**	parv[1] = origin
**	parv[2] = destination
*/
int	m_pong(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aClient *acptr;
  char	*origin, *destination;

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NOORIGIN), me.name, parv[0]);
      return 0;
    }

  origin = parv[1];
  destination = parv[2];
  cptr->flags &= ~FLAGS_PINGSENT;
  sptr->flags &= ~FLAGS_PINGSENT;

  /* Now attempt to route the PONG, comstud pointed out routable PING
   * is used for SPING.  routable PING should also probably be left in
   *        -Dianora
   * That being the case, we will route, but only for registered clients (a
   * case can be made to allow them only from servers). -Shadowfax
   */
  if (!BadPtr(destination) && mycmp(destination, me.name) != 0
		&& IsRegistered(sptr))
    {
      if ((acptr = find_client(destination, NULL)) ||
	  (acptr = find_server(destination, NULL)))
	sendto_one(acptr,":%s PONG %s %s",
		   parv[0], origin, destination);
      else
	{
	  sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
		     me.name, parv[0], destination);
	  return 0;
	}
    }

#ifdef	DEBUGMODE
  else
    Debug((DEBUG_NOTICE, "PONG: %s %s", origin,
	   destination ? destination : "*"));
#endif
  return 0;
}


/*
** m_oper
**	parv[0] = sender prefix
**	parv[1] = oper name
**	parv[2] = oper password
*/
int	m_oper(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aConfItem *aconf;
  char	*name, *password, *encr;
#ifdef CRYPT_OPER_PASSWORD
  extern	char *crypt();
#endif /* CRYPT_OPER_PASSWORD */
  char *operprivs;
#ifdef OPER_MOTD
  register aMessageFile *opermotd_ptr;
#endif

  name = parc > 1 ? parv[1] : (char *)NULL;
  password = parc > 2 ? parv[2] : (char *)NULL;

  if (!IsServer(cptr) && (BadPtr(name) || BadPtr(password)))
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "OPER");
      return 0;
    }
	
  /* if message arrived from server, trust it, and set to oper */
  
  if ((IsServer(cptr) || IsMe(cptr)) && !IsOper(sptr))
    {
      sptr->flags |= FLAGS_OPER;
      Count.oper++;
      sendto_serv_butone(cptr, ":%s MODE %s :+o", parv[0], parv[0]);
      if (IsMe(cptr))
	sendto_one(sptr, rpl_str(RPL_YOUREOPER),
		   me.name, parv[0]);
      return 0;
    }
  else if (IsAnOper(sptr))
    {
      if (MyConnect(sptr))
	{
	  sendto_one(sptr, rpl_str(RPL_YOUREOPER),
		     me.name, parv[0]);
#ifdef OPER_MOTD
	  (void)send_oper_motd(sptr, sptr, 1, parv,opermotd);
#endif
	}
      return 0;
    }
  if (!(aconf = find_conf_exact(name, sptr->username, sptr->sockhost,
				CONF_OPS)) &&
      !(aconf = find_conf_exact(name, sptr->username,
				cptr->hostip, CONF_OPS)))
    {
      sendto_one(sptr, err_str(ERR_NOOPERHOST), me.name, parv[0]);
#if defined(FAILED_OPER_NOTICE) && defined(SHOW_FAILED_OPER_ID)
#ifdef SHOW_FAILED_OPER_PASSWD
      sendto_realops("Failed OPER attempt [%s(%s)] - identity mismatch: %s [%s@%s]",
      	name, password, sptr->name, sptr->user->username, sptr->user->host);
#else
      sendto_realops("Failed OPER attempt - host mismatch by %s (%s@%s)",
		     parv[0], sptr->user->username, sptr->sockhost);
#endif /* SHOW_FAILED_OPER_PASSWD */
#endif /* FAILED_OPER_NOTICE && SHOW_FAILED_OPER_ID */
      return 0;
    }
#ifdef CRYPT_OPER_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL pointer. Head it off at the pass... */
  if (password && *aconf->passwd)
    encr = crypt(password, aconf->passwd);
  else
    encr = "";
#else
  encr = password;
#endif  /* CRYPT_OPER_PASSWORD */

  if ((aconf->status & CONF_OPS) &&
      StrEq(encr, aconf->passwd) && !attach_conf(sptr, aconf))
    {
      int old = (sptr->flags & ALL_UMODES);
      char *s;
      
      s = strchr(aconf->host, '@');
      if(s == (char *)NULL)
        {
          sendto_one(sptr, err_str(ERR_NOOPERHOST), me.name, parv[0]);
          sendto_realops("corrupt aconf->host = [%s]",aconf->host);
          return 0;
        }
      *s++ = '\0';
#ifdef	OPER_REMOTE
      if (aconf->status == CONF_LOCOP)
#else
	if ((matches(s,me.name) && !IsLocal(sptr)) ||
	    aconf->status == CONF_LOCOP)
#endif
	  {
	    SetLocOp(sptr);
	    sptr->flags |= (LOCOP_UMODES);
	  }
	else
	  {
	    SetOper(sptr);
	    sptr->flags |= (OPER_UMODES);
	  }
      Count.oper++;
      *--s =  '@';
      SetElined(cptr);
      
      /* LINKLIST */  
      /* add to oper link list -Dianora */
      cptr->next_oper_client = oper_cptr_list;
      oper_cptr_list = cptr;

      if(cptr->confs)
	{
	  aConfItem *aconf;
	  aconf = cptr->confs->value.aconf;
	  operprivs = oper_privs(cptr,aconf->port);
	}
      else
	operprivs = "";

#ifdef WINTRHAWK
      sendto_ops("%s (%s@%s) has just acquired the personality of a petty megalomaniacal tyrant [IRC(%c)p]", parv[0],
#else
      sendto_ops("%s (%s@%s) is now operator (%c)", parv[0],
#endif /* WINTRHAWK */
		 sptr->user->username, sptr->sockhost,
		 IsOper(sptr) ? 'O' : 'o');
      send_umode_out(cptr, sptr, old);
      sendto_one(sptr, rpl_str(RPL_YOUREOPER), me.name, parv[0]);
      sendto_one(sptr, ":%s NOTICE %s:*** Oper privs are %s",me.name,parv[0],
		 operprivs);

#ifdef OPER_MOTD
      (void)send_oper_motd(sptr, sptr, 1, parv,opermotd);
#endif

#if !defined(CRYPT_OPER_PASSWORD) && (defined(FNAME_OPERLOG) ||\
    (defined(USE_SYSLOG) && defined(SYSLOG_OPER)))
	encr = "";
#endif
#if defined(USE_SYSLOG) && defined(SYSLOG_OPER)
	syslog(LOG_INFO, "OPER (%s) (%s) by (%s!%s@%s)",
	       name, encr,
	       parv[0], sptr->user->username, sptr->sockhost);
#endif
#ifdef FNAME_OPERLOG
	{
	  int     logfile;

	  /*
	   * This conditional makes the logfile active only after
	   * it's been created - thus logging can be turned off by
	   * removing the file.
	   *
	   * stop NFS hangs...most systems should be able to open a
	   * file in 3 seconds. -avalon (curtesy of wumpus)
	   */
	  /* You are =nuts= if you run NFS at all on a modern EFnet server
	   * ewwwwwwwww so I am commenting it out for now
	   * it should all be deleted, but ya. there is history here :-)
	   * pjg,wumpus,avalon. avalon cannot spell "courtesy" either.
	   * -Dianora
	   */
	  /*	  (void)alarm(3); */

	  if (IsPerson(sptr) &&
	      (logfile = open(FNAME_OPERLOG, O_WRONLY|O_APPEND)) != -1)
	    {
	      /* (void)alarm(0); */
	      (void)ircsprintf(buf, "%s OPER (%s) (%s) by (%s!%s@%s)\n",
			       myctime(timeofday), name, encr,
			       parv[0], sptr->user->username,
			       sptr->sockhost);
	      /* (void)alarm(3); */
	      (void)write(logfile, buf, strlen(buf));
	      /* (void)alarm(0); */
	      (void)close(logfile);
	    }
	  /* (void)alarm(0); */
	  /* Modification by pjg */
	}
#endif
    }
  else
    {
      (void)detach_conf(sptr, aconf);
      sendto_one(sptr,err_str(ERR_PASSWDMISMATCH),me.name, parv[0]);
#ifdef FAILED_OPER_NOTICE
#ifdef SHOW_FAILED_OPER_PASSWD
      sendto_realops("Failed OPER attempt [%s(%s)] - passwd mismatch: %s [%s@%s]",
      	name, password, sptr->name, sptr->user->username, sptr->user->host);
#else
      sendto_realops("Failed OPER attempt by %s (%s@%s)",
		     parv[0], sptr->user->username, sptr->sockhost);
#endif /* SHOW_FAILED_OPER_PASSWD */
#endif
    }
  return 0;
}

/***************************************************************************
 * m_pass() - Added Sat, 4 March 1989
 ***************************************************************************/

/*
** m_pass
**	parv[0] = sender prefix
**	parv[1] = password
**	parv[2] = optional extra version information
*/
int	m_pass(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  char *password = parc > 1 ? parv[1] : NULL;

  if (BadPtr(password))
    {
      sendto_one(cptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "PASS");
      return 0;
    }
  if (!MyConnect(sptr) || (!IsUnknown(cptr) && !IsHandshake(cptr)))
    {
      sendto_one(cptr, err_str(ERR_ALREADYREGISTRED),
		 me.name, parv[0]);
      return 0;
    }
  strncpyzt(cptr->passwd, password, sizeof(cptr->passwd));
  if (parc > 2)
    {
      /* It looks to me as if orabidoo wanted to have more
       * than one set of option strings possible here...
       * i.e. ":AABBTS" as long as TS was the last two chars
       * however, as we are now using CAPAB, I think we can
       * safely assume if there is a ":TS" then its a TS server
       * -Dianora
       */

      /*      int l = strlen(parv[2]);
      
      if (l < 2)
	return 0; */

      /*      if (strcmp(parv[2]+l-2, "TS") == 0) */

      if(parv[2][0] == 'T' && parv[2][1] == 'S' && !parv[2][2])
	cptr->tsinfo = (ts_val)TS_DOESTS;
    }
  return 0;
}

/*
 * m_userhost added by Darren Reed 13/8/91 to aid clients and reduce
 * the need for complicated requests like WHOIS. It returns user/host
 * information only (no spurious AWAY labels or channels).
 */
int	m_userhost(aClient *cptr,
		   aClient *sptr,
		   int parc,
		   char *parv[])
{
  char	*p = NULL;
  aClient	*acptr;
  Reg	char	*s;
  Reg	int	i, len;

  if (parc > 2)
    (void)m_userhost(cptr, sptr, parc-1, parv+1);

  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "USERHOST");
      return 0;
    }

  (void)ircsprintf(buf, rpl_str(RPL_USERHOST), me.name, parv[0]);
  len = strlen(buf);
  *buf2 = '\0';

  for (i = 5, s = strtoken(&p, parv[1], " "); i && s;
       s = strtoken(&p, (char *)NULL, " "), i--)
    if ((acptr = find_person(s, NULL)))
      {
	if (*buf2)
	  (void)strcat(buf, " ");
	(void)ircsprintf(buf2, "%s%s=%c%s@%s",
			 acptr->name,
			 IsAnOper(acptr) ? "*" : "",
			 (acptr->user->away) ? '-' : '+',
			 acptr->user->username,
			 acptr->user->host);
	
	(void)strncat(buf, buf2, sizeof(buf) - len);
	len += strlen(buf2);
      }
  sendto_one(sptr, "%s", buf);
  return 0;
}

/*
 * m_usrip added by Jon Lusky 5/10/97 to track spoofers
 *
 * modified to not show real IP of opers to non-opers, to help protect
 * them against DOS attacks. As of this date, there is a lot
 * more work to do, to make this useful. (sep 2 1997)
 * -Dianora
 */    
int     m_usrip(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{                  
  char    *p = NULL;
  aClient *acptr;
  Reg     char    *s;
  Reg     int     i, len; 
  
  if (parc > 2)
    (void)m_usrip(cptr, sptr, parc-1, parv+1);
  
  if (parc < 2)   
    {                   
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "IP"); 
      return 0;  
    }
        
  (void)ircsprintf(buf, rpl_str(RPL_USERHOST), me.name, parv[0]);
  len = strlen(buf);
  *buf2 = '\0';   
    
  for (i = 5, s = strtoken(&p, parv[1], " "); i && s;
       s = strtoken(&p, (char *)NULL, " "), i--)
    if ((acptr = find_person(s, NULL)) && (MyConnect(acptr)))
      {
	if (*buf2)
	  (void)strcat(buf, " ");

	if(IsAnOper(sptr))
	  {
	    (void)ircsprintf(buf2, "%s%s=%c%s@%s",
			     acptr->name,
			     IsAnOper(acptr) ? "*" : "",
			     (acptr->user->away) ? '-' : '+',
			     acptr->user->username,
			     acptr->hostip);
	  }
	else
	  {
	    if(IsAnOper(acptr))
	       (void)ircsprintf(buf2, "%s%s=%c%s@127.0.0.1",
				acptr->name,
				IsAnOper(acptr) ? "*" : "",
				(acptr->user->away) ? '-' : '+',
				acptr->user->username);
	    else
	      (void)ircsprintf(buf2, "%s%s=%c%s@%s",
			       acptr->name,
			       IsAnOper(acptr) ? "*" : "",
			       (acptr->user->away) ? '-' : '+',
			       acptr->user->username,
			       acptr->hostip);
	  }

	(void)strncat(buf, buf2, sizeof(buf) - len);
	len += strlen(buf2);
      }
  sendto_one(sptr, "%s", buf);
  return 0;
}

/*
 * m_ison added by Darren Reed 13/8/91 to act as an efficent user indicator
 * with respect to cpu/bandwidth used. Implemented for NOTIFY feature in
 * clients. Designed to reduce number of whois requests. Can process
 * nicknames in batches as long as the maximum buffer length.
 *
 * format:
 * ISON :nicklist
 */
/*
 * Take care of potential nasty buffer overflow problem 
 * -Dianora
 *
 */


int	m_ison(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  Reg	aClient *acptr;
  Reg	char	*s, **pav = parv;
  char	*p = (char *)NULL;
  Reg	int len;
  Reg   int len2;

  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "ISON");
      return 0;
    }

  (void)ircsprintf(buf, rpl_str(RPL_ISON), me.name, *parv);
  len = strlen(buf);
  if (!IsOper(cptr))
    cptr->priority +=20; /* this keeps it from moving to 'busy' list */
  for (s = strtoken(&p, *++pav, " "); s; s = strtoken(&p, (char *)NULL, " "))
    if ((acptr = find_person(s, NULL)))
      {
	len2 = strlen(acptr->name);
	if((len + len2 + 5) < sizeof(buf)) /* make sure can never overflow  */
	  {				   /* allow for extra ' ','\0' etc. */
	    (void)strcat(buf, acptr->name);
	    len += len2;
	    (void)strcat(buf, " ");
	    len++;
	  }
	else
	  break;
      }
  sendto_one(sptr, "%s", buf);
  return 0;
}

/*
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int	m_umode(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  Reg	int	flag;
  Reg	int	*s;
  Reg	char	**p, *m;
  aClient *acptr;
  int	what, setflags;
  int   badflag = NO;	/* Only send one bad flag notice -Dianora */

  what = MODE_ADD;

  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "MODE");
      return 0;
    }

  if (!(acptr = find_person(parv[1], NULL)))
    {
      if (MyConnect(sptr))
	sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL),
		   me.name, parv[0], parv[1]);
      return 0;
    }

  if (IsServer(sptr) || sptr != acptr || acptr->from != sptr->from)
    {
      if (IsServer(cptr))
	sendto_ops_butone(NULL, &me,
			  ":%s WALLOPS :MODE for User %s From %s!%s",
			  me.name, parv[1],
			  get_client_name(cptr, FALSE), sptr->name);
      else
	sendto_one(sptr, err_str(ERR_USERSDONTMATCH),
		   me.name, parv[0]);
      return 0;
    }
 
  if (parc < 3)
    {
      m = buf;
      *m++ = '+';
      for (s = user_modes; (flag = *s) && (m - buf < BUFSIZE - 4);
	   s += 2)
	if (sptr->flags & flag)
	  *m++ = (char)(*(s+1));
      *m = '\0';
      sendto_one(sptr, rpl_str(RPL_UMODEIS),
		 me.name, parv[0], buf);
      return 0;
    }

  /* find flags already set for user */
  setflags = 0;
  for (s = user_modes; (flag = *s); s += 2)
    if (sptr->flags & flag)
      setflags |= flag;
  
  /*
   * parse mode change string(s)
   */
  for (p = &parv[2]; p && *p; p++ )
    for (m = *p; *m; m++)
      switch(*m)
	{
	case '+' :
	  what = MODE_ADD;
	  break;
	case '-' :
	  what = MODE_DEL;
	  break;	
	  /* we may not get these,
	   * but they shouldnt be in default
	   */
	case ' ' :
	case '\n' :
	case '\r' :
	case '\t' :
	  break;
	default :
	  for (s = user_modes; (flag = *s); s += 2)
	    if (*m == (char)(*(s+1)))
	      {
		if (what == MODE_ADD)
		  sptr->flags |= flag;
		else
		  sptr->flags &= ~flag;	
		break;
	      }
	  if (flag == 0 && MyConnect(sptr))
	    badflag = YES;
	  break;
	}

  if(badflag)
    sendto_one(sptr,
	       err_str(ERR_UMODEUNKNOWNFLAG),
	       me.name, parv[0]);

  /*
   * stop users making themselves operators too easily
   */
  if (!(setflags & FLAGS_OPER) && IsOper(sptr) && !IsServer(cptr))
    ClearOper(sptr);

  if (!(setflags & FLAGS_LOCOP) && IsLocOp(sptr) && !IsServer(cptr))
    sptr->flags &= ~FLAGS_LOCOP;

  if ((setflags & (FLAGS_OPER|FLAGS_LOCOP)) && !IsAnOper(sptr) &&
      MyConnect(sptr))
    det_confs_butmask(sptr, CONF_CLIENT & ~CONF_OPS);

  if (!(setflags & (FLAGS_OPER|FLAGS_LOCOP)) && IsAnOper(sptr))
    {
      Count.oper++;
    }

  if ((sptr->flags & FLAGS_NCHANGE) && !IsSetOperN(sptr))
    {
      sendto_one(sptr,":%s NOTICE %s :*** You need oper and N flag for +n",
		 me.name,parv[0]);
      sptr->flags &= ~FLAGS_NCHANGE; /* only tcm's really need this */
    }

  if ((setflags & (FLAGS_OPER|FLAGS_LOCOP)) && !IsAnOper(sptr))
    {
      Count.oper--;

      if (MyConnect(sptr))
        {
          aClient *prev_cptr = (aClient *)NULL;
          aClient *cur_cptr = oper_cptr_list;

	  sptr->flags2 &= ~(FLAGS2_OPER_GLOBAL_KILL|
			    FLAGS2_OPER_REMOTE|
			    FLAGS2_OPER_UNKLINE|
			    FLAGS2_OPER_GLINE|
			    FLAGS2_OPER_N|
			    FLAGS2_OPER_K);
          while(cur_cptr)
            {
              if(sptr == cur_cptr) 
                {
                  if(prev_cptr)
                    prev_cptr->next_oper_client = cur_cptr->next_oper_client;
                  else
                    oper_cptr_list = cur_cptr->next_oper_client;
                  cur_cptr->next_oper_client = (aClient *)NULL;
                  break;
                }
	      else
		prev_cptr = cur_cptr;
              cur_cptr = cur_cptr->next_oper_client;
            }
        }
    }

  if (!(setflags & FLAGS_INVISIBLE) && IsInvisible(sptr))
    Count.invisi++;
  if ((setflags & FLAGS_INVISIBLE) && !IsInvisible(sptr))
    Count.invisi--;
  /*
   * compare new flags with old flags and send string which
   * will cause servers to update correctly.
   */
  send_umode_out(cptr, sptr, setflags);

  return 0;
}
	
/*
 * send the MODE string for user (user) to connection cptr
 * -avalon
 */
void	send_umode(aClient *cptr,
		   aClient *sptr,
		   int old,
		   int sendmask,
		   char *umode_buf)
{
  Reg	int	*s, flag;
  Reg	char	*m;
  int	what = MODE_NULL;

  /*
   * build a string in umode_buf to represent the change in the user's
   * mode between the new (sptr->flag) and 'old'.
   */
  m = umode_buf;
  *m = '\0';
  for (s = user_modes; (flag = *s); s += 2)
    {
      if (MyClient(sptr) && !(flag & sendmask))
	continue;
      if ((flag & old) && !(sptr->flags & flag))
	{
	  if (what == MODE_DEL)
	    *m++ = *(s+1);
	  else
	    {
	      what = MODE_DEL;
	      *m++ = '-';
	      *m++ = *(s+1);
	    }
	}
      else if (!(flag & old) && (sptr->flags & flag))
	{
	  if (what == MODE_ADD)
	    *m++ = *(s+1);
	  else
	    {
	      what = MODE_ADD;
	      *m++ = '+';
	      *m++ = *(s+1);
	    }
	}
    }
  *m = '\0';
  if (*umode_buf && cptr)
    sendto_one(cptr, ":%s MODE %s :%s",
	       sptr->name, sptr->name, umode_buf);
}

/*
 * added Sat Jul 25 07:30:42 EST 1992
 */
/*
 * extra argument evenTS added to send to TS servers or not -orabidoo
 *
 * extra argument evenTS no longer needed with TS only th+hybrid
 * server -Dianora
 */
void	send_umode_out(aClient *cptr,
		       aClient *sptr,
		       int old)
{
  Reg    aClient *acptr;

  send_umode(NULL, sptr, old, SEND_UMODES, buf);

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      if((acptr != cptr) && (acptr != sptr) && (*buf))
	{
	  sendto_one(acptr, ":%s MODE %s :%s",
		   sptr->name, sptr->name, buf);
	}
    }

  if (cptr && MyClient(cptr))
    send_umode(cptr, sptr, old, ALL_UMODES, buf);
}


/**
 ** botreject(host)
 **   Reject a bot based on a fake hostname...
 **           -Taner
 **/
int botreject(char *host)
{

/*
 * Eggdrop Bots:	"USER foo 1 1 :foo"
 * Vlad, Com, joh Bots:	"USER foo null null :foo"
 * Annoy/OJNKbots:	"user foo . . :foo"   (disabled)
 * Spambots that are based on OJNK: "user foo x x :foo"
 */
#undef CHECK_FOR_ANNOYOJNK
  if (!strcmp(host,"1")) return 1;
  if (!strcmp(host,"null")) return 2;
  if (!strcmp(host, "x")) return 3;
#ifdef CHECK_FOR_ANNOYOJNK
  if (!strcmp(host,".")) return 4;
#endif
  return 0;
}

/**
 ** botwarn(host, nick, user, realhost)
 **     Warn that a bot MAY be connecting (added by ThemBones)
 **/
int
botwarn(char *host,
	char *nick,
	char *user,
	char *realhost)
{
/*
 * Eggdrop Bots:      "USER foo 1 1 :foo"
 * Vlad, Com, joh Bots:       "USER foo null null :foo"
 * Annoy/OJNKbots:    "user foo . . :foo"   (disabled)
 */
#undef CHECK_FOR_ANNOYOJNK
  if (!strcmp(host,"1"))
    sendto_realops_lev(CCONN_LEV,"Possible Eggdrop: %s (%s@%s) [B-lined]",
		       nick, user, realhost);
  if (!strcmp(host,"null"))
    sendto_realops_lev(CCONN_LEV,"Possible ComBot: %s (%s@%s) [B-lined]",
		       nick, user, realhost);
  if (!strcmp(host,"x"))
    sendto_realops_lev(CCONN_LEV,"Possible SpamBot: %s (%s@%s) [B-lined]",
		       nick, user, realhost);
#ifdef CHECK_FOR_ANNOYOJNK
  if (!strcmp(host,"."))
    sendto_realops_lev(CCONN_LEV,"Possible AnnoyBot: %s (%s@%s) [B-lined]",
		       nick, user, realhost);
#endif

  return 0;
}

/* Shadowfax's FLUD code */

#ifdef FLUD

void announce_fluder(aClient *fluder,	/* fluder, client being fluded */
		     aClient *cptr,
		     aChannel *chptr,	/* channel being fluded */
		     int type)		/* for future use */
{
  char *fludee;

  if(cptr) 
    fludee = cptr->name; 
  else
    fludee = chptr->chname;

  sendto_ops_lev(REJ_LEV, "Flooder %s [%s@%s] on %s target: %s",
		 fluder->name, fluder->user->username, fluder->user->host,
		 fluder->user->server, fludee);
}


/* This is really just a "convenience" function.  I can only keep three or
** four levels of pointer dereferencing straight in my head.  This remove
** an entry in a fluders list.  Use this when working on a fludees list :) */
struct fludbot *remove_fluder_reference(struct fludbot **fluders,
					aClient *fluder)
{
  struct fludbot *current, *prev, *next;

  prev = NULL;
  current = *fluders;
  while(current) { 
    next = current->next;
    if(current->fluder == fluder)
      {
	if(prev)
	  prev->next = next; 
	else
	  *fluders = next;
	
        BlockHeapFree(free_fludbots, current);
      }
    else
      prev = current;
    current = next; 
  }

  return(*fluders);       
}
 
 
/* Another function to unravel my mind. */
Link *remove_fludee_reference(Link **fludees,void *fludee)
{       
  Link *current, *prev, *next; 

  prev = NULL;
  current = *fludees;
  while(current)
    {
      next = current->next;
      if(current->value.cptr == (aClient *)fludee)
	{
	  if(prev)
	    prev->next = next; 
	  else
	    *fludees = next;
 
          BlockHeapFree(free_Links, current);
	}
      else
	prev = current;
      current = next; 
    }

  return(*fludees);       
}


/* This function checks to see if a CTCP message (other than ACTION) is
** contained in the passed string.  This might seem easier than I am doing it,
** but a CTCP message can be changed together, even after a normal message.
** 
** Unfortunately, this makes for a bit of extra processing in the server. */
int check_for_ctcp(char *str)
{
  char *p = str;          
  while((p = strchr(p, 1)) != NULL)
    {
      if(strncasecmp(++p, "ACTION", 6) != 0)
	return 1;
      if((p = strchr(p, 1)) == NULL)
	return 0;
      p++;    
    }       
  return 0;
}

 
int check_for_fludblock(aClient *fluder, /* fluder being fluded */
			aClient *cptr,	 /* client being fluded */
			aChannel *chptr, /* channel being fluded */
			int type)	 /* for future use */
{
  time_t now;
  int blocking;

  /* If it's disabled, we don't need to process all of this */
  if(flud_block == 0)     
    return 0;
 
  /* It's either got to be a client or a channel being fluded */
  if((cptr == NULL) && (chptr == NULL)) 
    return 0;
                
  if(cptr && !MyFludConnect(cptr))
    {
      sendto_ops("check_for_fludblock() called for non-local client");
      return 0;
    }
 
  /* Are we blocking fluds at this moment? */
  time(&now);
  if(cptr)                
    blocking = (cptr->fludblock > (now - flud_block));
  else
    blocking = (chptr->fludblock > (now - flud_block));
  
  return(blocking);
}


int check_for_flud(aClient *fluder,	/* fluder, client being fluded */
		   aClient *cptr,	
		   aChannel *chptr,	/* channel being fluded */
		   int type)		/* for future use */

{               
  time_t now;
  struct fludbot *current, *prev, *next;
  int blocking, count, found;
  Link *newfludee;

  /* If it's disabled, we don't need to process all of this */
  if(flud_block == 0)
    return 0;

  /* It's either got to be a client or a channel being fluded */
  if((cptr == NULL) && (chptr == NULL))
    return 0;
  
  if(cptr && !MyFludConnect(cptr))
    {
      sendto_ops("check_for_flud() called for non-local client");
      return 0;
    }
 
  /* Are we blocking fluds at this moment? */
  time(&now);
  if(cptr)                
    blocking = (cptr->fludblock > (now - flud_block));
  else
    blocking = (chptr->fludblock > (now - flud_block));
        
  /* Collect the Garbage */
  if(!blocking)
    {
      if(cptr) 
	current = cptr->fluders;
      else
	current = chptr->fluders;
      prev = NULL; 
      while(current)
	{
	  next = current->next;
	  if(current->last_msg < (now - flud_time))
	    {
	      if(cptr)
		remove_fludee_reference(&current->fluder->fludees,
					(void *)cptr);
	      else
		remove_fludee_reference(&current->fluder->fludees,
					(void *)chptr);

	      if(prev)
		prev->next = current->next;
	      else
		if(cptr)
		  cptr->fluders = current->next;
		else
		  chptr->fluders = current->next;
              BlockHeapFree(free_fludbots, current);
	    }
	  else
	    prev = current;
	  current = next;
	}
    }

  /* Find or create the structure for the fluder, and update the counter
  ** and last_msg members.  Also make a running total count */
  if(cptr)
    current = cptr->fluders;
  else
    current = chptr->fluders;
  count = found = 0;
  while(current)
    {
      if(current->fluder == fluder) {
	current->last_msg = now;
	current->count++;
	found = 1;
      }
      if(current->first_msg < (now - flud_time))
	count++;
      else    
	count += current->count;
      current = current->next;
    }
  if(!found)
    {
      if((current = BlockHeapALLOC(free_fludbots, struct fludbot)) != NULL)
	{
	  current->fluder = fluder;
	  current->count = 1; 
	  current->first_msg = now;
	  current->last_msg = now;
	  if(cptr)
	    {
	      current->next = cptr->fluders;
	      cptr->fluders = current;
	    }
	  else
	    {
	      current->next = chptr->fluders;
	      chptr->fluders = current;
	    }

	  count++;  

          if((newfludee = BlockHeapALLOC(free_Links, Link)) != NULL)
	    {
	      if(cptr)
		{
		  newfludee->flags = 0;
		  newfludee->value.cptr = cptr;
		}
	      else
		{
		  newfludee->flags = 1;
		  newfludee->value.chptr = chptr;
		}
	      newfludee->next = fluder->fludees;
	      fluder->fludees = newfludee;
	    }
          else
            outofmemory();

	  /* If we are already blocking now, we should go ahead
	  ** and announce the new arrival */
	  if(blocking)
	    announce_fluder(fluder, cptr, chptr, type);
	}       
      else
        outofmemory();
    }                       

  /* Okay, if we are not blocking, we need to decide if it's time to
  ** begin doing so.  We already have a count of messages received in
  ** the last flud_time seconds */
  if(!blocking && (count > flud_num))
    {
      blocking = 1;   
      ircstp->is_flud++;
      
      /* if we are going to say anything to the fludee, now is the
      ** time to mention it to them. */
      if(cptr)
	sendto_one(cptr,     
		   ":%s NOTICE %s :*** Notice -- Server flood protection activated for %s",
		   me.name, cptr->name, cptr->name);
      else            
	sendto_channel_butserv(chptr, &me,
	       ":%s NOTICE %s :*** Notice -- Server flood protection activated for %s",
			       me.name,
			       chptr->chname,
			       chptr->chname);
            
      /* Here we should go back through the existing list of
      ** fluders and announce that they were part of the game as 
      ** well. */
      if(cptr)        
	current = cptr->fluders;
      else
	current = chptr->fluders;
      while(current)
	{
	  announce_fluder(current->fluder, cptr, chptr, type);
	  current = current->next;
	}       
    }       
  
  /* update blocking timestamp, since we received a/another CTCP message */
  if(blocking)
    {
      if(cptr)
	cptr->fludblock = now;
      else
	chptr->fludblock = now;
    }               
 
  return(blocking);
}               

void free_fluders(aClient *cptr, aChannel *chptr)
{      
  struct fludbot *fluders, *next;

  if((cptr == NULL) && (chptr == NULL))
    {
      sendto_ops("free_fluders(NULL, NULL)");
      return;
    }    
 
  if(cptr && !MyFludConnect(cptr))  
    return;
                
  if(cptr)        
    fluders = cptr->fluders;
  else    
    fluders = chptr->fluders;

  while(fluders)
    {
      next = fluders->next;

      if(cptr)
	remove_fludee_reference(&fluders->fluder->fludees, (void *)cptr); 
      else
	remove_fludee_reference(&fluders->fluder->fludees, (void *)chptr);
      
      BlockHeapFree(free_fludbots, fluders);
      fluders = next;
    }    
}


void free_fludees(aClient *badguy)
{
  Link *fludees, *next;
                
  if(badguy == NULL)
    {
      sendto_ops("free_fludees(NULL)");
      return;
    }
  fludees = badguy->fludees;
  while(fludees)
    {
      next = fludees->next;  
      
      if(fludees->flags)
	remove_fluder_reference(&fludees->value.chptr->fluders, badguy);
      else
	{
	  if(!MyFludConnect(fludees->value.cptr))
	    sendto_ops("free_fludees() encountered non-local client");
	  else
	    remove_fluder_reference(&fludees->value.cptr->fluders, badguy);
	}

      BlockHeapFree(free_Links, fludees);
      fludees = next;
    }       
}       

#endif /* FLUD */
