/*
 * src/res.c (C)opyright 1992 Darren Reed. All rights reserved.
 * This file may not be distributed without the author's permission in any
 * shape or form. The author takes no responsibility for any damage or loss
 * of property which results from the use of this software.
 */
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "res.h"
#include "numeric.h"
#include "h.h"

#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <fcntl.h>

#include "nameser.h"
#include "resolv.h"
#include "inet.h"

#ifndef lint
static  char sccsid[] = "@(#)res.c	2.34 03 Nov 1993 (C) 1992 Darren Reed";
static  char *rcs_version = "$Id$";
#endif

#undef	DEBUG	/* because there is a lot of debug code in here :-) */
extern  void    debug();


extern	int	dn_expand (char *, char *, char *, char *, int);
extern	int	dn_skipname (char *, char *);
extern	int	res_mkquery (int, char *, int, int, char *, int,
				   struct rrec *, char *, int);

extern	int	errno, h_errno;
extern	int	highest_fd;
extern	aClient	*local[];
extern	int	spare_fd;

static	char	hostbuf[HOSTLEN+1];
static	int	incache = 0;
static	CacheTable	hashtable[ARES_CACSIZE];
static	aCache	*cachetop = NULL;
static	ResRQ	*last, *first;

static	void	rem_cache (aCache *);
static	void	rem_request (ResRQ *);
static	int	do_query_name (Link *, char *, ResRQ *);
static	int	do_query_number (Link *, struct in_addr *, ResRQ *);
static	void	resend_query (ResRQ *);
static	int	proc_answer (ResRQ *, HEADER *, char *, char *);
static	int	query_name (char *, int, int, ResRQ *);
static	aCache	*make_cache (ResRQ *);
static aCache  *find_cache_name (char *);
static	aCache	*find_cache_number (ResRQ *, char *);
static	int	add_request (ResRQ *);
static	ResRQ	*make_request (Link *);
static	int	send_res_msg (char *, int, int);
static	ResRQ	*find_id (int);
static	int	hash_number (unsigned char *);
static	void	update_list (ResRQ *, aCache *);
static	int	hash_name (char *);
static  struct  hostent *getres_err(ResRQ *,char *);

static	struct cacheinfo {
	int	ca_adds;
	int	ca_dels;
	int	ca_expires;
	int	ca_lookups;
	int	ca_na_hits;
	int	ca_nu_hits;
	int	ca_updates;
} cainfo;

static	struct	resinfo {
	int	re_errors;
	int	re_nu_look;
	int	re_na_look;
	int	re_replies;
	int	re_requests;
	int	re_resends;
	int	re_sent;
	int	re_timeouts;
	int	re_shortttl;
	int	re_unkrep;
} reinfo;

int	init_resolver(int op)
{
  int	ret = 0;
  char  sparemsg[80];

#ifdef	LRAND48
  srand48(timeofday);
#endif
  if (op & RES_INITLIST)
    {
      memset((void *)&reinfo, 0, sizeof(reinfo));
      first = last = NULL;
    }
  if (op & RES_CALLINIT)
    {
      close(spare_fd);
      ret = res_init();
      spare_fd = open("/dev/null",O_RDONLY,0);
      if ((spare_fd < 0) || (spare_fd > 256))
        {
          ircsprintf(sparemsg,"invalid spare_fd %d",spare_fd);
          restart(sparemsg);
        }
      if (!_res.nscount)
	{
	  _res.nscount = 1;
	  _res.nsaddr_list[0].sin_addr.s_addr =
	    inet_addr("127.0.0.1");
	}
    }

  if (op & RES_INITSOCK)
    {
      int     on = 0;
      
      ret = resfd = socket(AF_INET, SOCK_DGRAM, 0);
      (void) setsockopt(ret, SOL_SOCKET, SO_BROADCAST,
			(char *)&on, sizeof(on));
    }
#ifdef DEBUG
  if (op & RES_INITDEBG);
  _res.options |= RES_DEBUG;
#endif
  if (op & RES_INITCACH)
    {
      memset((void *)&cainfo, 0, sizeof(cainfo));
      memset((void *)hashtable, 0, sizeof(hashtable));
    }
  if (op == 0)
    ret = resfd;
  return ret;
}

int restart_resolver()
{
  int ret = 0;
  int on = 0;
  char sparemsg[80];

  flush_cache();	/* flush the dns cache */
  close(spare_fd);
  ret = res_init();
  spare_fd = open("/dev/null",O_RDONLY,0);
  if ((spare_fd < 0) || (spare_fd > 256))
    {
      ircsprintf(sparemsg,"invalid spare_fd %d",spare_fd);
      restart(sparemsg);
    }
  (void)close(resfd);
  ret = resfd = socket(AF_INET, SOCK_DGRAM, 0);
  (void) setsockopt(ret, SOL_SOCKET, SO_BROADCAST,
		    (char *)&on, sizeof(on));

  return ret;
}

static int add_request(ResRQ *new)
{
  if (!new)
    return -1;
  if (!first)
    first = last = new;
  else
    {
      last->next = new;
      last = new;
    }
  new->next = NULL;
  reinfo.re_requests++;
  return 0;
}

/*
 * remove a request from the list. This must also free any memory that has
 * been allocated for temporary storage of DNS results.
 */
static	void	rem_request(ResRQ *old)
{
  Reg	ResRQ	**rptr, *r2ptr = NULL;
  Reg	int	i;
  Reg	char	*s;

  if (!old)
    return;
  for (rptr = &first; *rptr; r2ptr = *rptr, rptr = &(*rptr)->next)
    if (*rptr == old)
      {
	*rptr = old->next;
	if (last == old)
	  last = r2ptr;
	break;
		    }
#ifdef	DEBUG
  Debug((DEBUG_INFO,"rem_request:Remove %#x at %#x %#x",
	 old, *rptr, r2ptr));
#endif
  r2ptr = old;
  if (r2ptr->he.h_name)
    MyFree((char *)r2ptr->he.h_name);
  for (i = 0; i < MAXALIASES; i++)
    if ((s = r2ptr->he.h_aliases[i]))
      MyFree(s);
  if (r2ptr->name)
    MyFree(r2ptr->name);
  MyFree(r2ptr);

  return;
}

