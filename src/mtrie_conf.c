/*
 * mtrie_conf.c
 *
 * This is a modified trie, i.e. instead of a node for each character
 * it is a node on each part of a domain name.
 * This turns out to be a reasonable model for handling I lines and K lines
 * within one tree.
 * The tree will have degenerate portions if long domain names are added. 
 * i.e. I:NOMATCH::*@*.this.is.a.long.host.name.com
 * This shouldn't be a major problem.
 *
 *
 * What the code does, is utilize a stack. The stack keeps
 * track of "pieces" of the domain hostname seen as its parsed.
 * i.e. "*.koruna.varner.com" is broken up into pieces of
 *
 * * 		- on stack
 * koruna	- on stack
 * varner	- on stack
 * com		- on stack
 *
 * by the time the host string is parsed, its broken up into pieces
 * on the stack with the TLD on the top of the stack.
 *
 *
 * The interesting thing about this code is, its actually 
 * an LALR(1) parser, where the "sentences" are hostnames.
 * There are tokens, there is a stack, and there is a state machine.
 * cool huh?
 *
 * Copyright (c) Diane Bruce, db@db.net , September 5 1998
 * May be used for any non commercial purpose including ircd.
 * Please let me know if you are going to use it somewhere else. I'm just
 * curious. Do not use it in a commercial program without my permission.
 * Use it at your own risk.
 *
 * Diane Bruce -db (db@db.net)
 *
 */

#include <string.h>
	/* WHAT TO DO HERE?  guess we'll find out --ns */
#undef STDLIBH
#include "sys.h"
#include "numeric.h"
#include "common.h"
#include "struct.h"

#include "h.h"

#include "mtrie_conf.h"

#if !defined(SYSV) && !defined(SOL20)
#define memmove(x,y,N) bcopy(y,x,N)
#endif

#ifndef lint
static char *version="$Id$";
#endif /* lint */

#define MAXPREFIX (HOSTLEN+USERLEN+10)

/* internally defined functions */

static int sortable(char *,char *);
static void tokenize_and_stack(char *,char *);
static void create_sub_mtrie(DOMAIN_LEVEL *,aConfItem *,int,char *);
static aConfItem *find_sub_mtrie(DOMAIN_LEVEL *,char *,char *,int);
static int wildcmp(char *,char *);
static char *show_iline_prefix(aClient *,aConfItem *,char *);
static DOMAIN_PIECE *find_or_add_host_piece(DOMAIN_LEVEL *,int,char *);
static DOMAIN_PIECE *find_host_piece(DOMAIN_LEVEL *,int,char *,char *);
static void find_or_add_user_piece(DOMAIN_PIECE *,aConfItem *,int,char *);
static aConfItem *find_user_piece(DOMAIN_PIECE *,char *,char *);

static aConfItem *look_in_unsortable_ilines(char *,char *);
static aConfItem *look_in_unsortable_klines(char *,char *);
static aConfItem *find_wild_card_iline(char *);

static void report_sub_mtrie(aClient *sptr,int,DOMAIN_LEVEL *);
static void report_unsortable_klines(aClient *,char *);
static void clear_sub_mtrie(DOMAIN_LEVEL *);
static aConfItem *find_matching_ip_i_line(unsigned long);

void add_to_ip_ilines(aConfItem *);

/* internally used variables, these can all be static */

int stack_pointer;		/* dns piece stack */
static char *dns_stack[MAX_TLD_STACK];
/* null *sigh* used all over the place in ircd. who am I to argue right now? */
static  char	null[] = "<NULL>";

DOMAIN_LEVEL *trie_list=(DOMAIN_LEVEL *)NULL;
DOMAIN_LEVEL *first_kline_trie_list=(DOMAIN_LEVEL *)NULL;
int saved_stack_pointer;

aConfItem *unsortable_list_ilines = (aConfItem *)NULL;
aConfItem *unsortable_list_klines = (aConfItem *)NULL;
aConfItem *wild_card_ilines = (aConfItem *)NULL;

/*
 * There is some argument for including the integer ip and needed mask
 * inside aConfItem, instead of "wrapping" it. This would change
 * the code handling for D lines as well (s_conf.c)
 * -db
 */

/* The observant will note, that this is an identical structure
 * to DLINE_ENTRY in s_conf.c, perhaps a common struct could be
 * defined in struct.h ..
 */

typedef struct i_line_ip_entry
{
  unsigned long ip;
  unsigned long mask;
  aConfItem *conf_entry;
  struct i_line_ip_entry *next;
}I_LINE_IP_ENTRY;

I_LINE_IP_ENTRY *ip_i_lines=(I_LINE_IP_ENTRY *)NULL;

/* add_mtrie_conf_entry
 *
 * inputs	- 
 * output	- NONE
 * side effects	-
 */

void add_mtrie_conf_entry(aConfItem *aconf,int flags)
{
  DOMAIN_LEVEL *cur_level;
  DOMAIN_PIECE *cur_piece;
  DOMAIN_PIECE *last_piece;
  char tokenized_host[HOSTLEN+1];
  char *cur_dns_piece;
  int index;

  stack_pointer = 0;

  /* check to see if its a kline on user@ip.ip.ip.ip/mask
   * or user@ip.ip.ip.* or user@ip.ip.ip.ip
   */

  if(host_is_legal_ip(aconf->host) && (aconf->status & CONF_KILL))
    {
      add_to_dline_hash(aconf);
      return;
    }

  switch(sortable(tokenized_host,aconf->host))
    {
    case 0:
    case 1:

      if(aconf->status & CONF_CLIENT)
	{
	  if(unsortable_list_ilines)
	    {
	      aconf->next = unsortable_list_ilines;
	      unsortable_list_ilines = aconf;
	    }
	  else
	    unsortable_list_ilines = aconf;
	}
      else
	{
	  if(unsortable_list_klines)
	    {
	      aconf->next = unsortable_list_klines;
	      unsortable_list_klines = aconf;
	    }
	  else
	    unsortable_list_klines = aconf;
	}
      return;
      break;

    case -2:
      if(aconf->status & CONF_CLIENT)
	{
	  if(wild_card_ilines)
	    {
	      aconf->next = wild_card_ilines;
	      wild_card_ilines = aconf;
	    }
	  else
	    wild_card_ilines = aconf;
	}
      else
	{
	  if(unsortable_list_klines)
	    {
	      aconf->next = unsortable_list_klines;
	      unsortable_list_klines = aconf;
	    }
	  else
	    unsortable_list_klines = aconf;
	}
      return;
      break;

    case -1:
      break;
    }

  if(trie_list == (DOMAIN_LEVEL *)NULL)
    {
      trie_list = (DOMAIN_LEVEL *)MyMalloc(sizeof(DOMAIN_LEVEL));
      memset((void *)trie_list,0,sizeof(DOMAIN_LEVEL));
    }

  /* now, start generating the sub mtrie tree */

  create_sub_mtrie(trie_list,aconf,flags,aconf->host);
}

