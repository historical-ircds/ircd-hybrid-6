/*
 * src/res.c (C)opyright 1992 Darren Reed. All rights reserved.
 * This file may not be distributed without the author's permission in any
 * shape or form. The author takes no responsibility for any damage or loss
 * of property which results from the use of this software.
 *
 * $Id$
 *
 * July 1999 - Rewrote a bunch of stuff here. Change hostent builder code,
 *     added callbacks and reference counting of returned hostents.
 *     --Bleep (Thomas Helvey <tomh@inxpress.net>)
 */
#include "m_commands.h"
#include "res.h"
#include "client.h"
#include "common.h"
#include "irc_string.h"
#include "ircd.h"
#include "numeric.h"
#include "restart.h"
#include "s_bsd.h"
#include "send.h"
#include "struct.h"
#include "s_debug.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <arpa/inet.h>

#ifdef DEBUGMODE
#include <errno.h>
#endif

#include <limits.h>
#if (CHAR_BIT != 8)
#error this code needs to be able to address individual octets 
#endif

#undef  DEBUG  /* because there is a lot of debug code in here :-) */

/*
 * Some systems do not define INADDR_NONE (255.255.255.255)
 * INADDR_NONE is actually a valid address, but it should never
 * be returned from any nameserver.
 * NOTE: The bit pattern for INADDR_NONE and INADDR_ANY (0.0.0.0) should be 
 * the same on all hosts so we shouldn't need to use htonl or ntohl to
 * compare or set the values.
 */ 
#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

#define MAXPACKET       1024  /* rfc sez 512 but we expand names so ... */
#define RES_MAXALIASES  1	/* 35    /* maximum aliases allowed */
#define RES_MAXADDRS    1	/* 35    /* maximum addresses allowed */

/*
 * macros used to calulate offsets into fixed query buffer
 */
#define ALIAS_BLEN (size_t) ((RES_MAXALIASES + 1) * sizeof(char*))
#define ADDRS_BLEN (size_t) ((RES_MAXADDRS + 1) * sizeof(struct in_addr*))

#define ADDRS_OFFSET  (size_t) (ALIAS_BLEN + ADDRS_BLEN)
#define ADDRS_DLEN    (size_t) (RES_MAXADDRS * sizeof(struct in_addr))
#define NAMES_OFFSET  (size_t) (ADDRS_OFFSET + ADDRS_DLEN)
#define MAXGETHOSTLEN (size_t) (NAMES_OFFSET + MAXPACKET)

#define AR_TTL          600   /* minimum TTL in seconds for dns cache entries */

/*
 * RFC 1104/1105 wasn't very helpful about what these fields
 * should be named, so for now, we'll just name them this way.
 * we probably should look at what named calls them or something.
 */
#define TYPE_SIZE       (size_t) 2
#define CLASS_SIZE      (size_t) 2
#define TTL_SIZE        (size_t) 4
#define RDLENGTH_SIZE   (size_t) 2
#define ANSWER_FIXED_SIZE (TYPE_SIZE + CLASS_SIZE + TTL_SIZE + RDLENGTH_SIZE)

