/************************************************************************
 *   IRC - Internet Relay Chat, src/send.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *		      University of Oulu, Computing Center
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
static  char sccsid[] = "@(#)send.c	2.32 2/28/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";

static char *rcs_version = "$Id$";
#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "h.h"
#include <stdio.h>
#include "numeric.h"


/* LINKLIST */
extern aClient *local_cptr_list;
extern aClient *oper_cptr_list;
extern aClient *serv_cptr_list;

#define	NEWLINE	"\n"

static	char	sendbuf[2048];
static	int	send_message (aClient *, char *, int);

#ifdef USE_SENTALONG
static	int	sentalong[MAXCONNECTIONS];
#else
static	unsigned long sentalong[MAXCONNECTIONS];
static unsigned long current_serial=0L;
#endif

int format(char *,char *,char *,char *,char *,char *,char *,char *,
	char *,char *,char *,char *,char *,char *);

/*
** dead_link
**	An error has been detected. The link *must* be closed,
**	but *cannot* call ExitClient (m_bye) from here.
**	Instead, mark it with FLAGS_DEADSOCKET. This should
**	generate ExitClient from the main loop.
**
**	If 'notice' is not NULL, it is assumed to be a format
**	for a message to local opers. I can contain only one
**	'%s', which will be replaced by the sockhost field of
**	the failing link.
**
**	Also, the notice is skipped for "uninteresting" cases,
**	like Persons and yet unknown connections...
*/
static	int	dead_link(aClient *to, char *notice)
{
  to->flags |= FLAGS_DEADSOCKET;
  /*
   * If because of BUFFERPOOL problem then clean dbuf's now so that
   * notices don't hurt operators below.
   */
  DBufClear(&to->recvQ);
  DBufClear(&to->sendQ);
  if (!IsPerson(to) && !IsUnknown(to) && !(to->flags & FLAGS_CLOSING))
    sendto_ops(notice, get_client_name(to, FALSE));
  Debug((DEBUG_ERROR, notice, get_client_name(to, FALSE)));
  return -1;
}

/*
** flush_connections
**	Used to empty all output buffers for all connections. Should only
**	be called once per scan of connections. There should be a select in
**	here perhaps but that means either forcing a timeout or doing a poll.
**	When flushing, all we do is empty the obuffer array for each local
**	client and try to send it. if we cant send it, it goes into the sendQ
**	-avalon
*/
void	flush_connections(int fd)
{
#ifdef SENDQ_ALWAYS
  Reg	int	i;
  Reg	aClient *cptr;

  if (fd == me.fd)
    {
      for (i = highest_fd; i >= 0; i--)
	if ((cptr = local[i]) && DBufLength(&cptr->sendQ) > 0)
	  (void)send_queued(cptr);
    }
  else if (fd >= 0 && (cptr = local[fd]) && DBufLength(&cptr->sendQ) > 0)
    (void)send_queued(cptr);
#endif
}


/*
** send_message
**	Internal utility which delivers one message buffer to the
**	socket. Takes care of the error handling and buffering, if
**	needed.
**      if SENDQ_ALWAYS is defined, the message will be queued.
**	if ZIP_LINKS is defined, the message will eventually be compressed,
**	anything stored in the sendQ is compressed.
*/
static	int	send_message(aClient *to, char *msg, int len)
	/* if msg is a null pointer, we are flushing connection */
#ifdef SENDQ_ALWAYS
{
  static int SQinK;

Debug((DEBUG_DEBUG,"send_message() msg = %s", msg));

  if (to->from)
    to = to->from;	/* shouldn't be necessary */

  if (IsMe(to))
    {
      sendto_ops("Trying to send to myself! [%s]", msg);
      return 0;
    }

  if (IsDead(to))
    return 0; /* This socket has already been marked as dead */
  if (DBufLength(&to->sendQ) > get_sendq(to))
    {
      if (IsServer(to))
	sendto_ops("Max SendQ limit exceeded for %s: %d > %d",
		   get_client_name(to, FALSE),
		   DBufLength(&to->sendQ), get_sendq(to));
      if (IsClient(to))
	to->flags |= FLAGS_SENDQEX;
      return dead_link(to, "Max Sendq exceeded");
    }
  else
    {
#ifdef ZIP_LINKS
      /*
      ** data is first stored in to->zip->outbuf until
      ** it's big enough to be compressed and stored in the sendq.
      ** send_queued is then responsible to never let the sendQ
      ** be empty and to->zip->outbuf not empty.
      */
      if (to->flags2 & FLAGS2_ZIP)
	msg = zip_buffer(to, msg, &len, 0);

      if (len && dbuf_put(&to->sendQ, msg, len) < 0)
#else /* ZIP_LINKS */
      if (dbuf_put(&to->sendQ, msg, len) < 0)
#endif /* ZIP_LINKS */
	return dead_link(to, "Buffer allocation error for %s");
    }

  /*
  ** Update statistics. The following is slightly incorrect
  ** because it counts messages even if queued, but bytes
  ** only really sent. Queued bytes get updated in SendQueued.
  */
  to->sendM += 1;
  me.sendM += 1;
  if (to->acpt != &me)
    to->acpt->sendM += 1;
  /*
  ** This little bit is to stop the sendQ from growing too large when
  ** there is no need for it to. Thus we call send_queued() every time
  ** 2k has been added to the queue since the last non-fatal write.
  ** Also stops us from deliberately building a large sendQ and then
  ** trying to flood that link with data (possible during the net
  ** relinking done by servers with a large load).
  */
  /*
   * Well, let's try every 4k for clients, and immediately for servers
   *  -Taner
   */
  SQinK = DBufLength(&to->sendQ)/1024;
  if (IsServer(to))
    {
      if (SQinK > to->lastsq)
	send_queued(to);
    }
  else
    {
      if (SQinK > (to->lastsq + 4))
	send_queued(to);
    }
  return 0;
}
#else
{
  int	rlen = 0;

  if (to->from)
      to = to->from;
  if (IsMe(to))
    {
      sendto_ops("Trying to send to myself! [%s]", msg);
      return 0;
    }
  if (IsDead(to))
		return 0; /* This socket has already been marked as dead */

  /*
  ** DeliverIt can be called only if SendQ is empty...
  */
  if ((DBufLength(&to->sendQ) == 0) &&
      (rlen = deliver_it(to, msg, len)) < 0)
    return dead_link(to,"Write error to %s, closing link");
  else if (rlen < len)
    {
      /*
      ** Was unable to transfer all of the requested data. Queue
      ** up the remainder for some later time...
      */
      if (DBufLength(&to->sendQ) > get_sendq(to))
	{
	  sendto_ops("Max SendQ limit exceeded for %s : %d > %d",
		     get_client_name(to, FALSE),
		     DBufLength(&to->sendQ), get_sendq(to));
	  return dead_link(to, "Max Sendq exceeded");
	}
      else
	{
#ifdef ZIP_LINKS
	  /*
	  ** data is first stored in to->zip->outbuf until
	  ** it's big enough to be compressed and stored in the sendq.
	  ** send_queued is then responsible to never let the sendQ
	  ** be empty and to->zip->outbuf not empty.
	  */
	  if (to->flags2 & FLAGS2_ZIP)
	    msg = zip_buffer(to, msg, &len, 0);

	  if (len && dbuf_put(&to->sendQ,msg+rlen,len-rlen) < 0)
#else /* ZIP_LINKS */
          if (dbuf_put(&to->sendQ,msg+rlen,len-rlen) < 0)
#endif /* ZIP_LINKS */
	      return dead_link(to,"Buffer allocation error for %s");
	}
    }
  /*
  ** Update statistics. The following is slightly incorrect
  ** because it counts messages even if queued, but bytes
  ** only really sent. Queued bytes get updated in SendQueued.
  */
  to->sendM += 1;
  me.sendM += 1;
  if (to->acpt != &me)
    to->acpt->sendM += 1;
  return 0;
}
#endif

