/************************************************************************
 *   IRC - Internet Relay Chat, src/s_bsd.c
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
static  char sccsid[] = "@(#)s_bsd.c	2.78 2/7/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
static char *rcs_version = "$Id$";
#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "res.h"
#include "numeric.h"
#include "patchlevel.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#if defined(SOL20) 
#include <sys/filio.h>
#include <sys/select.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#ifndef __EMX__
#include <utmp.h>
#endif
#include <sys/resource.h>
#include <netdb.h>

/*
 * Stuff for poll()
 */

#ifdef USE_POLL
#include <stropts.h>
#include <poll.h>
#else

/*
 * Stuff for select()
 */

fd_set  *read_set,*write_set;

#ifndef HAVE_FD_ALLOC
fd_set  readset,writeset;
#endif

#endif /* USE_POLL_ */

#ifdef	AIX
# include <time.h>
#endif
#include "h.h"
#include "fdlist.h"
extern fdlist serv_fdlist;

#ifndef NO_PRIORITY
extern fdlist busycli_fdlist;
#endif
extern fdlist default_fdlist;

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET	0x7f
#endif

#if defined(MAXBUFFERS) && !defined(SEQUENT)
int rcvbufmax = 0, sndbufmax = 0;
#endif
#ifdef MAXBUFFERS
void	reset_sock_opts (int, int);
#endif

extern char specific_virtual_host;	/* defined in s_conf.c */
extern struct sockaddr_in vserv;	/* defined in s_conf.c */
extern aClient *serv_cptr_list;	/* defined in ircd.c */
extern aClient *local_cptr_list;/* defined in ircd.c */
extern aClient *oper_cptr_list; /* defined in ircd.c */
extern int resfd;   /* defined in res.c */


aClient	*local[MAXCONNECTIONS];

int	highest_fd = 0;
time_t	timeofday;
static	struct	sockaddr_in	mysk;

static	struct	sockaddr *connect_inet (aConfItem *, aClient *, int *);
static	int	completed_connection (aClient *);
static	int	check_init (aClient *, char *);
static	void	do_dns_async (void), set_sock_opts (int, aClient *);

#if defined(MAXBUFFERS) && !defined(SEQUENT)
static	char	*readbuf;
#else
static	char	readbuf[READBUF_SIZE];
#endif

/*
 * Try and find the correct name to use with getrlimit() for setting the max.
 * number of files allowed to be open by this process.
 */
#ifdef RLIMIT_FDMAX
# define RLIMIT_FD_MAX   RLIMIT_FDMAX
#else
# ifdef RLIMIT_NOFILE
#  define RLIMIT_FD_MAX RLIMIT_NOFILE
# else
#  ifdef RLIMIT_OPEN_MAX
#   define RLIMIT_FD_MAX RLIMIT_OPEN_MAX
#  else
#   undef RLIMIT_FD_MAX
#  endif
# endif
#endif



/*
** Cannot use perror() within daemon. stderr is closed in
** ircd and cannot be used. And, worse yet, it might have
** been reassigned to a normal connection...
** 
** Actually stderr is still there IFF ircd was run with -s --Rodder
*/

/*
** report_error
**	This a replacement for perror(). Record error to log and
**	also send a copy to all *LOCAL* opers online.
**
**	text	is a *format* string for outputing error. It must
**		contain only two '%s', the first will be replaced
**		by the sockhost from the cptr, and the latter will
**		be taken from sys_errlist[errno].
**
**	cptr	if not NULL, is the *LOCAL* client associated with
**		the error.
*/
void	report_error(char *text,aClient *cptr)
{
  register	int	errtmp = errno; /* debug may change 'errno' */
  register	char	*host;
  int	err, len = sizeof(err);
  extern	char	*strerror();

  host = (cptr) ? get_client_name(cptr, FALSE) : "";

  Debug((DEBUG_ERROR, text, host, strerror(errtmp)));

  /*
   * Get the *real* error from the socket (well try to anyway..).
   * This may only work when SO_DEBUG is enabled but its worth the
   * gamble anyway.
   */
#ifdef	SO_ERROR
  if (!IsMe(cptr) && cptr->fd >= 0)
    if (!getsockopt(cptr->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len))
      if (err)
	errtmp = err;
#endif
  sendto_realops_lev(DEBUG_LEV,text, host, strerror(errtmp));
#ifdef USE_SYSLOG
  syslog(LOG_WARNING, text, host, strerror(errtmp));
#endif
  if (bootopt & BOOT_STDERR)
    {
	fprintf(stderr, text, host, strerror(errtmp));
	fprintf(stderr, "\n");
	fflush(stderr);
    }
}

/*
 * inetport
 *
 * Create a socket in the AF_INET domain, bind it to the port given in
 * 'port' and listen to it.  Returns the fd of the
 * socket created or -1 on error.
 */
int	inetport(aClient *cptr, int port, u_long bind_addr)
{
  static struct sockaddr_in server;
  int len = sizeof(server);
  struct hostent *hp;


  (void)strcpy(cptr->name, me.name);

  /*
   * At first, open a new socket
   */
  if (cptr->fd == -1)
    {
      cptr->fd = socket(AF_INET, SOCK_STREAM, 0);
      if (cptr->fd < 0 && errno == EAGAIN)
	{
	  sendto_realops("opening stream socket %s: No more sockets",
			 get_client_name(cptr, TRUE));
	  return -1;
	}
    }
  if (cptr->fd < 0)
    {
      report_error("opening stream socket %s:%s", cptr);
      return -1;
    }
  else if (cptr->fd >= (HARD_FDLIMIT - 10))
    {
      sendto_realops("No more connections allowed (%s)", cptr->name);
      (void)close(cptr->fd);
      return -1;
    }
  set_sock_opts(cptr->fd, cptr);
  /*
   * Bind a port to listen for new connections if port is non-null,
   * else assume it is already open and try get something from it.
   */
  if (port)
    {
      memset((void *) &server, 0, sizeof(server));
      server.sin_family = AF_INET;

      if (bind_addr)
        {
	  server.sin_addr.s_addr = bind_addr;
          if ( (hp = gethostbyaddr((char *)&bind_addr, sizeof(bind_addr),
            AF_INET)) )
              strncpyzt(cptr->sockhost, hp->h_name, HOSTLEN);
          else
              strncpyzt(cptr->sockhost, inetntoa((char *)&bind_addr), HOSTLEN);
        }
      else
        {
          server.sin_addr.s_addr = INADDR_ANY;
          strncpyzt(cptr->sockhost, me.name, HOSTLEN);
        }

      
      server.sin_port = htons(port);
      /*
       * Try 10 times to bind the socket with an interval of 20
       * seconds. Do this so we dont have to keepp trying manually
       * to bind. Why ? Because a port that has closed often lingers
       * around for a short time.
       * This used to be the case.  Now it no longer is.
       * Could cause the server to hang for too long - avalon
       */
      if (bind(cptr->fd, (struct sockaddr *)&server,
	       sizeof(server)) == -1)
	{
	  report_error("binding stream socket %s:%s", cptr);
	  (void)close(cptr->fd);
	  return -1;
	}
    }
  if (getsockname(cptr->fd, (struct sockaddr *)&server, &len))
    {
      report_error("getsockname failed for %s:%s",cptr);
      (void)close(cptr->fd);
      return -1;
    }

  if (cptr->fd > highest_fd)
    highest_fd = cptr->fd;

  cptr->port = (int)ntohs(server.sin_port);
/* If the operating system has a define for SOMAXCONN, use it, otherwise
 *   use HYBRID_SOMAXCONN -Dianora
*/

#ifdef SOMAXCONN
  (void)listen(cptr->fd, SOMAXCONN);
#else
  (void)listen(cptr->fd, HYBRID_SOMAXCONN);
#endif
  local[cptr->fd] = cptr;
  addto_fdlist(cptr->fd,&default_fdlist);
  return 0;
}

/*
 * add_listener
 *
 * Create a new client
 * for a socket that is passive (listen'ing for connections to be accepted).
 */
int	add_listener(aConfItem *aconf)
{
  aClient *cptr;
  u_long vaddr;

  cptr = make_client(NULL);
  cptr->flags = FLAGS_LISTEN;
  cptr->acpt = cptr;
  cptr->from = cptr;
  cptr->username[0] = '\0';
  SetMe(cptr);

  if ((aconf->passwd[0] != '\0') && (aconf->passwd[0] != '*'))
    {
      vaddr = inet_addr(aconf->passwd);
      cptr->ip.s_addr = vaddr;
    }
  else
      vaddr = (u_long) NULL;

  if (inetport(cptr, aconf->port, vaddr))
      cptr->fd = -2;

  if (cptr->fd >= 0)
    {
      cptr->confs = make_link();
      cptr->confs->next = NULL;
      cptr->confs->value.aconf = aconf;
      set_non_blocking(cptr->fd, cptr);
    }
  else
    free_client(cptr);
  return 0;
}

/*
 * close_listeners
 *
 * Close and free all clients which are marked as having their socket open
 * and in a state where they can accept connections.  Unix sockets have
 * the path to the socket unlinked for cleanliness.
 */
void	close_listeners()
{
  aClient	*cptr;
  int	i;
  aConfItem *aconf;

  /*
   * close all 'extra' listening ports we have and unlink the file
   * name if it was a unix socket.
   ***
   * unix domain sockets are no longer supported in hybrid -Dianora
   */
  for (i = highest_fd; i >= 0; i--)
    {
      if (!(cptr = local[i]))
	continue;
      if (cptr == &me || !IsListening(cptr))
	continue;
      aconf = cptr->confs->value.aconf;
      
      if (IsIllegal(aconf) && aconf->clients == 0)
	{
	  close_connection(cptr);
	}
    }
}