/*
 * Create a DNS request record for the server.
 */
static ResRQ *make_request(Link *lp)
{
  register ResRQ *nreq;

  nreq = (ResRQ *)MyMalloc(sizeof(ResRQ));
  memset((void *)nreq, 0, sizeof(ResRQ));
  nreq->next = NULL; /* where NULL is non-zero ;) */
  nreq->sentat = timeofday;
  nreq->retries = 3;
  nreq->resend = 1;
  nreq->srch = -1;
  if (lp)
    memcpy((void *)&nreq->cinfo,(void *)lp,sizeof(Link));
  else
    memset((void *)&nreq->cinfo, 0, sizeof(Link));
  nreq->timeout = 4;	/* start at 4 and exponential inc. */
  nreq->he.h_addrtype = AF_INET;
  nreq->he.h_name = NULL;
  nreq->he.h_aliases[0] = NULL;
  (void)add_request(nreq);
  return nreq;
}

/*
 * Remove queries from the list which have been there too long without
 * being resolved.
 */
time_t	timeout_query_list(time_t now)
{
  Reg	ResRQ	*rptr, *r2ptr;
  Reg	time_t	next = 0, tout;
  aClient	*cptr;

  Debug((DEBUG_DNS,"timeout_query_list at %s",myctime(now)));
  for (rptr = first; rptr; rptr = r2ptr)
    {
      r2ptr = rptr->next;
      tout = rptr->sentat + rptr->timeout;
      if (now >= tout)
       {
	if (--rptr->retries <= 0)
	  {
#ifdef DEBUG
	    Debug((DEBUG_ERROR,"timeout %x now %d cptr %x",
		   rptr, now, rptr->cinfo.value.cptr));
#endif
	    reinfo.re_timeouts++;
	    cptr = rptr->cinfo.value.cptr;
	    switch (rptr->cinfo.flags)
	      {
	      case ASYNC_CLIENT :
#ifdef SHOW_HEADERS
		sendheader(cptr, REPORT_FAIL_DNS, R_fail_dns);
#endif
		ClearDNS(cptr);
		if (!DoingAuth(cptr))
		  SetAccess(cptr);
		break;
	      case ASYNC_CONNECT :
		sendto_ops("Host %s unknown",
			   rptr->name);
		break;
	      }
	    rem_request(rptr);
	    continue;
	  }
	else
	  {
	    rptr->sentat = now;
	    rptr->timeout += rptr->timeout;
	    resend_query(rptr);
#ifdef DEBUG
	    Debug((DEBUG_INFO,"r %x now %d retry %d c %x",
		   rptr, now, rptr->retries,
		   rptr->cinfo.value.cptr));
#endif
	  }
       }
      if (!next || tout < next)
	{
	  next = tout;
	}
    }
  return (next > now) ? next : (now + AR_TTL);
}

/*
 * del_queries - called by the server to cleanup outstanding queries for
 * which there no longer exist clients or conf lines.
 */
void	del_queries(char *cp)
{
  Reg	ResRQ	*rptr, *r2ptr;

  for (rptr = first; rptr; rptr = r2ptr)
    {
      r2ptr = rptr->next;
      if (cp == rptr->cinfo.value.cp)
	rem_request(rptr);
    }
}

/*
 * sends msg to all nameservers found in the "_res" structure.
 * This should reflect /etc/resolv.conf. We will get responses
 * which arent needed but is easier than checking to see if nameserver
 * isnt present. Returns number of messages successfully sent to 
 * nameservers or -1 if no successful sends.
 */
static	int	send_res_msg(char *msg,int len,int rcount)
{
  Reg	int	i;
  int	sent = 0, max;

  if (!msg)
    return -1;

  max = MIN(_res.nscount, rcount);
  if (_res.options & RES_PRIMARY)
    max = 1;
  if (!max)
    max = 1;

  for (i = 0; i < max; i++)
    {
      _res.nsaddr_list[i].sin_family = AF_INET;
      if (sendto(resfd, msg, len, 0, (struct sockaddr *)&(_res.nsaddr_list[i]),
		 sizeof(struct sockaddr)) == len)
	{
	  reinfo.re_sent++;
	  sent++;
	}
      else
	Debug((DEBUG_ERROR,"s_r_m:sendto: %d on %d",
	       errno, resfd));
    }

  return (sent) ? sent : -1;
}


/*
 * find a dns request id (id is determined by dn_mkquery)
 */
static	ResRQ	*find_id(int id)
{
  Reg	ResRQ	*rptr;

  for (rptr = first; rptr; rptr = rptr->next)
    if (rptr->id == id)
      return rptr;
  return((ResRQ *)NULL);
}

struct hostent *gethost_byname(char *name, Link *lp)
{
  Reg	aCache	*cp;

  if(name == (char *)NULL)
    return ((struct hostent *)NULL);

  reinfo.re_na_look++;
  if ((cp = find_cache_name(name)))
    return (struct hostent *)&(cp->he);
  if (!lp)
    return NULL;
  (void)do_query_name(lp, name, NULL);
  return ((struct hostent *)NULL);
}

struct	hostent	*gethost_byaddr(char *addr,Link *lp)
{
  aCache *cp;

  if(addr == (char *)NULL)
    return( (struct hostent *)NULL);

  reinfo.re_nu_look++;
  if ((cp = find_cache_number(NULL, addr)))
    return (struct hostent *)&(cp->he);
  if (!lp)
    return NULL;
  (void)do_query_number(lp, (struct in_addr *)addr, NULL);
  return((struct hostent *)NULL);
}