/*
 * Building the Hostent
 * The Hostent struct is arranged like this:
 *          +-------------------------------+
 * Hostent: | struct hostent h              |
 *          |-------------------------------|
 *          | char *buf                     |
 *          +-------------------------------+
 *
 * allocated:
 *
 *          +-------------------------------+
 * buf:     | h_aliases pointer array       | Max size: ALIAS_BLEN;
 *          | NULL                          | contains `char *'s
 *          |-------------------------------|
 *          | h_addr_list pointer array     | Max size: ADDRS_BLEN;
 *          | NULL                          | contains `struct in_addr *'s
 *          |-------------------------------|
 *          | h_addr_list addresses         | Max size: ADDRS_DLEN;
 *          |                               | contains `struct in_addr's
 *          |-------------------------------|
 *          | storage for hostname strings  | Max size: ALIAS_DLEN;
 *          +-------------------------------+ contains `char's
 *
 *  For requests the size of the h_aliases, and h_addr_list pointer
 *  array sizes are set to MAXALISES and MAXADDRS respectively, and
 *  buf is a fixed size with enough space to hold the largest expected
 *  reply from a nameserver, see RFC 1034 and RFC 1035.
 *  For cached entries the sizes are dependent on the actual number
 *  of aliases and addresses. If new aliases and addresses are found
 *  for cached entries, the buffer is grown and the new entries are added.
 *  The hostent struct is filled in with the addresses of the entries in
 *  the Hostent buf as follows:
 *  h_name      - contains a pointer to the start of the hostname string area,
 *                or NULL if none is set.  The h_name is followed by the
 *                aliases, in the storage for hostname strings area.
 *  h_aliases   - contains a pointer to the start of h_aliases pointer array.
 *                This array contains pointers to the storage for hostname
 *                strings area and is terminated with a NULL.  The first alias
 *                is stored directly after the h_name.
 *  h_addr_list - contains a pointer to the start of h_addr_list pointer array.
 *                This array contains pointers to in_addr structures in the
 *                h_addr_list addresses area and is terminated with a NULL.
 *
 *  Filling the buffer this way allows for proper alignment of the h_addr_list
 *  addresses.
 *
 *  This arrangement allows us to alias a Hostent struct pointer as a
 *  real struct hostent* without lying. It also allows us to change the
 *  values contained in the cached entries and requests without changing
 *  the actual hostent pointer, which is saved in a client struct and can't
 *  be changed without blowing things up or a lot more fiddling around.
 *  It also allows for defered allocation of the fixed size buffers until
 *  they are really needed.
 *  Nov. 17, 1997 --Bleep
 */

struct Hostent {
  struct hostent h;      /* the hostent struct we are passing around */
  char*          buf;    /* buffer for data pointed to from hostent */
};

struct reslist {
  int             id;
  int             sent;              /* number of requests sent */
  time_t          ttl;
  char            type;
  char            retries;           /* retry counter */
  char            sends;             /* number of sends (>1 means resent) */
  char            resend;            /* send flag. 0 == dont resend */
  time_t          sentat;
  time_t          timeout;
  struct in_addr  addr;
  char*           name;
  struct reslist *next;
  struct DNSQuery query;             /* query callback for this request */
  struct Hostent  he;
};

struct result {
  struct Hostent  he;
  struct DNSReply reply;
};

int    ResolverFileDescriptor = -1;   /* GLOBAL - used in s_bsd.c */

static time_t nextDNSCheck    = 0;

/*
 * Keep a spare file descriptor open. res_init calls fopen to read the
 * resolv.conf file. If ircd is hogging all the file descriptors below 256,
 * on systems with crippled FILE structures this will cause wierd bugs.
 * This is definitely needed for Solaris which uses an unsigned char to
 * hold the file descriptor.
 */ 
static int         spare_fd = -1;

static struct reslist *requestListHead;     /* head of resolver request list */
static struct reslist *requestListTail;     /* tail of resolver request list */


static void     add_request(struct reslist *request);
static void     rem_request(struct reslist *request);
static struct reslist *make_request(const struct DNSQuery* query);
static void     do_query_name(const struct DNSQuery* query, 
                              const char* name, 
                              struct reslist *request);
static void     do_query_number(const struct DNSQuery* query,
                                const struct in_addr*, 
                                struct reslist *request);
static void     query_name(const char* name, 
                           int query_class, 
                           int query_type, 
                           struct reslist *request);
static int      send_res_msg(const char* buf, int len, int count);
static void     resend_query(struct reslist *request);
static int      proc_answer(struct reslist *request, HEADER* header, 
                                    char*, char *);
static struct reslist* find_id(int);

static  struct  resinfo {
  int  re_errors;
  int  re_nu_look;
  int  re_na_look;
  int  re_replies;
  int  re_requests;
  int  re_resends;
  int  re_sent;
  int  re_timeouts;
  int  re_shortttl;
  int  re_unkrep;
} reinfo;


/*
 * From bind 8.3, these aren't in earlier versions of bind
 *
 */
extern u_short  _getshort(const u_char *);
extern u_int    _getlong(const u_char *);
/*
 * int
 * res_isourserver(ina)
 *      looks up "ina" in _res.ns_addr_list[]
 * returns:
 *      0  : not found
 *      >0 : found
 * author:
 *      paul vixie, 29may94
 */
static int
res_ourserver(const struct __res_state* statp, const struct sockaddr_in *inp) 
{
  struct sockaddr_in ina;
  int ns;

  ina = *inp;
  for (ns = 0;  ns < statp->nscount;  ns++)
  {
    const struct sockaddr_in *srv = &statp->nsaddr_list[ns];

    if (srv->sin_family == ina.sin_family &&
      srv->sin_port == ina.sin_port &&
      (srv->sin_addr.s_addr == INADDR_ANY ||
       srv->sin_addr.s_addr == ina.sin_addr.s_addr))
    return (1);
  }
  return (0);
}

