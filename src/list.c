/************************************************************************
 *   IRC - Internet Relay Chat, src/list.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Finland
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
 *  (C) 1988 University of Oulu, Computing Center and Jarkko Oikarinen
 *
 * $Id$
 */
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "h.h"
#include "numeric.h"
#include "blalloc.h"
#include "res.h"
#include "class.h"
#include "send.h"
/* #include "s_conf.h"

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned int) 0xffffffff)
#endif
*/

extern int BlockHeapGarbageCollect(BlockHeap *);
extern SetOptionsType GlobalSetOptions;

#ifdef NEED_SPLITCODE
extern int server_was_split;
extern time_t server_split_time;
#endif

char *currentfile;
int currentline;

/* locally defined functions */


/*
 * re-written to use Wohali (joant@cadence.com)
 * block allocator routines. very nicely done Wohali
 *
 * -Dianora
 *
 */

/* Number of Link's to pre-allocate at a time 
   for Efnet 1000 seems reasonable, 
   for smaller nets who knows? -Dianora
   */

#define LINK_PREALLOCATE 1024

/* Number of aClient structures to preallocate at a time
   for Efnet 1024 is reasonable 
   for smaller nets who knows? -Dianora
   */

/* This means you call MyMalloc 30 some odd times,
   rather than 30k times -Dianora
*/

#define CLIENTS_PREALLOCATE 1024

void	outofmemory();

int	numclients = 0;

/* for Wohali's block allocator */
BlockHeap *free_local_aClients;
BlockHeap *free_Links;
BlockHeap *free_remote_aClients;
BlockHeap *free_anUsers;
#ifdef FLUD
BlockHeap *free_fludbots;
#endif /* FLUD */

void initlists()
{
  /* Might want to bump up LINK_PREALLOCATE if FLUD is defined */
  free_Links = BlockHeapCreate((size_t)sizeof(Link),LINK_PREALLOCATE);

  /* start off with CLIENTS_PREALLOCATE for now... on typical
     efnet these days, it can get up to 35k allocated */

  free_remote_aClients =
     BlockHeapCreate((size_t)CLIENT_REMOTE_SIZE,CLIENTS_PREALLOCATE);

  /* Can't EVER have more than MAXCONNECTIONS number of local aClients */

  free_local_aClients = BlockHeapCreate((size_t)CLIENT_LOCAL_SIZE,
                                 MAXCONNECTIONS);

  /* anUser structs are used by both local aClients, and remote aClients */

  free_anUsers = BlockHeapCreate((size_t)sizeof(anUser),
				 CLIENTS_PREALLOCATE+MAXCONNECTIONS);

#ifdef FLUD
  /* fludbot structs are used to track CTCP Flooders */
  free_fludbots = BlockHeapCreate((size_t)sizeof(struct fludbot),
				MAXCONNECTIONS);
#endif /* FLUD */
}

/*
 * outofmemory()
 *
 * input	- NONE
 * output	- NONE
 * side effects	- simply try to report there is a problem
 *	  	  I free all the memory in the kline lists
 *		  hoping to free enough memory so that a proper
 *		  report can be made. If I was already here (was_here)
 *	  	  then I got called twice, and more drastic measures
 *		  are in order. I'll try to just abort() at least.
 *		  -Dianora
 */

void outofmemory()
{
  static int was_here=0;

  if(was_here)
    abort();

  was_here = YES;
  clear_mtrie_conf_links();

  Debug((DEBUG_FATAL, "Out of memory: restarting server..."));
  restart("Out of Memory");
}

	
/*
** Create a new aClient structure and set it to initial state.
**
**	from == NULL,	create local client (a client connected
**			to a socket).
**
**	from,	create remote client (behind a socket
**			associated with the client defined by
**			'from'). ('from' is a local client!!).
*/
aClient	*make_client(aClient *from)
{
  aClient *cptr = NULL;

  if (!from)
    { /* from is NULL */
      cptr = BlockHeapALLOC(free_local_aClients,aClient);
      if(cptr == (aClient *)NULL)
	outofmemory();

      memset((void *)cptr, 0, CLIENT_LOCAL_SIZE);
      
      /* Note:  structure is zero (calloc) */
      cptr->from = cptr; /* 'from' of local client is self! */

      /* commenting out unnecessary assigns, but leaving them
       * for documentation. REMEMBER the fripping struct is already
       * zeroed up above =DUH= 
       * -Dianora 
       */

      /* cptr->next = NULL; */
      /* cptr->prev = NULL; */
      /* cptr->hnext = NULL; */
      /* cptr->user = NULL; */
      /* cptr->serv = NULL; */
      /* cptr->lnext = NULL; */
      /* cptr->lprev = NULL; */
#ifdef ZIP_LINKS
      /* cptr->zip = NULL; */
#endif
      /* cptr->user = NULL; */
      /* cptr->serv = NULL; */
      cptr->status = STAT_UNKNOWN;
      cptr->fd = -1;
      strcpy(cptr->username, "unknown");
      cptr->since = cptr->lasttime = cptr->firsttime = timeofday;
      /* cptr->confs = NULL; */
      /* cptr->sockhost[0] = '\0'; */
      /* cptr->buffer[0] = '\0'; */
      /* cptr->username[0] = '\0'; */
    }
  else
    { /* from is not NULL */
      cptr = BlockHeapALLOC(free_remote_aClients,aClient);
      if(cptr == (aClient *)NULL)
	outofmemory();

      memset((void *)cptr, 0, CLIENT_REMOTE_SIZE);

      /* Note:  structure is zero (calloc) */
      cptr->from = from; /* 'from' of local client is self! */
      /* cptr->next = NULL; */
      /* cptr->prev = NULL; */
      /* cptr->hnext = NULL; */
      /* cptr->idhnext = NULL; */
      /* cptr->lnext = NULL; */
      /* cptr->lprev = NULL; */
      /* cptr->user = NULL; */
      /* cptr->serv = NULL; */
      cptr->status = STAT_UNKNOWN;
      cptr->fd = -1;
      (void)strcpy(cptr->username, "unknown");
    }
  return (cptr);
}