/*
 * init_sys
 */
void	init_sys()
{
  int	fd;

#ifdef RLIMIT_FD_MAX
  struct rlimit limit;

  if (!getrlimit(RLIMIT_FD_MAX, &limit))
    {

#ifdef	pyr
      if (limit.rlim_cur < MAXCONNECTIONS)
#else
	if (limit.rlim_max < MAXCONNECTIONS)
#endif	/* ifdef pyr */

	  {
	    (void)fprintf(stderr,"ircd fd table too big\n");
	    (void)fprintf(stderr,"Hard Limit: %ld IRC max: %d\n",
			  (long) limit.rlim_max, MAXCONNECTIONS);
	    (void)fprintf(stderr,"Fix MAXCONNECTIONS\n");
	    exit(-1);
	  }

#ifndef	pyr
      limit.rlim_cur = limit.rlim_max; /* make soft limit the max */
      if (setrlimit(RLIMIT_FD_MAX, &limit) == -1)
	{
	  (void)fprintf(stderr,"error setting max fd's to %ld\n",
			(long) limit.rlim_cur);
	  exit(-1);
	}

#ifndef USE_POLL
      if( MAXCONNECTIONS > FD_SETSIZE )
        {
          (void)fprintf(stderr,
            "FD_SETSIZE = %d MAXCONNECTIONS = %d\n",FD_SETSIZE,
	    MAXCONNECTIONS);
          (void)fprintf(stderr,
            "Make sure your kernel supports a larger FD_SETSIZE then recompile with -DFD_SETSIZE=%d\n",MAXCONNECTIONS);
          exit(-1);
        }
#endif	/* USE_POLL */

#ifndef USE_POLL

#ifndef HAVE_FD_ALLOC
      printf("Value of FD_SETSIZE is %d\n", FD_SETSIZE);
#endif

#endif /* USE_POLL */

      printf("Value of NOFILE is %d\n", NOFILE);
#endif	/* pyr */
    }
#endif	/* RLIMIT_FD_MAX */

#ifdef sequent
# ifndef	DYNIXPTX
  int	fd_limit;

  fd_limit = setdtablesize(MAXCONNECTIONS + 1);
  if (fd_limit < MAXCONNECTIONS)
    {
      (void)fprintf(stderr,"ircd fd table too big\n");
      (void)fprintf(stderr,"Hard Limit: %d IRC max: %d\n",
		    fd_limit, MAXCONNECTIONS);
      (void)fprintf(stderr,"Fix MAXCONNECTIONS\n");
      exit(-1);
    }
# endif
#endif
#if defined(DYNIXPTX) || defined(SVR3)
  char	logbuf[BUFSIZ];

  (void)setvbuf(stderr,logbuf,_IOLBF,sizeof(logbuf));
#else
# if defined(HPUX) || defined(__CYGWIN__)
  (void)setvbuf(stderr, NULL, _IOLBF, 0);
# else
#  if !defined(SOL20) && !defined(__EMX__)
  (void)setlinebuf(stderr);
#  endif
# endif
#endif

#ifndef USE_POLL
  read_set = &readset;
  write_set = &writeset;
#endif

  for (fd = 3; fd < MAXCONNECTIONS; fd++)
    {
      (void)close(fd);
      local[fd] = NULL;
    }
  local[1] = NULL;

  if (bootopt & BOOT_TTY)	/* debugging is going to a tty */
    {
      resfd = init_resolver();
      return;
    }
  (void)close(1);
  if (!(bootopt & BOOT_DEBUG) && !(bootopt & BOOT_STDERR))
    (void)close(2);

#ifndef __CYGWIN__
  if (((bootopt & BOOT_CONSOLE) || isatty(0)) &&
      !(bootopt & (BOOT_INETD|BOOT_OPER)) &&
      !(bootopt & BOOT_STDERR))
    {
      int pid;
      if( (pid = fork()) < 0)
	{
	  if ((fd = open("/dev/tty", O_RDWR)) >= 0)
	  report_error_on_tty("Couldn't fork!\n");
	  exit(0);
	}
      else if(pid > 0)
	exit(0);
#ifdef TIOCNOTTY
      if ((fd = open("/dev/tty", O_RDWR)) >= 0)
	{
	  (void)ioctl(fd, TIOCNOTTY, (char *)NULL);
	  (void)close(fd);
	}
#endif
#if defined(HPUX) || defined(SOL20) || defined(DYNIXPTX) || \
    defined(_POSIX_SOURCE) || defined(SVR4)
      (void)setsid();
#else
# ifndef __EMX__
    (void)setpgrp(0, (int)getpid());
# endif /* __EMX__ */
#endif
    (void)close(0);	/* fd 0 opened by inetd */
    local[0] = NULL;
    }
#endif /* __CYGWIN__ */
  resfd = init_resolver();
  return;
}

void	write_pidfile()
{
#ifdef IRCD_PIDFILE
  int fd;
  char buff[20];
  if ((fd = open(IRCD_PIDFILE, O_CREAT|O_WRONLY, 0600))>=0)
    {
      (void)ircsprintf(buff,"%5d\n", (int)getpid());
      if (write(fd, buff, strlen(buff)) == -1)
	Debug((DEBUG_NOTICE,"Error writing to pid file %s",
	       IRCD_PIDFILE));
      (void)close(fd);
      return;
    }
#ifdef	DEBUGMODE
  else
    Debug((DEBUG_NOTICE,"Error opening pid file %s",
	   IRCD_PIDFILE));
#endif
#endif
}
		
/*
 * Initialize the various name strings used to store hostnames. This is set
 * from either the server's sockhost (if client fd is a tty or localhost)
 * or from the ip# converted into a string. 0 = success, -1 = fail.
 */
static	int	check_init(aClient *cptr,char *sockn)
{
  struct	sockaddr_in sk;
  int	len = sizeof(struct sockaddr_in);

  /* If descriptor is a tty, special checking... */
  /* IT can't EVER be a tty */

  if (getpeername(cptr->fd, (struct sockaddr *)&sk, &len) == -1)
    {
      report_error("connect failure: %s %s", cptr);
      return -1;
    }
  (void)strcpy(sockn, (char *)inetntoa((char *)&sk.sin_addr));
  if (inet_netof(sk.sin_addr) == IN_LOOPBACKNET)
    {
      cptr->hostp = NULL;
      strncpyzt(sockn, me.name, HOSTLEN);
    }
  (void)memcpy( (void *)&cptr->ip, (void *)&sk.sin_addr,
	sizeof(struct in_addr));
  cptr->port = (int)(ntohs(sk.sin_port));

  return 0;
}

/*
 * Ordinary client access check. Look for conf lines which have the same
 * status as the flags passed.
 *
 * outputs
 *  0 = Success
 * -1 = Access denied (no I line match)
 * -2 = Bad socket.
 * -3 = I-line is full
 * -4 = Too many connections from hostname
 * -5 = K-lined
 * also updates reason if a K-line
 *
 */
int	check_client(aClient *cptr,char *username,char **reason)
{
  static	char	sockname[HOSTLEN+1];
  struct	hostent *hp = NULL;
  int	i;
 
  ClearAccess(cptr);
  Debug((DEBUG_DNS, "ch_cl: check access for %s[%s]",
	 cptr->name, inetntoa((char *)&cptr->ip)));

  if (check_init(cptr, sockname))
    return -2;

  hp = cptr->hostp;
  /*
   * Verify that the host to ip mapping is correct both ways and that
   * the ip#(s) for the socket is listed for the host.
   */
  if (hp)
    {
      for (i = 0; hp->h_addr_list[i]; i++)
	if (!bcmp(hp->h_addr_list[i], (char *)&cptr->ip,
		  sizeof(struct in_addr)))
	  break;

      if (!hp->h_addr_list[i])
	{
          sendto_one(cptr, 
            "NOTICE AUTH :*** Your forward and reverse DNS do not match, ignoring hostname.");
	  hp = NULL;
	}
    }

  if ((i = attach_Iline(cptr, hp, sockname,username, reason)))
    {
      Debug((DEBUG_DNS,"ch_cl: access denied: %s[%s]",
	     cptr->name, sockname));
      return i;
    }

  Debug((DEBUG_DNS, "ch_cl: access ok: %s[%s]",
	 cptr->name, sockname));

  return 0;
}

/*
 * check_server_init(), check_server()
 *	check access for a server given its name (passed in cptr struct).
 *	Must check for all C/N lines which have a name which matches the
 *	name given and a host which matches. A host alias which is the
 *	same as the server name is also acceptable in the host field of a
 *	C/N line.
 *  0 = Success
 * -1 = Access denied
 * -2 = Bad socket.
 */
