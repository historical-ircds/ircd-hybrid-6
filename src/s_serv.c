/************************************************************************
 *   IRC - Internet Relay Chat, src/s_serv.c
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
#include "s_serv.h"
#include "channel.h"
#include "class.h"
#include "client.h"
#include "common.h"
#include "dline_conf.h"
#include "fdlist.h"
#include "fileio.h"
#include "hash.h"
#include "irc_string.h"
#include "ircd.h"
#include "list.h"
#include "listener.h"
#include "m_gline.h"
#include "msg.h"      /* msgtab */
#include "mtrie_conf.h"
#include "numeric.h"
#include "parse.h"
#include "res.h"
#include "restart.h"
#include "struct.h"
#include "s_bsd.h"
#include "s_conf.h"
#include "s_debug.h"
#include "s_misc.h"
#include "s_user.h"
#include "s_zip.h"
#include "scache.h"
#include "send.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#define MIN_CONN_FREQ 300

int MaxConnectionCount = 1;
int MaxClientCount     = 1;

/* Local function prototypes */
static void set_autoconn(struct Client *,char *,char *,int);
static int  m_set_parser(char *);

/*
 * list of recognized server capabilities.  "TS" is not on the list
 * because all servers that we talk to already do TS, and the kludged
 * extra argument to "PASS" takes care of checking that.  -orabidoo
 */
struct Capability captab[] = {
/*  name        cap     */ 
#ifdef ZIP_LINKS
  { "ZIP",      CAP_ZIP },
#endif
  { "QS",       CAP_QS },
  { "EX",       CAP_EX },
  { "CHW",      CAP_CHW },
  { "DE",       CAP_DE },
  { 0,   0 }
};


/*
 * my_name_for_link - return wildcard name of my server name 
 * according to given config entry --Jto
 * XXX - this is only called with me.name as name
 */
const char* my_name_for_link(const char* name, aConfItem* aconf)
{
  static char          namebuf[HOSTLEN + 1];
  register int         count = aconf->port;
  register const char* start = name;

  if (count <= 0 || count > 5)
    return start;

  while (count-- && name)
    {
      name++;
      name = strchr(name, '.');
    }
  if (!name)
    return start;

  namebuf[0] = '*';
  strncpy_irc(&namebuf[1], name, HOSTLEN - 1);
  namebuf[HOSTLEN] = '\0';
  return namebuf;
}

/*
 * hunt_server - Do the basic thing in delivering the message (command)
 *      across the relays to the specific server (server) for
 *      actions.
 *
 *      Note:   The command is a format string and *MUST* be
 *              of prefixed style (e.g. ":%s COMMAND %s ...").
 *              Command can have only max 8 parameters.
 *
 *      server  parv[server] is the parameter identifying the
 *              target server.
 *
 *      *WARNING*
 *              parv[server] is replaced with the pointer to the
 *              real servername from the matched client (I'm lazy
 *              now --msa).
 *
 *      returns: (see #defines)
 */
int hunt_server(struct Client *cptr, struct Client *sptr, char *command,
                int server, int parc, char *parv[])
{
  struct Client *acptr;
  int wilds;

  /*
   * Assume it's me, if no server
   */
  if (parc <= server || BadPtr(parv[server]) ||
      match(me.name, parv[server]) ||
      match(parv[server], me.name))
    return (HUNTED_ISME);
  /*
   * These are to pickup matches that would cause the following
   * message to go in the wrong direction while doing quick fast
   * non-matching lookups.
   */
  if ((acptr = find_client(parv[server], NULL)))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;
  if (!acptr && (acptr = find_server(parv[server])))
    if (acptr->from == sptr->from && !MyConnect(acptr))
      acptr = NULL;

  collapse(parv[server]);
  wilds = (strchr(parv[server], '?') || strchr(parv[server], '*'));

  /*
   * Again, if there are no wild cards involved in the server
   * name, use the hash lookup
   * - Dianora
   */
  if (!acptr)
    {
      if (!wilds)
        {
          if (!(acptr = find_server(parv[server])))
            {
              sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name,
                         parv[0], parv[server]);
              return(HUNTED_NOSUCH);
            }
        }
      else
        {
          for (acptr = GlobalClientList;
               (acptr = next_client(acptr, parv[server]));
               acptr = acptr->next)
            {
              if (acptr->from == sptr->from && !MyConnect(acptr))
                continue;
              /*
               * Fix to prevent looping in case the parameter for
               * some reason happens to match someone from the from
               * link --jto
               */
              if (IsRegistered(acptr) && (acptr != cptr))
                break;
            }
        }
    }

  if (acptr)
    {
      if (IsMe(acptr) || MyClient(acptr))
        return HUNTED_ISME;
      if (!match(acptr->name, parv[server]))
        parv[server] = acptr->name;
      sendto_one(acptr, command, parv[0],
                 parv[1], parv[2], parv[3], parv[4],
                 parv[5], parv[6], parv[7], parv[8]);
      return(HUNTED_PASS);
    } 
  sendto_one(sptr, form_str(ERR_NOSUCHSERVER), me.name,
             parv[0], parv[server]);
  return(HUNTED_NOSUCH);
}

/*
 * try_connections - scan through configuration and try new connections.
 * Returns the calendar time when the next call to this
 * function should be made latest. (No harm done if this
 * is called earlier or later...)
 */
time_t try_connections(time_t currenttime)
{
  struct ConfItem*   aconf;
  struct Client*     cptr;
  int                connecting = FALSE;
  int                confrq;
  time_t             next = 0;
  struct Class*      cltmp;
  struct ConfItem*   con_conf = NULL;
  int                con_class = 0;

  Debug((DEBUG_NOTICE,"Connection check at: %s", myctime(currenttime)));

  for (aconf = ConfigItemList; aconf; aconf = aconf->next )
    {
      /*
       * Also when already connecting! (update holdtimes) --SRB 
       */
      if (!(aconf->status & CONF_CONNECT_SERVER) || aconf->port <= 0)
        continue;
      cltmp = ClassPtr(aconf);
      /*
       * Skip this entry if the use of it is still on hold until
       * future. Otherwise handle this entry (and set it on hold
       * until next time). Will reset only hold times, if already
       * made one successfull connection... [this algorithm is
       * a bit fuzzy... -- msa >;) ]
       */
      if (aconf->hold > currenttime)
        {
          if (next > aconf->hold || next == 0)
            next = aconf->hold;
          continue;
        }

      if ((confrq = get_con_freq(cltmp)) < MIN_CONN_FREQ )
        confrq = MIN_CONN_FREQ;

      aconf->hold = currenttime + confrq;
      /*
       * Found a CONNECT config with port specified, scan clients
       * and see if this server is already connected?
       */
      cptr = find_server(aconf->name);
      
      if (!cptr && (Links(cltmp) < MaxLinks(cltmp)) &&
          (!connecting || (ClassType(cltmp) > con_class)))
        {
          con_class = ClassType(cltmp);
          con_conf = aconf;
          /* We connect only one at time... */
          connecting = TRUE;
        }
      if ((next > aconf->hold) || (next == 0))
        next = aconf->hold;
    }

  if (0 == GlobalSetOptions.autoconn)
    {
      /*
       * auto connects disabled, send message to ops and bail
       */
      if (connecting)
        sendto_ops("Connection to %s[%s] not activated.",
                 con_conf->name, con_conf->host);
      sendto_ops("WARNING AUTOCONN is 0, autoconns are disabled");
      Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
      return next;
    }

  if (connecting)
    {
#if 0
      /*
       * XXX - wheee modify the list as we traverse it
       * pointless, put the current one at the end of the list so we
       * spin through it again?
       */
      if (con_conf->next)  /* are we already last? */
        {
          struct ConfItem**  pconf;
          for (pconf = &ConfigItemList; (aconf = *pconf);
               pconf = &(aconf->next))
            /* 
             * put the current one at the end and
             * make sure we try all connections
             */
            if (aconf == con_conf)
              *pconf = aconf->next;
          (*pconf = con_conf)->next = 0;
        }
#endif
      if (!(con_conf->flags & CONF_FLAGS_ALLOW_AUTO_CONN))
        {
          sendto_ops("Connection to %s[%s] not activated, autoconn is off.",
                     con_conf->name, con_conf->host);
          sendto_ops("WARNING AUTOCONN on %s[%s] is disabled",
                     con_conf->name, con_conf->host);
        }
      else
        {
          if (connect_server(con_conf, 0, 0))
            sendto_ops("Connection to %s[%s] activated.",
                       con_conf->name, con_conf->host);
        }
    }
  Debug((DEBUG_NOTICE,"Next connection check : %s", myctime(next)));
  return next;
}

