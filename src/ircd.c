/************************************************************************
 *   IRC - Internet Relay Chat, src/ircd.c
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
static	char sccsid[] = "@(#)ircd.c	2.48 3/9/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
static char *rcs_version="$Id$";
#endif

#include "struct.h"
#include "common.h"
#include "dline_conf.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pwd.h>
#include <signal.h>
#include <fcntl.h>
/* DEBUG */
#include <sys/time.h>
#include <sys/resource.h>

#undef RUSAGE_SELF 0
#undef RUSAGE_CHILDREN /* hack for old slackware */
#define RUSAGE_CHILDREN -1

#include "inet.h"
#include "h.h"

#include "mtrie_conf.h"

#ifdef  IDLE_CHECK
int	idle_time = MIN_IDLETIME;
#endif

#ifdef OPER_MOTD
aMessageFile *opermotd=(aMessageFile *)NULL;
extern char oper_motd_last_changed_date[];
#endif
aMessageFile *motd=(aMessageFile *)NULL;
extern char motd_last_changed_date[];
#ifdef AMOTD
aMessageFile *amotd=(aMessageFile *)NULL;
#endif
aMessageFile *helpfile=(aMessageFile *)NULL;	

#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)
extern time_t server_split_time;
#endif

int cold_start=YES;	/* set if the server has just fired up */

#ifdef SETUID_ROOT
#include <sys/lock.h>
#include <sys/types.h>
#include <unistd.h>
#endif /* SETUID_ROOT */

/* client pointer lists -Dianora */ 
  
aClient *local_cptr_list=(aClient *)NULL;
aClient *oper_cptr_list=(aClient *)NULL;
aClient *serv_cptr_list=(aClient *)NULL;



int	MAXCLIENTS = MAX_CLIENTS;	/* semi-configurable if QUOTE_SET is def*/
struct	Counter	Count;

time_t	NOW;
aClient me;			/* That's me */
aClient *client = &me;		/* Pointer to beginning of Client list */
#ifdef  LOCKFILE
extern  time_t  pending_kline_time;
extern	struct pkl *pending_klines;
extern  void do_pending_klines(void);

#endif

/* DEBUG */
extern int max_read_so_far;

void	server_reboot();
void	restart (char *);
static	void	open_debugfile(), setup_signals();
static  time_t	io_loop(time_t);

/* externally needed functions */

extern	void dbuf_init();		/* defined in dbuf.c */
extern  void read_motd();		/* defined in s_serv.c */
#ifdef OPER_MOTD
extern  void read_oper_motd();		/* defined in s_serv.c */
#endif
extern  void read_help();		/* defined in s_serv.c */
extern  void sync_channels(time_t);	/* defined in channel.c */

char	**myargv;
int	portnum = -1;		    /* Server port number, listening this */
char	*configfile = CONFIGFILE;	/* Server configuration file */

#ifdef KPATH
char    *klinefile = KLINEFILE;         /* Server kline file */

#ifdef DLINES_IN_KPATH
char    *dlinefile = KLINEFILE;
#else
char    *dlinefile = CONFIGFILE;
#endif

#else
char    *klinefile = CONFIGFILE;
char    *dlinefile = CONFIGFILE;
#endif

#ifdef	GLINES
char	*glinefile = GLINEFILE;
#endif


int	debuglevel = -1;		/* Server debug level */
int	bootopt = 0;			/* Server boot option flags */
char	*debugmode = "";		/*  -"-    -"-   -"-  */
char	*sbrk0;				/* initial sbrk(0) */
static	int	dorehash = 0;
static	char	*dpath = DPATH;
int     rehashed = 1;
int     dline_in_progress = 0;	/* killing off matching D lines ? */
int     noisy_htm=NOISY_HTM;	/* Is high traffic mode noisy or not? */
time_t	nextconnect = 1;	/* time for next try_connections call */
time_t	nextping = 1;		/* same as above for check_pings() */
time_t	nextdnscheck = 0;	/* next time to poll dns to force timeouts */
time_t	nextexpire = 1;		/* next expire run on the dns cache */
int	autoconn = 1;		/* allow auto conns or not */
int	spare_fd = 0;		/* fd to be saved for special circumstances */

#ifdef	PROFIL
extern	etext();

VOIDSIG	s_monitor()
{
  static	int	mon = 0;
#ifdef	POSIX_SIGNALS
  struct	sigaction act;
#endif

  (void)moncontrol(mon);
  mon = 1 - mon;
#ifdef	POSIX_SIGNALS
  act.sa_handler = s_rehash;
  act.sa_flags = 0;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGUSR1);
  (void)sigaction(SIGUSR1, &act, NULL);
#else
  (void)signal(SIGUSR1, s_monitor);
#endif
}
#endif

VOIDSIG s_die()
{
  flush_connections(me.fd);
#ifdef	USE_SYSLOG
  (void)syslog(LOG_CRIT, "Server killed By SIGTERM");
#endif
  exit(-1);
}

static VOIDSIG s_rehash()
{
#ifdef	POSIX_SIGNALS
  struct	sigaction act;
#endif
  dorehash = 1;
#ifdef	POSIX_SIGNALS
  act.sa_handler = s_rehash;
  act.sa_flags = 0;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGHUP);
  (void)sigaction(SIGHUP, &act, NULL);
#else
  (void)signal(SIGHUP, s_rehash);	/* sysV -argv */
#endif
}

void	restart(char *mesg)
{
  static int was_here = NO; /* redundant due to restarting flag below */

  if(was_here)
    abort();
  was_here = YES;

#ifdef	USE_SYSLOG
  (void)syslog(LOG_WARNING, "Restarting Server because: %s, sbrk(0)-etext: %ld",
     mesg,(u_long)sbrk((size_t)0)-(u_long)sbrk0);
#endif
  if (bootopt & BOOT_STDERR)
    {
      fprintf(stderr, "Restarting Server because: %s, sbrk(0)-etext: %ld\n",
        mesg,(u_long)sbrk((size_t)0)-(u_long)sbrk0);
    }
  server_reboot();
}