/*
 * start_resolver - do everything we need to read the resolv.conf file
 * and initialize the resolver file descriptor if needed
 */
static void 
start_resolver(void)
{
  char sparemsg[80];

  /*
   * close the spare file descriptor so res_init can read resolv.conf
   * successfully. Needed on Solaris
   */
  if (spare_fd > -1)
    close(spare_fd);

  res_init();      /* res_init always returns 0 */
  /*
   * make sure we have a valid file descriptor below 256 so we can
   * do this again. Needed on Solaris
   */
  spare_fd = open("/dev/null",O_RDONLY,0);
  if ((spare_fd < 0) || (spare_fd > 255))
  {
    ircsprintf(sparemsg,"invalid spare_fd %d",spare_fd);
    restart(sparemsg);
  }

  if (_res.nscount == 0)
  {
    _res.nscount = 1;
    _res.nsaddr_list[0].sin_addr.s_addr = inet_addr("127.0.0.1");
  }
  _res.options |= RES_NOALIASES;
#ifdef DEBUG
  _res.options |= RES_DEBUG;
#endif
  if (ResolverFileDescriptor < 0)
  {
    ResolverFileDescriptor = socket(AF_INET, SOCK_DGRAM, 0);
    set_non_blocking(ResolverFileDescriptor);
  }
}

/*
 * init_resolver - initialize resolver and resolver library
 */
int 
init_resolver(void)
{

#ifdef  LRAND48
  srand48(CurrentTime);
#endif
  /*
   * XXX - we don't really need to do this, all of these are static
   */
  memset((void*) &reinfo,   0, sizeof(struct resinfo));

  requestListHead = requestListTail = NULL;
  start_resolver();
  return ResolverFileDescriptor;
}

/*
 * restart_resolver - reread resolv.conf, reopen socket
 */
void 
restart_resolver(void)
{
  start_resolver();
}

/*
 * add_local_domain - Add the domain to hostname, if it is missing
 * (as suggested by eps@TOASTER.SFSU.EDU)
 */
void 
add_local_domain(char* hname, int size)
{
  assert(hname != NULL);
  /* 
   * try to fix up unqualified names 
   */
  if ((_res.options & RES_DEFNAMES) && !strchr(hname, '.'))
  {
    if (_res.defdname[0])
      {
        size_t len = strlen(hname);
        if ((strlen(_res.defdname) + len + 2) < size)
        {
          hname[len++] = '.';
          strcpy(hname + len, _res.defdname);
        }
      }
  }
}

/*
 * add_request - place a new request in the request list
 */
static 
void add_request(struct reslist *request)
{
  assert(request != NULL);
  if (requestListHead == NULL)
  {
    requestListHead = requestListTail = request;
  }
  else
  {
    requestListTail->next = request;
    requestListTail = request;
  }
  request->next = NULL;
  reinfo.re_requests++;
}

/*
 * rem_request - remove a request from the list. 
 * This must also free any memory that has been allocated for 
 * temporary storage of DNS results.
 */
static void 
rem_request(struct reslist *request)
{
  struct reslist **current;
  struct reslist *prev = NULL;

  assert(request != NULL);
  for (current = &requestListHead; *current; )
  {
    if (*current == request)
    {
      *current = request->next;
      if (requestListTail == request)
        requestListTail = prev;
      break;
    } 
    prev    = *current;
    current = &(*current)->next;
  }

#ifdef  DEBUG
  Debug((DEBUG_DNS, "rem_request:Remove %#x at %#x %#x",
         old, *current, prev));
#endif

  MyFree(request->he.buf);
  MyFree(request->name);
  MyFree(request);
}

/*
 * make_request - Create a DNS request record for the server.
 */
static struct reslist* 
make_request(const struct DNSQuery* query)
{
  struct reslist *request;
  assert(query != NULL);
  request = (struct reslist *) MyMalloc(sizeof(struct reslist));
  memset(request, 0, sizeof(struct reslist));

  request->sentat  = CurrentTime;
  request->retries = 3;
  request->resend  = 1;
  request->timeout = 4;    /* start at 4 and exponential inc. */
  request->addr.s_addr      = INADDR_NONE;
  request->he.h.h_addrtype  = AF_INET;
  request->he.h.h_length    = sizeof(struct in_addr);
  request->query.vptr       = query->vptr;
  request->query.callback   = query->callback;

  add_request(request);
  return request;
}