/*
 * check_server - check access for a server given its name 
 * (passed in cptr struct). Must check for all C/N lines which have a 
 * name which matches the name given and a host which matches. A host 
 * alias which is the same as the server name is also acceptable in the 
 * host field of a C/N line.
 *  
 *  0 = Access denied
 *  1 = Success
 */
int check_server(struct Client* cptr)
{
  struct SLink*    lp;
  struct ConfItem* c_conf = 0;
  struct ConfItem* n_conf = 0;

  assert(0 != cptr);

  if (attach_confs(cptr, cptr->name, 
                   CONF_CONNECT_SERVER | CONF_NOCONNECT_SERVER ) < 2)
    {
      Debug((DEBUG_DNS,"No C/N lines for %s", cptr->name));
      return 0;
    }
#if 0
  if (cptr->dns_reply)
    {
      int             i;
      struct hostent* hp   = cptr->dns_reply->hp;
      char*           name = hp->h_name;
      /*
       * if we are missing a C or N line from above, search for
       * it under all known hostnames we have for this ip#.
       */
      for (i = 0, name = hp->h_name; name; name = hp->h_aliases[i++])
        {
          if (!c_conf)
            c_conf = find_conf_host(lp, name, CONF_CONNECT_SERVER );
          if (!n_conf)
            n_conf = find_conf_host(lp, name, CONF_NOCONNECT_SERVER );
          if (c_conf && n_conf)
            {
              strncpy_irc(cptr->host, name, HOSTLEN);
              break;
            }
        }
      for (i = 0; hp->h_addr_list[i]; ++i)
        {
          if (!c_conf)
            c_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_CONNECT_SERVER);
          if (!n_conf)
            n_conf = find_conf_ip(lp, hp->h_addr_list[i],
                                  cptr->username, CONF_NOCONNECT_SERVER);
        }
    }
#endif
  lp = cptr->confs;
  /*
   * Check for C and N lines with the hostname portion the ip number
   * of the host the server runs on. This also checks the case where
   * there is a server connecting from 'localhost'.
   */
  if (!c_conf)
    c_conf = find_conf_host(lp, cptr->host, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_host(lp, cptr->host, CONF_NOCONNECT_SERVER);
  /*
   * Attach by IP# only if all other checks have failed.
   * It is quite possible to get here with the strange things that can
   * happen when using DNS in the way the irc server does. -avalon
   */
  if (!c_conf)
    c_conf = find_conf_ip(lp, (char*)& cptr->ip,
                          cptr->username, CONF_CONNECT_SERVER);
  if (!n_conf)
    n_conf = find_conf_ip(lp, (char*)& cptr->ip,
                          cptr->username, CONF_NOCONNECT_SERVER);
  /*
   * detach all conf lines that got attached by attach_confs()
   */
  det_confs_butmask(cptr, 0);
  /*
   * if no C or no N lines, then deny access
   */
  if (!c_conf || !n_conf)
    {
      Debug((DEBUG_DNS, "sv_cl: access denied: %s[%s@%s] c %x n %x",
             name, cptr->name, cptr->host, c_conf, n_conf));
      return 0;
    }
  /*
   * attach the C and N lines to the client structure for later use.
   */
  attach_conf(cptr, n_conf);
  attach_conf(cptr, c_conf);
  attach_confs(cptr, cptr->name, CONF_HUB | CONF_LEAF);
  /*
   * if the C:line doesn't have an IP address assigned put the one from
   * the client socket there
   */ 
  if (INADDR_NONE == c_conf->ipnum.s_addr)
    c_conf->ipnum.s_addr = cptr->ip.s_addr;

  Debug((DEBUG_DNS,"sv_cl: access ok: %s[%s]", name, cptr->host));

  return 1;
}

/*
** m_functions execute protocol messages on this server:
**
**      cptr    is always NON-NULL, pointing to a *LOCAL* client
**              structure (with an open socket connected!). This
**              identifies the physical socket where the message
**              originated (or which caused the m_function to be
**              executed--some m_functions may call others...).
**
**      sptr    is the source of the message, defined by the
**              prefix part of the message if present. If not
**              or prefix not found, then sptr==cptr.
**
**              (!IsServer(cptr)) => (cptr == sptr), because
**              prefixes are taken *only* from servers...
**
**              (IsServer(cptr))
**                      (sptr == cptr) => the message didn't
**                      have the prefix.
**
**                      (sptr != cptr && IsServer(sptr) means
**                      the prefix specified servername. (?)
**
**                      (sptr != cptr && !IsServer(sptr) means
**                      that message originated from a remote
**                      user (not local).
**
**              combining
**
**              (!IsServer(sptr)) means that, sptr can safely
**              taken as defining the target structure of the
**              message in this server.
**
**      *Always* true (if 'parse' and others are working correct):
**
**      1)      sptr->from == cptr  (note: cptr->from == cptr)
**
**      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
**              *cannot* be a local connection, unless it's
**              actually cptr!). [MyConnect(x) should probably
**              be defined as (x == x->from) --msa ]
**
**      parc    number of variable parameter strings (if zero,
**              parv is allowed to be NULL)
**
**      parv    a NULL terminated list of parameter pointers,
**
**                      parv[0], sender (prefix string), if not present
**                              this points to an empty string.
**                      parv[1]...parv[parc-1]
**                              pointers to additional parameters
**                      parv[parc] == NULL, *always*
**
**              note:   it is guaranteed that parv[0]..parv[parc-1] are all
**                      non-NULL pointers.
*/

/*
** send the CAPAB line to a server  -orabidoo
*
* modified, always send all capabilities -Dianora
*/
void send_capabilities(struct Client* cptr, int use_zip)
{
  struct Capability* cap;
  char  msgbuf[BUFSIZE];

  msgbuf[0] = '\0';

  for (cap = captab; cap->name; ++cap)
    {
      /* kludge to rhyme with sludge */

      if (use_zip)
        {
          strcat(msgbuf, cap->name);
          strcat(msgbuf, " ");
        }
      else
        {
          if(cap->cap != CAP_ZIP)
            {
              strcat(msgbuf, cap->name);
              strcat(msgbuf, " ");
            }
        }
    }
  sendto_one(cptr, "CAPAB :%s", msgbuf);
}


static void sendnick_TS(struct Client *cptr, struct Client *acptr)
{
  static char ubuf[12];

  if (IsPerson(acptr))
    {
      send_umode(NULL, acptr, 0, SEND_UMODES, ubuf);
      if (!*ubuf)
        { /* trivial optimization - Dianora */
          
          ubuf[0] = '+';
          ubuf[1] = '\0';
          /*    original was    strcpy(ubuf, "+"); */
        }

      sendto_one(cptr, "NICK %s %d %lu %s %s %s %s :%s", acptr->name, 
                 acptr->hopcount + 1, acptr->tsinfo, ubuf,
                 acptr->username, acptr->host,
                 acptr->user->server, acptr->info);
    }
}


/*
 * show_capabilities - show current server capabilities
 *
 * inputs       - pointer to an aClient
 * output       - pointer to static string
 * side effects - build up string representing capabilities of server listed
 */

const char* show_capabilities(struct Client* acptr)
{
  static char        msgbuf[BUFSIZE];
  struct Capability* cap;

  strcpy(msgbuf,"TS ");
  if (!acptr->caps)        /* short circuit if no caps */
    return msgbuf;

  for (cap = captab; cap->cap; ++cap)
    {
      if(cap->cap & acptr->caps)
        {
          strcat(msgbuf, cap->name);
          strcat(msgbuf, " ");
        }
    }
  return msgbuf;
}

