/************************************************************************
 *   IRC - Internet Relay Chat, src/s_debug.c
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
 */

#ifndef lint
static  char sccsid[] = "@(#)s_debug.c	2.28 07 Nov 1993 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
static char *version = "$Id$";
#endif

#include "struct.h"
extern	void	count_whowas_memory(int *, u_long *);
extern	u_long	cres_mem(aClient *);
extern  void    count_ip_hash(int *,u_long *);	/* defined in s_conf.c */
extern	void	count_dline_hash(int *,u_long *); /* defined in s_conf.c */

/*
 * Option string.  Must be before #ifdef DEBUGMODE.
 */
char	serveropts[] = {
#ifdef	SENDQ_ALWAYS
'A',
#endif
#ifdef	CHROOTDIR
'c',
#endif
#ifdef	CMDLINE_CONFIG
'C',
#endif
#ifdef        DO_ID
'd',
#endif
#ifdef	DEBUGMODE
'D',
#endif
#ifdef	LOCOP_REHASH
'e',
#endif
#ifdef	OPER_REHASH
'E',
#endif
#ifdef	HUB
'H',
#endif
#ifdef	SHOW_INVISIBLE_LUSERS
'i',
#endif
#ifndef	NO_DEFAULT_INVISIBLE
'I',
#endif
#ifdef	OPER_KILL
# ifdef  LOCAL_KILL_ONLY
'k',
# else
'K',
# endif
#endif
#ifdef	IDLE_FROM_MSG
'M',
#endif
#ifdef	CRYPT_OPER_PASSWORD
'p',
#endif
#ifdef	CRYPT_LINK_PASSWORD
'P',
#endif
#ifdef	LOCOP_RESTART
'r',
#endif
#ifdef	OPER_RESTART
'R',
#endif
#ifdef	OPER_REMOTE
't',
#endif
#ifdef	VALLOC
'V',
#endif
#ifdef	USE_SYSLOG
'Y',
#endif
#ifdef ZIP_LINKS
'Z',
#endif
' ',
'T',
'S',
#ifdef TS_CURRENT
'0' + TS_CURRENT,
#endif
/* th+hybrid servers ONLY do TS */
/* th+hybrid servers ALWAYS do TS_WARNINGS */
'o',
'w',
'\0'};

#include "numeric.h"
#include "common.h"
#include "sys.h"
/* #include "whowas.h" */
#include "hash.h"
#include <sys/file.h>
#ifdef HPUX
#include <fcntl.h>
#endif
#if !defined(ULTRIX) && !defined(SGI) && !defined(sequent) && \
    !defined(__convex__)
# include <sys/param.h>
#endif
#ifdef HPUX
# include <sys/syscall.h>
# define getrusage(a,b) syscall(SYS_GETRUSAGE, a, b)
#endif
#if defined( HAVE_GETRUSAGE )
# ifdef SOL20
#  include <sys/time.h>
/*
#  include <sys/rusage.h>
*/
# endif
# include <sys/resource.h>
#else
#  if defined( HAVE_TIMES )
#   include <sys/times.h>
#  endif
#endif /* HAVE_GETRUSAGE */
#ifdef HPUX
#include <unistd.h>
#ifdef DYNIXPTX
#include <sys/types.h>
#include <time.h>
#endif
#endif
#include "h.h"

#ifndef ssize_t
#define ssize_t unsigned int
#endif

/* extern char *sys_errlist[]; */

/*#ifdef DEBUGMODE*/
#if defined(DNS_DEBUG) || defined(DEBUGMODE)
static	char	debugbuf[1024];

