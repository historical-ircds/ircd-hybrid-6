/************************************************************************
 *   IRC - Internet Relay Chat, src/s_auth.c
 *   Copyright (C) 1992 Darren Reed
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
#include "s_auth.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "s_bsd.h"
#include "h.h"
#include "res.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <assert.h>

struct AuthRequest* AuthPollList = 0; /* GLOBAL - auth queries pending io */

static struct AuthRequest* AuthIncompleteList = 0;

/*
 * make_auth_request - allocate a new auth request
 */
static struct AuthRequest* make_auth_request(struct Client* client)
{
  /*
   * XXX - use blalloc here?
   */
  struct AuthRequest* request = 
               (struct AuthRequest*) MyMalloc(sizeof(struct AuthRequest));
  assert(0 != request);
  memset(request, 0, sizeof(struct AuthRequest));
  request->fd      = -1;
  request->client  = client;
  request->timeout = timeofday + CONNECTTIMEOUT;
  return request;
}

/*
 * free_auth_request - cleanup auth request allocations
 */
void free_auth_request(struct AuthRequest* request)
{
  /*
   * XXX - use blfree here?
   */
  MyFree(request);
}

/*
 * unlink_auth_request - remove auth request from a list
 */
static void unlink_auth_request(struct AuthRequest* request,
                                struct AuthRequest** list)
{
  if (request->next)
    request->next->prev = request->prev;
  if (request->prev)
    request->prev->next = request->next;
  else
    *list = request->next;
}

/*
 * link_auth_request - add auth request to a list
 */
static void link_auth_request(struct AuthRequest* request,
                              struct AuthRequest** list)
{
  request->prev = 0;
  request->next = *list;
  if (*list)
    (*list)->prev = request;
  *list = request;
}

/*
 * release_auth_client - release auth client from auth system
 * this adds the client into the local client lists so it can be read by
 * the main io processing loop
 */
static void release_auth_client(struct Client* client)
{
  if (client->fd > highest_fd)
    highest_fd = client->fd;
  local[client->fd] = client;

  addto_fdlist(client->fd, &default_fdlist);
  add_client_to_list(client);
  
  SetAccess(client);
}
 
/*
 * auth_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * set the client on it's way to a connection completion, regardless
 * of success of failure
 */
static void auth_dns_callback(void* vptr, struct hostent* hp)
{
  struct AuthRequest* auth = (struct AuthRequest*) vptr;

  ClearDNSPending(auth);
  if (hp) {
    /*
     * XXX - we should copy info to the client here instead of saving
     * the pointer, holding the hp could be dangerous if the dns entries
     * are invalidated and not wiped here too, this also means that we
     * need to restart the dns query.
     */
    auth->client->hostp = hp;
    sendheader(auth->client, REPORT_FIN_DNS, R_fin_dns);
  }
  else
    sendheader(auth->client, REPORT_FAIL_DNS, R_fail_dns);

  if (!IsDoingAuth(auth)) {
    release_auth_client(auth->client);
    unlink_auth_request(auth, &AuthIncompleteList);
    free_auth_request(auth);
  }
}

/*
 * authsenderr - handle auth send errors
 */
static void auth_error(struct AuthRequest* auth)
{
  ++ircstp->is_abad;

  close(auth->fd);
  auth->fd = -1;

  ClearAuth(auth);
  sendheader(auth->client, REPORT_FAIL_ID, R_fail_id);

  unlink_auth_request(auth, &AuthPollList);

  if (IsDNSPending(auth))
    link_auth_request(auth, &AuthIncompleteList);
  else {
    release_auth_client(auth->client);
    free_auth_request(auth);
  }
}

/*
 * start_auth_query - Flag the client to show that an attempt to 
 * contact the ident server on
 * the client's host.  The connect and subsequently the socket are all put
 * into 'non-blocking' mode.  Should the connect or any later phase of the
 * identifing process fail, it is aborted and the user is given a username
 * of "unknown".
 */
