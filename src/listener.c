/************************************************************************
 *   IRC - Internet Relay Chat, src/listener.c
 *   Copyright (C) 1999 Thomas Helvey <tomh@inxpress.net>
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
#include "listener.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "ircd_defs.h"
#include "h.h"
#include "send.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

struct Listener* ListenerPollList = 0;

struct Listener* make_listener(const char* name, int port)
{
  struct Listener* listener = 
    (struct Listener*) MyMalloc(sizeof(struct Listener));
  assert(0 != listener);

  memset(listener, 0, sizeof(struct Listener));

  listener->name        = name;
  listener->fd          = -1;
  listener->port        = port;
  listener->addr.s_addr = INADDR_ANY;

#ifdef NULL_POINTER_NOT_ZERO
  listener->next = NULL;
  listener->conf = NULL;
#endif
  return listener;
}

void free_listener(struct Listener* listener)
{
  assert(0 != listener);
  if (listener->conf && IsIllegal(listener->conf)) {
    assert(0 < listener->conf->clients);
    if (0 == --listener->conf->clients)
      free_conf(listener->conf);
  }
  MyFree(listener);
}

#define PORTNAMELEN 6  /* ":31337" */

/*
 * get_listener_name - return displayable listener name and port
 * returns "host.foo.org:6667" for a given listener
 */
const char* get_listener_name(const struct Listener* listener)
{
  static char buf[HOSTLEN + HOSTLEN + PORTNAMELEN + 4];
  assert(0 != listener);
  ircsprintf(buf, "%s[%s/%u]", listener->name, listener->name, listener->port);
#if 0
  /* 
   * NOTE: strcpy ok, listener->name is always <= HOSTLEN
   */
  strcpy(buf, listener->name);
  /*
   * XXX - ircsprintf screws this up, sometimes it puts an extra space in
   */
  sprintf(buf + strlen(buf), ":%d", listener->port);
#endif
  return buf;
}

/*
 * get_listener_name_r - reentrant version of get_listener_name
 */
const char* get_listener_name_r(const struct Listener* listener, 
                                char* buf, size_t len)
{
  size_t s_len;

  strncpy(buf, listener->name, len);
  buf[len - 1] = '\0';
  s_len = strlen(buf);
  if ((s_len + PORTNAMELEN) < len)
    sprintf(buf + s_len, ":%d", listener->port);
  return buf;
}
  
/*
 * inetport - create a listener socket in the AF_INET domain, 
 * bind it to the port given in 'port' and listen to it  
 * returns true (1) if successful false (0) on error.
 *
 * If the operating system has a define for SOMAXCONN, use it, otherwise
 *   use HYBRID_SOMAXCONN -Dianora
 */
#ifdef SOMAXCONN
#undef HYBRID_SOMAXCONN
#define HYBRID_SOMAXCONN SOMAXCONN
#endif

static int inetport(struct Listener* listener)
{
  struct sockaddr_in sin;
  int                fd;
  int                opt = 1;

  /*
   * At first, open a new socket
   */
  fd = socket(AF_INET, SOCK_STREAM, 0);

  if (-1 == fd) {
    report_error("opening listener socket %s:%s", 
                 get_listener_name(listener), errno);
    return 0;
  }
  else if ((HARD_FDLIMIT - 10) < fd) {
    report_error("no more connections left for listener %s:%s", 
                 get_listener_name(listener), errno);
    close(fd);
    return 0;
  }
  /* 
   * XXX - we don't want to do all this crap for a listener
   * set_sock_opts(listener);
   */ 
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*) &opt, sizeof(opt))) {
    report_error("setting SO_REUSEADDR for listener %s:%s", 
                 get_listener_name(listener), errno);
    close(fd);
    return 0;
  }

  /*
   * Bind a port to listen for new connections if port is non-null,
   * else assume it is already open and try get something from it.
   */
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr   = listener->addr;
  sin.sin_port   = htons(listener->port);

  if (INADDR_ANY != listener->addr.s_addr) {
    struct hostent* hp;
    /*
     * XXX - blocking call to gethostbyaddr
     */
    if ((hp = gethostbyaddr((char*) &listener->addr, 
                            sizeof(struct sockaddr_in), AF_INET))) {
      strncpy(listener->vhost, hp->h_name, HOSTLEN);
      listener->name = listener->vhost;
    }
  }

  if (bind(fd, (struct sockaddr*) &sin, sizeof(sin))) {
    report_error("binding listener socket %s:%s", 
                 get_listener_name(listener), errno);
    close(fd);
    return 0;
  }

  if (listen(fd, HYBRID_SOMAXCONN)) {
    report_error("listen failed for %s:%s", 
                 get_listener_name(listener), errno);
    close(fd);
    return 0;
  }

  /*
   * XXX - this should always work, performance will suck if it doesn't
   */
  if (!set_non_blocking(fd))
    report_error(NB_ERROR_MESSAGE, get_listener_name(listener), errno);

  listener->fd = fd;

  return 1;
}

  
/*
 * add_listener
 *
 * Create a new client
 * for a socket that is passive (listen'ing for connections to be accepted).
 */