/*
 * timeout_query_list - Remove queries from the list which have been 
 * there too long without being resolved.
 */
static time_t 
timeout_query_list(time_t now)
{
  struct reslist *request;
  struct reslist *next_request = 0;
  time_t   next_time    = 0;
  time_t   timeout      = 0;

  Debug((DEBUG_DNS, "timeout_query_list at %s", myctime(now)));
  for (request = requestListHead; request; request = next_request)
  {
    next_request = request->next;
    timeout = request->sentat + request->timeout;
    if (now >= timeout)
    {
      if (--request->retries <= 0)
      {

#ifdef DEBUG
        Debug((DEBUG_ERROR, "timeout %x now %d cptr %x",
                 request, now, request->cinfo.value.cptr));
#endif
        reinfo.re_timeouts++;
        (*request->query.callback)(request->query.vptr, 0);
        rem_request(request);
        continue;
      }
      else
      {
        request->sentat = now;
        request->timeout += request->timeout;
        resend_query(request);

#ifdef DEBUG
        Debug((DEBUG_INFO,"r %x now %d retry %d c %x",
               request, now, request->retries,
	       request->cinfo.value.cptr));
#endif
       }
    }

    if ((next_time == 0) || (timeout < next_time))
    {
      next_time = timeout;
    }
  }

  return (next_time > now) ? next_time : (now + AR_TTL);
}


/*
 * timeout_resolver - check request list for expired entries
 */
time_t 
timeout_resolver(time_t now)
{
  if (nextDNSCheck < now)
    nextDNSCheck = timeout_query_list(now);
  return (nextDNSCheck);
}


/*
 * delete_resolver_queries - cleanup outstanding queries 
 * for which there no longer exist clients or conf lines.
 */
void 
delete_resolver_queries(const void* vptr)
{
  struct reslist *request;
  struct reslist *next_request;

  for (request = requestListHead; request; request = next_request)
  {
    next_request = request->next;
    if (vptr == request->query.vptr)
      rem_request(request);
  }
}

/*
 * send_res_msg - sends msg to all nameservers found in the "_res" structure.
 * This should reflect /etc/resolv.conf. We will get responses
 * which arent needed but is easier than checking to see if nameserver
 * isnt present. Returns number of messages successfully sent to 
 * nameservers or -1 if no successful sends.
 */
static int 
send_res_msg(const char* msg, int len, int rcount)
{
  int i;
  int sent = 0;
  int max_queries = IRCD_MIN(_res.nscount, rcount);

  assert(msg != NULL);
  /*
   * RES_PRIMARY option is not implemented
   * if (_res.options & RES_PRIMARY || 0 == max_queries)
   */
  if (max_queries == 0)
    max_queries = 1;

  for (i = 0; i < max_queries; i++)
  {
    /*
     * XXX - this should already have been done by res_init
     */
    _res.nsaddr_list[i].sin_family = AF_INET;
    if (sendto(ResolverFileDescriptor, msg, len, 0, 
        (struct sockaddr*) &(_res.nsaddr_list[i]),
        sizeof(struct sockaddr_in)) == len)
    {
      ++reinfo.re_sent;
      ++sent;
    }
    else
    {
      Debug((DEBUG_ERROR,"s_r_m:sendto: %d on %d", 
	     errno, ResolverFileDescriptor));
    }
  }

  return sent;
}

/*
 * find_id - find a dns request id (id is determined by dn_mkquery)
 */
static struct reslist* 
find_id(int id)
{
  struct reslist *request;

  for (request = requestListHead; request; request = request->next)
  {
    if (request->id == id)
      return request;
  }
  return NULL;
}

/*
 * gethost_byname - get host address from name
 */
void 
gethost_byname(const char* name, const struct DNSQuery* query)
{
  assert(name != NULL);

  ++reinfo.re_na_look;

  do_query_name(query, name, NULL);
  nextDNSCheck = 1;
}

/*
 * gethost_byaddr - get host name from address
 */
void 
gethost_byaddr(const char* addr, const struct DNSQuery* query)
{
  assert(addr != NULL);

  ++reinfo.re_nu_look;
  do_query_number(query, (const struct in_addr*) addr, NULL);

  nextDNSCheck = 1;
}