int	check_server_init(aClient *cptr)
{
  char	*name;
  aConfItem *c_conf = NULL, *n_conf = NULL;
  struct	hostent	*hp = NULL;
  Link	*lp;

  name = cptr->name;
  Debug((DEBUG_DNS, "sv_cl: check access for %s[%s]",
	 name, cptr->sockhost));

  if (IsUnknown(cptr) && !attach_confs(cptr, name, CONF_CONNECT_SERVER |
				       CONF_NOCONNECT_SERVER ))
    {
      Debug((DEBUG_DNS,"No C/N lines for %s", name));
      return -1;
    }
  lp = cptr->confs;
  /*
   * We initiated this connection so the client should have a C and N
   * line already attached after passing through the connect_server()
   * function earlier.
   */
  if (IsConnecting(cptr) || IsHandshake(cptr))
    {
      c_conf = find_conf(lp, name, CONF_CONNECT_SERVER);
      n_conf = find_conf(lp, name, CONF_NOCONNECT_SERVER);
      if (!c_conf || !n_conf)
	{
	  sendto_realops_lev(DEBUG_LEV, "Connecting Error: %s[%s]", name,
			     cptr->sockhost);
	  det_confs_butmask(cptr, 0);
	  return -1;
	}
    }
  /*
  ** If the servername is a hostname, either an alias (CNAME) or
  ** real name, then check with it as the host. Use gethostbyname()
  ** to check for servername as hostname.
  */
  if (!cptr->hostp)
    {
      aConfItem *aconf;

      aconf = count_cnlines(lp);
      if (aconf)
	{
	  Link	lin;
	  
	  /*
	  ** Do a lookup for the CONF line *only* and not
	  ** the server connection else we get stuck in a
	  ** nasty state since it takes a SERVER message to
	  ** get us here and we cant interrupt that very
	  ** well.
	  */
	  ClearAccess(cptr);
	  lin.value.aconf = aconf;
	  lin.flags = ASYNC_CONF;
	  nextdnscheck = 1;
	  Debug((DEBUG_DNS,"sv_ci:cache lookup (%s)",aconf->host));
	  hp = gethost_byname(aconf->host, &lin);
	}
    }
  return check_server(cptr, hp, c_conf, n_conf, 0);
}

int	check_server(aClient *cptr,
		     struct hostent *hp,
		     aConfItem *c_conf,
		     aConfItem *n_conf,
		     int estab)
{
  char	*name;
  char	sockname[HOSTLEN+1];
  Link	*lp = cptr->confs;
  int	i;

  ClearAccess(cptr);
  if (check_init(cptr, sockname))
    return -2;

  if (hp)
    {
      for (i = 0; hp->h_addr_list[i]; i++)
	if (!bcmp(hp->h_addr_list[i], (char *)&cptr->ip,
		  sizeof(struct in_addr)))
	  break;

      if (!hp->h_addr_list[i])
	{
	  sendto_realops_lev(DEBUG_LEV, "Server IP# Mismatch: %s != %s[%08x]",
			     inetntoa((char *)&cptr->ip), hp->h_name,
			     *((unsigned long *)hp->h_addr));
	  hp = NULL;
	}
    }
  else if (cptr->hostp)
    {
      hp = cptr->hostp;
      for (i = 0; hp->h_addr_list[i]; i++)
	if (!bcmp(hp->h_addr_list[i], (char *)&cptr->ip,
		  sizeof(struct in_addr)))
	  break;
    }

  if (hp)
    {
      /*
       * if we are missing a C or N line from above, search for
       * it under all known hostnames we have for this ip#.
       */
      for (i=0,name = hp->h_name; name ; name = hp->h_aliases[i++])
	{
	  Debug((DEBUG_DNS, "sv_cl: gethostbyaddr: %s->%s",
		 sockname, name));

	  if (!c_conf)
	    c_conf = find_conf_host(lp, name, CONF_CONNECT_SERVER );
	  if (!n_conf)
	    n_conf = find_conf_host(lp, name, CONF_NOCONNECT_SERVER );
	  if (c_conf && n_conf)
	    {
	      get_sockhost(cptr, name);
	      break;
	    }
	}
    }

  name = cptr->name;

  /*
   * Check for C and N lines with the hostname portion the ip number
   * of the host the server runs on. This also checks the case where
   * there is a server connecting from 'localhost'.
   */
  if (IsUnknown(cptr) && (!c_conf || !n_conf))
    {
      if (!c_conf)
	c_conf = find_conf_host(lp, sockname, CONF_CONNECT_SERVER);
      if (!n_conf)
	n_conf = find_conf_host(lp, sockname, CONF_NOCONNECT_SERVER);
    }
  /*
   * Attach by IP# only if all other checks have failed.
   * It is quite possible to get here with the strange things that can
   * happen when using DNS in the way the irc server does. -avalon
   */
  if (!hp)
    {
      if (!c_conf)
	c_conf = find_conf_ip(lp, (char *)&cptr->ip,
			      cptr->username, CONF_CONNECT_SERVER);
      if (!n_conf)
	n_conf = find_conf_ip(lp, (char *)&cptr->ip,
			      cptr->username, CONF_NOCONNECT_SERVER);
    }
  else
    for (i = 0; hp->h_addr_list[i]; i++)
      {
	if (!c_conf)
	  c_conf = find_conf_ip(lp, hp->h_addr_list[i],
				cptr->username, CONF_CONNECT_SERVER);
	if (!n_conf)
	  n_conf = find_conf_ip(lp, hp->h_addr_list[i],
				cptr->username, CONF_NOCONNECT_SERVER);
      }
  /*
   * detach all conf lines that got attached by attach_confs()
   */
  det_confs_butmask(cptr, 0);
  /*
   * if no C or no N lines, then deny access
   */
  if (!c_conf || !n_conf)
    {
      get_sockhost(cptr, sockname);
      Debug((DEBUG_DNS, "sv_cl: access denied: %s[%s@%s] c %x n %x",
	     name, cptr->username, cptr->sockhost,
	     c_conf, n_conf));
      return -1;
    }
  /*
   * attach the C and N lines to the client structure for later use.
   */
  (void)attach_conf(cptr, n_conf);
  (void)attach_conf(cptr, c_conf);
  (void)attach_confs(cptr, name, CONF_HUB|CONF_LEAF);
  
  if ((c_conf->ipnum.s_addr == -1))
    (void)memcpy((void *)&c_conf->ipnum, (void *)&cptr->ip, 
	  sizeof(struct in_addr));
  get_sockhost(cptr, c_conf->host);
  
  Debug((DEBUG_DNS,"sv_cl: access ok: %s[%s]",
	 name, cptr->sockhost));
  if (estab)
    return m_server_estab(cptr);
  return 0;
}

/*
** completed_connection
**	Complete non-blocking connect()-sequence. Check access and
**	terminate connection, if trouble detected.
**
**	Return	TRUE, if successfully completed
**		FALSE, if failed and ClientExit
*/
static	int completed_connection(aClient *cptr)
{
  aConfItem *c_conf;
  aConfItem *n_conf;

  SetHandshake(cptr);
	
  c_conf = find_conf(cptr->confs, cptr->name, CONF_CONNECT_SERVER);
  if (!c_conf)
    {
      sendto_realops("Lost C-Line for %s", get_client_name(cptr,FALSE));
      return -1;
    }
  if (!BadPtr(c_conf->passwd))
    sendto_one(cptr, "PASS %s :TS", c_conf->passwd);
  
  n_conf = find_conf(cptr->confs, cptr->name, CONF_NOCONNECT_SERVER);
  if (!n_conf)
    {
      sendto_realops("Lost N-Line for %s", get_client_name(cptr,FALSE));
      return -1;
    }
  
  send_capabilities(cptr, (c_conf->flags & CONF_FLAGS_ZIP_LINK));

  sendto_one(cptr, "SERVER %s 1 :%s",
	     my_name_for_link(me.name, n_conf), me.info);

  return (IsDead(cptr)) ? -1 : 0;
}

/*
** close_connection
**	Close the physical connection. This function must make
**	MyConnect(cptr) == FALSE, and set cptr->from == NULL.
*/
void	close_connection(aClient *cptr)
{
  aConfItem *aconf;

  if (IsServer(cptr))
    {
      ircstp->is_sv++;
      ircstp->is_sbs += cptr->sendB;
      ircstp->is_sbr += cptr->receiveB;
      ircstp->is_sks += cptr->sendK;
      ircstp->is_skr += cptr->receiveK;
      ircstp->is_sti += timeofday - cptr->firsttime;
      if (ircstp->is_sbs > 2047)
	{
	  ircstp->is_sks += (ircstp->is_sbs >> 10);
	  ircstp->is_sbs &= 0x3ff;
	}
      if (ircstp->is_sbr > 2047)
	{
	  ircstp->is_skr += (ircstp->is_sbr >> 10);
	  ircstp->is_sbr &= 0x3ff;
	}
    }
  else if (IsClient(cptr))
    {
      ircstp->is_cl++;
      ircstp->is_cbs += cptr->sendB;
      ircstp->is_cbr += cptr->receiveB;
      ircstp->is_cks += cptr->sendK;
      ircstp->is_ckr += cptr->receiveK;
      ircstp->is_cti += timeofday - cptr->firsttime;
      if (ircstp->is_cbs > 2047)
	{
	  ircstp->is_cks += (ircstp->is_cbs >> 10);
	  ircstp->is_cbs &= 0x3ff;
	}
      if (ircstp->is_cbr > 2047)
	{
	  ircstp->is_ckr += (ircstp->is_cbr >> 10);
	  ircstp->is_cbr &= 0x3ff;
	}
    }
  else
    ircstp->is_ni++;
  
  /*
   * remove outstanding DNS queries.
   */
  del_queries((char *)cptr);
  /*
   * If the connection has been up for a long amount of time, schedule
   * a 'quick' reconnect, else reset the next-connect cycle.
   */
  if ((aconf = find_conf_exact(cptr->name, cptr->username,
			       cptr->sockhost, CONF_CONNECT_SERVER)))
    {
      /*
       * Reschedule a faster reconnect, if this was a automatically
       * connected configuration entry. (Note that if we have had
       * a rehash in between, the status has been changed to
       * CONF_ILLEGAL). But only do this if it was a "good" link.
       */
      aconf->hold = time(NULL);
      aconf->hold += (aconf->hold - cptr->since > HANGONGOODLINK) ?
	HANGONRETRYDELAY : ConfConFreq(aconf);
      if (nextconnect > aconf->hold)
	nextconnect = aconf->hold;
    }
  
  if (cptr->authfd >= 0)
    {
      delfrom_fdlist(cptr->authfd,&default_fdlist);
      (void)close(cptr->authfd);
      cptr->authfd = -1;
    }

  if (cptr->fd >= 0)
    {
      flush_connections(cptr->fd);
      local[cptr->fd] = NULL;
#ifdef ZIP_LINKS
	/*
	** the connection might have zip data (even if
       	** FLAGS2_ZIP is not set)
	*/
      if (IsServer(cptr) || IsListening(cptr))
	zip_free(cptr);
#endif
      delfrom_fdlist(cptr->fd,&default_fdlist);
      (void)close(cptr->fd);
      cptr->fd = -2;
      DBufClear(&cptr->sendQ);
      DBufClear(&cptr->recvQ);
      memset((void *)cptr->passwd, 0, sizeof(cptr->passwd));
      /*
       * clean up extra sockets from P-lines which have been
       * discarded.
       */
      if (cptr->acpt != &me && cptr->acpt != cptr)
	{
	  aconf = cptr->acpt->confs->value.aconf;
	  if (aconf->clients > 0)
	    aconf->clients--;
	  if (!aconf->clients && IsIllegal(aconf))
	    close_connection(cptr->acpt);
	}
    }

  for (; highest_fd > 0; highest_fd--)
    if (local[highest_fd])
      break;

  det_confs_butmask(cptr, 0);
  cptr->from = NULL; /* ...this should catch them! >:) --msa */

  return;
}
#ifdef MAXBUFFERS
/*
 * reset_sock_opts
 *  type =  0 = client, 1 = server
 */