/*
** send_queued
**	This function is called from the main select-loop (or whatever)
**	when there is a chance the some output would be possible. This
**	attempts to empty the send queue as far as possible...
*/
int	send_queued(aClient *to)
{
  char	*msg;
  int	len, rlen, more = NO;

  /*
  ** Once socket is marked dead, we cannot start writing to it,
  ** even if the error is removed...
  */
  if (IsDead(to))
    {
      /*
      ** Actually, we should *NEVER* get here--something is
      ** not working correct if send_queued is called for a
      ** dead socket... --msa
      */
#ifndef SENDQ_ALWAYS
      return dead_link(to, "send_queued called for a DEADSOCKET:%s");
#else
      return -1;
#endif
    }

#ifdef ZIP_LINKS
  /*
  ** Here, we must make sure than nothing will be left in to->zip->outbuf
  ** This buffer needs to be compressed and sent if all the sendQ is sent
  */
  if ((to->flags2 & FLAGS2_ZIP) && to->zip->outcount)
    {
      if (DBufLength(&to->sendQ) > 0)
	  more = 1;
      else
	{
	  msg = zip_buffer(to, NULL, &len, 1);
	  
	  if (len == -1)
	     return dead_link(to, "fatal error in zip_buffer()");

	  if (dbuf_put(&to->sendQ, msg, len) < 0)
	    {
	      return dead_link(to, "Buffer allocation error for %s");
	    }
	}
    }
#endif /* ZIP_LINKS */

  while (DBufLength(&to->sendQ) > 0)
    {
      msg = dbuf_map(&to->sendQ, &len);
      /* Returns always len > 0 */
      if ((rlen = deliver_it(to, msg, len)) < 0)
	return dead_link(to,"Write error to %s, closing link");
      (void)dbuf_delete(&to->sendQ, rlen);
      to->lastsq = DBufLength(&to->sendQ)/1024;
      if (rlen < len) 
	/* ..or should I continue until rlen==0? */
	/* no... rlen==0 means the send returned EWOULDBLOCK... */
	break;

#ifdef ZIP_LINKS
      if (DBufLength(&to->sendQ) == 0 && more)
	{
	  /*
	  ** The sendQ is now empty, compress what's left
	  ** uncompressed and try to send it too
	  */
	  more = 0;
	  msg = zip_buffer(to, NULL, &len, 1);

Debug((DEBUG_DEBUG,"sendQ is now empty len = %d",len));      

	  if (len == -1)
	    return dead_link(to, "fatal error in zip_buffer()");
      
	  if (dbuf_put(&to->sendQ, msg, len) < 0)
	    {
	      return dead_link(to, "Buffer allocation error for %s");
	    }
	}
#endif /* ZIP_LINKS */      
    }

  return (IsDead(to)) ? -1 : 0;
}