/*
 * do_query_name - nameserver lookup name
 */
static void 
do_query_name(const struct DNSQuery* query, 
                          const char* name, struct reslist *request)
{
  char  hname[HOSTLEN + 1];
  assert(name != NULL);

  strncpy_irc(hname, name, HOSTLEN);
  hname[HOSTLEN] = '\0';
  add_local_domain(hname, HOSTLEN);

  if (request == NULL)
  {
    request       = make_request(query);
    request->type = T_A;
    request->name = (char*) MyMalloc(strlen(hname) + 1);
    strcpy(request->name, hname);
  }

  query_name(hname, C_IN, T_A, request);
}

/*
 * do_query_number - Use this to do reverse IP# lookups.
 */
static void 
do_query_number(const struct DNSQuery* query, 
                            const struct in_addr* addr,
                            struct reslist *request)
{
  char  ipbuf[32];
  const unsigned char* cp;

  assert(addr != NULL);
  cp = (const unsigned char*) &addr->s_addr;
  ircsprintf(ipbuf, "%u.%u.%u.%u.in-addr.arpa.",
             (unsigned int)(cp[3]), (unsigned int)(cp[2]),
             (unsigned int)(cp[1]), (unsigned int)(cp[0]));

  if (request == NULL)
  {
    request              = make_request(query);
    request->type        = T_PTR;
    request->addr.s_addr = addr->s_addr;
  }

  query_name(ipbuf, C_IN, T_PTR, request);
}

/*
 * query_name - generate a query based on class, type and name.
 */
static void 
query_name(const char* name, int query_class,
                       int type, struct reslist *request)
{
  char buf[MAXPACKET];
  int  request_len = 0;

  assert(name != NULL);
  assert(request != NULL);

  memset(buf, 0, sizeof(buf));
  if ((request_len = res_mkquery(QUERY, name, query_class, type, 
				 NULL, 0, NULL, (u_char *) buf, sizeof(buf)))
      > 0)
  {
    HEADER* header = (HEADER*) buf;
#ifndef LRAND48
    int            k = 0;
    struct timeval tv;
#endif
    /*
     * generate a unique id
     * NOTE: we don't have to worry about converting this to and from
     * network byte order, the nameserver does not interpret this value
     * and returns it unchanged
     */
#ifdef LRAND48
    do
    {
      header->id = (header->id + lrand48()) & 0xffff;
    } while (find_id(header->id));
#else
    gettimeofday(&tv, NULL);
    do
    {
      header->id = (header->id + k + tv.tv_usec) & 0xffff;
      k++;
    } while (find_id(header->id));
#endif /* LRAND48 */
    request->id = header->id;
    ++request->sends;

    request->sent += send_res_msg(buf, request_len, request->sends);
  }
}

static void 
resend_query(struct reslist *request)
{
  assert(request != NULL);

  if (request->resend == 0)
    return;
  ++reinfo.re_resends;
  switch(request->type)
  {
    case T_PTR:
      do_query_number(NULL, &request->addr, request);
      break;
    case T_A:
      do_query_name(NULL, request->name, request);
      break;
    default:
      break;
  }
}

/*
 * proc_answer - process name server reply
 * build a hostent struct in the passed request
 */