/*
 * If there is a failure on setsockopt, that shouldn't go to all realops
 * only ones that care. logically, its to do with connecting clients
 * so, putting it on CCONN_LEV seems logical to me - Dianora
 */
void	reset_sock_opts(int fd,int type)
{
#define CLIENT_BUFFER_SIZE	4096
#define SEND_BUF_SIZE		2048
  int opt;
  opt = type ? rcvbufmax : CLIENT_BUFFER_SIZE;

  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&opt, sizeof(opt)) < 0)
    sendto_realops_lev(CCONN_LEV,
		       "REsetsockopt(SO_RCVBUF) for fd %d (%s) failed", fd, type ? "server" : "client");
  opt = type ? (SEND_BUF_SIZE*4) : SEND_BUF_SIZE;

  if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&opt, sizeof(opt)) < 0)
    sendto_realops_lev(CCONN_LEV,
		       "REsetsockopt(SO_SNDBUF) for fd %d (%s) failed", fd, type ? "server" : "client");
}
#endif /* MAXBUFFERS */

/*
** set_sock_opts
*/
static	void	set_sock_opts(int fd, aClient *cptr)
{
  int	opt;

#ifdef SO_REUSEADDR
  opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0)
    report_error("setsockopt(SO_REUSEADDR) %s:%s", cptr);
#endif
#if  defined(SO_DEBUG) && defined(DEBUGMODE) && 0
/* Solaris with SO_DEBUG writes to syslog by default */
#if !defined(SOL20) || defined(USE_SYSLOG)
  opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_DEBUG, (char *)&opt, sizeof(opt)) < 0)
    report_error("setsockopt(SO_DEBUG) %s:%s", cptr);
#endif /* SOL20 */
#endif
#if defined(SO_USELOOPBACK) && !defined(__CYGWIN__)
  opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_USELOOPBACK, (char *)&opt, sizeof(opt)) < 0)
    report_error("setsockopt(SO_USELOOPBACK) %s:%s", cptr);
#endif
#ifdef	SO_RCVBUF
# if defined(MAXBUFFERS) && !defined(SEQUENT)
  if (rcvbufmax==0)
    {
#ifdef ZIP_LINKS
      rcvbufmax = READBUF_SIZE;  /* the zlib part needs buffers to be at least
				    this big to make things fit, and not bigger
				    for interoperatibility...  anyway 16k is
				    not a bad value  -orabidoo
				  */
#else
      int optlen;
      optlen = sizeof(rcvbufmax);
      getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &rcvbufmax,
		 &optlen);
      while((rcvbufmax<16385)&&(setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
		  (char *) (char *)&rcvbufmax, optlen) >= 0)) rcvbufmax+=1024;
      getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *) &rcvbufmax,
		 &optlen);
#endif
      readbuf = (char *)malloc(rcvbufmax * sizeof(char));
    }
  if (IsServer(cptr))
    opt = rcvbufmax;
  else
    opt = 4096;
# else
    opt = 8192;
# endif
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&opt, sizeof(opt)) < 0)
      report_error("setsockopt(SO_RCVBUF) %s:%s", cptr);
#endif
#ifdef	SO_SNDBUF
# ifdef	_SEQUENT_
/* seems that Sequent freezes up if the receving buffer is a different size
 * to the sending buffer (maybe a tcp window problem too).
 */
    opt = 8192;
# else
#  if defined(MAXBUFFERS) && !defined(SEQUENT)
    if (sndbufmax==0)
      {
	int optlen;
	optlen = sizeof(sndbufmax);
	getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &sndbufmax,
		   &optlen);
	while((sndbufmax<16385)&&(setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
		       (char *) &sndbufmax, optlen) >= 0)) sndbufmax+=1024;
	getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *) &sndbufmax,
		   &optlen);
      }
    if (IsServer(cptr))
      opt = sndbufmax;
    else
      opt = 4096;
#  else
      opt = 8192;
#  endif
# endif
      if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&opt, sizeof(opt)) < 0)
	report_error("setsockopt(SO_SNDBUF) %s:%s", cptr);
#endif
#if defined(IP_OPTIONS) && defined(IPPROTO_IP)
        {
# if defined(MAXBUFFERS) && !defined(SEQUENT)
	  char	*s = readbuf, *t = readbuf + (rcvbufmax*sizeof(char))/ 2;
	  opt = (rcvbufmax*sizeof(char))/8;
# else
	  char	*s = readbuf, *t = readbuf + sizeof(readbuf) / 2;
	  opt = sizeof(readbuf) / 8;
# endif
	  if (getsockopt(fd, IPPROTO_IP, IP_OPTIONS, t, &opt) < 0)
	    report_error("getsockopt(IP_OPTIONS) %s:%s", cptr);
	  else if (opt > 0)
	    {
	      for (*readbuf = '\0'; opt > 0; opt--, s+= 3)
		(void)ircsprintf(s, "%02.2x:", *t++);
	      *s = '\0';
	      sendto_realops("Connection %s using IP opts: (%s)",
			     get_client_name(cptr, TRUE), readbuf);
	    }
	  if (setsockopt(fd, IPPROTO_IP, IP_OPTIONS, (char *)NULL, 0) < 0)
	    report_error("setsockopt(IP_OPTIONS) %s:%s", cptr);
	}
#endif
}


int	get_sockerr(aClient *cptr)
{
  int errtmp = errno, err = 0, len = sizeof(err);
#ifdef	SO_ERROR
  if (cptr->fd >= 0)
    if (!getsockopt(cptr->fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len))
      if (err)
	errtmp = err;
#endif
  return errtmp;
}

/*
** set_non_blocking
**	Set the client connection into non-blocking mode. If your
**	system doesn't support this, you can make this a dummy
**	function (and get all the old problems that plagued the
**	blocking version of IRC--not a problem if you are a
**	lightly loaded node...)
*/
void	set_non_blocking(int fd,aClient *cptr)
{
  int	res, nonb = 0;

  /*
  ** NOTE: consult ALL your relevant manual pages *BEFORE* changing
  **	 these ioctl's.  There are quite a few variations on them,
  **	 as can be seen by the PCS one.  They are *NOT* all the same.
  **	 Heed this well. - Avalon.
  */
#ifdef	NBLOCK_POSIX
  nonb |= O_NONBLOCK;
#endif
#ifdef	NBLOCK_BSD
  nonb |= O_NDELAY;
#endif
#ifdef	NBLOCK_SYSV
  /* This portion of code might also apply to NeXT.  -LynX */
  res = 1;

  if (ioctl (fd, FIONBIO, &res) < 0)
    report_error("ioctl(fd,FIONBIO) failed for %s:%s", cptr);
#else
  if ((res = fcntl(fd, F_GETFL, 0)) == -1)
    report_error("fcntl(fd, F_GETFL) failed for %s:%s",cptr);
  else if (fcntl(fd, F_SETFL, res | nonb) == -1)
    report_error("fcntl(fd, F_SETL, nonb) failed for %s:%s",cptr);
#endif
  return;
}

/*
 * Creates a client which has just connected to us on the given fd.
 * The sockhost field is initialized with the ip# of the host.
 * The client is added to the linked list of clients but isnt added to any
 * hash tables yet since it doesnt have a name.
 */