int server_estab(struct Client *cptr)
{
  struct Channel*   chptr;
  struct Client*    acptr;
  struct ConfItem*  n_conf;
  struct ConfItem*  c_conf;
  const char*       inpath;
  char*             host;
  char*             encr;
  int               split;

  assert(0 != cptr);
  ClearAccess(cptr);

  inpath = get_client_name(cptr, TRUE); /* "refresh" inpath with host */
  split = irccmp(cptr->name, cptr->host);
  host = cptr->name;

  if (!(n_conf = find_conf_name(cptr->confs, host, CONF_NOCONNECT_SERVER)))
    {
      ircstp->is_ref++;
      sendto_one(cptr,
                 "ERROR :Access denied. No N line for server %s", inpath);
      sendto_ops("Access denied. No N line for server %s", inpath);
      return exit_client(cptr, cptr, cptr, "No N line for server");
    }
  if (!(c_conf = find_conf_name(cptr->confs, host, CONF_CONNECT_SERVER )))
    {
      ircstp->is_ref++;
      sendto_one(cptr, "ERROR :Only N (no C) field for server %s", inpath);
      sendto_ops("Only N (no C) field for server %s",inpath);
      return exit_client(cptr, cptr, cptr, "No C line for server");
    }

#ifdef CRYPT_LINK_PASSWORD
  /* use first two chars of the password they send in as salt */

  /* passwd may be NULL. Head it off at the pass... */
  if(*cptr->passwd && *n_conf->passwd)
    {
      extern  char *crypt();
      encr = crypt(cptr->passwd, n_conf->passwd);
    }
  else
    encr = "";
#else
  encr = cptr->passwd;
#endif  /* CRYPT_LINK_PASSWORD */
  if (*n_conf->passwd && 0 != strcmp(n_conf->passwd, encr))
    {
      ircstp->is_ref++;
      sendto_one(cptr, "ERROR :No Access (passwd mismatch) %s",
                 inpath);
      sendto_ops("Access denied (passwd mismatch) %s", inpath);
      return exit_client(cptr, cptr, cptr, "Bad Password");
    }
  memset((void *)cptr->passwd, 0,sizeof(cptr->passwd));

  /* Its got identd , since its a server */
  SetGotId(cptr);

#ifndef HUB
  /* Its easy now, if there is a server in my link list
   * and I'm not a HUB, I can't grow the linklist more than 1
   *
   * -Dianora
   */
  if (serv_cptr_list)   
    {
      ircstp->is_ref++;
      sendto_one(cptr, "ERROR :I'm a leaf not a hub");
      return exit_client(cptr, cptr, cptr, "I'm a leaf");
    }
#endif
  if (IsUnknown(cptr))
    {
      if (c_conf->passwd[0])
        sendto_one(cptr,"PASS %s :TS", c_conf->passwd);
      /*
      ** Pass my info to the new server
      */

      send_capabilities(cptr,(c_conf->flags & CONF_FLAGS_ZIP_LINK));
      sendto_one(cptr, "SERVER %s 1 :%s",
                 my_name_for_link(me.name, n_conf), 
                 (me.info[0]) ? (me.info) : "IRCers United");
    }
  else
    {
      Debug((DEBUG_INFO, "Check Usernames [%s]vs[%s]",
             n_conf->user, cptr->username));
      if (!match(n_conf->user, cptr->username))
        {
          ircstp->is_ref++;
          sendto_ops("Username mismatch [%s]v[%s] : %s",
                     n_conf->user, cptr->username,
                     get_client_name(cptr, TRUE));
          sendto_one(cptr, "ERROR :No Username Match");
          return exit_client(cptr, cptr, cptr, "Bad User");
        }
    }
  

#ifdef ZIP_LINKS
  if (IsCapable(cptr, CAP_ZIP) && (c_conf->flags & CONF_FLAGS_ZIP_LINK))
    {
      if (zip_init(cptr) == -1)
        {
          zip_free(cptr);
          sendto_ops("Unable to setup compressed link for %s",
                      get_client_name(cptr, TRUE));
          return exit_client(cptr, cptr, &me, "zip_init() failed");
        }
      cptr->flags2 |= (FLAGS2_ZIP|FLAGS2_ZIPFIRST);
    }
  else
    ClearCap(cptr, CAP_ZIP);
#endif /* ZIP_LINKS */

  sendto_one(cptr,"SVINFO %d %d 0 :%lu", TS_CURRENT, TS_MIN, CurrentTime);
  
  det_confs_butmask(cptr, CONF_LEAF|CONF_HUB|CONF_NOCONNECT_SERVER);
  /*
  ** *WARNING*
  **    In the following code in place of plain server's
  **    name we send what is returned by get_client_name
  **    which may add the "sockhost" after the name. It's
  **    *very* *important* that there is a SPACE between
  **    the name and sockhost (if present). The receiving
  **    server will start the information field from this
  **    first blank and thus puts the sockhost into info.
  **    ...a bit tricky, but you have been warned, besides
  **    code is more neat this way...  --msa
  */
  SetServer(cptr);
  cptr->servptr = &me;
  add_client_to_llist(&(me.serv->servers), cptr);

  Count.server++;
  Count.myserver++;

  /*
   * XXX - this should be in s_bsd
   */
  if (!set_sock_buffers(cptr->fd, READBUF_SIZE))
    report_error(SETBUF_ERROR_MSG, get_client_name(cptr, TRUE), errno);

  /* LINKLIST */
  /* add to server link list -Dianora */
  cptr->next_server_client = serv_cptr_list;
  serv_cptr_list = cptr;

  fdlist_add(cptr->fd, FDL_SERVER | FDL_BUSY);

  nextping = CurrentTime;
  /* ircd-hybrid-6 can do TS links, and  zipped links*/
  sendto_ops("Link with %s established: (%s) link",
             inpath,show_capabilities(cptr));

  add_to_client_hash_table(cptr->name, cptr);
  /* doesnt duplicate cptr->serv if allocated this struct already */
  make_server(cptr);
  cptr->serv->up = me.name;
  /* add it to scache */
  find_or_add(cptr->name);
  
  cptr->serv->nline = n_conf;
  cptr->flags2 |= FLAGS2_CBURST;

  /*
  ** Old sendto_serv_but_one() call removed because we now
  ** need to send different names to different servers
  ** (domain name matching) Send new server to other servers.
  */
  for(acptr=serv_cptr_list;acptr;acptr=acptr->next_server_client)
    {
      if (acptr == cptr)
        continue;

      if ((n_conf = acptr->serv->nline) &&
          match(my_name_for_link(me.name, n_conf), cptr->name))
        continue;
      if (split)
        {
          /*
          sendto_one(acptr,":%s SERVER %s 2 :[%s] %s",
                   me.name, cptr->name,
                   cptr->host, cptr->info);
                   */

          /* DON'T give away the IP of the server here
           * if its a hub especially.
           */

          sendto_one(acptr,":%s SERVER %s 2 :%s",
                   me.name, cptr->name,
                   cptr->info);
        }
      else
        sendto_one(acptr,":%s SERVER %s 2 :%s",
                   me.name, cptr->name, cptr->info);
    }

  /*
  ** Pass on my client information to the new server
  **
  ** First, pass only servers (idea is that if the link gets
  ** cancelled beacause the server was already there,
  ** there are no NICK's to be cancelled...). Of course,
  ** if cancellation occurs, all this info is sent anyway,
  ** and I guess the link dies when a read is attempted...? --msa
  ** 
  ** Note: Link cancellation to occur at this point means
  ** that at least two servers from my fragment are building
  ** up connection this other fragment at the same time, it's
  ** a race condition, not the normal way of operation...
  **
  ** ALSO NOTE: using the get_client_name for server names--
  **    see previous *WARNING*!!! (Also, original inpath
  **    is destroyed...)
  */

  n_conf = cptr->serv->nline;
  for (acptr = &me; acptr; acptr = acptr->prev)
    {
      /* acptr->from == acptr for acptr == cptr */
      if (acptr->from == cptr)
        continue;
      if (IsServer(acptr))
        {
          if (match(my_name_for_link(me.name, n_conf), acptr->name))
            continue;
          split = (MyConnect(acptr) &&
                   irccmp(acptr->name, acptr->host));

          /* DON'T give away the IP of the server here
           * if its a hub especially.
           */

          if (split)
            sendto_one(cptr, ":%s SERVER %s %d :%s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1,
                       acptr->info);
            /*
            sendto_one(cptr, ":%s SERVER %s %d :[%s] %s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1,
                       acptr->host, acptr->info);
                       */
          else
            sendto_one(cptr, ":%s SERVER %s %d :%s",
                       acptr->serv->up, acptr->name,
                       acptr->hopcount+1, acptr->info);
        }
    }
  
 
      /*
      ** Send it in the shortened format with the TS, if
      ** it's a TS server; walk the list of channels, sending
      ** all the nicks that haven't been sent yet for each
      ** channel, then send the channel itself -- it's less
      ** obvious than sending all nicks first, but on the
      ** receiving side memory will be allocated more nicely
      ** saving a few seconds in the handling of a split
      ** -orabidoo
      */

  {
    struct SLink* l;
    static char   nickissent = 1;
      
    nickissent = 3 - nickissent;
    /* flag used for each nick to check if we've sent it
       yet - must be different each time and !=0, so we
       alternate between 1 and 2 -orabidoo
       */
    for (chptr = channel; chptr; chptr = chptr->nextch)
      {
        for (l = chptr->members; l; l = l->next)
          {
            acptr = l->value.cptr;
            if (acptr->nicksent != nickissent)
              {
                acptr->nicksent = nickissent;
                if (acptr->from != cptr)
                  sendnick_TS(cptr, acptr);
              }
          }
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
        /* don't send 0 user channels on rejoin (Mortiis)
         */
        if(chptr->users != 0)
#endif
          send_channel_modes(cptr, chptr);
      }
    /*
    ** also send out those that are not on any channel
    */
    for (acptr = &me; acptr; acptr = acptr->prev)
      if (acptr->nicksent != nickissent)
        {
          acptr->nicksent = nickissent;
          if (acptr->from != cptr)
            sendnick_TS(cptr, acptr);
        }
    }

  cptr->flags2 &= ~FLAGS2_CBURST;

#ifdef  ZIP_LINKS
  /*
  ** some stats about the connect burst,
  ** they are slightly incorrect because of cptr->zip->outbuf.
  */
  if ((cptr->flags2 & FLAGS2_ZIP) && cptr->zip->out->total_in)
    sendto_ops("Connect burst to %s: %lu, compressed: %lu (%3.1f%%)",
                get_client_name(cptr, TRUE),
                cptr->zip->out->total_in,cptr->zip->out->total_out,
                (100.0*(float)cptr->zip->out->total_out) /
                (float)cptr->zip->out->total_in);
#endif /* ZIP_LINKS */

  /* Always send a PING after connect burst is done */
  sendto_one(cptr, "PING :%s", me.name);

#ifdef NEED_SPLITCODE
#ifdef SPLIT_PONG
  if (server_was_split)
    got_server_pong = NO;
#endif /* SPLIT_PONG */
#endif /* NEED_SPLITCODE */

  return 0;
}