static int start_auth_query(struct AuthRequest* auth)
{
  struct sockaddr_in sock;
  struct sockaddr_in localaddr;
  int                locallen = sizeof(struct sockaddr_in);
  int                fd;

  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
#ifdef        USE_SYSLOG
    syslog(LOG_ERR, "Unable to create auth socket for %s:%m",
           get_client_name(auth->client,TRUE));
#endif
    ++ircstp->is_abad;
    return 0;
  }
  if ((HARD_FDLIMIT - 10) <= fd) {
    sendto_ops("Can't allocate fd for auth on %s",
                get_client_name(auth->client, TRUE));

    close(fd);
    return 0;
  }

  sendheader(auth->client, REPORT_DO_ID, R_do_id);
  set_non_blocking(fd, auth->client);

  /* 
   * get the local address of the client and bind to that to
   * make the auth request.  This used to be done only for
   * ifdef VIRTTUAL_HOST, but needs to be done for all clients
   * since the ident request must originate from that same address--
   * and machines with multiple IP addresses are common now
   */
  memset(&localaddr, 0, locallen);
  getsockname(auth->client->fd, (struct sockaddr*) &localaddr, &locallen);
  localaddr.sin_port = htons(0);

  if (bind(fd, (struct sockaddr*)&localaddr, sizeof(localaddr)) == -1) {
    report_error("binding auth stream socket %s:%s", auth->client);
    close(fd);
    return 0;
  }

  memcpy(&sock.sin_addr, &auth->client->ip, sizeof(struct in_addr));
  
  sock.sin_port = htons(113);
  sock.sin_family = AF_INET;

  alarm(4);
  if (connect(fd, (struct sockaddr*) &sock, sizeof(sock)) == -1) {
    if (errno != EINPROGRESS) {
      ircstp->is_abad++;
      /*
       * No error report from this...
       */
      alarm(0);
      close(fd);
      sendheader(auth->client, REPORT_FAIL_ID, R_fail_id);
      return 0;
    }
  }
  alarm(0);

  auth->fd = fd;

  SetAuthConnect(auth);
  return 1;
}

void start_auth(struct Client* client)
{
  struct DNSQuery     query;
  struct AuthRequest* auth = 0;

  assert(0 != client);

  auth = make_auth_request(client);

  query.vptr     = auth;
  query.callback = auth_dns_callback;

  sendheader(client, REPORT_DO_DNS, R_do_dns);
  Debug((DEBUG_DNS, "lookup %s", inetntoa((char *)&addr.sin_addr)));

  client->hostp = gethost_byaddr((const char*) &client->ip, &query);
  if (client->hostp)
    sendheader(client, REPORT_FIN_DNSC, R_fin_dnsc);
  else
    SetDNSPending(auth);

  if (start_auth_query(auth))
    link_auth_request(auth, &AuthPollList);
  else if (IsDNSPending(auth))
    link_auth_request(auth, &AuthIncompleteList);
  else {
    free_auth_request(auth);
    release_auth_client(client);
  }
}

/*
 * timeout_auth_queries - timeout resolver and identd requests
 * allow clients through if requests failed
 */
void timeout_auth_queries(time_t now)
{
  struct AuthRequest* auth;
  struct AuthRequest* auth_next = 0;

  for (auth = AuthPollList; auth; auth = auth_next) {
    auth_next = auth->next;
    if (auth->timeout < timeofday) {
      if (-1 < auth->fd)
        close(auth->fd);

      sendheader(auth->client, REPORT_FAIL_ID, R_fail_id);
      if (IsDNSPending(auth)) {
        delete_resolver_queries(auth);
        sendheader(auth->client, REPORT_FAIL_DNS, R_fail_dns);
      }
      Debug((DEBUG_NOTICE,"DNS/AUTH timeout %s",
             get_client_name(auth->client,TRUE)));

      auth->client->since = now;
      release_auth_client(auth->client);
      unlink_auth_request(auth, &AuthPollList);
      free_auth_request(auth);
    }
  }
  for (auth = AuthIncompleteList; auth; auth = auth_next) {
    auth_next = auth->next;
    if (auth->timeout < timeofday) {
      delete_resolver_queries(auth);
      sendheader(auth->client, REPORT_FAIL_DNS, R_fail_dns);
      Debug((DEBUG_NOTICE,"DNS timeout %s",
             get_client_name(auth->client,TRUE)));

      auth->client->since = now;
      release_auth_client(auth->client);
      unlink_auth_request(auth, &AuthIncompleteList);
      free_auth_request(auth);
    }
  }
}

