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
 *
 *  $Id$
 */
#include "s_bsd.h"
#include "s_serv.h"
#include "listener.h"
#include "config.h"
#include "struct.h"
#include "common.h"
#include "res.h"
#include "s_conf.h"
#include "s_auth.h"
#include "class.h"
#include "numeric.h"
#include "packet.h"
#include "parse.h"
#include "fdlist.h"
#include "send.h"
#include "restart.h"
#include "s_zip.h"
#include "ircd.h"
#include "s_misc.h"
#include "list.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#define BSD_COMP          /* needed on Solaris for FIONBIO */
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/param.h>    /* NOFILE */
#include <arpa/inet.h>

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

fd_set*  read_set;
fd_set*  write_set;

#ifndef HAVE_FD_ALLOC
fd_set  readset;
fd_set  writeset;
#endif

#endif /* USE_POLL_ */

#ifndef IN_LOOPBACKNET
#define IN_LOOPBACKNET        0x7f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

extern struct sockaddr_in vserv;               /* defined in s_conf.c */

const char* const NONB_ERROR_MSG   = "set_non_blocking failed for %s:%s"; 
const char* const OPT_ERROR_MSG    = "disable_sock_options failed for %s:%s";
const char* const SETBUF_ERROR_MSG = "set_sock_buffers failed for server %s:%s";

aClient*       local[MAXCONNECTIONS];

int            highest_fd = 0;

static struct sockaddr_in mysk;
static char               readBuf[READBUF_SIZE];

static int        completed_connection (aClient *);
static int        check_init (aClient *, char *);
static void       do_dns_async(void);


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
 * get_sockerr - get the error value from the socket or the current errno
 *
 * Get the *real* error from the socket (well try to anyway..).
 * This may only work when SO_DEBUG is enabled but its worth the
 * gamble anyway.
 */
static int get_sockerr(int fd)
{
  int errtmp = errno;
#ifdef SO_ERROR
  int err = 0;
  int len = sizeof(err);

  if (-1 < fd && !getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*) &err, &len)) {
    if (err)
      errtmp = err;
  }
  errno = errtmp;
#endif
  return errtmp;
}

/*
 * report_error - report an error from an errno. 
 * Record error to log and also send a copy to all *LOCAL* opers online.
 *
 *        text        is a *format* string for outputing error. It must
 *                contain only two '%s', the first will be replaced
 *                by the sockhost from the cptr, and the latter will
 *                be taken from sys_errlist[errno].
 *
 *        cptr        if not NULL, is the *LOCAL* client associated with
 *                the error.
 *
 * Cannot use perror() within daemon. stderr is closed in
 * ircd and cannot be used. And, worse yet, it might have
 * been reassigned to a normal connection...
 * 
 * Actually stderr is still there IFF ircd was run with -s --Rodder
 */

void report_error(const char* text, const char* who, int error) 
{
  who = (who) ? who : "";

  sendto_realops_flags(FLAGS_DEBUG, text, who, strerror(error));

#ifdef USE_SYSLOG
  syslog(LOG_WARNING, text, who, strerror(error));
#endif

  if (bootopt & BOOT_STDERR)
    {
       fprintf(stderr, text, who, strerror(error));
       fprintf(stderr, "\n");
       /* fflush(stderr); XXX - stderr is unbuffered, pointless */
    }
  Debug((DEBUG_ERROR, text, who, strerror(error)));
}

/*
 * connect_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, reply will contain
 * a non-null pointer, otherwise reply will be null.
 * if successful start the connection, otherwise notify opers
 */
static void connect_dns_callback(void* vptr, struct DNSReply* reply)
{
  aConfItem* aconf = (aConfItem*) vptr;
  aconf->dns_pending = 0;
  if (reply) {
    memcpy(&aconf->ipnum, reply->hp->h_addr, sizeof(struct in_addr));
    connect_server(aconf, 0, reply);
  }
  else
    sendto_ops("Connect to %s failed: host lookup", aconf->host);
}

/*
 * do_dns_async - called when the fd returned from init_resolver() has 
 * been selected for reading.
 */
static void do_dns_async()
{
  int bytes = 0;
  int packets = 0;

  do {
    get_res();
    if (ioctl(ResolverFileDescriptor, FIONREAD, &bytes) == -1)
      bytes = 0;
    packets++;
  }  while ((bytes > 0) && (packets < 10)); 
}

/*
 * set_sock_buffers - set send and receive buffers for socket
 * returns true (1) if successful, false (0) otherwise
 */
int set_sock_buffers(int fd, int size)
{
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*) &size, sizeof(size)) ||
      setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size)))
    return 0;
  return 1;
}

/*
 * disable_sock_options - if remote has any socket options set, disable them 
 * returns true (1) if successful, false (0) otherwise
 */
static int disable_sock_options(int fd)
{
#if defined(IP_OPTIONS) && defined(IPPROTO_IP)
  if (setsockopt(fd, IPPROTO_IP, IP_OPTIONS, NULL, 0))
    return 0;
#endif
  return 1;
}

/*
 * set_non_blocking - Set the client connection into non-blocking mode. 
 * If your system doesn't support this, you're screwed, ircd will run like
 * crap.
 * returns true (1) if successful, false (0) otherwise
 */