/*
** m_locops (write to *all* local opers currently online)
**      parv[0] = sender prefix
**      parv[1] = message text
*/
int     m_locops(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  char *message = NULL;
#ifdef SLAVE_SERVERS
  char *slave_oper;
  struct Client *acptr;
#endif

#ifdef SLAVE_SERVERS
  if(IsServer(sptr))
    {
      if(!find_special_conf(sptr->name,CONF_ULINE))
        {
          sendto_realops("received Unauthorized locops from %s",sptr->name);
          return 0;
        }

      if(parc > 2)
        {
          slave_oper = parv[1];

          parc--;
          parv++;

          if ((acptr = hash_find_client(slave_oper,(struct Client *)NULL)))
            {
              if(!IsPerson(acptr))
                return 0;
            }
          else
            return 0;

          if(parv[1])
            {
              message = parv[1];
              send_operwall(acptr, "SLOCOPS", message);
            }
          else
            return 0;
#ifdef HUB
          sendto_slaves(sptr,"LOCOPS",slave_oper,parc,parv);
#endif
          return 0;
        }
    }
  else
    {
      message = parc > 1 ? parv[1] : NULL;
    }
#else
  if(IsServer(sptr))
    return 0;
  message = parc > 1 ? parv[1] : NULL;
#endif

  if (BadPtr(message))
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "LOCOPS");
      return 0;
    }

  if(MyConnect(sptr) && IsAnOper(sptr))
    {

#ifdef SLAVE_SERVERS
      sendto_slaves(NULL,"LOCOPS",sptr->name,parc,parv);
#endif
      send_operwall(sptr, "LOCOPS", message);
    }
  else
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return(0);
    }

  return(0);
}

int     m_operwall(struct Client *cptr,
                   struct Client *sptr,
                   int parc,
                   char *parv[])
{
  char *message = parc > 1 ? parv[1] : NULL;


  if (check_registered_user(sptr))
    return 0;
  if (!IsAnOper(sptr) || IsServer(sptr))
    {
      if (MyClient(sptr) && !IsServer(sptr))
        sendto_one(sptr, form_str(ERR_NOPRIVILEGES),
                   me.name, parv[0]);
      return 0;
    }
  if (BadPtr(message))
    {
      if (MyClient(sptr))
        sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                   me.name, parv[0], "OPERWALL");
      return 0;
    }

#ifdef PACE_WALLOPS
  if( MyClient(sptr) && ((LastUsedWallops + WALLOPS_WAIT) > CurrentTime) )
    {
      sendto_one(sptr, ":%s NOTICE %s :Oh, one of those annoying opers who doesn't know how to use a channel",
                 me.name,parv[0]); 
      return 0;
    }
  LastUsedWallops = CurrentTime;
#endif

  sendto_serv_butone(IsServer(cptr) ? cptr : NULL, ":%s OPERWALL :%s",
                     parv[0], message);
  send_operwall(sptr, "OPERWALL", message);
  return 0;
}


/*
** m_time
**      parv[0] = sender prefix
**      parv[1] = servername
*/
int     m_time(struct Client *cptr,
               struct Client *sptr,
               int parc,
               char *parv[])
{
  if (hunt_server(cptr,sptr,":%s TIME :%s",1,parc,parv) == HUNTED_ISME)
    sendto_one(sptr, form_str(RPL_TIME), me.name,
               parv[0], me.name, date((long)0));
  return 0;
}

/*
** m_admin
**      parv[0] = sender prefix
**      parv[1] = servername
*/
int     m_admin(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct ConfItem *aconf;
  static time_t last_used=0L;

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > CurrentTime)
        {
          /* safe enough to give this on a local connect only */
          if(MyClient(sptr))
            sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
          return 0;
        }
      else
        last_used = CurrentTime;
    }

  if (hunt_server(cptr,sptr,":%s ADMIN :%s",1,parc,parv) != HUNTED_ISME)
    return 0;

  if (IsPerson(sptr))
    sendto_realops_flags(FLAGS_SPY,
                         "ADMIN requested by %s (%s@%s) [%s]", sptr->name,
                         sptr->username, sptr->host, sptr->user->server);
  if ((aconf = find_admin()))
    {
      sendto_one(sptr, form_str(RPL_ADMINME),
                 me.name, parv[0], me.name);
      sendto_one(sptr, form_str(RPL_ADMINLOC1),
                 me.name, parv[0], aconf->host);
      sendto_one(sptr, form_str(RPL_ADMINLOC2),
                 me.name, parv[0], aconf->passwd);
      sendto_one(sptr, form_str(RPL_ADMINEMAIL),
                 me.name, parv[0], aconf->user);
    }
  else
    sendto_one(sptr, form_str(ERR_NOADMININFO),
               me.name, parv[0], me.name);
  return 0;
}

/* Shadowfax's server side, anti flood code */

#ifdef FLUD
extern int flud_num;
extern int flud_time;
extern int flud_block;
#endif

#ifdef ANTI_SPAMBOT
extern int spam_num;
extern int spam_time;
#endif


/*
 * m_set_parser - find the correct int. to return 
 * so we can switch() it.
 * KEY:  0 - MAX
 *       1 - AUTOCONN
 *       2 - IDLETIME
 *       3 - FLUDNUM
 *       4 - FLUDTIME
 *       5 - FLUDBLOCK
 *       6 - DRONETIME
 *       7 - DRONECOUNT
 *       8 - SPLITDELAY
 *       9 - SPLITNUM
 *      10 - SPLITUSERS
 *      11 - SPAMNUM
 *      12 - SPAMTIME
 * - rjp
 */

#define TOKEN_MAX 0
#define TOKEN_AUTOCONN 1
#define TOKEN_IDLETIME 2
#define TOKEN_FLUDNUM 3
#define TOKEN_FLUDTIME 4
#define TOKEN_FLUDBLOCK 5
#define TOKEN_DRONETIME 6
#define TOKEN_DRONECOUNT 7
#define TOKEN_SPLITDELAY 8
#define TOKEN_SPLITNUM 9
#define TOKEN_SPLITUSERS 10
#define TOKEN_SPAMNUM 11
#define TOKEN_SPAMTIME 12
#define TOKEN_BAD 13

static char *set_token_table[] = {
  "MAX",
  "AUTOCONN",
  "IDLETIME",
  "FLUDNUM",
  "FLUDTIME",
  "FLUDBLOCK",
  "DRONETIME",
  "DRONECOUNT",
  "SPLITDELAY",
  "SPLITNUM",
  "SPLITUSERS",
  "SPAMNUM",
  "SPAMTIME",
  NULL
};

static int m_set_parser(char *parsethis)
{
  int i;

  for( i = 0; set_token_table[i]; i++ )
    {
      if(!irccmp(set_token_table[i],parsethis))
        return i;
    }
  return TOKEN_BAD;
}

/*
 * m_set - set options while running
 */
