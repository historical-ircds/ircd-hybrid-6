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

#ifdef GLINES
char	*glinefile = GLINEFILE;

/* external variables */
extern int rehashed;		/* defined in ircd.c */
extern int dline_in_progress;	/* defined in ircd.c */

/* internal variables */
static  aConfItem *glines = (aConfItem *)NULL;
static GLINE_PENDING *pending_glines;

/* external functions */
extern int bad_tld(char *);	/* defined in m_kline.c */
extern int safe_write(aClient *,char *,char *,int,char *); /* in m_kline.c */
extern char *small_file_date(time_t);  /* defined in s_misc.c */
extern char *smalldate(time_t);		/* defined in s_misc.c */

/* internal functions */
static void add_gline(aConfItem *);
static void log_gline_request(
			      char *,char *,char *,char *,
			      char *,char *,char *);

static void log_gline(aClient *,char *,GLINE_PENDING *,
		      char *,char *,char *,char *,char *,char *,char *);


static void expire_pending_glines();

static int majority_gline(aClient *,
			  char *,char *,
			  char *,char *,
			  char *,char *,char *); /* defined in s_conf.c */



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
static void log_gline(
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

/*
 * flush_glines
 * 
 * inputs	- NONE
 * output	- NONE
 * side effects	-
 *
 * Get rid of all placed G lines, hopefully to be replaced by gline.log
 * placed k-lines
 */
void flush_glines()
{
  aConfItem *kill_list_ptr;

  if((kill_list_ptr = glines))
    {
      while(kill_list_ptr)
        {
          glines = kill_list_ptr->next;
	  free_conf(kill_list_ptr);
	  kill_list_ptr = glines;
        }
    }
  glines = (aConfItem *)NULL;
}

/* find_gkill
 *
 * inputs	- aClient pointer to a Client struct
 * output	- aConfItem pointer if a gline was found for this client
 * side effects	- none
 */

aConfItem *find_gkill(aClient *cptr)
{
  char *host, *name;
  
  if (!cptr->user)
    return 0;

  host = cptr->sockhost;
  name = cptr->user->username;

  if (strlen(host)  > (size_t) HOSTLEN ||
      (name ? strlen(name) : 0) > (size_t) HOSTLEN)
    return (0);
  
  if(IsElined(cptr))
    return 0;

  return(find_is_glined(host,name));
}

/*
 * find_is_glined
 * inputs	- hostname
 *		- username
 * output	- pointer to aConfItem if user@host glined
 * side effects	-
 *  WARNING, no sanity checking on length of name,host etc.
 * thats expected to be done by caller.... *sigh* -Dianora
 */

aConfItem *find_is_glined(char *host,char *name)
{
  aConfItem *kill_list_ptr;	/* used for the link list only */
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;

  /* gline handling... exactly like temporary klines 
   * I expect this list to be very tiny. (crosses fingers) so CPU
   * time in this, should be minimum.
   * -Dianora
  */

  if(glines)
    {
      kill_list_ptr = last_list_ptr = glines;

      while(kill_list_ptr)
	{
	  if(kill_list_ptr->hold <= NOW)	/* a gline has expired */
	    {
	      if(glines == kill_list_ptr)
		{
		  /* Its pointing to first one in link list*/
		  /* so, bypass this one, remember bad things can happen
		     if you try to use an already freed pointer.. */

		  glines = last_list_ptr = tmp_list_ptr =
		    kill_list_ptr->next;
		}
	      else
		{
		  /* its in the middle of the list, so link around it */
		  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
		}

	      free_conf(kill_list_ptr);
	      kill_list_ptr = tmp_list_ptr;
	    }
	  else
	    {
	      if( (kill_list_ptr->name && (!name || match(kill_list_ptr->name,
                 name))) && (kill_list_ptr->host &&
                   (!host || match(kill_list_ptr->host,host))))
		return(kill_list_ptr);
              last_list_ptr = kill_list_ptr;
              kill_list_ptr = kill_list_ptr->next;
	    }
	}
    }

  return((aConfItem *)NULL);
}

/* report_glines
 *
 * inputs	- aClient pointer
 * output	- NONE
 * side effects	- 
 *
 * report pending glines, and placed glines.
 * 
 * - Dianora		  
 */
void report_glines(aClient *sptr)
{
  GLINE_PENDING *gline_pending_ptr;
  aConfItem *kill_list_ptr;
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;
  char *host;
  char *name;
  char *reason;

  expire_pending_glines();	/* This is not the g line list, but
				   the pending possible g line list */

  if((gline_pending_ptr = pending_glines))
    {
      sendto_one(sptr,":%s NOTICE %s :Pending G-lines", me.name,
			 sptr->name);
      while(gline_pending_ptr)
	{
	  tmptr = localtime(&gline_pending_ptr->time_request1);
	  strftime(timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);

	  sendto_one(sptr,":%s NOTICE %s :1) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
		     me.name,sptr->name,
		     gline_pending_ptr->oper_nick1,
		     gline_pending_ptr->oper_user1,
		     gline_pending_ptr->oper_host1,
		     gline_pending_ptr->oper_server1,
		     timebuffer,
		     gline_pending_ptr->user,
		     gline_pending_ptr->host,
		     gline_pending_ptr->reason1);

	  if(gline_pending_ptr->oper_nick2[0])
	    {
	      tmptr = localtime(&gline_pending_ptr->time_request2);
	      strftime(timebuffer, MAX_DATE_STRING, "%Y/%m/%d %H:%M:%S", tmptr);
	      sendto_one(sptr,
	      ":%s NOTICE %s :2) %s!%s@%s on %s requested gline at %s for %s@%s [%s]",
			 me.name,sptr->name,
			 gline_pending_ptr->oper_nick2,
			 gline_pending_ptr->oper_user2,
			 gline_pending_ptr->oper_host2,
			 gline_pending_ptr->oper_server2,
			 timebuffer,
			 gline_pending_ptr->user,
			 gline_pending_ptr->host,
			 gline_pending_ptr->reason2);
	    }
	  gline_pending_ptr = gline_pending_ptr->next;
	}
      sendto_one(sptr,":%s NOTICE %s :End of Pending G-lines", me.name,
		 sptr->name);
    }

  if(glines)
    {
      kill_list_ptr = last_list_ptr = glines;

      while(kill_list_ptr)
        {
	  if(kill_list_ptr->hold <= NOW)	/* gline has expired */
	    {
	      if(glines == kill_list_ptr)
		{
		  glines = last_list_ptr = tmp_list_ptr =
		    kill_list_ptr->next;
		}
	      else
		{
		  /* its in the middle of the list, so link around it */
		  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
		}

	      free_conf(kill_list_ptr);
	      kill_list_ptr = tmp_list_ptr;
	    }
	  else
	    {
	      if(kill_list_ptr->host)
		host = kill_list_ptr->host;
	      else
		host = "*";

	      if(kill_list_ptr->name)
		name = kill_list_ptr->name;
	      else
		name = "*";

	      if(kill_list_ptr->passwd)
		reason = kill_list_ptr->passwd;
	      else
		reason = "No Reason";

	      sendto_one(sptr,rpl_str(RPL_STATSKLINE), me.name,
			 sptr->name, 'G' , host, name, reason);

	      last_list_ptr = kill_list_ptr;
	      kill_list_ptr = kill_list_ptr->next;
	    }
        }
    }
}

/*
 * expire_pending_glines
 * 
 * inputs	- NONE
 * output	- NONE
 * side effects	-
 *
 * Go through the pending gline list, expire any that haven't had
 * enough "votes" in the time period allowed
 */

static void expire_pending_glines()
{
  GLINE_PENDING *gline_pending_ptr;
  GLINE_PENDING *last_gline_pending_ptr;
  GLINE_PENDING *tmp_pending_ptr;

  if(pending_glines == (GLINE_PENDING *)NULL)
    return;

  last_gline_pending_ptr = (GLINE_PENDING *)NULL;
  gline_pending_ptr = pending_glines;

  while(gline_pending_ptr)
    {
      if( (gline_pending_ptr->last_gline_time + GLINE_PENDING_EXPIRE) <= NOW )
	{
	  if(last_gline_pending_ptr)
	    last_gline_pending_ptr->next = gline_pending_ptr->next;
	  else
	    pending_glines = gline_pending_ptr->next;

	  tmp_pending_ptr = gline_pending_ptr;
	  gline_pending_ptr = gline_pending_ptr->next;
	  MyFree(tmp_pending_ptr->reason1);
	  MyFree(tmp_pending_ptr->reason2);
	  MyFree(tmp_pending_ptr);
	}
      else
	{
	  last_gline_pending_ptr = gline_pending_ptr;
	  gline_pending_ptr = gline_pending_ptr->next;
	}
    }
}

/*
 * majority_gline()
 *
 * inputs	- oper_nick, oper_user, oper_host, oper_server
 *		  user,host reason
 *
 * output	- YES if there are 3 different opers on 3 different servers
 *		  agreeing to this gline, NO if there are not.
 * Side effects	-
 *	See if there is a majority agreement on a GLINE on the given user
 *	There must be at least 3 different opers agreeing on this GLINE
 *
 *	Expire old entries.
 */

static int majority_gline(aClient *sptr,
			  char *oper_nick,
			  char *oper_user,
			  char *oper_host,
			  char *oper_server,
			  char *user,
			  char *host,
			  char *reason)
{
  GLINE_PENDING *new_pending_gline;
  GLINE_PENDING *gline_pending_ptr;

  /* special case condition where there are no pending glines */

  if(pending_glines == (GLINE_PENDING *)NULL) /* first gline request placed */
    {
      new_pending_gline = (GLINE_PENDING *)malloc(sizeof(GLINE_PENDING));
      if(new_pending_gline == (GLINE_PENDING *)NULL)
	{
	  sendto_realops("No memory for GLINE, GLINE dropped");
	  return NO;
	}

      memset((void *)new_pending_gline,0,sizeof(GLINE_PENDING));

      strncpyzt(new_pending_gline->oper_nick1,oper_nick,NICKLEN+1);
      new_pending_gline->oper_nick2[0] = '\0';

      strncpyzt(new_pending_gline->oper_user1,oper_user,USERLEN);
      new_pending_gline->oper_user2[0] = '\0';

      strncpyzt(new_pending_gline->oper_host1,oper_host,HOSTLEN);
      new_pending_gline->oper_host2[0] = '\0';

      new_pending_gline->oper_server1 = find_or_add(oper_server);

      strncpyzt(new_pending_gline->user,user,USERLEN);
      strncpyzt(new_pending_gline->host,host,HOSTLEN);
      new_pending_gline->reason1 = strdup(reason);
      new_pending_gline->reason2 = (char *)NULL;

      new_pending_gline->next = (GLINE_PENDING *)NULL;
      new_pending_gline->last_gline_time = NOW;
      new_pending_gline->time_request1 = NOW;
      pending_glines = new_pending_gline;
      return NO;
    }

  expire_pending_glines();

  for(gline_pending_ptr = pending_glines;
      gline_pending_ptr; gline_pending_ptr = gline_pending_ptr->next)
    {
      if( (strcasecmp(gline_pending_ptr->user,user) == 0) &&
	  (strcasecmp(gline_pending_ptr->host,host) ==0 ) )
	{
	  if(((strcasecmp(gline_pending_ptr->oper_user1,oper_user) == 0) &&
	      (strcasecmp(gline_pending_ptr->oper_host1,oper_host) == 0)) ||
	      (strcasecmp(gline_pending_ptr->oper_server1,oper_server) == 0) )
	    {
	      /* This oper or server has already "voted" */
	      sendto_realops("oper or server has already voted");
	      return NO;
	    }

	  if(gline_pending_ptr->oper_user2[0] != '\0')
	    {
	      /* if two other opers on two different servers have voted yes */

	      if(((strcasecmp(gline_pending_ptr->oper_user2,oper_user)==0) &&
		  (strcasecmp(gline_pending_ptr->oper_host2,oper_host)==0)) ||
		  (strcasecmp(gline_pending_ptr->oper_server2,oper_server)==0))
		{
		  /* This oper or server has already "voted" */
		  sendto_ops("oper or server has already voted");
		  return NO;
		}

	      if(find_is_glined(host,user))
		return NO;

	      if(find_is_klined(host,user,0L))
		return NO;

	      log_gline(sptr,sptr->name,gline_pending_ptr,
			oper_nick,oper_user,oper_host,oper_server,
			user,host,reason);
	      return YES;
	    }
	  else
	    {
	      strncpyzt(gline_pending_ptr->oper_nick2,oper_nick,NICKLEN+1);
	      strncpyzt(gline_pending_ptr->oper_user2,oper_user,USERLEN);
	      strncpyzt(gline_pending_ptr->oper_host2,oper_host,HOSTLEN);
	      gline_pending_ptr->reason2 = strdup(reason);
	      gline_pending_ptr->oper_server2 = find_or_add(oper_server);
	      gline_pending_ptr->last_gline_time = NOW;
	      gline_pending_ptr->time_request1 = NOW;
	      return NO;
	    }
	}
    }
  /* Didn't find this user@host gline in pending gline list
   * so add it.
   */

  new_pending_gline = (GLINE_PENDING *)malloc(sizeof(GLINE_PENDING));
  if(new_pending_gline == (GLINE_PENDING *)NULL)
    {
      sendto_realops("No memory for GLINE, GLINE dropped");
      return NO;
    }

  memset((void *)new_pending_gline,0,sizeof(GLINE_PENDING));

  strncpyzt(new_pending_gline->oper_nick1,oper_nick,NICKLEN+1);
  new_pending_gline->oper_nick2[0] = '\0';

  strncpyzt(new_pending_gline->oper_user1,oper_user,USERLEN);
  new_pending_gline->oper_user2[0] = '\0';

  strncpyzt(new_pending_gline->oper_host1,oper_host,HOSTLEN);
  new_pending_gline->oper_host2[0] = '\0';

  new_pending_gline->oper_server1 = find_or_add(oper_server);

  strncpyzt(new_pending_gline->user,user,USERLEN);
  strncpyzt(new_pending_gline->host,host,HOSTLEN);
  new_pending_gline->reason1 = strdup(reason);
  new_pending_gline->reason2 = (char *)NULL;

  new_pending_gline->last_gline_time = NOW;
  new_pending_gline->time_request1 = NOW;
  new_pending_gline->next = pending_glines;
  pending_glines = new_pending_gline;
  
  return NO;
}

/* add_gline
 *
 * inputs	- pointer to aConfItem
 * output	- none
 * Side effects	- links in given aConfItem into gline link list
 *
 * Identical to add_temp_kline code really.
 *
 * -Dianora
 */

static void add_gline(aConfItem *aconf)
{
  aconf->next = glines;
  glines = aconf;
}

#endif /* GLINES */