aClient	*add_connection(aClient *cptr, int fd)
{
  Link	lin;
  aClient *acptr;
  aConfItem *aconf = NULL;
  acptr = make_client(NULL);

  if (cptr != &me)
    aconf = cptr->confs->value.aconf;
  /* Removed preliminary access check. Full check is performed in
   * m_server and m_user instead. Also connection time out help to
   * get rid of unwanted connections.
   */

    {
      struct	sockaddr_in addr;
      int	len = sizeof(struct sockaddr_in);

      if (getpeername(fd, (struct sockaddr *) &addr, &len) == -1)
	{
	  report_error("Failed in connecting to %s :%s", cptr);
	  ircstp->is_ref++;
	  delfrom_fdlist(fd,&default_fdlist);
	  acptr->fd = -2;
	  free_client(acptr);
	  (void)close(fd);
	  return NULL;
	}
      /* don't want to add "Failed in connecting to" here.. */
      if (aconf && IsIllegal(aconf))
	{
	  ircstp->is_ref++;
	  delfrom_fdlist(fd,&default_fdlist);
	  acptr->fd = -2;
	  free_client(acptr);
	  (void)close(fd);
	  return NULL;
	}
      /* Copy ascii address to 'sockhost' just in case. Then we
       * have something valid to put into error messages...
       */
      get_sockhost(acptr, (char *)inetntoa((char *)&addr.sin_addr));
      (void)memcpy ( (void *)&acptr->ip, (void *)&addr.sin_addr,
	     sizeof(struct in_addr));
      acptr->port = ntohs(addr.sin_port);

      /* can't use sendheader in this section since acptr->fd hasn't
       * been set yet, must use fd -Dianora/JailBird
       */

#ifdef SHOW_HEADERS
      /* sendheader(acptr, REPORT_DO_DNS, R_do_dns); */
      if(IsUnknown(acptr))
	send(fd, REPORT_DO_DNS, R_do_dns, 0);
#endif
      lin.flags = ASYNC_CLIENT;
      lin.value.cptr = acptr;
      Debug((DEBUG_DNS, "lookup %s",
	     inetntoa((char *)&addr.sin_addr)));
      acptr->hostp = gethost_byaddr((char *)&acptr->ip, &lin);
      if (!acptr->hostp)
	SetDNS(acptr);
#ifdef SHOW_HEADERS
      else
	/*	sendheader(acptr, REPORT_FIN_DNSC, R_fin_dnsc); */
	if(IsUnknown(acptr))
	  send(fd, REPORT_FIN_DNSC, R_fin_dnsc, 0);
#endif
      nextdnscheck = 1;
    }

  if (aconf)
    aconf->clients++;
  acptr->fd = fd;
  if (fd > highest_fd)
    highest_fd = fd;

  local[fd] = acptr;

  acptr->acpt = cptr;
  add_client_to_list(acptr);
  set_non_blocking(acptr->fd, acptr);
  set_sock_opts(acptr->fd, acptr);
#ifdef DO_IDENTD
  start_auth(acptr);
#endif
  return acptr;
}

/*
** read_packet
**
** Read a 'packet' of data from a connection and process it.  Read in 8k
** chunks to give a better performance rating (for server connections).
** Do some tricky stuff for client connections to make sure they don't do
** any flooding >:-) -avalon
*/

int read_packet(aClient *cptr, int msg_ready)
{
  int	dolen = 0, length = 0, done;

  if (  msg_ready &&
      !(IsPerson(cptr) && DBufLength(&cptr->recvQ) > 6090))
      {
	errno = 0;

#if defined(MAXBUFFERS) && !defined(SEQUENT)
	if (IsPerson(cptr))
	  length = recv(cptr->fd, readbuf, 8192*sizeof(char), 0);
	else
	  length = recv(cptr->fd, readbuf, rcvbufmax*sizeof(char), 0);
#else
	length = recv(cptr->fd, readbuf, sizeof(readbuf), 0);
#endif

	
#ifdef REJECT_HOLD

	/* If client has been marked as rejected i.e. it is a client
	 * that is trying to connect again after a k-line,
	 * pretend to read it but don't actually.
	 * -Dianora
	 */

	/* FLAGS_REJECT_HOLD should NEVER be set for non local client */
	if(IsRejectHeld(cptr))
	  return 1;
#endif

	cptr->lasttime = timeofday;
	if (cptr->lasttime > cptr->since)
	  cptr->since = cptr->lasttime;
	cptr->flags &= ~(FLAGS_PINGSENT|FLAGS_NONL);
	/*
	 * If not ready, fake it so it isnt closed
	 */
	if (length == -1 &&
	    ((errno == EWOULDBLOCK) || (errno == EAGAIN)))
	  return 1;
	if (length <= 0)
	  return length;
      }

      /*
      ** For server connections, we process as many as we can without
      ** worrying about the time of day or anything :)
      */
      if (IsServer(cptr) || IsConnecting(cptr) || IsHandshake(cptr))
      
      {
	if (length > 0)
	  if ((done = dopacket(cptr, readbuf, length)))
	    return done;
      }
      else
      {
	/*
	** Before we even think of parsing what we just read, stick
	** it on the end of the receive queue and do it when its
	** turn comes around.
	*/
	if (dbuf_put(&cptr->recvQ, readbuf, length) < 0)
	  return exit_client(cptr, cptr, cptr, "dbuf_put fail");
	
	if (IsPerson(cptr) &&
#ifdef NO_OPER_FLOOD
	    !IsAnOper(cptr) &&
#endif
	    DBufLength(&cptr->recvQ) > CLIENT_FLOOD)
	  return exit_client(cptr, cptr, cptr, "Excess Flood");

	while (DBufLength(&cptr->recvQ) && !NoNewLine(cptr) &&
	       ((cptr->status < STAT_UNKNOWN) ||
		(cptr->since - timeofday < 10)))
	  {
	    /*
	    ** If it has become registered as a Server
	    ** then skip the per-message parsing below.
	    */
	    if (IsServer(cptr))
	      {
		/* This is actually useful, but it needs the ZIP_FIRST
		** kludge or it will break zipped links  -orabidoo
		*/

#if defined(MAXBUFFERS) && !defined(SEQUENT)
		dolen = dbuf_get(&cptr->recvQ, readbuf,
				 rcvbufmax*sizeof(char));
#else
		dolen = dbuf_get(&cptr->recvQ, readbuf,
				 sizeof(readbuf));
#endif
		if (dolen <= 0)
		  break;
		if ((done = dopacket(cptr, readbuf, dolen)))
		  return done;
		break;
	      }
#if defined(MAXBUFFERS) && !defined(SEQUENT)
	    dolen = dbuf_getmsg(&cptr->recvQ, readbuf,
				rcvbufmax*sizeof(char));
#else
	    dolen = dbuf_getmsg(&cptr->recvQ, readbuf,
				sizeof(readbuf));
#endif
	    /*
	    ** Devious looking...whats it do ? well..if a client
	    ** sends a *long* message without any CR or LF, then
	    ** dbuf_getmsg fails and we pull it out using this
	    ** loop which just gets the next 512 bytes and then
	    ** deletes the rest of the buffer contents.
	    ** -avalon
	    */
	    while (dolen <= 0)
	      {
		if (dolen < 0)
		  return exit_client(cptr, cptr, cptr,
				     "dbuf_getmsg fail");
		if (DBufLength(&cptr->recvQ) < 510)
		  {
		    cptr->flags |= FLAGS_NONL;
		    break;
		  }
		dolen = dbuf_get(&cptr->recvQ, readbuf, 511);
		if (dolen > 0 && DBufLength(&cptr->recvQ))
		  DBufClear(&cptr->recvQ);
	      }
      
	    if (dolen > 0 &&
		(dopacket(cptr, readbuf, dolen) == FLUSH_BUFFER))
	      return FLUSH_BUFFER;
	  }
      }
      return 1;
}

/*
 * Check all connections for new connections and input data that is to be
 * processed. Also check for connections with data queued and whether we can
 * write it out.
 */