static int do_query_name(Link *lp, char *name, ResRQ *rptr)
{
  char	hname[HOSTLEN+1];
  int	len;

  strncpyzt(hname, name, HOSTLEN);
  len = strlen(hname);

  if (rptr && !index(hname, '.') && _res.options & RES_DEFNAMES)
    {
      if( (sizeof(hname) - len - 1) >= 2)
	{
	  (void)strncat(hname, ".", sizeof(hname) - len - 1);
	  len++;
	  if( (sizeof(hname) - len - 1 ) >= 1)
	    (void)strncat(hname, _res.defdname, sizeof(hname) - len -1);
	}
    }

  /*
   * Store the name passed as the one to lookup and generate other host
   * names to pass onto the nameserver(s) for lookups.
   */
  if (!rptr)
    {
      rptr = make_request(lp);
      rptr->type = T_A;
      rptr->name = (char *)MyMalloc(strlen(name) + 1);
      (void)strcpy(rptr->name, name);
    }
  return (query_name(hname, C_IN, T_A, rptr));
}

/*
 * Use this to do reverse IP# lookups.
 */
static	int	do_query_number(Link *lp,
				struct in_addr *numb,
				ResRQ *rptr)
{
  char	ipbuf[32];
  Reg	u_char	*cp;

  cp = (u_char *)&numb->s_addr;
  (void)ircsprintf(ipbuf,"%u.%u.%u.%u.in-addr.arpa.",
		   (u_int)(cp[3]), (u_int)(cp[2]),
		   (u_int)(cp[1]), (u_int)(cp[0]));

  if (!rptr)
    {
      rptr = make_request(lp);
      rptr->type = T_PTR;
      rptr->addr.s_addr = numb->s_addr;
      bcopy((char *)&numb->s_addr,
	    (char *)&rptr->he.h_addr, sizeof(struct in_addr));
      rptr->he.h_length = sizeof(struct in_addr);
    }
  return (query_name(ipbuf, C_IN, T_PTR, rptr));
}

/*
 * generate a query based on class, type and name.
 */
