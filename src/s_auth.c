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
 *
 * Changes:
 *   July 6, 1999 - Rewrote most of the code here. When a client connects
 *     to the server and passes initial socket validation checks, it
 *     is owned by this module (auth) which returns it to the rest of the
 *     server when dns and auth queries are finished. Until the client is
 *     released, the server does not know it exists and does not process
 *     any messages from it.
 *     --Bleep  Thomas Helvey <tomh@inxpress.net>
 */
#include "s_auth.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "s_bsd.h"
#include "h.h"
#include "res.h"
#include "send.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <assert.h>

#ifdef USE_REPORT_MACROS
/*
 * moved these here from struct.h, this is the only place they're used
 * Changed: Added CR's 1459 sez CRLF is the record terminator
 * NOTE: These macros do not reduce the space required for these strings.
 * Notably, the Solaris compiler does not fold string constants so you will
 * get a new copy of the string for every location it's used in. 
 */
#define REPORT_DO_DNS    "NOTICE AUTH :*** Looking up your hostname...\r\n"
#define REPORT_FIN_DNS   "NOTICE AUTH :*** Found your hostname\r\n"
#define REPORT_FIN_DNSC  "NOTICE AUTH :*** Found your hostname, cached\r\n"
#define REPORT_FAIL_DNS  "NOTICE AUTH :*** Couldn't look up your hostname\r\n"
#define REPORT_DO_ID     "NOTICE AUTH :*** Checking Ident\r\n"
#define REPORT_FIN_ID    "NOTICE AUTH :*** Got Ident response\r\n"
#define REPORT_FAIL_ID   "NOTICE AUTH :*** No Ident response\r\n"
/*
 * We don't want to calculate these every time they are used :)
 * NOTE: Macros just do text replacement, if the compiler is brain dead
 * this will get computed anyways, replacement != optimization, it's still
 * up to the compiler to get it right. None of these buy anything but
 * notational convenience.
 */
#define R_do_dns   (sizeof(REPORT_DO_DNS)   - 1)
#define R_fin_dns  (sizeof(REPORT_FIN_DNS)  - 1)
#define R_fin_dnsc (sizeof(REPORT_FIN_DNSC) - 1)
#define R_fail_dns (sizeof(REPORT_FAIL_DNS) - 1)
#define R_do_id    (sizeof(REPORT_DO_ID)    - 1)
#define R_fin_id   (sizeof(REPORT_FIN_ID)   - 1)
#define R_fail_id  (sizeof(REPORT_FAIL_ID)  - 1)
/*
 * sendheader from orabidoo TS4
 * Changed, if a client is here, it's unknown
 */
#define sendheader(cptr, msg, len) \
   send((cptr)->fd, (msg), (len), 0)
/*
 * Using the above macros the preprocesor will substitute the text:
 * sendheader(client, REPORT_DNS, R_do_dns);
 * with the text:
 * send(client->fd, "NOTICE AUTH :*** Looking up your hostname...\r\n", \
 * (sizeof("NOTICE AUTH :*** Looking up your hostname...\r\n") - 1));
 * It can be observed by the preceding that what you see is not always
 * what you get, or what you wanted when you try to use the preprocessor 
 * for optimization.
 *
 * Sorry 'bout the rant, but many people have notions about preprocessors
 * that are simply incorrect. --Bleep
 */
#else
/*
 * a bit different approach
 */
static struct {
  const char* message;
  size_t      length;
} HeaderMessages [] = {
  /* 12345678901234567890123456789012345678901234567890123456 */
  { "NOTICE AUTH :*** Looking up your hostname...\r\n",    46 },
  { "NOTICE AUTH :*** Found your hostname\r\n",            38 },
  { "NOTICE AUTH :*** Found your hostname, cached\r\n",    46 },
  { "NOTICE AUTH :*** Couldn't look up your hostname\r\n", 49 },
  { "NOTICE AUTH :*** Checking Ident\r\n",                 33 },
  { "NOTICE AUTH :*** Got Ident response\r\n",             37 },
  { "NOTICE AUTH :*** No Ident response\r\n",              36 }
};

typedef enum {
  REPORT_DO_DNS,
  REPORT_FIN_DNS,
  REPORT_FIN_DNSC,
  REPORT_FAIL_DNS,
  REPORT_DO_ID,
  REPORT_FIN_ID,
  REPORT_FAIL_ID
} ReportType;

#define sendheader(c, r) \
   send((c)->fd, HeaderMessages[(r)].message, HeaderMessages[(r)].length, 0)
#endif

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
#ifdef USE_REPORT_MACROS
    sendheader(auth->client, REPORT_FIN_DNS, R_fin_dns);
#else
    sendheader(auth->client, REPORT_FIN_DNS);
#endif
  }
  else
#ifdef USE_REPORT_MACROS
    sendheader(auth->client, REPORT_FAIL_DNS, R_fail_dns);
#else
    sendheader(auth->client, REPORT_FAIL_DNS);
#endif

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
#ifdef USE_REPORT_MACROS
  sendheader(auth->client, REPORT_FAIL_ID, R_fail_id);