/*
 * create_sub_mtrie
 *
 * inputs	- DOMAIN_LEVEL pointer
 *		- flags as integer
 *		- full hostname
 *		- username
 *		- reason (if kline)
 *
 * create a sub mtrie tree entry
 */

static void create_sub_mtrie(DOMAIN_LEVEL *cur_level,
			     aConfItem *aconf,
			     int flags,
			     char *host)
{
  char *cur_dns_piece;
  DOMAIN_PIECE *last_piece;
  DOMAIN_PIECE *cur_piece;

  cur_dns_piece = dns_stack[--stack_pointer];
  cur_piece = find_or_add_host_piece(cur_level,flags,cur_dns_piece);

  if(stack_pointer == 0)
    {
      (void)find_or_add_user_piece(cur_piece, aconf, flags, cur_dns_piece);
      return;
    }

  last_piece = cur_piece;

  cur_level = last_piece->next_level;

  if(cur_level == (DOMAIN_LEVEL *)NULL)
    {
      cur_level = (DOMAIN_LEVEL *)MyMalloc(sizeof(DOMAIN_LEVEL));
      memset((void *)cur_level,0,sizeof(DOMAIN_LEVEL));
      last_piece->next_level = cur_level;
    }
  create_sub_mtrie(cur_level,aconf,flags,host);
}


/* find_or_add_host_piece
 *
 * inputs	- pointer to current level 
 *		- piece of domain name being looked for
 *		- username
 * output	- pointer to next DOMAIN_PIECE to use
 * side effects -
 *
 */

static DOMAIN_PIECE *find_or_add_host_piece(DOMAIN_LEVEL *level_ptr,
				     int flags,char *host_piece)
{
  DOMAIN_PIECE *piece_ptr;
  DOMAIN_PIECE *cur_piece;
  DOMAIN_PIECE *new_ptr;
  DOMAIN_PIECE *last_ptr;
  DOMAIN_PIECE *ptr;
  int index;

  index = *host_piece&(MAX_PIECE_LIST-1);
  piece_ptr = level_ptr->piece_list[index];

  if(piece_ptr == (DOMAIN_PIECE *)NULL)
    {
      cur_piece = (DOMAIN_PIECE *)MyMalloc(sizeof(DOMAIN_PIECE));
      memset((void *)cur_piece,0,sizeof(DOMAIN_PIECE));
      DupString(cur_piece->host_piece,host_piece);
      level_ptr->piece_list[index] = cur_piece;
      cur_piece->flags |= flags;
      return(cur_piece);
    }

  last_ptr = (DOMAIN_PIECE *)NULL;

  for(ptr=piece_ptr; ptr; ptr = ptr->next_piece)
    {
      if(!strcasecmp(ptr->host_piece,host_piece))
	{
	  ptr->flags |= flags;
	  return(ptr);
	}
      last_ptr = ptr;
    }

  if(last_ptr)
    {
      new_ptr = (DOMAIN_PIECE *)MyMalloc(sizeof(DOMAIN_PIECE));
      memset((void *)new_ptr,0,sizeof(DOMAIN_PIECE));
      DupString(new_ptr->host_piece,host_piece);

      last_ptr->next_piece = new_ptr;
      new_ptr->flags |= flags;
      return(new_ptr);
    }
  else
    {
      sendto_realops("Bug: in find_or_add_host_piece. yay.");
      return(NULL);
    }
  /* NOT REACHED */
}

/* find_or_add_user_piece
 *
 * inputs	- pointer to current level 
 *		- piece of domain name being looked for
 *		- flags
 *		- username
 *		- reason
 * output	- pointer to next DOMAIN_LEVEL to use
 * side effects -
 *
 */

