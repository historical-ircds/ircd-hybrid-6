/************************************************************************
 *   IRC - Internet Relay Chat, src/s_conf.c
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
 *  (C) 1988 University of Oulu,Computing Center and Jarkko Oikarinen"
 *
 *  $Id$
 */
#include "s_conf.h"
#include "class.h"
#include "struct.h"
#include "common.h"
#include "dline_conf.h"
#include "sys.h"
#include "numeric.h"
#include "h.h"
#include "mtrie_conf.h"
#include "s_bsd.h"
#include "send.h"

#if defined(AIX) || defined(DYNIXPTX) || defined(SVR3)
#include <time.h>
#endif
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
extern int rehashed;

extern ConfigFileEntryType ConfigFileEntry; /* defined in ircd.c */

#include "res.h"    /* gethost_byname, gethost_byaddr */

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif

struct sockaddr_in vserv;
char        specific_virtual_host;

/* internally defined functions */

static void lookup_confhost(aConfItem* aconf);
static int     SplitUserHost( aConfItem * );

static FBFILE*  openconf(char* filename);
static void	initconf(FBFILE*, int);
static void     clear_out_old_conf(void);
static void     flush_deleted_I_P(void);

#ifdef LIMIT_UH
static  int     attach_iline(aClient *, aConfItem *,char *);
#else
static  int     attach_iline(aClient *, aConfItem *);
#endif
aConfItem *find_special_conf(char *, int );
int       find_q_line(char *, char *,char *);

void add_q_line(aConfItem *);
void clear_q_lines(void);
void clear_special_conf(aConfItem **);
static aConfItem *find_tkline(char *,char *);

aConfItem *temporary_klines = (aConfItem *)NULL;

static  char *set_conf_flags(aConfItem *,char *);
static  int  get_oper_privs(int,char *);
static  int  get_oper_flags(char *);

/* externally defined functions */
extern  void    outofmemory(void);        /* defined in list.c */

#ifdef GLINES
extern  aConfItem *find_gkill(aClient *); /* defined in m_gline.c */
#endif

/* usually, with hash tables, you use a prime number...
 * but in this case I am dealing with ip addresses, not ascii strings.
 */

#define IP_HASH_SIZE 0x1000

typedef struct ip_entry
{
  unsigned long ip;
  int        count;
  struct ip_entry *next;
#ifdef LIMIT_UH
  Link  *ptr_clients_on_this_ip;
  int        count_of_idented_users_on_this_ip;
#endif
}IP_ENTRY;

IP_ENTRY *ip_hash_table[IP_HASH_SIZE];

extern void zap_Dlines();

static int hash_ip(unsigned long);

#ifdef LIMIT_UH
static IP_ENTRY *find_or_add_ip(aClient *,char *);
static int count_users_on_this_ip(IP_ENTRY *,aClient *,char *);
#else
static IP_ENTRY *find_or_add_ip(aClient *);
#endif

/* general conf items link list root */
aConfItem* ConfigItemList = NULL;

/* conf xline link list root */
aConfItem        *x_conf = ((aConfItem *)NULL);

typedef struct QlineItem {
  char      *name;
  aConfItem *confList;
  struct    QlineItem *next;
}aQlineItem;

static void makeQlineEntry(aQlineItem *, aConfItem *, char *);

/* conf qline link list root */
aQlineItem         *q_conf = ((aQlineItem *)NULL);

/* conf uline link list root */
aConfItem        *u_conf = ((aConfItem *)NULL);

/* keep track of .include files to hash in */
aConfItem        *include_list = ((aConfItem *)NULL);

#ifdef LOCKFILE 
extern void do_pending_klines(void);
#endif

/*
 * conf_dns_callback - called when resolver query finishes
 * if the query resulted in a successful search, hp will contain
 * a non-null pointer, otherwise hp will be null.
 * if successful save hp in the conf item it was called with
 */
static void conf_dns_callback(void* vptr, struct DNSReply* reply)
{
  aConfItem* aconf = (aConfItem*) vptr;
  aconf->dns_pending = 0;
  if (reply)
    memcpy(&aconf->ipnum, reply->hp->h_addr, sizeof(struct in_addr));
}

/*
 * conf_dns_lookup - do a nameserver lookup of the conf host
 * if the conf entry is currently doing a ns lookup do nothing, otherwise
 * if the lookup returns a null pointer, set the conf dns_pending flag
 */
struct DNSReply* conf_dns_lookup(struct ConfItem* aconf)
{
  struct DNSReply* dns_reply = 0;
  if (!aconf->dns_pending) {
    struct DNSQuery query;
    query.vptr     = aconf;
    query.callback = conf_dns_callback;
    if (0 == (dns_reply = gethost_byname(aconf->host, &query)))
      aconf->dns_pending = 1;
  }
  return dns_reply;
}

/*
 * make_conf - create a new conf entry
 */
struct ConfItem* make_conf()
{
  struct ConfItem* aconf;

  aconf = (struct ConfItem*) MyMalloc(sizeof(struct ConfItem));
  memset(aconf, 0, sizeof(struct ConfItem));
  aconf->status       = CONF_ILLEGAL;
  aconf->ipnum.s_addr = INADDR_NONE;

#if defined(NULL_POINTER_NOT_ZERO)
  aconf->next = NULL;
  aconf->host = aconf->passwd = aconf->name = NULL;
  ClassPtr(aconf) = NULL;
#endif
  return (aconf);
}

/*
 * delist_conf - remove conf item from ConfigItemList
 */
static void delist_conf(struct ConfItem* aconf)
{
  if (aconf == ConfigItemList)
    ConfigItemList = ConfigItemList->next;
  else
    {
      struct ConfItem* bconf;

      for (bconf = ConfigItemList; aconf != bconf->next; bconf = bconf->next)
        ;
      bconf->next = aconf->next;
    }
  aconf->next = NULL;
}

void free_conf(struct ConfItem* aconf)
{
  assert(0 != aconf);

  if (aconf->dns_pending)
    delete_resolver_queries(aconf);
  MyFree(aconf->host);
  if (aconf->passwd)
    memset(aconf->passwd, 0, strlen(aconf->passwd));
  MyFree(aconf->passwd);
  MyFree(aconf->user);
  MyFree(aconf->name);
  MyFree((char*) aconf);
}

/*
 * remove all conf entries from the client except those which match
 * the status field mask.
 */
void        det_confs_butmask(aClient *cptr,int mask)
{
  Link *tmp, *tmp2;

  for (tmp = cptr->confs; tmp; tmp = tmp2)
    {
      tmp2 = tmp->next;
      if ((tmp->value.aconf->status & mask) == 0)
        (void)detach_conf(cptr, tmp->value.aconf);
    }
}

/*
 * find the first (best) I line to attach.
 */
/*
 *  cleanup aug 3 1997 - Dianora
 *  Cleaned up again Sept 7 1998 - Dianora
 */

int attach_Iline(aClient* cptr, struct hostent* hp,
                 const char* sockhost, const char* username, char **preason)
{
  aConfItem* aconf;
  aConfItem* gkill_conf;
  aConfItem* tkline_conf;
  char       host[HOSTLEN + 3];
  char       non_ident[USERLEN + 1];

  host[HOSTLEN] = '\0';

  /* who cares about aliases? sheeeshhh -db */

  if (hp && hp->h_name)
    {
      strncpy(host, hp->h_name, HOSTLEN);
      /*
       * XXX - this probably isn't needed, but ...
       */
      add_local_domain(host, HOSTLEN);
      Debug((DEBUG_DNS, "a_il: %s->%s", sockhost, host));
    }
  else
    {
      strncpy(host, sockhost, HOSTLEN);
    }

  if (IsGotId(cptr))
    {
      aconf = find_matching_mtrie_conf(host,cptr->username,
                                       ntohl(cptr->ip.s_addr));
      if(aconf && !IsConfElined(aconf))
        {
          if( (tkline_conf = find_tkline(host,cptr->username)) )
            aconf = tkline_conf;
        }
    }
  else
    {
      non_ident[0] = '~';
      strncpy(&non_ident[1],username, USERLEN - 1);
      non_ident[USERLEN] = '\0';
      aconf = find_matching_mtrie_conf(host,non_ident,
                                       ntohl(cptr->ip.s_addr));
      if(aconf && !IsConfElined(aconf))
        {
          if( (tkline_conf = find_tkline(host,non_ident)) )
            aconf = tkline_conf;
        }
    }

  if(aconf)
    {
      if (aconf->status & CONF_CLIENT)
        {
#ifdef GLINES
          if ( (gkill_conf = find_gkill(cptr)) )
            {
              *preason = gkill_conf->passwd;
              sendto_one(cptr, ":%s NOTICE %s :*** G-lined",
                           me.name,cptr->name);
              return ( -5 );
            }
#endif        /* GLINES */

          if(IsConfDoIdentd(aconf))
            SetNeedId(cptr);

          /* Thanks for spoof idea amm */
          if(IsConfDoSpoofIp(aconf))
            {
              /* abuse it, lose it. */
#ifdef SPOOF_FREEFORM
              sendto_realops("%s spoofing: %s as %s", cptr->name,
                             host, aconf->name);
              strncpy(cptr->host, aconf->name, HOSTLEN);
#else
              /* default to oper.server.name.tld */
              sendto_realops("%s spoofing: %s(%s) as oper.%s", cptr->name, 
                             host, inetntoa((char*) &cptr->ip), me.name);
              strcpy(cptr->host, "oper.");
              strncpy(&cptr->host[5], me.name, HOSTLEN - 5);
#endif
              SetIPSpoof(cptr);
              SetIPHidden(cptr);
            }

#ifdef LIMIT_UH
          return(attach_iline(cptr, aconf, username));
#else
          return(attach_iline(cptr, aconf));
#endif

        }
      else if(aconf->status & CONF_KILL)
        {
          *preason = aconf->passwd;
          return(-5);
        }
    }

  return -1;        /* -1 on no match *bleh* */
}

/*
 *  rewrote to remove the "ONE" lamity *BLEH* I agree with comstud
 *  on this one. 
 * - Dianora
 */

#ifdef LIMIT_UH
static int attach_iline(
                 aClient *cptr,
                 aConfItem *aconf,char *username)
#else
static int attach_iline(
                 aClient *cptr,
                 aConfItem *aconf)
#endif
{
  IP_ENTRY *ip_found;

  /* if LIMIT_UH is set, limit clients by idented usernames not by ip */

#ifdef LIMIT_UH
  ip_found = find_or_add_ip(cptr,username);
#else
  ip_found = find_or_add_ip(cptr);
#endif

  /* too tired FIX later */
  /*  SetIpHash(cptr); */
  cptr->flags |= FLAGS_IPHASH;
  ip_found->count++;

#ifdef LIMIT_UH
  if ((aconf->class->conFreq) && (ip_found->count_of_idented_users_on_this_ip
                                  > aconf->class->conFreq))
    {
      if(!IsConfFlined(aconf))
        return -4; /* Already at maximum allowed ip#'s */
      else
        {
          sendto_one(cptr,
       ":%s NOTICE %s :*** :I: line is full, but you have an >I: line!",
                     me.name,cptr->name);
        }
    }
#else
  /* only check it if its non zero */
  if (ConfConFreq(aconf) && ip_found->count > ConfConFreq(aconf))
    {
      if(!IsConfFlined(aconf))
        return -4; /* Already at maximum allowed ip#'s */
      else
        {
          sendto_one(cptr,
       ":%s NOTICE %s :*** :I: line is full, but you have an >I: line!",
                     me.name,cptr->name);
        }
    }
#endif

  return ( attach_conf(cptr, aconf) );
}

