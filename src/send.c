/************************************************************************
 *   IRC - Internet Relay Chat, src/send.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                    University of Oulu, Computing Center
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
#include "send.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "m_commands.h"
#include "numeric.h"
#include "s_bsd.h"
#include "s_serv.h"
#include "s_zip.h"
#include "sprintf_irc.h"
#include "struct.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

#define NEWLINE "\r\n"

static  char    sendbuf[2048];
static  int     send_message (aClient *, char *, int);

static  void vsendto_prefix_one(register aClient *, register aClient *, const char *, va_list);
static  void vsendto_one(aClient *, const char *, va_list);
static  void vsendto_realops(const char *, va_list);

static  unsigned long sentalong[MAXCONNECTIONS];
static unsigned long current_serial=0L;

/*
** dead_link
**      An error has been detected. The link *must* be closed,
**      but *cannot* call ExitClient (m_bye) from here.
**      Instead, mark it with FLAGS_DEADSOCKET. This should
**      generate ExitClient from the main loop.
**
**      If 'notice' is not NULL, it is assumed to be a format
**      for a message to local opers. I can contain only one
**      '%s', which will be replaced by the sockhost field of
**      the failing link.
**
**      Also, the notice is skipped for "uninteresting" cases,
**      like Persons and yet unknown connections...
*/

static int
dead_link(aClient *to, char *notice)

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

  return (-1);
} /* dead_link() */

/*
 * flush_connections - Used to empty all output buffers for all connections. 
 * Should only be called once per scan of connections. There should be a 
 * select in here perhaps but that means either forcing a timeout or doing 
 * a poll. When flushing, all we do is empty the obuffer array for each local
 * client and try to send it. if we cant send it, it goes into the sendQ
 *      -avalon
 */
void flush_connections(struct Client* cptr)
{
#ifdef SENDQ_ALWAYS
  if (0 == cptr) {
    int i;
    for (i = highest_fd; i >= 0; --i) {
      if ((cptr = local[i]) && DBufLength(&cptr->sendQ) > 0)
        send_queued(cptr);
    }
  }
  else if (-1 < cptr->fd && DBufLength(&cptr->sendQ) > 0)
    send_queued(cptr);

#endif /* SENDQ_ALWAYS */
}


/*
** send_message
**      Internal utility which delivers one message buffer to the
**      socket. Takes care of the error handling and buffering, if
**      needed.
**      if SENDQ_ALWAYS is defined, the message will be queued.
**      if ZIP_LINKS is defined, the message will eventually be compressed,
**      anything stored in the sendQ is compressed.
**
**      if msg is a null pointer, we are flushing connection
*/
static int
send_message(aClient *to, char *msg, int len)

#ifdef SENDQ_ALWAYS

{
        static int SQinK;

        if (to->from)
                to = to->from; /* shouldn't be necessary */

        if (IsMe(to))
        {
                sendto_realops("Trying to send to myself! [%s]", msg);
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

                if (IsDoingList(to)) {
      /* Pop the sendq for this message */
      /*if (!IsAnOper(to))
        sendto_ops("LIST blocked for %s", get_client_name(to, FALSE)); */
      SetSendqPop(to);
      return 0;
    }
                else
      {
        if (IsClient(to))
          to->flags |= FLAGS_SENDQEX;
        return dead_link(to, "Max Sendq exceeded");
      }
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
} /* send_message() */

#else /* SENDQ_ALWAYS */

{
        int rlen = 0;

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
                        sendto_ops_butone(to,
                                "Max SendQ limit exceeded for %s : %d > %d",
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
        } /* else if (rlen < len) */

        /*
        ** Update statistics. The following is slightly incorrect
        ** because it counts messages even if queued, but bytes
        ** only really sent. Queued bytes get updated in SendQueued.
        */
        to->sendM += 1;
        me.sendM += 1;

        return 0;
} /* send_message() */

#endif /* SENDQ_ALWAYS */

/*
** send_queued
**      This function is called from the main select-loop (or whatever)
**      when there is a chance the some output would be possible. This
**      attempts to empty the send queue as far as possible...
*/
int
send_queued(aClient *to)

{
        char *msg;
        int len, rlen;
#ifdef ZIP_LINKS
        int more = NO;
#endif

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
        } /* if (IsDead(to)) */

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
                                return dead_link(to, "Buffer allocation error for %s");
                }
        } /* if ((to->flags2 & FLAGS2_ZIP) && to->zip->outcount) */