static void find_or_add_user_piece(DOMAIN_PIECE *piece_ptr,
				   aConfItem *aconf,
				   int flags,
				   char *host_piece)
{
  DOMAIN_PIECE *ptr;
  DOMAIN_PIECE *new_ptr;
  DOMAIN_PIECE *last_ptr;
  aConfItem *found_aconf;
  char *user;

#ifdef DEBUG_MTRIE
  if(aconf->status & CONF_KILL)
    sendto_realops("DEBUG: found kline");
#endif

  last_ptr = (DOMAIN_PIECE *)NULL;
  user = aconf->name;

  if(user[0] == '*' && user[1] == '\0')
    {
      if(!(piece_ptr->wild_conf_ptr))
	 {
	   aconf->status |= flags;
	   piece_ptr->wild_conf_ptr = aconf;
	   piece_ptr->flags |= flags;
	   return;
	 }
      else
	{
	  found_aconf = piece_ptr->wild_conf_ptr;

	  if(found_aconf->status & CONF_ELINE)
	    {
	      /* if requested kline aconf =exactly=
	       * matches an already present aconf
	       * discard the requested K line
	       * it should also be NOTICE'd back to the 
	       * oper who did the K-line, if I'm not 
	       * reading the conf file.
	       */

	      free_conf(aconf);	/* toss it in the garbage */

	      found_aconf->status |= flags;
	      found_aconf->status &= ~CONF_KILL;
	      piece_ptr->flags |= flags;
	      piece_ptr->flags &= ~CONF_KILL;
	    }
	  else if(flags & CONF_KILL)
	    {
	      if(found_aconf->clients)
		found_aconf->status |= CONF_ILLEGAL;
	      else
		free_conf(found_aconf);
	      piece_ptr->wild_conf_ptr = aconf;
	    }
	  else if(flags & CONF_CLIENT)	/* I line replacement */
	    {
	      /* another I line/CONF_CLIENT exactly matching this
	       * toss the new one into the garbage
	       */
	      free_conf(aconf);	
	    }
	  found_aconf->status |= flags;
	  piece_ptr->flags |= flags;
	  return;
	}
    }

  /*
   * if the piece_ptr->conf_ptr is NULL, then its the first piece_ptr
   * being added. The flags in piece_ptr will have already been set
   * but OR them in again anyway. aconf->status will also already have
   * the right flags. hint, these are optimization places for later.
   *
   * -Dianora
   */

  for( ptr = piece_ptr; ptr; ptr = ptr->next_piece)
    {
      if(!ptr->conf_ptr)
	{
	  aconf->status |= flags;	/* redundant -db */
	  piece_ptr->flags |= flags;	/* redundant -db */
	  ptr->conf_ptr = aconf;
	  return;
	}

      found_aconf=ptr->conf_ptr;

      if( (!matches(ptr->host_piece,host_piece)) &&
	  (!matches(found_aconf->name,user)) )
	{
	  found_aconf->status |= flags;
	  piece_ptr->flags |= flags;

	  if(found_aconf->status & CONF_ELINE)
	    {
	      free_conf(aconf);		/* toss it in the garbage */
	      found_aconf->status &= ~CONF_KILL;
	      piece_ptr->flags &= ~CONF_KILL;
	    }
	  else if(found_aconf->status & CONF_CLIENT)
	    {
	      if(flags & CONF_CLIENT)
		free_conf(aconf);	/* toss new I line into the garbage */
	      else
		{
		  /* Its a K line */

		  if(found_aconf->clients)
		    found_aconf->status |= CONF_ILLEGAL;
		  else
		    free_conf(found_aconf);
		  ptr->conf_ptr = aconf;
		}
	    }
	  return;
	}
      last_ptr = ptr;
   }

  if(last_ptr)
    {
      new_ptr = (DOMAIN_PIECE *)MyMalloc(sizeof(DOMAIN_PIECE));
      memset((void *)new_ptr,0,sizeof(DOMAIN_PIECE));
      DupString(new_ptr->host_piece,host_piece);
      new_ptr->conf_ptr = aconf;
      last_ptr->next_piece = new_ptr;
    }
  else
    {
      /*
       */
      sendto_realops("Bug in mtrie_conf.c last_ptr found NULL");
    }

  return;
}

/* find_user_piece
 *
 * inputs	- pointer to current level 
 *		- piece of domain name being looked for
 *		- username
 * output	- pointer to next DOMAIN_LEVEL to use
 * side effects -
 *
 */

static aConfItem *find_user_piece(DOMAIN_PIECE *piece_ptr,
		     char *host_piece,char *user)
{
  DOMAIN_PIECE *ptr;
  aConfItem *aconf=(aConfItem *)NULL;
  aConfItem *wild_aconf=(aConfItem *)NULL;
  int match = NO;

  wild_aconf = piece_ptr->wild_conf_ptr;

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: find_user_piece host_piece[%s] user [%s] piece_ptr %X",
		 host_piece,user,piece_ptr);
#endif

  for(ptr=piece_ptr; ptr; ptr=ptr->next_piece)
    {
      if(!ptr->conf_ptr)
	continue;
      aconf=ptr->conf_ptr;
#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: host_piece [%s] ptr->host_piece [%s]",
		     host_piece,ptr->host_piece);
      sendto_realops("DEBUG: user [%s] aconf->name [%s]",
		     user,aconf->name);
#endif
      if( (!matches(ptr->host_piece,host_piece)) &&
	  (!matches(aconf->name,user)))
	{
#ifdef DEBUG_MTRIE
	  sendto_realops("DEBUG: found match aconf->name [%s] user [%s]",
			 aconf->name, user);
#endif
	  match = YES;
	  if(aconf->status & CONF_ELINE)
	    {
	      return(aconf);
	    }
	}
    }

  if(!match)
    aconf = (aConfItem *)NULL;

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: find_user_piece aconf = %X now looking for wild_aconf",aconf);
  if(aconf)
    sendto_realops("DEBUG: aconf->name [%s]",aconf->name);
  sendto_realops("DEBUG: wild_aconf %X ",wild_aconf);
  if(wild_aconf)
    sendto_realops("DEBUG: wild_aconf->name [%s]",wild_aconf->name);
#endif

  if(!aconf)
    return(wild_aconf);

  /*
   * ok. if there was an exact aconf found, if there
   * is a "wild_aconf" i.e. *@... pattern
   * if so, the exact aconf over-rules it if it has an E line
   * and the wild_aconf is a K line
   * if the exact aconf is a K line but the wild aconf is an E line
   * then the wild aconf over-rules the K line aconf.
   * finally, if there isn't an aconf, return the wild_aconf
   * if there isn't a wild_aconf, but there is an aconf, return the aconf
   *
   * if neither is found, aconf, wild_aconf default to NULL and thats
   * what will be returned.
   *
   * -Dianora
   */

  if(wild_aconf)
    {
#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: wild_aconf found %X",wild_aconf);
#endif
      if((wild_aconf->status & CONF_ELINE) && (aconf->status & CONF_KILL))
	return(wild_aconf);
      else if((wild_aconf->status & CONF_KILL) && (aconf->status & CONF_ELINE))
	return(aconf);
    }
  return(aconf);
}

/* find_host_piece
 *
 * inputs	- pointer to current level 
 *		- piece of domain name being looked for
 *		- usename
 * output	- pointer to next DOMAIN_LEVEL to use
 * side effects -
 *
 */

static DOMAIN_PIECE *find_host_piece(DOMAIN_LEVEL *level_ptr,int flags,
				     char *host_piece,char *user)
{
  DOMAIN_PIECE *ptr;
  DOMAIN_PIECE *piece_ptr;
  int index;
  
  index = *host_piece&(MAX_PIECE_LIST-1);
  piece_ptr = level_ptr->piece_list[index];

  for(ptr=piece_ptr;ptr;ptr=ptr->next_piece)
    {
      if(!wildcmp(ptr->host_piece,host_piece) && (ptr->flags & flags))
	{
	  return(ptr);
	}
    }

  index = '*'&(MAX_PIECE_LIST-1);
  piece_ptr = level_ptr->piece_list[index];

  for(ptr=piece_ptr;ptr;ptr=ptr->next_piece)
    {
      if( ((ptr->host_piece[0] == '*') && (ptr->host_piece[1] == '\0'))
	  && (ptr->flags & flags))
	{
	  return(ptr);
	}
    }

  return((DOMAIN_PIECE *)NULL);
}


