#include "common.h"
#include "h.h"
#include "send.h"

#include <signal.h>


/* function definition */

/* external var */
extern char 	**myargv;
extern int 	dorehash;
extern void     *edata;

void restart(char *mesg)


{
  static int was_here = NO; /* redundant due to restarting flag below */

  if (was_here)
    abort();
  was_here = YES;

#ifdef	USE_SYSLOG
  syslog(LOG_WARNING, "Restarting Server because: %s, memory data limit: %ld",
         mesg, edata );
#endif
  if (bootopt & BOOT_STDERR)
    {
      fprintf(stderr, "Restarting Server because: %s, memory: %ld\n",
	      mesg, (unsigned long)edata);
    }
  server_reboot();
}

void s_restart()
{
  static int restarting = 0;

#ifdef	USE_SYSLOG
  syslog(LOG_WARNING, "Server Restarting on SIGINT");
#endif
  if (restarting == 0)
    {
      /* Send (or attempt to) a dying scream to oper if present */

      restarting = 1;
      server_reboot();
    }
}

void server_reboot()
{
  int i;
  
  sendto_ops("Aieeeee!!!  Restarting server... memory: %d",
	     edata );

  Debug((DEBUG_NOTICE,"Restarting server..."));
  flush_connections(me.fd);

#ifdef USE_SYSLOG
  closelog();
#endif

  for (i = 3; i < MAXCONNECTIONS; i++)
    close(i);
  if (!(bootopt & (BOOT_TTY | BOOT_DEBUG | BOOT_STDERR)))
    close(2);
  close(1);
  close(0);
  execv(SPATH, myargv);

#ifdef USE_SYSLOG
  /* Have to reopen since it has been closed above */
  openlog(myargv[0], LOG_PID|LOG_NDELAY, LOG_FACILITY);
  syslog(LOG_CRIT, "execv(%s,%s) failed: %m\n", SPATH, myargv[0]);
  closelog();
#endif
  Debug((DEBUG_FATAL,"Couldn't restart server: %s", strerror(errno)));
  exit(-1);
}

void s_die()  
{
  flush_connections(me.fd);
#ifdef  USE_SYSLOG
  syslog(LOG_CRIT, "Server killed By SIGTERM");
#endif
  exit(-1);
}
  
void s_rehash()
{
  dorehash = 1;
#if !defined(POSIX_SIGNALS)
  signal(SIGHUP, s_rehash);     /* sysV -argv */
#endif
}

/* not sure about where this should be */
void	 setup_signals()
{
#ifdef	POSIX_SIGNALS
  struct	sigaction act;

  act.sa_handler = SIG_IGN;
  act.sa_flags = 0;
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGPIPE);
  sigaddset(&act.sa_mask, SIGALRM);
# ifdef	SIGWINCH
  sigaddset(&act.sa_mask, SIGWINCH);
  sigaction(SIGWINCH, &act, NULL);
# endif
  sigaction(SIGPIPE, &act, NULL);
  act.sa_handler = dummy;
  sigaction(SIGALRM, &act, NULL);
  act.sa_handler = s_rehash;
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGHUP);
  sigaction(SIGHUP, &act, NULL);
  act.sa_handler = s_restart;
  sigaddset(&act.sa_mask, SIGINT);
  sigaction(SIGINT, &act, NULL);
  act.sa_handler = s_die;
  sigaddset(&act.sa_mask, SIGTERM);
  sigaction(SIGTERM, &act, NULL);

#else
# ifndef	HAVE_RELIABLE_SIGNALS
  signal(SIGPIPE, dummy);
#  ifdef	SIGWINCH
  signal(SIGWINCH, dummy);
#  endif
# else
#  ifdef	SIGWINCH
  signal(SIGWINCH, SIG_IGN);
#  endif
  signal(SIGPIPE, SIG_IGN);
# endif
  signal(SIGALRM, dummy);   
  signal(SIGHUP, s_rehash);
  signal(SIGTERM, s_die); 
  signal(SIGINT, s_restart);
#endif

#ifdef RESTARTING_SYSTEMCALLS
  /*
  ** At least on Apollo sr10.1 it seems continuing system calls
  ** after signal is the default. The following 'siginterrupt'
  ** should change that default to interrupting calls.
  */
  siginterrupt(SIGALRM, 1);
#endif
}