int set_non_blocking(int fd)
{
  /*
   * NOTE: consult ALL your relevant manual pages *BEFORE* changing
   * these ioctl's.  There are quite a few variations on them,
   * as can be seen by the PCS one.  They are *NOT* all the same.
   * Heed this well. - Avalon.
   */
  /* This portion of code might also apply to NeXT.  -LynX */
#ifdef NBLOCK_SYSV
  int res = 1;

  if (ioctl(fd, FIONBIO, &res) == -1)
    return 0;

#else /* !NBLOCK_SYSV */
  int nonb = 0;
  int res;

#ifdef NBLOCK_POSIX
  nonb |= O_NONBLOCK;
#endif
#ifdef NBLOCK_BSD
  nonb |= O_NDELAY;
#endif

  res = fcntl(fd, F_GETFL, 0);
  if (-1 == res || fcntl(fd, F_SETFL, res | nonb) == -1)
    return 0;
#endif /* !NBLOCK_SYSV */
  return 1;
}


/*
 * init_sys
 */
void init_sys()
{
  int fd;

#ifdef RLIMIT_FD_MAX
  struct rlimit limit;

  if (!getrlimit(RLIMIT_FD_MAX, &limit))
    {

      if (limit.rlim_max < MAXCONNECTIONS)
        {
          fprintf(stderr,"ircd fd table too big\n");
          fprintf(stderr,"Hard Limit: %ld IRC max: %d\n",
                        (long) limit.rlim_max, MAXCONNECTIONS);
          fprintf(stderr,"Fix MAXCONNECTIONS\n");
          exit(-1);
        }

      limit.rlim_cur = limit.rlim_max; /* make soft limit the max */
      if (setrlimit(RLIMIT_FD_MAX, &limit) == -1)
        {
          fprintf(stderr,"error setting max fd's to %ld\n",
                        (long) limit.rlim_cur);
          exit(-1);
        }

#ifndef USE_POLL
      if( MAXCONNECTIONS > FD_SETSIZE )
        {
          fprintf(stderr, "FD_SETSIZE = %d MAXCONNECTIONS = %d\n",
                  FD_SETSIZE, MAXCONNECTIONS);
          fprintf(stderr,
            "Make sure your kernel supports a larger FD_SETSIZE then " \
            "recompile with -DFD_SETSIZE=%d\n", MAXCONNECTIONS);
          exit(-1);
        }
#ifndef HAVE_FD_ALLOC
      printf("Value of FD_SETSIZE is %d\n", FD_SETSIZE);
#endif
#endif /* USE_POLL */
      printf("Value of NOFILE is %d\n", NOFILE);
    }
#endif        /* RLIMIT_FD_MAX */

#ifndef USE_POLL
  read_set = &readset;
  write_set = &writeset;
#endif

  for (fd = 3; fd < MAXCONNECTIONS; fd++)
    {
      close(fd);
      local[fd] = NULL;
    }
  local[1] = NULL;
  local[2] = NULL;

  if (bootopt & BOOT_TTY)        /* debugging is going to a tty */
    {
      init_resolver();
      return;
    }
  close(1);
  if (!(bootopt & BOOT_DEBUG) && !(bootopt & BOOT_STDERR))
    close(2);

#ifndef __CYGWIN__
  if (((bootopt & BOOT_CONSOLE) || isatty(0)) && !(bootopt & BOOT_STDERR))
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
          ioctl(fd, TIOCNOTTY, (char *)NULL);
          close(fd);
        }
#endif
     setsid();
     close(0);        /* fd 0 opened by inetd */
     local[0] = NULL;
    }
#endif /* __CYGWIN__ */
  init_resolver();
  return;
}

                
/*
 * check_init - initialize the various name strings used to store hostnames. 
 * This is set from either the server's sockhost (if client fd is a tty or 
 * localhost) or from the ip# converted into a string. 
 * 0 = success, -1 = fail.
 */