/* find_matching_mtrie_conf
 *
 * inputs	- host name
 *		- user name
 * output	- pointer to aConfItem that corresponds to user/host pair
 *		  or NULL if not found
 * side effects	- NONE

 */

aConfItem *find_matching_mtrie_conf(char *host,char *user,
				    unsigned long ip)
{
  DOMAIN_PIECE *cur_piece;
  aConfItem *iline_aconf_unsortable=(aConfItem *)NULL;
  aConfItem *iline_aconf=(aConfItem *)NULL;
  aConfItem *kline_aconf=(aConfItem *)NULL;
  aConfItem *ip_iline_aconf=(aConfItem *)NULL;
  char tokenized_host[HOSTLEN+1];
  int flags=CONF_CLIENT;
  int top_of_stack;

  /* I look in the unsortable i line list first, to find
   * special cases like *@*ppp* first
   */

  iline_aconf_unsortable = look_in_unsortable_ilines(host,user);

  /* an E lined I line is always accepted first
   * there is no point checking for a k-line
   */

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: iline_aconf_unsortable %X",iline_aconf_unsortable);
#endif

  if(iline_aconf_unsortable && (iline_aconf_unsortable->status & CONF_ELINE))
    return(iline_aconf_unsortable);

  /* I now look in the mtrie tree, if I found an I line
   * in the unsortable, an E line is going to over-rule it.
   * Otherwise, I will find the bulk of the I lines here,
   * in the mtrie tree.
   */

  stack_pointer = 0;
  tokenize_and_stack(tokenized_host,host);
  top_of_stack = stack_pointer;

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: top_of_stack %d trie_list %X",top_of_stack,trie_list);
#endif

  if(trie_list)
    {
      saved_stack_pointer = -1;
      first_kline_trie_list = (DOMAIN_LEVEL *)NULL;

      iline_aconf = find_sub_mtrie(trie_list,host,user,CONF_CLIENT);

      /* If either an E line or K line is found, there is no need
       * to go any further. If there wasn't an I line found,
       * the client has no access anyway, so there is no point checking
       * for a K line.
       * If the client had an E line in the unsortable list, I've already
       * returned that aConfItem and I'm not even at this spot in the code.
       * -Dianora
       */

      /* If nothing found in the mtrie,
       * try looking for a top level domain match
       */

#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: after mtrie search iline_aconf %X",iline_aconf);
#endif

      if(!iline_aconf)
	iline_aconf= find_wild_card_iline(user);

#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: after find_wild_card_iline search iline_aconf %X",iline_aconf);
#endif

      /* If its an E line, found from either the mtrie or the top level
       * domain "*" return it. If its a K line found from the mtrie
       * return it.
       */

      if(iline_aconf && (iline_aconf->status & (CONF_ELINE|CONF_KILL)))
	return(iline_aconf);
    }
  else
    {
      iline_aconf= find_wild_card_iline(user);

#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: after find_wild_card_iline search iline_aconf %X",iline_aconf);
#endif

      /* If its an E line, found from either the mtrie or the top level
       * domain "*" return it. If its a K line found from the mtrie
       * return it.
       */

      if(iline_aconf && (iline_aconf->status & (CONF_ELINE|CONF_KILL)))
	return(iline_aconf);
    }

  /* always default to an I line found in the unsortable list */

  if(iline_aconf_unsortable)
    iline_aconf = iline_aconf_unsortable;

  /* Keep as close to the original semantics as possible, 
   * IP mask OR dns name
   */

  if(ip && host_is_legal_ip(host))
    {
      ip_iline_aconf = find_matching_ip_i_line(ip);
      if(ip_iline_aconf)
	iline_aconf = ip_iline_aconf;
    }

  /* If there is no I line, there is no point checking for a K line now
   * is there? -Dianora
   */

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: found iline %X for host [%s] user [%s]",
		 iline_aconf,host,user);
#endif

  if(!iline_aconf)
    return((aConfItem *)NULL);

  /* I have an I line, now I have to see if it gets
   * over-ruled by a K line somewhere else in the tree.
   * Note, that if first_kline_trie_list is non NULL
   * then trie_list had to have been non NULL as well.
   * call me paranoid.
   * Remember again, if any of the I lines
   * found also had an E line, I've already returned it
   * and not bothering with the K line search - Dianora
   */

  if(trie_list && first_kline_trie_list)
    {
      stack_pointer = saved_stack_pointer;
#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: about to look for kline");
#endif
      kline_aconf = find_sub_mtrie(first_kline_trie_list,host,user,CONF_KILL);
#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: found kline_aconf %X",kline_aconf);
      if(kline_aconf)
	sendto_realops("DEBUG: host [%s] user [%s]",
		      kline_aconf->host,kline_aconf->name);
#endif
    }
  else
    kline_aconf = (aConfItem *)NULL;

  /* I didn't find a kline in the mtrie, I'll try the unsortable list */

  if(!kline_aconf)
    kline_aconf = look_in_unsortable_klines(host,user);

  /* Try an IP hostname ban */

  if(!kline_aconf)
    kline_aconf = find_user_host_in_dline_hash(ip,user,CONF_KILL);

  /* If this client isn't k-lined return the I line found */

  if(kline_aconf)
    return(kline_aconf);
  else
    return(iline_aconf);
}

/*
 * find_sub_mtrie 
 * inputs	- pointer to current domain level 
 * 		- hostname piece
 *		- username
 *		- flags flags to match for
 * output	- pointer to aConfItem or NULL
 * side effects	-
 */