int   m_set(struct Client *cptr,
            struct Client *sptr,
            int parc,
            char *parv[])
{
  char *command;
  int cnum;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if (parc > 1)
    {
      command = parv[1];
      cnum = m_set_parser(command);
/* This strcasecmp crap is annoying.. a switch() would be better.. 
 * - rjp
 */
      switch(cnum)
        {
        case TOKEN_MAX:
          if (parc > 2)
            {
              int new_value = atoi(parv[2]);
              if (new_value > MASTER_MAX)
                {
                  sendto_one(sptr,
                             ":%s NOTICE %s :You cannot set MAXCLIENTS to > MASTER_MAX (%d)",
                             me.name, parv[0], MASTER_MAX);
                  return 0;
                }
              if (new_value < 32)
                {
                  sendto_one(sptr, ":%s NOTICE %s :You cannot set MAXCLIENTS to < 32 (%d:%d)",
                             me.name, parv[0], MAXCLIENTS, highest_fd);
                  return 0;
                }
              MAXCLIENTS = new_value;
              sendto_realops("%s!%s@%s set new MAXCLIENTS to %d (%d current)",
                             parv[0], sptr->username, sptr->host, MAXCLIENTS, Count.local);
              return 0;
            }
          sendto_one(sptr, ":%s NOTICE %s :Current Maxclients = %d (%d)",
                     me.name, parv[0], MAXCLIENTS, Count.local);
          return 0;
          break;

        case TOKEN_AUTOCONN:
          if(parc > 3)
            {
              int newval = atoi(parv[3]);

              if(!irccmp(parv[2],"ALL"))
                {
                  sendto_realops(
                                 "%s has changed AUTOCONN ALL to %i",
                                 parv[0], newval);
                  GlobalSetOptions.autoconn = newval;
                }
              else
                set_autoconn(sptr,parv[0],parv[2],newval);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :AUTOCONN ALL is currently %i",
                         me.name, parv[0], GlobalSetOptions.autoconn);
            }
          return 0;
          break;

#ifdef IDLE_CHECK
          case TOKEN_IDLETIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);
                if(newval == 0)
                  {
                    sendto_realops("%s has disabled IDLE_CHECK",
                                   parv[0]);
                    IDLETIME = 0;
                  }
                else
                  {
                    sendto_realops("%s has changed IDLETIME to %i",
                                   parv[0], newval);
                    IDLETIME = (newval*60);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :IDLETIME is currently %i",
                           me.name, parv[0], IDLETIME/60);
              }
            return 0;
            break;
#endif
#ifdef FLUD
          case TOKEN_FLUDNUM:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDNUM must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                FLUDNUM = newval;
                sendto_realops("%s has changed FLUDNUM to %i",
                               parv[0], FLUDNUM);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDNUM is currently %i",
                           me.name, parv[0], FLUDNUM);
              }
            return 0;
            break;

          case TOKEN_FLUDTIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval <= 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDTIME must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                FLUDTIME = newval;
                sendto_realops("%s has changed FLUDTIME to %i",
                               parv[0], FLUDTIME);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDTIME is currently %i",
                           me.name, parv[0], FLUDTIME);
              }
            return 0;       
            break;

          case TOKEN_FLUDBLOCK:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK must be >= 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                FLUDBLOCK = newval;
                if(FLUDBLOCK == 0)
                  {
                    sendto_realops("%s has disabled flud detection/protection",
                                   parv[0]);
                  }
                else
                  {
                    sendto_realops("%s has changed FLUDBLOCK to %i",
                                   parv[0],FLUDBLOCK);
                  }
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :FLUDBLOCK is currently %i",
                           me.name, parv[0], FLUDBLOCK);
              }
            return 0;       
            break;
#endif
#ifdef ANTI_DRONE_FLOOD
          case TOKEN_DRONETIME:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :DRONETIME must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }       
                DRONETIME = newval;
                if(DRONETIME == 0)
                  sendto_realops("%s has disabled the ANTI_DRONE_FLOOD code",
                                 parv[0]);
                else
                  sendto_realops("%s has changed DRONETIME to %i",
                                 parv[0], DRONETIME);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :DRONETIME is currently %i",
                           me.name, parv[0], DRONETIME);
              }
            return 0;
            break;

        case TOKEN_DRONECOUNT:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT must be > 0",
                             me.name, parv[0]);
                  return 0;
                }       
              DRONECOUNT = newval;
              sendto_realops("%s has changed DRONECOUNT to %i",
                             parv[0], DRONECOUNT);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :DRONECOUNT is currently %i",
                         me.name, parv[0], DRONECOUNT);
            }
          return 0;
          break;
#endif
#ifdef NEED_SPLITCODE

            case TOKEN_SPLITDELAY:
              if(parc > 2)
                {
                  int newval = atoi(parv[2]);
                  
                  if(newval < 0)
                    {
                      sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY must be > 0",
                                 me.name, parv[0]);
                      return 0;
                    }
                  /* sygma found it, the hard way */
                  if(newval > MAX_SERVER_SPLIT_RECOVERY_TIME)
                    {
                      sendto_one(sptr,
                                 ":%s NOTICE %s :Cannot set SPLITDELAY over %d",
                                 me.name, parv[0], MAX_SERVER_SPLIT_RECOVERY_TIME);
                      newval = MAX_SERVER_SPLIT_RECOVERY_TIME;
                    }
                  sendto_realops("%s has changed SPLITDELAY to %i",
                                 parv[0], newval);
                  SPLITDELAY = (newval*60);
                  if(SPLITDELAY == 0)
                    {
                      cold_start = NO;
                      if (server_was_split)
                        {
                          server_was_split = NO;
                          sendto_ops("split-mode deactived by manual override");
                        }
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
                      remove_empty_channels();
#endif
#if defined(SPLIT_PONG)
                      got_server_pong = YES;
#endif
                    }
                }
              else
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITDELAY is currently %i",
                             me.name,
                             parv[0],
                             SPLITDELAY/60);
                }
          return 0;
          break;

        case TOKEN_SPLITNUM:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval < SPLIT_SMALLNET_SIZE)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPLITNUM must be >= %d",
                             me.name, parv[0],SPLIT_SMALLNET_SIZE);
                  return 0;
                }
              sendto_realops("%s has changed SPLITNUM to %i",
                             parv[0], newval);
              SPLITNUM = newval;
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPLITNUM is currently %i",
                         me.name,
                         parv[0],
                         SPLITNUM);
            }
          return 0;
          break;

          case TOKEN_SPLITUSERS:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS must be >= 0",
                               me.name, parv[0]);
                    return 0;
                  }
                sendto_realops("%s has changed SPLITUSERS to %i",
                               parv[0], newval);
                SPLITUSERS = newval;
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPLITUSERS is currently %i",
                           me.name,
                           parv[0],
                           SPLITUSERS);
              }
            return 0;
            break;
#endif
#ifdef ANTI_SPAMBOT
          case TOKEN_SPAMNUM:
            if(parc > 2)
              {
                int newval = atoi(parv[2]);

                if(newval < 0)
                  {
                    sendto_one(sptr, ":%s NOTICE %s :SPAMNUM must be > 0",
                               me.name, parv[0]);
                    return 0;
                  }
                if(newval == 0)
                  {
                    sendto_realops("%s has disabled ANTI_SPAMBOT",
                                   parv[0]);
                    return 0;
                  }

                if(newval < MIN_SPAM_NUM)
                  SPAMNUM = MIN_SPAM_NUM;
                else
                  SPAMNUM = newval;
                sendto_realops("%s has changed SPAMNUM to %i",
                               parv[0], SPAMNUM);
              }
            else
              {
                sendto_one(sptr, ":%s NOTICE %s :SPAMNUM is currently %i",
                           me.name, parv[0], SPAMNUM);
              }

            return 0;
            break;

        case TOKEN_SPAMTIME:
          if(parc > 2)
            {
              int newval = atoi(parv[2]);

              if(newval <= 0)
                {
                  sendto_one(sptr, ":%s NOTICE %s :SPAMTIME must be > 0",
                             me.name, parv[0]);
                  return 0;
                }
              if(newval < MIN_SPAM_TIME)
                SPAMTIME = MIN_SPAM_TIME;
              else
                SPAMTIME = newval;
              sendto_realops("%s has changed SPAMTIME to %i",
                             parv[0], SPAMTIME);
            }
          else
            {
              sendto_one(sptr, ":%s NOTICE %s :SPAMTIME is currently %i",
                         me.name, parv[0], SPAMTIME);
            }
          return 0;
          break;
#endif
        default:
        case TOKEN_BAD:
          break;
        }
    }
  sendto_one(sptr, ":%s NOTICE %s :Options: MAX AUTOCONN",
             me.name, parv[0]);
#ifdef FLUD
  sendto_one(sptr, ":%s NOTICE %s :Options: FLUDNUM, FLUDTIME, FLUDBLOCK",
             me.name, parv[0]);
#endif
#ifdef ANTI_DRONE_FLOOD
  sendto_one(sptr, ":%s NOTICE %s :Options: DRONETIME, DRONECOUNT",
             me.name, parv[0]);
#endif
#ifdef ANTI_SPAMBOT
  sendto_one(sptr, ":%s NOTICE %s :Options: SPAMNUM, SPAMTIME",
             me.name, parv[0]);
#endif
#ifdef NEED_SPLITCODE
  sendto_one(sptr, ":%s NOTICE %s :Options: SPLITNUM SPLITUSERS SPLITDELAY",
               me.name, parv[0]);
#endif
#ifdef IDLE_CHECK
  sendto_one(sptr, ":%s NOTICE %s :Options: IDLETIME",
             me.name, parv[0]);
#endif
  return 0;
}

/*
 * m_htm - high traffic mode info
 */