/* link list of free IP_ENTRY's */

static IP_ENTRY *free_ip_entries;

/*
 * clear_ip_hash_table()
 *
 * input                - NONE
 * output                - NONE
 * side effects        - clear the ip hash table
 *
 * stole the link list pre-allocator from list.c
*/

void clear_ip_hash_table()
{
  void *block_IP_ENTRIES;        /* block of IP_ENTRY's */
  IP_ENTRY *new_IP_ENTRY;        /* new IP_ENTRY being made */
  IP_ENTRY *last_IP_ENTRY;        /* last IP_ENTRY in chain */
  int size;
  int n_left_to_allocate = MAXCONNECTIONS;

  /* ok. if the sizeof the struct isn't aligned with that of the
   * smallest guaranteed valid pointer (void *), then align it
   * ya. you could just turn 'size' into a #define. do it. :-)
   *
   * -Dianora
   */

  size = sizeof(IP_ENTRY) + (sizeof(IP_ENTRY) & (sizeof(void*) - 1) );

  block_IP_ENTRIES = (void *)MyMalloc((size * n_left_to_allocate));  

  free_ip_entries = (IP_ENTRY *)block_IP_ENTRIES;
  last_IP_ENTRY = free_ip_entries;

  /* *shudder* pointer arithmetic */
  while(--n_left_to_allocate)
    {
      block_IP_ENTRIES = (void *)((unsigned long)block_IP_ENTRIES + 
                        (unsigned long) size);
      new_IP_ENTRY = (IP_ENTRY *)block_IP_ENTRIES;
      last_IP_ENTRY->next = new_IP_ENTRY;
      new_IP_ENTRY->next = (IP_ENTRY *)NULL;
      last_IP_ENTRY = new_IP_ENTRY;
    }
  memset((void *)ip_hash_table, 0, sizeof(ip_hash_table));
}

/* 
 * find_or_add_ip()
 *
 * inputs                - cptr
 *                        - name
 *
 * output                - pointer to an IP_ENTRY element
 * side effects        -
 *
 * If the ip # was not found, a new IP_ENTRY is created, and the ip
 * count set to 0.
 */

#ifdef LIMIT_UH
static IP_ENTRY *find_or_add_ip(aClient *cptr,char *username)
#else
static IP_ENTRY *find_or_add_ip(aClient *cptr)
#endif
{
  unsigned long ip_in=cptr->ip.s_addr;        
#ifdef LIMIT_UH
  Link *new_link;
#endif

  int hash_index;
  IP_ENTRY *ptr, *newptr;

  newptr = (IP_ENTRY *)NULL;
  for(ptr = ip_hash_table[hash_index = hash_ip(ip_in)]; ptr; ptr = ptr->next )
    {
      if(ptr->ip == ip_in)
        {
#ifdef LIMIT_UH
          new_link = make_link();
          new_link->value.cptr = cptr;
          new_link->next = ptr->ptr_clients_on_this_ip;
          ptr->ptr_clients_on_this_ip = new_link;
          ptr->count_of_idented_users_on_this_ip =
            count_users_on_this_ip(ptr,cptr,username);
#endif
          return(ptr);
        }
    }

  if ( (ptr = ip_hash_table[hash_index]) != (IP_ENTRY *)NULL )
    {
      if( free_ip_entries == (IP_ENTRY *)NULL)
        {
          sendto_ops("s_conf.c free_ip_entries was found NULL in find_or_add");
          sendto_ops("Please report to the hybrid team! ircd-hybrid@the-project.org");
          outofmemory();
        }

      newptr = ip_hash_table[hash_index] = free_ip_entries;
      free_ip_entries = newptr->next;

      newptr->ip = ip_in;
      newptr->count = 0;
      newptr->next = ptr;
#ifdef LIMIT_UH
      newptr->count_of_idented_users_on_this_ip = 0;
      new_link = make_link();
      new_link->value.cptr = cptr;
      new_link->next = (Link *)NULL;
      newptr->ptr_clients_on_this_ip = new_link;
#endif
      return(newptr);
    }
  else
    {
      if( free_ip_entries == (IP_ENTRY *)NULL)
        {
          sendto_ops("s_conf.c free_ip_entries was found NULL in find_or_add");
          sendto_ops("Please report to the hybrid team! ircd-hybrid@the-project.org");
          outofmemory();
        }

      ptr = ip_hash_table[hash_index] = free_ip_entries;
      free_ip_entries = ptr->next;
      ptr->ip = ip_in;
      ptr->count = 0;
      ptr->next = (IP_ENTRY *)NULL;
#ifdef LIMIT_UH
      ptr->count_of_idented_users_on_this_ip = 0;
      new_link = make_link();
      new_link->value.cptr = cptr;
      new_link->next = (Link *)NULL;
      ptr->ptr_clients_on_this_ip = new_link;
#endif
     return (ptr);
    }
}

#ifdef LIMIT_UH
static int count_users_on_this_ip(IP_ENTRY *ip_list,
                           aClient *this_client,char *username)
{
  int count=0;
  Link *ptr;
  
  for( ptr = ip_list->ptr_clients_on_this_ip; ptr; ptr = ptr->next )
    {
      if(ptr->value.cptr->user)
        {
          if (IsGotId(this_client))
            {
              if(!strcasecmp(ptr->value.cptr->user->username,username))
                  count++;
            }
          else
            {
              if(this_client == ptr->value.cptr)
                count++;
              else
                if(ptr->value.cptr->user->username[0] == '~')
                  count++;
            }
        }
    }
  return(count);
}
#endif

/* 
 * remove_one_ip
 *
 * inputs        - unsigned long IP address value
 * output        - NONE
 * side effects        - ip address listed, is looked up in ip hash table
 *                  and number of ip#'s for that ip decremented.
 *                  if ip # count reaches 0, the IP_ENTRY is returned
 *                  to the free_ip_enties link list.
 */

#ifdef LIMIT_UH
void remove_one_ip(aClient *cptr)
#else
void remove_one_ip(unsigned long ip_in)
#endif
{
  int hash_index;
  IP_ENTRY *last_ptr;
  IP_ENTRY *ptr;
  IP_ENTRY *old_free_ip_entries;
#ifdef LIMIT_UH
  unsigned long ip_in=cptr->ip.s_addr;
  Link *prev_link;
  Link *cur_link;
#endif

  last_ptr = ptr = ip_hash_table[hash_index = hash_ip(ip_in)];
  while(ptr)
    {
      if(ptr->ip == ip_in)
        {
          if(ptr->count != 0)
            ptr->count--;
#ifdef LIMIT_UH

          /* remove the corresponding pointer to this cptr as well */
          prev_link = (Link *)NULL;
          cur_link = ptr->ptr_clients_on_this_ip;

          while(cur_link)
            {
              if(cur_link->value.cptr == cptr)
                {
                  if(prev_link)
                    prev_link->next = cur_link->next;
                  else
                    ptr->ptr_clients_on_this_ip = cur_link->next;
                  free_link(cur_link);
                  break;
                }
              else
                prev_link = cur_link;
              cur_link = cur_link->next;
            }
#endif
          if(ptr->count == 0)
            {
              if(ip_hash_table[hash_index] == ptr)
                ip_hash_table[hash_index] = ptr->next;
              else
                last_ptr->next = ptr->next;
        
              if(free_ip_entries != (IP_ENTRY *)NULL)
                {
                  old_free_ip_entries = free_ip_entries;
                  free_ip_entries = ptr;
                  ptr->next = old_free_ip_entries;
                }
              else
                {
                  free_ip_entries = ptr;
                  ptr->next = (IP_ENTRY *)NULL;
                }
            }
          return;
        }
      else
        {
          last_ptr = ptr;
          ptr = ptr->next;
        }
    }
  sendto_ops("s_conf.c couldn't find ip# in hash table in remove_one_ip()");
  sendto_ops("Please report to the hybrid team! ircd-hybrid@the-project.org");
  return;
}

/*
 * hash_ip()
 * 
 * input        - unsigned long ip address
 * output        - integer value used as index into hash table
 * side effects        - hopefully, none
 */


static int hash_ip(unsigned long ip)
{
  int hash;
  ip = ntohl(ip);
  hash = ((ip >>= 12) + ip) & (IP_HASH_SIZE-1);
  return(hash);
}

/* Added so s_debug could check memory usage in here -Dianora */
/*
 * count_ip_hash
 *
 * inputs        - pointer to counter of number of ips hashed 
 *                - pointer to memory used for ip hash
 * output        - returned via pointers input
 * side effects        - NONE
 *
 * number of hashed ip #'s is counted up, plus the amount of memory
 * used in the hash.
 */

void count_ip_hash(int *number_ips_stored,u_long *mem_ips_stored)
{
  IP_ENTRY *ip_hash_ptr;
  int i;

  *number_ips_stored = 0;
  *mem_ips_stored = 0;

  for(i = 0; i < IP_HASH_SIZE ;i++)
    {
      ip_hash_ptr = ip_hash_table[i];
      while(ip_hash_ptr)
        {
          *number_ips_stored = *number_ips_stored + 1;
          *mem_ips_stored = *mem_ips_stored +
             sizeof(IP_ENTRY);

          ip_hash_ptr = ip_hash_ptr->next;
        }
    }
}


/*
 * iphash_stats()
 *
 * inputs        - 
 * output        -
 * side effects        -
 */

void iphash_stats(aClient *cptr, aClient *sptr,int parc, char *parv[],int out)
{
  IP_ENTRY *ip_hash_ptr;
  int i;
  int collision_count;
  char result_buf[256];

  if(out < 0)
    sendto_one(sptr,":%s NOTICE %s :*** hash stats for iphash",
               me.name,cptr->name);
  else
    {
      (void)sprintf(result_buf,"*** hash stats for iphash\n");
      (void)write(out,result_buf,strlen(result_buf));
    }

  for(i = 0; i < IP_HASH_SIZE ;i++)
    {
      ip_hash_ptr = ip_hash_table[i];

      collision_count = 0;
      while(ip_hash_ptr)
        {
          collision_count++;
          ip_hash_ptr = ip_hash_ptr->next;
        }
      if(collision_count)
        {
          if(out < 0)
            {
              sendto_one(sptr,":%s NOTICE %s :Entry %d (0x%X) Collisions %d",
                         me.name,cptr->name,i,i,collision_count);
            }
          else
            {
              (void)sprintf(result_buf,"Entry %d (0x%X) Collisions %d\n",
                            i,i,collision_count);
              (void)write(out,result_buf,strlen(result_buf));
            }
        }
    }
}