static int check_init(aClient* cptr, char* sockn)
{
  struct sockaddr_in sk;
  int                len = sizeof(struct sockaddr_in);

  /* If descriptor is a tty, special checking... */
  /* IT can't EVER be a tty */

  if (getpeername(cptr->fd, (struct sockaddr *)&sk, &len) == -1)
    {
      report_error("connect failure: %s %s", 
                    get_client_name(cptr, TRUE), errno);
      return -1;
    }
  strcpy(sockn, inetntoa((char*)&sk.sin_addr));

  if (inet_netof(sk.sin_addr) == IN_LOOPBACKNET)
    {
      if (cptr->dns_reply) {
        --cptr->dns_reply->ref_count;
        cptr->dns_reply = NULL;
      }
      strncpy_irc(sockn, me.name, HOSTLEN);
    }
  memcpy(&cptr->ip, &sk.sin_addr, sizeof(struct in_addr));
  cptr->port = ntohs(sk.sin_port);

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
int check_client(aClient *cptr,char *username,char **reason)
{
  static char     sockname[HOSTLEN + 1];
  int             i;
  struct hostent* hp = 0;
 
  ClearAccess(cptr);
  Debug((DEBUG_DNS, "ch_cl: check access for %s[%s]",
         cptr->name, inetntoa((char *)&cptr->ip)));

#if 0
  /* 
   * XXX - already done by the time this is called
   */
  if (check_init(cptr, sockname))
    return -2;
#endif

  if (cptr->dns_reply)
    hp = cptr->dns_reply->hp;

  if ((i = attach_Iline(cptr, hp, sockname, username, reason)))
    {
      Debug((DEBUG_DNS,"ch_cl: access denied: %s[%s]",
             cptr->name, sockname));
      return i;
    }

  Debug((DEBUG_DNS, "ch_cl: access ok: %s[%s]", cptr->name, sockname));

  return 0;
}

static int check_server(aClient* cptr, struct DNSReply* dns_reply,
                        aConfItem *c_conf, aConfItem *n_conf)
{
  char*           name;
  char            sockname[HOSTLEN + 1];
  Link*           lp = cptr->confs;
  int             i;
  struct hostent* hp;

  ClearAccess(cptr);
  if (IsConnecting(cptr)) {
    if (check_init(cptr, sockname))
      return -2;
  }

  hp = (dns_reply) ? dns_reply->hp : 0;
  if (!hp && cptr->dns_reply)
    hp = cptr->dns_reply->hp;

  if (hp)
    {
      /*
       * XXX - this is already done for all connecting clients
       */
      for (i = 0; hp->h_addr_list[i]; i++)
        {
          if (0 == memcmp(hp->h_addr_list[i], (char*)&cptr->ip,
                          sizeof(struct in_addr)))
            break;
        }
      if (!hp->h_addr_list[i])
        {
          sendto_realops_flags(FLAGS_DEBUG,
                               "Server IP# Mismatch: %s != %s[%08x]",
                               inetntoa((char *)&cptr->ip), hp->h_name,
                               *((unsigned long *)hp->h_addr));
          hp = NULL;
        }
    }
  if (hp)
    {
      /*
       * if we are missing a C or N line from above, search for
       * it under all known hostnames we have for this ip#.
       */
      for (i = 0, name = hp->h_name; name; name = hp->h_aliases[i++])
        {
          if (!c_conf)
            c_conf = find_conf_host(lp, name, CONF_CONNECT_SERVER );
          if (!n_conf)
            n_conf = find_conf_host(lp, name, CONF_NOCONNECT_SERVER );
          if (c_conf && n_conf)
            {
              strncpy_irc(cptr->host, name, HOSTLEN);
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
        c_conf = find_conf_ip(lp, (char*)&cptr->ip,
                              cptr->username, CONF_CONNECT_SERVER);
      if (!n_conf)
        n_conf = find_conf_ip(lp, (char*)&cptr->ip,
                              cptr->username, CONF_NOCONNECT_SERVER);
    }
  else
    {
      for (i = 0; hp->h_addr_list[i]; i++)
        {
          if (!c_conf)
            c_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_CONNECT_SERVER);
          if (!n_conf)
            n_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_NOCONNECT_SERVER);
        }
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
      /* strncpy_irc(cptr->host, sockname); */
      Debug((DEBUG_DNS, "sv_cl: access denied: %s[%s@%s] c %x n %x",
             name, cptr->username, cptr->host, c_conf, n_conf));
      return -1;
    }
  /*
   * attach the C and N lines to the client structure for later use.
   */
  attach_conf(cptr, n_conf);
  attach_conf(cptr, c_conf);
  attach_confs(cptr, name, CONF_HUB | CONF_LEAF);
  
  if (c_conf->ipnum.s_addr == INADDR_NONE)
    c_conf->ipnum.s_addr = cptr->ip.s_addr;

  strncpy_irc(cptr->host, c_conf->host, HOSTLEN);
  Debug((DEBUG_DNS,"sv_cl: access ok: %s[%s]", name, cptr->host));

  return 0;
}

/*
 * check_server_init, check_server
 *        check access for a server given its name (passed in cptr struct).
 *        Must check for all C/N lines which have a name which matches the
 *        name given and a host which matches. A host alias which is the
 *        same as the server name is also acceptable in the host field of a
 *        C/N line.
 *  0 = Success
 * -1 = Access denied
 * -2 = Bad socket.
 */
int check_server_init(aClient *cptr)
{
  char*            name;
  struct ConfItem* c_conf    = NULL;
  struct ConfItem* n_conf    = NULL;
  struct DNSReply* dns_reply = NULL;
  struct SLink*    lp;

  name = cptr->name;
  Debug((DEBUG_DNS, "sv_cl: check access for %s[%s]", name, cptr->host));

  if (IsUnknown(cptr) && 
      !attach_confs(cptr, name, CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER ))
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
          sendto_realops_flags(FLAGS_DEBUG, "Connecting Error: %s[%s]", name,
                             cptr->host);
          det_confs_butmask(cptr, 0);
          return -1;
        }
    }
  /*
  ** If the servername is a hostname, either an alias (CNAME) or
  ** real name, then check with it as the host. Use gethostbyname()
  ** to check for servername as hostname.
  */
  if (!cptr->dns_reply)
    {
      aConfItem* aconf = count_cnlines(lp);
      if (aconf)
        {
          /*
          ** Do a lookup for the CONF line *only* and not
          ** the server connection else we get stuck in a
          ** nasty state since it takes a SERVER message to
          ** get us here and we cant interrupt that very
          ** well.
          */
          ClearAccess(cptr);
          Debug((DEBUG_DNS,"sv_ci:cache lookup (%s)",aconf->host));
          dns_reply = conf_dns_lookup(aconf);
        }
    }
  return check_server(cptr, dns_reply, c_conf, n_conf);
}

/*
 * completed_connection - Complete non-blocking connect-sequence. 
 * Check access and terminate connection, if trouble detected.
 *
 * Return         TRUE, if successfully completed
 *                FALSE, if failed and ClientExit
 */
static int completed_connection(aClient *cptr)
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
 * connect_inet - open a socket and connect to another server
 * returns true (1) if successful, false (0) otherwise
 */