static int 
proc_answer(struct reslist *request, HEADER* header,
                       char* buf, char* eob)
{
  char   hostbuf[HOSTLEN + 1]; /* working buffer */
  unsigned char*  current;     /* current position in buf */
  char** alias;                /* alias list */
  char** addr;                 /* address list */
  char*  name;                 /* pointer to name string */
  char*  address;              /* pointer to address */
  char*  endp;                 /* end of our buffer */
  int    query_class;          /* answer class */
  int    type;                 /* answer type */
  int    rd_length;            /* record data length */
  int    answer_count = 0;     /* answer counter */
  int    n;                    /* temp count */
  int    addr_count  = 0;      /* number of addresses in hostent */
  int    alias_count = 0;      /* number of aliases in hostent */
  struct hostent* hp;          /* hostent getting filled */

  assert(request != NULL);
  assert(header != NULL);
  assert(buf != NULL);
  assert(eob != NULL);
  
  current = (unsigned char *) buf + sizeof(HEADER);
  hp = &(request->he.h);

  /*
   * lazy allocation of request->he.buf, we don't allocate a buffer
   * unless there is something to put in it.
   */

  if (request->he.buf == NULL)
  {
    request->he.buf = (char*) MyMalloc(MAXGETHOSTLEN + 1);
    request->he.buf[MAXGETHOSTLEN] = '\0';

    /*
     * array of alias list pointers starts at beginning of buf
     */

    hp->h_aliases = (char**) request->he.buf;
    hp->h_aliases[0] = NULL;

    /*
     * array of address list pointers starts after alias list pointers
     * the actual addresses follow the the address list pointers
     */ 

    hp->h_addr_list = (char**)(request->he.buf + ALIAS_BLEN);

    /*
     * don't copy the host address to the beginning of h_addr_list
     */

     hp->h_addr_list[0] = NULL;
  }

  endp = request->he.buf + MAXGETHOSTLEN;

  /*
   * find the end of the address list
   */

  addr = hp->h_addr_list;
  while (*addr)
  {
    ++addr;
    ++addr_count;
  }

  /*
   * make address point to first available address slot
   */

  address = request->he.buf + ADDRS_OFFSET +
                    (sizeof(struct in_addr) * addr_count);
  /*
   * find the end of the alias list
   */

  alias = hp->h_aliases;
  while (*alias)
  {
    ++alias;
    ++alias_count;
  }

  /*
   * make name point to first available space in request->buf
   */

  if (alias_count > 0)
  {
    name = hp->h_aliases[alias_count - 1];
    name += (strlen(name) + 1);
  }
  else if (hp->h_name)
    name = hp->h_name + strlen(hp->h_name) + 1;
  else
    name = request->he.buf + ADDRS_OFFSET + ADDRS_DLEN;
 
  /*
   * skip past queries
   */ 

  for (; header->qdcount > 0; --header->qdcount)
  {
    if ((n = dn_skipname(current, (u_char *) eob)) < 0)
      break;
    current += (size_t) n + QFIXEDSZ;
  }

  /*
   * process each answer sent to us blech.
   */

  while (header->ancount-- > 0 && (char *) current < eob && name < endp)
  {
    n = dn_expand((u_char *) buf, (u_char *) eob, current, hostbuf, sizeof(hostbuf));
    if (n < 0)
    {
      /*
       * broken message
       */

      return 0;
    }
    else if (n == 0)
    {
      /*
       * no more answers left
       */

      return answer_count;
    }

    hostbuf[HOSTLEN] = '\0';

    /* 
     * With Address arithmetic you have to be very anal
     * this code was not working on alpha due to that
     * (spotted by rodder/jailbird/dianora)
     */

    current += (size_t) n;

    if (!(((char *)current + ANSWER_FIXED_SIZE) < eob))
      break;

    type = _getshort(current);
    current += TYPE_SIZE;

    query_class = _getshort(current);
    current += CLASS_SIZE;

    request->ttl = _getlong(current);
    current += TTL_SIZE;

    rd_length = _getshort(current);
    current += RDLENGTH_SIZE;

    /* 
     * Wait to set request->type until we verify this structure 
     */
    /* add_local_domain(hostbuf, HOSTLEN); */

    switch(type)
    {
      case T_A:
        /*
         * check for invalid rd_length or too many addresses
         */
        if (rd_length != sizeof(struct in_addr))
          return answer_count;

        if (++addr_count < RES_MAXADDRS)
        {
          if (answer_count == 1)
            hp->h_addrtype = (query_class == C_IN) ?  AF_INET : AF_UNSPEC;

          memcpy(address, current, sizeof(struct in_addr));
          *addr++ = address;
          *addr = 0;
          address += sizeof(struct in_addr);

          if (hp->h_name == NULL)
          {
            strcpy(name, hostbuf);
            hp->h_name = name;
            name += strlen(name) + 1;
          }
          Debug((DEBUG_INFO,"got ip # %s for %s", 
          inetntoa((char*) hp->h_addr_list[addr_count - 1]), hostbuf));
        }
        current += rd_length;
        ++answer_count;
        break;

        case T_PTR:
          n = dn_expand((u_char *) buf, (u_char *) eob, current, hostbuf, sizeof(hostbuf));
          if (n < 0)
          {
            /*
             * broken message
             */
            return 0;
          }
          else if (n == 0)
          {
            /*
             * no more answers left
             */
            return answer_count;
          }

          /*
           * This comment is based on analysis by Shadowfax, Wohali and johan, 
           * not me.  (Dianora) I am only commenting it.
           *
           * dn_expand is guaranteed to not return more than sizeof(hostbuf)
           * but do all implementations of dn_expand also guarantee
           * buffer is terminated with null byte? Lets not take chances.
           *  -Dianora
           */

          hostbuf[HOSTLEN] = '\0';
          current += (size_t) n;

          Debug((DEBUG_INFO,"got host %s",hostbuf));

          /*
           * copy the returned hostname into the host name
           * ignore duplicate ptr records
           */

          if (hp->h_name == NULL)
          {
            strcpy(name, hostbuf);
            hp->h_name = name;
            name += strlen(name) + 1;
          }
          ++answer_count;
          break;
        case T_CNAME:
          Debug((DEBUG_INFO,"got cname %s", hostbuf));
          if (++alias_count < RES_MAXALIASES)
          {
            strncpy_irc(name, hostbuf, endp - name);
            *alias++ = name;
            *alias   = 0;
            name += strlen(name) + 1;
          }
          current += rd_length;
          ++answer_count;
          break;
        default :

#ifdef DEBUG
          Debug((DEBUG_INFO,"proc_answer: type:%d for:%s", type, hostbuf));
#endif
          current += rd_length;
          break;
        }
    }
  return answer_count;
}


