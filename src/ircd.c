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
 *
 * $Id$
 */
#include "ircd.h"
#include "channel.h"
#include "class.h"
#include "common.h"
#include "dline_conf.h"
#include "fdlist.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd_signal.h"
#include "list.h"
#include "m_gline.h"
#include "motd.h"
#include "msg.h"         /* msgtab */
#include "mtrie_conf.h"
#include "numeric.h"
#include "parse.h"
#include "res.h"
#include "restart.h"
#include "s_auth.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_debug.h"
#include "s_misc.h"
#include "s_serv.h"      /* try_connections */
#include "s_stats.h"
#include "s_zip.h"
#include "scache.h"
#include "send.h"
#include "struct.h"
#include "whowas.h"

#include <string.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>

#ifdef SETUID_ROOT
#include <sys/lock.h>
#include <unistd.h>
#endif /* SETUID_ROOT */


#ifdef  REJECT_HOLD
int reject_held_fds = 0;
#endif

#ifdef NEED_SPLITCODE
extern time_t server_split_time;
extern int    server_was_split;
#endif

int cold_start = YES;   /* set if the server has just fired up */

/* /quote set variables */
struct SetOptions GlobalSetOptions;

/* config.h config file paths etc */
ConfigFileEntryType ConfigFileEntry; 

struct  Counter Count;

time_t  CurrentTime;            /* GLOBAL - current system timestamp */
int     ServerRunning;          /* GLOBAL - server execution state */
aClient me;                     /* That's me */

aClient* GlobalClientList = 0;  /* Pointer to beginning of Client list */
/* client pointer lists -Dianora */ 
aClient *local_cptr_list = NULL;
aClient *oper_cptr_list  = NULL;
aClient *serv_cptr_list  = NULL;

static size_t initialVMTop;     /* top of virtual memory at init */

static void open_debugfile();
static void write_pidfile(void);

static void initialize_global_set_options(void);
static void initialize_message_files(void);
static time_t io_loop(time_t);

char**  myargv;
int     dorehash   = 0;
int     debuglevel = -1;        /* Server debug level */
int     bootopt    = 0;         /* Server boot option flags */
char*   debugmode  = "";        /*  -"-    -"-   -"-  */


int     rehashed = YES;
int     dline_in_progress = NO; /* killing off matching D lines ? */
time_t  nextconnect = 1;        /* time for next try_connections call */
time_t  nextping = 1;           /* same as above for check_pings() */


/*
 * get_vm_top - get the operating systems notion of the resident set size
 */
static size_t get_vm_top(void)
{
  /*
   * NOTE: sbrk is not part of the ANSI C library or the POSIX.1 standard
   * however it seems that everyone defines it. Calling sbrk with a 0
   * argument will return a pointer to the top of the process virtual
   * memory without changing the process size, so this call should be
   * reasonably safe (sbrk returns the new value for the top of memory).
   * This code relies on the notion that the address returned will be an 
   * offset from 0 (NULL), so the result of sbrk is cast to a size_t and 
   * returned. We really shouldn't be using it here but...
   */
  void* vptr = sbrk(0);
  return (size_t) vptr;
}

/*
 * get_maxrss - get the operating systems notion of the resident set size
 */
size_t get_maxrss(void)
{
  return get_vm_top() - initialVMTop;
}


/*
 * I re-wrote check_pings a tad
 *
 * check_pings()
 * inputs       - current time
 * output       - next time_t when check_pings() should be called again
 *
 * side effects - 
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

aClient *dying_clients[MAXCONNECTIONS]; /* list of dying clients */
char *dying_clients_reason[MAXCONNECTIONS];