#else
  sendheader(auth->client, REPORT_FAIL_ID);
#endif

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

#ifdef USE_REPORT_MACROS
  sendheader(auth->client, REPORT_DO_ID, R_do_id);
#else
  sendheader(auth->client, REPORT_DO_ID);
#endif
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
#ifdef USE_REPORT_MACROS
      sendheader(auth->client, REPORT_FAIL_ID, R_fail_id);
#else
      sendheader(auth->client, REPORT_FAIL_ID);
#endif
      return 0;
    }
  }
  alarm(0);

  auth->fd = fd;

  SetAuthConnect(auth);
  return 1;
}

/*
 * GetValidIdent - parse ident query reply from identd server
 * 
 * Inputs        - pointer to ident buf
 * Output        - NULL if no valid ident found, otherwise pointer to name
 * Side effects        -
 */
static char* GetValidIdent(char *buf)
{
  int   remp = 0;
  int   locp = 0;
  char* colon1Ptr;
  char* colon2Ptr;
  char* colon3Ptr;
  char* commaPtr;
  char* remotePortString;

  /* All this to get rid of a sscanf() fun. */
  remotePortString = buf;
  
  colon1Ptr = strchr(remotePortString,':');
  if(!colon1Ptr)
    return 0;

  *colon1Ptr = '\0';
  colon1Ptr++;
  colon2Ptr = strchr(colon1Ptr,':');
  if(!colon2Ptr)
    return 0;

  *colon2Ptr = '\0';
  colon2Ptr++;
  commaPtr = strchr(remotePortString, ',');

  if(!commaPtr)
    return 0;

  *commaPtr = '\0';
  commaPtr++;

  remp = atoi(remotePortString);
  if(!remp)
    return 0;
              
  locp = atoi(commaPtr);
  if(!locp)
    return 0;

  /* look for USERID bordered by first pair of colons */
  if(!strstr(colon1Ptr, "USERID"))
    return 0;

  colon3Ptr = strchr(colon2Ptr,':');
  if(!colon3Ptr)
    return 0;
  
  *colon3Ptr = '\0';
  colon3Ptr++;
  Debug((DEBUG_INFO,"auth reply ok"));
  return(colon3Ptr);
}

/*
 * start_auth - starts auth (identd) and dns queries for a client
 */
void start_auth(struct Client* client)
{
  struct DNSQuery     query;
  struct AuthRequest* auth = 0;

  assert(0 != client);

  auth = make_auth_request(client);

  query.vptr     = auth;
  query.callback = auth_dns_callback;

#ifdef USE_REPORT_MACROS
  sendheader(client, REPORT_DO_DNS, R_do_dns);
#else
  sendheader(client, REPORT_DO_DNS);
#endif
  Debug((DEBUG_DNS, "lookup %s", inetntoa((char *)&addr.sin_addr)));

  client->hostp = gethost_byaddr((const char*) &client->ip, &query);
  if (client->hostp)
#ifdef USE_REPORT_MACROS
    sendheader(client, REPORT_FIN_DNSC, R_fin_dnsc);
#else
    sendheader(client, REPORT_FIN_DNSC);
#endif
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

#ifdef USE_REPORT_MACROS
      sendheader(auth->client, REPORT_FAIL_ID, R_fail_id);
#else
      sendheader(auth->client, REPORT_FAIL_ID);
#endif
      if (IsDNSPending(auth)) {
        delete_resolver_queries(auth);
#ifdef USE_REPORT_MACROS
        sendheader(auth->client, REPORT_FAIL_DNS, R_fail_dns);
#else
        sendheader(auth->client, REPORT_FAIL_DNS);
#endif
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
#ifdef USE_REPORT_MACROS
      sendheader(auth->client, REPORT_FAIL_DNS, R_fail_dns);
#else
      sendheader(auth->client, REPORT_FAIL_DNS);
#endif
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
  int   count;
  char  buf[AUTH_BUFSIZ + 1]; /* buffer to read auth reply into */

  Debug((DEBUG_NOTICE,"read_auth_reply(%x) fd %d authfd %d flags %d",
         auth, auth->client->fd, auth->fd, auth->flags));

  len = recv(auth->fd, buf, AUTH_BUFSIZ, 0);
  
  if (len > 0) {
    buf[len] = '\0';

    if( (s = GetValidIdent(buf)) ) {
      t = auth->client->username;
      for (count = USERLEN; *s && count; s++) {
        if (!isspace(*s) && *s != ':' && *s != '@') {
          *t++ = *s;
          count--;
        }
      }
      *t = '\0';
    }
  }

  close(auth->fd);
  auth->fd = -1;
  ClearAuth(auth);
  
  if (!s) {
    ++ircstp->is_abad;
    strcpy(auth->client->username, "unknown");
  }
  else {
#ifdef USE_REPORT_MACROS
    sendheader(auth->client, REPORT_FIN_ID, R_fin_id);
#else
    sendheader(auth->client, REPORT_FIN_ID);
#endif
    ++ircstp->is_asuc;
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