/*
** send message to single client
*/
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_one(to, pattern, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12)
aClient *to;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8, *p9, *p10, *p11, *p12;
{
#else
void	sendto_one(to, pattern, va_alist)
aClient	*to;
char	*pattern;
va_dcl
{
  va_list	vl;
#endif

  int len; /* used for the length of the current message */

#ifdef	USE_VARARGS
  va_start(vl);
  (void)vsprintf(sendbuf, pattern, vl);
  va_end(vl);
#else
  len=format(sendbuf, pattern, p1, p2, p3, p4, p5, p6,
	     p7, p8, p9, p10, p11, p12);
#endif

  if (to->from)
    to = to->from;
  if (to->fd < 0)
    {
      Debug((DEBUG_ERROR,
	     "Local socket %s with negative fd... AARGH!",
	     to->name));
    }
  else if (IsMe(to))
    {
      sendto_ops("Trying to send [%s] to myself!", sendbuf);
      return;
    }

#ifdef USE_VARARGS
  (void)strcat(sendbuf, NEWLINE);
  sendbuf[511] = '\n';
  sendbuf[512] = '\0';
  len = strlen(sendbuf);
#endif /* use_varargs */
  (void)send_message(to, sendbuf, len);
}

# ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_channel_butone(one, from, chptr, pattern,
			      p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
aChannel *chptr;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
# else
void	sendto_channel_butone(one, from, chptr, pattern, va_alist)
aClient	*one, *from;
aChannel *chptr;
char	*pattern;
va_dcl
{
  va_list	vl;
# endif
  register	Link	*lp;
  register	aClient *acptr;
  register	int	i;
  
# ifdef	USE_VARARGS
  va_start(vl);
# endif

#ifdef USE_SENTALONG
  memset((void *)sentalong, 0, sizeof(sentalong));
#else
  current_serial++;
#endif

  for (lp = chptr->members; lp; lp = lp->next)
    {
      acptr = lp->value.cptr;
      if (acptr->from == one)
	continue;	/* ...was the one I should skip */
      i = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
	{
# ifdef	USE_VARARGS
	  sendto_prefix_one(acptr, from, pattern, vl);
# else
	  sendto_prefix_one(acptr, from, pattern, p1, p2,
			    p3, p4, p5, p6, p7, p8);
# endif
#ifdef USE_SENTALONG
	  sentalong[i] = 1;
#else
	  sentalong[i] = current_serial;
#endif
	}
      else
	{
	  /* Now check whether a message has been sent to this
	   * remote link already */
#ifdef USE_SENTALONG
	  if (sentalong[i] == 0)
#else
          if(sentalong[i] != current_serial)
#endif
	    {
# ifdef	USE_VARARGS
	      sendto_prefix_one(acptr, from, pattern, vl);
# else
	      sendto_prefix_one(acptr, from, pattern,
				p1, p2, p3, p4,
				p5, p6, p7, p8);
# endif
#ifdef USE_SENTALONG
	      sentalong[i] = 1;
#else
	      sentalong[i] = current_serial;
#endif
	    }
	}
    }
# ifdef	USE_VARARGS
  va_end(vl);
# endif
  return;
}

# ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_channel_type(one, from, chptr, type,  pattern,
			      p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
aChannel *chptr;
int type;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
# else
void	sendto_channel_type(one, from, chptr, type, pattern, va_alist)
aClient	*one, *from;
int type;
aChannel *chptr;
char	*pattern;
va_dcl
{
  va_list	vl;
# endif
  register	Link	*lp;
  register	aClient *acptr;
  register	int	i;
  
# ifdef	USE_VARARGS
  va_start(vl);
# endif

#ifdef USE_SENTALONG
  memset((void *)sentalong,0,sizeof(sentalong));
#else
  current_serial++;
#endif

  for (lp = chptr->members; lp; lp = lp->next)
    {
      if (!(lp->flags & type))
	continue;

      acptr = lp->value.cptr;

      i = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
	{
# ifdef	USE_VARARGS
	  sendto_prefix_one(acptr, from, pattern, vl);
# else
	  sendto_prefix_one(acptr, from, pattern, p1, p2,
			    p3, p4, p5, p6, p7, p8);
# endif
#ifdef USE_SENTALONG
	  sentalong[i] = 1;
#else
	  sentalong[i] = current_serial;
#endif
	}
      else
	{
	  /* Now check whether a message has been sent to this
	   * remote link already */
#ifdef USE_SENTALONG
	  if (sentalong[i] == 0)
#else
	  if (sentalong[i] != current_serial)
#endif
	    {
# ifdef	USE_VARARGS
	      sendto_prefix_one(acptr, from, pattern, vl);
# else
	      sendto_prefix_one(acptr, from, pattern,
				p1, p2, p3, p4,
				p5, p6, p7, p8);
# endif
#ifdef USE_SENTALONG
	      sentalong[i] = 1;
#else
	      sentalong[i] = current_serial;
#endif
	    }
	}
    }
# ifdef	USE_VARARGS
  va_end(vl);
# endif
  return;
}

/*
 * sendto_serv_butone
 *
 * Send a message to all connected servers except the client 'one'.
 */
# ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_serv_butone(one, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
# else
void	sendto_serv_butone(one, pattern, va_alist)
aClient	*one;
char	*pattern;
va_dcl
{
  va_list	vl;
# endif
  register	aClient *cptr;

  /* USE_VARARGS IS BROKEN someone volunteer to fix it :-) -Dianora */
# ifdef	USE_VARARGS
  va_start(vl);
# endif

  /* LINKLIST */

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if ((one && cptr == one->from)) 
	continue;
      sendto_one(cptr, pattern, p1, p2, p3, p4, p5, p6, p7, p8);
    }
  return;
}

/*
 * sendto_common_channels()
 *
 * Sends a message to all people (excluding user) on local server who are
 * in same channel with user.
 */
# ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_common_channels(user, pattern, p1, p2, p3, p4,
				p5, p6, p7, p8)
