/*
 * restart.c
 *
 * $Id$
 */
#include "restart.h"
#include "common.h"
#include "h.h"
#include "ircd.h"
#include "send.h"
#include "struct.h"

/* function definition */

/* external var */
extern char** myargv;
extern void*  edata;

void restart(char *mesg)
{
  static int was_here = NO; /* redundant due to restarting flag below */

  if (was_here)
    abort();
  was_here = YES;

#ifdef  USE_SYSLOG
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

void server_reboot(void)
{
  int i;
  
  sendto_ops("Aieeeee!!!  Restarting server... memory: %d",
             edata );

  Debug((DEBUG_NOTICE,"Restarting server..."));
  flush_connections(0);

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