int   m_htm(struct Client *cptr,
            struct Client *sptr,
            int parc,
            char *parv[])
{
#define LOADCFREQ 5
  char *command;

  extern int LRV, LCF;  /* in ircd.c */
  extern float currlife;
  
  if (!MyClient(sptr) || !IsOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  sendto_one(sptr,
        ":%s NOTICE %s :HTM is %s(%d), %s. Max rate = %dk/s. Current = %.1fk/s",
          me.name, parv[0], LIFESUX ? "ON" : "OFF", LIFESUX,
          NOISYHTM ? "NOISY" : "QUIET",
          LRV, currlife);
  if (parc > 1)
    {
      command = parv[1];
      if (!irccmp(command,"TO"))
        {
          if (parc > 2)
            {
              int new_value = atoi(parv[2]);
              if (new_value < 10)
                {
                  sendto_one(sptr, ":%s NOTICE %s :\002Cannot set LRV < 10!\002",
                             me.name, parv[0]);
                }
              else
                LRV = new_value;
              sendto_one(sptr, ":%s NOTICE %s :NEW Max rate = %dk/s. Current = %.1fk/s",
                         me.name, parv[0], LRV, currlife);
              sendto_realops("%s!%s@%s set new HTM rate to %dk/s (%.1fk/s current)",
                             parv[0], sptr->username, sptr->host,
                             LRV, currlife);
            }
          else 
            sendto_one(sptr, ":%s NOTICE %s :LRV command needs an integer parameter",me.name, parv[0]);
        }
      else
        {
          if (!irccmp(command,"ON"))
            {
              LIFESUX = 1;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now ON.", me.name, parv[0]);
              sendto_ops("Entering high-traffic mode: Forced by %s!%s@%s",
                         parv[0], sptr->username, sptr->host);
              LCF = 30; /* 30s */
            }
          else if (!irccmp(command,"OFF"))
            {
              LIFESUX = 0;
              LCF = LOADCFREQ;
              sendto_one(sptr, ":%s NOTICE %s :HTM is now OFF.", me.name, parv[0]);
              sendto_ops("Resuming standard operation: Forced by %s!%s@%s",
                         parv[0], sptr->username, sptr->host);
            }
          else if (!irccmp(command,"QUIET"))
            {
              sendto_ops("HTM is now QUIET");
              NOISYHTM = NO;
            }
          else if (!irccmp(command,"NOISY"))
            {
              sendto_ops("HTM is now NOISY");
              NOISYHTM = YES;
            }
          else
            sendto_one(sptr,
                       ":%s NOTICE %s :Commands are:HTM [ON] [OFF] [TO int] [QUIET] [NOISY]",
                       me.name, parv[0]);
        }
    }
  return 0;
}

/*
** m_rehash
**
*/
int m_rehash(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int found = NO;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( !IsOperRehash(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s: You have no H flag", me.name, parv[0]);
      return 0;
    }

  if(parc > 1)
    {
      if(irccmp(parv[1],"DNS") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "DNS");
#ifdef CUSTOM_ERR
          sendto_ops("%s is rehashing DNS while whistling innocently",
#else
          sendto_ops("%s is rehashing DNS",
#endif
                 parv[0]);
          restart_resolver();   /* re-read /etc/resolv.conf AGAIN?
                                   and close/re-open res socket */
          found = YES;
        }
      else if(irccmp(parv[1],"TKLINES") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "temp klines");
          flush_temp_klines();
#ifdef CUSTOM_ERR
          sendto_ops("%s is clearing temp klines while whistling innocently",
#else
          sendto_ops("%s is clearing temp klines",
#endif
                 parv[0]);
          found = YES;
        }
#ifdef GLINES
      else if(irccmp(parv[1],"GLINES") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "g-lines");
          flush_glines();
#ifdef CUSTOM_ERR
          sendto_ops("%s is clearing G-lines while whistling innocently",
#else
          sendto_ops("%s is clearing G-lines",
#endif
                 parv[0]);
          found = YES;
        }
#endif
      else if(irccmp(parv[1],"GC") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0], "garbage collecting");
          block_garbage_collect();
#ifdef CUSTOM_ERR
          sendto_ops("%s is garbage collecting while whistling innocently",
#else
          sendto_ops("%s is garbage collecting",
#endif
                 parv[0]);
          found = YES;
        }
      else if(irccmp(parv[1],"MOTD") == 0)
        {
          sendto_ops("%s is forcing re-reading of MOTD file",parv[0]);
          ReadMessageFile( &ConfigFileEntry.motd );
          found = YES;
        }
      else if(irccmp(parv[1],"OMOTD") == 0)
        {
          sendto_ops("%s is forcing re-reading of OPER MOTD file",parv[0]);
          ReadMessageFile( &ConfigFileEntry.motd );
          found = YES;
        }
      else if(irccmp(parv[1],"HELP") == 0)
        {
          sendto_ops("%s is forcing re-reading of oper help file",parv[0]);
          ReadMessageFile( &ConfigFileEntry.motd );
          found = YES;
        }
      else if(irccmp(parv[1],"dump") == 0)
        {
          sendto_ops("%s is dumping conf file",parv[0]);
          rehash_dump(sptr);
          found = YES;
        }
      else if(irccmp(parv[1],"dlines") == 0)
        {
          sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0],
                     ConfigFileEntry.configfile);
          /* this does a full rehash right now, so report it as such */
#ifdef CUSTOM_ERR
          sendto_ops("%s is rehashing dlines from server config file while whistling innocently",
#else
          sendto_ops("%s is rehashing dlines from server config file",
#endif
                     parv[0]);
#ifdef USE_SYSLOG
          syslog(LOG_NOTICE, "REHASH From %s\n", get_client_name(sptr, FALSE));
#endif
          dline_in_progress = 1;
          return rehash(cptr, sptr, 0);
        }
      if(found)
        {
#ifdef USE_SYSLOG
          syslog(LOG_NOTICE, "REHASH %s From %s\n",
                 parv[1],
                 get_client_name(sptr, FALSE));
#endif
          return 0;
        }
      else
        {
#undef OUT

#ifdef GLINES
#define OUT "rehash one of :DNS TKLINES GLINES GC MOTD OMOTD DUMP"
#else
#define OUT "rehash one of :DNS TKLINES GC MOTD OMOTD DUMP"
#endif
          sendto_one(sptr,":%s NOTICE %s : " OUT,me.name,sptr->name);
          return(0);
        }
    }
  else
    {
      sendto_one(sptr, form_str(RPL_REHASHING), me.name, parv[0],
                 ConfigFileEntry.configfile);
#ifdef CUSTOM_ERR
      sendto_ops("%s is rehashing server config file while whistling innocently",
#else
      sendto_ops("%s is rehashing server config file",
#endif
                 parv[0]);
#ifdef USE_SYSLOG
      syslog(LOG_NOTICE, "REHASH From %s\n", get_client_name(sptr, FALSE));
#endif
      return rehash(cptr, sptr, 0);
    }
  return 0; /* shouldn't ever get here */
}

/*
** m_restart
**
*/
int     m_restart(struct Client *cptr,
                  struct Client *sptr,
                  int parc,
                  char *parv[])
{
  char buf[BUFSIZE]; 
  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( !IsOperDie(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s: You have no D flag", me.name, parv[0]);
      return 0;
    }

#ifdef USE_SYSLOG
  syslog(LOG_WARNING, "Server RESTART by %s\n",
         get_client_name(sptr,FALSE));
#endif
  ircsprintf(buf, "Server RESTART by %s", get_client_name(sptr, TRUE));
  restart(buf);
  return 0; /*NOT REACHED*/
}

/*
** m_trace
**      parv[0] = sender prefix
**      parv[1] = servername
*/
int     m_trace(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  int   i;
  struct Client       *acptr = NULL;
  struct Class        *cltmp;
  char  *tname;
  int   doall, link_s[MAXCONNECTIONS], link_u[MAXCONNECTIONS];
  int   cnt = 0, wilds, dow;
#ifdef PACE_TRACE
  static time_t last_trace=0L;
  static time_t last_used=0L;
#endif
  static time_t now;

  now = time(NULL);  
  if (parc > 2)
    if (hunt_server(cptr, sptr, ":%s TRACE %s :%s",
                    2, parc, parv))
      return 0;
  
  if (parc > 1)
    tname = parv[1];
  else
    {
      tname = me.name;
    }

  switch (hunt_server(cptr, sptr, ":%s TRACE :%s", 1, parc, parv))
    {
    case HUNTED_PASS: /* note: gets here only if parv[1] exists */
      {
        struct Client *ac2ptr;
        
        ac2ptr = next_client_double(GlobalClientList, tname);
        if (ac2ptr)
          sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
                     version, debugmode, tname, ac2ptr->from->name);
        else
          sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
                     version, debugmode, tname, "ac2ptr_is_NULL!!");
        return 0;
      }
    case HUNTED_ISME:
      break;
    default:
      return 0;
    }

  if(!IsAnOper(sptr))
    {
      /* pacing for /trace is problemmatical */
#if PACE_TRACE
      if((last_used + PACE_WAIT) > CurrentTime)
        {
          /* safe enough to give this on a local connect only */
          if(MyClient(sptr))
            sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
          return 0;
        }
      else
        {
          last_used = CurrentTime;
        }
#endif

      if (parv[1] && !index(parv[1],'.') && (index(parv[1], '*')
          || index(parv[1], '?'))) /* bzzzt, no wildcard nicks for nonopers */
        {
          sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
                     parv[0], parv[1]);
          return 0;
        }
    }

  if(MyClient(sptr))
    sendto_realops_flags(FLAGS_SPY, "trace requested by %s (%s@%s) [%s]",
                       sptr->name, sptr->username, sptr->host,
                       sptr->user->server);


  doall = (parv[1] && (parc > 1)) ? match(tname, me.name): TRUE;
  wilds = !parv[1] || strchr(tname, '*') || strchr(tname, '?');
  dow = wilds || doall;
  
  if(!IsAnOper(sptr) || !dow) /* non-oper traces must be full nicks */
                              /* lets also do this for opers tracing nicks */
    {
      const char* name;
      const char* ip;
      int         c_class;

      acptr = hash_find_client(tname,(struct Client *)NULL);
      if(!acptr || !IsPerson(acptr)) 
        {
          /* this should only be reached if the matching
             target is this server */
          sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
                     parv[0], tname);
          return 0;
        }
      name = get_client_name(acptr, FALSE);
      ip = inetntoa((char*) &acptr->ip);

      c_class = get_client_class(acptr);

      if (IsAnOper(acptr))
        {
          sendto_one(sptr, form_str(RPL_TRACEOPERATOR),
                     me.name, parv[0], c_class,
                     name, 
                     IsAnOper(sptr)?ip:"127.0.0.1",
                     now - acptr->lasttime,
                     (acptr->user)?(now - acptr->user->last):0);
        }
      else
        {
          sendto_one(sptr,form_str(RPL_TRACEUSER),
                     me.name, parv[0], c_class,
                     name, 
                     IsIPHidden(acptr)?"127.0.0.1":ip,
                     now - acptr->lasttime,
                     (acptr->user)?(now - acptr->user->last):0);
        }
      sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
                 parv[0], tname);
      return 0;
    }

  if (dow && LIFESUX && !IsOper(sptr))
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }

  memset((void *)link_s,0,sizeof(link_s));
  memset((void *)link_u,0,sizeof(link_u));

  /*
   * Count up all the servers and clients in a downlink.
   */
  if (doall)
   {
    for (acptr = GlobalClientList; acptr; acptr = acptr->next)
#ifdef  SHOW_INVISIBLE_LUSERS
      if (IsPerson(acptr))
        link_u[acptr->from->fd]++;
#else
      if (IsPerson(acptr) &&
        (!IsInvisible(acptr) || IsAnOper(sptr)))
        {
          link_u[acptr->from->fd]++;
        }
#endif
   }
  else
    {
      if (IsServer(acptr))
      {
        link_s[acptr->from->fd]++;
      }
    }

  /* report all direct connections */
  for (i = 0; i <= highest_fd; i++)
    {
      const char* name;
      const char* ip;
      int         c_class;
      
      if (!(acptr = local[i])) /* Local Connection? */
        continue;
      if (IsInvisible(acptr) && dow &&
          !(MyConnect(sptr) && IsAnOper(sptr)) &&
          !IsAnOper(acptr) && (acptr != sptr))
        continue;
      if (!doall && wilds && !match(tname, acptr->name))
        continue;
      if (!dow && irccmp(tname, acptr->name))
        continue;
      name = get_client_name(acptr, FALSE);
      ip = inetntoa((const char*) &acptr->ip);

      c_class = get_client_class(acptr);
      
      switch(acptr->status)
        {
        case STAT_CONNECTING:
          sendto_one(sptr, form_str(RPL_TRACECONNECTING), me.name,
                     parv[0], c_class, name);
          cnt++;
          break;
        case STAT_HANDSHAKE:
          sendto_one(sptr, form_str(RPL_TRACEHANDSHAKE), me.name,
                     parv[0], c_class, name);
          cnt++;
          break;
        case STAT_ME:
          break;
        case STAT_UNKNOWN:
/* added time -Taner */
          sendto_one(sptr, form_str(RPL_TRACEUNKNOWN),
                     me.name, parv[0], c_class, name, ip,
                     acptr->firsttime ? CurrentTime - acptr->firsttime : -1);
          cnt++;
          break;
        case STAT_CLIENT:
          /* Only opers see users if there is a wildcard
           * but anyone can see all the opers.
           */
          if ((IsAnOper(sptr) &&
              (MyClient(sptr) || !(dow && IsInvisible(acptr))))
              || !dow || IsAnOper(acptr))
            {
              if (IsAnOper(acptr))
                sendto_one(sptr,
                           form_str(RPL_TRACEOPERATOR),
                           me.name,
                           parv[0], c_class,
                           name, IsAnOper(sptr)?ip:"127.0.0.1",
                           now - acptr->lasttime,
                           (acptr->user)?(now - acptr->user->last):0);
              else
                sendto_one(sptr,form_str(RPL_TRACEUSER),
                           me.name, parv[0], c_class,
                           name,
                           IsIPHidden(acptr)?"127.0.0.1":ip,
                           now - acptr->lasttime,
                           (acptr->user)?(now - acptr->user->last):0);
              cnt++;
            }
          break;
        case STAT_SERVER:
#if 0
          if (acptr->serv->user)
            sendto_one(sptr, form_str(RPL_TRACESERVER),
                       me.name, parv[0], c_class, link_s[i],
                       link_u[i], name, acptr->serv->by,
                       acptr->serv->user->username,
                       acptr->serv->user->host, now - acptr->lasttime);
          else
#endif
            sendto_one(sptr, form_str(RPL_TRACESERVER),
                       me.name, parv[0], c_class, link_s[i],
                       link_u[i], name, *(acptr->serv->by) ?
                       acptr->serv->by : "*", "*",
                       me.name, now - acptr->lasttime);
          cnt++;
          break;
        default: /* ...we actually shouldn't come here... --msa */
          sendto_one(sptr, form_str(RPL_TRACENEWTYPE), me.name,
                     parv[0], name);
          cnt++;
          break;
        }
    }
  /*
   * Add these lines to summarize the above which can get rather long
   * and messy when done remotely - Avalon
   */
  if (!SendWallops(sptr) || !cnt)
    {
      if (cnt)
        {
          sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
                     parv[0],tname);
          return 0;
        }
      /* let the user have some idea that its at the end of the
       * trace
       */
      sendto_one(sptr, form_str(RPL_TRACESERVER),
                 me.name, parv[0], 0, link_s[me.fd],
                 link_u[me.fd], me.name, "*", "*", me.name);
      sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name,
                 parv[0],tname);
      return 0;
    }
  for (cltmp = ClassList; doall && cltmp; cltmp = cltmp->next)
    if (Links(cltmp) > 0)
      sendto_one(sptr, form_str(RPL_TRACECLASS), me.name,
                 parv[0], ClassType(cltmp), Links(cltmp));
  sendto_one(sptr, form_str(RPL_ENDOFTRACE),me.name, parv[0],tname);
  return 0;
}

