/************************************************************************
 *   IRC - Internet Relay Chat, src/hash.c
 *   Copyright (C) 1991 Darren Reed
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
 */

#ifndef lint
static char sccsid[] = "@(#)hash.c	2.10 03 Jul 1993 (C) 1991 Darren Reed";

static char *rcs_version = "$Id$";
#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "hash.h"
#include "h.h"

#include <sys/stat.h>
#include <fcntl.h>

int HASHSIZE = U_MAX;
int CHANNELHASHSIZE = CH_MAX;

#ifdef  DEBUGMODE
static  aHashEntry	*clientTable = NULL;
static	aHashEntry	*channelTable = NULL;
static  int	clhits, clmiss;
static	int	chhits, chmiss;
#else

static	aHashEntry	clientTable[U_MAX];
static	aHashEntry	channelTable[CH_MAX];

#endif

/*

look in whowas.c for the missing ...[WW_MAX]; entry
  - Dianora
*/

/*
 * Hashing.
 *
 *   The server uses a chained hash table to provide quick and efficient
 * hash table mantainence (providing the hash function works evenly over
 * the input range).  The hash table is thus not susceptible to problems
 * of filling all the buckets or the need to rehash.
 *    It is expected that the hash table would look somehting like this
 * during use:
 *                   +-----+    +-----+    +-----+   +-----+
 *                ---| 224 |----| 225 |----| 226 |---| 227 |---
 *                   +-----+    +-----+    +-----+   +-----+
 *                      |          |          |
 *                   +-----+    +-----+    +-----+
 *                   |  A  |    |  C  |    |  D  |
 *                   +-----+    +-----+    +-----+
 *                      |
 *                   +-----+
 *                   |  B  |
 *                   +-----+
 *
 * A - GOPbot, B - chang, C - hanuaway, D - *.mu.OZ.AU
 *
 * The order shown above is just one instant of the server.  Each time a
 * lookup is made on an entry in the hash table and it is found, the entry
 * is moved to the top of the chain.
 *
 *    ^^^^^^^^^^^^^^^^ **** Not anymore - Dianora
 *
 */

unsigned hash_nick_name(nname)
     char *nname;
{
  Reg unsigned hash = 0;
  Reg int hash2 = 0;
  Reg int ret;
  Reg char lower;
  while (*nname)
    {
      lower = tolower(*nname);
      hash = (hash << 1) + lower;
      hash2 = (hash2 >> 1) + lower;
      nname++;
    }
  ret = ((hash & U_MAX_INITIAL_MASK) << BITS_PER_COL) +
    (hash2 & BITS_PER_COL_MASK);
  return ret;
}

/*
 * hash_channel_name
 *
 * calculate a hash value on at most the first 30 characters of the channel
 * name. Most names are short than this or dissimilar in this range. There
 * is little or no point hashing on a full channel name which maybe 255 chars
 * long.
 */
int	hash_channel_name(char *name)
{
  register unsigned char *hname = (unsigned char *)name;
  register unsigned int hash = 0;
  register int hash2 = 0;
  register char lower;
  register int i = 30;

  while(*hname && --i)
    {
      lower = tolower(*hname);
      hash = (hash << 1) + lower;
      hash2 = (hash2 >> 1) + lower;
      hname++;
    }
  return ((hash & CH_MAX_INITIAL_MASK) << BITS_PER_COL) +
    (hash2 & BITS_PER_COL_MASK);
}

unsigned int hash_whowas_name(char *name)
{
  register unsigned char *nname = (unsigned char *) name;
  register unsigned int hash = 0;
  register int hash2 = 0;
  register int ret;
  register char lower;
  
  while (*nname)
    {
      lower = tolower(*nname);
      hash = (hash << 1) + lower;
      hash2 = (hash2 >> 1) + lower;
      nname++;
    }
  ret = ((hash & WW_MAX_INITIAL_MASK) << BITS_PER_COL) +
    (hash2 & BITS_PER_COL_MASK);
  return ret;
}

/*
 * clear_*_hash_table
 *
 * Nullify the hashtable and its contents so it is completely empty.
 */
void	clear_client_hash_table()
{
#ifdef	DEBUGMODE
  clhits = 0;
  clmiss = 0;
  if(!clientTable)
    clientTable = (aHashEntry *)MyMalloc(HASHSIZE *
					sizeof(aHashEntry));
#endif

  memset((void *)clientTable, 0, sizeof(aHashEntry) * U_MAX);
}

