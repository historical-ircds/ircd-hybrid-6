/************************************************************************
 *   IRC - Internet Relay Chat, src/m_gline.c
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
static char *rcs_version = "$Id$";
#endif

#include "struct.h"

#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#ifndef __EMX__
#include <utmp.h> /* old slackware utmp.h defines BYTE_ORDER */
#endif /* __EMX__ */

#if defined(AIX) || defined(DYNIXPTX) || defined(SVR3)
#include <time.h>
#endif
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
#include "fdlist.h"

#ifdef GLINES
extern int rehashed;
extern int dline_in_progress;	/* defined in ircd.c */
extern int bad_tld(char *);
extern int safe_write(aClient *,char *,char *,int,char *);

extern char *smalldate(time_t);		/* defined in s_misc.c */
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

extern char *small_file_date(time_t);  /* defined in s_misc.c */

#endif

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
/* Allow this server to pass along GLINE if received and
 * GLINES is not defined.
 */

int     m_gline(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  char *oper_name;		/* nick of oper requesting GLINE */
  char *oper_username;		/* username of oper requesting GLINE */
  char *oper_host;		/* hostname of oper requesting GLINE */
  char *oper_server;		/* server of oper requesting GLINE */
  char *user, *host;		/* user and host of GLINE "victim" */
  char *reason;			/* reason for "victims" demise */
#ifdef GLINES
  char buffer[512];
  char *current_date;
  char tempuser[USERLEN+2];
  char temphost[HOSTLEN+1];
  aConfItem *aconf;
#endif

  if(!IsServer(sptr)) /* allow remote opers to apply g lines */
    {
#ifdef GLINES
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

      if (match(user, "akjhfkahfasfjd") &&
                match(host, "ldksjfl.kss...kdjfd.jfklsjf"))
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
#else
      sendto_one(sptr,":%s NOTICE %s :GLINE disabled",me.name,parv[0]);  
#endif
    }
  else
    {
      if(!IsServer(sptr))
        return(0);

      /* Always good to be paranoid about arguments */
      if(parc < 8)
	return 0;

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
#ifdef GLINES
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
#endif  
  return 0;
}


#ifdef GLINES
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
  strftime(timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);

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
  strftime(timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);

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