/*
 * Find the single N line and return pointer to it (from list).
 * If more than one then return NULL pointer.
 */
aConfItem        *count_cnlines(Link *lp)
{
  aConfItem        *aconf, *cline = NULL, *nline = NULL;
  
  for (; lp; lp = lp->next)
    {
      aconf = lp->value.aconf;
      if (!(aconf->status & CONF_SERVER_MASK))
        continue;
      if ((aconf->status == CONF_CONNECT_SERVER) && !cline)
        cline = aconf;
      else if (aconf->status == CONF_NOCONNECT_SERVER && !nline)
        nline = aconf;
    }
  return nline;
}

/*
** detach_conf
**        Disassociate configuration from the client.
**      Also removes a class from the list if marked for deleting.
*/
int        detach_conf(aClient *cptr,aConfItem *aconf)
{
  Link        **lp, *tmp;

  lp = &(cptr->confs);

  while (*lp)
    {
      if ((*lp)->value.aconf == aconf)
        {
          if ((aconf) && (ClassPtr(aconf)))
            {
              if (aconf->status & CONF_CLIENT_MASK)
                if (ConfLinks(aconf) > 0)
                  --ConfLinks(aconf);
              if (ConfMaxLinks(aconf) == -1 &&
                  ConfLinks(aconf) == 0)
                {
                  free_class(ClassPtr(aconf));
                  ClassPtr(aconf) = NULL;
                }
            }
          if (aconf && !--aconf->clients && IsIllegal(aconf))
            {
              free_conf(aconf);
            }
          tmp = *lp;
          *lp = tmp->next;
          free_link(tmp);
          return 0;
        }
      else
        lp = &((*lp)->next);
    }
  return -1;
}

static        int        is_attached(aConfItem *aconf,aClient *cptr)
{
  Link        *lp;

  for (lp = cptr->confs; lp; lp = lp->next)
    if (lp->value.aconf == aconf)
      break;
  
  return (lp) ? 1 : 0;
}

/*
** attach_conf
**        Associate a specific configuration entry to a *local*
**        client (this is the one which used in accepting the
**        connection). Note, that this automatically changes the
**        attachment if there was an old one...
*/
int        attach_conf(aClient *cptr,aConfItem *aconf)
{
  Link *lp;

  if (is_attached(aconf, cptr))
    {
      return 1;
    }
  if (IsIllegal(aconf))
    {
      return -1;
    }
  /*
   * By using "ConfLinks(aconf) >= ConfMaxLinks(aconf)....
   * the client limit is set by the Y line, connection class, not
   * by the individual client count in each I line conf.
   *
   * -Dianora
   *
   */

  /* If the requested change, is to turn them into an OPER, then
   * they are already attached to a fd there is no need to check for
   * max in a class now is there?
   *
   * -Dianora
   */

  /* If OLD_Y_LIMIT is defined the code goes back to the old way
   * I lines used to work, i.e. number of clients per I line
   * not total in Y
   * -Dianora
   */
#ifdef OLD_Y_LIMIT
  if ((aconf->status & (CONF_LOCOP | CONF_OPERATOR | CONF_CLIENT)) &&
    aconf->clients >= ConfMaxLinks(aconf) && ConfMaxLinks(aconf) > 0)
#else
  if ( (aconf->status & (CONF_LOCOP | CONF_OPERATOR ) ) == 0 )
    {
      if ((aconf->status & CONF_CLIENT) &&
          ConfLinks(aconf) >= ConfMaxLinks(aconf) && ConfMaxLinks(aconf) > 0)
#endif
        {
          if (!IsConfFlined(aconf))
            {
              return -3;        /* Use this for printing error message */
            }
          else
            {
              send(cptr->fd,
                   "NOTICE FLINE :I: line is full, but you have an >I: line!\n",
                   56, 0);
              SetFlined(cptr);
            }

        }
#ifndef OLD_Y_LIMIT
    }
#endif

  lp = make_link();
  lp->next = cptr->confs;
  lp->value.aconf = aconf;
  cptr->confs = lp;
  aconf->clients++;
  if (aconf->status & CONF_CLIENT_MASK)
    ConfLinks(aconf)++;
  return 0;
}


aConfItem *find_admin()
{
  aConfItem *aconf;

  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    if (aconf->status & CONF_ADMIN && aconf->user)
      break;
  
  return (aconf);
}

aConfItem *find_me()
{
  aConfItem *aconf;
  for (aconf = ConfigItemList; aconf; aconf = aconf->next)
    if (aconf->status & CONF_ME)
      return(aconf);

#ifdef USE_SYSLOG
  syslog(LOG_CRIT,"Server has no M: line");
#endif

  exit(-1);

  return((aConfItem *)NULL);        /* oh oh... is there code to handle
                                   this case ? - Dianora */
                                /* There is now... -Dianora */
}

/*
 * attach_confs
 *  Attach a CONF line to a client if the name passed matches that for
 * the conf file (for non-C/N lines) or is an exact match (C/N lines
 * only).  The difference in behaviour is to stop C:*::* and N:*::*.
 */
aConfItem *attach_confs(aClient *cptr,char *name,int statmask)
{
  aConfItem *tmp;
  aConfItem *first = NULL;
  int len = strlen(name);
  
  if (!name || len > HOSTLEN)
    return((aConfItem *)NULL);

  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) && !IsIllegal(tmp) &&
          ((tmp->status & (CONF_SERVER_MASK|CONF_HUB)) == 0) &&
          tmp->name && match(tmp->name, name))
        {
          if (!attach_conf(cptr, tmp) && !first)
            first = tmp;
        }
      else if ((tmp->status & statmask) && !IsIllegal(tmp) &&
               (tmp->status & (CONF_SERVER_MASK|CONF_HUB)) &&
               tmp->name && !irccmp(tmp->name, name))
        {
          if (!attach_conf(cptr, tmp) && !first)
            first = tmp;
        }
    }
  return (first);
}

/*
 * Added for new access check    meLazy
 */
aConfItem *attach_confs_host(aClient *cptr,char *host,int statmask)
{
  aConfItem *tmp;
  aConfItem *first = NULL;
  int        len = strlen(host);
  
  if (!host || len > HOSTLEN)
    return( (aConfItem *)NULL);

  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) && !IsIllegal(tmp) &&
          (tmp->status & CONF_SERVER_MASK) == 0 &&
          (!tmp->host || match(tmp->host, host)))
        {
          if (!attach_conf(cptr, tmp) && !first)
            first = tmp;
        }
      else if ((tmp->status & statmask) && !IsIllegal(tmp) &&
               (tmp->status & CONF_SERVER_MASK) &&
               (tmp->host && irccmp(tmp->host, host) == 0))
        {
          if (!attach_conf(cptr, tmp) && !first)
            first = tmp;
        }
    }
  return (first);
}

/*
 * find a conf entry which matches the hostname and has the same name.
 */
aConfItem *find_conf_exact(char *name,
                           char *user,
                           char *host,
                           int statmask)
{
  aConfItem *tmp;

  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if (!(tmp->status & statmask) || !tmp->name || !tmp->host ||
          irccmp(tmp->name, name))
        continue;
      /*
      ** Accept if the *real* hostname (usually sockethost)
      ** socket host) matches *either* host or name field
      ** of the configuration.
      */
      if (!match(tmp->host, host) || !match(tmp->user,user)
          || strcasecmp(tmp->name,name) )
        continue;
      if (tmp->status & (CONF_OPERATOR|CONF_LOCOP))
        {
          if (tmp->clients < ConfMaxLinks(tmp))
            return tmp;
          else
            continue;
        }
      else
        return tmp;
    }
  return((aConfItem *)NULL);
}

/*
 * find_conf_name()
 *
 *
 * Accept if the *real* hostname (usually sockethost)
 * matches *either* host or name field of the configuration.
 */

aConfItem *find_conf_name(char *name,int statmask)
{
  aConfItem *tmp;
 
  for (tmp = ConfigItemList; tmp; tmp = tmp->next)
    {
      if ((tmp->status & statmask) &&
          (!tmp->name || match(tmp->name, name)))
        return tmp;
    }
  return((aConfItem *)NULL);
}

aConfItem *find_conf(Link *lp,char *name,int statmask)
{
  aConfItem *tmp;
  int        namelen = name ? strlen(name) : 0;
  
  if (namelen > HOSTLEN)
    return ((aConfItem *) NULL);

  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if ((tmp->status & statmask) &&
          (((tmp->status & (CONF_SERVER_MASK|CONF_HUB)) &&
            tmp->name && !irccmp(tmp->name, name)) ||
           ((tmp->status & (CONF_SERVER_MASK|CONF_HUB)) == 0 &&
            tmp->name && match(tmp->name, name))))
        return tmp;
    }
  return((aConfItem *) NULL);
}

/*
 * Added for new access check    meLazy
 */
aConfItem *find_conf_host(Link *lp, char *host,int statmask)
{
  aConfItem *tmp;
  int        hostlen = host ? strlen(host) : 0;
  
  if (hostlen > HOSTLEN || BadPtr(host))
    return ((aConfItem *)NULL);
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if (tmp->status & statmask &&
          (!(tmp->status & CONF_SERVER_MASK || tmp->host) ||
           (tmp->host && match(tmp->host, host))))
        return tmp;
    }
  return ((aConfItem *)NULL);
}

/*
 * find_conf_ip
 *
 * Find a conf line using the IP# stored in it to search upon.
 * Added 1/8/92 by Avalon.
 */
aConfItem *find_conf_ip(Link *lp,char *ip,char *user, int statmask)
{
  aConfItem *tmp;
  
  for (; lp; lp = lp->next)
    {
      tmp = lp->value.aconf;
      if (!(tmp->status & statmask))
        continue;

      if (!match(tmp->user, user))
        {
          continue;
        }

      if (!memcmp((void *)&tmp->ipnum, (void *)ip, sizeof(struct in_addr)))
        return tmp;
    }
  return ((aConfItem *)NULL);
}

/*
 * find_conf_entry
 *
 * - looks for a match on all given fields.
 */
aConfItem *find_conf_entry(aConfItem *aconf, int mask)
{
  aConfItem *bconf;

  for (bconf = ConfigItemList, mask &= ~CONF_ILLEGAL; bconf; 
       bconf = bconf->next)
    {
      if (!(bconf->status & mask) || (bconf->port != aconf->port))
        continue;
      
      if ((BadPtr(bconf->host) && !BadPtr(aconf->host)) ||
          (BadPtr(aconf->host) && !BadPtr(bconf->host)))
        continue;

      if (!BadPtr(bconf->host) && irccmp(bconf->host, aconf->host))
        continue;

      if ((BadPtr(bconf->passwd) && !BadPtr(aconf->passwd)) ||
          (BadPtr(aconf->passwd) && !BadPtr(bconf->passwd)))
        continue;

      if (!BadPtr(bconf->passwd) &&
          irccmp(bconf->passwd, aconf->passwd))
      continue;

      if ((BadPtr(bconf->name) && !BadPtr(aconf->name)) ||
          (BadPtr(aconf->name) && !BadPtr(bconf->name)))
        continue;

      if (!BadPtr(bconf->name) && irccmp(bconf->name, aconf->name))
        continue;
      break;
    }
  return bconf;
}