static int connect_inet(aConfItem *aconf, aClient *cptr)
{
  static struct sockaddr_in sin;

  /*
   * Might as well get sockhost from here, the connection is attempted
   * with it so if it fails its useless.
   */
  cptr->fd = socket(AF_INET, SOCK_STREAM, 0);

  if (cptr->fd == -1)
    {
      report_error("opening stream socket to server %s:%s", cptr->name, errno);
      return 0;
    }

  if (cptr->fd >= (HARD_FDLIMIT - 10))
    {
      sendto_realops("No more connections allowed (%s)", cptr->name);
      return 0;
    }

  mysk.sin_port   = 0;
  mysk.sin_family = AF_INET;

  memset(&sin, 0, sizeof(sin));
  sin.sin_family  = AF_INET;

  /*
   * Bind to a local IP# (with unknown port - let unix decide) so
   * we have some chance of knowing the IP# that gets used for a host
   * with more than one IP#.
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
      if (bind(cptr->fd, (struct sockaddr*) &mysk, sizeof(mysk)))
        {
          report_error("error binding to local port for %s:%s", 
                       cptr->name, errno);
          return 0;
        }
    }
  /*
   * By this point we should know the IP# of the host listed in the
   * conf line, whether as a result of the hostname lookup or the ip#
   * being present instead. If we dont know it, then the connect fails.
   */
  if (IsDigit(*aconf->host) && INADDR_NONE == aconf->ipnum.s_addr)
    aconf->ipnum.s_addr = inet_addr(aconf->host);

  if (INADDR_NONE == aconf->ipnum.s_addr)
    {
      struct DNSReply* reply = cptr->dns_reply;
      if (!reply)
        {
          Debug((DEBUG_FATAL, "%s: unknown host", aconf->host));
          return 0;
        }
      memcpy(&aconf->ipnum, reply->hp->h_addr, sizeof(struct in_addr));
    }

  sin.sin_addr.s_addr = aconf->ipnum.s_addr;
  cptr->ip.s_addr     = aconf->ipnum.s_addr;
  sin.sin_port        = htons(aconf->port);

  if (!set_non_blocking(cptr->fd))
    report_error(NONB_ERROR_MSG, get_client_name(cptr, TRUE), errno);

  if (!set_sock_buffers(cptr->fd, READBUF_SIZE))
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, TRUE), errno);

  if (connect(cptr->fd, (const struct sockaddr*) &sin, sizeof(sin)) && 
      errno != EINPROGRESS)
    {
      int errtmp = errno; /* other system calls may eat errno */
      report_error("Connect to host %s failed: %s",
                   get_client_name(cptr, TRUE), errno);
      errno = errtmp;
      return 0;
    }
  return 1;
}

/*
 * connect_server - start or complete a connection to another server
 * returns true (1) if successful, false (0) otherwise
 */
int connect_server(aConfItem *aconf, aClient* by, struct DNSReply* reply)
{
  struct Client* cptr;

  Debug((DEBUG_NOTICE,"Connect to %s[%s] @%s",
         aconf->user, aconf->host, inetntoa((char*)&aconf->ipnum)));

  if ((cptr = find_server(aconf->name, NULL)))
    {
      sendto_ops("Server %s already present from %s",
                 aconf->name, get_client_name(cptr, TRUE));
      if (by && IsPerson(by) && !MyClient(by))
        sendto_one(by, ":%s NOTICE %s :Server %s already present from %s",
                   me.name, by->name, aconf->name,
                   get_client_name(cptr, TRUE));
      return 0;
    }

  /*
   * If we dont know the IP# for this host and it is a hostname and
   * not a ip# string, then try and find the appropriate host record.
   */
  if (INADDR_NONE == aconf->ipnum.s_addr && !aconf->dns_pending)
    {
      if ((aconf->ipnum.s_addr = inet_addr(aconf->host)) == INADDR_NONE)
        {
          struct DNSQuery  query;
          
          query.vptr     = aconf;
          query.callback = connect_dns_callback;
          reply = gethost_byname(aconf->host, &query);
          Debug((DEBUG_NOTICE, "co_sv: reply %x ac %x na %s ho %s",
                 reply, aconf, aconf->name, aconf->host));
          if (!reply) 
            {
              aconf->dns_pending = 1;
              return 0;
            }
          memcpy(&aconf->ipnum, reply->hp->h_addr, sizeof(struct in_addr));
        }
    }
  cptr = make_client(NULL);
  if (reply) 
    ++reply->ref_count;
  cptr->dns_reply = reply;
  
  /*
   * Copy these in so we have something for error detection.
   */
  strncpy_irc(cptr->name, aconf->name, HOSTLEN);
  strncpy_irc(cptr->host, aconf->host, HOSTLEN);

  if (!connect_inet(aconf, cptr)) {
    if (by && IsPerson(by) && !MyClient(by))
      sendto_one(by, ":%s NOTICE %s :Connect to host %s failed.",
                 me.name, by->name, cptr);
    free_client(cptr);
    return 0;
  }

  /* Attach config entries to client here rather than in
   * completed_connection. This to avoid null pointer references
   * when name returned by gethostbyaddr matches no C lines
   * (could happen in 2.6.1a when host and servername differ).
   * No need to check access and do gethostbyaddr calls.
   * There must at least be one as we got here C line...  meLazy
   */
  attach_confs_host(cptr, aconf->host,
                    CONF_NOCONNECT_SERVER | CONF_CONNECT_SERVER );

  if (!find_conf_host(cptr->confs, aconf->host, CONF_NOCONNECT_SERVER) ||
      !find_conf_host(cptr->confs, aconf->host, CONF_CONNECT_SERVER))
    {
      sendto_ops("Host %s is not enabled for connecting:no C/N-line",
                 aconf->host);
      if (by && IsPerson(by) && !MyClient(by))
        sendto_one(by, ":%s NOTICE %s :Connect to host %s failed.",
                   me.name, by->name, cptr);
      det_confs_butmask(cptr, 0);

      free_client(cptr);
      return 0;
    }
  /*
   * The socket has been connected or connect is in progress.
   */
  make_server(cptr);
  if (by && IsPerson(by))
    {
      strcpy(cptr->serv->by, by->name);
      if (cptr->serv->user) 
        free_user(cptr->serv->user, NULL);
      cptr->serv->user = by->user;
      by->user->refcnt++;
    } 
  else
    {
      strcpy(cptr->serv->by, "AutoConn.");
      if (cptr->serv->user)
        free_user(cptr->serv->user, NULL);
      cptr->serv->user = NULL;
    }
  cptr->serv->up = me.name;

  if (cptr->fd > highest_fd)
    highest_fd = cptr->fd;
  local[cptr->fd] = cptr;
  SetConnecting(cptr);

  add_client_to_list(cptr);
  fdlist_add(cptr->fd, FDL_DEFAULT);
  nextping = CurrentTime;

  return 1;
}