#endif /* ZIP_LINKS */

        while (DBufLength(&to->sendQ) > 0)
        {
                msg = dbuf_map(&to->sendQ, &len);

                /* Returns always len > 0 */
                if ((rlen = deliver_it(to, msg, len)) < 0)
                        return dead_link(to,"Write error to %s, closing link");

                (void)dbuf_delete(&to->sendQ, rlen);
                to->lastsq = DBufLength(&to->sendQ)/1024;
    /* sendq is now empty.. if there a blocked list? */
    if (IsSendqPopped(to) && (DBufLength(&to->sendQ) == 0)) {
      char **parv;
      ClearSendqPop(to);
      /*sendto_ops("LIST restarted at %d, %d for %s", to->listprogress,
        to->listprogress2, to->name);*/
      parv=(char**)malloc(sizeof(char**));
      parv[0]=(char*)malloc(strlen(to->name)+1);
      strcpy(parv[0], to->name);
      (void)m_list(to, to, 1, parv);
      free(parv[0]); free(parv);
    }
                if (rlen < len)
                {    
                        /* ..or should I continue until rlen==0? */
                        /* no... rlen==0 means the send returned EWOULDBLOCK... */
                        break;
                }

        #ifdef ZIP_LINKS
                if (DBufLength(&to->sendQ) == 0 && more)
                {
                        /*
                        ** The sendQ is now empty, compress what's left
                        ** uncompressed and try to send it too
                        */
                        more = 0;
                        msg = zip_buffer(to, NULL, &len, 1);

                        if (len == -1)
                                return dead_link(to, "fatal error in zip_buffer()");

                        if (dbuf_put(&to->sendQ, msg, len) < 0)
                                return dead_link(to, "Buffer allocation error for %s");
                } /* if (DBufLength(&to->sendQ) == 0 && more) */
        #endif /* ZIP_LINKS */      
        } /* while (DBufLength(&to->sendQ) > 0) */

        return (IsDead(to)) ? -1 : 0;
} /* send_queued() */

/*
** send message to single client
*/

void
sendto_one(aClient *to, const char *pattern, ...)

{
  va_list       args;

  va_start(args, pattern);

  vsendto_one(to, pattern, args);

  va_end(args);
} /* sendto_one() */

/*
 * vsendto_one()
 * Backend for sendto_one() - send string with variable
 * arguments to client 'to'
 * -wnder
*/

static void
vsendto_one(aClient *to, const char *pattern, va_list args)

{
  int len; /* used for the length of the current message */
  
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

  len = vsprintf_irc(sendbuf, pattern, args);

  /*
   * from rfc1459
   *
   * IRC messages are always lines of characters terminated with a CR-LF
   * (Carriage Return - Line Feed) pair, and these messages shall not
   * exceed 512 characters in length, counting all characters including
   * the trailing CR-LF. Thus, there are 510 characters maximum allowed
   * for the command and its parameters.  There is no provision for
   * continuation message lines.  See section 7 for more details about
   * current implementations.
   */

  /*
   * We have to get a \r\n\0 onto sendbuf[] somehow to satisfy
   * the rfc. We must assume sendbuf[] is defined to be 513
   * bytes - a maximum of 510 characters, the CR-LF pair, and
   * a trailing \0, as stated in the rfc. Now, if len is greater
   * than the third-to-last slot in the buffer, an overflow will
   * occur if we try to add three more bytes, if it has not
   * already occured. In that case, simply set the last three
   * bytes of the buffer to \r\n\0. Otherwise, we're ok. My goal
   * is to get some sort of vsnprintf() function operational
   * for this routine, so we never again have a possibility
   * of an overflow.
   * -wnder
   */
        if (len > 510)
        {
                sendbuf[510] = '\r';
                sendbuf[511] = '\n';
                sendbuf[512] = '\0';
                len = 512;
        }
        else
        {
                sendbuf[len++] = '\r';
                sendbuf[len++] = '\n';
                sendbuf[len] = '\0';
        }

        Debug((DEBUG_SEND,"Sending [%s] to %s",sendbuf,to->name));

        (void)send_message(to, sendbuf, len);
} /* vsendto_one() */