void	clear_channel_hash_table()
{
#ifdef	DEBUGMODE
  chmiss = 0;
  chhits = 0;
  if (!channelTable)
    channelTable = (aHashEntry *)MyMalloc(CHANNELHASHSIZE *
					  sizeof(aHashEntry));
#endif
  memset((void *)channelTable, 0, sizeof(aHashEntry) * CH_MAX);
}

/*
 * add_to_client_hash_table
 */
int	add_to_client_hash_table(char *name, aClient *cptr)
{
  Reg	int	hashv;

  hashv = hash_nick_name(name);
  cptr->hnext = (aClient *)clientTable[hashv].list;
  clientTable[hashv].list = (void *)cptr;
  clientTable[hashv].links++;
  clientTable[hashv].hits++;
  return 0;
}

/*
 * add_to_channel_hash_table
 */
int	add_to_channel_hash_table(char *name,aChannel *chptr)
{
  Reg	int	hashv;

  hashv = hash_channel_name(name);
  chptr->hnextch = (aChannel *)channelTable[hashv].list;
  channelTable[hashv].list = (void *)chptr;
  channelTable[hashv].links++;
  channelTable[hashv].hits++;
  return 0;
}

/*
 * del_from_client_hash_table
 */
int	del_from_client_hash_table(char *name,aClient *cptr)
{
  Reg	aClient	*tmp, *prev = NULL;
  Reg	int	hashv;

  hashv = hash_nick_name(name);
  for (tmp = (aClient *)clientTable[hashv].list; tmp; tmp = tmp->hnext)
    {
      if (tmp == cptr)
	{
	  if (prev)
	    prev->hnext = tmp->hnext;
	  else
	    clientTable[hashv].list = (void *)tmp->hnext;
	  tmp->hnext = NULL;
	  if (clientTable[hashv].links > 0)
	    {
	      clientTable[hashv].links--;
	      return 1;
	    } 
	  else
	    /*
	     * Should never actually return from here and
	     * if we do it is an error/inconsistency in the
	     * hash table.
	     */
	    return -1;
	}
      prev = tmp;
    }
  return 0;
}

/*
 * del_from_channel_hash_table
 */
int	del_from_channel_hash_table(char *name, aChannel *chptr)
{
  Reg	aChannel	*tmp, *prev = NULL;
  Reg	int	hashv;

  hashv = hash_channel_name(name);
  for (tmp = (aChannel *)channelTable[hashv].list; tmp;
       tmp = tmp->hnextch)
    {
      if (tmp == chptr)
	{
	  if (prev)
	    prev->hnextch = tmp->hnextch;
	  else
	    channelTable[hashv].list=(void *)tmp->hnextch;
	  tmp->hnextch = NULL;
	  if (channelTable[hashv].links > 0)
	    {
	      channelTable[hashv].links--;
	      return 1;
	    }
	  else
	    return -1;
	}
      prev = tmp;
    }
  return 0;
}


/*
 * hash_find_client
 */
aClient	*hash_find_client(char *name, aClient *cptr)
{
  Reg	aClient	*tmp;
  Reg	aClient	*prv = NULL;
  Reg	aHashEntry	*tmp3;
  int	hashv;

  hashv = hash_nick_name(name);
  tmp3 = &clientTable[hashv];

  /*
   * Got the bucket, now search the chain.
   */
  for (tmp = (aClient *)tmp3->list; tmp; prv = tmp, tmp = tmp->hnext)
    if (mycmp(name, tmp->name) == 0)
      {
#ifdef	DEBUGMODE
	clhits++;
#endif
	return(tmp);
      }
#ifdef	DEBUGMODE
  clmiss++;
#endif
return (cptr);

  /*
   * If the member of the hashtable we found isnt at the top of its
   * chain, put it there.  This builds a most-frequently used order into
   * the chains of the hash table, giving speedier lookups on those nicks
   * which are being used currently.  This same block of code is also
   * used for channels and servers for the same performance reasons.
   *
   * I don't believe it does.. it only wastes CPU, lets try it and
   * see....
   *
   * - Dianora
   */
}

/*
 * hash_find_nickserver
 */
aClient	*hash_find_nickserver(char *name, aClient *cptr)
{
  Reg	aClient	*tmp;
  Reg	aClient	*prv = NULL;
  Reg	aHashEntry	*tmp3;
  int	hashv;
  char	*serv;
  
  serv = index(name, '@');
  *serv++ = '\0';
  hashv = hash_nick_name(name);
  tmp3 = &clientTable[hashv];

  /*
   * Got the bucket, now search the chain.
   */
  for (tmp = (aClient *)tmp3->list; tmp; prv = tmp, tmp = tmp->hnext)
    if (mycmp(name, tmp->name) == 0 && tmp->user &&
	mycmp(serv, tmp->user->server) == 0)
      {
#ifdef DEBUGMODE
	clmiss++;
#endif
	*--serv = '\0';
	return (tmp);
      }

#ifdef DEBUGMODE
  clmiss++;
#endif
  *--serv = '\0';
  return (cptr);
}