/*
** close_connection
**        Close the physical connection. This function must make
**        MyConnect(cptr) == FALSE, and set cptr->from == NULL.
*/
void close_connection(aClient *cptr)
{
  aConfItem *aconf;

  if (IsServer(cptr))
    {
      ircstp->is_sv++;
      ircstp->is_sbs += cptr->sendB;
      ircstp->is_sbr += cptr->receiveB;
      ircstp->is_sks += cptr->sendK;
      ircstp->is_skr += cptr->receiveK;
      ircstp->is_sti += CurrentTime - cptr->firsttime;
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
      /*
       * If the connection has been up for a long amount of time, schedule
       * a 'quick' reconnect, else reset the next-connect cycle.
       */
      if ((aconf = find_conf_exact(cptr->name, cptr->username,
                                   cptr->host, CONF_CONNECT_SERVER)))
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

    }
  else if (IsClient(cptr))
    {
      ircstp->is_cl++;
      ircstp->is_cbs += cptr->sendB;
      ircstp->is_cbr += cptr->receiveB;
      ircstp->is_cks += cptr->sendK;
      ircstp->is_ckr += cptr->receiveK;
      ircstp->is_cti += CurrentTime - cptr->firsttime;
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
  
  if (cptr->dns_reply) {
    --cptr->dns_reply->ref_count;
    cptr->dns_reply = 0;
  }
  if (cptr->fd >= 0)
    {
      flush_connections(cptr);
      local[cptr->fd] = NULL;
#ifdef ZIP_LINKS
        /*
        ** the connection might have zip data (even if
               ** FLAGS2_ZIP is not set)
        */
      if (IsServer(cptr))
        zip_free(cptr);
#endif
      fdlist_delete(cptr->fd, FDL_ALL);
      close(cptr->fd);
      cptr->fd = -1;
      DBufClear(&cptr->sendQ);
      DBufClear(&cptr->recvQ);
      memset(cptr->passwd, 0, sizeof(cptr->passwd));
      /*
       * clean up extra sockets from P-lines which have been discarded.
       */
      if (cptr->listener) {
        assert(0 < cptr->listener->ref_count);
        if (0 == --cptr->listener->ref_count && !cptr->listener->active) 
          close_listener(cptr->listener);
        cptr->listener = 0;
      }
    }

  for (; highest_fd > 0; --highest_fd) {
    if (local[highest_fd])
      break;
  }

  det_confs_butmask(cptr, 0);
  cptr->from = NULL; /* ...this should catch them! >:) --msa */
}

/*
 * add_connection - creates a client which has just connected to us on 
 * the given fd. The sockhost field is initialized with the ip# of the host.
 * The client is sent to the auth module for verification, and not put in
 * any client list yet.
 */
void add_connection(struct Listener* listener, int fd)
{
  aClient*           new_client;
  struct sockaddr_in addr;
  int                len = sizeof(struct sockaddr_in);

  assert(0 != listener);

  /* 
   * get the client socket name from the socket
   * the client has already been checked out in accept_connection
   */
  if (getpeername(fd, (struct sockaddr*) &addr, &len)) {
    report_error("Failed in adding new connection %s :%s", 
                 get_listener_name(listener), errno);
    ircstp->is_ref++;
    close(fd);
    return;
  }

  new_client = make_client(NULL);

  /* 
   * copy address to 'sockhost' as a string, copy it to host too
   * so we have something valid to put into error messages...
   */
  strncpy_irc(new_client->sockhost, 
              inetntoa((char*) &addr.sin_addr), HOSTIPLEN);
  strcpy(new_client->host, new_client->sockhost);
  new_client->ip.s_addr = addr.sin_addr.s_addr;
  new_client->port      = ntohs(addr.sin_port);
  new_client->fd        = fd;

  new_client->listener  = listener;
  ++listener->ref_count;

  if (!set_non_blocking(new_client->fd))
    report_error(NONB_ERROR_MSG, get_client_name(new_client, TRUE), errno);
  if (!disable_sock_options(new_client->fd))
    report_error(OPT_ERROR_MSG, get_client_name(new_client, TRUE), errno);
  start_auth(new_client);
}

/*
 * parse_client_queued - parse client queued messages
 */
static int parse_client_queued(struct Client* cptr)
{
  int dolen  = 0;
  int done   = 0;

  while (DBufLength(&cptr->recvQ) && !NoNewLine(cptr) &&
	 ((cptr->status < STAT_UNKNOWN) || (cptr->since - CurrentTime < 10))) {
    /*
     * If it has become registered as a Server
     * then skip the per-message parsing below.
     */
    if (IsServer(cptr)) {
      /* 
       * This is actually useful, but it needs the ZIP_FIRST
       * kludge or it will break zipped links  -orabidoo
       */
      dolen = dbuf_get(&cptr->recvQ, readBuf, READBUF_SIZE);

      if (dolen <= 0)
	break;
      if ((done = dopacket(cptr, readBuf, dolen)))
	return done;
      break;
    }
    dolen = dbuf_getmsg(&cptr->recvQ, readBuf, READBUF_SIZE);

    /*
    ** Devious looking...whats it do ? well..if a client
    ** sends a *long* message without any CR or LF, then
    ** dbuf_getmsg fails and we pull it out using this
    ** loop which just gets the next 512 bytes and then
    ** deletes the rest of the buffer contents.
    ** -avalon
    */
    while (dolen <= 0) {
      if (dolen < 0)
	return exit_client(cptr, cptr, cptr, "dbuf_getmsg fail");
      if (DBufLength(&cptr->recvQ) < 510) {
	cptr->flags |= FLAGS_NONL;
	break;
      }
      dolen = dbuf_get(&cptr->recvQ, readBuf, 511);
      if (dolen > 0 && DBufLength(&cptr->recvQ))
	DBufClear(&cptr->recvQ);
    }
    if (dolen > 0 && (dopacket(cptr, readBuf, dolen) == CLIENT_EXITED))
      return CLIENT_EXITED;
  }
  return 1;
}

/*
 * read_packet - Read a 'packet' of data from a connection and process it.
 * Do some tricky stuff for client connections to make sure they don't do
 * any flooding >:-) -avalon
 */
#define SBSD_MAX_CLIENT 6090

static int read_packet(struct Client *cptr)
{
  int length = 0;
  int done;

  if (!(IsPerson(cptr) && DBufLength(&cptr->recvQ) > SBSD_MAX_CLIENT)) {
    errno = 0;
    length = recv(cptr->fd, readBuf, READBUF_SIZE, 0);
    /*
     * If not ready, fake it so it isnt closed
     */
    if (length == -1) {
      if (EWOULDBLOCK == errno || EAGAIN == errno)
        length = 1;
      return length;
    }
  }
  if (length == 0)
    return length;

#ifdef REJECT_HOLD
  /* 
   * If client has been marked as rejected i.e. it is a client
   * that is trying to connect again after a k-line,
   * pretend to read it but don't actually.
   * -Dianora
   *
   * FLAGS_REJECT_HOLD should NEVER be set for non local client 
   */
  if (IsRejectHeld(cptr))
    return 1;
#endif

  cptr->lasttime = CurrentTime;
  if (cptr->lasttime > cptr->since)
    cptr->since = cptr->lasttime;
  cptr->flags &= ~(FLAGS_PINGSENT | FLAGS_NONL);

  /*
   * For server connections, we process as many as we can without
   * worrying about the time of day or anything :)
   */
  if (PARSE_AS_SERVER(cptr)) {
    if (length > 0) {
      if ((done = dopacket(cptr, readBuf, length)))
        return done;
    }
  }
  else {
    /*
    ** Before we even think of parsing what we just read, stick
    ** it on the end of the receive queue and do it when its
    ** turn comes around.
    */
    if (dbuf_put(&cptr->recvQ, readBuf, length) < 0)
      return exit_client(cptr, cptr, cptr, "dbuf_put fail");
    
    if (IsPerson(cptr) &&
#ifdef NO_OPER_FLOOD
        !IsAnOper(cptr) &&
#endif
        DBufLength(&cptr->recvQ) > CLIENT_FLOOD) {
      return exit_client(cptr, cptr, cptr, "Excess Flood");
    }
    return parse_client_queued(cptr);
  }
  return 1;
}

static void error_exit_client(struct Client* cptr, int error)
{
  /*
   * ...hmm, with non-blocking sockets we might get
   * here from quite valid reasons, although.. why
   * would select report "data available" when there
   * wasn't... so, this must be an error anyway...  --msa
   * actually, EOF occurs when read() returns 0 and
   * in due course, select() returns that fd as ready
   * for reading even though it ends up being an EOF. -avalon
   */
  char errmsg[255];
  int  current_error = get_sockerr(cptr->fd);

  Debug((DEBUG_ERROR, "READ ERROR: fd = %d %d %d",
         cptr->fd, current_error, error));
  if (IsServer(cptr) || IsHandshake(cptr))
    {
      int connected = CurrentTime - cptr->firsttime;
      
      if (0 == error)
        sendto_ops("Server %s closed the connection",
                   get_client_name(cptr, FALSE));
      else
        report_error("Lost connection to %s:%s", 
                     get_client_name(cptr, TRUE), current_error);
      sendto_ops("%s had been connected for %d day%s, %2d:%02d:%02d",
                 cptr->name, connected/86400,
                 (connected/86400 == 1) ? "" : "s",
                 (connected % 86400) / 3600, (connected % 3600) / 60,
                 connected % 60);
    }
  if (0 == error)
    strcpy(errmsg, "Remote closed the connection");
  else
    ircsprintf(errmsg, "Read error: %d (%s)", 
               current_error, strerror(current_error));
  exit_client(cptr, cptr, &me, errmsg);
}

/*
 * Check all connections for new connections and input data that is to be
 * processed. Also check for connections with data queued and whether we can
 * write it out.
 */
#ifndef USE_POLL
int read_message(time_t delay, unsigned char mask)        /* mika */

     /* Don't ever use ZERO here, unless you mean to poll
        and then you have to have sleep/wait somewhere 
        else in the code.--msa
      */
{
  aClient*            cptr;
  int                 nfds;
  struct timeval      wait;
  time_t              delay2 = delay;
  time_t              now;
  u_long              usec = 0;
  int                 res;
  int                 length;
  struct AuthRequest* auth = 0;
  struct AuthRequest* auth_next = 0;
  struct Listener*    listener = 0;
  int                 i;

  now = CurrentTime;

  for (res = 0;;)
    {
      FD_ZERO(read_set);
      FD_ZERO(write_set);

      for (auth = AuthPollList; auth; auth = auth->next) {
        assert(-1 < auth->fd);
        if (IsAuthConnect(auth))
          FD_SET(auth->fd, write_set);
        else /* if(IsAuthPending(auth)) */
          FD_SET(auth->fd, read_set);
      }
      for (listener = ListenerPollList; listener; listener = listener->next) {
        assert(-1 < listener->fd);
        FD_SET(listener->fd, read_set);
      }
      for (i = 0; i <= highest_fd; i++)
        {
          if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
            continue;

          /*
           * anything that IsMe should NEVER be in the local client array
           */
          assert(!IsMe(cptr));

          if (DBufLength(&cptr->recvQ) && delay2 > 2)
            delay2 = 1;
          if (DBufLength(&cptr->recvQ) < 4088)        
            {
               FD_SET(i, read_set);
            }

          if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
#ifdef ZIP_LINKS
              || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
#endif
              )
            {
               FD_SET(i, write_set);
            }
        }
      
      if (ResolverFileDescriptor >= 0)
        {
          FD_SET(ResolverFileDescriptor, read_set);
        }
      wait.tv_sec = IRCD_MIN(delay2, delay);
      wait.tv_usec = usec;

      nfds = select(MAXCONNECTIONS, read_set, write_set, 0, &wait);

      if ((CurrentTime = time(NULL)) == -1)
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

  if (-1 < ResolverFileDescriptor && 
      FD_ISSET(ResolverFileDescriptor, read_set)) {
    do_dns_async();
    --nfds;
  }
  /*
   * Check the auth fd's
   */
  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    assert(-1 < auth->fd);
    if (IsAuthConnect(auth) && FD_ISSET(auth->fd, write_set)) {
      send_auth_query(auth);
      if (0 == --nfds)
        break;
    }
    else if (FD_ISSET(auth->fd, read_set)) {
      read_auth_reply(auth);
      if (0 == --nfds)
        break;
    }
  }
  for (listener = ListenerPollList; listener; listener = listener->next) {
    assert(-1 < listener->fd);
    if (FD_ISSET(listener->fd, read_set))
      accept_connection(listener);
  }

  for (i = 0; i <= highest_fd; i++) {
    if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
      continue;

    /*
     * See if we can write...
     */
    if (FD_ISSET(i, write_set)) {
      int write_err = 0;
      --nfds;

      /*
       * ...room for writing, empty some queue then...
       */
      if (IsConnecting(cptr))
        write_err = completed_connection(cptr);
      if (!write_err)
        send_queued(cptr);

      if (write_err || IsDead(cptr)) {
        exit_client(cptr, cptr, &me, 
                    (cptr->flags & FLAGS_SENDQEX) ? 
                    "SendQ Exceeded" : strerror(get_sockerr(cptr->fd)));
        continue;
      }
    }
    length = 1;     /* for fall through case */

    if (FD_ISSET(i, read_set)) {
      --nfds;
      length = read_packet(cptr);
    }
    else if (PARSE_AS_CLIENT(cptr) && !NoNewLine(cptr))
      length = parse_client_queued(cptr);

    if (length > 0 || length == CLIENT_EXITED)
      continue;
    if (IsDead(cptr)) {
       exit_client(cptr, cptr, &me,
		    strerror(get_sockerr(cptr->fd)));
       continue;
    }
    error_exit_client(cptr, length);
    errno = 0;
  }
  return 0;
}
  