VOIDSIG s_restart()
{
  static int restarting = 0;

#ifdef	USE_SYSLOG
  (void)syslog(LOG_WARNING, "Server Restarting on SIGINT");
#endif
  if (restarting == 0)
    {
      /* Send (or attempt to) a dying scream to oper if present */

      restarting = 1;
      server_reboot();
    }
}

void	server_reboot()
{
  Reg	int	i;
  
  sendto_ops("Aieeeee!!!  Restarting server... sbrk(0)-etext: %d",
	(u_long)sbrk((size_t)0)-(u_long)sbrk0);

  Debug((DEBUG_NOTICE,"Restarting server..."));
  flush_connections(me.fd);

  /*
  ** fd 0 must be 'preserved' if either the -d or -i options have
  ** been passed to us before restarting.
  */
#ifdef USE_SYSLOG
  (void)closelog();
#endif
  for (i = 3; i < MAXCONNECTIONS; i++)
    (void)close(i);
  if (!(bootopt & (BOOT_TTY|BOOT_DEBUG|BOOT_STDERR)))
    (void)close(2);
  (void)close(1);
  if ((bootopt & BOOT_CONSOLE) || isatty(0))
    (void)close(0);
  if (!(bootopt & (BOOT_INETD|BOOT_OPER)))
    (void)execv(MYNAME, myargv);
#ifdef USE_SYSLOG
  /* Have to reopen since it has been closed above */

  openlog(myargv[0], LOG_PID|LOG_NDELAY, LOG_FACILITY);
  syslog(LOG_CRIT, "execv(%s,%s) failed: %m\n", MYNAME, myargv[0]);
  closelog();
#endif
  Debug((DEBUG_FATAL,"Couldn't restart server: %s", strerror(errno)));
  exit(-1);
}