/*
 * find_special_conf
 *
 * - looks for a match on name field
 */
aConfItem *find_special_conf(char *to_find, int mask)
{
  aConfItem *aconf;
  aConfItem *this_conf;

  if(mask & CONF_XLINE)
    this_conf = x_conf;
  else if(mask & CONF_ULINE)
    this_conf = u_conf;
  else
    return((aConfItem *)NULL);

  for (aconf = this_conf; aconf; aconf = aconf->next)
    {
      if (!(aconf->status & mask))
        continue;
      
      if (BadPtr(aconf->name))
          continue;

      if(match(aconf->name,to_find))
        return aconf;

    }
  return((aConfItem *)NULL);
}

/*
 * find_q_line
 *
 * - looks for matches on Q lined nick
 */
int find_q_line(char *nickToFind,char *user,char *host)
{
  aQlineItem *qp;
  aConfItem *aconf;

  for (qp = q_conf; qp; qp = qp->next)
    {
      if (BadPtr(qp->name))
          continue;

      if(match(qp->name,nickToFind))
        {
          if(qp->confList)
            {
              for(aconf=qp->confList;aconf;aconf=aconf->next)
                {
                  if(match(aconf->user,user) && match(aconf->host,host))
                    return NO;
                }
            }
          return YES;
        }
    }
  return NO;
}

/*
 * clear_q_lines
 *
 * - clear out the q lines
 */
void clear_q_lines()
{
  aQlineItem *qp;
  aQlineItem *qp_next;
  aConfItem *aconf;
  aConfItem *next_aconf;

  for (qp = q_conf; qp; qp = qp_next)
    {
      qp_next = qp->next;

      if(qp->name)
        {
          MyFree(qp->name);
          qp->name = (char *)NULL;
        }

      if (qp->confList)
        {
          for (aconf = qp->confList; aconf; aconf = next_aconf)
            {
              next_aconf = aconf->next;
              free_conf(aconf);
            }
        }
      MyFree(qp);
    }
  q_conf = (aQlineItem *)NULL;
}


void report_qlines(aClient *sptr)
{
  aQlineItem *qp;
  aConfItem *aconf;
  char *host, *user, *pass, *name;
  int port;

  for (qp = q_conf; qp; qp = qp->next)
    {
      if(!qp->name)
        {
          continue;
        }

      for (aconf=qp->confList;aconf;aconf = aconf->next)
        {
          GetPrintableaConfItem(aconf, &name, &host, &pass, &user, &port);
          
          sendto_one(sptr, rpl_str(RPL_STATSQLINE),
                     me.name,
                     sptr->name,
                     name,
                     pass,
                     user,
                     host);
        }
    }
}

void add_q_line(aConfItem *aconf)
{
  char *pc;
  aQlineItem *qp, *newqp;
  char *uath;

  if(!aconf->user)
    DupString(aconf->user, "-");

  for (qp = q_conf; qp; qp = qp->next)
    {
      if(!qp->name)
        {
          continue;
        }

      if(!strcasecmp(aconf->name,qp->name))
        {
          if (qp->confList)
            {
              /* 
               * - Slowaris
               */
              
              uath = strtoken(&pc, aconf->user, ",");
              if(!uath)
                {
                  uath = aconf->user;
                  makeQlineEntry(qp, aconf, uath);
                }
              else
                {
                  for( ; uath; uath = strtoken(&pc,NULL,","))
                    {
                      makeQlineEntry(qp, aconf, uath);
                    }
                }
            }
          free_conf(aconf);
          return;
        }
    }

  newqp = (aQlineItem *)MyMalloc(sizeof(aQlineItem));
  newqp->confList = (aConfItem *)NULL;
  DupString(newqp->name,aconf->name);
  newqp->next = q_conf;
  q_conf = newqp;

  /* 
   * - Slowaris
   */

  uath = strtoken(&pc, aconf->user, ",");
  if(!uath)
    {
      uath = aconf->user;
      makeQlineEntry(newqp, aconf, uath);
    }
  else
    {
      for ( ;uath; uath = strtoken(&pc,(char *)NULL,","))
        {
          makeQlineEntry(newqp, aconf, uath);
        }
    }

  free_conf(aconf);
}

static void makeQlineEntry(aQlineItem *qp, aConfItem *aconf, char *uath)
{
  char *p,*comu,*comh;
  aConfItem *bconf;

  p = strchr(uath, '@');
  if(!p)
    {
      DupString(comu,"-");
      DupString(comh,"-");
    }
  else
    {
      *p = '\0';
      DupString(comu,uath);
      p++;
      DupString(comh,p);
    }
                  
  bconf = make_conf();
  DupString(bconf->name, aconf->name);
  DupString(bconf->passwd,aconf->passwd);
  bconf->user = comu;
  bconf->host = comh;
  bconf->next = qp->confList;
  qp->confList = bconf;
}

/*
 * clear_special_conf
 *
 * - clears given special conf lines
 */
void clear_special_conf(aConfItem **this_conf)
{
  aConfItem *aconf;
  aConfItem *next_aconf;

  for (aconf = *this_conf; aconf; aconf = next_aconf)
    {
      next_aconf = aconf->next;
      free_conf(aconf);
    }
  *this_conf = (aConfItem *)NULL;
  return;
}


/*
 * partially reconstruct an ircd.conf file (tsk tsk, you should have
 * been making backups;but we've all done it)
 * I just cull out the N/C/O/o/A lines, you'll have to finish
 * the rest to use this dump.
 *
 * -Dianora
 */