aClient *user;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
# else
void	sendto_common_channels(user, pattern, va_alist)
aClient	*user;
char	*pattern;
va_dcl
{
  va_list	vl;
# endif
  register Link *channels;
  register Link *users;
  register aClient *cptr;
  
# ifdef	USE_VARARGS
  va_start(vl);
# endif

#ifdef USE_SENTALONG
  memset((void *)sentalong,0,sizeof(sentalong));

  if (user->fd >= 0)
    sentalong[user->fd] = 1;
#else
  current_serial++;
  if (user->fd >= 0)
    sentalong[user->fd] = current_serial;
#endif

  if (user->user)
    for (channels=user->user->channel;channels;channels=channels->next)
      for(users=channels->value.chptr->members;users;users=users->next)
	{
	  cptr = users->value.cptr;
#ifdef USE_SENTALONG
	  if (!MyConnect(cptr) || sentalong[cptr->fd])
	    continue;
	  sentalong[cptr->fd]++;
#else
	  if (!MyConnect(cptr) || (sentalong[cptr->fd] == current_serial))
	    continue;
	  sentalong[cptr->fd] = current_serial;
#endif
# ifdef	USE_VARARGS
	  sendto_prefix_one(cptr, user, pattern, vl);
# else
	  sendto_prefix_one(cptr, user, pattern,
			    p1, p2, p3, p4, p5, p6, p7, p8);
# endif
	}
  if (MyConnect(user))
# ifdef	USE_VARARGS
    sendto_prefix_one(user, user, pattern, vl);
  va_end(vl);
# else
  sendto_prefix_one(user, user, pattern, p1, p2, p3, p4,
		    p5, p6, p7, p8);
# endif
  return;
}

/*
 * sendto_cap_serv_butone
 *
 * Send a message to servers other than 'one' which have capability cap
 */
# ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_cap_serv_butone(cap, one, pattern, p1, p2, p3, p4, p5, p6, p7, p8, p9)
int	cap;
aClient *one;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8, *p9;
{
# else
void	sendto_cap_serv_butone(cap, one, pattern, va_alist)
int	cap;
aClient	*one;
char	*pattern;
va_dcl
{
  va_list	vl;
# endif
  Reg	aClient *cptr;

# ifdef	USE_VARARGS
  va_start(vl);
# endif

  /* USE_VARARGS IS BROKEN someone volunteer to fix it :-) -Dianora */

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if ((one && cptr == one->from)) 
	continue;
      if(!IsCapable(cptr,cap))
	continue;
      sendto_one(cptr, pattern, p1, p2, p3, p4, p5, p6, p7, p8);
    }
  return;
}

#ifdef FLUD
# ifndef USE_VARARGS
void    sendto_channel_butlocal(one, from, chptr, pattern,
                              p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
aChannel *chptr;
char    *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{               
# else  
void    sendto_channel_butlocal(one, from, chptr, pattern, va_alist)
aClient *one, *from;
aChannel *chptr;
char    *pattern;
va_dcl
{
  va_list vl;
# endif
  register    Link    *lp;
  register    aClient *acptr;
  register    int     i;
#ifdef USE_SENTALONG
  int	sentalong[MAXCONNECTIONS];
#else
  unsigned long sentalong[MAXCONNECTIONS];
#endif

# ifdef USE_VARARGS
  va_start(vl);
# endif
#ifdef USE_SENTALONG
  memset((void *)sentalong,0,sizeof(sentalong));
#else
  current_serial++;
#endif

  for (lp = chptr->members; lp; lp = lp->next)
    {
      acptr = lp->value.cptr;
      if (acptr->from == one)
	continue;       /* ...was the one I should skip */
      i = acptr->from->fd;
      if (!MyFludConnect(acptr))
	{
	  /* Now check whether a message has been sent to this
	   * remote link already */
#ifdef USE_SENTALONG
	  if (sentalong[i] == 0)
#else
	  if (sentalong[i] != current_serial)
#endif
	    {
# ifdef USE_VARARGS
	      sendto_prefix_one(acptr, from, pattern, vl);
# else
	      sendto_prefix_one(acptr, from, pattern,
				p1, p2, p3, p4,
				p5, p6, p7, p8);
# endif
#ifdef USE_SENTALONG
	      sentalong[i] = 1;
#else
	      sentalong[i] = current_serial;
#endif
	    }
	}
    }
# ifdef USE_VARARGS
  va_end(vl);
# endif
  return;
}
#endif /* FLUD */


/*
 * sendto_channel_butserv
 *
 * Send a message to all members of a channel that are connected to this
 * server.
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_channel_butserv(chptr, from, pattern, p1, p2, p3,
			       p4, p5, p6, p7, p8)
aChannel *chptr;
aClient *from;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void	sendto_channel_butserv(chptr, from, pattern, va_alist)
aChannel *chptr;
aClient *from;
char	*pattern;
va_dcl
{
  va_list	vl;
#endif
  register	Link	*lp;
  register	aClient	*acptr;

#ifdef	USE_VARARGS
  for (va_start(vl), lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      sendto_prefix_one(acptr, from, pattern, vl);
  va_end(vl);
#else
  for (lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      sendto_prefix_one(acptr, from, pattern,
			p1, p2, p3, p4,
			p5, p6, p7, p8);
#endif

  return;
}

/*
** send a msg to all ppl on servers/hosts that match a specified mask
** (used for enhanced PRIVMSGs)
**
** addition -- Armin, 8jun90 (gruner@informatik.tu-muenchen.de)
*/

static	int	match_it(aClient *one, char *mask,int what)
{
  if(what == MATCH_HOST)
    return (matches(mask, one->user->host)==0);
  else
    return (matches(mask, one->user->server)==0);
}

/*
 * sendto_match_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask.
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_match_servs(chptr, from, format, p1,p2,p3,p4,p5,p6,p7,p8,p9)
aChannel *chptr;
aClient	*from;
char	*format, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8, *p9;
{
#else
void	sendto_match_servs(chptr, from, format, va_alist)
aChannel *chptr;
aClient	*from;
char	*format;
va_dcl
{
  va_list	vl;
#endif
  register aClient	*cptr;

#ifdef	USE_VARARGS
  va_start(vl);
#endif

  if (chptr)
    {
      if (*chptr->chname == '&')
	return;
    }

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;
      sendto_one(cptr, format, p1, p2, p3, p4, p5, p6, p7, p8, p9);
    }
  return;
}

/*
 * sendto_match_cap_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask, and match the capability
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_match_cap_servs(chptr, from, cap, format, p1,p2,p3,p4,p5,p6,p7,p8,p9)
aChannel *chptr;
aClient	*from;
int     cap;
char	*format, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8, *p9;
{
#else
void	sendto_match_cap_servs(chptr, from, cap, format, va_alist)
aChannel *chptr;
aClient	*from;
int     cap;
char	*format;
va_dcl
{
  va_list	vl;
#endif
  register aClient	*cptr;

#ifdef	USE_VARARGS
  va_start(vl);
#endif

  /* USE_VARARGS IS BROKEN someone volunteer to fix it :-) -Dianora */

  if (chptr)
    {
      if (*chptr->chname == '&')
	return;
    }

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;
      if(!IsCapable(cptr,cap))
	continue;
      sendto_one(cptr, format, p1, p2, p3, p4, p5, p6, p7, p8, p9);
    }
  return;
}