#ifndef USE_POLL
  int	read_message(time_t delay,

		     /* Don't ever use ZERO here, unless you mean to poll
			and then you have to have sleep/wait somewhere 
			else in the code.--msa
			*/
		     fdlist *listp)	/* mika */
{
  aClient	*cptr;
  int	nfds;
  struct	timeval	wait;
#ifdef	pyr
  struct	timeval	nowt;
  u_long	us;
#endif
  time_t	delay2 = delay, now;
  u_long	usec = 0;
  int	res, length, fd;
  int	auth = 0;
  register int i;
  int rr;
  time_t	last_full_to_opers_notice = (time_t)0;
  aConfItem     *found_conf;

#ifdef	pyr
  (void) gettimeofday(&nowt, NULL);
  now = nowt.tv_sec;
#else
  now = timeofday;
#endif

  /* if it is called with NULL we check all active fd's */
  if(!listp)
    {
      listp = &default_fdlist;
    }

  for (res = 0;;)
    {
      FD_ZERO(read_set);
      FD_ZERO(write_set);

      for (i=0; i<=highest_fd; i++)
	{
	  if(!listp->entry[i])
	    continue;

	  if (!(cptr = local[i]))
	    continue;
	  if (IsLog(cptr))
	    continue;
	  if (DoingAuth(cptr))
	    {
	      auth++;
	      Debug((DEBUG_NOTICE,"auth on %x %d", cptr, i));
	      FD_SET(cptr->authfd, read_set);
	      if (cptr->flags & FLAGS_WRAUTH)
		FD_SET(cptr->authfd, write_set);
	    }
	  if (DoingDNS(cptr) || DoingAuth(cptr))
	    continue;
	  if (IsMe(cptr) && IsListening(cptr))
	    {
	      FD_SET(i, read_set);
	    }
	  else if (!IsMe(cptr))
	    {
	      if (DBufLength(&cptr->recvQ) && delay2 > 2)
		delay2 = 1;
	      if (DBufLength(&cptr->recvQ) < 4088)	
		{
		  FD_SET(i, read_set);
		}
	    }

	  if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
#ifdef ZIP_LINKS
	      || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
#endif
	      )
#ifndef	pyr
	  FD_SET(i, write_set);
#else
	  {
	    if (!(cptr->flags & FLAGS_BLOCKED))
	      {
		FD_SET(i, write_set);
	      }
	    else
	      delay2 = 0, usec = 500000;
	  }
	  if (now - cptr->lw.tv_sec &&
	      nowt.tv_usec - cptr->lw.tv_usec < 0)
	    us = 1000000;
	  else
	    us = 0;
	  us += nowt.tv_usec;
	  if (us - cptr->lw.tv_usec > 500000)
	    cptr->flags &= ~FLAGS_BLOCKED;
#endif
	}
      
      if (resfd >= 0)
	{
	  FD_SET(resfd, read_set);
	}
      wait.tv_sec = MIN(delay2, delay);
      wait.tv_usec = usec;

#ifdef	HPUX
      nfds = select(FD_SETSIZE, (fd_set *)read_set, (fd_set *)write_set,
		    (fd_set *)0, &wait);
#else
      nfds = select(MAXCONNECTIONS, read_set, write_set,
		    0, &wait);
#endif

      if((timeofday = time(NULL)) == -1)
        {
#ifdef USE_SYSLOG
          syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
          sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
        }   

      if (nfds == -1 && errno == EINTR)
	{
	  return -1;
	}
      else if( nfds >= 0)
	break;

      res++;
      if (res > 5)
	restart("too many select errors");
      sleep(10);
    }

  /*
   * Check the name resolver
   */

  if (resfd >= 0 && FD_ISSET(resfd, read_set))
    {
      do_dns_async();
      nfds--;
      FD_CLR(resfd, read_set);
    }

  for (i=0; i<=highest_fd; i++)
    {
      if(!listp->entry[i])
	continue;

      if (!(cptr = local[i]))
	continue;
  
      if(IsLog(cptr))
	continue;
    
      /*
       * Check the auth fd's first...
       */
      if ((auth>0) && (cptr->authfd >= 0))
	{
	  auth--;
	  if ((nfds > 0) && FD_ISSET(cptr->authfd, write_set))
	    {
	      nfds--;
	      send_authports(cptr);
	    }
	  else if ((nfds > 0) && FD_ISSET(cptr->authfd, read_set))
	    {
	      nfds--;
	      read_authports(cptr);
	    }
	}

      /*
       * Now see if there's a connection pending...
       */

      if (IsListening(cptr) && FD_ISSET(i, read_set))
	{
	  static struct sockaddr_in	addr;
	  int addrlen = sizeof(struct sockaddr_in);
	  char host[HOSTLEN+2];

	  FD_CLR(i, read_set);
	  nfds--;
	  cptr->lasttime = timeofday;
	  /*
	  ** There may be many reasons for error return, but
	  ** in otherwise correctly working environment the
	  ** probable cause is running out of file descriptors
	  ** (EMFILE, ENFILE or others?). The man pages for
	  ** accept don't seem to list these as possible,
	  ** although it's obvious that it may happen here.
	  ** Thus no specific errors are tested at this
	  ** point, just assume that connections cannot
	  ** be accepted until some old is closed first.
	  */
	  if ((fd = accept(i, (struct sockaddr *)&addr, &addrlen)) < 0)
	    {
	      report_error("Cannot accept connections %s:%s",
			   cptr);
	      break;
	    }
	  strncpyzt(host,
		    (char *)inetntoa((char *)&addr.sin_addr),
		    sizeof(host));

	  found_conf = match_Dline(ntohl((unsigned long)addr.sin_addr.s_addr));

	  if (found_conf && !IsConfElined(found_conf))
	    {
	      ircstp->is_ref++;
#ifdef REPORT_DLINE_TO_USER
	      /* Can't use orabidoo's sendheader here
	       * and if I'm sending a D line to a server,
	       * well it can't connect anyway
	       * -Dianora
	       */
	      send(fd, REPORT_DLINED, sizeof(REPORT_DLINED)-1, 0);
#endif

	      (void)close(fd);
	      continue;
	    }

	    if (fd >= (HARD_FDLIMIT - 10))
	      {
		ircstp->is_ref++;
		/* slow down the whining to opers bit */
		if((last_full_to_opers_notice + 20) <= NOW)
		  {
		    sendto_realops("All connections in use. (%s)",
				   get_client_name(cptr, TRUE));
		    last_full_to_opers_notice = NOW;
		  }
		(void)send(fd,
			   "ERROR :All connections in use\r\n",
			   32, 0);
		(void)close(fd);
		break;
	      }
	    ircstp->is_ac++;
	    /*
	     * Use of add_connection (which never fails :) meLazy
	     */
	    addto_fdlist(fd, &default_fdlist);
	    (void)add_connection(cptr, fd);
	    nextping = timeofday;
	    if (!cptr->acpt)
	      cptr->acpt = &me;
	  }

      if (IsMe(cptr))
	continue;

      /*
       * See if we can write...
       */
      if (FD_ISSET(i, write_set))
	{
	  int	write_err = 0;
	  nfds--;

          /*
	  ** ...room for writing, empty some queue then...
	  */
	  if (IsConnecting(cptr))
	    write_err = completed_connection(cptr);
	  if (!write_err)
	    (void)send_queued(cptr);
	  if (IsDead(cptr) || write_err)
	    {
	      if (FD_ISSET(i, read_set))
		{
		  nfds--;
		  FD_CLR(i, read_set);
		}
	      (void)exit_client(cptr, cptr, &me,
				(cptr->flags & FLAGS_SENDQEX) ? 
				"SendQ Exceeded" :
				strerror(get_sockerr(cptr)));
	      continue;
	    }
	}
      length = 1;	/* for fall through case */
      rr = 0;
      rr = FD_ISSET(i, read_set);
      if (!NoNewLine(cptr) || rr)
	length = read_packet(cptr, rr);
#ifdef DEBUGMODE
       readcalls++;
#endif

      if ((length != FLUSH_BUFFER) && IsDead(cptr))
	{
	  if (FD_ISSET(i, read_set))
	    {
	      nfds--;
	      FD_CLR(i, read_set);
	    }
	  (void)exit_client(cptr, cptr, &me,
			    (cptr->flags & FLAGS_SENDQEX) ? "SendQ Exceeded" :
			    strerror(get_sockerr(cptr)));
	  continue;
	}
      if (!FD_ISSET(i, read_set) && length > 0)
	continue;
      nfds--;

      if (length > 0)
	continue;
      /*
      ** ...hmm, with non-blocking sockets we might get
      ** here from quite valid reasons, although.. why
      ** would select report "data available" when there
      ** wasn't... so, this must be an error anyway...  --msa
      ** actually, EOF occurs when read() returns 0 and
      ** in due course, select() returns that fd as ready
      ** for reading even though it ends up being an EOF. -avalon
      */
      Debug((DEBUG_ERROR, "READ ERROR: fd = %d %d %d",
	     i, errno, length));
    
      /*
      ** NOTE: if length == -2 then cptr has already been freed!
      */
      if (length != -2 && (IsServer(cptr) || IsHandshake(cptr)))
	{
	  if (length == 0)
	    sendto_ops("Server %s closed the connection",
		       get_client_name(cptr,FALSE));
	  else
	    report_error("Lost connection to %s:%s",
			 cptr);
	}
      if (length != FLUSH_BUFFER)
	{
	  char errmsg[255];
	  (void)ircsprintf(errmsg,"Read error: %d (%s)",errno,strerror(errno));
	  (void)exit_client(cptr, cptr, &me, errmsg);
	}
    }
  return 0;
}
  
#else /* USE_POLL */

#ifdef AIX
#define POLLREADFLAGS (POLLIN|POLLMSG)
#else
# if defined(POLLMSG) && defined(POLLIN) && defined(POLLRDNORM)
# define POLLREADFLAGS (POLLMSG|POLLIN|POLLRDNORM)
# else
#  if defined(POLLIN) && defined(POLLRDNORM)
#  define POLLREADFLAGS (POLLIN|POLLRDNORM)
#  else
#   if defined(POLLIN)
#   define POLLREADFLAGS POLLIN
#   else
#    if defined(POLLRDNORM)
#    define POLLREADFLAGS POLLRDNORM
#    endif
#   endif
#  endif
# endif
#endif

#if defined(POLLOUT) && defined(POLLWRNORM)
#define POLLWRITEFLAGS (POLLOUT|POLLWRNORM)
#else
# if defined(POLLOUT)
# define POLLWRITEFLAGS POLLOUT
# else
#  if defined(POLLWRNORM)
#  define POLLWRITEFLAGS POLLWRNORM
#  endif
# endif
#endif

#if defined(POLLERR) && defined(POLLHUP)
#define POLLERRORS (POLLERR|POLLHUP)
#else
#define POLLERRORS POLLERR
#endif

#define PFD_SETR( thisfd ){	CHECK_PFD( thisfd );\
				pfd->events |= POLLREADFLAGS;}
#define PFD_SETW( thisfd ){	CHECK_PFD( thisfd );\
				pfd->events |= POLLWRITEFLAGS;}