int rehash_dump(aClient *sptr,char *parv0)
{
  aConfItem *aconf;
  FBFILE* out;
  char ircd_dump_file[256];
  char result_buf[256];
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  tmptr = localtime(&NOW);
  strftime(timebuffer, MAX_DATE_STRING, "%Y%m%d%H%M", tmptr);
  (void)sprintf(ircd_dump_file,"%s/ircd.conf.%s",
                DPATH,timebuffer);
  
  if ((out = fbopen(ircd_dump_file, "a")) == 0)
    {
      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
                 me.name, parv0, ircd_dump_file);
      return -1;
    }
  else
    sendto_one(sptr, ":%s NOTICE %s :Dump ircd.conf to %s ",
               me.name, parv0, ircd_dump_file);

  for(aconf = ConfigItemList; aconf; aconf = aconf->next)
    {
      aClass* class_ptr = ClassPtr(aconf);

      if(aconf->status == CONF_CONNECT_SERVER)
        {
          
          (void)sprintf(result_buf,"C:%s:%s:%s::%d\n",
                        aconf->host,aconf->passwd,
                        aconf->name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_NOCONNECT_SERVER)
        {
          (void)sprintf(result_buf,"N:%s:%s:%s::%d\n",
                        aconf->host,aconf->passwd,
                        aconf->name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_OPERATOR)
        {
          (void)sprintf(result_buf,"O:%s@%s:%s:%s::%d\n",
                        aconf->user,aconf->host,
                        aconf->passwd,
                        aconf->name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_LOCOP)
        {
          (void)sprintf(result_buf,"o:%s@%s:%s:%s::%d\n",
                        aconf->user,aconf->host,
                        aconf->passwd,
                        aconf->name,
                        ClassType(class_ptr));
          fbputs(result_buf, out);
        }
      else if(aconf->status == CONF_ADMIN)
        {
          (void)sprintf(result_buf,"A:%s:%s:%s::\n",
                        aconf->user,aconf->passwd,aconf->name);
          fbputs(result_buf, out);
        }
    }
  fbclose(out);
  return 0;
}

/*
 * rehash
 *
 * Actual REHASH service routine. Called with sig == 0 if it has been called
 * as a result of an operator issuing this command, else assume it has been
 * called as a result of the server receiving a HUP signal.
 */
int rehash(aClient *cptr,aClient *sptr,int sig)
{
  if (sig == SIGHUP)
    {
      sendto_ops("Got signal SIGHUP, reloading ircd conf. file");
#ifdef        ULTRIX
      if (fork() > 0)
        exit(0);
      write_pidfile();
#endif
    }
  read_conf_files(NO);
  close_listeners();
  flush_deleted_I_P();
  rehashed = 1;
  return 0;
}

/*
 * openconf
 *
 * returns 0 (NULL) on any error or else the fd opened from which to read the
 * configuration file from.  
 */
static FBFILE* openconf(char *filename)
{
  return fbopen(filename, "r");
}
extern char *getfield();

/*
** from comstud
*/

static char *set_conf_flags(aConfItem *aconf,char *tmp)
{
  for(;*tmp;tmp++)
    {
      switch(*tmp)
        {
        case '=':
          aconf->flags |= CONF_FLAGS_SPOOF_IP;
          break;
        case '!':
          aconf->flags |= CONF_FLAGS_LIMIT_IP;
          break;
        case '-':
          aconf->flags |= CONF_FLAGS_NO_TILDE;
          break;
        case '+':
          aconf->flags |= CONF_FLAGS_NEED_IDENTD;
          break;
        case '$':
          aconf->flags |= CONF_FLAGS_PASS_IDENTD;
          break;
        case '%':
          aconf->flags |= CONF_FLAGS_NOMATCH_IP;
          break;
        case '^':        /* is exempt from k/g lines */
          aconf->flags |= CONF_FLAGS_E_LINED;
          break;
        case '&':        /* can run a bot */
          aconf->flags |= CONF_FLAGS_B_LINED;
          break;
        case '>':        /* can exceed max connects */
          aconf->flags |= CONF_FLAGS_F_LINED;
          break;
#ifdef IDLE_CHECK
        case '<':        /* can idle */
          aconf->flags |= CONF_FLAGS_IDLE_LINED;
          break;
#endif
        default:
          return tmp;
        }
    }
  return tmp;
}

/*
** initconf() 
**    Read configuration file.
**
*
* Inputs   	- file descriptor pointing to config file to use
*
**    returns -1, if file cannot be opened
**             0, if file opened
*/

#define MAXCONFLINKS 150

static void initconf(FBFILE* file, int use_include)
{
  static char  quotes[9][2] = {
    {'b', '\b'}, {'f', '\f'}, {'n', '\n'},
    {'r', '\r'}, {'t', '\t'}, {'v', '\v'},
    {'\\', '\\'}, { 0, 0}
  };

  char        *tmp, *s;
  int        i, dontadd;
  char        line[BUFSIZE];
  int        ccount = 0, ncount = 0;
  u_long vaddr;
  aConfItem *aconf = NULL;
  aConfItem *include_conf = NULL;
  unsigned long ip;
  unsigned long ip_mask;
  int sendq = 0;
  aClass *class0;

  class0 = find_class(0);        /* which one is class 0 ? */

  while (fbgets(line, sizeof(line), file))
    {
      if ((tmp = strchr(line, '\n')))
        *tmp = '\0';

      /*
       * Do quoting of characters and # detection.
       */
      for (tmp = line; *tmp; tmp++)
        {
          if (*tmp == '\\')
            {
              for (i = 0; quotes[i][0]; i++)
                if (quotes[i][0] == *(tmp+1))
                  {
                    *tmp = quotes[i][1];
                    break;
                  }
              if (!quotes[i][0])
                *tmp = *(tmp+1);
              if (!*(tmp+1))
                break;
              else
                for (s = tmp; (*s = *(s+1)); s++)
                  ;
            }
          else if (*tmp == '#')
            *tmp = '\0';
        }
      if (!*line || line[0] == '#' || line[0] == '\n' ||
          line[0] == ' ' || line[0] == '\t')
        continue;

      /* Horrible kludge to do .include "filename" */

      if(use_include && (line[0] == '.'))
        {
          char *filename;
          char *back;

          if(!strncasecmp(line+1,"include ",8))
            {
              if( (filename = strchr(line+8,'"')) )
                filename++;
              else
                {
                  Debug((DEBUG_ERROR, "Bad config line: %s", line));
                  continue;
                }

              if( (back = strchr(filename,'"')) )
                *back = '\0';
              else
                {
                  Debug((DEBUG_ERROR, "Bad config line: %s", line));
                  continue;
                }
              include_conf = make_conf();
              DupString(include_conf->name,filename);
              include_conf->next = include_list;
              include_list = include_conf;
            }
          /* 
           * A line consisting of the first char '.' will now
           * be treated as a comment line.
           * a line `.include "file"' will result in an included
           * portion of the conf file.
           */
          continue;
        }

      /* Could we test if it's conf line at all?        -Vesa */
      if (line[1] != ':')
        {
          Debug((DEBUG_ERROR, "Bad config line: %s", line));
          continue;
        }
      if (aconf)
        free_conf(aconf);
      aconf = make_conf();

      tmp = getfield(line);
      if (!tmp)
        continue;
      dontadd = 0;
      switch (*tmp)
        {
        case 'A': /* Name, e-mail address of administrator */
        case 'a': /* of this server. */
          aconf->status = CONF_ADMIN;
          break;

        case 'C': /* Server where I should try to connect */
                    /* in case of lp failures             */
          ccount++;
          aconf->status = CONF_CONNECT_SERVER;
          aconf->flags = CONF_FLAGS_ALLOW_AUTO_CONN;
          break;

        case 'c':
          ccount++;
          aconf->status = CONF_CONNECT_SERVER;
          aconf->flags = CONF_FLAGS_ALLOW_AUTO_CONN|CONF_FLAGS_ZIP_LINK;
          break;

        case 'd':
          aconf->status = CONF_DLINE;
          aconf->flags = CONF_FLAGS_E_LINED;
          break;
        case 'D': /* Deny lines (immediate refusal) */
          aconf->status = CONF_DLINE;
          break;

        case 'H': /* Hub server line */
        case 'h':
          aconf->status = CONF_HUB;
          break;

#ifdef LITTLE_I_LINES
        case 'i': /* Just plain normal irc client trying  */
                  /* to connect to me */
          aconf->status = CONF_CLIENT;
          aconf->flags |= CONF_FLAGS_LITTLE_I_LINE;
          break;

        case 'I': /* Just plain normal irc client trying  */
                  /* to connect to me */
          aconf->status = CONF_CLIENT;
          break;
#else
        case 'i': /* Just plain normal irc client trying  */
        case 'I': /* to connect to me */
          aconf->status = CONF_CLIENT;
          break;
#endif
        case 'K': /* Kill user line on irc.conf           */
        case 'k':
          aconf->status = CONF_KILL;
          break;

        case 'L': /* guaranteed leaf server */
        case 'l':
          aconf->status = CONF_LEAF;
          break;

          /* Me. Host field is name used for this host */
          /* and port number is the number of the port */
        case 'M':
        case 'm':
          aconf->status = CONF_ME;
          break;

        case 'N': /* Server where I should NOT try to     */
        case 'n': /* connect in case of lp failures     */
          /* but which tries to connect ME        */
          ++ncount;
          aconf->status = CONF_NOCONNECT_SERVER;
          break;

          /* Operator. Line should contain at least */
          /* password and host where connection is  */

        case 'O':
          aconf->status = CONF_OPERATOR;
          break;
          /* Local Operator, (limited privs --SRB) */

        case 'o':
          aconf->status = CONF_LOCOP;
          break;

        case 'P': /* listen port line */
        case 'p':
          aconf->status = CONF_LISTEN_PORT;
          break;

        case 'Q': /* reserved nicks */
        case 'q': 
          aconf->status = CONF_QUARANTINED_NICK;
          break;

        case 'U': /* Uphost, ie. host where client reading */
        case 'u': /* this should connect.                  */
          aconf->status = CONF_ULINE;
          break;

        case 'X': /* rejected gecos */
        case 'x': 
          aconf->status = CONF_XLINE;
          break;

        case 'Y':
        case 'y':
          aconf->status = CONF_CLASS;
          sendq = 0;
          break;

        default:
          Debug((DEBUG_ERROR, "Error in config file: %s", line));
          break;
        }
      if (IsIllegal(aconf))
        continue;

      for (;;) /* Fake loop, that I can use break here --msa */
        {
          if ((tmp = getfield(NULL)) == NULL)
            break;
          /*from comstud*/
          if(aconf->status & CONF_CLIENT)
            tmp = set_conf_flags(aconf, tmp);
          DupString(aconf->host, tmp);

          if ((tmp = getfield(NULL)) == NULL)
            break;
          DupString(aconf->passwd, tmp);

          if ((tmp = getfield(NULL)) == NULL)
            break;
          /*from comstud */

          if(aconf->status & CONF_CLIENT)
            tmp = set_conf_flags(aconf, tmp);
          DupString(aconf->user, tmp);

          if(aconf->status & CONF_OPERATOR)
            {
              Debug((DEBUG_DEBUG,"Setting defaults for oper"));
              /* defaults */
              aconf->port = 
                CONF_OPER_GLOBAL_KILL|CONF_OPER_REMOTE|CONF_OPER_UNKLINE|
                CONF_OPER_K|CONF_OPER_GLINE|CONF_OPER_REHASH;
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->port = get_oper_privs(aconf->port,tmp);
            }
          else if(aconf->status & CONF_LOCOP)
            {
              Debug((DEBUG_DEBUG,"Setting defaults for local oper"));
              aconf->port = CONF_OPER_UNKLINE|CONF_OPER_K;
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->port = get_oper_privs(aconf->port,tmp);
            }
          else
            {
              if ((tmp = getfield(NULL)) == NULL)
                break;
              aconf->port = atoi(tmp);
            }

          Debug((DEBUG_DEBUG,"aconf->port %x",aconf->port));

          if ((tmp = getfield(NULL)) == NULL)
            break;
          Debug((DEBUG_DEBUG,"class tmp = %s",tmp));

          if(aconf->status & CONF_CLASS)
            {
              sendq = atoi(tmp);
            }
          else
            {
              int classToFind;

              classToFind = atoi(tmp);

              ClassPtr(aconf) = find_class(classToFind);

              if(classToFind && (ClassPtr(aconf) == class0))
                {
                  sendto_realops(
                           "Warning *** Defaulting to class 0 for class %d",
                         classToFind);
                }
            }

          if(aconf->status & (CONF_LOCOP|CONF_OPERATOR))
            {
              if ((tmp = getfield(NULL)) == NULL)
                break;
              (int)aconf->hold = get_oper_flags(tmp);
            }

          break;
          /* NOTREACHED */
        }

      /* For Gersh
       * make sure H: lines don't have trailing spaces!
       * BUG: This code will fail if there is leading whitespace.
       */

      if( aconf->status & (CONF_HUB|CONF_LEAF) )
        {
          char *ps;        /* space finder */
          char *pt;        /* tab finder */

          ps = strchr(aconf->user,' ');
          pt = strchr(aconf->user,'\t');

          if(ps || pt)
            {
              sendto_realops("H: or L: line trailing whitespace [%s]",
                             aconf->name);
              if(ps)*ps = '\0';
              if(pt)*pt = '\0';
            }
          aconf->name = aconf->user;
          DupString(aconf->user, "*");
        }

      /*
      ** If conf line is a class definition, create a class entry
      ** for it and make the conf_line illegal and delete it.
      ** Negative class numbers are not accepted.
      */
      if (aconf->status & CONF_CLASS && atoi(aconf->host) > -1)
        {
          add_class(atoi(aconf->host), atoi(aconf->passwd),
                    atoi(aconf->user), aconf->port,
                    sendq );
          continue;
        }
      /*
      ** associate each conf line with a class by using a pointer
      ** to the correct class record. -avalon
      */

      /*  Unless its a Y line itself, associate this with a class */
      if (aconf->status & (CONF_CLIENT_MASK|CONF_LISTEN_PORT))
        {
          if (0 == ClassPtr(aconf))
            ClassPtr(aconf) = find_class(0);
          if (ConfMaxLinks(aconf) < 0)
            ClassPtr(aconf) = find_class(0);
        }

      /* P: line or I: line */
      if (aconf->status & (CONF_LISTEN_PORT|CONF_CLIENT))
        {
          aConfItem *bconf;
          
          if ( (bconf = find_conf_entry(aconf, aconf->status)) )
            {
              delist_conf(bconf);
              bconf->status &= ~CONF_ILLEGAL;
              if (aconf->status == CONF_CLIENT)
                {
                  ConfLinks(bconf) -= bconf->clients;
                  ClassPtr(bconf) = ClassPtr(aconf);
                  ConfLinks(bconf) += bconf->clients;
                  /*
                   * still... I've munged the flags possibly
                   * so update the found aConfItem for now 
                   * -Dianora
                   */
                  bconf->flags = aconf->flags;
                  if(bconf->flags & (CONF_LOCOP|CONF_OPERATOR))
                    bconf->port = aconf->port;
                }
              free_conf(aconf);
              aconf = bconf;
            }
          else if (aconf->host &&
                   aconf->status == CONF_LISTEN_PORT)
            (void)add_listener(aconf);
        }

      if (aconf->status & CONF_SERVER_MASK)
        {
          if (ncount > MAXCONFLINKS || ccount > MAXCONFLINKS ||
              !aconf->host || !aconf->user)
            {
              sendto_realops("Bad C/N line");
              continue;
            }

          if (BadPtr(aconf->passwd))
            {
              sendto_realops("Bad C/N line host %s", aconf->host);
              continue;
            }
          
          if( SplitUserHost(aconf) < 0 )
            {
              sendto_realops("Bad C/N line host %s", aconf->host);
              free_conf(aconf);
              aconf = NULL;
              continue;
            }

	  lookup_confhost(aconf);
        }
      
      /* o: or O: line */

      if (aconf->status & (CONF_LOCOP|CONF_OPERATOR))
        {
          if(SplitUserHost(aconf) < 0)
            {
              sendto_realops("Bad O/o line host %s", aconf->host);
              free_conf(aconf);
              aconf = NULL;
            }
        }

      /*
      ** Own port and name cannot be changed after the startup.
      ** (or could be allowed, but only if all links are closed
      ** first).
      ** Configuration info does not override the name and port
      ** if previously defined. Note, that "info"-field can be
      ** changed by "/rehash".
      ** Can't change vhost mode/address either 
      */
      if (aconf->status == CONF_ME)
        {
          strncpyzt(me.info, aconf->user, sizeof(me.info));

          if (me.name[0] == '\0' && aconf->host[0])
          {
            strncpyzt(me.name, aconf->host,
                      sizeof(me.name));
            if ((aconf->passwd[0] != '\0') && (aconf->passwd[0] != '*'))
            {
                memset(&vserv,0, sizeof(vserv));
                vserv.sin_family = AF_INET;
                vaddr = inet_addr(aconf->passwd);
                memcpy( (void *)&vserv.sin_addr,
                        (void *) &vaddr, sizeof(struct in_addr));
                specific_virtual_host = 1;
            }
          }

          if (portnum < 0 && aconf->port >= 0)
            portnum = aconf->port;
        }
      else if (aconf->host && (aconf->status & CONF_CLIENT))
        {
          char *p;

          dontadd = 1;

          (void)collapse(aconf->host);
          (void)collapse(aconf->user);

          /* The idea here is, to separate a name@host part
           * into aconf->host part and aconf->user part
           * If the user@host part is found in the aconf->host field
           * from conf file, then it has to be an IP I line.
           */

          MyFree(aconf->name); /* should be already NULL here */

          /* Keep a copy of the original host part in "name" */
          DupString(aconf->name,aconf->host);

          /* see if the user@host part is on the 'left side'
           * in the aconf->host field. If it is, then it should be
           * an IP I line only, but I won't enforce it here. 
           */

          if( (p = strchr(aconf->host,'@')) )
            {
              aconf->flags |= CONF_FLAGS_DO_IDENTD;
              *p = '\0';
              MyFree(aconf->user);
              DupString(aconf->user,aconf->host);
              p++;
              MyFree(aconf->name);
              DupString(aconf->name,p);
            }
          else
            {

            /* See if there is a name@host part on the 'right side'
             * in the aconf->name field.
             */

              if( ( p = strchr(aconf->user,'@')) )
                {
                  aconf->flags |= CONF_FLAGS_DO_IDENTD;
                  *p = '\0';
                  p++;
                  MyFree(aconf->host);
                  DupString(aconf->host,p);
                }
              else
                {
                  MyFree(aconf->host);
                  aconf->host = aconf->user;
                  DupString(aconf->user,"*");
                }
            }
          
          add_mtrie_conf_entry(aconf,CONF_CLIENT);
        }
      else if (aconf->host && (aconf->status & CONF_KILL))
        {
          dontadd = 1;
          if(is_address(aconf->host,&ip,&ip_mask))
            {
              ip &= ip_mask;
              aconf->ip = ip;
              aconf->ip_mask = ip_mask;
              add_ip_Kline(aconf);
            }
          else
            {
              (void)collapse(aconf->host);
              (void)collapse(aconf->user);
              add_mtrie_conf_entry(aconf,CONF_KILL);
            }
        }
      else if (aconf->host && (aconf->status & CONF_DLINE))
        {
          dontadd = 1;
          DupString(aconf->user,aconf->host);
          (void)is_address(aconf->host,&ip,&ip_mask);
          ip &= ip_mask;
          aconf->ip = ip;
          aconf->ip_mask = ip_mask;

          if(aconf->flags & CONF_FLAGS_E_LINED)
            add_dline(aconf);
          else
            add_Dline(aconf);
        }
      else if (aconf->status & CONF_XLINE)
        {
          dontadd = 1;
          MyFree(aconf->user);
          aconf->user = aconf->host;
          aconf->host = (char *)NULL;
          aconf->next = x_conf;
          x_conf = aconf;
        }
      else if (aconf->status & CONF_ULINE)
        {
          dontadd = 1;
          MyFree(aconf->user);
          aconf->user = aconf->host;
          aconf->host = (char *)NULL;
          aconf->next = u_conf;
          u_conf = aconf;
        }
      else if (aconf->status & CONF_QUARANTINED_NICK)
        {
          dontadd = 1;
          aconf->name = aconf->host;
          DupString(aconf->host, "*");

#ifdef JUPE_CHANNEL
          if(aconf->name[0] == '#')
            {
              aChannel *chptr;
              int len;

              if( (chptr = find_channel(aconf->name, (aChannel *)NULL)) )
                chptr->mode.mode |= MODE_JUPED;
              else
                {
                  /* create a zero user channel, marked as MODE_JUPED
                   * which just place holds the channel down.
                   */

                  len = strlen(aconf->name);
                  chptr = (aChannel *)MyMalloc(sizeof(aChannel) + len);
                  memset((void *)chptr, 0, sizeof(aChannel));
                  strncpyzt(chptr->chname, aconf->name, len+1);
                  chptr->mode.mode = MODE_JUPED;
                  if (channel)
                    channel->prevch = chptr;
                  chptr->prevch = NULL;
                  chptr->nextch = channel;
                  channel = chptr;
                  /* JIC */
                  chptr->channelts = NOW;
                  (void)add_to_channel_hash_table(aconf->name, chptr);
                  Count.chan++;
                }

              if(aconf->passwd)
                strncpyzt(chptr->topic, aconf->passwd, sizeof(chptr->topic));
            }
#endif

          /* host, password, name, port, class */
          /* nick, reason, user@host */
          
          add_q_line(aconf);
        }

      if (!dontadd)
        {
          (void)collapse(aconf->host);
          (void)collapse(aconf->user);
          Debug((DEBUG_NOTICE,
                 "Read Init: (%d) (%s) (%s) (%s) (%d) (%d)",
                 aconf->status, aconf->host, aconf->passwd,
                 aconf->user, aconf->port, Class(aconf)));
          aconf->next = ConfigItemList;
          ConfigItemList = aconf;
        }
      aconf = NULL;
    }
  if (aconf)
    free_conf(aconf);
  aconf = NULL;

  fbclose(file);
  check_class();
  nextping = nextconnect = time(NULL);

  if(me.name[0] == '\0')
    {
      report_error_on_tty("Server has no M: line\n");
#ifdef USE_SYSLOG
      syslog(LOG_CRIT,"Server has no M: line");
#endif
      exit(-1);
    }
}

/*
 * commonly used function to split user@host part into user and host fields
 */

static int SplitUserHost(aConfItem *aconf)
{
  char *p;

  if ( (p = strchr(aconf->host, '@')) )
    {
      *p = '\0';
      p++;
      if(aconf->user)
        {
          aconf->name = aconf->user;
          DupString(aconf->user, aconf->host);
          strcpy(aconf->host, p);
        }
      else
        {
          return(-1);
        }
    }
  else
    {
      if(aconf->user)
        {
          aconf->name = aconf->user;
          DupString(aconf->user, "*");
        }
    }
  return(1);
}

/*
 * do_include_conf()
 *
 * inputs        - NONE
 * output        - NONE
 * side effect        -
 * hash in any .include conf files listed in the conf file
 * -Dianora
 */

void do_include_conf()
{
  FBFILE* file=0;
  aConfItem *nextinclude;

  for( ; include_list; include_list = nextinclude )
    {
      nextinclude = include_list->next;
      if ((file = openconf(include_list->name)) == 0)
        sendto_ops("Can't open %s include file",include_list->name);
      else
        {
          sendto_ops("Hashing in %s include file",include_list->name);
          initconf(file, NO);
        }
      free_conf(include_list);
    }
}

/*
 * lookup_confhost - start DNS lookups of all hostnames in the conf
 * line and convert an IP addresses in a.b.c.d number for to IP#s.
 *
 */
static void lookup_confhost(aConfItem* aconf)
{
  struct DNSReply* dns_reply;
  unsigned long    ip;
  unsigned long    mask;

  if (BadPtr(aconf->host) || BadPtr(aconf->name))
    {
      Debug((DEBUG_ERROR,"Host/server name error: (%s) (%s)",
             aconf->host, aconf->name));
      return;
    }

  /*
  ** Do name lookup now on hostnames given and store the
  ** ip numbers in conf structure.
  */
  if (is_address(aconf->host, &ip, &mask))
    {
      aconf->ipnum.s_addr = htonl(ip);
    }
  else if (0 != (dns_reply = conf_dns_lookup(aconf)))
    memcpy(&aconf->ipnum, dns_reply->hp->h_addr, sizeof(struct in_addr));
  
  if (INADDR_NONE == aconf->ipnum.s_addr) {
    Debug((DEBUG_ERROR,"Host/server name error: (%s) (%s)",
           aconf->host, aconf->name));
  }
}

/*
 * find_kill
 *
 * See if this user is klined already, and if so, return aConfItem pointer
 * to the entry for this kline. This wildly changes the way find_kill works
 * -Dianora
 *
 */
aConfItem *find_kill(aClient* cptr)
{
  assert(0 != cptr);
#if 0
  /*
   * this can't possibly ever happen, this this whole thing is a 
   * waste of cpu, if either of these tests are true it's too late,
   * we're already screwed
   * --Bleep
   */
  char* host;
  char* name;
  
  if (!cptr->user)
    return 0;

  host = cptr->host;
  name = cptr->username;
  if (strlen(host)  > (size_t) HOSTLEN ||
      (name ? strlen(name) : 0) > (size_t) HOSTLEN)
    return (0);
#endif
  /* If client is e-lined, then its not k-linable */
  /* opers get that flag automatically, normal users do not */
  return (IsElined(cptr)) ? 0 : find_is_klined(cptr->host, 
                                               cptr->username, 
                                               cptr->ip.s_addr);
}

/*
 * find_is_klined()
 *
 * inputs        - hostname
 *                - username
 *                - ip of possible "victim"
 * output        - matching aConfItem or NULL
 * side effects        -
 *
 * WARNING, no sanity checking on length of name,host etc.
 * thats expected to be done by caller.... *sigh* -Dianora
 */

aConfItem *find_is_klined(char *host,char *name,unsigned long ip)
{
  aConfItem *found_aconf;

  if( (found_aconf = find_tkline(host,name)) )
    return(found_aconf);

  /* find_matching_mtrie_conf() can return either CONF_KILL,
   * CONF_CLIENT or NULL, i.e. no I line at all.
   */

  found_aconf = find_matching_mtrie_conf(host,name,ntohl(ip));
  if(found_aconf && (found_aconf->status & (CONF_ELINE|CONF_DLINE|CONF_KILL)))
    return(found_aconf);
  else
    return((aConfItem *)NULL);
}

/*
 * find_tkline
 *
 * inputs        - hostname
 *                - username
 * output        - matching aConfItem or NULL
 * side effects        -
 *
 * WARNING, no sanity checking on length of name,host etc.
 * thats expected to be done by caller.... *sigh* -Dianora
 */

static aConfItem *find_tkline(char *host,char *user)
{
  aConfItem *kill_list_ptr;        /* used for the link list only */
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;

  if(temporary_klines)
    {
      kill_list_ptr = last_list_ptr = temporary_klines;

      while(kill_list_ptr)
        {
          if(kill_list_ptr->hold <= NOW)        /* a kline has expired */
            {
              if(temporary_klines == kill_list_ptr)
                {
                  temporary_klines = last_list_ptr = tmp_list_ptr =
                    kill_list_ptr->next;
                }
              else
                {
                  /* its in the middle of the list, so link around it */
                  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
                }

              free_conf(kill_list_ptr);
              kill_list_ptr = tmp_list_ptr;
            }
          else
            {
              if( (kill_list_ptr->user
                   && (!user || match(kill_list_ptr->user, user)))
                  && (kill_list_ptr->host
                      && (!host || match(kill_list_ptr->host,host))))
                return(kill_list_ptr);
              last_list_ptr = kill_list_ptr;
              kill_list_ptr = kill_list_ptr->next;
            }
        }
    }

  return((aConfItem *)NULL);
}

/* add_temp_kline
 *
 * inputs        - pointer to aConfItem
 * output        - none
 * Side effects        - links in given aConfItem into temporary kline link list
 * 
 * -Dianora
 */

void add_temp_kline(aConfItem *aconf)
{
  aconf->next = temporary_klines;
  temporary_klines = aconf;
}

/* flush_temp_klines
 *
 * inputs        - NONE
 * output        - NONE
 * side effects        - All temporary klines are flushed out. 
 *
 */
void flush_temp_klines()
{
  aConfItem *kill_list_ptr;

  if( (kill_list_ptr = temporary_klines) )
    {
      while(kill_list_ptr)
        {
          temporary_klines = kill_list_ptr->next;
          free_conf(kill_list_ptr);
          kill_list_ptr = temporary_klines;
        }
    }
}

/* report_temp_klines
 *
 * inputs        - aClient pointer, client to report to
 * output        - NONE
 * side effects        - NONE
 *                  
 */
void report_temp_klines(aClient *sptr)
{
  aConfItem *kill_list_ptr;
  aConfItem *last_list_ptr;
  aConfItem *tmp_list_ptr;
  char *host;
  char *name;
  char *reason;
  char *p;

  if(temporary_klines)
    {
      kill_list_ptr = last_list_ptr = temporary_klines;

      while(kill_list_ptr)
        {
          if(kill_list_ptr->hold <= NOW)        /* kline has expired */
            {
              if(temporary_klines == kill_list_ptr)
                {
                  /* Its pointing to first one in link list*/
                  /* so, bypass this one, remember bad things can happen
                     if you try to use an already freed pointer.. */

                  temporary_klines = last_list_ptr = tmp_list_ptr =
                    kill_list_ptr->next;
                }
              else
                {
                  /* its in the middle of the list, so link around it */
                  tmp_list_ptr = last_list_ptr->next = kill_list_ptr->next;
                }

              free_conf(kill_list_ptr);
              kill_list_ptr = tmp_list_ptr;
            }
          else
            {
              if(kill_list_ptr->host)
                host = kill_list_ptr->host;
              else
                host = "*";

              if(kill_list_ptr->name)
                name = kill_list_ptr->name;
              else
                name = "*";

              if(kill_list_ptr->passwd)
                reason = kill_list_ptr->passwd;
              else
                reason = "No Reason";

              if(!IsAnOper(sptr))
                {
                  if( (p = strchr(reason,'|')) )
                    *p = '\0';

                  sendto_one(sptr,rpl_str(RPL_STATSKLINE), me.name,
                             sptr->name, 'k' , host, name, reason);
                  if(p)
                    *p = '|';
                }
              else
                sendto_one(sptr,rpl_str(RPL_STATSKLINE), me.name,
                           sptr->name, 'k' , host, name, reason);

              last_list_ptr = kill_list_ptr;
              kill_list_ptr = kill_list_ptr->next;
            }
        }
    }
}

/* get_oper_privs
 *
 * inputs        - default privs
 *               - privs as string
 * output        - default privs as modified by privs string
 * side effects -
 *
 * -Dianora
 */

int get_oper_privs(int int_privs,char *privs)
{
  Debug((DEBUG_DEBUG,"get_oper_privs called privs = [%s]",privs));

  while(*privs)
    {
      if(*privs == 'O')                     /* allow global kill */
        int_privs |= CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'o')                /* disallow global kill */
        int_privs &= ~CONF_OPER_GLOBAL_KILL;
      else if(*privs == 'U')                /* allow unkline */
        int_privs |= CONF_OPER_UNKLINE;
      else if(*privs == 'u')                /* disallow unkline */
        int_privs &= ~CONF_OPER_UNKLINE;
      else if(*privs == 'R')                /* allow remote squit/connect etc.*/
        int_privs |= CONF_OPER_REMOTE;        
      else if(*privs == 'r')                /* disallow remote squit/connect etc.*/
        int_privs &= ~CONF_OPER_REMOTE;
      else if(*privs == 'N')                /* allow +n see nick changes */
        int_privs |= CONF_OPER_N;
      else if(*privs == 'n')                /* disallow +n see nick changes */
        int_privs &= ~CONF_OPER_N;
      else if(*privs == 'K')                /* allow kill and kline privs */
        int_privs |= CONF_OPER_K;
      else if(*privs == 'k')                /* disallow kill and kline privs */
        int_privs &= ~CONF_OPER_K;
#ifdef GLINES
      else if(*privs == 'G')                /* allow gline */
        int_privs |= CONF_OPER_GLINE;
      else if(*privs == 'g')                /* disallow gline */
        int_privs &= ~CONF_OPER_GLINE;
#endif
      else if(*privs == 'H')                /* allow rehash */
        int_privs |= CONF_OPER_REHASH;
      else if(*privs == 'h')                /* disallow rehash */
        int_privs &= ~CONF_OPER_REHASH;
      else if(*privs == 'D')
        int_privs |= CONF_OPER_DIE;         /* allow die */
      else if(*privs == 'd')
        int_privs &= ~CONF_OPER_DIE;        /* disallow die */
      privs++;
    }

  Debug((DEBUG_DEBUG,"about to return int_privs %x",int_privs));
  return(int_privs);
}

/*
 * oper_privs
 *
 * inputs        - pointer to cptr or NULL
 * output        - pointer to static string showing oper privs
 * side effects        -
 * return as string, the oper privs as derived from port
 * also, set the oper privs if given cptr is non NULL
 */

char *oper_privs(aClient *cptr,int port)
{
  static char privs_out[16];
  char *privs_ptr;

  privs_ptr = privs_out;
  *privs_ptr = '\0';

  if(port & CONF_OPER_GLINE)
    {
      if(cptr)
        SetOperGline(cptr);
      *privs_ptr++ = 'G';
    }
  else
    *privs_ptr++ = 'g';

  if(port & CONF_OPER_K)
    {
      if(cptr)
        SetOperK(cptr);
    }
  else
    *privs_ptr++ = 'k';

  if(port & CONF_OPER_N)
    {
      if(cptr)
        SetOperN(cptr);
      *privs_ptr++ = 'N';
    }

  if(port & CONF_OPER_GLOBAL_KILL)
    {
      if(cptr)
        SetOperGlobalKill(cptr);
      *privs_ptr++ = 'O';
    }
  else
    *privs_ptr++ = 'o';

  if(port & CONF_OPER_REMOTE)
    {
      if(cptr)
        SetOperRemote(cptr);
      *privs_ptr++ = 'R';
    }
  else
    *privs_ptr++ = 'r';
  
  if(port & CONF_OPER_UNKLINE)
    {
      if(cptr)
        SetOperUnkline(cptr);
      *privs_ptr++ = 'U';
    }
  else
    *privs_ptr++ = 'u';

  if(port & CONF_OPER_REHASH)
    {
      if(cptr)
        SetOperRehash(cptr);
      *privs_ptr++ = 'H';
    }
  else
    *privs_ptr++ = 'h';

  if(port & CONF_OPER_DIE)
    {
      if(cptr)
        SetOperDie(cptr);
      *privs_ptr++ = 'D';
    }
  else
    *privs_ptr++ = 'd';

  *privs_ptr = '\0';

  return(privs_out);
}

/* get_oper_flags
 *
 * inputs        - flags as string
 * output        - flags
 * side effects -
 *
 * -Dianora
 */

int get_oper_flags(char *flags)
{
  int int_flags=0;

  Debug((DEBUG_DEBUG,"get_oper_flags called flags = [%s]",flags));

  while(*flags)
    {
      if(*flags == 'i')                        /* invisible */
        int_flags |= FLAGS_INVISIBLE;
      else if(*flags == 'w')                /* see wallops */
        int_flags |= FLAGS_WALLOP;
      else if(*flags == 's')
        int_flags |= FLAGS_SERVNOTICE;
      else if(*flags == 'c')
        int_flags |= FLAGS_CCONN;
      else if(*flags == 'r')
        int_flags |= FLAGS_REJ;
      else if(*flags == 'k')
        int_flags |= FLAGS_SKILL;
      else if(*flags == 'f')
        int_flags |= FLAGS_FULL;
      else if(*flags == 'y')
        int_flags |= FLAGS_SPY;
      else if(*flags == 'd')
        int_flags |= FLAGS_DEBUG;
      else if(*flags == 'n')
        int_flags |= FLAGS_NCHANGE;
      flags++;
    }

  Debug((DEBUG_DEBUG,"about to return int_flags %x",int_flags));
  return(int_flags);
}

/* oper_flags
 *
 * inputs        - flags as string
 * output        - flags
 * side effects -
 *
 * -Dianora
 */

char *oper_flags(int flags)
{
  static char flags_out[16];
  char *flags_ptr;

  flags_ptr = flags_out;
  *flags_ptr = '\0';

  Debug((DEBUG_DEBUG,"per_flags called flags = [%d]",flags));

  if(flags & FLAGS_INVISIBLE)
    *flags_ptr++ = 'i';
  if(flags & FLAGS_WALLOP)
    *flags_ptr++ = 'w';
  if(flags & FLAGS_SERVNOTICE)
    *flags_ptr++ = 's';
  if(flags & FLAGS_CCONN)
    *flags_ptr++ = 'c';
  if(flags & FLAGS_REJ)
    *flags_ptr++ = 'r';
  if(flags & FLAGS_SKILL)
    *flags_ptr++ = 'k';
  if(flags & FLAGS_FULL)
    *flags_ptr++ = 'f';
  if(flags & FLAGS_SPY)
    *flags_ptr++ = 'y';
  if(flags & FLAGS_DEBUG)
    *flags_ptr++ = 'd';
  if(flags & FLAGS_NCHANGE)
    *flags_ptr++ = 'n';
  *flags_ptr = '\0';

  Debug((DEBUG_DEBUG,"about to return flags_out %s",flags_out));
  return(flags_out);
}

/*
 * is_address
 *
 * inputs        - hostname
 *                - pointer to ip result
 *                - pointer to ip_mask result
 * output        - YES if hostname is ip# only NO if its not
 *              - 
 * side effects        - NONE
 * 
 * Thanks Soleil
 *
 * BUGS
 */
unsigned long cidr_to_bitmask[]=
{
  /* 00 */ 0x00000000,
  /* 01 */ 0x80000000,
  /* 02 */ 0xC0000000,
  /* 03 */ 0xE0000000,
  /* 04 */ 0xF0000000,
  /* 05 */ 0xF8000000,
  /* 06 */ 0xFC000000,
  /* 07 */ 0xFE000000,
  /* 08 */ 0xFF000000,
  /* 09 */ 0xFF800000,
  /* 10 */ 0xFFC00000,
  /* 11 */ 0xFFE00000,
  /* 12 */ 0xFFF00000,
  /* 13 */ 0xFFF80000,
  /* 14 */ 0xFFFC0000,
  /* 15 */ 0xFFFE0000,
  /* 16 */ 0xFFFF0000,
  /* 17 */ 0xFFFF8000,
  /* 18 */ 0xFFFFC000,
  /* 19 */ 0xFFFFE000,
  /* 20 */ 0xFFFFF000,
  /* 21 */ 0xFFFFF800,
  /* 22 */ 0xFFFFFC00,
  /* 23 */ 0xFFFFFE00,
  /* 24 */ 0xFFFFFF00,
  /* 25 */ 0xFFFFFF80,
  /* 26 */ 0xFFFFFFC0,
  /* 27 */ 0xFFFFFFE0,
  /* 28 */ 0xFFFFFFF0,
  /* 29 */ 0xFFFFFFF8,
  /* 30 */ 0xFFFFFFFC,
  /* 31 */ 0xFFFFFFFE,
  /* 32 */ 0xFFFFFFFF
};

int        is_address(char *host,
                   unsigned long *ip_ptr,
                   unsigned long *ip_mask_ptr)
{
  unsigned long current_ip=0L;
  unsigned int octet=0;
  int found_mask=0;
  int dot_count=0;
  char c;

  while( (c = *host) )
    {
      if(isdigit(c))
        {
          octet *= 10;
          octet += (*host & 0xF);
        }
      else if(c == '.')
        {
          current_ip <<= 8;
          current_ip += octet;
          if( octet > 255 )
            return( 0 );
          octet = 0;
          dot_count++;
        }
      else if(c == '/')
        {
          if( octet > 255 )
            return( 0 );
          found_mask = 1;
          current_ip <<= 8;
          current_ip += octet;
          octet = 0;
          *ip_ptr = current_ip;
          current_ip = 0L;
        }
      else if(c == '*')
        {
          if( (dot_count == 3) && (*(host+1) == '\0') && (*(host-1) == '.'))
            {
              current_ip <<= 8;
              *ip_ptr = current_ip;
              *ip_mask_ptr = 0xFFFFFF00L;
              return( 1 );
            }
          else
            return( 0 );
        }
      else
        return( 0 );
      host++;
    }

  current_ip <<= 8;
  current_ip += octet;

  if(found_mask)
    {
      if(current_ip>32)
        return( 0 );
      *ip_mask_ptr = cidr_to_bitmask[current_ip];
    }
  else
    {
      *ip_ptr = current_ip;
      *ip_mask_ptr = 0xFFFFFFFFL;
    }

  return( 1 );
}

#ifdef KILL_COMMENT_IS_FILE
/*
**  output the reason for being k lined from a file
** sptr is server
** parv is the sender prefix
** filename is the file that is to be output to the K lined client
*/
int     m_killcomment(sptr, parv, filename)
aClient *sptr;
char    *parv, *filename;
{
  FBFILE* file;
  char    line[256];
  char    *tmp;
  struct  stat        sb;
  struct  tm        *tm;

  if ((file = fbopen(filename, "r")) == NULL)
    {
      sendto_one(sptr, ":%s %d %s :You are not welcome on this server.", me.name, ERR_YOUREBANNEDCREEP, parv);
      return 0;
    }
  (void)fbstat(&sb, file);
  tm = localtime(&sb.st_mtime);
  while (fbgets(line, sizeof(line), file))
    {
      if ((tmp = strchr(line,'\n')))
        *tmp = '\0';
      sendto_one(sptr, ":%s %d %s :%s.", me.name, ERR_YOUREBANNEDCREEP, parv,line);
    }
  fbclose(file);
  return 0;
}
#endif /* KILL_COMMENT_IS_FILE */


/*
 * command to test I/K lines on server
 *
 * /quote testline user@host,ip
 *
 */

int m_testline(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
  aConfItem *aconf;
  unsigned long ip;
  unsigned long host_mask;
  char *host, *pass, *user, *name, *given_host, *given_name, *p;
  int port;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      given_name = parv[1];
      if(!(p = strchr(given_name,'@')))
        {
          sendto_one(sptr, ":%s NOTICE %s :usage: user@host[,ip]",
                     me.name, parv[0]);
          return 0;
        }

      *p = '\0';
      p++;
      given_host = p;
      ip = 0L;
      (void)is_address(given_host,&ip,&host_mask);

      aconf = find_matching_mtrie_conf(given_host,given_name,(unsigned long)ip);

      if(aconf)
        {
          GetPrintableaConfItem(aconf, &name, &host, &pass, &user, &port);
      
          if(aconf->status & CONF_KILL) 
            {
              sendto_one(sptr, 
                         ":%s NOTICE %s :K-line name [%s] host [%s] pass [%s]",
                         me.name, parv[0], 
                         user,
                         host,
                         pass);
            }
          else if(aconf->status & CONF_CLIENT)
            {
              sendto_one(sptr,
":%s NOTICE %s :I-line mask [%s] prefix [%s] name [%s] host [%s] port [%d] class [%d]",
                         me.name, parv[0], 
                         name,
                         show_iline_prefix(sptr,aconf,user),
                         user,
                         host,
                         port,
                         get_conf_class(aconf));

              aconf = find_tkline(given_host,given_name);
              if(aconf)
                {
                  sendto_one(sptr, 
                     ":%s NOTICE %s :k-line name [%s] host [%s] pass [%s]",
                             me.name, parv[0], 
                             aconf->user,
                             aconf->host,
                             aconf->passwd);
                }
            }
        }
      else
        sendto_one(sptr, ":%s NOTICE %s :No aconf found",
                   me.name, parv[0]);
    }
  else
    sendto_one(sptr, ":%s NOTICE %s :usage: user@host,ip",
               me.name, parv[0]);
  return 0;
}