void _free_client(aClient *cptr)
{
  int retval = 0;

  if(cptr->fd == -2)	
    {
      retval = BlockHeapFree(free_local_aClients,cptr);
    }
  else
    {
      retval = BlockHeapFree(free_remote_aClients,cptr);
    }
  if(retval)
    {
/* Looks "unprofessional" maybe, but I am going to leave this sendto_ops in
   it should never happen, and if it does, the hybrid team wants to hear
   about it
*/
      sendto_ops("list.c couldn't BlockHeapFree(free_remote_aClients,cptr) cptr = %lX", cptr );
       sendto_ops("Please report to the hybrid team! ircd-hybrid@the-project.org");

#if defined(USE_SYSLOG) && defined(SYSLOG_BLOCK_ALLOCATOR)
       syslog(LOG_DEBUG,"list.c couldn't BlockHeapFree(free_remote_aClients,cptr) cptr = %lX", (long unsigned int) cptr);
#endif
    }
}

/*
** 'make_user' add's an User information block to a client
** if it was not previously allocated.
*/
anUser *make_user(aClient *cptr)
{
  anUser	*user;

  user = cptr->user;
  if (!user)
    {
      user = BlockHeapALLOC(free_anUsers,anUser);
      if( user == (anUser *)NULL)
	outofmemory();
      user->away = NULL;
      *user->username = '\0';		/* Initialize this crap */
      *user->host = '\0';
      user->server = (char *)NULL;	/* scache server name */
      user->refcnt = 1;
      user->joined = 0;
      user->channel = NULL;
      user->invited = NULL;
      cptr->user = user;
    }
  return user;
}

aServer *make_server(aClient *cptr)
{
  aServer	*serv = cptr->serv;

  if (!serv)
    {
      serv = (aServer *)MyMalloc(sizeof(aServer));
      memset((void *)serv, 0, sizeof(aServer));

      /* The commented out lines before are
       * for documentation purposes only
       * as they are zeroed by memset above
       */
      /*      serv->user = NULL; */
      /*      serv->users = NULL; */
      /*      serv->servers = NULL; */
      /*      *serv->by = '\0'; */
      /*      serv->up = (char *)NULL; */

      cptr->serv = serv;
    }
  return cptr->serv;
}

/*
** free_user
**	Decrease user reference count by one and release block,
**	if count reaches 0
*/
void _free_user(anUser *user, aClient *cptr)
{
  if (--user->refcnt <= 0)
    {
      if (user->away)
	MyFree((char *)user->away);
      /*
       * sanity check
       */
      if (user->joined || user->refcnt < 0 ||
	  user->invited || user->channel)
      sendto_ops("* %#x user (%s!%s@%s) %#x %#x %#x %d %d *",
		 cptr, cptr ? cptr->name : "<noname>",
		 user->username, user->host, user,
		 user->invited, user->channel, user->joined,
		 user->refcnt);

      if(BlockHeapFree(free_anUsers,user))
	{
	  sendto_ops("list.c couldn't BlockHeapFree(free_anUsers,user) user = %lX", user );
	  sendto_ops("Please report to the hybrid team! ircd-hybrid@the-project.org");
#if defined(USE_SYSLOG) && defined(SYSLOG_BLOCK_ALLOCATOR)
	  syslog(LOG_DEBUG,"list.c couldn't BlockHeapFree(free_anUsers,user) user = %lX", (long unsigned int) user);
#endif
	}


    }
}

/*
 * taken the code from ExitOneClient() for this and placed it here.
 * - avalon
 */