static	int	query_name(char *name,
			   int class,
			   int type,
			   ResRQ *rptr)
{
  struct timeval tv;
  char	buf[MAXPACKET];
  int	r,s,k = 0;
  HEADER	*hptr;

  memset((void *)buf, 0, sizeof(buf));
  r = res_mkquery(QUERY, name, class, type, NULL, 0, NULL,
		  buf, sizeof(buf));
  if (r <= 0)
    {
      h_errno = NO_RECOVERY;
      return r;
    }
  hptr = (HEADER *)buf;
#ifdef LRAND48
  do {
    hptr->id = htons(ntohs(hptr->id) + k + lrand48() & 0xffff);
#else
    (void) gettimeofday(&tv, NULL);
    do {
      hptr->id = htons(ntohs(hptr->id) + k +
		       (u_short)(tv.tv_usec & 0xffff));
#endif /* LRAND48 */
      k++;
    } while (find_id(ntohs(hptr->id)));
    rptr->id = ntohs(hptr->id);
    rptr->sends++;
    s = send_res_msg(buf, r, rptr->sends);
    if (s == -1)
      {
	h_errno = TRY_AGAIN;
	return -1;
      }
    else
      rptr->sent += s;
    return 0;
}

static	void	resend_query(ResRQ *rptr)
{
  if (rptr->resend == 0)
    return;
  reinfo.re_resends++;
  switch(rptr->type)
    {
    case T_PTR:
      (void)do_query_number(NULL, &rptr->addr, rptr);
      break;
    case T_A:
      (void)do_query_name(NULL, rptr->name, rptr);
      break;
    default:
      break;
    }
  return;
}

/*
 * process name server reply.
 */
static	int	proc_answer(ResRQ *rptr,
			    HEADER *hptr,
			    char *buf,
			    char *eob)
{
  Reg	char	*cp, **alias;
  Reg	struct	hent	*hp;
  int	class, type, dlen, len, ans = 0, n;
  struct	in_addr	dr, *adr;
  
  cp = buf + sizeof(HEADER);
  hp = (struct hent *)&(rptr->he);
  adr = &hp->h_addr;
  while (adr->s_addr)
    adr++;
  alias = hp->h_aliases;
  while (*alias)
    alias++;
#ifdef SOL20           /* brain damaged compiler (Solaris2) it seems */
  for (; hptr->qdcount > 0; hptr->qdcount--)
#else
    while (hptr->qdcount-- > 0)
#endif
      if ((n = dn_skipname(cp, eob)) == -1)
	break;
      else
	cp += (n + QFIXEDSZ);
  /*
   * process each answer sent to us blech.
   */
  while (hptr->ancount-- > 0 && cp && cp < eob)
    {
      n = dn_expand(buf, eob, cp, hostbuf, sizeof(hostbuf));
      hostbuf[HOSTLEN] = '\0';

      if (n <= 0)
	break;

      /* With Address arithmetic you have to be very anal
       * this code was not working on alpha due to that
       * (spotted by rodder/jailbird/dianora)
       */
      /*
       * RFC 1104/1105 wasn't very helpful about what these fields
       * should be named, so for now, we'll just name them this way.
       * we probably should look at what named calls them or something.
       */

#define TYPE_SIZE 2
#define CLASS_SIZE 2
#define TTL_SIZE 4
#define DLEN_SIZE 2

      cp = (char *)((unsigned long)cp + (unsigned long)n);

      type = (int)_getshort(cp);
      cp = (char *)((unsigned long)cp + (unsigned long)TYPE_SIZE);

      class = (int)_getshort(cp);
      cp = (char *)((unsigned long)cp + (unsigned long)CLASS_SIZE);

      rptr->ttl = _getlong(cp);
      cp = (char *)((unsigned long)cp + (unsigned long)TTL_SIZE);

      dlen =  (int)_getshort(cp);
      cp = (char *)((unsigned long)cp + (unsigned long)DLEN_SIZE);

      /* Wait to set rptr->type until we verify this structure */
      
      len = strlen(hostbuf);
      /* name server never returns with trailing '.' */
      if (!strchr(hostbuf,'.') && (_res.options & RES_DEFNAMES))
	{
	  (void)strcat(hostbuf, ".");
	  len++;
          if( (len + 2) < sizeof(hostbuf))
	    {
              strncpy(hostbuf, _res.defdname,
			sizeof(hostbuf) - 1 - len);
	      hostbuf[HOSTLEN] = '\0';
	      len = MIN(len + strlen(_res.defdname),
		    sizeof(hostbuf)) - 1;
	    }
	}

      switch(type)
	{
	case T_A :
	  hp->h_length = dlen;
	  if (ans == 1)
	    hp->h_addrtype =  (class == C_IN) ?
	      AF_INET : AF_UNSPEC;
	  /* from Christophe Kalt <kalt@stealth.net> */
          if (dlen != sizeof(dr))
            {
              sendto_realops("Bad IP length (%d) returned for %s",
                dlen, hostbuf);

              Debug((DEBUG_DNS,
                "Bad IP length (%d) returned for %s",
                dlen, hostbuf));
              return(-2); 
            }
	  bcopy(cp, (char *)&dr, sizeof(dr));
	  adr->s_addr = dr.s_addr;
	  Debug((DEBUG_INFO,"got ip # %s for %s",
		 inetntoa((char *)adr), hostbuf));
	  if (!hp->h_name)
	    {
	      hp->h_name =(char *)MyMalloc(len+1);
	      strcpy(hp->h_name, hostbuf);
	    }
	  ans++;
	  adr++;
	  cp += dlen;
          rptr->type = type;
	  break;
	case T_PTR :
	  if((n = dn_expand(buf, eob, cp, hostbuf,
			    sizeof(hostbuf) )) < 0)
	    {
	      cp = NULL;
	      break;
	    }
/*
This comment is based on analysis by Shadowfax, Wohali and johan, not me.
(Dianora) I am only commenting it.

	dn_expand is guaranteed to not return more than sizeof(hostbuf)
	but do all implementations of dn_expand also guarantee
	buffer is terminated with null byte? Lets not take chances.
	-Dianora
*/
	  hostbuf[HOSTLEN] = '\0';
	  cp += n;
	  len = strlen(hostbuf);
	  Debug((DEBUG_INFO,"got host %s",hostbuf));
	  /*
	   * copy the returned hostname into the host name
	   * or alias field if there is a known hostname
	   * already.
	   */
	  if (hp->h_name)
	    {
	      if (alias >= &(hp->h_aliases[MAXALIASES-1]))
		break;
	      *alias = (char *)MyMalloc(len + 1);
/* remember, strcpy is safe here, since we have ensured hostbuf is 
   always < HOSTLEN above
   -Dianora
*/
              strcpy(*alias++, hostbuf);
	      *alias = NULL;
	    }
	  else
	    {
	      hp->h_name = (char *)MyMalloc(len+1);
	      strcpy(hp->h_name, hostbuf);
	    }
	  ans++;
          rptr->type = type;
	  break;
	case T_CNAME :
	  cp += dlen;
	  Debug((DEBUG_INFO,"got cname %s", hostbuf));
	  if (alias >= &(hp->h_aliases[MAXALIASES-1]))
	    break;
	  *alias = (char *)MyMalloc(len + 1);
          strcpy(*alias++, hostbuf);
	  *alias = NULL;
	  ans++;
          rptr->type = type;
	  break;
	default :
#ifdef DEBUG
	  Debug((DEBUG_INFO,"proc_answer: type:%d for:%s",
		 type, hostbuf));
#endif
	  break;
	}
    }
  return ans;
}

/*
 * read a dns reply from the nameserver and process it.
 */
struct	hostent	*get_res(char *lp)
{
  static	char	buf[sizeof(HEADER) + MAXPACKET];
  Reg	HEADER	*hptr;
  Reg	ResRQ	*rptr = NULL;
  aCache	*cp = (aCache *) NULL;
  struct	sockaddr_in	sin;
  int	rc, a, len = sizeof(sin), max;

  rc = recvfrom(resfd, buf, sizeof(buf), 0, (struct sockaddr *)&sin,
		&len);
  if (rc <= sizeof(HEADER))
    return getres_err(rptr,lp);
  /*
   * convert DNS reply reader from Network byte order to CPU byte order.
   */
  hptr = (HEADER *)buf;
  hptr->id = ntohs(hptr->id);
  hptr->ancount = ntohs(hptr->ancount);
  hptr->qdcount = ntohs(hptr->qdcount);
  hptr->nscount = ntohs(hptr->nscount);
  hptr->arcount = ntohs(hptr->arcount);
#ifdef	DEBUG
  Debug((DEBUG_NOTICE, "get_res:id = %d rcode = %d ancount = %d",
	 hptr->id, hptr->rcode, hptr->ancount));
#endif
  reinfo.re_replies++;
  /*
   * response for an id which we have already received an answer for
   * just ignore this response.
   */
  rptr = find_id(hptr->id);
  if (!rptr)
    return getres_err(rptr,lp);
  /*
   * check against possibly fake replies
   */
  max = MIN(_res.nscount, rptr->sends);
  if (!max)
    max = 1;

  for (a = 0; a < max; a++)
    if (!_res.nsaddr_list[a].sin_addr.s_addr ||
	!bcmp((char *)&sin.sin_addr,
	      (char *)&_res.nsaddr_list[a].sin_addr,
	      sizeof(struct in_addr)))
      break;
  if (a == max)
    {
      reinfo.re_unkrep++;
      return getres_err(rptr,lp);
    }

  if ((hptr->rcode != NOERROR) || (hptr->ancount == 0))
    {
      switch (hptr->rcode)
	{
	case NXDOMAIN:
	  h_errno = TRY_AGAIN;
	  break;
	case SERVFAIL:
	  h_errno = TRY_AGAIN;
	  break;
	case NOERROR:
	  h_errno = NO_DATA;
	  break;
	case FORMERR:
	case NOTIMP:
	case REFUSED:
	default:
	  h_errno = NO_RECOVERY;
	  break;
	}
      reinfo.re_errors++;
      /*
      ** If a bad error was returned, we stop here and dont send
      ** send any more (no retries granted).
      */
      if (h_errno != TRY_AGAIN)
	{
	  Debug((DEBUG_DNS, "Fatal DNS error %d for %d",
		 h_errno, hptr->rcode));
	  rptr->resend = 0;
	  rptr->retries = 0;
	}
      return getres_err(rptr,lp);
    }
  a = proc_answer(rptr, hptr, buf, buf+rc);
#ifdef DEBUG
  Debug((DEBUG_INFO,"get_res:Proc answer = %d",a));
#endif
  if (a && rptr->type == T_PTR)
    {
      struct	hostent	*hp2 = NULL;
      
      Debug((DEBUG_DNS, "relookup %s <-> %s",
	     rptr->he.h_name, inetntoa((char *)&rptr->he.h_addr)));
      /*
       * Lookup the 'authoritive' name that we were given for the
       * ip#.  By using this call rather than regenerating the
       * type we automatically gain the use of the cache with no
       * extra kludges.
       */
      if ((hp2 = gethost_byname(rptr->he.h_name, &rptr->cinfo)))
	if (lp)
	  bcopy((char *)&rptr->cinfo, lp, sizeof(Link));
      /*
       * If name wasn't found, a request has been queued and it will
       * be the last one queued.  This is rather nasty way to keep
       * a host alias with the query. -avalon
       */
      if (!hp2 && rptr->he.h_aliases[0])
	for (a = 0; rptr->he.h_aliases[a]; a++)
	  {
	    Debug((DEBUG_DNS, "Copied CNAME %s for %s",
		   rptr->he.h_aliases[a],
		   rptr->he.h_name));
	    last->he.h_aliases[a] = rptr->he.h_aliases[a];
	    rptr->he.h_aliases[a] = NULL;
	  }

      rem_request(rptr);
      return hp2;
    }
  
  if (a > 0)
    {
      if (lp)
	bcopy((char *)&rptr->cinfo, lp, sizeof(Link));
      cp = make_cache(rptr);
#ifdef	DEBUG
      Debug((DEBUG_INFO,"get_res:cp=%#x rptr=%#x (made)",cp,rptr));
#endif

      rem_request(rptr);
    }
  else
    if (!rptr->sent)
      rem_request(rptr);
  return cp ? (struct hostent *)&cp->he : NULL;
}

static struct hostent *getres_err(ResRQ *rptr,char *lp)
{
  /*
   * Reprocess an error if the nameserver didnt tell us to "TRY_AGAIN".
   */
  if (rptr)
    {
      if (h_errno != TRY_AGAIN)
	{
	  /*
	   * If we havent tried with the default domain and its
	   * set, then give it a try next.
	   */
	  if (_res.options & RES_DEFNAMES && ++rptr->srch == 0)
	    {
	      rptr->retries = _res.retry;
	      rptr->sends = 0;
	      rptr->resend = 1;
	      resend_query(rptr);
	    }
	  else
	    resend_query(rptr);
	}
      else if (lp)
	bcopy((char *)&rptr->cinfo, lp, sizeof(Link));
    }
  return (struct hostent *)NULL;
}

static	int	hash_number(unsigned char *ip)
{
  Reg	u_int	hashv = 0;

  /* could use loop but slower */
  hashv += (int)*ip++;
  hashv += hashv + (int)*ip++;
  hashv += hashv + (int)*ip++;
  hashv += hashv + (int)*ip++;
  hashv %= ARES_CACSIZE;
  return (hashv);
}

static	int	hash_name(char *name)
{
  Reg	u_int	hashv = 0;

  for (; *name && *name != '.'; name++)
    hashv += *name;
  hashv %= ARES_CACSIZE;
  return (hashv);
}

/*
** Add a new cache item to the queue and hash table.
*/
static	aCache	*add_to_cache(aCache *ocp)
{
  Reg	aCache	*cp = NULL;
  Reg	int	hashv;

#ifdef DEBUG
  Debug((DEBUG_INFO,
	 "add_to_cache:ocp %#x he %#x name %#x addrl %#x 0 %#x",
	 ocp, &ocp->he, ocp->he.h_name, ocp->he.h_addr_list,
	 ocp->he.h_addr_list[0]));
#endif
  ocp->list_next = cachetop;
  cachetop = ocp;

  /*
   * Make sure non-bind resolvers don't blow up (Thanks to Yves)
   */
  if (!ocp) return NULL;
  if (!(ocp->he.h_name)) return NULL;
  if (!(ocp->he.h_addr)) return NULL;
  
  hashv = hash_name(ocp->he.h_name);

  ocp->hname_next = hashtable[hashv].name_list;
  hashtable[hashv].name_list = ocp;

  hashv = hash_number((u_char *)ocp->he.h_addr);

  ocp->hnum_next = hashtable[hashv].num_list;
  hashtable[hashv].num_list = ocp;

#ifdef	DEBUG
  Debug((DEBUG_INFO, "add_to_cache:added %s[%08x] cache %#x.",
	 ocp->he.h_name, ocp->he.h_addr_list[0], ocp));
  Debug((DEBUG_INFO,
	 "add_to_cache:h1 %d h2 %x lnext %#x namnext %#x numnext %#x",
	 hash_name(ocp->he.h_name), hashv, ocp->list_next,
	 ocp->hname_next, ocp->hnum_next));
#endif

  /*
   * LRU deletion of excessive cache entries.
   */
  if (++incache > MAXCACHED)
    {
      for (cp = cachetop; cp->list_next; cp = cp->list_next)
	;
      rem_cache(cp);
    }
  cainfo.ca_adds++;

  return ocp;
}

/*
** update_list does not alter the cache structure passed. It is assumed that
** it already contains the correct expire time, if it is a new entry. Old
** entries have the expirey time updated.
*/
static	void	update_list(ResRQ *rptr,aCache *cachep)
{
  Reg	aCache	**cpp, *cp = cachep;
  Reg	char	*s, *t, **base;
  Reg	int	i, j;
  int	addrcount;

  /*
  ** search for the new cache item in the cache list by hostname.
  ** If found, move the entry to the top of the list and return.
  */
  cainfo.ca_updates++;

  for (cpp = &cachetop; *cpp; cpp = &((*cpp)->list_next))
    if (cp == *cpp)
      break;
  if (!*cpp)
    return;
  *cpp = cp->list_next;
  cp->list_next = cachetop;
  cachetop = cp;
  if (!rptr)
    return;

#ifdef	DEBUG
  Debug((DEBUG_DEBUG,"u_l:cp %#x na %#x al %#x ad %#x",
	 cp,cp->he.h_name,cp->he.h_aliases,cp->he.h_addr));
  Debug((DEBUG_DEBUG,"u_l:rptr %#x h_n %#x", rptr, rptr->he.h_name));
#endif
  /*
   * Compare the cache entry against the new record.  Add any
   * previously missing names for this entry.
   */
  for (i = 0; cp->he.h_aliases[i]; i++)
    ;
  addrcount = i;
  for (i = 0, s = rptr->he.h_name; s && i < MAXALIASES;
       s = rptr->he.h_aliases[i++])
    {
      for (j = 0, t = cp->he.h_name; t && j < MAXALIASES;
	   t = cp->he.h_aliases[j++])
	if (!mycmp(t, s))
	  break;
      if (!t && j < MAXALIASES-1)
	{
	  base = cp->he.h_aliases;
	  
	  addrcount++;
	  base = (char **)MyRealloc((char *)base,
				    sizeof(char *) * (addrcount + 1));
	  cp->he.h_aliases = base;
#ifdef	DEBUG
	  Debug((DEBUG_DNS,"u_l:add name %s hal %x ac %d",
		 s, cp->he.h_aliases, addrcount));
#endif
	  base[addrcount-1] = s;
	  base[addrcount] = NULL;
	  if (i)
	    rptr->he.h_aliases[i-1] = NULL;
	  else
	    rptr->he.h_name = NULL;
	}
    }
  for (i = 0; cp->he.h_addr_list[i]; i++)
    ;
  addrcount = i;

  /*
   * Do the same again for IP#'s.
   */
  for (s = (char *)&rptr->he.h_addr.s_addr;
       ((struct in_addr *)s)->s_addr; s += sizeof(struct in_addr))
    {
      for (i = 0; (t = cp->he.h_addr_list[i]); i++)
	if (!bcmp(s, t, sizeof(struct in_addr)))
	  break;
      if (i >= MAXADDRS || addrcount >= MAXADDRS)
	break;
      /*
       * Oh man this is bad...I *HATE* it. -avalon
       *
       * Whats it do ?  Reallocate two arrays, one of pointers
       * to "char *" and the other of IP addresses.  Contents of
       * the IP array *MUST* be preserved and the pointers into
       * it recalculated.
       */
      if (!t)
	{
	  base = cp->he.h_addr_list;
	  addrcount++;
	  t = (char *)MyRealloc(*base,
				addrcount * sizeof(struct in_addr));
	  base = (char **)MyRealloc((char *)base,
				    (addrcount + 1) * sizeof(char *));
	  cp->he.h_addr_list = base;
#ifdef	DEBUG
	  Debug((DEBUG_DNS,"u_l:add IP %x hal %x ac %d",
		 ntohl(((struct in_addr *)s)->s_addr),
		 cp->he.h_addr_list,
		 addrcount));
#endif
	  for (; addrcount; addrcount--)
	    {
	      *base++ = t;
	      t += sizeof(struct in_addr);
	    }
	  *base = NULL;
	  bcopy(s, *--base, sizeof(struct in_addr));
	}
    }
  return;
}

static aCache  *find_cache_name(char *name)
{
  Reg	aCache	*cp;
  Reg	char	*s;
  Reg	int	hashv, i;

  if(name == (char *)NULL)
    return(aCache *)NULL;
  hashv = hash_name(name);

  cp = hashtable[hashv].name_list;
#ifdef	DEBUG
  Debug((DEBUG_DNS,"find_cache_name:find %s : hashv = %d",name,hashv));
#endif

  for (; cp; cp = cp->hname_next)
    for (i = 0, s = cp->he.h_name; s; s = cp->he.h_aliases[i++])
      if (mycmp(s, name) == 0)
	{
	  cainfo.ca_na_hits++;
	  update_list(NULL, cp);
	  return cp;
	}

  for (cp = cachetop; cp; cp = cp->list_next)
    {
      /*
       * if no aliases or the hash value matches, we've already
       * done this entry and all possiblilities concerning it.
       */
      if (!*cp->he.h_aliases)
	continue;
      if(cp->he.h_name == (char *)NULL)   /* don't trust anything -Dianora */
        continue;
      if (hashv == hash_name(cp->he.h_name))
	continue;
      for (i = 0, s = cp->he.h_aliases[i]; s && i < MAXALIASES; i++)
	if (!mycmp(name, s))
	  {
	    cainfo.ca_na_hits++;
	    update_list(NULL, cp);
	    return cp;
	  }
    }
  return NULL;
}

/*
 * find a cache entry by ip# and update its expire time
 */
static	aCache	*find_cache_number(ResRQ *rptr,char *numb)
{
  Reg	aCache	*cp;
  Reg	int	hashv,i;
#ifdef	DEBUG
  struct	in_addr	*ip = (struct in_addr *)numb;
#endif

  if((u_char *)numb == (u_char *)NULL)
    return((aCache *)NULL);
  hashv = hash_number((u_char *)numb);
  cp = hashtable[hashv].num_list;
#ifdef DEBUG
  Debug((DEBUG_DNS,"find_cache_number:find %s[%08x]: hashv = %d",
	 inetntoa(numb), ntohl(ip->s_addr), hashv));
#endif

  for (; cp; cp = cp->hnum_next)
    for (i = 0; cp->he.h_addr_list[i]; i++)
      if (!bcmp(cp->he.h_addr_list[i], numb,
		sizeof(struct in_addr)))
	{
	  cainfo.ca_nu_hits++;
	  update_list(NULL, cp);
	  return cp;
	}

  for (cp = cachetop; cp; cp = cp->list_next)
    {
      /*
       * single address entry...would have been done by hashed
       * search above...
       */
      if (!cp->he.h_addr_list[1])
	continue;
      /*
       * if the first IP# has the same hashnumber as the IP# we
       * are looking for, its been done already.
       */
      if (hashv == hash_number((u_char *)cp->he.h_addr_list[0]))
	continue;
      for (i = 1; cp->he.h_addr_list[i]; i++)
	if (!bcmp(cp->he.h_addr_list[i], numb,
		  sizeof(struct in_addr)))
	  {
	    cainfo.ca_nu_hits++;
	    update_list(NULL, cp);
	    return cp;
	  }
    }
  return NULL;
}

static	aCache	*make_cache(ResRQ *rptr)
{
  Reg	aCache	*cp;
  Reg	int	i, n;
  Reg	struct	hostent	*hp;
  Reg	char	*s, **t;
  
  /*
  ** shouldn't happen but it just might...
  */
  if (!rptr->he.h_name || !rptr->he.h_addr.s_addr)
    return NULL;
  /*
  ** Make cache entry.  First check to see if the cache already exists
  ** and if so, return a pointer to it.
  */
  if ((cp = find_cache_number(rptr, (char *)&rptr->he.h_addr.s_addr)))
    return cp;
  for (i = 1; rptr->he.h_addr_list[i].s_addr; i++)
    if ((cp = find_cache_number(rptr,
				(char *)&(rptr->he.h_addr_list[i].s_addr))))
      return cp;

  /*
  ** a matching entry wasnt found in the cache so go and make one up.
  */ 
  cp = (aCache *)MyMalloc(sizeof(aCache));
  memset((void *)cp, 0, sizeof(aCache));
  hp = &cp->he;
  for (i = 0; i < MAXADDRS; i++)
    if (!rptr->he.h_addr_list[i].s_addr)
      break;

  /*
  ** build two arrays, one for IP#'s, another of pointers to them.
  */
  t = hp->h_addr_list = (char **)MyMalloc(sizeof(char *) * (i+1));
  memset((void *)t, 0, sizeof(char *) * (i+1));

  s = (char *)MyMalloc(sizeof(struct in_addr) * i);
  memset((void *)s, 0, sizeof(struct in_addr) * i);

  for (n = 0; n < i; n++, s += sizeof(struct in_addr))
    {
      *t++ = s;
      bcopy((char *)&(rptr->he.h_addr_list[n].s_addr), s,
	    sizeof(struct in_addr));
    }
  *t = (char *)NULL;

  /*
  ** an array of pointers to CNAMEs.
  */
  for (i = 0; i < MAXALIASES; i++)
    if (!rptr->he.h_aliases[i])
      break;
  i++;
  t = hp->h_aliases = (char **)MyMalloc(sizeof(char *) * i);
  for (n = 0; n < i; n++, t++)
    {
      *t = rptr->he.h_aliases[n];
      rptr->he.h_aliases[n] = NULL;
    }

  hp->h_addrtype = rptr->he.h_addrtype;
  hp->h_length = rptr->he.h_length;
  hp->h_name = rptr->he.h_name;
  if (rptr->ttl < 600)
    {
      reinfo.re_shortttl++;
      cp->ttl = 600;
    }
  else
    cp->ttl = rptr->ttl;
  cp->expireat = timeofday + cp->ttl;
  rptr->he.h_name = NULL;
#ifdef DEBUG
  Debug((DEBUG_INFO,"make_cache:made cache %#x", cp));
#endif
  return add_to_cache(cp);
}

/*
 * rem_cache
 *     delete a cache entry from the cache structures and lists and return
 *     all memory used for the cache back to the memory pool.
 */
static	void	rem_cache(aCache *ocp)
{
  Reg	aCache	**cp;
  Reg	struct	hostent *hp = &ocp->he;
  Reg	int	hashv;
  Reg	aClient	*cptr;

#ifdef	DEBUG
  Debug((DEBUG_DNS, "rem_cache: ocp %#x hp %#x l_n %#x aliases %#x",
	 ocp, hp, ocp->list_next, hp->h_aliases));
#endif
  /*
  ** Cleanup any references to this structure by destroying the
  ** pointer.
  */
  for (hashv = highest_fd; hashv >= 0; hashv--)
    if ((cptr = local[hashv]) && (cptr->hostp == hp))
      cptr->hostp = NULL;
  /*
   * remove cache entry from linked list
   */
  for (cp = &cachetop; *cp; cp = &((*cp)->list_next))
    if (*cp == ocp)
      {
	*cp = ocp->list_next;
	break;
      }
  /*
   * remove cache entry from hashed name lists
   */
  if(hp->h_name == (char *)NULL)
    return;
  hashv = hash_name(hp->h_name);

#ifdef	DEBUG
  Debug((DEBUG_DEBUG,"rem_cache: h_name %s hashv %d next %#x first %#x",
	 hp->h_name, hashv, ocp->hname_next,
	 hashtable[hashv].name_list));
#endif
  for (cp = &hashtable[hashv].name_list; *cp; cp = &((*cp)->hname_next))
    if (*cp == ocp)
      {
	*cp = ocp->hname_next;
	break;
      }
  /*
   * remove cache entry from hashed number list
   */
  hashv = hash_number((u_char *)hp->h_addr);
  if( hashv < 0 )
    return;
#ifdef	DEBUG
  Debug((DEBUG_DEBUG,"rem_cache: h_addr %s hashv %d next %#x first %#x",
	 inetntoa(hp->h_addr), hashv, ocp->hnum_next,
	 hashtable[hashv].num_list));
#endif
  for (cp = &hashtable[hashv].num_list; *cp; cp = &((*cp)->hnum_next))
    if (*cp == ocp)
      {
	*cp = ocp->hnum_next;
	break;
      }

  /*
   * free memory used to hold the various host names and the array
   * of alias pointers.
   */
  if (hp->h_name)
    MyFree(hp->h_name);
  if (hp->h_aliases)
    {
      for (hashv = 0; hp->h_aliases[hashv]; hashv++)
	MyFree(hp->h_aliases[hashv]);
      MyFree((char *)hp->h_aliases);
    }

  /*
   * free memory used to hold ip numbers and the array of them.
   */
  if (hp->h_addr_list)
    {
      if (*hp->h_addr_list)
	MyFree((char *)*hp->h_addr_list);
      MyFree((char *)hp->h_addr_list);
    }

  MyFree((char *)ocp);
  
  incache--;
  cainfo.ca_dels++;

  return;
}

/*
 * removes entries from the cache which are older than their expirey times.
 * returns the time at which the server should next poll the cache.
 */
time_t	expire_cache(time_t now)
{
  Reg	aCache	*cp, *cp2;
  Reg	time_t	next = 0;

  for (cp = cachetop; cp; cp = cp2)
    {
      cp2 = cp->list_next;
      
      if (now >= cp->expireat)
	{
	  cainfo.ca_expires++;
	  rem_cache(cp);
	}
      else if (!next || next > cp->expireat)
	next = cp->expireat;
    }
  return (next > now) ? next : (now + AR_TTL);
}

/*
 * remove all dns cache entries.
 */
void	flush_cache()
{
  Reg	aCache	*cp;

  while ((cp = cachetop))
    rem_cache(cp);
}

int	m_dns(aClient *cptr,
	      aClient *sptr,
	      int parc,
	      char *parv[])
{
  Reg	aCache	*cp;
  Reg	int	i;

  if (parv[1] && *parv[1] == 'l')
    {
      for(cp = cachetop; cp; cp = cp->list_next)
	{
	  sendto_one(sptr, "NOTICE %s :Ex %d ttl %d host %s(%s)",
		     parv[0], cp->expireat - timeofday, cp->ttl,
		     cp->he.h_name, inetntoa(cp->he.h_addr));
	  for (i = 0; cp->he.h_aliases[i]; i++)
	    sendto_one(sptr,"NOTICE %s : %s = %s (CN)",
		       parv[0], cp->he.h_name,
		       cp->he.h_aliases[i]);
	  for (i = 1; cp->he.h_addr_list[i]; i++)
	    sendto_one(sptr,"NOTICE %s : %s = %s (IP)",
		       parv[0], cp->he.h_name,
		       inetntoa(cp->he.h_addr_list[i]));
	}
      return 0;
    }
  if (parv[1] && *parv[1] == 'd')
    {
      sendto_one(sptr, "NOTICE %s :resfd = %d", parv[0], resfd);
      return 0;
    }

  sendto_one(sptr,"NOTICE %s :Ca %d Cd %d Ce %d Cl %d Ch %d:%d Cu %d",
	     sptr->name,
	     cainfo.ca_adds, cainfo.ca_dels, cainfo.ca_expires,
	     cainfo.ca_lookups,
	     cainfo.ca_na_hits, cainfo.ca_nu_hits, cainfo.ca_updates);
  
  sendto_one(sptr,"NOTICE %s :Re %d Rl %d/%d Rp %d Rq %d",
	     sptr->name, reinfo.re_errors, reinfo.re_nu_look,
	     reinfo.re_na_look, reinfo.re_replies, reinfo.re_requests);
  sendto_one(sptr,"NOTICE %s :Ru %d Rsh %d Rs %d(%d) Rt %d", sptr->name,
	     reinfo.re_unkrep, reinfo.re_shortttl, reinfo.re_sent,
	     reinfo.re_resends, reinfo.re_timeouts);
  return 0;
}

u_long	cres_mem(aClient *sptr)
{
  register aCache	*c = cachetop;
  register struct	hostent	*h;
  register int	i;
  u_long	nm = 0, im = 0, sm = 0, ts = 0;

  for ( ;c ; c = c->list_next)
    {
      sm += sizeof(*c);
      h = &c->he;
      for (i = 0; h->h_addr_list[i]; i++)
	{
	  im += sizeof(char *);
	  im += sizeof(struct in_addr);
	}
      im += sizeof(char *);
      for (i = 0; h->h_aliases[i]; i++)
	{
	  nm += sizeof(char *);
	  nm += strlen(h->h_aliases[i]);
	}
      nm += i - 1;
      nm += sizeof(char *);
      if (h->h_name)
	nm += strlen(h->h_name);
    }
  ts = ARES_CACSIZE * sizeof(CacheTable);
  sendto_one(sptr, ":%s %d %s :RES table %d",
	     me.name, RPL_STATSDEBUG, sptr->name, ts);
  sendto_one(sptr, ":%s %d %s :Structs %d IP storage %d Name storage %d",
	     me.name, RPL_STATSDEBUG, sptr->name, sm, im, nm);
  return ts + sm + im + nm;
}