/*
 * sendto_match_butone
 *
 * Send to all clients which match the mask in a way defined on 'what';
 * either by user hostname or user servername.
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_match_butone(one, from, mask, what, pattern,
			    p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
int	what;
char	*mask, *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void	sendto_match_butone(one, from, mask, what, pattern, va_alist)
aClient *one, *from;
int	what;
char	*mask, *pattern;
va_dcl
{
  va_list	vl;
#endif

  register	aClient *cptr, *acptr;
  
#ifdef	USE_VARARGS
  va_start(vl);
#endif

  /* scan the local clients */
  for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    {
      if (cptr == one)	/* must skip the origin !! */
	continue;
      if (match_it(cptr, mask, what))
	{
#ifdef	USE_VARARGS
	  sendto_prefix_one(cptr, from, pattern, vl);
	  va_end(vl);
#else
	  sendto_prefix_one(cptr, from, pattern,
			    p1, p2, p3, p4, p5, p6, p7, p8);
#endif
	}
    }

  /* Now scan servers */
  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == one)	/* must skip the origin !! */
	continue;
      /*
       * scan all clients, if there is at least one client that
       * matches the mask, and is on this server, send
       * *one* message to the server, and let that server deal
       * with sending it to all the clients that match on that server.
       */

      for (acptr = client; acptr; acptr = acptr->next)
	if (IsRegisteredUser(acptr)
	    && match_it(acptr, mask, what)
	    && acptr->from == cptr)
	  {

	    /* a person on that server matches the mask, so we
	    ** send *one* msg to that server ...
	    */

#ifdef	USE_VARARGS
	    sendto_prefix_one(cptr, from, pattern, vl);
	    va_end(vl);
#else
	    sendto_prefix_one(cptr, from, pattern,
			      p1, p2, p3, p4, p5, p6, p7, p8);
	    break;
	  }
#endif
    }
  return;
}

/*
 * sendto_all_butone.
 *
 * Send a message to all connections except 'one'. The basic wall type
 * message generator.
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_all_butone(one, from, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void	sendto_all_butone(one, from, pattern, va_alist)
aClient *one, *from;
char	*pattern;
va_dcl
{
  va_list	vl;
#endif

  register	aClient *cptr;

#ifdef USE_VARARGS
  va_start(vl);
#endif

  for (cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    {
      if (!IsMe(cptr) && one != cptr)
#ifdef USE_VARARGS
      sendto_prefix_one(cptr, from, pattern, vl);
      va_end(vl);
#else
      sendto_prefix_one(cptr, from, pattern,
			p1, p2, p3, p4, p5, p6, p7, p8);
#endif
    }
  return;
}

/*
 * sendto_ops_lev
 *
 *    Send to *local* ops only at a certain level...
 *    0 = normal +s
 *    1 = client connect/disconnect   (+c) [IRCOPS ONLY]
 *    2 = bot rejection               (+r)
 *    3 = server kills		      (+k)
 */