void remove_client_from_list(aClient *cptr)
{
  if (IsServer(cptr))
    {
      Count.server--;

#ifdef NEED_SPLITCODE

	/* Don't bother checking for a split, if split code
	 * is deactivated with server_split_recovery_time == 0
	 */

	if(SPLITDELAY && (Count.server < SPLITNUM))
	  {
	    if (!server_was_split)
	      {
	        sendto_ops("Netsplit detected, split-mode activated");
	        server_was_split = YES;
	      }
	    server_split_time = NOW;
	  }
#endif
    }

  else if (IsClient(cptr))
    {
      Count.total--;
      if (IsAnOper(cptr))
	{
	  Count.oper--;
	}
    }
  if (IsInvisible(cptr)) Count.invisi--;
  checklist();
  if (cptr->prev)
    cptr->prev->next = cptr->next;
  else
    {
      GlobalClientList = cptr->next;
      GlobalClientList->prev = NULL;
    }
  if (cptr->next)
    cptr->next->prev = cptr->prev;
  if (IsPerson(cptr) && cptr->user)
    {
      add_history(cptr,0);
      off_history(cptr);
    }
  if (cptr->user)
    free_user(cptr->user, cptr); /* try this here */
  if (cptr->serv)
    {
      if (cptr->serv->user)
	free_user(cptr->serv->user, cptr);
      MyFree((char *)cptr->serv);
    }

  /* YEUCK... This is telling me the only way of knowing that
   * a cptr was pointing to a remote aClient or not is by checking
   * to see if its fd is not -2
   *
   * -Dianora 
   *
   */
  /*
  if (cptr->fd == -2)
    cloc.inuse--;
  else
    crem.inuse--;
    */

#ifdef FLUD
  if(MyFludConnect(cptr))
    free_fluders(cptr,NULL);
  free_fludees(cptr);
#endif

  free_client(cptr);
/* WTF is this?!#  -Taner */
  /* numclients--; */
  return;
}

/*
 * although only a small routine, it appears in a number of places
 * as a collection of a few lines...functions like this *should* be
 * in this file, shouldnt they ?  after all, this is list.c, isnt it ?
 * -avalon
 */
void add_client_to_list(aClient *cptr)
{
  /*
   * since we always insert new clients to the top of the list,
   * this should mean the "me" is the bottom most item in the list.
   */
  cptr->next = GlobalClientList;
  GlobalClientList = cptr;
  if (cptr->next)
    cptr->next->prev = cptr;
  return;
}

/*
 * Look for ptr in the linked listed pointed to by link.
 */
Link *find_user_link(Link *lp, aClient *ptr)
{
  if (ptr)
    while (lp)
      {
	if (lp->value.cptr == ptr)
	  return (lp);
	lp = lp->next;
      }
  return ((Link *)NULL);
}

Link *find_channel_link(Link *lp, aChannel *chptr)
{ 
  if (chptr)
    for(;lp;lp=lp->next)
      if (lp->value.chptr == chptr)
	return lp;
  return ((Link *)NULL);
}

Link *make_link()
{
  Link	*lp;

  lp = BlockHeapALLOC(free_Links,Link);
  if( lp == (Link *)NULL)
    outofmemory();

  lp->next = (Link *)NULL;		/* just to be paranoid... */

  return lp;
}

void _free_link(Link *lp)
{
  if(BlockHeapFree(free_Links,lp))
    {
      sendto_ops("list.c couldn't BlockHeapFree(free_Links,lp) lp = %lX", lp );
      sendto_ops("Please report to the hybrid team!");
    }
}

aClass *make_class()
{
  aClass	*tmp;

  tmp = (aClass *)MyMalloc(sizeof(aClass));
  return tmp;
}

void free_class(aClass *tmp)
{
  MyFree((char *)tmp);
}

/*
Attempt to free up some block memory

list_garbage_collect

inputs		- NONE
output		- NONE
side effects	- memory is possibly freed up
*/

void block_garbage_collect()
{
  BlockHeapGarbageCollect(free_Links);
  BlockHeapGarbageCollect(free_local_aClients);
  BlockHeapGarbageCollect(free_remote_aClients);
  BlockHeapGarbageCollect(free_anUsers);
#ifdef FLUD
  BlockHeapGarbageCollect(free_fludbots);
#endif /* FLUD */
}

/* Functions taken from +CSr31, paranoified to check that the client
** isn't on a llist already when adding, and is there when removing -orabidoo
*/
void add_client_to_llist(aClient **bucket, aClient *client)
{
  if (!client->lprev && !client->lnext)
    {
      client->lprev = (aClient *)NULL;
      if ((client->lnext = *bucket) != (aClient *)NULL)
	client->lnext->lprev = client;
      *bucket = client;
    }
}

void del_client_from_llist(aClient **bucket, aClient *client)
{
  if (client->lprev)
    {
      client->lprev->lnext = client->lnext;
    }
  else if (*bucket == client)
    {
      *bucket = client->lnext;
    }
  if (client->lnext)
    {
      client->lnext->lprev = client->lprev;
    }
  client->lnext = client->lprev = (aClient *)NULL;
}