static aConfItem *find_sub_mtrie(DOMAIN_LEVEL *cur_level,
				 char *host,char *user,int flags)
{
  DOMAIN_PIECE *cur_piece;
  char *cur_dns_piece;
  int index;
  aConfItem *aconf;

  cur_dns_piece = dns_stack[--stack_pointer];

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: find_sub_mtrie() host [%s] user [%s] flags %X",
		 host,user,flags);
  sendto_realops("DEBUG: cur_dns_piece = [%s]", cur_dns_piece);
#endif

  if(flags & CONF_KILL)
    {
      /* looking for CONF_KILL, look first for a kline at this level */

      cur_piece = find_host_piece(cur_level,flags,"*",user);
      if(cur_piece)
	{
	  aconf = find_user_piece(cur_piece,"*",user);
	  if(aconf && aconf->status & CONF_KILL)
	    return(aconf);
	}
      cur_piece = find_host_piece(cur_level,flags,cur_dns_piece,user);
      if(cur_piece == (DOMAIN_PIECE *)NULL)
	return((aConfItem *)NULL);
    }
  else
    {
      /* looking for CONF_CLIENT, so descend deeper */

      cur_piece = find_host_piece(cur_level,flags,cur_dns_piece,user);

      if(cur_piece == (DOMAIN_PIECE *)NULL)
	return((aConfItem *)NULL);
    }

  if((cur_piece->flags & CONF_KILL) && (!first_kline_trie_list))
    {
#ifdef DEBUG_MTRIE
      sendto_realops("DEBUG: found kline stack_pointer %d [%s] mtrie %X",
		     stack_pointer+1,cur_dns_piece,trie_list); 
#endif
      first_kline_trie_list = cur_level;
      saved_stack_pointer = stack_pointer+1;
    }

  if(stack_pointer == 0)
    return(find_user_piece(cur_piece,cur_dns_piece,user));

  if(cur_piece->next_level)
    cur_level = cur_piece->next_level;
  else
    return(find_user_piece(cur_piece,cur_dns_piece,user));

  return(find_sub_mtrie(cur_level,host,user,flags));
}


/*
 * This function decides whether a string may be used with ordered lists.
 * -1 means the string has to be reversed. A string that can't be put in
 * an ordered list yields 0 (yes, a piece of Soleil)
 *
 * a little bit rewritten, and yes. I tested it. it is faster.
 * 	- Dianora
 *
 * modified for use with mtrie_conf.c
 */


static int sortable(char *tokenized,char *p)
{
  int  state=0;

  if (!p)
    return(0);			/* NULL patterns aren't allowed in ordered
				 * lists (will have to use linear functions)
				 * -Sol
				 *
				 * uh, if its NULL then nothing can be done
				 * with it -Dianora
				 */

  if (strchr(p, '?'))
    return(0);			/* reject strings with '?' as non-sortable
				 *  whoever uses '?' patterns anyway ? -Sol
				 */


  tokenized = tokenized+HOSTLEN;
  *tokenized-- = '\0';

  if((*p == '*') && (*(p+1) == '\0'))	/* special case a single '*' */
    return(-2);

  FOREVER
    {
      switch(state)
	{
	case 0:
	  if(*p == '*')
	    {
	      *tokenized = *p;
	      state = 1;
	    }
	  else if(*p == '.')
	    {
	      *tokenized = '\0';
	      dns_stack[stack_pointer++] = tokenized+1;
	    }
	  else
	    {
	      *tokenized = *p;
	      state = 2;
	    }
	  break;

	case 1:
	  if(!*p)		/* '*' followed by anything other than '*' */
	    {
	      *tokenized = *p;
	      dns_stack[stack_pointer++] = tokenized;
	      return(-1);	/* then by null terminator is sortable */
	    }
	  else if(*p == '*')	/* '*' followed by another '*' is unsortable */
	    return(0);
	  else if(*p == '.')
	    {
	      *tokenized = '\0';
	      dns_stack[stack_pointer++] = tokenized+1;
	    }
	  else
	    {
	      *tokenized = *p;
	      state = 3;	/*  '*' followed by non '*' sit in state 3 */
	    }
	  break;
	 
	case 2:
	  if(*p == '\0')	/* state 2, sit here if no '*' seen and */
	    {
	      *tokenized = *p;
	      dns_stack[stack_pointer++] = tokenized+1;
	      return(-1);	/* if null terminator seen, its sortable */
	    }
	  else if(*p == '*')	/* '*' on end of string is still fine */
	    {
	      if(*(p+1) == '\0')  /* use look ahead, if p+1 is null */
		return(1);	/* its sortable in forward order */
	      else		/* else its "blah*blah" which is not sortable*/
		return(0);
	    }
	  else if (*p == '.')
	    {
	      *tokenized = '\0';
	      dns_stack[stack_pointer++] = tokenized+1;
	    }
	  else
	    *tokenized = *p;
	  break;
	 
	case 3:
	  if(*p== '\0')		/* I got a '*' already then "blah" */
	    {
	      *tokenized = *p;
	      dns_stack[stack_pointer++] = tokenized+1;
	      return(-1);	/* so its a "*blah" which is sortable */
	    }
	  else if(*p == '*')	/* I got a '*' already, so its not sortable */
	    return(0);
	  else if(*p == '.')
	    {
	      *tokenized = '\0';
	      dns_stack[stack_pointer++] = tokenized+1;
	    }
	  else
	    *tokenized = *p;
	  break;		/* else just stick in state 3 */

	default:
	  *tokenized = *p;
	  state = 0;
	  break;
	}
      tokenized--;
      p++;
    }
}

/*
 * tokenize_and_stack
 *
 * inputs	- pointer to where reversed output
 * output	- none
 * side effects	-
 * This function tokenizes the input, reversing it onto
 * a dns stack. Basically what sortable() does, but without
 * scanning for sortability.
 */

static void tokenize_and_stack(char *tokenized,char *p)
{
  if (!p)
    return;

  tokenized = tokenized+HOSTLEN;
  *tokenized-- = '\0';

  while(*p)
    {
      if(*p == '.')
	{
	  *tokenized = '\0';
	  dns_stack[stack_pointer++] = tokenized+1;
	}
      else
	{
	  *tokenized = *p;
	}

      tokenized--;
      p++;
    }
  dns_stack[stack_pointer++] = tokenized+1;
}