/*
 * dup_hostent - Duplicate a hostent struct, allocate only enough memory for
 * the data we're putting in it.
 */
static void 
dup_hostent(struct Hostent *new_hp, struct hostent* hp)
{
  char*  p;
  char** ap;
  char** pp;
  int    alias_count = 0;
  int    addr_count = 0;
  size_t bytes_needed = 0;

  assert(new_hp != NULL);
  assert(hp != NULL);

  /* how much buffer do we need? */
  bytes_needed += (strlen(hp->h_name) + 1);

  pp = hp->h_aliases;
  while (*pp)
  {
    bytes_needed += (strlen(*pp++) + 1 + sizeof(void*));
    ++alias_count;
  }

  pp = hp->h_addr_list;

  while (*pp++)
  {
    bytes_needed += (hp->h_length + sizeof(void*));
    ++addr_count;
  }

  /* Reserve space for 2 nulls to terminate h_aliases and h_addr_list */

  bytes_needed += (2 * sizeof(void*));

  /* Allocate memory */
  new_hp->buf = (char*) MyMalloc(bytes_needed);

  new_hp->h.h_addrtype = hp->h_addrtype;
  new_hp->h.h_length = hp->h_length;

  /* first write the address list */

  pp = hp->h_addr_list;
  ap = new_hp->h.h_addr_list =
      (char**)(new_hp->buf + ((alias_count + 1) * sizeof(void*)));
  p = (char*)ap + ((addr_count + 1) * sizeof(void*));
  while (*pp)
  {
    *ap++ = p;
    memcpy(p, *pp++, hp->h_length);
    p += hp->h_length;
  }
  *ap = 0;

  /* next write the name */

  new_hp->h.h_name = p;
  strcpy(p, hp->h_name);
  p += (strlen(p) + 1);

  /* last write the alias list */

  pp = hp->h_aliases;
  ap = new_hp->h.h_aliases = (char**) new_hp->buf;
  while (*pp)
  {
    *ap++ = p;
    strcpy(p, *pp++);
    p += (strlen(p) + 1);
  }
  *ap = 0;
}


/*
 * get_res - read a dns reply from the nameserver and process it.
 */