/*
 * send_auth_query - send the ident server a query giving "theirport , ourport"
 * The write is only attempted *once* so it is deemed to be a fail if the
 * entire write doesn't write all the data given.  This shouldnt be a
 * problem since the socket should have a write buffer far greater than
 * this message to store it in should problems arise. -avalon
 */
void send_auth_query(struct AuthRequest* auth)
{
  struct sockaddr_in us;
  struct sockaddr_in them;
  char            authbuf[32];
  int             ulen = sizeof(struct sockaddr_in);
  int             tlen = sizeof(struct sockaddr_in);

  Debug((DEBUG_NOTICE,"write_authports(%x) fd %d authfd %d flags %d",
         auth, auth->client->fd, auth->fd, auth->flags));

  if (getsockname(auth->client->fd, (struct sockaddr *)&us,   &ulen) ||
      getpeername(auth->client->fd, (struct sockaddr *)&them, &tlen)) {
#ifdef        USE_SYSLOG
    syslog(LOG_DEBUG, "auth get{sock,peer}name error for %s:%m",
           get_client_name(auth->client, TRUE));
#endif
    auth_error(auth);
    return;
  }
  ircsprintf(authbuf, "%u , %u\r\n",
             (unsigned int) ntohs(them.sin_port),
             (unsigned int) ntohs(us.sin_port));

  Debug((DEBUG_SEND, "sending [%s] to auth port %s.113",
         authbuf, inetntoa((char*) &them.sin_addr)));
      
  if (send(auth->fd, authbuf, strlen(authbuf), 0) == -1) {
    auth_error(auth);
    return;
  }
  ClearAuthConnect(auth);
  SetAuthPending(auth);
}


/*
 * read_auth_reply - read the reply (if any) from the ident server 
 * we connected to.
 * We only give it one shot, if the reply isn't good the first time
 * fail the authentication entirely. --Bleep
 */
#define AUTH_BUFSIZ 128

void read_auth_reply(struct AuthRequest* auth)
{
  char* s;
  char* t;
  int   len;
  char  buf[AUTH_BUFSIZ + 1]; /* buffer to read auth reply into */
  char  ruser[USERLEN + 1];
  char  tuser[USERLEN + 1];
  int   remp = 0;
  int   locp = 0;

  *ruser = '\0';
  Debug((DEBUG_NOTICE,"read_auth_reply(%x) fd %d authfd %d flags %d",
         auth, auth->client->fd, auth->fd, auth->flags));

  len = recv(auth->fd, buf, AUTH_BUFSIZ, 0);
  
  if (len > 0) {
    buf[len] = '\0';
    if (sscanf(buf, "%d , %d : USERID : %*[^:]: %10s", 
               &remp, &locp, tuser) == 3) {
      if ((s = strrchr(buf, ':'))) {
        t = ruser;
        for (++s; *s && (t < ruser + USERLEN); ++s) {
          if (!isspace(*s) && *s != ':' && *s != '@')
            *t++ = *s;
        }
        *t = '\0';
        Debug((DEBUG_INFO,"auth reply ok"));
      }
    }
  }
  close(auth->fd);
  auth->fd = -1;
  ClearAuth(auth);
  
  if (0 == locp || 0 == remp || !*ruser) {
    ++ircstp->is_abad;
    strcpy(auth->client->username, "unknown");
    return;
  }
  else {
    sendheader(auth->client, REPORT_FIN_ID, R_fin_id);
    ++ircstp->is_asuc;
    strncpy(auth->client->username, ruser, USERLEN);
    auth->client->username[USERLEN] = '\0';
    SetGotId(auth->client);
    Debug((DEBUG_INFO, "got username [%s]", ruser));
  }
  unlink_auth_request(auth, &AuthPollList);

  if (IsDNSPending(auth))
    link_auth_request(auth, &AuthIncompleteList);
  else {
    release_auth_client(auth->client);
    free_auth_request(auth);
  }
}


