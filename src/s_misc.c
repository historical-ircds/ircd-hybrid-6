/************************************************************************
 *   IRC - Internet Relay Chat, src/s_misc.c
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
 *  $Id$
 */
#include "s_conf.h"
#include "struct.h"
#include "res.h"
#include "common.h"
#include "numeric.h"
#include "h.h"
#include "fdlist.h"
#include "s_bsd.h"
#include "send.h"

#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

/* LINKLIST */
extern aClient *local_cptr_list;
extern aClient *oper_cptr_list;
extern aClient *serv_cptr_list;

char *show_capabilities(aClient *);

extern char *oper_privs(aClient *, int);        /* defined in s_conf.c */

static char* months[] = {
  "January",   "February", "March",   "April",
  "May",       "June",     "July",    "August",
  "September", "October",  "November","December"
};

static char* weekdays[] = {
  "Sunday",   "Monday", "Tuesday", "Wednesday",
  "Thursday", "Friday", "Saturday"
};

/*
 * stats stuff
 */
struct stats  ircst;
struct stats* ircstp = &ircst;

char* date(time_t clock) 
{
  static        char        buf[80], plus;
  struct        tm *lt, *gm;
  struct        tm        gmbuf;
  int        minswest;

  if (!clock) 
    time(&clock);
  gm = gmtime(&clock);
  memcpy((void *)&gmbuf, (void *)gm, sizeof(gmbuf));
  gm = &gmbuf;
  lt = localtime(&clock);

  if (lt->tm_yday == gm->tm_yday)
    minswest = (gm->tm_hour - lt->tm_hour) * 60 +
      (gm->tm_min - lt->tm_min);
  else if (lt->tm_yday > gm->tm_yday)
    minswest = (gm->tm_hour - (lt->tm_hour + 24)) * 60;
  else
    minswest = ((gm->tm_hour + 24) - lt->tm_hour) * 60;

  plus = (minswest > 0) ? '-' : '+';
  if (minswest < 0)
    minswest = -minswest;
  
  ircsprintf(buf, "%s %s %d %d -- %t:%t:%t %c%t:%t",
	  weekdays[lt->tm_wday], months[lt->tm_mon],lt->tm_mday,
	  lt->tm_year + 1900, lt->tm_hour, lt->tm_min, lt->tm_sec,
	  plus, minswest/60, minswest%60);

  return buf;
}

char *smalldate(time_t clock)
{
  static  char    buf[MAX_DATE_STRING];
  struct  tm *lt, *gm;
  struct  tm      gmbuf;

  if (!clock)
    time(&clock);
  gm = gmtime(&clock);
  memcpy((void *)&gmbuf, (void *)gm, sizeof(gmbuf));
  gm = &gmbuf; 
  lt = localtime(&clock);
  
  ircsprintf(buf, "%d/%t/%t %t.%t",
	     lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
	     lt->tm_hour, lt->tm_min);
  
  return buf;
}


#if defined(GLINES) || defined(SEPARATE_QUOTE_KLINES_BY_DATE)
/*
 * small_file_date
 * Make a small YYYYMMDD formatted string suitable for a
 * dated file stamp. 
 */
char* small_file_date(time_t clock)
{
  static  char    timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  if (!clock)
    time(&clock);
  tmptr = localtime(&clock);
  strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d", tmptr);
  return timebuffer;
}
#endif

/*
 * Return wildcard name of my server name according to given config entry
 * --Jto
 */
const char* my_name_for_link(const char* name, aConfItem* aconf)
{
  static char          namebuf[HOSTLEN];
  register int         count = aconf->port;
  register const char* start = name;

  if (count <= 0 || count > 5)
    return start;

  while (count-- && name)
    {
      name++;
      name = strchr(name, '.');
    }
  if (!name)
    return start;

  namebuf[0] = '*';
  strncpy(&namebuf[1], name, HOSTLEN - 1);
  namebuf[HOSTLEN - 1] = '\0';
  return namebuf;
}

void        initstats()
{
  memset(&ircst, 0, sizeof(ircst));
}