/*
** try_connections
**
**	Scan through configuration and try new connections.
**	Returns the calendar time when the next call to this
**	function should be made latest. (No harm done if this
**	is called earlier or later...)
*/
static	time_t	try_connections(time_t currenttime)
{
  Reg	aConfItem *aconf;
  Reg	aClient *cptr;
  aConfItem **pconf;
  int	connecting, confrq;
  time_t	next = 0;
  aClass	*cltmp;
  aConfItem *con_conf = (aConfItem *)NULL;
  int	con_class = 0;

  connecting = FALSE;
  Debug((DEBUG_NOTICE,"Connection check at   : %s",
	 myctime(currenttime)));
  for (aconf = conf; aconf; aconf = aconf->next )
    {
      /* Also when already connecting! (update holdtimes) --SRB */
      if (!(aconf->status & CONF_CONNECT_SERVER) || aconf->port <= 0)
	continue;
      cltmp = Class(aconf);
      /*
      ** Skip this entry if the use of it is still on hold until
      ** future. Otherwise handle this entry (and set it on hold
      ** until next time). Will reset only hold times, if already
      ** made one successfull connection... [this algorithm is
      ** a bit fuzzy... -- msa >;) ]
      */

      if ((aconf->hold > currenttime))
	{
	  if ((next > aconf->hold) || (next == 0))
	    next = aconf->hold;
	  continue;
	}

      confrq = get_con_freq(cltmp);
      aconf->hold = currenttime + confrq;
      /*
      ** Found a CONNECT config with port specified, scan clients
      ** and see if this server is already connected?
      */
      cptr = find_name(aconf->name, (aClient *)NULL);
      
      if (!cptr && (Links(cltmp) < MaxLinks(cltmp)) &&
	  (!connecting || (Class(cltmp) > con_class)))
	{
	  con_class = Class(cltmp);
	  con_conf = aconf;
	  /* We connect only one at time... */
	  connecting = TRUE;
	}
      if ((next > aconf->hold) || (next == 0))
	next = aconf->hold;
    }

  if(autoconn == 0)
    {
      if(connecting)
	sendto_ops("Connection to %s[%s] activated.",
		 con_conf->name, con_conf->host);
      sendto_ops("WARNING AUTOCONN is 0, autoconns are disabled");
      Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
      return (next);
    }

  if (connecting)
    {
      if (con_conf->next)  /* are we already last? */
	{
	  for (pconf = &conf; (aconf = *pconf);
	       pconf = &(aconf->next))
	    /* put the current one at the end and
	     * make sure we try all connections
	     */
	    if (aconf == con_conf)
	      *pconf = aconf->next;
	  (*pconf = con_conf)->next = 0;
	}

      if(!(con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
	{
	  sendto_ops("Connection to %s[%s] activated but autoconn is off.",
		     con_conf->name, con_conf->host);
	  sendto_ops("WARNING AUTOCONN on %s[%s] is disabled",
		     con_conf->name, con_conf->host);
	}
      else
	{
	  if (connect_server(con_conf, (aClient *)NULL,
			     (struct hostent *)NULL) == 0)
	    sendto_ops("Connection to %s[%s] activated.",
		       con_conf->name, con_conf->host);
	}
    }
  Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
  return (next);
}


/*
 * I re-wrote check_pings a tad
 *
 * check_pings()
 * inputs	- current time
 * output	- next time_t when check_pings() should be called again
 *
 * side effects	- 
 *
 * Clients can be k-lined/d-lined/g-lined/r-lined and exit_client
 * called for each of these.
 *
 * A PING can be sent to clients as necessary.
 *
 * Client/Server ping outs are handled.
 *
 * -Dianora
 */

/* Note, that dying_clients and dying_clients_reason
 * really don't need to be any where near as long as MAXCONNECTIONS
 * but I made it this long for now. If its made shorter,
 * then a limit check is going to have to be added as well
 * -Dianora
 */

aClient *dying_clients[MAXCONNECTIONS];	/* list of dying clients */
char *dying_clients_reason[MAXCONNECTIONS];

static	time_t	check_pings(time_t currenttime)
{		
  register	aClient	*cptr;		/* current local cptr being examined */
  aConfItem 	*aconf = (aConfItem *)NULL;
  int		ping = 0;		/* ping time value from client */
  int		i;			/* used to index through fd/cptr's */
  time_t	oldest = 0;		/* next ping time */
  time_t	timeout;		/* found necessary ping time */
  char *reason;				/* pointer to reason string */
  int die_index=0;			/* index into list */
  char ping_time_out_buffer[64];	/* blech that should be a define */

					/* of dying clients */
  dying_clients[0] = (aClient *)NULL;	/* mark first one empty */


  /*
   * I re-wrote the way klines are handled. Instead of rescanning
   * the local[] array and calling exit_client() right away, I
   * mark the client thats dying by placing a pointer to its aClient
   * into dying_clients[]. When I have examined all in local[],
   * I then examine the dying_clients[] for aClient's to exit.
   * This saves the rescan on k-lines, also greatly simplifies the code,
   *
   * Jan 28, 1998
   * -Dianora
   */

   for (i = 0; i <= highest_fd; i++)
    {
      if (!(cptr = local[i]) || IsMe(cptr) || IsLog(cptr))
	continue;		/* and go examine next fd/cptr */
      /*
      ** Note: No need to notify opers here. It's
      ** already done when "FLAGS_DEADSOCKET" is set.
      */
      if (cptr->flags & FLAGS_DEADSOCKET)
	{
	  /* N.B. EVERY single time dying_clients[] is set
	   * it must be followed by an immediate continue,
	   * to prevent this cptr from being marked again for exit.
	   * If you don't, you could cause exit_client() to be called twice
	   * for the same cptr. i.e. bad news
	   * -Dianora
	   */

	  dying_clients[die_index] = cptr;
	  dying_clients_reason[die_index++] =
	    ((cptr->flags & FLAGS_SENDQEX) ?
	     "SendQ exceeded" : "Dead socket");
	  dying_clients[die_index] = (aClient *)NULL;
	  continue;		/* and go examine next fd/cptr */
	}

      if (rehashed)
	{
	  if(dline_in_progress)
	    {
	      if(IsPerson(cptr))
		{
		  if( aconf = match_Dline(ntohl(cptr->ip.s_addr)))

		      /* if there is a returned 
		       * aConfItem then kill it
		       */
		    {
		      sendto_realops("D-line active for %s",
				 get_client_name(cptr, FALSE));

		      dying_clients[die_index] = cptr;
#ifdef KLINE_WITH_REASON
		      reason = aconf->passwd ? aconf->passwd : "D-lined";
		      dying_clients_reason[die_index++] = reason;
#else
		      dying_clients_reason[die_index++] = "D-lined";
#endif
		      dying_clients[die_index] = (aClient *)NULL;
		      sendto_one(cptr, err_str(ERR_YOUREBANNEDCREEP),
				 me.name, cptr->name, reason);
		      continue;		/* and go examine next fd/cptr */
		    }
		}
	    }
	  else
	    {
	      if(IsPerson(cptr))
		{
#ifdef GLINES
		  if( (aconf = find_gkill(cptr)) )
		    {
		      if(IsElined(cptr))
			{
			  sendto_realops("G-line over-ruled for %s client is E-lined",
				     get_client_name(cptr,FALSE));
				     continue;
			}

		      sendto_realops("G-line active for %s",
				 get_client_name(cptr, FALSE));

		      dying_clients[die_index] = cptr;
#ifdef KLINE_WITH_REASON
		      reason = aconf->passwd ? aconf->passwd : "G-lined";
		      dying_clients_reason[die_index++] = reason;
#else
		      dying_clients_reason[die_index++] = "G-lined";
#endif
		      dying_clients[die_index] = (aClient *)NULL;
		      sendto_one(cptr, err_str(ERR_YOUREBANNEDCREEP),
				 me.name, cptr->name, reason);
		      continue;		/* and go examine next fd/cptr */
		    }
		  else
#endif
		  if((aconf = find_kill(cptr)))	/* if there is a returned
						   aConfItem.. then kill it */
		    {
		      if(IsElined(cptr))
			{
			  sendto_realops("K-line over-ruled for %s client is E-lined",
				     get_client_name(cptr,FALSE));
				     continue;
			}

		      sendto_realops("K-line active for %s",
				 get_client_name(cptr, FALSE));
		      dying_clients[die_index] = cptr;

#ifdef KLINE_WITH_REASON
		      reason = aconf->passwd ? aconf->passwd : "K-lined";
		      dying_clients_reason[die_index++] = reason;
#else
		      dying_clients_reason[die_index++] = "K-lined";
#endif
		      dying_clients[die_index] = (aClient *)NULL;
		      sendto_one(cptr, err_str(ERR_YOUREBANNEDCREEP),
				 me.name, cptr->name, reason);
		      continue;		/* and go examine next fd/cptr */
		    }
		}
	    }
	}

#ifdef IDLE_CHECK
      if (IsPerson(cptr))
	{
	  if( !IsElined(cptr) && idle_time && 
	      ((timeofday - cptr->user->last) > idle_time))
	    {
	      aConfItem *aconf;

	      dying_clients[die_index] = cptr;
	      dying_clients_reason[die_index++] = "idle exceeder";
	      dying_clients[die_index] = (aClient *)NULL;

	      aconf = make_conf();
	      aconf->status = CONF_KILL;
	      DupString(aconf->host, cptr->user->host);
	      DupString(aconf->passwd, "idle exceeder" );
	      DupString(aconf->name, cptr->user->username);
	      aconf->port = 0;
	      aconf->hold = timeofday + 60;
	      add_temp_kline(aconf);
	      sendto_realops("Idle exceeder %s temp k-lining",
			 get_client_name(cptr,FALSE));
	      continue;		/* and go examine next fd/cptr */
	    }
	}
#endif

#ifdef REJECT_HOLD
      if (IsRejectHeld(cptr))
	{
	  if( timeofday > (cptr->firsttime + REJECT_HOLD_TIME) )
	    {
	      dying_clients[die_index] = cptr;
	      dying_clients_reason[die_index++] = "reject held client";
	      dying_clients[die_index] = (aClient *)NULL;
	      continue;		/* and go examine next fd/cptr */
	    }
	}
#endif

#if defined(R_LINES) && defined(R_LINES_OFTEN)
      /*
       * this is used for KILL lines with time restrictions
       * on them - send a message to the user being killed
       * first.
       * *** Moved up above  -taner ***
       *
       * Moved here, no more rflag -Dianora 
       */
      if (IsPerson(cptr) && find_restrict(cptr))
	{
	  sendto_ops("Restricting %s, closing link.",
		     get_client_name(cptr,FALSE));

	  dying_clients[die_index] = cptr;
	  dying_clients_reason[die_index++] = "you have been R-lined";
	  dying_clients[die_index] = (aClient *)NULL;
	  continue;			/* and go examine next fd/cptr */
	}
#endif

      if (!IsRegistered(cptr))
	ping = CONNECTTIMEOUT;
      else
	ping = get_client_ping(cptr);

      /*
       * Ok, so goto's are ugly and can be avoided here but this code
       * is already indented enough so I think its justified. -avalon
       */
       /*  if (!rflag &&
	       (ping >= currenttime - cptr->lasttime))
	      goto ping_timeout; */

      /*
       * *sigh* I think not -Dianora
       */

      if (ping < (currenttime - cptr->lasttime))
	{

	  /*
	   * If the server hasnt talked to us in 2*ping seconds
	   * and it has a ping time, then close its connection.
	   * If the client is a user and a KILL line was found
	   * to be active, close this connection too.
	   */
	  if (((currenttime - cptr->lasttime) >= (2 * ping) &&
	       (cptr->flags & FLAGS_PINGSENT)) ||
	      ((!IsRegistered(cptr) && (currenttime - cptr->since) >= ping)))
	    {
	      if (!IsRegistered(cptr) &&
		  (DoingDNS(cptr) || DoingAuth(cptr)))
		{
		  if (cptr->authfd >= 0)
		    {
		      (void)close(cptr->authfd);
		      cptr->authfd = -1;
		      cptr->count = 0;
		      *cptr->buffer = '\0';
		    }
#ifdef SHOW_HEADERS
		  if (DoingDNS(cptr))
		    sendheader(cptr, REPORT_FAIL_DNS, R_fail_dns);
		  else
		    sendheader(cptr, REPORT_FAIL_ID, R_fail_id);
#endif
		  Debug((DEBUG_NOTICE,"DNS/AUTH timeout %s",
			 get_client_name(cptr,TRUE)));
		  del_queries((char *)cptr);
		  ClearAuth(cptr);
		  ClearDNS(cptr);
		  SetAccess(cptr);
		  cptr->since = currenttime;
		  continue;
		}
	      if (IsServer(cptr) || IsConnecting(cptr) ||
		  IsHandshake(cptr))
		{
		  sendto_ops("No response from %s, closing link",
			     get_client_name(cptr, FALSE));
		}
	      /*
	       * this is used for KILL lines with time restrictions
	       * on them - send a messgae to the user being killed
	       * first.
	       * *** Moved up above  -taner ***
	       */
	      cptr->flags2 |= FLAGS2_PING_TIMEOUT;
	      dying_clients[die_index++] = cptr;
	      /* the reason is taken care of at exit time */
      /*      dying_clients_reason[die_index++] = "Ping timeout"; */
	      dying_clients[die_index] = (aClient *)NULL;
	      
	      /*
	       * need to start loop over because the close can
	       * affect the ordering of the local[] array.- avalon
	       *
	       ** Not if you do it right - Dianora
	       */

	      continue;
	    }
	  else if ((cptr->flags & FLAGS_PINGSENT) == 0)
	    {
	      /*
	       * if we havent PINGed the connection and we havent
	       * heard from it in a while, PING it to make sure
	       * it is still alive.
	       */
	      cptr->flags |= FLAGS_PINGSENT;
	      /* not nice but does the job */
	      cptr->lasttime = currenttime - ping;
	      sendto_one(cptr, "PING :%s", me.name);
	    }
	}
      /* ping_timeout: */
      timeout = cptr->lasttime + ping;
      while (timeout <= currenttime)
	timeout += ping;
      if (timeout < oldest || !oldest)
	oldest = timeout;

      /*
       * Check UNKNOWN connections - if they have been in this state
       * for > 100s, close them.
       */

      if (IsUnknown(cptr))
	{
	  if (cptr->firsttime ? ((timeofday - cptr->firsttime) > 100) : 0)
	    {
	      dying_clients[die_index] = cptr;
	      dying_clients_reason[die_index++] = "Connection Timed Out";
	      dying_clients[die_index] = (aClient *)NULL;
	      continue;
	    }
	}
    }

  /* Now exit clients marked for exit above.
   * it doesn't matter if local[] gets re-arranged now
   *
   * -Dianora
   */

  for(die_index = 0; (cptr = dying_clients[die_index]); die_index++)
    {
      if(cptr->flags2 & FLAGS2_PING_TIMEOUT)
	{
	  (void)ircsprintf(ping_time_out_buffer,
			    "Ping timeout: %d seconds",
			    currenttime - cptr->lasttime);
	  (void)exit_client(cptr, cptr, &me, ping_time_out_buffer );
	}
      else
	(void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
    }

  rehashed = 0;
  dline_in_progress = 0;

  if (!oldest || oldest < currenttime)
    oldest = currenttime + PINGFREQUENCY;
  Debug((DEBUG_NOTICE,"Next check_ping() call at: %s, %d %d %d",
	 myctime(oldest), ping, oldest, currenttime));
  
  return (oldest);
}

/*
** bad_command
**	This is called when the commandline is not acceptable.
**	Give error message and exit without starting anything.
*/
static	int	bad_command()
{
  (void)printf(
	 "Usage: ircd %s[-h servername] [-p portnumber] [-x loglevel] [-s] [-t]\n",
#ifdef CMDLINE_CONFIG
	 "[-f config] "
#else
	 ""
#endif
	 );
  (void)printf("Server not started\n\n");
  return (-1);
}
#ifndef TRUE
#define TRUE 1
#endif

/* code added by mika nystrom (mnystrom@mit.edu) */
/* this flag is used to signal globally that the server is heavily loaded,
   something which can be taken into account when processing e.g. user commands
   and scheduling ping checks */
/* Changed by Taner Halicioglu (taner@CERF.NET) */

#define LOADCFREQ 5	/* every 5s */
#define LOADRECV 40	/* 40k/s */

int lifesux = 1;
int LRV = LOADRECV;
time_t LCF = LOADCFREQ;
float currlife = 0.0;

int	main(int argc, char *argv[])
{
  int	portarg = 0;
  uid_t	uid, euid;
  time_t	delay = 0;
  int fd;

  cold_start = YES;		/* set when server first starts up */

  if((timeofday = time(NULL)) == -1)
    {
      (void)fprintf(stderr,"ERROR: Clock Failure (%d)\n", errno);
      exit(errno);
    }

  Count.server = 1;	/* us */
  Count.oper = 0;
  Count.chan = 0;
  Count.local = 0;
  Count.total = 0;
  Count.invisi = 0;
  Count.unknown = 0;
  Count.max_loc = 0;
  Count.max_tot = 0;

/* this code by mika@cs.caltech.edu */
/* it is intended to keep the ircd from being swapped out. BSD swapping
   criteria do not match the requirements of ircd */
#ifdef SETUID_ROOT
  if(plock(TXTLOCK)<0) fprintf(stderr,"could not plock...\n");
  if(setuid(IRCD_UID)<0)exit(-1); /* blah.. this should be done better */
#endif
#ifdef INITIAL_DBUFS
  dbuf_init();  /* set up some dbuf stuff to control paging */
#endif
  sbrk0 = (char *)sbrk((size_t)0);
  uid = getuid();
  euid = geteuid();
#ifdef	PROFIL
  (void)monstartup(0, etext);
  (void)moncontrol(1);
  (void)signal(SIGUSR1, s_monitor);
#endif
#ifdef	CHROOTDIR
  if (chdir(dpath))
    {
      perror("chdir");
      exit(-1);
    }
  res_init();
  spare_fd=open("/dev/null",O_RDONLY,0); /* save this fd for rehash dns */
  if (spare_fd < 0)
    {
      perror("cannot save spare_fd");
      exit(-1);
    }
  if (chroot(DPATH))
    {
      (void)fprintf(stderr,"ERROR:  Cannot chdir/chroot\n");
      exit(5);
    }
#endif /*CHROOTDIR*/

#ifdef  ZIP_LINKS
  if (zlib_version[0] == '0')
    {
      fprintf(stderr, "zlib version 1.0 or higher required\n");
      exit(1);
    }
  if (zlib_version[0] != ZLIB_VERSION[0])
    {
      fprintf(stderr, "incompatible zlib version\n");
      exit(1);
    }
  if (strcmp(zlib_version, ZLIB_VERSION) != 0)
    {
      fprintf(stderr, "warning: different zlib version\n");
    }
#endif

  myargv = argv;
  (void)umask(077);                /* better safe than sorry --SRB */
  memset((void *)&me, 0, sizeof(me));

  setup_signals();
  
  /*
  ** All command line parameters have the syntax "-fstring"
  ** or "-f string" (e.g. the space is optional). String may
  ** be empty. Flag characters cannot be concatenated (like
  ** "-fxyz"), it would conflict with the form "-fstring".
  */
  while (--argc > 0 && (*++argv)[0] == '-')
    {
      char	*p = argv[0]+1;
      int	flag = *p++;

      if (flag == '\0' || *p == '\0')
       {
	if (argc > 1 && argv[1][0] != '-')
	  {
	    p = *++argv;
	    argc -= 1;
	  }
	else
	  {
	    p = "";
	  }
       }
      switch (flag)
	{
	case 'a':
	  bootopt |= BOOT_AUTODIE;
	  break;
	case 'c':
	  bootopt |= BOOT_CONSOLE;
	  break;
	case 'q':
	  bootopt |= BOOT_QUICK;
	  break;
	case 'd' :
	  (void)setuid((uid_t)uid);
	  dpath = p;
	  break;
	case 'o': /* Per user local daemon... */
	  (void)setuid((uid_t)uid);
	  bootopt |= BOOT_OPER;
	  break;
#ifdef CMDLINE_CONFIG
	case 'f':
	  (void)setuid((uid_t)uid);
	  configfile = p;
	  break;

#ifdef KPATH
	case 'k':
	  (void)setuid((uid_t)uid);
	  klinefile = p;
	  break;
#endif

#endif
	case 'h':
	  strncpyzt(me.name, p, sizeof(me.name));
	  break;
	case 'i':
	  bootopt |= BOOT_INETD|BOOT_AUTODIE;
	  break;
	case 'p':
	  if ((portarg = atoi(p)) > 0 )
	    portnum = portarg;
	  break;
	case 's':
	  bootopt |= BOOT_STDERR;
	  break;
	case 't':
	  (void)setuid((uid_t)uid);
	  bootopt |= BOOT_TTY;
	  break;
	case 'v':
	  (void)printf("ircd %s\n\tzlib %s\n\tircd_dir: %s\n", version,
#ifndef ZIP_LINKS
		       "not used",
#else
		       zlib_version,
#endif
		       dpath);
	  exit(0);
	case 'x':
#ifdef	DEBUGMODE
	  (void)setuid((uid_t)uid);
	  debuglevel = atoi(p);
	  debugmode = *p ? p : "0";
	  bootopt |= BOOT_DEBUG;
	  break;
#else
	  (void)fprintf(stderr,
			"%s: DEBUGMODE must be defined for -x y\n",
			myargv[0]);
	  exit(0);
#endif
	default:
	  bad_command();
	  break;
	}
    }

#ifndef	CHROOT
  if (chdir(dpath))
    {
      perror("chdir");
      exit(-1);
    }
#endif

#if !defined(IRC_UID)
  if ((uid != euid) && !euid)
    {
      (void)fprintf(stderr,
		    "ERROR: do not run ircd setuid root. Make it setuid a\
normal user.\n");
      exit(-1);
    }
#endif

#if !defined(CHROOTDIR) || (defined(IRC_UID) && defined(IRC_GID))
# ifndef	AIX
  (void)setuid((uid_t)euid);
# endif

  if ((int)getuid() == 0)
    {
# if defined(IRC_UID) && defined(IRC_GID)

      /* run as a specified user */
      (void)fprintf(stderr,"WARNING: running ircd with uid = %d\n",
		    IRC_UID);
      (void)fprintf(stderr,"         changing to gid %d.\n",IRC_GID);

      /* setgid/setuid previous usage noted unsafe by ficus@neptho.net
       */

      if(setgid(IRC_GID) < 0)
	{
	  (void)fprintf(stderr,"ERROR: can't setgid(%d)\n", IRC_GID);
	  exit(-1);
	}

      if(setuid(IRC_UID) < 0)
	{
	  (void)fprintf(stderr,"ERROR: can't setuid(%d)\n", IRC_UID);
	  exit(-1);
	}

#else
#  ifndef __EMX__
      /* check for setuid root as usual */
      (void)fprintf(stderr,
		    "ERROR: do not run ircd setuid root. Make it setuid a\
 normal user.\n");
      exit(-1);
#  endif /* __EMX__ */
# endif	
	    } 
#endif /*CHROOTDIR/UID/GID*/

#if 0
  /* didn't set debuglevel */
  /* but asked for debugging output to tty */
  if ((debuglevel < 0) &&  (bootopt & BOOT_TTY))
    {
      (void)fprintf(stderr,
		    "you specified -t without -x. use -x <n>\n");
      exit(-1);
    }
#endif

  if (argc > 0)
    return bad_command(); /* This should exit out */

#ifdef OPER_MOTD
  oper_motd_last_changed_date[0] = '\0';
  opermotd = (aMessageFile *)NULL;
  read_oper_motd();
#endif 
  motd = (aMessageFile *)NULL;
  helpfile = (aMessageFile *)NULL;

  motd_last_changed_date[0] = '\0';

  read_motd();
#ifdef AMOTD
  read_amotd();
#endif
  read_help();
  clear_client_hash_table();
  clear_channel_hash_table();
  clear_scache_hash_table();	/* server cache name table */
  clear_ip_hash_table();	/* client host ip hash table */
  clear_Dline_table();		/* d line tree */
  initlists();
  initclass();
  initwhowas();
  initstats();
  init_tree_parse(msgtab);
  NOW = time(NULL);

#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)
  server_split_time = NOW;
#endif

  open_debugfile();
  NOW = time(NULL);

  if((timeofday = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  if (portnum < 0)
    portnum = PORTNUM;
  me.port = portnum;
  (void)init_sys();
  me.flags = FLAGS_LISTEN;
  if (bootopt & BOOT_INETD)
    {
      me.fd = 0;
      local[0] = &me;
      me.flags = FLAGS_LISTEN;
    }
  else
    me.fd = -1;

#ifdef USE_SYSLOG
#define SYSLOG_ME     "ircd"
  openlog(SYSLOG_ME, LOG_PID|LOG_NDELAY, LOG_FACILITY);
#endif
  if ((fd = openconf(configfile)) == -1)
    {
      Debug((DEBUG_FATAL, "Failed in reading configuration file %s",
	     configfile));
      (void)printf("Couldn't open configuration file %s\n",
		   configfile);
      exit(-1);
    }
  (void)initconf(bootopt,fd,YES);
  (void)do_include_conf();
/* comstuds SEPARATE_QUOTE_KLINES_BY_DATE code */
#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  {
    struct tm *tmptr;
    char timebuffer[20], filename[200];

    tmptr = localtime(&NOW);
    (void)strftime(timebuffer, 20, "%y%m%d", tmptr);
    ircsprintf(filename, "%s.%s", klinefile, timebuffer);
    if ((fd = openconf(filename)) == -1)
      {
	Debug((DEBUG_ERROR,"Failed reading kline file %s",
	       filename));
	(void)printf("Couldn't open kline file %s\n",
		     filename);
      }
    else
      (void)initconf(0,fd,NO);
  }
#else
#ifdef KPATH
  if ((fd = openconf(klinefile)) == -1)
    {
      Debug((DEBUG_ERROR,"Failed reading kline file %s",
	     klinefile));
      (void)printf("Couldn't open kline file %s\n",
		   klinefile);
    }
  else
    (void)initconf(0,fd,NO);
#endif
#endif
  if (!(bootopt & BOOT_INETD))
    {
      static	char	star[] = "*";
      aConfItem	*aconf;
      u_long vaddr;
      
      if ((aconf = find_me()) && portarg <= 0 && aconf->port > 0)
	portnum = aconf->port;
      Debug((DEBUG_ERROR, "Port = %d", portnum));
      if ((aconf->passwd[0] != '\0') && (aconf->passwd[0] != '*'))
	vaddr = inet_addr(aconf->passwd);
      else
        vaddr = (u_long) NULL;

      if (inetport(&me, star, portnum, vaddr))
      {
	if (bootopt & BOOT_STDERR)
          fprintf(stderr,"Couldn't bind to primary port %d\n", portnum);
#ifdef USE_SYSLOG
	(void)syslog(LOG_CRIT, "Couldn't bind to primary port %d\n", portnum);
#endif
	exit(1);
      }
    }
  else if (inetport(&me, "*", 0, 0))
  {
    if (bootopt & BOOT_STDERR)
      fprintf(stderr,"Couldn't bind to port passed from inetd\n");
#ifdef USE_SYSLOG
      (void)syslog(LOG_CRIT, "Couldn't bind to port passed from inetd\n");
#endif
    exit(1);
  }

  (void)get_my_name(&me, me.sockhost, sizeof(me.sockhost)-1);
  if (me.name[0] == '\0')
    strncpyzt(me.name, me.sockhost, sizeof(me.name));
  me.hopcount = 0;
  me.authfd = -1;
  me.confs = NULL;
  me.next = NULL;
  me.user = NULL;
  me.from = &me;
  me.servptr = &me;
  SetMe(&me);
  make_server(&me);
  me.serv->up = me.name;
  me.lasttime = me.since = me.firsttime = NOW;
  (void)add_to_client_hash_table(me.name, &me);


  check_class();
  if (bootopt & BOOT_OPER)
    {
      aClient *tmp = add_connection(&me, 0);
      
      if (!tmp)
	exit(1);
      SetMaster(tmp);
    }
  else
    write_pidfile();

  Debug((DEBUG_NOTICE,"Server ready..."));
  if (bootopt & BOOT_STDERR)
    fprintf(stderr,"Server Ready\n");
#ifdef USE_SYSLOG
  syslog(LOG_NOTICE, "Server Ready");
#endif
  NOW = time(NULL);


  if((timeofday = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }
  while (1)
    delay = io_loop(delay);
}

time_t io_loop(time_t delay)
{
  static	char	to_send[200];
  static time_t	lasttime	= 0;
  static long	lastrecvK	= 0;
  static int	lrv		= 0;
  time_t lasttimeofday;

  lasttimeofday = timeofday;
  if((timeofday = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  if (timeofday < lasttimeofday)
  {
  	(void)ircsprintf(to_send,
		"System clock is running backwards - (%d < %d)",
		timeofday, lasttimeofday);
	report_error(to_send, &me);
  }
  else if((lasttimeofday + 60) < timeofday)
    {
      (void)ircsprintf(to_send,
		       "System clock was reset into the future - (%d+60 > %d)",
		       timeofday, lasttimeofday);
      report_error(to_send, &me);
      sync_channels(timeofday - lasttimeofday);
    }

  NOW = timeofday;

  /*
   * This chunk of code determines whether or not
   * "life sucks", that is to say if the traffic
   * level is so high that standard server
   * commands should be restricted
   *
   * Changed by Taner so that it tells you what's going on
   * as well as allows forced on (long LCF), etc...
   */
  
  if ((timeofday - lasttime) >= LCF)
    {
      lrv = LRV * LCF;
      lasttime = timeofday;
      currlife = (float)(me.receiveK - lastrecvK)/(float)LCF;
      if ((me.receiveK - lrv) > lastrecvK )
	{
	  if (!lifesux)
	    {
/* In the original +th code Taner had

              LCF << 1;  / * add hysteresis * /

   which does nothing...
   so, the hybrid team changed it to

	      LCF <<= 1;  / * add hysteresis * /

   suddenly, there were reports of clients mysteriously just dropping
   off... Neither rodder or I can see why it makes a difference, but
   lets try it this way...

   The original dog3 code, does not have an LCF variable

   -Dianora

*/
	      lifesux = 1;

	      if(noisy_htm) {
     	        (void)sprintf(to_send, 
                  "Entering high-traffic mode - (%.1fk/s > %dk/s)",
		      (float)currlife, LRV);
	        sendto_ops(to_send);
              }
	    }
	  else
	    {
	      lifesux++;		/* Ok, life really sucks! */
	      LCF += 2;			/* Wait even longer */
              if(noisy_htm) {
	        (void)sprintf(to_send,
		   "Still high-traffic mode %d%s (%d delay): %.1fk/s",
		      lifesux, (lifesux & 0x04) ? " (TURBO)" : "",
		      (int)LCF, (float)currlife);
	        sendto_ops(to_send);
              }
	    }
	}
      else
	{
	  LCF = LOADCFREQ;
	  if (lifesux)
	    {
	      lifesux = 0;
              if(noisy_htm)
	        sendto_ops("Resuming standard operation . . . .");
	    }
	}
      lastrecvK = me.receiveK;
    }

  /*
  ** We only want to connect if a connection is due,
  ** not every time through.  Note, if there are no
  ** active C lines, this call to Tryconnections is
  ** made once only; it will return 0. - avalon
  */
  if (nextconnect && timeofday >= nextconnect)
    nextconnect = try_connections(timeofday);
  /*
  ** DNS checks. One to timeout queries, one for cache expiries.
  */
  if (timeofday >= nextdnscheck)
    nextdnscheck = timeout_query_list(timeofday);
  if (timeofday >= nextexpire)
    nextexpire = expire_cache(timeofday);
  /*
  ** take the smaller of the two 'timed' event times as
  ** the time of next event (stops us being late :) - avalon
  ** WARNING - nextconnect can return 0!
  */
  if (nextconnect)
    delay = MIN(nextping, nextconnect);
  else
    delay = nextping;
  delay = MIN(nextdnscheck, delay);
  delay = MIN(nextexpire, delay);
  delay -= timeofday;
  /*
  ** Adjust delay to something reasonable [ad hoc values]
  ** (one might think something more clever here... --msa)
  ** We don't really need to check that often and as long
  ** as we don't delay too long, everything should be ok.
  ** waiting too long can cause things to timeout...
  ** i.e. PINGS -> a disconnection :(
  ** - avalon
  */
  if (delay < 1)
    delay = 1;
  else
    delay = MIN(delay, TIMESEC);
  /*
   * We want to read servers on every io_loop, as well
   * as "busy" clients (which again, includes servers.
   * If "lifesux", then we read servers AGAIN, and then
   * flush any data to servers.
   *	-Taner
   */

  if((timeofday = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  Debug((DEBUG_DEBUG,"read_message call at: %s %d",
	 myctime(NOW), NOW));

  (void)read_message(delay); /* check everything! */
	
  /*
  ** ...perhaps should not do these loops every time,
  ** but only if there is some chance of something
  ** happening (but, note that conf->hold times may
  ** be changed elsewhere--so precomputed next event
  ** time might be too far away... (similarly with
  ** ping times) --msa
  */

  if ((timeofday >= nextping))	/* && !lifesux ) */
    nextping = check_pings(timeofday);

  if (dorehash && !lifesux)
    {
      (void)rehash(&me, &me, 1);
      dorehash = 0;
    }
  /*
  ** Flush output buffers on all connections now if they
  ** have data in them (or at least try to flush)
  ** -avalon
  */
  flush_connections(me.fd);

#ifdef	LOCKFILE
  /*
  ** If we have pending klines and
  ** CHECK_PENDING_KLINES minutes
  ** have passed, try writing them
  ** out.  -ThemBones
  */
  if ((pending_klines) && ((timeofday - pending_kline_time)
			   >= (CHECK_PENDING_KLINES * 60)))
    do_pending_klines();
#endif

  return delay;
}

/*
 * open_debugfile
 *
 * If the -t option is not given on the command line when the server is
 * started, all debugging output is sent to the file set by LPATH in config.h
 * Here we just open that file and make sure it is opened to fd 2 so that
 * any fprintf's to stderr also goto the logfile.  If the debuglevel is not
 * set from the command line by -x, use /dev/null as the dummy logfile as long
 * as DEBUGMODE has been defined, else dont waste the fd.
 */
static	void	open_debugfile()
{
#ifdef	DEBUGMODE
  int	fd;
  aClient	*cptr;

  if (debuglevel >= 0)
    {
      cptr = make_client(NULL);
      cptr->fd = 2;
      SetLog(cptr);
      cptr->port = debuglevel;
      cptr->flags = 0;
      cptr->acpt = cptr;
      local[2] = cptr;
      (void)strcpy(cptr->sockhost, me.sockhost);

      (void)printf("isatty = %d ttyname = %#x\n",
		   isatty(2), (u_long)ttyname(2));
      if (!(bootopt & BOOT_TTY)) /* leave debugging output on fd 2 */
	{
	  (void)truncate(LOGFILE, 0);
	  if ((fd = open(LOGFILE, O_WRONLY | O_CREAT, 0600)) < 0) 
	    if ((fd = open("/dev/null", O_WRONLY)) < 0)
	      exit(-1);
	  if (fd != 2)
	    {
	      (void)dup2(fd, 2);
	      (void)close(fd); 
	    }
	  strncpyzt(cptr->name, LOGFILE, sizeof(cptr->name));
	}
      else if (isatty(2) && ttyname(2))
	strncpyzt(cptr->name, ttyname(2), sizeof(cptr->name));
      else
	(void)strcpy(cptr->name, "FD2-Pipe");
      Debug((DEBUG_FATAL, "Debug: File <%s> Level: %d at %s",
	     cptr->name, cptr->port, myctime(time(NULL))));
    }
  else
    local[2] = NULL;
#endif
  return;
}

static	void	setup_signals()
{
#ifdef	POSIX_SIGNALS
  struct	sigaction act;

  act.sa_handler = SIG_IGN;
  act.sa_flags = 0;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGPIPE);
  (void)sigaddset(&act.sa_mask, SIGALRM);
# ifdef	SIGWINCH
  (void)sigaddset(&act.sa_mask, SIGWINCH);
  (void)sigaction(SIGWINCH, &act, NULL);
# endif
  (void)sigaction(SIGPIPE, &act, NULL);
  act.sa_handler = dummy;
  (void)sigaction(SIGALRM, &act, NULL);
  act.sa_handler = s_rehash;
  (void)sigemptyset(&act.sa_mask);
  (void)sigaddset(&act.sa_mask, SIGHUP);
  (void)sigaction(SIGHUP, &act, NULL);
  act.sa_handler = s_restart;
  (void)sigaddset(&act.sa_mask, SIGINT);
  (void)sigaction(SIGINT, &act, NULL);
  act.sa_handler = s_die;
  (void)sigaddset(&act.sa_mask, SIGTERM);
  (void)sigaction(SIGTERM, &act, NULL);

#else
# ifndef	HAVE_RELIABLE_SIGNALS
  (void)signal(SIGPIPE, dummy);
#  ifdef	SIGWINCH
  (void)signal(SIGWINCH, dummy);
#  endif
# else
#  ifdef	SIGWINCH
  (void)signal(SIGWINCH, SIG_IGN);
#  endif
  (void)signal(SIGPIPE, SIG_IGN);
# endif
  (void)signal(SIGALRM, dummy);   
  (void)signal(SIGHUP, s_rehash);
  (void)signal(SIGTERM, s_die); 
  (void)signal(SIGINT, s_restart);
#endif

#ifdef RESTARTING_SYSTEMCALLS
  /*
  ** At least on Apollo sr10.1 it seems continuing system calls
  ** after signal is the default. The following 'siginterrupt'
  ** should change that default to interrupting calls.
  */
  (void)siginterrupt(SIGALRM, 1);
#endif
}

/*
 * simple function added because its used more than once
 * - Dianora
 */

void report_error_on_tty(char *error_message)
{
  int fd;
  if((fd = open("/dev/tty", O_WRONLY)) >= 0 )
    {
      write(fd,error_message,strlen(error_message));
      close(fd);
    }
}