static  time_t  check_pings(time_t currenttime)
{               
  register      aClient *cptr;          /* current local cptr being examined */
  aConfItem     *aconf = (aConfItem *)NULL;
  int           ping = 0;               /* ping time value from client */
  int           i;                      /* used to index through fd/cptr's */
  time_t        oldest = 0;             /* next ping time */
  time_t        timeout;                /* found necessary ping time */
  char          *reason;                /* pointer to reason string */
  int           die_index=0;            /* index into list */
  char          ping_time_out_buffer[64];   /* blech that should be a define */

#if defined(IDLE_CHECK) && defined(SEND_FAKE_KILL_TO_CLIENT)
  int           fakekill=0;
#endif /* IDLE_CHECK && SEND_FAKE_KILL_TO_CLIENT */

                                        /* of dying clients */
  dying_clients[0] = (aClient *)NULL;   /* mark first one empty */

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
      if (!(cptr = local[i]) || IsMe(cptr))
        continue;               /* and go examine next fd/cptr */
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
          continue;             /* and go examine next fd/cptr */
        }

      if (rehashed)
        {
          if(dline_in_progress)
            {
              if(IsPerson(cptr))
                {
                  if( (aconf = match_Dline(ntohl(cptr->ip.s_addr))) )

                      /* if there is a returned 
                       * aConfItem then kill it
                       */
                    {
                      if(IsConfElined(aconf))
                        {
                          sendto_realops("D-line over-ruled for %s client is E-lined",
                                     get_client_name(cptr,FALSE));
                                     continue;
                          continue;
                        }

                      sendto_realops("D-line active for %s",
                                 get_client_name(cptr, FALSE));

                      dying_clients[die_index] = cptr;
/* Wintrhawk */
#ifdef KLINE_WITH_CONNECTION_CLOSED
                      /*
                       * We use a generic non-descript message here on 
                       * purpose so as to prevent other users seeing the
                       * client disconnect from harassing the IRCops
                       */
                      reason = "Connection closed";
#else
#ifdef KLINE_WITH_REASON
                      reason = aconf->passwd ? aconf->passwd : "D-lined";
#else
                      reason = "D-lined";
#endif /* KLINE_WITH_REASON */
#endif /* KLINE_WITH_CONNECTION_CLOSED */

                      dying_clients_reason[die_index++] = reason;
                      dying_clients[die_index] = (aClient *)NULL;
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);
                      continue;         /* and go examine next fd/cptr */
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
/* Wintrhawk */
#ifdef KLINE_WITH_CONNECTION_CLOSED
                      /*
                       * We use a generic non-descript message here on 
                       * purpose so as to prevent other users seeing the
                       * client disconnect from harassing the IRCops
                       */
                      reason = "Connection closed";
#else
#ifdef KLINE_WITH_REASON
                      reason = aconf->passwd ? aconf->passwd : "G-lined";
#else
                      reason = "G-lined";
#endif /* KLINE_WITH_REASON */
#endif /* KLINE_WITH_CONNECTION_CLOSED */

                      dying_clients_reason[die_index++] = reason;
                      dying_clients[die_index] = (aClient *)NULL;
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);
                      continue;         /* and go examine next fd/cptr */
                    }
                  else
#endif
                  if((aconf = find_kill(cptr))) /* if there is a returned
                                                   aConfItem.. then kill it */
                    {
                      if(aconf->status & CONF_ELINE)
                        {
                          sendto_realops("K-line over-ruled for %s client is E-lined",
                                     get_client_name(cptr,FALSE));
                                     continue;
                        }

                      sendto_realops("K-line active for %s",
                                 get_client_name(cptr, FALSE));
                      dying_clients[die_index] = cptr;

/* Wintrhawk */
#ifdef KLINE_WITH_CONNECTION_CLOSED
                      /*
                       * We use a generic non-descript message here on 
                       * purpose so as to prevent other users seeing the
                       * client disconnect from harassing the IRCops
                       */
                      reason = "Connection closed";
#else
#ifdef KLINE_WITH_REASON
                      reason = aconf->passwd ? aconf->passwd : "K-lined";
#else
                      reason = "K-lined";
#endif /* KLINE_WITH_REASON */
#endif /* KLINE_WITH_CONNECTION_CLOSED */

                      dying_clients_reason[die_index++] = reason;
                      dying_clients[die_index] = (aClient *)NULL;
                      sendto_one(cptr, form_str(ERR_YOUREBANNEDCREEP),
                                 me.name, cptr->name, reason);
                      continue;         /* and go examine next fd/cptr */
                    }
                }
            }
        }