void 
get_res(void)
{
  char               buf[sizeof(HEADER) + MAXPACKET];
  HEADER*            header;
  struct reslist     *request = NULL;
  struct result      *cp = NULL;
  int                rc;
  int                answer_count;
  int                len = sizeof(struct sockaddr_in);
  struct sockaddr_in res_sin;
  struct hostent*    hp;

  rc = recvfrom(ResolverFileDescriptor, buf, sizeof(buf), 0, 
                (struct sockaddr*) &res_sin, &len);
  if (rc <= sizeof(HEADER))
    return;

  /*
   * convert DNS reply reader from Network byte order to CPU byte order.
   */

  header = (HEADER*) buf;

  /* header->id = ntohs(header->id); */

  header->ancount = ntohs(header->ancount);
  header->qdcount = ntohs(header->qdcount);
  header->nscount = ntohs(header->nscount);
  header->arcount = ntohs(header->arcount);
#ifdef  DEBUG
  Debug((DEBUG_NOTICE, "get_res:id = %d rcode = %d ancount = %d",
         header->id, header->rcode, header->ancount));
#endif

  ++reinfo.re_replies;

  /*
   * response for an id which we have already received an answer for
   * just ignore this response.
   */

  if ((request = find_id(header->id)) == 0)
    return;
  /*
   * check against possibly fake replies
   */

  if (!res_ourserver(&_res, &res_sin))
  {
    ++reinfo.re_unkrep;
    return;
  }

  if ((header->rcode != NOERROR) || (header->ancount == 0))
  {
    ++reinfo.re_errors;
    if (SERVFAIL == header->rcode)
      resend_query(request);
    else
    {
      /*
       * If a bad error was returned, we stop here and dont send
       * send any more (no retries granted).
       */
      Debug((DEBUG_DNS, "Fatal DNS error: %d", header->rcode));
      (*request->query.callback)(request->query.vptr, 0);
      rem_request(request);
    } 
    return;
  }
  /*
   * If this fails there was an error decoding the received packet, 
   * try it again and hope it works the next time.
   */
  answer_count = proc_answer(request, header, buf, buf + rc);

#ifdef DEBUG
  Debug((DEBUG_INFO,"get_res:Proc answer = %d",a));
#endif

  if (answer_count)
  {
    if (request->type == T_PTR)
    {
      struct DNSReply* reply = NULL;
      if (0 == request->he.h.h_name)
      {
        /*
         * got a PTR response with no name, something bogus is happening
         * don't bother trying again, the client address doesn't resolve
         */
        (*request->query.callback)(request->query.vptr, reply);
        rem_request(request);
        return;
      }
      
      Debug((DEBUG_DNS, "relookup %s <-> %s",
             request->he.h.h_name, inetntoa((char*) &request->he.h.h_addr)));
      /*
       * Lookup the 'authoritive' name that we were given for the
       * ip#.  By using this call rather than regenerating the
       * type we automatically gain the use of the cache with no
       * extra kludges.
       */
      gethost_byname(request->he.h.h_name, &request->query);
      MyFree(requestListTail->he.buf);
      requestListTail->he.buf = request->he.buf;
      request->he.buf = 0;
      memcpy(&requestListTail->he.h, &request->he.h, sizeof(struct hostent));
      rem_request(request);
    }
    else
    {
      /*
       * got a name and address response, client resolved
       */
      assert(request != NULL);
      hp = &request->he.h;
      /*
       * shouldn't happen but it just might...
       */
      if ((hp->h_name == NULL) || (hp->h_addr_list[0] == NULL))
      {
        (*request->query.callback)(request->query.vptr, 0 );
      }
      else
      {
        cp = (struct result*) MyMalloc(sizeof(struct result));
        memset(cp, 0, sizeof(struct result));
        dup_hostent(&cp->he, hp);
        cp->reply.hp = &cp->he.h;
        (*request->query.callback)(request->query.vptr, &cp->reply);
      }

#ifdef  DEBUG
      Debug((DEBUG_INFO,"get_res:cp=%#x request=%#x (made)",cp,request));
#endif
      rem_request(request);
    }
  }
  else if (!request->sent)
  {
    /*
     * XXX - we got a response for a query we didn't send with a valid id?
     * this should never happen, bail here and leave the client unresolved
     */
    (*request->query.callback)(request->query.vptr, 0);
    rem_request(request);
  }
}


/*
 * m_dns - dns status query
 */
int 
m_dns(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  if (parv[1] && *parv[1] == 'd')
  {
    sendto_one(sptr, "NOTICE %s :ResolverFileDescriptor = %d", parv[0], ResolverFileDescriptor);
    return 0;
  }
  
  sendto_one(sptr,"NOTICE %s :Re %d Rl %d/%d Rp %d Rq %d",
             sptr->name, reinfo.re_errors, reinfo.re_nu_look,
             reinfo.re_na_look, reinfo.re_replies, reinfo.re_requests);
  sendto_one(sptr,"NOTICE %s :Ru %d Rsh %d Rs %d(%d) Rt %d", sptr->name,
             reinfo.re_unkrep, reinfo.re_shortttl, reinfo.re_sent,
             reinfo.re_resends, reinfo.re_timeouts);
  return 0;
}


unsigned long 
cres_mem(aClient *sptr)
{
  return 0L;
}