#else /* USE_POLL */

#if defined(POLLMSG) && defined(POLLIN) && defined(POLLRDNORM)
#define POLLREADFLAGS (POLLMSG | POLLIN | POLLRDNORM)
#else

# if defined(POLLIN) && defined(POLLRDNORM)
# define POLLREADFLAGS (POLLIN | POLLRDNORM)
# else

#  if defined(POLLIN)
#  define POLLREADFLAGS POLLIN
#  else

#   if defined(POLLRDNORM)
#    define POLLREADFLAGS POLLRDNORM
#   endif

#  endif

# endif

#endif

#if defined(POLLOUT) && defined(POLLWRNORM)
#define POLLWRITEFLAGS (POLLOUT | POLLWRNORM)
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
#define POLLERRORS (POLLERR | POLLHUP)
#else
#define POLLERRORS POLLERR
#endif

#define PFD_SETR(thisfd) do { CHECK_PFD(thisfd) \
                           pfd->events |= POLLREADFLAGS; } while (0)
#define PFD_SETW(thisfd) do { CHECK_PFD(thisfd) \
                           pfd->events |= POLLWRITEFLAGS; } while (0)
#define CHECK_PFD(thisfd)                     \
        if (pfd->fd != thisfd) {              \
                pfd = &poll_fdarray[nbr_pfds++];\
                pfd->fd     = thisfd;           \
                pfd->events = 0;                \
        }