/*
 * GetPrintableaConfItem
 *
 * inputs        - aConfItem
 *
 * output        - name 
 *                - host
 *                 - pass
 *                - user
 *                - port
 *
 * side effects        -
 * Examine the struct aConfItem, setting the values
 * of name, host, pass, user to values either
 * in aconf, or "<NULL>" port is set to aconf->port in all cases.
 */

void GetPrintableaConfItem(aConfItem *aconf, char **name, char **host,
                           char **pass, char **user,int *port)
{
  static  char        null[] = "<NULL>";

  *name = BadPtr(aconf->name) ? null : aconf->name;
  *host = BadPtr(aconf->host) ? null : aconf->host;
  *pass = BadPtr(aconf->passwd) ? null : aconf->passwd;
  *user = BadPtr(aconf->user) ? null : aconf->user;
  *port = (int)aconf->port;
}


/*
 * read_conf_files
 */

void read_conf_files(int cold)
{
  FBFILE* file = 0;      /* initconf */

  if ((file = openconf(ConfigFileEntry.configfile)) == 0)
    {
      if(cold)
	{
	  Debug((DEBUG_FATAL, "Failed in reading configuration file %s",
		 ConfigFileEntry.configfile));
	  (void)printf("Couldn't open configuration file %s\n",
		       ConfigFileEntry.configfile);
	  exit(-1);
	}
      else
	{
	  sendto_ops("Can't open %s file aborting rehash!",
		     ConfigFileEntry.configfile);
	  return;
	}
    }

  if(!cold)
    clear_out_old_conf();

  initconf(file, YES);

  do_include_conf();

/* comstuds SEPARATE_QUOTE_KLINES_BY_DATE code */
#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  {
    struct tm *tmptr;
    char timebuffer[20], filename[200];

    tmptr = localtime(&NOW);
    (void)strftime(timebuffer, 20, "%Y%m%d", tmptr);
    ircsprintf(filename, "%s.%s", ConfigFileEntry.klinefile, timebuffer);
    if ((file = openconf(filename)) == 0)
      {
	if(cold)
	  {
	    Debug((DEBUG_ERROR,"Failed reading kline file %s",
		   filename));
	    (void)printf("Couldn't open kline file %s\n",
			 filename);
	  }
	else
	  {
	    sendto_ops("Can't open %s file klines could be missing!",
		       filename);
	  }
      }
    else
      initconf(file, NO);
  }