#define CHECK_PFD( thisfd )                     \
	if ( pfd->fd != thisfd ) {              \
		pfd = &poll_fdarray[nbr_pfds++];\
		pfd->fd     = thisfd;           \
		pfd->events = 0;                \
	}

int	read_message(time_t delay, fdlist *listp)
{
  aClient *cptr;
  int     nfds;
  struct	timeval wait;

  static struct pollfd	poll_fdarray[MAXCONNECTIONS];
  struct pollfd	*pfd     = poll_fdarray;
  struct pollfd	*res_pfd = NULL;
  int		nbr_pfds = 0;
  time_t	delay2 = delay;
  u_long	usec = 0;
  int		res, length, fd, newfd;
  int		auth, rr, rw;
  register	int i,j;
  static aClient	*authclnts[MAXCONNECTIONS];
  char		errmsg[255];
  aConfItem     *found_conf;

  /* if it is called with NULL we check all active fd's */
  if (!listp)
    {
      listp = &default_fdlist;
    }

  for (res = 0;;)
    {
      nbr_pfds = 0;
      pfd      = poll_fdarray;
      pfd->fd  = -1;
      res_pfd  = NULL;
      auth = 0;

      for (i=0; j<=highest_fd;i++)
	{
	  if(!listp->entry[i])
	    continue;

	  if (!(cptr = local[i]))
	    continue;
	  if (!
	  if (IsLog(cptr))
	    continue;
	  if (DoingAuth(cptr))
	    {
	      if (auth == 0)
		memset( (void *)&authclnts, 0, sizeof(authclnts) );
	      auth++;
	      Debug((DEBUG_NOTICE,"auth on %x %d", cptr, i));
	      PFD_SETR(cptr->authfd);
	      if (IsWriteAuth(cptr))
		PFD_SETW(cptr->authfd);
	      authclnts[cptr->authfd] = cptr;
	      continue;
	    }
	  if (DoingDNS(cptr) || DoingAuth(cptr))
	    continue;
	  if (IsMe(cptr) && IsListening(cptr))
	    {
#if defined(SOL20) || defined(AIX)
#define CONNECTFAST
#endif

#ifdef CONNECTFAST
	      /* next line was 2, changing to 1 */
	      /* if we dont have many clients just let em on */
	      /* This is VERY bad if someone tries to send a lot
		 of clones to the server though, as mbuf's can't
		 be allocated quickly enough... - Comstud */
	      if (1)
#else
		if (timeofday > (cptr->lasttime + 2))
#endif
		  {
		    PFD_SETR(i);
		  }
		else if (delay2 > 2)
		  delay2 = 2;
	    }
	  else if (!IsMe(cptr))
	    {
	      if (DBufLength(&cptr->recvQ) && delay2 > 2)
		delay2 = 1;
	      if (DBufLength(&cptr->recvQ) < 4088)
		PFD_SETR(i);
	    }
	  
	  if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
#ifdef ZIP_LINKS
	      || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
#endif
	      )
	    PFD_SETW(i);
	}

      if (resfd >= 0)
	{
	  PFD_SETR(resfd);
	  res_pfd = pfd;
	}
      wait.tv_sec = MIN(delay2, delay);
      wait.tv_usec = usec;
      nfds = poll(poll_fdarray, nbr_pfds,
		  wait.tv_sec*1000 + wait.tv_usec/1000);
      if (nfds == -1 && ((errno == EINTR) || (errno == EAGAIN)))
	return -1;
      else if (nfds >= 0)
	break;
      report_error("poll %s:%s", &me);
      res++;
      if (res > 5)
	restart("too many poll errors");
      sleep(10);
    }
  if (res_pfd && (res_pfd->revents & (POLLREADFLAGS|POLLERRORS)))
    {
      do_dns_async();
      nfds--;
    }
  for (pfd = poll_fdarray, i = 0; (nfds > 0) && (i < nbr_pfds);
       i++, pfd++)
    {
      if (!pfd->revents)
	continue;
      if (pfd == res_pfd)
	continue;
      nfds--;
      fd = pfd->fd;                   
      rr = pfd->revents & POLLREADFLAGS;
      rw = pfd->revents & POLLWRITEFLAGS;
      if (pfd->revents & POLLERRORS)
	{
	  if (pfd->events & POLLREADFLAGS)
	    rr++;
	  if (pfd->events & POLLWRITEFLAGS)
	    rw++;
	}
      if ((auth>0) && ((cptr=authclnts[fd]) != NULL) &&
	  (cptr->authfd == fd))
	{
	  auth--;
	  if (rr)
	    read_authports(cptr);
	  if (rw)
	    send_authports(cptr);
	  continue;
	}
      if (!(cptr = local[fd]))
	continue;
      if (rr && IsListening(cptr))
	{
	  static struct sockaddr_in	addr;
	  int addrlen = sizeof(struct sockaddr_in);
	  char host[HOSTLEN+2];
	  
	  rr = 0;
	  cptr->lasttime = timeofday;
	  /*
	  ** There may be many reasons for error return, but
	  ** in otherwise correctly working environment the
	  ** probable cause is running out of file descriptors
	  ** (EMFILE, ENFILE or others?). The man pages for
	  ** accept don't seem to list these as possible,
	  ** although it's obvious that it may happen here.
	  ** Thus no specific errors are tested at this
	  ** point, just assume that connections cannot
	  ** be accepted until some old is closed first.
	  */
	  if ((newfd = accept(fd, (struct sockaddr *)&addr, &addrlen)) < 0)
	    {
#ifdef EPROTO
	      /* If a connection is closed before the accept(), it
		 returns EPROTO on Solaris. */
	      if (errno != EPROTO)
#endif
		report_error("Cannot accept connections %s:%s", cptr);
	      break;
	    }
	  strncpyzt(host,
		    (char *)inetntoa((char *)&addr.sin_addr),
		    sizeof(host));

	  found_conf = match_Dline(ntohl((unsigned long)addr.sin_addr.s_addr));

	  if (found_conf && !IsConfElined(found_conf))
	    {
	      ircstp->is_ref++;
#ifdef REPORT_DLINE_TO_USER
	      /* can't use orabidoo's sendheader here */
	      send(fd, REPORT_DLINED, strlen(REPORT_DLINED), 0);
#endif
	      (void)close(newfd);
	      continue;
	    }

	  if (newfd >= HARD_FDLIMIT - 10)
	    {
	      ircstp->is_ref++;
	      sendto_ops("All connections in use. (%s)",
			 get_client_name(cptr, TRUE));
	      (void)send(newfd, "ERROR :All connections in use\r\n", 32, 0);
	      (void)close(newfd);
	      break;
	    }
	  ircstp->is_ac++;
	  /*
	   * Use of add_connection (which never fails :) meLazy
	   */
	  (void)add_connection(cptr, newfd);
	  nextping = timeofday;
	  if (!cptr->acpt)
	    cptr->acpt = &me;
	  continue;
	}
      if (IsMe(cptr))
	continue;
      if (rw)
	{
	  int     write_err = 0;
	  /*
	  ** ...room for writing, empty some queue then...
	  */
	  if (IsConnecting(cptr))
	    write_err = completed_connection(cptr);
	  if (!write_err)
	    (void)send_queued(cptr);
	  if (IsDead(cptr) || write_err)
	    {
	      (void)exit_client(cptr, cptr, &me,
				strerror(get_sockerr(cptr)));
	      continue;
	    }
	}
      length = 1;     /* for fall through case */
      if (!NoNewLine(cptr) || rr)
	length = read_packet(cptr, rr);
#ifdef DEBUGMODE
          readcalls++;
#endif
      if (length == FLUSH_BUFFER)
	continue;
      if (IsDead(cptr))
	{
	  (void)exit_client(cptr, cptr, &me,
			    strerror(get_sockerr(cptr)));
	  continue;
	}
      if (length > 0)
	continue;

      /*
      ** ...hmm, with non-blocking sockets we might get
      ** here from quite valid reasons, although.. why
      ** would select report "data available" when there
      ** wasn't... so, this must be an error anyway...  --msa
      ** actually, EOF occurs when read() returns 0 and
      ** in due course, select() returns that fd as ready
      ** for reading even though it ends up being an EOF. -avalon
      */
      Debug((DEBUG_ERROR, "READ ERROR: fd = %d %d %d",
	     fd, errno, length));
      if (IsServer(cptr) || IsHandshake(cptr))
	{
	  int connected = timeofday - cptr->firsttime;
	  
	  if (length == 0)
	    sendto_ops("Server %s closed the connection",
		       get_client_name(cptr,FALSE));
	  else
	    report_error("Lost connection to %s:%s", cptr);
	  sendto_ops("%s had been connected for %d day%s, %2d:%02d:%02d",
		     cptr->name, connected/86400,
		     (connected/86400 == 1) ? "" : "s",
		     (connected % 86400) / 3600, (connected % 3600) / 60,
		     connected % 60);
	}
      (void)ircsprintf(errmsg, "Read error: %d (%s)", errno,
		       strerror(errno));
      (void)exit_client(cptr, cptr, &me, errmsg);
    }
  return 0;
}

#endif /* USE_POLL */

/*
 * connect_server
 */
int	connect_server(aConfItem *aconf,
		       aClient *by,
		       struct hostent *hp)
{
  struct sockaddr *svp;
  aClient *cptr, *c2ptr;
  int	errtmp, len;

  Debug((DEBUG_NOTICE,"Connect to %s[%s] @%s",
	 aconf->user, aconf->host, inetntoa((char *)&aconf->ipnum)));