/*
 * hash_find_server
 */
aClient	*hash_find_server(char *server,aClient *cptr)
{
  Reg	aClient	*tmp, *prv = NULL;
  Reg	char	*t;
  Reg	char	ch;
  aHashEntry	*tmp3;

  int hashv;

  hashv = hash_nick_name(server);
  tmp3 = &clientTable[hashv];

  for (tmp = (aClient *)tmp3->list; tmp; prv = tmp, tmp = tmp->hnext)
    {
      if (!IsServer(tmp) && !IsMe(tmp))
	continue;
      if (mycmp(server, tmp->name) == 0)
	{
#ifdef	DEBUGMODE
	  clhits++;
#endif
	  return(tmp);
	}
    }
  t = ((char *)server + strlen(server));
  /*
   * Whats happening in this next loop ? Well, it takes a name like
   * foo.bar.edu and proceeds to earch for *.edu and then *.bar.edu.
   * This is for checking full server names against masks although
   * it isnt often done this way in lieu of using matches().
   */
  for (;;)
    {
      t--;
      for (; t > server; t--)
	if (*(t+1) == '.')
	  break;
      if (*t == '*' || t == server)
	break;
      ch = *t;
      *t = '*';
      /*
       * Dont need to check IsServer() here since nicknames cant
       *have *'s in them anyway.
       */
      if (((tmp = hash_find_client(t, cptr))) != cptr)
	{
	  *t = ch;
	  return (tmp);
	}
      *t = ch;
    }
#ifdef	DEBUGMODE
  clmiss++;
#endif
  return (cptr);
}

/*
 * hash_find_channel
 */
aChannel	*hash_find_channel(char *name,aChannel *chptr)
{
  int	hashv;
  Reg	aChannel	*tmp, *prv = NULL;
  aHashEntry	*tmp3;
  
  hashv = hash_channel_name(name);
  tmp3 = &channelTable[hashv];

  for (tmp = (aChannel *)tmp3->list; tmp; prv = tmp, tmp = tmp->hnextch)
    if (mycmp(name, tmp->chname) == 0)
      {
#ifdef	DEBUGMODE
	chhits++;
#endif
	return (tmp);
      }
#ifdef	DEBUGMODE
  chmiss++;
#endif
  return chptr;
}

/*
 * NOTE: this command is not supposed to be an offical part of the ircd
 *       protocol.  It is simply here to help debug and to monitor the
 *       performance of the hash functions and table, enabling a better
 *       algorithm to be sought if this one becomes troublesome.
 *       -avalon
 *
 * partially rewritten (finally) -Dianora
 */

int	m_hash(aClient *cptr,aClient *sptr,int parc,char *parv[])
{
#ifdef DEBUGMODE
  register	int	l, i;
  register	aHashEntry	*tab;
  int	deepest = 0, deeplink = 0, showlist = 0, tothits = 0;
  int	mosthit = 0, mosthits = 0, used = 0, used_now = 0, totlink = 0;
  int	link_pop[10], size = HASHSIZE;
  char	ch;
  char  result_buf[256];
  aHashEntry	*table;
#endif
  char  hash_log_file[256];
  int   out;
  char timebuffer[MAX_DATE_STRING];
  struct tm *tmptr;

  if (!MyClient(sptr) || !IsOper(sptr))
    {
      sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
      return 0;
    }
  if(parc > 1)
    {
      if(!strcasecmp(parv[1],"iphash"))
	{
	  iphash_stats(cptr,sptr,parc,parv,-1);
	  return 0;
	}
      else if(!strcasecmp(parv[1],"dhash"))
	{
	  dhash_stats(cptr,sptr,parc,parv,-1);
	  return 0;
	}
      else if(!strcasecmp(parv[1],"Diphash"))
	{
	  tmptr = localtime(&NOW);
	  strftime(timebuffer, MAX_DATE_STRING, "%y%m%d%H%M", tmptr);
	  (void)sprintf(hash_log_file,"%s/hash/iphash.%s",
			DPATH,timebuffer);

	  if ((out = open(hash_log_file, O_RDWR|O_APPEND|O_CREAT,0664))==-1)
	      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
			 me.name, parv[0], hash_log_file);
	  else
	    sendto_one(sptr, ":%s NOTICE %s :Writing hash log to %s ",
		       me.name, parv[0], hash_log_file);

	  iphash_stats(cptr,sptr,parc,parv,out);
	  return 0;
	}
      else if(!strcasecmp(parv[1],"Ddhash"))
	{
	  tmptr = localtime(&NOW);
	  strftime(timebuffer, MAX_DATE_STRING, "%y%m%d%H%M", tmptr);
	  (void)sprintf(hash_log_file,"%s/hash/dhash.%s",
			DPATH,timebuffer);

	  if ((out = open(hash_log_file, O_RDWR|O_APPEND|O_CREAT,0664))==-1)
	      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
			 me.name, parv[0], hash_log_file);
	  else
	    sendto_one(sptr, ":%s NOTICE %s :Writing hash log to %s ",
		       me.name, parv[0], hash_log_file);
	  dhash_stats(cptr,sptr,parc,parv,out);
	  return 0;
	}