void tstats(aClient *cptr,char *name)
{
  aClient        *acptr;
  int        i;
  struct stats        *sp;
  struct        stats        tmp;

  sp = &tmp;
  memcpy((void *)sp, (void *)ircstp, sizeof(*sp));
  for (i = 0; i < highest_fd; i++)
    {
      if (!(acptr = local[i]))
        continue;
      if (IsServer(acptr))
        {
          sp->is_sbs += acptr->sendB;
          sp->is_sbr += acptr->receiveB;
          sp->is_sks += acptr->sendK;
          sp->is_skr += acptr->receiveK;
          sp->is_sti += timeofday - acptr->firsttime;
          sp->is_sv++;
          if (sp->is_sbs > 1023)
            {
              sp->is_sks += (sp->is_sbs >> 10);
              sp->is_sbs &= 0x3ff;
            }
          if (sp->is_sbr > 1023)
            {
              sp->is_skr += (sp->is_sbr >> 10);
              sp->is_sbr &= 0x3ff;
            }
          
        }
      else if (IsClient(acptr))
        {
          sp->is_cbs += acptr->sendB;
          sp->is_cbr += acptr->receiveB;
          sp->is_cks += acptr->sendK;
          sp->is_ckr += acptr->receiveK;
          sp->is_cti += timeofday - acptr->firsttime;
          sp->is_cl++;
          if (sp->is_cbs > 1023)
            {
              sp->is_cks += (sp->is_cbs >> 10);
              sp->is_cbs &= 0x3ff;
            }
          if (sp->is_cbr > 1023)
            {
              sp->is_ckr += (sp->is_cbr >> 10);
              sp->is_cbr &= 0x3ff;
            }
          
        }
      else if (IsUnknown(acptr))
        sp->is_ni++;
    }

  sendto_one(cptr, ":%s %d %s :accepts %u refused %u",
             me.name, RPL_STATSDEBUG, name, sp->is_ac, sp->is_ref);
  sendto_one(cptr, ":%s %d %s :unknown commands %u prefixes %u",
             me.name, RPL_STATSDEBUG, name, sp->is_unco, sp->is_unpf);
  sendto_one(cptr, ":%s %d %s :nick collisions %u unknown closes %u",
             me.name, RPL_STATSDEBUG, name, sp->is_kill, sp->is_ni);
  sendto_one(cptr, ":%s %d %s :wrong direction %u empty %u",
             me.name, RPL_STATSDEBUG, name, sp->is_wrdi, sp->is_empt);
  sendto_one(cptr, ":%s %d %s :numerics seen %u mode fakes %u",
             me.name, RPL_STATSDEBUG, name, sp->is_num, sp->is_fake);
  sendto_one(cptr, ":%s %d %s :auth successes %u fails %u",
             me.name, RPL_STATSDEBUG, name, sp->is_asuc, sp->is_abad);
  sendto_one(cptr, ":%s %d %s :local connections %u udp packets %u",
             me.name, RPL_STATSDEBUG, name, sp->is_loc, sp->is_udp);
  sendto_one(cptr, ":%s %d %s :Client Server",
             me.name, RPL_STATSDEBUG, name);
  sendto_one(cptr, ":%s %d %s :connected %u %u",
             me.name, RPL_STATSDEBUG, name, sp->is_cl, sp->is_sv);
  sendto_one(cptr, ":%s %d %s :bytes sent %u.%uK %u.%uK",
             me.name, RPL_STATSDEBUG, name,
             sp->is_cks, sp->is_cbs, sp->is_sks, sp->is_sbs);
  sendto_one(cptr, ":%s %d %s :bytes recv %u.%uK %u.%uK",
             me.name, RPL_STATSDEBUG, name,
             sp->is_ckr, sp->is_cbr, sp->is_skr, sp->is_sbr);
  sendto_one(cptr, ":%s %d %s :time connected %u %u",
             me.name, RPL_STATSDEBUG, name, sp->is_cti, sp->is_sti);
#ifdef FLUD
  sendto_one(cptr, ":%s %d %s :CTCP Floods Blocked %u",
             me.name, RPL_STATSDEBUG, name, sp->is_flud);
#endif /* FLUD */
#ifdef ANTI_IP_SPOOF
  sendto_one(cptr, ":%s %d %s :IP Spoofers %u",
             me.name, RPL_STATSDEBUG, name, sp->is_ipspoof);
#endif /* ANTI_IP_SPOOF */
}