  if ((c2ptr = find_server(aconf->name, NULL)))
    {
      sendto_ops("Server %s already present from %s",
		 aconf->name, get_client_name(c2ptr, TRUE));
      if (by && IsPerson(by) && !MyClient(by))
	sendto_one(by,
		   ":%s NOTICE %s :Server %s already present from %s",
		   me.name, by->name, aconf->name,
		   get_client_name(c2ptr, TRUE));
      return -1;
    }

  /*
   * If we dont know the IP# for this host and it is a hostname and
   * not a ip# string, then try and find the appropriate host record.
   */
  if ( ( !aconf->ipnum.s_addr ) )
    {
      Link    lin;
      
      lin.flags = ASYNC_CONNECT;
      lin.value.aconf = aconf;
      nextdnscheck = 1;
      if ((aconf->ipnum.s_addr = inet_addr(aconf->host)) == -1)
	{
	  aconf->ipnum.s_addr = 0;
	  hp = gethost_byname(aconf->host, &lin);
	  Debug((DEBUG_NOTICE, "co_sv: hp %x ac %x na %s ho %s",
		 hp, aconf, aconf->name, aconf->host));
	  if (!hp)
	    return 0;
	  (void)memcpy( (void *)&aconf->ipnum, (void *)hp->h_addr,
		sizeof(struct in_addr));
	}
    }
  cptr = make_client(NULL);
  cptr->hostp = hp;
  /*
   * Copy these in so we have something for error detection.
   */
  strncpyzt(cptr->name, aconf->name, sizeof(cptr->name));
  strncpyzt(cptr->sockhost, aconf->host, HOSTLEN+1);
  svp = connect_inet(aconf, cptr, &len);

  if (!svp)
    {
      if (cptr->fd != -1)
	{
	  (void)close(cptr->fd);
	}
      cptr->fd = -2;
      free_client(cptr);
      return -1;
    }

  set_non_blocking(cptr->fd, cptr);
  set_sock_opts(cptr->fd, cptr);
  (void)signal(SIGALRM, dummy);
  if (connect(cptr->fd, svp, len) < 0 && errno != EINPROGRESS)
    {
      errtmp = errno; /* other system calls may eat errno */
      report_error("Connect to host %s failed: %s",cptr);
      if (by && IsPerson(by) && !MyClient(by))
	sendto_one(by,
		   ":%s NOTICE %s :Connect to host %s failed.",
		   me.name, by->name, cptr);
      (void)close(cptr->fd);
      cptr->fd = -2;
      free_client(cptr);
      errno = errtmp;
      if (errno == EINTR)
	errno = ETIMEDOUT;
      return -1;
    }

  addto_fdlist(cptr->fd, &default_fdlist);

  /* Attach config entries to client here rather than in
   * completed_connection. This to avoid null pointer references
   * when name returned by gethostbyaddr matches no C lines
   * (could happen in 2.6.1a when host and servername differ).
   * No need to check access and do gethostbyaddr calls.
   * There must at least be one as we got here C line...  meLazy
   */
  (void)attach_confs_host(cptr, aconf->host,
	  CONF_NOCONNECT_SERVER | CONF_CONNECT_SERVER );

  if (!find_conf_host(cptr->confs, aconf->host, CONF_NOCONNECT_SERVER) ||
      !find_conf_host(cptr->confs, aconf->host, CONF_CONNECT_SERVER))
    {
      sendto_ops("Host %s is not enabled for connecting:no C/N-line",
		 aconf->host);
      if (by && IsPerson(by) && !MyClient(by))
	sendto_one(by,
		   ":%s NOTICE %s :Connect to host %s failed.",
		   me.name, by->name, cptr);
      det_confs_butmask(cptr, 0);
      (void)close(cptr->fd);
      cptr->fd = -2;
      free_client(cptr);
      return(-1);
    }
  /*
  ** The socket has been connected or connect is in progress.
  */
  (void)make_server(cptr);
  if (by && IsPerson(by))
    {
      (void)strcpy(cptr->serv->by, by->name);
      if (cptr->serv->user) free_user(cptr->serv->user, NULL);
      cptr->serv->user = by->user;
      by->user->refcnt++;
    } 
  else
    {
      (void)strcpy(cptr->serv->by, "AutoConn.");
      if (cptr->serv->user) free_user(cptr->serv->user, NULL);
      cptr->serv->user = NULL;
    }
  cptr->serv->up = me.name;
  if (cptr->fd > highest_fd)
    highest_fd = cptr->fd;
  local[cptr->fd] = cptr;
  cptr->acpt = &me;
  SetConnecting(cptr);

  get_sockhost(cptr, aconf->host);
  add_client_to_list(cptr);
  nextping = timeofday;
  return 0;
}

static	struct	sockaddr *connect_inet(aConfItem *aconf,
				       aClient *cptr,
				       int *lenp)
{
  static  struct sockaddr_in	server;
  struct	hostent	*hp;

  /*
   * Might as well get sockhost from here, the connection is attempted
   * with it so if it fails its useless.
   */
  cptr->fd = socket(AF_INET, SOCK_STREAM, 0);

  if (cptr->fd == -1)
    {
      report_error("opening stream socket to server %s:%s", cptr);
      return NULL;
    }

  if (cptr->fd >= (HARD_FDLIMIT - 10))
    {
      sendto_realops("No more connections allowed (%s)", cptr->name);
      return NULL;
    }

  mysk.sin_port = 0;
  mysk.sin_family = AF_INET;
  memset((void *)&server, 0, sizeof(server));
  server.sin_family = AF_INET;
  get_sockhost(cptr, aconf->host);

  /*
  ** Bind to a local IP# (with unknown port - let unix decide) so
  ** we have some chance of knowing the IP# that gets used for a host
  ** with more than one IP#.
  */
  /* No we don't bind it, not all OS's can handle connecting with
     an already bound socket, different ip# might occur anyway
     leading to a freezing select() on this side for some time.
     */
  if (specific_virtual_host)
    {
      mysk.sin_addr = vserv.sin_addr;

      /*
      ** No, we do bind it if we have virtual host support. If we don't
      ** explicitly bind it, it will default to IN_ADDR_ANY and we lose
      ** due to the other server not allowing our base IP --smg
      */	
      if (bind(cptr->fd, (struct sockaddr *)&mysk, sizeof(mysk)) == -1)
	{
	  report_error("error binding to local port for %s:%s", cptr);
	  return NULL;
	}
    }
  /*
   * By this point we should know the IP# of the host listed in the
   * conf line, whether as a result of the hostname lookup or the ip#
   * being present instead. If we dont know it, then the connect fails.
   */
  if (isdigit(*aconf->host) && (aconf->ipnum.s_addr == -1))
    aconf->ipnum.s_addr = inet_addr(aconf->host);
  if (aconf->ipnum.s_addr == -1)
    {
      hp = cptr->hostp;
      if (!hp)
	{
	  Debug((DEBUG_FATAL, "%s: unknown host", aconf->host));
	  return NULL;
	}
      (void)memcpy((void *)&aconf->ipnum, (void *)hp->h_addr,
	    sizeof(struct in_addr));
    }
  (void)memcpy( (void *)&server.sin_addr, (void *)&aconf->ipnum,
	sizeof(struct in_addr));
  (void)memcpy((void *)&cptr->ip, (void *)&aconf->ipnum,
	sizeof(struct in_addr));
  server.sin_port = htons((aconf->port > 0) ? aconf->port : portnum);
  *lenp = sizeof(server);
  return	(struct sockaddr *)&server;
}

/*
 * do_dns_async
 *
 * Called when the fd returned from init_resolver() has been selected for
 * reading.
 */
static void do_dns_async()
{
  static Link ln;
  aClient *cptr;
  aConfItem *aconf;
  struct hostent *hp;
  int bytes, packets = 0;

/*
  if (ioctl(resfd, FIONREAD, &bytes) == -1)
    bytes = 1;
*/
  do {
    ln.flags = -1;
    hp = get_res((char *)&ln);
    Debug((DEBUG_DNS,"%#x = get_res(%d,%#x)",
	   hp, ln.flags, ln.value.cptr));
    
    switch (ln.flags)
      {
      case ASYNC_NONE :
	/*
	 * no reply was processed that was outstanding
	 * or had a client still waiting.
	 */
	break;
      case ASYNC_CLIENT :
	if ((cptr = ln.value.cptr))
	  {
	    del_queries((char *)cptr);
#ifdef SHOW_HEADERS
	    sendheader(cptr, REPORT_FIN_DNS, R_fin_dns);
#endif
	    ClearDNS(cptr);
	    cptr->hostp = hp;
	    if (!DoingAuth(cptr))
	      SetAccess(cptr);
	  }
	break;
      case ASYNC_CONNECT :
	aconf = ln.value.aconf;
	if (hp && aconf)
	  {
	    (void)memcpy((void *)&aconf->ipnum, (void *)hp->h_addr,
		  sizeof(struct in_addr));
	    (void)connect_server(aconf, NULL, hp);
	  }
	else
	  sendto_ops("Connect to %s failed: host lookup",
		     (aconf) ? aconf->host : "unknown");
	break;
      case ASYNC_CONF :
	aconf = ln.value.aconf;
	if (hp && aconf)
	  (void)memcpy((void *)&aconf->ipnum, (void *)hp->h_addr,
		sizeof(struct in_addr));
	break;
      default :
	break;
      }
    if (ioctl(resfd, FIONREAD, &bytes) == -1)
      bytes = 0;
    packets++;
  }  while ((bytes > 0) && (packets < 10)); 
}