void
sendto_channel_butone(aClient *one, aClient *from, aChannel *chptr, 
                      const char *pattern, ...)

{
  va_list       args;
  register Link *lp;
  register aClient *acptr;
  register int index; /* index of sentalong[] to flag client as having received message */

  va_start(args, pattern);

  ++current_serial;
  
  for (lp = chptr->members; lp; lp = lp->next)
    {
      acptr = lp->value.cptr;
      
      if (acptr->from == one)
        continue;       /* ...was the one I should skip */
      
      index = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          vsendto_prefix_one(acptr, from, pattern, args);
          sentalong[index] = current_serial;
        }
      else
        {
          /*
           * Now check whether a message has been sent to this
           * remote link already
           */
          if(sentalong[index] != current_serial)
            {
              vsendto_prefix_one(acptr, from, pattern, args);
              sentalong[index] = current_serial;
            }
        }
    }

        va_end(args);
} /* sendto_channel_butone() */

void
sendto_channel_type(aClient *one, aClient *from, aChannel *chptr, 
                    int type, const char *pattern, ...)

{
  va_list       args;
  register Link *lp;
  register aClient *acptr;
  register int i;

  va_start(args, pattern);

  ++current_serial;

  for (lp = chptr->members; lp; lp = lp->next)
    {
      if (!(lp->flags & type))
        continue;

      acptr = lp->value.cptr;
      if (acptr->from == one)
        continue;

      i = acptr->from->fd;
      if (MyConnect(acptr) && IsRegisteredUser(acptr))
        {
          vsendto_prefix_one(acptr, from, pattern, args);
          sentalong[i] = current_serial;
        }
      else
        {
#if 0
          /*
           * We have to remove this kludge since it is not
           * compatible with stdarg.h - if we call va_arg()
           * several times to grab the p1 and p4, it will work
           * short-term, but the next time we hit a local
           * client and call vsendto_prefix_one(), 'args'
           * will be basically null, causing a core dump.
           * -wnder
           *
           */

          /*
           * If the target's server can do CAP_CHW, only
           * one send is needed, otherwise, I do a bunch of
           * send's to each target on that server. (kludge)
           *
           * The USE_VARARGS is broken, we'll have to fix that later
           * -Dianora
           */
          if(!IsCapable(acptr->from,CAP_CHW))
            {
              /* kludge in a built in chan wall for older servers */
              
              sendto_prefix_one(acptr, from,
                                ":%s NOTICE %s :%s",
                                p1,
                                /* ignore p2, replace with NOTICE */
                                /* ignore p3, replace with username */
                                lp->value.cptr->name,
                                /* p4 is message "payload" */
                                p4);
            }
          else
            {
#endif /* 0 */

              /* Now check whether a message has been sent to this
               * remote link already
               */
              if (sentalong[i] != current_serial)
                {
                  vsendto_prefix_one(acptr, from, pattern, args);
                  sentalong[i] = current_serial;
                }
            }
        } /* for (lp = chptr->members; lp; lp = lp->next) */

      va_end(args);
} /* sendto_channel_type() */


/* 
** sendto_channel_type_notice()  - sends a message to all users on a channel who meet the
** type criteria (chanop/voice/whatever).
** message is also sent back to the sender if they have those privs.
** used in knock/invite/privmsg@+/notice@+
** -good
*/
void
sendto_channel_type_notice(aClient *from, aChannel *chptr, int type, char *message)

{
        register Link *lp;
        register aClient *acptr;
        register int i;

        for (lp = chptr->members; lp; lp = lp->next)
        {
                if (!(lp->flags & type))
                        continue;

                acptr = lp->value.cptr;

                i = acptr->from->fd;
                if (IsRegisteredUser(acptr))
                {
                        sendto_prefix_one(acptr, from, ":%s NOTICE %s :%s",
                                from->name, 
                                acptr->name, message);
                }
        }
} /* sendto_channel_type_notice() */


/*
 * sendto_serv_butone
 *
 * Send a message to all connected servers except the client 'one'.
 */

void
sendto_serv_butone(aClient *one, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;

  /*
   * USE_VARARGS IS BROKEN someone volunteer to fix it :-) -Dianora
   *
   * fixed! :-)
   * -wnder
   */

  va_start(args, pattern);
  
  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (one && (cptr == one->from))
        continue;
      
      vsendto_one(cptr, pattern, args);
    }

  va_end(args);
} /* sendto_serv_butone() */