/*
 * Retarded - so sue me :-P
 */
#define _1MEG     (1024.0)
#define _1GIG     (1024.0*1024.0)
#define _1TER     (1024.0*1024.0*1024.0)
#define _GMKs(x)  ( (x > _1TER) ? "Terabytes" : ((x > _1GIG) ? "Gigabytes" : \
                  ((x > _1MEG) ? "Megabytes" : "Kilobytes")))
#define _GMKv(x)  ( (x > _1TER) ? (float)(x/_1TER) : ((x > _1GIG) ? \
               (float)(x/_1GIG) : ((x > _1MEG) ? (float)(x/_1MEG) : (float)x)))

void serv_info(aClient *cptr,char *name)
{
  static char Lformat[] = ":%s %d %s %s %u %u %u %u %u :%u %u %s";
  int        j;
  long        sendK, receiveK, uptime;
  aClient        *acptr;

  sendK = receiveK = 0;
  j = 1;

  for(acptr = serv_cptr_list; acptr; acptr = acptr->next_server_client)
    {
      sendK += acptr->sendK;
      receiveK += acptr->receiveK;
      /* There are no more non TS servers on this network, so that test has
       * been removed. Also, do not allow non opers to see the IP's of servers
       * on stats ?
       */
      if(IsAnOper(cptr))
        sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO,
                   name, get_client_name(acptr, TRUE),
                   (int)DBufLength(&acptr->sendQ),
                   (int)acptr->sendM, (int)acptr->sendK,
                   (int)acptr->receiveM, (int)acptr->receiveK,
                   timeofday - acptr->firsttime,
                   (timeofday > acptr->since) ? (timeofday - acptr->since): 0,
                   IsServer(acptr) ? show_capabilities(acptr) : "-" );
      else
        {
          sendto_one(cptr, Lformat, me.name, RPL_STATSLINKINFO,
                     name, get_client_name(acptr, HIDEME),
                     (int)DBufLength(&acptr->sendQ),
                     (int)acptr->sendM, (int)acptr->sendK,
                     (int)acptr->receiveM, (int)acptr->receiveK,
                     timeofday - acptr->firsttime,
                     (timeofday > acptr->since)?(timeofday - acptr->since): 0,
                     IsServer(acptr) ? show_capabilities(acptr) : "-" );
        }
      j++;
    }

  sendto_one(cptr, ":%s %d %s :%u total server%s",
             me.name, RPL_STATSDEBUG, name, --j, (j==1)?"":"s");

  sendto_one(cptr, ":%s %d %s :Sent total : %7.2f %s",
             me.name, RPL_STATSDEBUG, name, _GMKv(sendK), _GMKs(sendK));
  sendto_one(cptr, ":%s %d %s :Recv total : %7.2f %s",
             me.name, RPL_STATSDEBUG, name, _GMKv(receiveK), _GMKs(receiveK));

  uptime = (timeofday - me.since);
  sendto_one(cptr, ":%s %d %s :Server send: %7.2f %s (%4.1f K/s)",
             me.name, RPL_STATSDEBUG, name, _GMKv(me.sendK), _GMKs(me.sendK),
             (float)((float)me.sendK / (float)uptime));
  sendto_one(cptr, ":%s %d %s :Server recv: %7.2f %s (%4.1f K/s)",
             me.name, RPL_STATSDEBUG, name, _GMKv(me.receiveK), _GMKs(me.receiveK),
             (float)((float)me.receiveK / (float)uptime));
}

/*
 * show_capabilities
 *
 * inputs	- pointer to an aClient
 * output	- pointer to static string
 * side effects	- build up string representing capabilities of server listed
 */

char *show_capabilities(aClient *acptr)
{
  static char        msgbuf[BUFSIZE];
  register        struct Capability *cap;

  strcpy(msgbuf,"TS ");
  if(!acptr->caps)        /* short circuit if no caps */
    return(msgbuf);

  for (cap=captab; cap->cap; cap++)
    {
      if(cap->cap & acptr->caps)
        {
          strcat(msgbuf, cap->name);
          strcat(msgbuf, " ");
        }
    }
  return(msgbuf);
}