#ifdef LTRACE
/*
** m_ltrace - LimitedTRACE... like m_trace() but doesn't return TRACEUSER, TRACEUNKNOWN, or TRACECLASS
**      parv[0] = sender prefix
**      parv[1] = servername
*/
int     m_ltrace(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  int   i;
  struct Client       *acptr = NULL;
  char  *tname;
  int   doall, link_s[MAXCONNECTIONS], link_u[MAXCONNECTIONS];
  int   cnt = 0, wilds, dow;
  static time_t now;
  
  if (check_registered(sptr))
    return 0;

  if (parc > 2)
    if (hunt_server(cptr, sptr, ":%s LTRACE %s :%s",
                    2, parc, parv))
      return 0;
  
  if (parc > 1)
    tname = parv[1];
  else
    tname = me.name;

  switch (hunt_server(cptr, sptr, ":%s LTRACE :%s", 1, parc, parv))
    {
    case HUNTED_PASS: /* note: gets here only if parv[1] exists */
      {
        struct Client *ac2ptr;
        
        ac2ptr = next_client(GlobalClientList, tname);
        if (ac2ptr)
          sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
                     version, debugmode, tname, ac2ptr->from->name);
        else
          sendto_one(sptr, form_str(RPL_TRACELINK), me.name, parv[0],
                     version, debugmode, tname, "ac2ptr_is_NULL!!");
        return 0;
      }
    case HUNTED_ISME:
      break;
    default:
      return 0;
    }


  if(MyClient(sptr))
    sendto_realops_flags(FLAGS_SPY, "ltrace requested by %s (%s@%s) [%s]",
                       sptr->name, sptr->username, sptr->host,
                       sptr->user->server);


  doall = (parv[1] && (parc > 1)) ? match(tname, me.name): TRUE;
  wilds = !parv[1] || index(tname, '*') || index(tname, '?');
  dow = wilds || doall;
  
  for (i = 0; i < MAXCONNECTIONS; i++)
    link_s[i] = 0, link_u[i] = 0;
                        
  if (dow && LIFESUX && !IsOper(sptr))
    {
      sendto_one(sptr,form_str(RPL_LOAD2HI),me.name,parv[0]);
      return 0;
    }

  /*
   * Count up all the servers and clients in a downlink.
   */
  if (doall) {
    for (acptr = GlobalClientList; acptr; acptr = acptr->next) {
      if (IsServer(acptr)) 
        ++link_s[acptr->from->fd];
    }
  }

  /* report all direct connections */
  now = time(NULL);
  for (i = 0; i <= highest_fd; i++)
    {
      const char* name;
      const char* ip;
      int         c_class;
      
      if (!(acptr = local[i])) /* Local Connection? */
        continue;
      if (IsInvisible(acptr) && dow &&
          !(MyConnect(sptr) && IsAnOper(sptr)) &&
          !IsAnOper(acptr) && (acptr != sptr))
        continue;
      if (!doall && wilds && !match(tname, acptr->name))
        continue;
      if (!dow && irccmp(tname, acptr->name))
        continue;
      name = get_client_name(acptr, FALSE);
      ip = inetntoa((const char*) &acptr->ip);

      c_class = get_client_class(acptr);
      
      switch(acptr->status)
        {
        case STAT_HANDSHAKE:
          sendto_one(sptr, form_str(RPL_TRACEHANDSHAKE), me.name,
                     parv[0], c_class, name);
          cnt++;
          break;
        case STAT_ME:
          break;
        case STAT_CLIENT:
          /* Well, most servers don't have a LOT of OPERs... let's show them too */
          if ((IsAnOper(sptr) &&
              (MyClient(sptr) || !(dow && IsInvisible(acptr))))
              || !dow || IsAnOper(acptr))
            {
              if (IsAnOper(acptr))
                sendto_one(sptr,
                           form_str(RPL_TRACEOPERATOR),
                           me.name, parv[0], c_class,
                           name, 
                           IsAnOper(sptr)?ip:"127.0.0.1", 
                           now - acptr->lasttime,
                           (acptr->user)?(now - acptr->user->last):0);
              cnt++;
            }
          break;
        case STAT_SERVER:
#if 0
          if (acptr->serv->user)
            sendto_one(sptr, form_str(RPL_TRACESERVER),
                       me.name, parv[0], c_class, link_s[i],
                       link_u[i], name, acptr->serv->by,
                       acptr->serv->user->username,
                       acptr->serv->user->host, now - acptr->lasttime);
          else
#endif
            sendto_one(sptr, form_str(RPL_TRACESERVER),
                       me.name, parv[0], c_class, link_s[i],
                       link_u[i], name, *(acptr->serv->by) ?
                       acptr->serv->by : "*", "*",
                       me.name, now - acptr->lasttime);
          cnt++;
          break;
        default: /* ...we actually shouldn't come here... --msa */
          sendto_one(sptr, form_str(RPL_TRACENEWTYPE), me.name,
                     parv[0], name);
          cnt++;
          break;
        }
    }
  /*
   * Add these lines to summarize the above which can get rather long
   * and messy when done remotely - Avalon
   */
  if (!IsAnOper(sptr) || !cnt)
    {
      if (cnt)
          return 0;
      /* let the user have some idea that its at the end of the
       * trace
       */
      sendto_one(sptr, form_str(RPL_TRACESERVER),
                 me.name, parv[0], 0, link_s[me.fd],
                 link_u[me.fd], me.name, "*", "*", me.name);
      return 0;
    }
  return 0;
}
#endif /* LTRACE */