/*
 * look_in_unsortable_ilines()
 *
 * inputs	- host name
 * 		- username
 * output	- aConfItem pointer or NULL
 * side effects -
 *
 * scan the link list of unsortable patterns
 */

static aConfItem *look_in_unsortable_ilines(char *host,char *user)
{
  aConfItem *found_conf;

  for(found_conf=unsortable_list_ilines;found_conf;found_conf=found_conf->next)
    {
      if(!matches(found_conf->host,host) && !matches(found_conf->name,user))
	 return(found_conf);
    }
  return((aConfItem *)NULL);
}

/*
 * look_in_unsortable_klines()
 *
 * inputs	- host name
 * 		- username
 * output	- aConfItem pointer or NULL
 * side effects -
 *
 * scan the link list of unsortable patterns
 */

static aConfItem *look_in_unsortable_klines(char *host,char *user)
{
  aConfItem *found_conf;

  for(found_conf=unsortable_list_klines;found_conf;found_conf=found_conf->next)
    {
      if(!matches(found_conf->host,host) && !matches(found_conf->name,user))
	return(found_conf);
    }
  return((aConfItem *)NULL);
}

/*
 * find_wild_card_iline()
 *
 * inputs	- username
 * output	- aConfItem pointer or NULL
 * side effects -
 *
 * scan the link list of top level domain *
 */

static aConfItem *find_wild_card_iline(char *user)
{
  aConfItem *found_conf;

  for(found_conf=wild_card_ilines;found_conf;found_conf=found_conf->next)
    {
      if(!(found_conf->status & CONF_CLIENT))	/* shouldn't happen */
	continue;

      if(!matches(found_conf->name,user))
	return(found_conf);
    }
  return((aConfItem *)NULL);
}

/*
 * report_matching_host_klines
 *
 * inputs	- pointer to aClient to send reply to
 *		- hostname to match
 * output	- NONE
 * side effects	-
 * all klines in the same domain as hostname given are listed.
 *
 * two_letter_tld is for future use.
 * The idea is to descend one level deeper to list two letter tld
 * K lines.
 */

void report_matching_host_klines(aClient *sptr,char *host)
{
  DOMAIN_PIECE *cur_piece;
  DOMAIN_LEVEL *cur_level;
  char *cur_dns_piece;
  int index;
  char *p;
  int two_letter_tld = 0;
  char tokenized_host[HOSTLEN+1];

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: report_matching_klines host = [%s]", host);
#endif

  if (strlen(host) > (size_t) HOSTLEN)
    return;

  if(host_is_legal_ip(host))
    {
      report_unsortable_klines(sptr,host);
      return;
    }

  stack_pointer = 0;
  tokenize_and_stack(tokenized_host,host);

  p = host;

  while(*p)
    p++;
  p -= 4;	
  if(p[3] == '\0')
    two_letter_tld = YES;

#ifdef DEBUG_MTRIE
  sendto_realops("DEBUG: host [%s] p = [%s] two_letter_tld = %d",
		 host,p,two_letter_tld);
#endif

  cur_dns_piece = dns_stack[--stack_pointer];
  if(!cur_dns_piece)
    return;

  cur_piece = find_host_piece(trie_list,CONF_KILL,cur_dns_piece,"*");

  if(cur_piece == (DOMAIN_PIECE *)NULL)
    return;

  if(!(cur_piece->flags & CONF_KILL))
    return;

  if(cur_piece->next_level)
    cur_level = cur_piece->next_level;
  else
    return;

  cur_dns_piece = dns_stack[--stack_pointer];
  if(!cur_dns_piece)
    return;

  cur_piece = find_host_piece(cur_level,CONF_KILL,cur_dns_piece,"*");

  if(cur_piece == (DOMAIN_PIECE *)NULL)
    return;

  if(!(cur_piece->flags & CONF_KILL))
    return;

  if(cur_piece->next_level)
    cur_level = cur_piece->next_level;
  else
    return;

  report_sub_mtrie(sptr,CONF_KILL,cur_level);
  report_dline_hash(sptr,CONF_KILL);
  report_unsortable_klines(sptr,host);

}

/*
 * report_unsortable_klines()
 *
 * inputs	- pointer to client pointer to report to
 *		- pointer to host name needed
 * output	- NONE
 * side effects	- report the klines in the unsortable list
 */

static void report_unsortable_klines(aClient *sptr,char *need_host)
{
  aConfItem *found_conf;
  char *host;
  char *pass;
  char *name;
  int port;

  for(found_conf = unsortable_list_klines;
      found_conf;found_conf=found_conf->next)
    {
      host = BadPtr(found_conf->host) ? null : found_conf->host;
      pass = BadPtr(found_conf->passwd) ? null : found_conf->passwd;
      name = BadPtr(found_conf->name) ? null : found_conf->name;
      port = (int)found_conf->port;
      
      if(!match(host,need_host))
	{
	  sendto_one(sptr, rpl_str(RPL_STATSKLINE), me.name,
		     sptr->name, 'K', host,
		     name, pass);
	}
    }
}


/*
 * report_mtrie_conf_links()
 *
 * inputs	- aClient pointer
 *		- flags type either CONF_KILL or CONF_CLIENT
 * output	- none
 * side effects	- report I lines/K lines found in the mtrie
 */