#ifndef	DEBUGMODE
    }
#endif
#ifdef	DEBUGMODE
      ch = *parv[1];
      if (islower(ch))
	{
	  table = clientTable;
	  
	}
      else
	{
	  table = channelTable;
	  size = CHANNELHASHSIZE;
	}
      if (ch == 'L' || ch == 'l')
	{
	  tmptr = localtime(&NOW);
	  strftime(timebuffer, MAX_DATE_STRING, "%y%m%d%H%M", tmptr);
	  (void)sprintf(hash_log_file,"%s/hash/%cdump.%s",
			DPATH,ch,timebuffer);
	  showlist = 1;
	  if ((out = open(hash_log_file, O_RDWR|O_APPEND|O_CREAT,0664))==-1)
	      sendto_one(sptr, ":%s NOTICE %s :Problem opening %s ",
			 me.name, parv[0], hash_log_file);
	  else
	    sendto_one(sptr, ":%s NOTICE %s :Writing hash log to %s ",
		       me.name, parv[0], hash_log_file);
	}
    }
  else
    {
      ch = '\0';
      table = clientTable;
    }

  for (i = 0; i < 10; i++)
    link_pop[i] = 0;
  for (i = 0; i < size; i++)
    {
      tab = &table[i];
      l = tab->links;
      if (showlist)
	{
	/*
	  sendto_one(sptr,
	  "NOTICE %s :Hash Entry:%6d Hits:%7d Links:%6d",
	  parv[0], i, tab->hits, l); */
	  if(out >= 0)
	    {
	      (void)sprintf(result_buf,"Hash Entry:%6d Hits;%7d Links:%6d\n",
			    i, tab->hits, l);
	      (void)write(out,result_buf,strlen(result_buf));
	    }
	}

      if (l > 0)
	{
	  if (l < 10)
	    link_pop[l]++;
	  else
	    link_pop[9]++;
	  used_now++;
	  totlink += l;
	  if (l > deepest)
	    {
	      deepest = l;
	      deeplink = i;
	    }
	}
      else
	link_pop[0]++;
      l = tab->hits;
      if (l)
	{
	  used++;
	  tothits += l;
	  if (l > mosthits)
	    {
	      mosthits = l;
	      mosthit = i;
	    }
	}
    }
  if(showlist && (out >= 0))
     (void)close(out);

  switch((int)ch)
    {
    case 'V' : case 'v' :
      {
	register	aClient	*acptr;
	int	bad = 0, listlength = 0;
	
	for (acptr = client; acptr; acptr = acptr->next) {
	  if (hash_find_client(acptr->name,acptr) != acptr)
	    {
	      if (ch == 'V')
		sendto_one(sptr, "NOTICE %s :Bad hash for %s",
			   parv[0], acptr->name);
	      bad++;
	    }
	  listlength++;
	}
	sendto_one(sptr,"NOTICE %s :List Length: %d Bad Hashes: %d",
		   parv[0], listlength, bad);
      }
    case 'P' : case 'p' :
      for (i = 0; i < 10; i++)
	sendto_one(sptr,"NOTICE %s :Entires with %d links : %d",
		   parv[0], i, link_pop[i]);
      return (0);
    case 'r' :
      {
	register	aClient	*acptr;

	sendto_one(sptr,"NOTICE %s :Rehashing Client List.", parv[0]);
	clear_client_hash_table();
	for (acptr = client; acptr; acptr = acptr->next)
	  (void)add_to_client_hash_table(acptr->name, acptr);
	break;
      }
    case 'R' :
      {
	register	aChannel	*acptr;

	sendto_one(sptr,"NOTICE %s :Rehashing Channel List.", parv[0]);
	clear_channel_hash_table();
	for (acptr = channel; acptr; acptr = acptr->nextch)
	  (void)add_to_channel_hash_table(acptr->chname, acptr);
	break;
      }
    case 'H' :
      if (parc > 2)
	sendto_one(sptr,"NOTICE %s :%s hash to entry %d",
		   parv[0], parv[2],
		   hash_channel_name(parv[2]));
      return (0);
    case 'h' :
      if (parc > 2)
	sendto_one(sptr,"NOTICE %s :%s hash to entry %d",
		   parv[0], parv[2],
		   hash_nick_name(parv[2]));
      return (0);
    case 'n' :
      {
	aClient	*tmp;
	int	max;
	
	if (parc <= 2)
	  return (0);
	l = atoi(parv[2]) % HASHSIZE;
	if (parc > 3)
	  max = atoi(parv[3]) % HASHSIZE;
	else
	  max = l;
	for (;l <= max; l++)
	  for (i = 0, tmp = (aClient *)clientTable[l].list; tmp;
	       i++, tmp = tmp->hnext)
	    {
	      if (parv[1][2] == '1' && tmp != tmp->from)
		continue;
	      sendto_one(sptr,"NOTICE %s :Node: %d #%d %s",
			 parv[0], l, i, tmp->name);
	    }
	return (0);
      }
    case 'N' :
      {
	aChannel *tmp;
	int	max;

	if (parc <= 2)
	  return (0);
	l = atoi(parv[2]) % CHANNELHASHSIZE;
	if (parc > 3)
	  max = atoi(parv[3]) % CHANNELHASHSIZE;
	else
	  max = l;
	for (;l <= max; l++)
	  for (i = 0, tmp = (aChannel *)channelTable[l].list; tmp;
	       i++, tmp = tmp->hnextch)
	    sendto_one(sptr,"NOTICE %s :Node: %d #%d %s",
		       parv[0], l, i, tmp->chname);
	return (0);
      }
    case 'S' :
#endif
      return 0;
#ifdef	DEBUGMODE
    case 'z' :
      {
	register aClient *acptr;

	if (parc <= 2)
	  return 0;
	l = atoi(parv[2]);
	if (l < 256)
	  return 0;
	MyFree((char *)clientTable);
	clientTable = (aHashEntry *)MyMalloc(sizeof(aHashEntry) * l);
	HASHSIZE = l;
	clear_client_hash_table();
	for (acptr = client; acptr; acptr = acptr->next)
	  {
	    acptr->hnext = NULL;
	    (void)add_to_client_hash_table(acptr->name, acptr);
	  }
	sendto_one(sptr, "NOTICE %s :HASHSIZE now %d", parv[0], l);
	break;
      }
    case 'Z' :
      {
	register aChannel *acptr;
	
	if (parc <= 2)
	  return 0;
	l = atoi(parv[2]);
	if (l < 256)
	  return 0;
	MyFree((char *)channelTable);
	channelTable = (aHashEntry *)MyMalloc(sizeof(aHashEntry) * l);
	CHANNELHASHSIZE = l;
	clear_channel_hash_table();
	for (acptr = channel; acptr; acptr = acptr->nextch)
	  {
	    acptr->hnextch = NULL;
	    (void)add_to_channel_hash_table(acptr->chname, acptr);
	  }
	sendto_one(sptr, "NOTICE %s :CHANNELHASHSIZE now %d",
		   parv[0], l);
	break;
      }
    default :
      break;
    }
  sendto_one(sptr,"NOTICE %s :Entries Hashed: %d NonEmpty: %d of %d",
	     parv[0], totlink, used_now, size);
  if (!used_now)
    used_now = 1;
  sendto_one(sptr,"NOTICE %s :Hash Ratio (av. depth): %f %Full: %f",
	     parv[0], (float)((1.0 * totlink) / (1.0 * used_now)),
	     (float)((1.0 * used_now) / (1.0 * size)));
  sendto_one(sptr,"NOTICE %s :Deepest Link: %d Links: %d",
	     parv[0], deeplink, deepest);
  if (!used)
    used = 1;
  sendto_one(sptr,"NOTICE %s :Total Hits: %d Unhit: %d Av Hits: %f",
	     parv[0], tothits, size-used,
	     (float)((1.0 * tothits) / (1.0 * used)));
  sendto_one(sptr,"NOTICE %s :Entry Most Hit: %d Hits: %d",
	     parv[0], mosthit, mosthits);
  sendto_one(sptr,"NOTICE %s :Client hits %d miss %d",
	     parv[0], clhits, clmiss);
  sendto_one(sptr,"NOTICE %s :Channel hits %d miss %d",
	     parv[0], chhits, chmiss);
  return 0;
#endif
}