#else
#ifdef KPATH
  if ((file = openconf(ConfigFileEntry.klinefile)) == 0)
    {
      if(cold)
	{
	  Debug((DEBUG_ERROR,"Failed reading kline file %s",
		 ConfigFileEntry.klinefile));
	  (void)printf("Couldn't open kline file %s\n",
		       ConfigFileEntry.klinefile);
	}
      else
	{
	  sendto_ops("Can't open %s file klines could be missing!",
		     ConfigFileEntry.klinefile);
	}
    }
  else
    initconf(file, NO);
#endif
#endif
}



static void clear_out_old_conf(void)
{
  aConfItem **tmp = &ConfigItemList;
  aConfItem *tmp2;
  aClass    *cltmp;

    /* Shadowfax's LOCKFILE code */
#ifdef LOCKFILE
    do_pending_klines();
#endif

    while ((tmp2 = *tmp))
      {
	if (tmp2->clients || tmp2->status & CONF_LISTEN_PORT)
	  {
	    /*
	    ** Configuration entry is still in use by some
	    ** local clients, cannot delete it--mark it so
	    ** that it will be deleted when the last client
	    ** exits...
	    */
	    if (!(tmp2->status & (CONF_LISTEN_PORT|CONF_CLIENT)))
	      {
		*tmp = tmp2->next;
		tmp2->next = NULL;
	      }
	    else
	      tmp = &tmp2->next;
	    tmp2->status |= CONF_ILLEGAL;
	  }
	else
	  {
	    *tmp = tmp2->next;
	    free_conf(tmp2);
	  }
      }

    /*
     * We don't delete the class table, rather mark all entries
     * for deletion. The table is cleaned up by check_class. - avalon
     */
    assert(0 != ClassList);
    for (cltmp = ClassList->next; cltmp; cltmp = cltmp->next)
      MaxLinks(cltmp) = -1;

    clear_mtrie_conf_links();

    zap_Dlines();
    clear_special_conf(&x_conf);
    clear_special_conf(&u_conf);
    clear_q_lines();
}

static void flush_deleted_I_P(void)
{
  aConfItem **tmp = &ConfigItemList;
  aConfItem *tmp2;

  /*
   * flush out deleted I and P lines although still in use.
   */
  for (tmp = &ConfigItemList; (tmp2 = *tmp); )
    {
      if (!(tmp2->status & CONF_ILLEGAL))
	tmp = &tmp2->next;
      else
	{
	  *tmp = tmp2->next;
	  tmp2->next = NULL;
	  if (!tmp2->clients)
	    free_conf(tmp2);
	}
    }
}