#ifndef	USE_VARARGS
/*VARARGS2*/
void debug(int level, 
	      char *form, char *p1, char *p2, char *p3,
	      char *p4, char *p5, char *p6, char *p7, char *p8,
	      char *p9, char *p10)
{
#else
void	debug(int level, char *form, va_alist)
va_dcl
{
  va_list	vl;

  va_start(vl);
#endif
  int	err = errno;
#ifdef USE_SYSLOG
  if (level == DEBUG_ERROR)
    syslog(LOG_ERR, form, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
#endif
  if ((debuglevel >= 0) && (level <= debuglevel))
    {
#ifndef	USE_VARARGS
      (void)ircsprintf(debugbuf, form,
		       p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
#else
      (void)vsprintf(debugbuf, form, vl);
#endif
      if (local[2])
	{
	  local[2]->sendM++;
	  local[2]->sendB += strlen(debugbuf);
	}
      (void)fprintf(stderr, "%s", debugbuf);
      (void)fputc('\n', stderr);
    }
  errno = err;
}

/*
 * This is part of the STATS replies. There is no offical numeric for this
 * since this isnt an official command, in much the same way as HASH isnt.
 * It is also possible that some systems wont support this call or have
 * different field names for "struct rusage".
 * -avalon
 */
void	send_usage(aClient *cptr, char *nick)
{

#if defined( HAVE_GETRUSAGE )
  struct	rusage	rus;
  time_t	secs, rup;
#ifdef	hz
# define hzz hz
#else
# ifdef HZ
#  define hzz HZ
# else
  int	hzz = 1;
#  ifdef HPUX
  hzz = (int)sysconf(_SC_CLK_TCK);
#  endif
# endif
#endif

  if (getrusage(RUSAGE_SELF, &rus) == -1)
    {
      sendto_one(cptr,":%s NOTICE %s :Getruseage error: %s.",
		 me.name, nick, sys_errlist[errno]);
      return;
    }
  secs = rus.ru_utime.tv_sec + rus.ru_stime.tv_sec;
  rup = timeofday - me.since;
  if (secs == 0)
    secs = 1;

  sendto_one(cptr,
	     ":%s %d %s :CPU Secs %d:%d User %d:%d System %d:%d",
	     me.name, RPL_STATSDEBUG, nick, secs/60, secs%60,
	     rus.ru_utime.tv_sec/60, rus.ru_utime.tv_sec%60,
	     rus.ru_stime.tv_sec/60, rus.ru_stime.tv_sec%60);
  sendto_one(cptr, ":%s %d %s :RSS %d ShMem %d Data %d Stack %d",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_maxrss,
	     rus.ru_ixrss / (rup * hzz), rus.ru_idrss / (rup * hzz),
	     rus.ru_isrss / (rup * hzz));
  sendto_one(cptr, ":%s %d %s :Swaps %d Reclaims %d Faults %d",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_nswap,
	     rus.ru_minflt, rus.ru_majflt);
  sendto_one(cptr, ":%s %d %s :Block in %d out %d",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_inblock,
	     rus.ru_oublock);
  sendto_one(cptr, ":%s %d %s :Msg Rcv %d Send %d",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_msgrcv, rus.ru_msgsnd);
  sendto_one(cptr, ":%s %d %s :Signals %d Context Vol. %d Invol %d",
	     me.name, RPL_STATSDEBUG, nick, rus.ru_nsignals,
	     rus.ru_nvcsw, rus.ru_nivcsw);
#else
# if defined( HAVE_TIMES )
  struct	tms	tmsbuf;
  time_t	secs, mins;
  int	hzz = 1, ticpermin;
  int	umin, smin, usec, ssec;

#  ifdef HPUX
  hzz = sysconf(_SC_CLK_TCK);
#  endif
  ticpermin = hzz * 60;

  umin = tmsbuf.tms_utime / ticpermin;
  usec = (tmsbuf.tms_utime%ticpermin)/(float)hzz;
  smin = tmsbuf.tms_stime / ticpermin;
  ssec = (tmsbuf.tms_stime%ticpermin)/(float)hzz;
  secs = usec + ssec;
  mins = (secs/60) + umin + smin;
  secs %= hzz;

  if (times(&tmsbuf) == -1)
    {
      sendto_one(cptr,":%s %d %s :times(2) error: %s.",
		 me.name, RPL_STATSDEBUG, nick, strerror(errno));
      return;
    }
  secs = tmsbuf.tms_utime + tmsbuf.tms_stime;

  sendto_one(cptr,
	     ":%s %d %s :CPU Secs %d:%d User %d:%d System %d:%d",
	     me.name, RPL_STATSDEBUG, nick, mins, secs, umin, usec,
	     smin, ssec);
# endif /* HAVE_TIMES */
#endif /* HAVE_GETRUSAGE */
  sendto_one(cptr, ":%s %d %s :Reads %d Writes %d",
	     me.name, RPL_STATSDEBUG, nick, readcalls, writecalls);
  sendto_one(cptr, ":%s %d %s :DBUF alloc %d blocks %d",
	     me.name, RPL_STATSDEBUG, nick, dbufalloc, dbufblocks);
  sendto_one(cptr,
	     ":%s %d %s :Writes:  <0 %d 0 %d <16 %d <32 %d <64 %d",
	     me.name, RPL_STATSDEBUG, nick,
	     writeb[0], writeb[1], writeb[2], writeb[3], writeb[4]);
  sendto_one(cptr,
	     ":%s %d %s :<128 %d <256 %d <512 %d <1024 %d >1024 %d",
	     me.name, RPL_STATSDEBUG, nick,
	     writeb[5], writeb[6], writeb[7], writeb[8], writeb[9]);
  return;
}
#endif

void	count_memory(aClient *cptr,char *nick)
{
  extern	aChannel	*channel;
  extern	aClass	*classes;
  extern	aConfItem	*conf;
  
  Reg aClient *acptr;
  Reg Link *link;
  Reg aChannel *chptr;
  Reg aConfItem *aconf;
  Reg aClass *cltmp;

  int lc = 0;		/* local clients */
  int ch = 0;		/* channels */
  int lcc = 0;		/* local client conf links */
  int rc = 0;		/* remote clients */
  int us = 0;		/* user structs */
  int chu = 0;		/* channel users */
  int chi = 0;		/* channel invites */
  int chb = 0;		/* channel bans */
  int wwu = 0;		/* whowas users */
  int cl = 0;		/* classes */
  int co = 0;		/* conf lines */

  int usi = 0;		/* users invited */
  int usc = 0;		/* users in channels */
  int aw = 0;		/* aways set */
  int number_ips_stored;	/* number of ip addresses hashed */
  int number_dlines_stored;	/* number of dlines hashed */
  int number_servers_cached; /* number of servers cached by scache */

  u_long chm = 0;	/* memory used by channels */
  u_long chbm = 0;	/* memory used by channel bans */
  u_long lcm = 0;	/* memory used by local clients */
  u_long rcm = 0;	/* memory used by remote clients */
  u_long awm = 0;	/* memory used by aways */
  u_long wwm = 0;	/* whowas array memory used */
  u_long com = 0;	/* memory used by conf lines */
  u_long db = 0;	/* memory used by dbufs */
  u_long maxdb = 0;	/* max used by dbufs */
  u_long rm = 0;	/* res memory used */
  u_long mem_servers_cached; /* memory used by scache */
  u_long mem_ips_stored;	/* memory used by ip address hash */
  u_long mem_dlines_stored;	/* memory used by dline hash */

  u_long totcl = 0;
  u_long totch = 0;
  u_long totww = 0;
  u_long tot = 0;

  count_whowas_memory(&wwu, &wwm);	/* no more away memory to count */

  for (acptr = client; acptr; acptr = acptr->next)
    {
      if (MyConnect(acptr))
	{
	  lc++;
	  for (link = acptr->confs; link; link = link->next)
	    lcc++;
	}
      else
	rc++;
      if (acptr->user)
	{
	  us++;
	  for (link = acptr->user->invited; link;
	       link = link->next)
	    usi++;
	  for (link = acptr->user->channel; link;
	       link = link->next)
	    usc++;
	  if (acptr->user->away)
	    {
	      aw++;
	      awm += (strlen(acptr->user->away)+1);
	    }
	}
    }
  lcm = lc * CLIENT_LOCAL_SIZE;
  rcm = rc * CLIENT_REMOTE_SIZE;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      ch++;
      chm += (strlen(chptr->chname) + sizeof(aChannel));
      for (link = chptr->members; link; link = link->next)
	chu++;
      for (link = chptr->invites; link; link = link->next)
	chi++;
      for (link = chptr->banlist; link; link = link->next)
	{
	  chb++;
	  chbm += (strlen(link->value.cp)+1+sizeof(Link));
	}
    }

  for (aconf = conf; aconf; aconf = aconf->next)
    {
      co++;
      com += aconf->host ? strlen(aconf->host)+1 : 0;
      com += aconf->passwd ? strlen(aconf->passwd)+1 : 0;
      com += aconf->name ? strlen(aconf->name)+1 : 0;
      com += sizeof(aConfItem);
    }

  for (cltmp = classes; cltmp; cltmp = cltmp->next)
    cl++;

  sendto_one(cptr, ":%s %d %s :Client Local %d(%d) Remote %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, lc, lcm, rc, rcm);
  sendto_one(cptr, ":%s %d %s :Users %d(%d) Invites %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, us, us*sizeof(anUser), usi,
	     usi * sizeof(Link));
  sendto_one(cptr, ":%s %d %s :User channels %d(%d) Aways %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, usc, usc*sizeof(Link),
	     aw, awm);
  sendto_one(cptr, ":%s %d %s :Attached confs %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, lcc, lcc*sizeof(Link));

  totcl = lcm + rcm + us*sizeof(anUser) + usc*sizeof(Link) + awm;
  totcl += lcc*sizeof(Link) + usi*sizeof(Link);

  sendto_one(cptr, ":%s %d %s :Conflines %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, co, com);

  sendto_one(cptr, ":%s %d %s :Classes %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, cl, cl*sizeof(aClass));

  sendto_one(cptr, ":%s %d %s :Channels %d(%d) Bans %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, ch, chm, chb, chbm);
  sendto_one(cptr, ":%s %d %s :Channel members %d(%d) invite %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, chu, chu*sizeof(Link),
	     chi, chi*sizeof(Link));

  totch = chm + chbm + chu*sizeof(Link) + chi*sizeof(Link);

  sendto_one(cptr, ":%s %d %s :Whowas users %d(%d))",
	     me.name, RPL_STATSDEBUG, nick, wwu, wwu*sizeof(anUser));

  sendto_one(cptr, ":%s %d %s :Whowas array %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, NICKNAMEHISTORYLENGTH, wwm);

  totww = wwu*sizeof(anUser) + wwm;

  sendto_one(cptr, ":%s %d %s :Hash: client %d(%d) chan %d(%d)",
	     me.name, RPL_STATSDEBUG, nick,
	     U_MAX, sizeof(aHashEntry) * U_MAX,
	     CH_MAX, sizeof(aHashEntry) * CH_MAX);

  db = dbufblocks * sizeof(dbufbuf);
  maxdb = maxdbufblocks * sizeof(dbufbuf);
  sendto_one(cptr, ":%s %d %s :Dbuf blocks %d(%d), Max %d(%d)",
	     me.name, RPL_STATSDEBUG, nick, dbufblocks, db,
	     maxdbufblocks, maxdb);

  rm = cres_mem(cptr);

  count_scache(&number_servers_cached,&mem_servers_cached);

  sendto_one(cptr, ":%s %d %s :scache %d(%d)",
	     me.name, RPL_STATSDEBUG, nick,
	     number_servers_cached,
	     mem_servers_cached);

  count_ip_hash(&number_ips_stored,&mem_ips_stored);
  sendto_one(cptr, ":%s %d %s :iphash %d(%d)",
	     me.name, RPL_STATSDEBUG, nick,
	     number_ips_stored,
	     mem_ips_stored);

  count_dline_hash(&number_dlines_stored,&mem_dlines_stored);
  sendto_one(cptr, ":%s %d %s :dhash %d(%d)",
	     me.name, RPL_STATSDEBUG, nick,
	     number_dlines_stored,
	     mem_dlines_stored);

  tot = totww + totch + totcl + com + cl*sizeof(aClass) + db + rm;
  tot += sizeof(aHashEntry) * U_MAX;
  tot += sizeof(aHashEntry) * CH_MAX;

  tot += mem_servers_cached;
  sendto_one(cptr, ":%s %d %s :Total: ww %d ch %d cl %d co %d db %d",
	     me.name, RPL_STATSDEBUG, nick, totww, totch, totcl, com, db);

  sendto_one(cptr, ":%s %d %s :TOTAL: %d sbrk(0)-etext: %u",
	     me.name, RPL_STATSDEBUG, nick, tot,
	     (u_long)sbrk((size_t)0)-(u_long)sbrk0);

  return;
}