/*
** m_close - added by Darren Reed Jul 13 1992.
*/
int     m_close(struct Client *cptr,
                struct Client *sptr,
                int parc,
                char *parv[])
{
  struct Client       *acptr;
  int   i;
  int   closed = 0;

  if (!MyOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  for (i = highest_fd; i; i--)
    {
      if (!(acptr = local[i]))
        continue;
      if (!IsUnknown(acptr) && !IsConnecting(acptr) &&
          !IsHandshake(acptr))
        continue;
      sendto_one(sptr, form_str(RPL_CLOSING), me.name, parv[0],
                 get_client_name(acptr, TRUE), acptr->status);
      (void)exit_client(acptr, acptr, acptr, "Oper Closing");
      closed++;
    }
  sendto_one(sptr, form_str(RPL_CLOSEEND), me.name, parv[0], closed);
  return 0;
}

int m_die(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  struct Client* acptr;
  int      i;

  if (!MyClient(sptr) || !IsAnOper(sptr))
    {
      sendto_one(sptr, form_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }

  if ( !IsOperDie(sptr) )
    {
      sendto_one(sptr,":%s NOTICE %s: You have no D flag", me.name, parv[0]);
      return 0;
    }

  if(parc < 2)
    {
      sendto_one(sptr,":%s NOTICE %s :Need server name /die %s",
                 me.name,sptr->name,me.name);
      return 0;
    }
  else
    {
      if(irccmp(parv[1],me.name))
        {
          sendto_one(sptr,":%s NOTICE %s :Mismatch on /die %s",
                     me.name,sptr->name,me.name);
          return 0;
        }
    }

  for (i = 0; i <= highest_fd; i++)
    {
      if (!(acptr = local[i]))
        continue;
      if (IsClient(acptr))
        {
          if(IsAnOper(acptr))
            sendto_one(acptr,
                       ":%s NOTICE %s :Server Terminating. %s",
                       me.name, acptr->name,
                       get_client_name(sptr, TRUE));
          else
            sendto_one(acptr,
                       ":%s NOTICE %s :Server Terminating. %s",
                       me.name, acptr->name,
                       get_client_name(sptr, HIDEME));
        }
      else if (IsServer(acptr))
        sendto_one(acptr, ":%s ERROR :Terminated by %s",
                   me.name, get_client_name(sptr, HIDEME));
    }
  flush_connections(0);
  /* 
   * this is a normal exit, tell the os it's ok 
   */
  exit(0);
  /* NOT REACHED */
  return 0;
}


/*
 * set_autoconn
 *
 * inputs       - struct Client pointer to oper requesting change
 *              -
 * output       - none
 * side effects -
 */

static void set_autoconn(struct Client *sptr,char *parv0,char *name,int newval)
{
  struct ConfItem *aconf;

  if((aconf= find_conf_by_name(name, CONF_CONNECT_SERVER)))
    {
      if (newval)
        aconf->flags |= CONF_FLAGS_ALLOW_AUTO_CONN;
      else
        aconf->flags &= ~CONF_FLAGS_ALLOW_AUTO_CONN;

      sendto_ops(
                 "%s has changed AUTOCONN for %s to %i",
                 parv0, name, newval);
      sendto_one(sptr,
                 ":%s NOTICE %s :AUTOCONN for %s is now set to %i",
                 me.name, parv0, name, newval);
    }
  else
    {
      sendto_one(sptr,
                 ":%s NOTICE %s :Can't find %s",
                 me.name, parv0, name);
    }
}


/*
 * show_servers - send server list to client
 *
 * inputs        - struct Client pointer to client to show server list to
 *               - name of client
 * output        - NONE
 * side effects        -
 */
void show_servers(struct Client *cptr)
{
  register struct Client *cptr2;
  register int j=0;                /* used to count servers */

  for(cptr2 = serv_cptr_list; cptr2; cptr2 = cptr2->next_server_client)
    {
      ++j;
      sendto_one(cptr, ":%s %d %s :%s (%s!%s@%s) Idle: %d",
                 me.name, RPL_STATSDEBUG, cptr->name, cptr2->name,
                 (cptr2->serv->by[0] ? cptr2->serv->by : "Remote."), 
                 "*", "*", CurrentTime - cptr2->lasttime);

      /*
       * NOTE: moving the username and host to the client struct
       * makes the names in the server->user struct no longer available
       * IMO this is not a big problem because as soon as the user that
       * started the connection leaves the user info has to go away
       * anyhow. Simply showing the nick should be enough here.
       * --Bleep
       */ 
    }

  sendto_one(cptr, ":%s %d %s :%d Server%s", me.name, RPL_STATSDEBUG,
             cptr->name, j, (j==1) ? "" : "s");
}