#if defined(SOL20)
#define CONNECTFAST
#endif

int read_message(time_t delay, unsigned char mask)
{
  aClient*             cptr;
  int                  nfds;
  struct timeval       wait;

  static struct pollfd poll_fdarray[MAXCONNECTIONS];
  struct pollfd*       pfd = poll_fdarray;
  struct pollfd*       res_pfd = NULL;
  int                  nbr_pfds = 0;
  time_t               delay2 = delay;
  u_long               usec = 0;
  int                  res = 0;
  int                  length;
  int                  fd;
  struct AuthRequest*  auth;
  struct AuthRequest*  auth_next;
  struct Listener*     listener;
  int                  rr;
  int                  rw;
  int                  i;

  for ( ; ; ) {
    nbr_pfds = 0;
    pfd      = poll_fdarray;
    pfd->fd  = -1;
    res_pfd  = NULL;
    auth = 0;

    /*
     * set resolver descriptor
     */
    if (ResolverFileDescriptor >= 0) {
      PFD_SETR(ResolverFileDescriptor);
      res_pfd = pfd;
    }
    /*
     * set auth descriptors
     */
    for (auth = AuthPollList; auth; auth = auth->next) {
      assert(-1 < auth->fd);
      auth->index = nbr_pfds;
      if (IsAuthConnect(auth))
        PFD_SETW(auth->fd);
      else
        PFD_SETR(auth->fd);
    }
    /*
     * set listener descriptors
     */
    for (listener = ListenerPollList; listener; listener = listener->next) {
      assert(-1 < listener->fd);
#ifdef CONNECTFAST
      listener->index = nbr_pfds;
      PFD_SETR(listener->fd);
#else
     /* 
      * It is VERY bad if someone tries to send a lot
      * of clones to the server though, as mbuf's can't
      * be allocated quickly enough... - Comstud
      */
      listener->index = -1;
      if (CurrentTime > (listener->last_accept + 2)) {
        listener->index = nbr_pfds;
        PFD_SETR(listener->fd);
      }
      else if (delay2 > 2)
        delay2 = 2;
#endif
    }
    /*
     * set client descriptors
     */
    for (i = 0; i <= highest_fd; ++i) {
      if (!(GlobalFDList[i] & mask) || !(cptr = local[i]))
        continue;

     /*
      * anything that IsMe should NEVER be in the local client array
      */
      assert(!IsMe(cptr));
      if (DBufLength(&cptr->recvQ) && delay2 > 2)
        delay2 = 1;

      if (DBufLength(&cptr->recvQ) < 4088)
        PFD_SETR(i);
      
      if (DBufLength(&cptr->sendQ) || IsConnecting(cptr)
#ifdef ZIP_LINKS
          || ((cptr->flags2 & FLAGS2_ZIP) && (cptr->zip->outcount > 0))
#endif
          )
        PFD_SETW(i);
    }

    wait.tv_sec = IRCD_MIN(delay2, delay);
    wait.tv_usec = usec;
    nfds = poll(poll_fdarray, nbr_pfds,
                wait.tv_sec * 1000 + wait.tv_usec / 1000);
    if (nfds == -1 && ((errno == EINTR) || (errno == EAGAIN)))
      return -1;
    else if (nfds >= 0)
      break;
    report_error("poll %s:%s", me.name, errno);
    res++;
    if (res > 5)
      restart("too many poll errors");
    sleep(10);
  }
  /*
   * check resolver descriptor
   */
  if (res_pfd && (res_pfd->revents & (POLLREADFLAGS | POLLERRORS))) {
    do_dns_async();
    --nfds;
  }
  /*
   * check auth descriptors
   */
  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    i = auth->index;
    /*
     * check for any event, we only ask for one at a time
     */
    if (poll_fdarray[i].revents) { 
      if (IsAuthConnect(auth))
        send_auth_query(auth);
      else
        read_auth_reply(auth);
      if (0 == --nfds)
        break;
    }
  }
  /*
   * check listeners
   */
  for (listener = ListenerPollList; listener; listener = listener->next) {
    if (-1 == listener->index)
      continue;
    i = listener->index;
    if (poll_fdarray[i].revents) {
      accept_connection(listener);
      if (0 == --nfds)
        break;
    }
  }
  /*
   * i contains the next non-auth/non-listener index, since we put the 
   * resolver, auth and listener, file descriptors in poll_fdarray first, 
   * the very next one should be the start of the clients
   */
  pfd = &poll_fdarray[++i];
    
  for ( ; (nfds > 0) && (i < nbr_pfds); i++, pfd++)
    {
      if (!pfd->revents)
        continue;
      --nfds;
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
      if (!(cptr = local[fd]))
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
            send_queued(cptr);
          if (IsDead(cptr) || write_err)
            {
              exit_client(cptr, cptr, &me,
                          strerror(get_sockerr(cptr->fd)));
              continue;
            }
        }
      length = 1;     /* for fall through case */
      if (rr)
        length = read_packet(cptr);
      else if (PARSE_AS_CLIENT(cptr) && !NoNewLine(cptr))
        length = parse_client_queued(cptr);

      if (length > 0 || length == CLIENT_EXITED)
        continue;
      if (IsDead(cptr)) {
         exit_client(cptr, cptr, &me,
                      strerror(get_sockerr(cptr->fd)));
         continue;
      }
      error_exit_client(cptr, length);
      errno = 0;
    }
  return 0;
}

#endif /* USE_POLL */