#ifdef IDLE_CHECK
      if (IsPerson(cptr))
        {
          if( !IsElined(cptr) &&
              IDLETIME && 
#ifdef OPER_IDLE
              !IsAnOper(cptr) &&
#endif /* OPER_IDLE */
              !IsIdlelined(cptr) && 
              ((CurrentTime - cptr->user->last) > IDLETIME))
            {
              aConfItem *aconf;

              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "Idle time limit exceeded";
#if defined(SEND_FAKE_KILL_TO_CLIENT) && defined(IDLE_CHECK)
              fakekill = 1;
#endif /* SEND_FAKE_KILL_TO_CLIENT && IDLE_CHECK */
              dying_clients[die_index] = (aClient *)NULL;

              aconf = make_conf();
              aconf->status = CONF_KILL;
              DupString(aconf->host, cptr->user->host);
              DupString(aconf->passwd, "idle exceeder" );
              DupString(aconf->name, cptr->user->username);
              aconf->port = 0;
              aconf->hold = CurrentTime + 60;
              add_temp_kline(aconf);
              sendto_realops("Idle time limit exceeded for %s - temp k-lining",
                         get_client_name(cptr,FALSE));
              continue;         /* and go examine next fd/cptr */
            }
        }
#endif

#ifdef REJECT_HOLD
      if (IsRejectHeld(cptr))
        {
          if( CurrentTime > (cptr->firsttime + REJECT_HOLD_TIME) )
            {
              if( reject_held_fds )
                reject_held_fds--;

              dying_clients[die_index] = cptr;
              dying_clients_reason[die_index++] = "reject held client";
              dying_clients[die_index] = (aClient *)NULL;
              continue;         /* and go examine next fd/cptr */
            }
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
               (cptr->flags & FLAGS_PINGSENT)))
            {
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
          if (cptr->firsttime ? ((CurrentTime - cptr->firsttime) > 100) : 0)
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

          /* ugh. this is horrible.
           * but I can get away with this hack because of the
           * block allocator, and right now,I want to find out
           * just exactly why occasional already bit cleared errors
           * are still happening
           */
          if(cptr->flags2 & FLAGS2_ALREADY_EXITED)
            {
              sendto_realops("Client already exited doing ping timeout %X",cptr);
            }
          else
            (void)exit_client(cptr, cptr, &me, ping_time_out_buffer );
          cptr->flags2 |= FLAGS2_ALREADY_EXITED;
        }
      else
