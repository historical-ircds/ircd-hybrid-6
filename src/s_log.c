/************************************************************************
 *   IRC - Internet Relay Chat, src/s_log.c
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
 *   $Id$
 */
#include "s_log.h"
#include "irc_string.h"
#include "ircd.h"
#include "s_misc.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define LOG_BUFSIZE 2048 

static int logFile = -1;
static int logLevel = L_DEBUG;

static int sysLogLevel[] = {
  LOG_CRIT,
  LOG_ERR,
  LOG_WARNING,
  LOG_NOTICE,
  LOG_INFO,
  LOG_INFO,
  LOG_INFO
};

/*
 * open_log - open ircd logging file
 * returns true (1) if successful, false (0) otherwise
 */
static int open_log(const char* filename)
{
  logFile = open(filename, 
                 O_WRONLY | O_APPEND | O_CREAT | O_NONBLOCK, 0644);
  if (-1 == logFile) {
    syslog(LOG_ERR, "Unable to open log file: %s: %s",
           filename, strerror(errno));
    return 0;
  }
  return 1;
}

void close_log(void)
{
  if (-1 < logFile) {
    close(logFile);
    logFile = -1;
  }
#ifdef USE_SYSLOG  
  closelog();
#endif
}

static void write_log(const char* message)
{
  char buf[LOG_BUFSIZE];
  sprintf(buf, "[%s] %s\n", smalldate(CurrentTime), message);
  write(logFile, buf, strlen(buf));
}
   
void log(int priority, const char* fmt, ...)
{
  char    buf[LOG_BUFSIZE];
  va_list args;
  assert(-1 < priority);
  assert(0 != fmt);

  if (priority > logLevel)
    return;

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

#ifdef USE_SYSLOG  
  if (priority < L_DEBUG)
    syslog(sysLogLevel[priority], buf);
#endif
  write_log(buf);
}
  
void init_log(const char* filename)
{
  open_log(filename);
#ifdef USE_SYSLOG
  openlog("ircd", LOG_PID | LOG_NDELAY, LOG_FACILITY);
#endif
}

void set_log_level(int level)
{
  if (L_ERROR < level && level <= L_DEBUG)
    logLevel = level;
}