void add_listener(struct ConfItem* conf)
{
  struct Listener* listener;
  struct in_addr   vaddr;

  assert(0 != conf);
  /*
   * if no port in conf line, don't bother
   */
  if (!conf->port)
    return;

  listener = make_listener(me.name, conf->port);

  if (*conf->passwd && '*' != *conf->passwd) {
    vaddr.s_addr = inet_addr(conf->passwd);
    if (INADDR_NONE != vaddr.s_addr)
      listener->addr = vaddr;
  }

  if (inetport(listener)) {
    ++conf->clients;
    listener->conf   = conf;
    listener->next   = ListenerPollList;
    ListenerPollList = listener; 
  }
  else
    free_listener(listener);
}

/*
 * close_listener - close a single listener
 */
void close_listener(struct Listener* listener)
{
  assert(0 != listener);
  /*
   * remove from listener list
   */
  if (listener == ListenerPollList)
    ListenerPollList = listener->next;
  else {
    struct Listener* prev = ListenerPollList;
    for ( ; prev; prev = prev->next) {
      if (listener == prev->next) {
        prev->next = listener->next;
        break; 
      }
    }
  }
  if (-1 < listener->fd)
    close(listener->fd);
  free_listener(listener);
}
 
/*
 * close_listeners - close and free all listeners that are not being used
 */
void close_listeners()
{
  struct Listener* listener;
  struct Listener* listener_next = 0;
  /*
   * close all 'extra' listening ports we have
   */
  for (listener = ListenerPollList; listener; listener = listener_next) {
    listener_next = listener->next;
    if (IsIllegal(listener->conf) && 0 == listener->ref_count)
      close_listener(listener);
  }
}

void accept_connection(struct Listener* listener)
{
  static time_t      last_oper_notice = 0;
  struct sockaddr_in addr;
  int                addrlen = sizeof(struct sockaddr_in);
  int                fd;

  assert(0 != listener);
  assert(0 != listener->conf);

  listener->last_accept = timeofday;
  /*
   * There may be many reasons for error return, but
   * in otherwise correctly working environment the
   * probable cause is running out of file descriptors
   * (EMFILE, ENFILE or others?). The man pages for
   * accept don't seem to list these as possible,
   * although it's obvious that it may happen here.
   * Thus no specific errors are tested at this
   * point, just assume that connections cannot
   * be accepted until some old is closed first.
   */
  if (-1 == (fd = accept(listener->fd, (struct sockaddr*) &addr, &addrlen))) {
    report_error("Error accepting connection %s:%s", 
                 listener->name, errno);
    return;
  }
  /*
   * check for connection limit
   */
  if ((MAXCONNECTIONS - 10) < fd) {
    ++ircstp->is_ref;
    /* 
     * slow down the whining to opers bit 
     */
    if((last_oper_notice + 20) <= timeofday) {
      sendto_realops("All connections in use. (%s)", 
                     get_listener_name(listener));
      last_oper_notice = timeofday;
    }
    send(fd, "ERROR :All connections in use\r\n", 32, 0);
    close(fd);
    return;
  }
  /*
   * check to see if listener is shutting down
   */
  if (IsIllegal(listener->conf)) {
    ++ircstp->is_ref;
    send(fd, "ERROR :Use another port\r\n", 25, 0);
    close(fd);
    return;
  }
  /*
   * check conf for ip address access
   */
#if 0
  if (!conf_connect_allowed(addr.sin_addr)) {
    ircstp->is_ref++;
#ifdef REPORT_DLINE_TO_USER
     send(fd, "NOTICE DLINE :*** You have been D-lined\r\n", 41, 0);
#endif
    close(fd);
    return;
  }
#endif
  ircstp->is_ac++;
  nextping = timeofday;

  add_connection(listener, fd);
}