#if defined(SEND_FAKE_KILL_TO_CLIENT) && defined(IDLE_CHECK)
        {
          if (fakekill)
            sendto_prefix_one(cptr, cptr, ":AutoKILL KILL %s :(%s)",
            cptr->name, dying_clients_reason[die_index]);
          /* ugh. this is horrible.
           * but I can get away with this hack because of the
           * block allocator, and right now,I want to find out
           * just exactly why occasional already bit cleared errors
           * are still happening
           */
          if(cptr->flags2 & FLAGS2_ALREADY_EXITED)
            {
              sendto_realops("Client already exited %X",cptr);
            }
          else
            (void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
          cptr->flags2 |= FLAGS2_ALREADY_EXITED;
        }
#else 
          /* ugh. this is horrible.
           * but I can get away with this hack because of the
           * block allocator, and right now,I want to find out
           * just exactly why occasional already bit cleared errors
           * are still happening
           */
          if(cptr->flags2 & FLAGS2_ALREADY_EXITED)
            {
              sendto_realops("Client already exited %X",cptr);
            }
          else
            (void)exit_client(cptr, cptr, &me, dying_clients_reason[die_index]);
          cptr->flags2 |= FLAGS2_ALREADY_EXITED;          
#endif /* SEND_FAKE_KILL_TO_CLIENT && IDLE_CHECK */
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
 * bad_command
 *      This is called when the commandline is not acceptable.
 *      Give error message and exit without starting anything.
 */
static int bad_command()
{
  fprintf(stderr, 
          "Usage: ircd [-f config] [-h servername] [-x loglevel] [-s] [-t]\n");
  fprintf(stderr, "Server not started\n\n");
  return -1;
}

/* code added by mika nystrom (mnystrom@mit.edu) */
/* this flag is used to signal globally that the server is heavily loaded,
   something which can be taken into account when processing e.g. user commands
   and scheduling ping checks */
/* Changed by Taner Halicioglu (taner@CERF.NET) */

#define LOADCFREQ 5     /* every 5s */
#define LOADRECV 40     /* 40k/s */

int    LRV = LOADRECV;
time_t LCF = LOADCFREQ;
float currlife = 0.0;

int main(int argc, char *argv[])
{
  uid_t       uid;
  uid_t       euid;
  time_t      delay = 0;
  aConfItem*  aconf;

  /*
   * save server boot time right away, so getrusage works correctly
   */
  if ((CurrentTime = time(NULL)) == -1)
    {
      fprintf(stderr,"ERROR: Clock Failure (%d)\n", errno);
      exit(errno);
    }

  ServerRunning = 0;
  memset(&me, 0, sizeof(me));
  GlobalClientList = &me;       /* Pointer to beginning of Client list */
  cold_start = YES;             /* set when server first starts up */

  memset(&Count, 0, sizeof(Count));
  Count.server = 1;     /* us */

  /* 
   * set initialVMTop before we allocate any memory
   */
  initialVMTop = get_vm_top();

  initialize_global_set_options();

#ifdef REJECT_HOLD
  reject_held_fds = 0;
#endif

/* this code by mika@cs.caltech.edu */
/* it is intended to keep the ircd from being swapped out. BSD swapping

   criteria do not match the requirements of ircd */

#ifdef SETUID_ROOT
  if(plock(TXTLOCK)<0) fprintf(stderr,"could not plock...\n");
  if(setuid(IRCD_UID)<0)exit(-1); /* blah.. this should be done better */
#endif

  dbuf_init();  /* set up some dbuf stuff to control paging */

  uid = getuid();
  euid = geteuid();

  ConfigFileEntry.dpath = DPATH;

  ConfigFileEntry.configfile = CPATH;   /* Server configuration file */

#ifdef KPATH
  ConfigFileEntry.klinefile = KPATH;         /* Server kline file */
#else
  ConfigFileEntry.klinefile = CPATH;
#endif /* KPATH */

#ifdef DLPATH
  ConfigFileEntry.dlinefile = DLPATH;
#else
  ConfigFileEntry.dlinefile = CPATH;
#endif /* DLPATH */

#ifdef GLINES
  ConfigFileEntry.glinefile = GLINEFILE;
#endif

#ifdef  CHROOTDIR
  if (chdir(DPATH))
    {
      perror("chdir " DPATH );
      exit(-1);
    }

  if (chroot(DPATH))
    {
      fprintf(stderr,"ERROR:  Cannot chdir/chroot\n");
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
  umask(077);                /* better safe than sorry --SRB */

  setup_signals();
  
  /*
  ** All command line parameters have the syntax "-fstring"
  ** or "-f string" (e.g. the space is optional). String may
  ** be empty. Flag characters cannot be concatenated (like
  ** "-fxyz"), it would conflict with the form "-fstring".
  */
  while (--argc > 0 && (*++argv)[0] == '-')
    {
      char *p = argv[0]+1;
      int  flag = *p++;

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
        case 'c':
          bootopt |= BOOT_CONSOLE;
          break;
        case 'd' :
          setuid((uid_t) uid);
          ConfigFileEntry.dpath = p;
          break;
#ifdef CMDLINE_CONFIG
        case 'f':
          setuid((uid_t) uid);
          ConfigFileEntry.configfile = p;
          break;

#ifdef KPATH
        case 'k':
          setuid((uid_t) uid);
          ConfigFileEntry.klinefile = p;
          break;
#endif

#endif
        case 'h':
          strncpy_irc(me.name, p, HOSTLEN);
          break;
        case 's':
          bootopt |= BOOT_STDERR;
          break;
        case 't':
          setuid((uid_t) uid);
          bootopt |= BOOT_TTY;
          break;
        case 'v':
          printf("ircd %s\n\tzlib %s\n\tircd_dir: %s\n", version,
#ifndef ZIP_LINKS
                       "not used",
#else
                       zlib_version,
#endif
                       ConfigFileEntry.dpath);
          exit(0);
        case 'x':
#ifdef  DEBUGMODE
          setuid((uid_t) uid);
          debuglevel = atoi(p);
          debugmode = *p ? p : "0";
          bootopt |= BOOT_DEBUG;
          break;
#else
          fprintf(stderr, "%s: DEBUGMODE must be defined for -x y\n",
                  myargv[0]);
          exit(0);
#endif
        default:
          bad_command();
          break;
        }
    }

#ifndef CHROOT
  if (chdir(ConfigFileEntry.dpath))
    {
      perror("chdir");
      exit(-1);
    }
#endif

#if !defined(IRC_UID)
  if ((uid != euid) && !euid)
    {
      fprintf(stderr,
              "ERROR: do not run ircd setuid root. " \
              "Make it setuid a normal user.\n");
      exit(-1);
    }
#endif

#if !defined(CHROOTDIR) || (defined(IRC_UID) && defined(IRC_GID))

  setuid((uid_t)euid);

  if (getuid() == 0)
    {
# if defined(IRC_UID) && defined(IRC_GID)

      /* run as a specified user */
      fprintf(stderr,"WARNING: running ircd with uid = %d\n", IRC_UID);
      fprintf(stderr,"         changing to gid %d.\n",IRC_GID);

      /* setgid/setuid previous usage noted unsafe by ficus@neptho.net
       */

      if (setgid(IRC_GID) < 0)
        {
          fprintf(stderr,"ERROR: can't setgid(%d)\n", IRC_GID);
          exit(-1);
        }

      if(setuid(IRC_UID) < 0)
        {
          fprintf(stderr,"ERROR: can't setuid(%d)\n", IRC_UID);
          exit(-1);
        }

#else
      /* check for setuid root as usual */
      fprintf(stderr,
              "ERROR: do not run ircd setuid root. " \
              "Make it setuid a normal user.\n");
      return -1;
# endif 
            } 
#endif /*CHROOTDIR/UID/GID*/

  if (argc > 0)
    return bad_command(); /* This should exit out */

  initialize_message_files();

  clear_client_hash_table();
  clear_channel_hash_table();
  clear_scache_hash_table();    /* server cache name table */
  clear_ip_hash_table();        /* client host ip hash table */
  clear_Dline_table();          /* d line tree */
  initlists();
  initclass();
  initwhowas();
  init_stats();
  init_tree_parse(msgtab);      /* tree parse code (orabidoo) */

  fdlist_init();
  open_debugfile();

#if 0
  if ((CurrentTime = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }
#endif

  init_sys();

#ifdef USE_SYSLOG
  openlog("ircd", LOG_PID | LOG_NDELAY, LOG_FACILITY);
#endif

  read_conf_files(YES);         /* cold start init conf files */

  aconf = find_me();
  strncpy_irc(me.name, aconf->host, HOSTLEN);
  strncpy_irc(me.host, aconf->host, HOSTLEN);

  me.fd = -1;
  me.hopcount = 0;
  me.confs = NULL;
  me.next = NULL;
  me.user = NULL;
  me.from = &me;
  me.servptr = &me;
  SetMe(&me);
  make_server(&me);
  me.serv->up = me.name;
  me.lasttime = me.since = me.firsttime = CurrentTime;
  add_to_client_hash_table(me.name, &me);

  check_class();
  write_pidfile();

  Debug((DEBUG_NOTICE,"Server ready..."));
  if (bootopt & BOOT_STDERR)
    fprintf(stderr,"Server Ready\n");
#ifdef USE_SYSLOG
  syslog(LOG_NOTICE, "Server Ready");
#endif

  ServerRunning = 1;
  while (ServerRunning)
    delay = io_loop(delay);
  return 0;
}

time_t io_loop(time_t delay)
{
  static char   to_send[200];
  static time_t lasttime  = 0;
  static long   lastrecvK = 0;
  static int    lrv       = 0;
  time_t        lasttimeofday;

  lasttimeofday = CurrentTime;
  if ((CurrentTime = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  if (CurrentTime < lasttimeofday)
    {
      ircsprintf(to_send, "System clock is running backwards - (%d < %d)",
                 CurrentTime, lasttimeofday);
      report_error(to_send, me.name, 0);
    }
  else if ((lasttimeofday + 60) < CurrentTime)
    {
      ircsprintf(to_send,
                 "System clock was reset into the future - (%d+60 > %d)",
                 CurrentTime, lasttimeofday);
      report_error(to_send, me.name, 0);
      sync_channels(CurrentTime - lasttimeofday);
    }

  /*
   * This chunk of code determines whether or not
   * "life sucks", that is to say if the traffic
   * level is so high that standard server
   * commands should be restricted
   *
   * Changed by Taner so that it tells you what's going on
   * as well as allows forced on (long LCF), etc...
   */
  
  if ((CurrentTime - lasttime) >= LCF)
    {
      lrv = LRV * LCF;
      lasttime = CurrentTime;
      currlife = (float)((long)me.receiveK - lastrecvK)/(float)LCF;
      if (((long)me.receiveK - lrv) > lastrecvK )
        {
          if (!LIFESUX)
            {
              LIFESUX = 1;

              if (NOISYHTM)
                {
                  sprintf(to_send, 
                        "Entering high-traffic mode - (%.1fk/s > %dk/s)",
                                (float)currlife, LRV);
                  sendto_ops(to_send);
                }
            }
          else
            {
              LIFESUX++;                /* Ok, life really sucks! */
              LCF += 2;                 /* Wait even longer */
              if (NOISYHTM) 
                {
                  sprintf(to_send,
                        "Still high-traffic mode %d%s (%d delay): %.1fk/s",
                                LIFESUX,
                                (LIFESUX & 0x04) ?  " (TURBO)" : "",
                                (int)LCF, (float)currlife);
                  sendto_ops(to_send);
                }
            }
        }
      else
        {
          LCF = LOADCFREQ;
          if (LIFESUX)
            {
              LIFESUX = 0;
              if (NOISYHTM)
                sendto_ops("Resuming standard operation . . . .");
            }
        }
      lastrecvK = (long)me.receiveK;
    }

  /*
  ** We only want to connect if a connection is due,
  ** not every time through.  Note, if there are no
  ** active C lines, this call to Tryconnections is
  ** made once only; it will return 0. - avalon
  */
  if (nextconnect && CurrentTime >= nextconnect)
    nextconnect = try_connections(CurrentTime);
  /*
   * DNS checks, use smaller of resolver delay or next ping
   */
  delay = IRCD_MIN(timeout_resolver(CurrentTime), nextping);
  /*
  ** take the smaller of the two 'timed' event times as
  ** the time of next event (stops us being late :) - avalon
  ** WARNING - nextconnect can return 0!
  */
  if (nextconnect)
    delay = IRCD_MIN(nextping, nextconnect);
  delay -= CurrentTime;
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
    delay = IRCD_MIN(delay, TIMESEC);
  /*
   * We want to read servers on every io_loop, as well
   * as "busy" clients (which again, includes servers.
   * If "lifesux", then we read servers AGAIN, and then
   * flush any data to servers.
   *    -Taner
   */

#ifndef NO_PRIORITY
  read_message(0, FDL_SERVER);
  read_message(1, FDL_BUSY);
  if (LIFESUX)
    {
      read_message(1, FDL_SERVER);
      if (LIFESUX & 0x4)
        {       /* life really sucks */
          read_message(1, FDL_BUSY);
          read_message(1, FDL_SERVER);
        }
      flush_server_connections();
    }
  if ((CurrentTime = time(NULL)) == -1)
    {
#ifdef USE_SYSLOG
      syslog(LOG_WARNING, "Clock Failure (%d), TS can be corrupted", errno);
#endif
      sendto_ops("Clock Failure (%d), TS can be corrupted", errno);
    }

  /*
   * CLIENT_SERVER = TRUE:
   *    If we're in normal mode, or if "lifesux" and a few
   *    seconds have passed, then read everything.
   * CLIENT_SERVER = FALSE:
   *    If it's been more than lifesux*2 seconds (that is, 
   *    at most 1 second, or at least 2s when lifesux is
   *    != 0) check everything.
   *    -Taner
   */
  {
    static time_t lasttime=0;
#ifdef CLIENT_SERVER
    if (!LIFESUX || (lasttime + LIFESUX) < CurrentTime)
      {
#else
    if ((lasttime + (LIFESUX + 1)) < CurrentTime)
      {
#endif
        read_message(delay, FDL_ALL); /*  check everything! */
        lasttime = CurrentTime;
      }
   }
#else
  read_message(delay, FDL_ALL); /*  check everything! */
  flush_server_connections();
#endif

  /*
  ** ...perhaps should not do these loops every time,
  ** but only if there is some chance of something
  ** happening (but, note that conf->hold times may
  ** be changed elsewhere--so precomputed next event
  ** time might be too far away... (similarly with
  ** ping times) --msa
  */

  if (CurrentTime >= nextping) {
    nextping = check_pings(CurrentTime);
    timeout_auth_queries(CurrentTime);
  }

  if (dorehash && !LIFESUX)
    {
      rehash(&me, &me, 1);
      dorehash = 0;
    }
  /*
  ** Flush output buffers on all connections now if they
  ** have data in them (or at least try to flush)
  ** -avalon
  */
  flush_connections(0);

#ifndef NO_PRIORITY
  fdlist_check(CurrentTime);
#endif

  Debug((DEBUG_DEBUG,"About to return delay %d",delay));
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
static void open_debugfile()
{
#ifdef  DEBUGMODE
  int   fd;
  const char* name = LOGFILE;

  if (debuglevel >= 0)
    {
      printf("isatty = %d ttyname = %#x\n", isatty(2), ttyname(2));
      if (!(bootopt & BOOT_TTY)) /* leave debugging output on fd 2 */
        {
          truncate(name, 0);
          if ((fd = open(name, O_WRONLY | O_CREAT, 0600)) < 0) 
            if ((fd = open("/dev/null", O_WRONLY)) < 0)
              exit(-1);
          if (fd != 2)
            {
              dup2(fd, 2);
              close(fd); 
            }
        }
      else if (isatty(2) && ttyname(2))
        name = ttyname(2);
      else
        name = "FD2-Pipe";
      Debug((DEBUG_FATAL, "Debug: File <%s> Level: %d at %s",
             name, debuglevel, myctime(time(NULL))));
    }
#endif
}

/*
 * simple function added because its used more than once
 * - Dianora
 */

void report_error_on_tty(const char *error_message)
{
  int fd;
  if ((fd = open("/dev/tty", O_WRONLY)) != -1)
    {
      write(fd, error_message, strlen(error_message));
      close(fd);
    }
}


/*
 * initalialize_global_set_options
 *
 * inputs       - none
 * output       - none
 * side effects - This sets all global set options needed 
 */

static void initialize_global_set_options(void)
{
  memset( &GlobalSetOptions, 0, sizeof(GlobalSetOptions));

  MAXCLIENTS = MAX_CLIENTS;
  NOISYHTM = NOISY_HTM;
  GlobalSetOptions.autoconn = 1;

#ifdef FLUD
  FLUDNUM = FLUD_NUM;
  FLUDTIME = FLUD_TIME;
  FLUDBLOCK = FLUD_BLOCK;
#endif

#ifdef IDLE_CHECK
  IDLETIME = MIN_IDLETIME;
#endif

#ifdef ANTI_SPAMBOT
  SPAMTIME = MIN_JOIN_LEAVE_TIME;
  SPAMNUM = MAX_JOIN_LEAVE_COUNT;
#endif

#ifdef ANTI_DRONE_FLOOD
  DRONETIME = DEFAULT_DRONE_TIME;
  DRONECOUNT = DEFAULT_DRONE_COUNT;
#endif

#ifdef NEED_SPLITCODE
 SPLITDELAY = (DEFAULT_SERVER_SPLIT_RECOVERY_TIME * 60);
 SPLITNUM = SPLIT_SMALLNET_SIZE;
 SPLITUSERS = SPLIT_SMALLNET_USER_SIZE;
 server_split_time = CurrentTime;
#endif

 /* End of global set options */

}

/*
 * initialize_message_files
 *
 * inputs       - none
 * output       - none
 * side effects - Set up all message files needed, motd etc.
 */

static void initialize_message_files(void)
  {
  InitMessageFile( HELP_MOTD, HPATH, &ConfigFileEntry.helpfile );
  InitMessageFile( USER_MOTD, MPATH, &ConfigFileEntry.motd );
  InitMessageFile( OPER_MOTD, OPATH, &ConfigFileEntry.opermotd );

  ReadMessageFile( &ConfigFileEntry.helpfile );
  ReadMessageFile( &ConfigFileEntry.motd );
  ReadMessageFile( &ConfigFileEntry.opermotd );
  }

/*
 * write_pidfile
 *
 * inputs       - none
 * output       - none
 * side effects - write the pid of the ircd to PPATH
 */

static void write_pidfile(void)
{
  int fd;
  char buff[20];
  if ((fd = open(PPATH, O_CREAT|O_WRONLY, 0600))>=0)
    {
      ircsprintf(buff,"%d\n", (int)getpid());
      if (write(fd, buff, strlen(buff)) == -1)
        Debug((DEBUG_NOTICE,"Error writing to pid file %s",
               PPATH));
      close(fd);
      return;
    }
#ifdef        DEBUGMODE
  else
    Debug((DEBUG_NOTICE,"Error opening pid file %s", PPATH));
#endif
}