/*
 * sendto_common_channels()
 *
 * Sends a message to all people (excluding user) on local server who are
 * in same channel with user.
 */

void
sendto_common_channels(aClient *user, const char *pattern, ...)

{
  va_list args;
  register Link *channels;
  register Link *users;
  register aClient *cptr;

  va_start(args, pattern);
  
  ++current_serial;
  if (user->fd >= 0)
    sentalong[user->fd] = current_serial;

  if (user->user)
    {
      for (channels = user->user->channel; channels; channels = channels->next)
        for(users = channels->value.chptr->members; users; users = users->next)
          {
            cptr = users->value.cptr;
            if (!MyConnect(cptr) || (sentalong[cptr->fd] == current_serial))
              continue;
            
            sentalong[cptr->fd] = current_serial;
            
            vsendto_prefix_one(cptr, user, pattern, args);
          }
    }

  if (MyConnect(user))
    vsendto_prefix_one(user, user, pattern, args);

  va_end(args);
} /* sendto_common_channels() */

/*
 * sendto_channel_butserv
 *
 * Send a message to all members of a channel that are connected to this
 * server.
 */

void
sendto_channel_butserv(aChannel *chptr, aClient *from, 
                       const char *pattern, ...)

{
  va_list args;
  register Link *lp;
  register aClient *acptr;

  va_start(args, pattern);

  for (lp = chptr->members; lp; lp = lp->next)
    if (MyConnect(acptr = lp->value.cptr))
      vsendto_prefix_one(acptr, from, pattern, args);
  
  va_end(args);
} /* sendto_channel_butserv() */

/*
** send a msg to all ppl on servers/hosts that match a specified mask
** (used for enhanced PRIVMSGs)
**
** addition -- Armin, 8jun90 (gruner@informatik.tu-muenchen.de)
*/

static int
match_it(const aClient *one, const char *mask, int what)

{
  if(what == MATCH_HOST)
    return match(mask, one->host);
  else
    return match(mask, one->user->server);
} /* match_it() */

/*
 * sendto_match_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask.
 */

void
sendto_match_servs(aChannel *chptr, aClient *from, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;
  
  va_start(args, pattern);

  if (chptr)
    {
      if (*chptr->chname == '&')
        return;
    }

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;
      
      vsendto_one(cptr, pattern, args);
    }

  va_end(args);
} /* sendto_match_servs() */

/*
 * sendto_match_cap_servs
 *
 * send to all servers which match the mask at the end of a channel name
 * (if there is a mask present) or to all if no mask, and match the capability
 */

void
sendto_match_cap_servs(aChannel *chptr, aClient *from, int cap, 
                       const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;

  va_start(args, pattern);

  if (chptr)
    {
      if (*chptr->chname == '&')
        return;
    }

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == from)
        continue;
      
      if(!IsCapable(cptr, cap))
        continue;
      
      vsendto_one(cptr, pattern, args);
    }

  va_end(args);
} /* sendto_match_cap_servs() */

/*
 * sendto_match_butone
 *
 * Send to all clients which match the mask in a way defined on 'what';
 * either by user hostname or user servername.
 */