void report_mtrie_conf_links(aClient *sptr, int flags)
{
  aConfItem *found_conf;
  char *host;
  char *pass;
  char *name;
  char *mask;
  int  port;
  char c;		/* conf char used for CONF_CLIENT only */

  if(trie_list)
    report_sub_mtrie(sptr,flags,trie_list);

  /* If requesting I lines do this */
  if(flags & CONF_CLIENT)
    {
      for(found_conf = unsortable_list_ilines;
	  found_conf;found_conf=found_conf->next)
	{
	  host = BadPtr(found_conf->host) ? null : found_conf->host;
	  pass = BadPtr(found_conf->passwd) ? null : found_conf->passwd;
	  name = BadPtr(found_conf->name) ? null : found_conf->name;
	  mask = BadPtr(found_conf->mask) ? null : found_conf->mask;
	  port = (int)found_conf->port;

	  c = 'I';
#ifdef LITTLE_I_LINES
	  if(IsConfLittleI(found_conf))
	    c = 'i';
#endif
	  sendto_one(sptr, rpl_str(RPL_STATSILINE), me.name,
		     sptr->name,
		     c,
		     mask,
		     show_iline_prefix(sptr,found_conf,name),
		     host,
		     port,
		     get_conf_class(found_conf));
	}

      for(found_conf = wild_card_ilines;
	  found_conf;found_conf=found_conf->next)
	{
	  host = BadPtr(found_conf->host) ? null : found_conf->host;
	  pass = BadPtr(found_conf->passwd) ? null : found_conf->passwd;
	  name = BadPtr(found_conf->name) ? null : found_conf->name;
	  mask = BadPtr(found_conf->mask) ? null : found_conf->mask;
	  port = (int)found_conf->port;

	  if(!(found_conf->status&CONF_CLIENT))
	    continue;

	  c = 'I';
#ifdef LITTLE_I_LINES
	  if(IsConfLittleI(found_conf))
	    c = 'i';
#endif
	  sendto_one(sptr, rpl_str(RPL_STATSILINE), me.name,
		     sptr->name,
		     c,
		     mask,
		     show_iline_prefix(sptr,found_conf,name),
		     host,
		     port,
		     get_conf_class(found_conf));
	}
    }
  else
    {
      report_dline_hash(sptr,CONF_KILL);

      for(found_conf = unsortable_list_klines;
	  found_conf;found_conf=found_conf->next)
	{
	  host = BadPtr(found_conf->host) ? null : found_conf->host;
	  pass = BadPtr(found_conf->passwd) ? null : found_conf->passwd;
	  name = BadPtr(found_conf->name) ? null : found_conf->name;
	  port = (int)found_conf->port;

	  sendto_one(sptr, rpl_str(RPL_STATSKLINE), me.name,
		     sptr->name, 'K', host,
		     name, pass);
	}
    }
}

/*
 * show_iline_prefix()
 *
 * inputs	- pointer to aClient requesting output
 *		- pointer to aConfItem 
 *		- name to which iline prefix will be prefixed to
 * output	- pointer to static string with prefixes listed in ascii form
 * side effects	- NONE
 */

static char *show_iline_prefix(aClient *sptr,aConfItem *aconf,char *name)
{
  static char prefix_of_host[MAXPREFIX];
  char *prefix_ptr;

  prefix_ptr = prefix_of_host;
 
  if (IsNoTilde(aconf))
    *prefix_ptr++ = '-';
  if (IsLimitIp(aconf))
    *prefix_ptr++ = '!';
  if (IsNeedIdentd(aconf))
    *prefix_ptr++ = '+';
  if (IsPassIdentd(aconf))
    *prefix_ptr++ = '$';
  if (IsNoMatchIp(aconf))
    *prefix_ptr++ = '%';

#ifdef E_LINES_OPER_ONLY
  if(IsAnOper(sptr))
#endif
    if (IsConfElined(aconf))
      *prefix_ptr++ = '^';

#ifdef B_LINES_OPER_ONLY
  if(IsAnOper(sptr))
#endif
    if (IsConfBlined(aconf))
      *prefix_ptr++ = '&';

#ifdef F_LINES_OPER_ONLY
  if(IsAnOper(sptr))
#endif
    if (IsConfFlined(aconf))
      *prefix_ptr++ = '>';
  *prefix_ptr = '\0';

  strncat(prefix_of_host,name,MAXPREFIX);
  return(prefix_of_host);
}

/*
 * report_sub_mtrie()
 * inputs	- pointer to DOMAIN_LEVEL (mtrie subtree)
 * output	- none
 * side effects	-
 * report sub mtrie entries recursively
 */

static void report_sub_mtrie(aClient *sptr, int flags, DOMAIN_LEVEL *dl_ptr)
{
  DOMAIN_PIECE *dp_ptr;
  aConfItem *aconf;
  int i;
  char *p;
  char *host;
  char *pass;
  char *name;
  char *mask;
  int  port;
  char c;

  if(!dl_ptr)
    return;

  for(i=0; i < MAX_PIECE_LIST; i++)
    {
      for(dp_ptr=dl_ptr->piece_list[i];dp_ptr; dp_ptr = dp_ptr->next_piece)
	{
	  report_sub_mtrie(sptr,flags,dp_ptr->next_level);
	  if(dp_ptr->conf_ptr)
	    {
	      /* Only show desired I/K lines */
	      aconf = dp_ptr->conf_ptr;

	      if(aconf->status & flags)
		{
		  host = BadPtr(aconf->host) ? null : aconf->host;
		  pass = BadPtr(aconf->passwd) ? null : aconf->passwd;
		  name = BadPtr(aconf->name) ? null : aconf->name;
		  mask = BadPtr(aconf->mask) ? null : aconf->mask;
		  port = (int)aconf->port;

		  if (aconf->status == CONF_KILL)
		    {
		      sendto_one(sptr, rpl_str(RPL_STATSKLINE),
				 me.name,
				 sptr->name,
				 'K',
				 host,
				 name,
				 pass);
		    }
		  else
		    {
		      c = 'I';
#ifdef LITTLE_I_LINES
		      if(IsConfLittleI(aconf))
			c = 'i';
#endif
		      sendto_one(sptr, rpl_str(RPL_STATSILINE),
				 me.name,
				 sptr->name,
				 c,
				 mask,
				 show_iline_prefix(sptr,aconf,name),
				 host,
				 port,
				 get_conf_class(aconf));
		    }
		}
	    }

	  if(dp_ptr->wild_conf_ptr)
	    {
	      aconf = dp_ptr->wild_conf_ptr;

	      if(aconf->status & flags)
		{
		  host = BadPtr(aconf->host) ? null : aconf->host;
		  pass = BadPtr(aconf->passwd) ? null : aconf->passwd;
		  name = BadPtr(aconf->name) ? null : aconf->name;
		  mask = BadPtr(aconf->mask) ? null : aconf->mask;
		  port = (int)aconf->port;

		  if (aconf->status == CONF_KILL)
		    {
		      sendto_one(sptr, rpl_str(RPL_STATSKLINE),
				 me.name,
				 sptr->name,
				 'K',
				 host,
				 name,
				 pass);
		    }
		  else
		    {
		      sendto_one(sptr, rpl_str(RPL_STATSILINE),
				 me.name,
				 sptr->name,
				 c,
				 mask,
				 show_iline_prefix(sptr,aconf,name),
				 host,
				 port,
				 get_conf_class(aconf));
		    }
		}
	    }
	}
    }
}