#ifndef       USE_VARARGS
/*VARARGS*/
void  sendto_ops_lev(lev, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
int   lev;
char  *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void  sendto_ops_lev(lev, pattern, va_alist)
int   lev;
char  *pattern;
va_dcl
{
  va_list vl;
#endif
  register    aClient *cptr;
  char   nbuf[1024];

#ifdef        USE_VARARGS
  va_start(vl);
#endif

  for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
      {
	switch (lev)
	  {
	  case CCONN_LEV:
	    if (!SendCConnNotice(cptr) || !IsAnOper(cptr))
	      continue;
	    break;
	  case REJ_LEV:
	    if (!SendRejNotice(cptr) || !IsAnOper(cptr))
	      continue;
	    break;
	  case SKILL_LEV:
	    if (!SendSkillNotice(cptr))
	      continue;
	    break;
	  case FULL_LEV:
	    if (!SendFullNotice(cptr) || !IsAnOper(cptr))
	      continue;
	    break;
	  case SPY_LEV: 
	    if (!SendSpyNotice(cptr) || !IsAnOper(cptr))
	      continue;
	    break;
	  case DEBUG_LEV:
	    if (!SendDebugNotice(cptr) || !IsAnOper(cptr))
	      continue;
	    break;
	  case NCHANGE_LEV:
	    if (!SendNickChange(cptr) || !IsAnOper(cptr))
	      continue;
	    break;
	  default: /* this is stupid, but oh well */
	    if (!SendServNotice(cptr)) continue;
	  }
	(void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
			 me.name, cptr->name);
	(void)strncat(nbuf, pattern,
		      sizeof(nbuf) - strlen(nbuf));

#ifdef        USE_VARARGS
	sendto_one(cptr, nbuf, va_alist);
#else
	sendto_one(cptr, nbuf, p1, p2, p3, p4, p5, p6, p7, p8);
#endif
      }
  return;
}  /* sendto_ops_lev */

/*
 * sendto_ops
 *
 *	Send to *local* ops only.
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_ops(pattern, p1, p2, p3, p4, p5, p6, p7, p8)
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void	sendto_ops(pattern, va_alist)
char	*pattern;
va_dcl
{
  va_list	vl;
#endif
  register	aClient *cptr;
  char	nbuf[1024];

#ifdef	USE_VARARGS
  va_start(vl);
#endif

  for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    if(SendServNotice(cptr))
      {
	(void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
			 me.name, cptr->name);
	(void)strncat(nbuf, pattern,
		      sizeof(nbuf) - strlen(nbuf));
#ifdef	USE_VARARGS
	sendto_one(cptr, nbuf, va_alist);
#else
	sendto_one(cptr, nbuf, p1, p2, p3, p4, p5, p6, p7, p8);
#endif
      }
  return;
}


/*
** sendto_ops_butone
**	Send message to all operators.
** one - client not to send message to
** from- client which message is from *NEVER* NULL!!
*/
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_ops_butone(one, from, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void	sendto_ops_butone(one, from, pattern, va_alist)
aClient *one, *from;
char	*pattern;
va_dcl
{
  va_list	vl;
#endif
  register	int	i;
  register	aClient *cptr;

#ifdef	USE_VARARGS
  va_start(vl);
#endif

#ifdef USE_SENTALONG
  memset((void *)sentalong,0,sizeof(sentalong));
#else
  current_serial++;
#endif

  for (cptr = client; cptr; cptr = cptr->next)
    {
      if (!SendWallops(cptr))
	continue;
/* we want wallops
   if (MyClient(cptr) && !(IsServer(from) || IsMe(from)))
   continue;
*/
      i = cptr->from->fd;	/* find connection oper is on */

#ifdef USE_SENTALONG
      if (sentalong[i])	/* sent message along it already ? */
	continue;
#else
      if (sentalong[i] == current_serial)
	continue;
#endif
      if (cptr->from == one)
	continue;	/* ...was the one I should skip */
#ifdef USE_SENTALONG
      sentalong[i] = 1;
#else
      sentalong[i] = current_serial;
#endif
# ifdef	USE_VARARGS
      sendto_prefix_one(cptr->from, from, pattern, vl);
    }
  va_end(vl);
# else
  sendto_prefix_one(cptr->from, from, pattern,
		    p1, p2, p3, p4, p5, p6, p7, p8);
    }
# endif
  return;
}

/*
** sendto_wallops_butone
**      Send message to all operators.
** one - client not to send message to
** from- client which message is from *NEVER* NULL!!
*/
#ifndef USE_VARARGS
/*VARARGS*/
void    sendto_wallops_butone(one, from, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
aClient *one, *from;
char    *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void    sendto_wallops_butone(one, from, pattern, va_alist)
aClient *one, *from;
char    *pattern;
va_dcl
{
  va_list vl;
#endif
  register     int     i;
  register     aClient *cptr;

#ifdef  USE_VARARGS
  va_start(vl);
#endif
#ifdef USE_SENTALONG
  memset((void *)sentalong,0,sizeof(sentalong));
#else
  current_serial++;
#endif

  for (cptr = client; cptr; cptr = cptr->next)
    {
      if (!SendWallops(cptr))
	continue;
      if (!(IsServer(from) || IsMe(from)) &&
	  MyClient(cptr) && !IsOper(cptr))
	continue;
      if (MyClient(cptr) && !IsAnOper(cptr) &&
	  !(IsServer(from) || IsMe(from)))
	continue;
      i = cptr->from->fd;     /* find connection oper is on */
#ifdef USE_SENTALONG
      if (sentalong[i])       /* sent message along it already ? */
	continue;
#else
      if (sentalong[i] == current_serial)
	continue;
#endif
      if (cptr->from == one)
	continue;       /* ...was the one I should skip */
#ifdef USE_SENTALONG
      sentalong[i] = 1;
#else
      sentalong[i] = current_serial;
#endif

# ifdef USE_VARARGS
      sendto_prefix_one(cptr->from, from, pattern, vl);
    }
  va_end(vl);
# else
  sendto_prefix_one(cptr->from, from, pattern,
                                  p1, p2, p3, p4, p5, p6, p7, p8);
  }
# endif
  return;
}

/*
** send_operwall -- Send Wallop to All Opers on this server
**
*/

void    send_operwall(aClient *from, char *type_message,char *message)
{
  char sender[NICKLEN+USERLEN+HOSTLEN+5];
  aClient *acptr;
  anUser *user;
  

  if (!from || !message)
    return;
  if (!IsPerson(from))
    return;
  user = from->user;
  (void)strcpy(sender, from->name);
  if (*user->username) 
    {
      (void)strcat(sender, "!");
      (void)strcat(sender, user->username);
    }
  if (*user->host && !MyConnect(from)) 
    {
      (void)strcat(sender, "@");
      (void)strcat(sender, user->host);
    }
  else if (*user->host && MyConnect(from))
    {
      (void)strcat(sender, "@");
      (void)strcat(sender, from->sockhost);
    }

  for(acptr = oper_cptr_list; acptr; acptr = acptr->next_oper_client)
    {
      if (!SendOperwall(acptr))
	continue; /* has to be oper if in this linklist */
      sendto_one(acptr, ":%s WALLOPS :%s - %s", sender, type_message, message);
    }
}

/*
 * to - destination client
 * from - client which message is from
 *
 * NOTE: NEITHER OF THESE SHOULD *EVER* BE NULL!!
 * -avalon
 */
#ifndef	USE_VARARGS
/*VARARGS*/
void	sendto_prefix_one(to, from, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
register	aClient *to;
register	aClient *from;
char	*pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void	sendto_prefix_one(to, from, pattern, va_alist)
register	aClient *to;
register	aClient *from;
char	*pattern;
va_dcl
{
  va_list	vl;
#endif
  static	char	sender[HOSTLEN+NICKLEN+USERLEN+5];
  register	anUser	*user;
  char	*par;
  static	char	temp[1024];
  int	flag = 0;

#ifdef	USE_VARARGS
  va_start(vl);
  par = va_arg(vl, char *);
#else
  par = p1;
#endif

/* Optimize by checking if (from && to) before everything */
  if (to && from)
    {
      if (!MyClient(from) && IsPerson(to) && (to->from == from->from))
	{
	  if (IsServer(from))
	    {
#ifdef	USE_VARARGS
	      (void)ircsprintf(temp, pattern, par, vl);
	      va_end(vl);
#else
	      (void)ircsprintf(temp, pattern, par, p2, p3,
			       p4, p5, p6, p7, p8);
#endif
	      sendto_ops(
		"Send message (%s) to %s[%s] dropped from %s(Fake Dir)",
			 temp,
			 to->name, to->from->name, from->name);
	      return;
	    }
	  sendto_ops("Ghosted: %s[%s@%s] from %s[%s@%s] (%s)",
		     to->name, to->user->username, to->user->host,
		     from->name, from->user->username, from->user->host,
		     to->from->name);
	  sendto_serv_butone(NULL, ":%s KILL %s :%s (%s[%s@%s] Ghosted %s)",
			     me.name, to->name, me.name, to->name,
			     to->user->username, to->user->host, to->from->name);
	  to->flags |= FLAGS_KILLED;
	  (void)exit_client(NULL, to, &me, "Ghosted client");
	  if (IsPerson(from))
	    sendto_one(from, err_str(ERR_GHOSTEDCLIENT),
		       me.name, from->name, to->name, to->user->username,
		       to->user->host, to->from);
	  return;
	}
      if (MyClient(to) && IsPerson(from) && !mycmp(par, from->name))
	{
	  user = from->user;
	  (void)strcpy(sender, from->name);
	  if (user)
	    {
	      if (*user->username)
		{
		  (void)strcat(sender, "!");
		  (void)strcat(sender, user->username);
		}
	      if (*user->host && !MyConnect(from))
		{
		  (void)strcat(sender, "@");
		  (void)strcat(sender, user->host);
		  flag = 1;
		}
	    }
	  /*
	  ** flag is used instead of index(sender, '@') for speed and
	  ** also since username/nick may have had a '@' in them. -avalon
	  */
	  if (!flag && MyConnect(from) && *user->host)
	    {
	      (void)strcat(sender, "@");
	      (void)strcat(sender, from->sockhost);
	    }
	  par = sender;
	}
    } /* if (from && to) */
#ifdef	USE_VARARGS
  sendto_one(to, pattern, par, vl);
  va_end(vl);
#else
  sendto_one(to, pattern, par, p2, p3, p4, p5, p6, p7, p8);
#endif
}

int format(char *outp,char *formp,char *in0p,char *in1p,char *in2p,
	char *in3p,char *in4p,char *in5p,char *in6p,char *in7p,
	char *in8p,char *in9p,char *in10p,char *in11p)
{
/* rp for Reading, wp for Writing, fp for the Format string */
char *inp[12]; /* we could hack this if we know the format of the stack */
register char *rp,*fp,*wp;
register char f;
  register int i=0;

  inp[0]=in0p; inp[1]=in1p; inp[2]=in2p; inp[3]=in3p; inp[4]=in4p;
  inp[5]=in5p; inp[6]=in6p; inp[7]=in7p; inp[8]=in8p; inp[9]=in9p;
  inp[10]=in10p; inp[11]=in11p;

  fp = formp;
  wp = outp;

  rp = inp[i]; /* start with the first input string */
  /* just scan the format string and puke out whatever is necessary
     along the way... */

  while ( (f = *(fp++)) )
    {
      if (f!= '%') *(wp++) = f;
      else
	switch (*(fp++))
	  {
	  case 's': /* put the most common case at the top */
	    if(rp)
	      {
		while(*rp)
		  *wp++ = *rp++;
		*wp = '\0';
	      }
	    else
	      {
		*wp++ = '{'; *wp++ = 'n'; *wp++ = 'u'; *wp++ = 'l';
		*wp++ = 'l'; *wp++ = '}'; *wp++ = '\0';
	      }
	    rp = inp[++i];                  /* get the next parameter */
	    break;
	  case 'c':
	    *wp++ = (char)(int)rp;
	    rp = inp[++i];
	    break;
	  case 'd':
	    {
	      register int myint,quotient;
	      myint = (int)rp;
	      if (myint > 999 || myint < 0)
		{
		  /* don't call ircsprintf here... that's stupid.. */
		  (void)sprintf(outp,formp,in0p,in1p,in2p,in3p,
				in4p,in5p,in6p,in7p,in8p,
				in9p,in10p,in11p);
		  /* *blink */
		  strcat(outp,NEWLINE);
		  outp[511] = '\n'; outp[512]= '\0';
		  return strlen(outp);
		}
	      /* leading 0's are suppressed unlike ircsprintf() - Dianora */
	      if( (quotient=myint/100) )
		{
		  *(wp++) = (char) (quotient + (int) '0');
		  myint %=100;
		  *(wp++) = (char) (myint/10 + (int) '0');
		}
	      else
		{
		  myint %=100;
		  if( (quotient = myint/10) )
		    *(wp++) = (char) (quotient + (int)'0');
		}
	      myint %=10;
	      *(wp++) = (char) ((myint) + (int) '0');
	      
	      rp = inp[++i];
	    }
	  break;
	  case 'u':
	    {
	      register unsigned int myuint;
	      myuint = (unsigned int)rp;

	      if (myuint < 100 || myuint > 999)
		{
		  (void)sprintf(outp,formp,in0p,in1p,in2p,in3p,
				in4p,in5p,in6p,in7p,in8p,
				in9p,in10p,in11p);
		  strcat(outp,NEWLINE);
		  outp[511] = '\n'; outp[512]= '\0';
		  return strlen(outp);
		}

	      *(wp++) = (char) ((myuint / 100) + (unsigned int) '0');
	      myuint %=100;
	      *(wp++) = (char) ((myuint / 10) + (unsigned int) '0');
	      myuint %=10;
	      *(wp++) = (char) ((myuint) + (unsigned int) '0');
	      rp = inp[++i];
	    }
	  break;
	  case '%':
	    *(wp++) = '%';
	    break;
	  default:
	    /* oh shit */
	    /* don't call ircsprintf here... that's stupid.. */
	    (void)sprintf(outp,formp,in0p,in1p,in2p,in3p,
			  in4p,in5p,in6p,in7p,in8p,
			  in9p,in10p,in11p);
	    strcat(outp,NEWLINE);
	    outp[511] = '\n'; outp[512]= '\0';
	    return strlen(outp);
	    break;
	  }
    }
  *(wp++) = '\n';
  *wp = '\0'; /* leaves wp pointing to the terminating NULL in the string */
  {
    register int len;
    if ((len = wp-outp) >= 511) len = 512;
    outp[511] = '\n'; outp[512] = '\0';
    return len;
  }
}

 /*
  * sendto_realops
  *
  *    Send to *local* ops only but NOT +s nonopers.
  */
#ifndef       USE_VARARGS
void  sendto_realops(pattern, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10)
char  *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8, *p9, *p10;
{
#else
void  sendto_realops(pattern, va_alist)
char  *pattern;
va_dcl
{
  va_list vl;
#endif
  register aClient	*cptr;
  char		nbuf[1024];


#ifdef        USE_VARARGS
  va_start(vl);
#endif

  for(cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
    {
      if (SendServNotice(cptr))
	{
	  (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
			   me.name, cptr->name);
	  (void)strncat(nbuf, pattern,
			sizeof(nbuf) - strlen(nbuf));
#ifdef        USE_VARARGS
	  sendto_one(cptr, nbuf, va_alist);
#else
	  sendto_one(cptr, nbuf, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
#endif
	}
    }
#ifdef        USE_VARARGS
  va_end(vl);
#endif
  return;
}

 /*
  * sendto_realops_lev
  *
  *    Send to *local* ops only but NOT +s nonopers at a certain level
  */
#ifndef       USE_VARARGS
void  sendto_realops_lev(lev, pattern, p1, p2, p3, p4, p5, p6, p7, p8)
int	lev;
char  *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void  sendto_realops_lev(lev, pattern, va_alist)
int	lev;
char  *pattern;
va_dcl
{
  va_list vl;
#endif
  register aClient	*cptr;
  char		nbuf[1024];


#ifdef        USE_VARARGS
  va_start(vl);
#endif

  for(cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
    {
      switch (lev)
	{
	case CCONN_LEV:
	  if (!SendCConnNotice(cptr))
	    continue;
	  break;
	case REJ_LEV:
	  if (!SendRejNotice(cptr))
	    continue;
	  break;
	case SKILL_LEV: /* This should not be sent, since this can go to
			   normal people */
	  if (!SendSkillNotice(cptr))
	    continue;
	  break;
	case FULL_LEV:
	  if (!SendFullNotice(cptr))
	    continue;
	  break;
	case SPY_LEV:
	  if (!SendSpyNotice(cptr))
	    continue;
	  break;
	case DEBUG_LEV:
	  if (!SendDebugNotice(cptr))
	    continue;
	  break;
	case NCHANGE_LEV:
	  if (!SendNickChange(cptr))
	    continue;
	  break;
	}
      (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
		       me.name, cptr->name);
      (void)strncat(nbuf, pattern,
		    sizeof(nbuf) - strlen(nbuf));
#ifdef        USE_VARARGS
      sendto_one(cptr, nbuf, va_alist);
#else
      sendto_one(cptr, nbuf, p1, p2, p3, p4, p5, p6, p7, p8);
#endif
    }
#ifdef        USE_VARARGS
  va_end(vl);
#endif
  return;
}


/*
** ts_warn
**      Call sendto_ops, with some flood checking (at most 5 warnings
**      every 5 seconds)
*/
 
#ifndef USE_VARARGS
/*VARARGS*/
void    ts_warn(pattern, p1, p2, p3, p4, p5, p6, p7, p8)
char    *pattern, *p1, *p2, *p3, *p4, *p5, *p6, *p7, *p8;
{
#else
void    ts_warn(pattern, va_alist)
char    *pattern;
va_dcl
{
        va_list vl; 
#endif
        static  ts_val last = 0;
        static  int warnings = 0;
        register ts_val now;
 
#ifdef  USE_VARARGS 
        va_start(vl);
#endif
 
        /*
        ** if we're running with TS_WARNINGS enabled and someone does
        ** something silly like (remotely) connecting a nonTS server,
        ** we'll get a ton of warnings, so we make sure we don't send
        ** more than 5 every 5 seconds.  -orabidoo
        */
	/*
	th+hybrid servers always do TS_WARNINGS -Dianora
	*/
        now = time(NULL);
        if (now - last < 5)
            {
                if (++warnings > 5)
                        return;
            }
        else
            {
                last = now;
                warnings = 0;
            }
 
#ifdef  USE_VARARGS
        sendto_realops(pattern, va_alist);
#else
        sendto_realops(pattern, p1, p2, p3, p4, p5, p6, p7, p8);
#endif
        return;
}
 