void
sendto_match_butone(aClient *one, aClient *from, char *mask, 
                    int what, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;

  va_start(args, pattern);

  /* scan the local clients */
  for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    {
      if (cptr == one)  /* must skip the origin !! */
        continue;
      
      if (match_it(cptr, mask, what))
        vsendto_prefix_one(cptr, from, pattern, args);
    }

  /* Now scan servers */
  for (cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    {
      if (cptr == one) /* must skip the origin !! */
        continue;

      /*
       * The old code looped through every client on the
       * network for each server to check if the
       * server (cptr) has at least 1 client matching
       * the mask, using something like:
       *
       * for (acptr = GlobalClientList; acptr; acptr = acptr->next)
       *        if (IsRegisteredUser(acptr) &&
       *                        match_it(acptr, mask, what) &&
       *                        (acptr->from == cptr))
       *   vsendto_prefix_one(cptr, from, pattern, args);
       *
       * That way, we wouldn't send the message to
       * a server who didn't have a matching client.
       * However, on a network such as EFNet, that
       * code would have looped through about 50
       * servers, and in each loop, loop through
       * about 50k clients as well, calling match()
       * in each nested loop. That is a very bad
       * thing cpu wise - just send the message
       * to every connected server and let that
       * server deal with it.
       * -wnder
       */

      vsendto_prefix_one(cptr, from, pattern, args);
    } /* for (cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client) */

  va_end(args);
} /* sendto_match_butone() */

/*
 * sendto_ops_flags
 *
 *    Send to *local* ops only at a certain level...
 */

void
sendto_ops_flags(int flags, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;
  char nbuf[1024];

  va_start(args, pattern);

  if( flags & FLAGS_SKILL)
    {
      for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
        {
          if(cptr->umodes & FLAGS_SKILL)
            {
              (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                               me.name, cptr->name);
      
              (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
      
              vsendto_one(cptr, nbuf, args);
            }
        } /* for(cptr = local_cptr_list; cptr; cptr = cptr->next_local_client) */
    }


  flags &= ~FLAGS_SKILL;

  if(flags)
    {
      for(cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
        {
          if(cptr->umodes & flags)
            {
              (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                               me.name, cptr->name);
              
              (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
              
              vsendto_one(cptr, nbuf, args);
            }
        } /* for(cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client) */
    }
  va_end(args);
}  /* sendto_ops_flags */

/*
 * sendto_ops
 *
 *      Send to *local* ops only.
 */

void
sendto_ops(const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;
  char nbuf[1024];

  va_start(args, pattern);
  
  for (cptr = local_cptr_list; cptr; cptr = cptr->next_local_client)
    {
      if (SendServNotice(cptr))
        {
          (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                           me.name, cptr->name);
          (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
          
          vsendto_one(cptr, nbuf, args);
        }
    }

  va_end(args);
} /* sendto_ops() */


/*
** sendto_ops_butone
**      Send message to all operators.
** one - client not to send message to
** from- client which message is from *NEVER* NULL!!
*/

void
sendto_ops_butone(aClient *one, aClient *from, const char *pattern, ...)

{
  va_list args;
  register int index;
  register aClient *cptr;

  va_start(args, pattern);

  ++current_serial;

  for (cptr = GlobalClientList; cptr; cptr = cptr->next)
    {
      if (!SendWallops(cptr))
        continue;

/* we want wallops
   if (MyClient(cptr) && !(IsServer(from) || IsMe(from)))
   continue;
*/

      index = cptr->from->fd; /* find connection oper is on */

      if (sentalong[index] == current_serial)
        continue;
      
      if (cptr->from == one)
        continue;       /* ...was the one I should skip */
      
      sentalong[index] = current_serial;
      
      vsendto_prefix_one(cptr->from, from, pattern, args);
    }

  va_end(args);
} /* sendto_ops_butone() */

/*
** sendto_wallops_butone
**      Send message to all operators.
** one - client not to send message to
** from- client which message is from *NEVER* NULL!!
*/

void
sendto_wallops_butone(aClient *one, aClient *from, const char *pattern, ...)

{
  va_list args;
  register int index;
  register aClient *cptr;

  va_start(args, pattern);

  ++current_serial;

  for (cptr = GlobalClientList; cptr; cptr = cptr->next)
    {
      if (!SendWallops(cptr))
        continue;
      
      if (!(IsServer(from) || IsMe(from)) &&
          MyClient(cptr) && !IsOper(cptr))
        continue;

      if (MyClient(cptr) && !IsAnOper(cptr) &&
          !(IsServer(from) || IsMe(from)))
        continue;

      /* find connection oper is on */
      index = cptr->from->fd;

      if (sentalong[index] == current_serial)
        continue;

      if (cptr->from == one)
        continue; /* ...was the one I should skip */

      sentalong[index] = current_serial;
      
      vsendto_prefix_one(cptr->from, from, pattern, args);
    }

        va_end(args);
} /* sendto_wallops_butone() */

/*
** send_operwall -- Send Wallop to All Opers on this server
**
*/

void
send_operwall(aClient *from, char *type_message, char *message)

{
  char sender[NICKLEN + USERLEN + HOSTLEN + 5];
  aClient *acptr;
  anUser *user;
  
  if (!from || !message)
    return;

  if (!IsPerson(from))
    return;

  user = from->user;
  (void)strcpy(sender, from->name);

  if (*from->username) 
    {
      strcat(sender, "!");
      strcat(sender, from->username);
    }

  if (*from->host)
    {
      strcat(sender, "@");
      strcat(sender, from->host);
    }

  for (acptr = oper_cptr_list; acptr; acptr = acptr->next_oper_client)
    {
      if (!SendOperwall(acptr))
        continue; /* has to be oper if in this linklist */

      sendto_one(acptr, ":%s WALLOPS :%s - %s", sender, type_message, message);
    }
} /* send_operwall() */

/*
 * to - destination client
 * from - client which message is from
 *
 * NOTE: NEITHER OF THESE SHOULD *EVER* BE NULL!!
 * -avalon
 *
 */

void
sendto_prefix_one(register aClient *to, register aClient *from, 
                  const char *pattern, ...)

{
  va_list args;

  va_start(args, pattern);

  vsendto_prefix_one(to, from, pattern, args);

  va_end(args);
} /* sendto_prefix_one() */

/*
 * vsendto_prefix_one()
 * Backend to sendto_prefix_one(). stdarg.h does not work
 * well when variadic functions pass their arguments to other
 * variadic functions, so we can call this function in those
 * situations.
 *  This function must ALWAYS be passed a string of the form:
 * ":%s COMMAND <other args>"
 * 
 * -wnder
 */

static void
vsendto_prefix_one(register aClient *to, register aClient *from,
                   const char *pattern, va_list args)

{
  static char sender[HOSTLEN + NICKLEN + USERLEN + 5];
  char *par;
  static char temp[1024];

  par = va_arg(args, char *);

/* Optimize by checking if (from && to) before everything */
  if (to && from)
    {
      if (!MyClient(from) && IsPerson(to) && (to->from == from->from))
        {
          if (IsServer(from))
            {
              (void)ircsprintf(temp, pattern, par, args);
              va_end(args);
              
              sendto_ops(
                         "Send message (%s) to %s[%s] dropped from %s(Fake Dir)",
                         temp, to->name, to->from->name, from->name);
              return;
            }

          sendto_ops("Ghosted: %s[%s@%s] from %s[%s@%s] (%s)",
                     to->name, to->username, to->host,
                     from->name, from->username, from->host,
                     to->from->name);
          
          sendto_serv_butone(NULL, ":%s KILL %s :%s (%s[%s@%s] Ghosted %s)",
                             me.name, to->name, me.name, to->name,
                             to->username, to->host, to->from->name);

          to->flags |= FLAGS_KILLED;

          (void)exit_client(NULL, to, &me, "Ghosted client");

          if (IsPerson(from))
            sendto_one(from, form_str(ERR_GHOSTEDCLIENT),
                       me.name, from->name, to->name, to->username,
                       to->host, to->from);
          
          return;
        } /* if (!MyClient(from) && IsPerson(to) && (to->from == from->from)) */
      
      if (MyClient(to) && IsPerson(from) && !irccmp(par, from->name))
        {
          strcpy(sender, from->name);
          
          if (*from->username)
            {
              strcat(sender, "!");
              strcat(sender, from->username);
            }

          if (*from->host)
            {
              strcat(sender, "@");
              strcat(sender, from->host);
            }
          
          par = sender;
        } /* if (user) */
    } /* if (from && to) */

  /*
   * Assume pattern is of the form: ":%s COMMAND ...",
   * so jump past the ":%s " after we insert our new
   * prefix
   */
  sprintf(temp, ":%s %s", par, &pattern[4]);

  /*
   * temp[] is now a modified version of pattern - pattern
   * used to be: ":%s COMMAND ..."
   * temp is now: ":nick!user@host COMMAND ..."
   */

  vsendto_one(to, temp, args);
} /* vsendto_prefix_one() */

/*
 * sendto_realops
 *
 *    Send to *local* ops only but NOT +s nonopers.
 */

void
sendto_realops(const char *pattern, ...)

{
  va_list args;

  va_start(args, pattern);

  vsendto_realops(pattern, args);

  va_end(vl);
} /* sendto_realops() */

/*
vsendto_realops()
 Send the given string to local operators (not +s)
*/

static void
vsendto_realops(const char *pattern, va_list args)

{
  register aClient *cptr;
  char nbuf[1024];
  
  for (cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
    {
      if (SendServNotice(cptr))
        {
          (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                           me.name, cptr->name);
          (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
          
          vsendto_one(cptr, nbuf, args);
        }
    }
} /* vsendto_realops() */

/*
 * sendto_realops_flags
 *
 *    Send to *local* ops with matching flags
 */

void
sendto_realops_flags(int flags, const char *pattern, ...)

{
  va_list args;
  register aClient *cptr;
  char nbuf[1024];

  va_start(args, pattern);

  for (cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client)
    {
      if(cptr->umodes & flags)
        {
          (void)ircsprintf(nbuf, ":%s NOTICE %s :*** Notice -- ",
                           me.name, cptr->name);
          (void)strncat(nbuf, pattern, sizeof(nbuf) - strlen(nbuf));
          
          vsendto_one(cptr, nbuf, args);
        }
    } /* for (cptr = oper_cptr_list; cptr; cptr = cptr->next_oper_client) */

  va_end(args);
} /* sendto_realops_flags() */

/*
** ts_warn
**      Call sendto_ops, with some flood checking (at most 5 warnings
**      every 5 seconds)
*/
 
void
ts_warn(const char *pattern, ...)

{
  va_list args;
  static time_t last = 0;
  static int warnings = 0;
  time_t now;

  va_start(args, pattern);
 
  /*
  ** if we're running with TS_WARNINGS enabled and someone does
  ** something silly like (remotely) connecting a nonTS server,
  ** we'll get a ton of warnings, so we make sure we don't send
  ** more than 5 every 5 seconds.  -orabidoo
  */

  /*
   * hybrid servers always do TS_WARNINGS -Dianora
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

  vsendto_realops(pattern, args);

  va_end(args);
} /* ts_warn() */

void
flush_server_connections()
{
  aClient *cptr;

  for(cptr = serv_cptr_list; cptr; cptr = cptr->next_server_client)
    if (DBufLength(&cptr->sendQ) > 0)
      (void)send_queued(cptr);
} /* flush_server_connections() */

#ifdef SLAVE_SERVERS

extern aConfItem *u_conf;

int
sendto_slaves(aClient *one, char *message, char *nick, int parc, char *parv[])

{
  aClient *acptr;
  aConfItem *aconf;

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      if (one == acptr)
        continue;
      
      for (aconf = u_conf; aconf; aconf = aconf->next)
        {
          if (match(acptr->name,aconf->name))
            { 
              if(parc > 3)
                sendto_one(acptr,":%s %s %s %s %s :%s",
                           me.name,
                           message,
                           nick,
                           parv[1],
                           parv[2],
                           parv[3]);
              else if(parc > 2)
                sendto_one(acptr,":%s %s %s %s :%s",
                           me.name,
                           message,
                           nick,
                           parv[1],
                           parv[2]);
              else if(parc > 1)
                sendto_one(acptr,":%s %s %s :%s",
                           me.name,
                           message,
                           nick,
                           parv[1]);
            } /* if (match(acptr->name,aconf->name)) */
        } /* for (aconf = u_conf; aconf; aconf = aconf->next) */
    } /* for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client) */

  return 0;
} /* sendto_slaves() */

#endif /* SLAVE_SERVERS */