/*
 * clear_mtrie_conf_links()
 *
 * inputs	- NONE
 * output	- NONE
 * side effects	-
 * Clear out the mtrie list and the unsortable list (recursively)
 */

void clear_mtrie_conf_links()
{
  aConfItem *found_conf;
  aConfItem *found_conf_next;
  I_LINE_IP_ENTRY *found_ip_entry;
  I_LINE_IP_ENTRY *found_ip_entry_next;

  if(trie_list)
    {
      clear_sub_mtrie(trie_list);
      trie_list = (DOMAIN_LEVEL *)NULL;
    }

  for(found_conf=unsortable_list_ilines;
      found_conf;found_conf=found_conf_next)
    {
      found_conf_next = found_conf->next;

      /* this is an I line list */

      if(found_conf->clients)
	found_conf->status |= CONF_ILLEGAL;
      else
	free_conf(found_conf);
    }
  unsortable_list_ilines = (aConfItem *)NULL;

  for(found_conf=unsortable_list_klines;
      found_conf;found_conf=found_conf_next)
    {
      found_conf_next = found_conf->next;
      free_conf(found_conf);
    }
  unsortable_list_klines = (aConfItem *)NULL;

  for(found_conf=wild_card_ilines;
      found_conf;found_conf=found_conf_next)
    {
      found_conf_next = found_conf->next;
      free_conf(found_conf);
    }
  wild_card_ilines = (aConfItem *)NULL;

  for(found_ip_entry = ip_i_lines; found_ip_entry;
      found_ip_entry = found_ip_entry_next)
    {
      found_ip_entry_next = found_ip_entry->next;

      /* The aconf's pointed to by each ip entry here,
       * have already been cleared out of the mtrie tree above.
       */

      MyFree(found_ip_entry);
    }
  ip_i_lines = (I_LINE_IP_ENTRY *)NULL;
}

/*
 * clear_sub_mtrie
 *
 * inputs	- DOMAIN_LEVEL pointer
 * output	- none
 * side effects	- this portion of the mtrie is cleared
 */

static void clear_sub_mtrie(DOMAIN_LEVEL *dl_ptr)
{
  DOMAIN_PIECE *dp_ptr;
  DOMAIN_PIECE *next_dp_ptr;
  aConfItem *conf_ptr;
  int i;

  if(!dl_ptr)
    return;

  for(i=0; i < MAX_PIECE_LIST; i++)
    {
      dp_ptr = dl_ptr->piece_list[i];
      dl_ptr->piece_list[i] = NULL;

      for(;dp_ptr; dp_ptr = next_dp_ptr)
	{
	  clear_sub_mtrie(dp_ptr->next_level);

	  if(dp_ptr->wild_conf_ptr)
	    {
	      conf_ptr = dp_ptr->wild_conf_ptr;
	      if( (conf_ptr->status & CONF_CLIENT) && conf_ptr->clients)
		conf_ptr->status |= CONF_ILLEGAL;
	      else
		free_conf(conf_ptr);
	    }

	  if(dp_ptr->conf_ptr)
	    {
	      conf_ptr = dp_ptr->conf_ptr;
	      if( (conf_ptr->status & CONF_CLIENT) && conf_ptr->clients)
		conf_ptr->status |= CONF_ILLEGAL;
	      else
		free_conf(conf_ptr);
	    }
	    
	  next_dp_ptr = dp_ptr->next_piece;
	  MyFree(dp_ptr);
	}
    }
  MyFree(dl_ptr);
}

/*
 * wildcmp
 *
 * inputs	- pointer s1 to string
 *		- pointer s2 to string
 * output	- 0 or 1, 0 if string "match" 1 if they do not
 * side effects	-
 *
 * walk the pointers until either both strings match
 * or there is a wildcard '*' char found at the end of s1
 * (Thank you sean, "walk the pointers" indeed.)
 *
 */

static int wildcmp(char *s1,char *s2)
{
  while (*s1 & *s2)
    {
      if(*s1 == '*')	/* match everything at this point */
	return(0);

      /* very unportable, 0xDF force upper case of ASCII characters */

      if( (*s1 & 0xDF) != (*s2 & 0xDF) )
	return(1);
      s1++;
      s2++;
    }

  /* special case *foo matching foo */

  if(*s1 == '*')
    return(0);
  else
    return((*s1 & 0xDF) - (*s2 * 0xDF));
}

/*
 * add_to_ip_ilines
 *
 * inputs	- pointer to an aConfItem to add
 * output	- NONE
 * side effects	- a conf describing an I line for an IP is added
 */

void add_to_ip_ilines(aConfItem *aconf)
{
  I_LINE_IP_ENTRY *new_ip;
  unsigned long ip_host;
  unsigned long ip_mask;

  new_ip = (I_LINE_IP_ENTRY *)MyMalloc(sizeof(I_LINE_IP_ENTRY));
  memset((void *)new_ip,0,sizeof(I_LINE_IP_ENTRY));

  ip_host = host_name_to_ip(aconf->mask,&ip_mask); /*returns in =host= order*/

  new_ip->ip = ip_host;
  new_ip->mask = ip_mask;
  new_ip->conf_entry = aconf;

  if(ip_i_lines)
    new_ip->next = ip_i_lines;

  ip_i_lines = new_ip;
}

/*
 * find_matching_ip_i_line()
 * 
 * inputs	- unsigned long IP in host order
 * output	- aConfItem pointer if found, NULL if not
 * side effects	-
 * search the ip_i_line link list
 * looking for a match, return aConfItem pointer if found 
 */

static aConfItem *find_matching_ip_i_line(unsigned long host_ip)
{
  unsigned long host_ip_host_order;
  I_LINE_IP_ENTRY *p;

  host_ip_host_order = ntohl(host_ip);

  for( p = ip_i_lines; p; p = p->next)
    {
      if((host_ip_host_order & p->mask) == p->ip)
	return(p->conf_entry);
    }
  return((aConfItem *)NULL);
}
