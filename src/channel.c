/************************************************************************
 *   IRC - Internet Relay Chat, src/channel.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Co Center
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

/*
 * a number of behaviours in set_mode() have been rewritten
 * These flags can be set in a define if you wish.
 *
 * OLD_P_S	- restore xor of p vs. s modes per channel
 *		  currently p is rather unused, so using +p
 *		  to disable "knock" seemed worth while.
 * OLD_MODE_K	- new mode k behaviour means user can set new key
 *		  while old one is present, mode * -k of old key is done
 *		  on behalf of user, with mode * +k of new key.
 *		  /mode * -key results in the sending of a *, which
 *		  can be used to resynchronize a channel.
 * OLD_NON_RED	- Current code allows /mode * -s etc. to be applied
 *		  even if +s is not set. Old behaviour was not to allow
 *		  mode * -p etc. if flag was clear
 */

#ifndef	lint
static	char sccsid[] = "@(#)channel.c	2.58 2/18/94 (C) 1990 University of Oulu, Computing\
 Center and Jarkko Oikarinen";

static char *rcs_version="$Id$";
#endif

#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "channel.h"
#include "h.h"

#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)
int server_was_split=YES;
time_t server_split_time;
int server_split_recovery_time = (DEFAULT_SERVER_SPLIT_RECOVERY_TIME * 60);
int split_smallnet_size = SPLIT_SMALLNET_SIZE;
static void check_still_split();
#define USE_ALLOW_OP
#endif

extern int cold_start;	/* defined in ircd.c */

aChannel *channel = NullChn;

static	void	add_invite (aClient *, aChannel *);
static	int	add_banid (aClient *, aChannel *, char *);
static	int	add_exceptid(aClient *, aChannel *, char *);
static	int	can_join (aClient *, aChannel *, char *);
static	void	channel_modes (aClient *, char *, char *, aChannel *);
static	int	del_banid (aChannel *, char *);
static	int	del_exceptid (aChannel *, char *);
static  void	clear_bans_exceptions(aClient *,aChannel *);
static	Link	*is_banned (aClient *, aChannel *);
static	void	set_mode (aClient *, aClient *, aChannel *, int, char **);
static	void	sub1_from_channel (aChannel *);

void	clean_channelname(unsigned char *);
void	del_invite (aClient *, aChannel *);

/* static functions used in set_mode */
static char *pretty_mask(char *);
static char *fix_key(char *);
static void collapse_signs(char *);
static int errsent(int,int *);
static void change_chan_flag(aChannel *, aClient *, int );
static void set_deopped(aClient *,aChannel *,int);

#ifdef ORATIMING
struct timeval tsdnow, tsdthen; 
unsigned long tsdms;
#endif

typedef struct
{
  int mode;
  char letter;
}FLAG_ITEM;


static	char	*PartFmt = ":%s PART %s";
/*
 * some buffers for rebuilding channel/nick lists with ,'s
 */
/*
 * hrmmm "char	nickbuf[BUFSIZE];" was never used. odd. removed
 * actually, it was only used if V28PlusOnly was defined,
 * which it never was.
 *
 * -Dianora
 */

static	char	buf[BUFSIZE];
static	char	modebuf[MODEBUFLEN], modebuf2[MODEBUFLEN];
static	char	parabuf[MODEBUFLEN], parabuf2[MODEBUFLEN];

/* externally defined function */
extern Link *find_channel_link(Link *,aChannel *);	/* defined in list.c */

#ifdef ANTI_SPAMBOT
extern int spam_num;	/* defined in s_serv.c */
extern int spam_time;	/* defined in s_serv.c */
#endif

/*
 * return the length (>=0) of a chain of links.
 */
static	int	list_length(Link *lp)
{
  Reg	int	count = 0;

  for (; lp; lp = lp->next)
    count++;
  return count;
}

/*
** find_chasing
**	Find the client structure for a nick name (user) using history
**	mechanism if necessary. If the client is not found, an error
**	message (NO SUCH NICK) is generated. If the client was found
**	through the history, chasing will be 1 and otherwise 0.
*/
aClient *find_chasing(aClient *sptr, char *user, int *chasing)
{
  Reg	aClient *who = find_client(user, (aClient *)NULL);
  
  if (chasing)
    *chasing = 0;
  if (who)
    return who;
  if (!(who = get_history(user, (long)KILLCHASETIMELIMIT)))
    {
      sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		 me.name, sptr->name, user);
      return ((aClient *)NULL);
    }
  if (chasing)
    *chasing = 1;
  return who;
}

/*
 *  Fixes a string so that the first white space found becomes an end of
 * string marker (`\-`).  returns the 'fixed' string or "*" if the string
 * was NULL length or a NULL pointer.
 */
static	char	*check_string(char *s)
{
  static char star[2] = "*";
  char	*str = s;

  if (BadPtr(s))
    return star;

  for ( ;*s; s++)
    if (isspace(*s))
      {
	*s = '\0';
	break;
      }

  return (str);
}

/*
 * create a string of form "foo!bar@fubar" given foo, bar and fubar
 * as the parameters.  If NULL, they become "*".
 */
static	char *make_nick_user_host(char *nick, char *name, char *host)
{
  static char	namebuf[NICKLEN+USERLEN+HOSTLEN+6];
  int	n;
  Reg	char	*ptr1,*ptr2;

  ptr1 = namebuf;
  for(ptr2=check_string(nick),n=NICKLEN;*ptr2 && n--;)
    *ptr1++ = *ptr2++;
  *ptr1++ = '!';
  for(ptr2=check_string(name),n=USERLEN;*ptr2 && n--;)
    *ptr1++ = *ptr2++;
  *ptr1++ = '@';
  for(ptr2=check_string(host),n=HOSTLEN;*ptr2 && n--;)
    *ptr1++ = *ptr2++;
  *ptr1 = '\0';
  return (namebuf);
}

/*
 * Ban functions to work with mode +b
 */
/* add_banid - add an id to be banned to the channel  (belongs to cptr) */

static	int	add_banid(aClient *cptr, aChannel *chptr, char *banid)
{
  Reg	Link	*ban;
  Reg	int	cnt = 0, len = 0;

  if (MyClient(cptr))
    (void)collapse(banid);

  for (ban = chptr->banlist; ban; ban = ban->next)
    {
      len = strlen(BANSTR(ban));

      if (MyClient(cptr))
	{
	  if((len > MAXBANLENGTH) || (++cnt >= MAXBANS))
	    {
	      sendto_one(cptr, err_str(ERR_BANLISTFULL),
			 me.name, cptr->name,
			 chptr->chname, banid);
	      return -1;
	    }
	  if(!match(BANSTR(ban), banid) ||
	     !match(banid,BANSTR(ban)))
	    return -1;
	}
      else if (!mycmp(BANSTR(ban), banid))
	return -1;
    }

  ban = make_link();
  memset((void *)ban, 0, sizeof(Link));
  ban->flags = CHFL_BAN;
  ban->next = chptr->banlist;

#ifdef BAN_INFO

  ban->value.banptr = (aBan *)MyMalloc(sizeof(aBan));
  ban->value.banptr->banstr = (char *)MyMalloc(strlen(banid)+1);
  (void)strcpy(ban->value.banptr->banstr, banid);

#ifdef USE_UH
  if (IsPerson(cptr))
    {
      ban->value.banptr->who =
	(char *)MyMalloc(strlen(cptr->name)+
			 strlen(cptr->user->username)+
			 strlen(cptr->user->host)+3);
      (void)sprintf(ban->value.banptr->who, "%s!%s@%s",
		    cptr->name, cptr->user->username, cptr->user->host);
    }
  else
    {
#endif
      ban->value.banptr->who = (char *)MyMalloc(strlen(cptr->name)+1);
      (void)strcpy(ban->value.banptr->who, cptr->name);
#ifdef USE_UH
    }
#endif

  ban->value.banptr->when = timeofday;

#else

  ban->value.cp = (char *)MyMalloc(strlen(banid)+1);
  (void)strcpy(ban->value.cp, banid);

#endif	/* #ifdef BAN_INFO */

  chptr->banlist = ban;
  return 0;
}

/* add_exceptid - add an id to the exception list for the channel  
 * (belongs to cptr) 
 */

static	int	add_exceptid(aClient *cptr, aChannel *chptr, char *eid)
{
  Reg	Link	*ex, *ban;
  Reg	int	cnt = 0, len = 0;

  if (MyClient(cptr))
    {
      for (ban = chptr->banlist; ban; ban = ban->next)
	{
	  len += strlen(BANSTR(ban));
	  if (len > MAXBANLENGTH || ++cnt >= MAXBANS)
		  return -1;
	}
    }

  for (ex = chptr->exceptlist; ex; ex = ex->next)
    {
      len += strlen(BANSTR(ex));

      if (MyClient(cptr) &&
	  ((len > MAXBANLENGTH) || (++cnt >= MAXBANS) ||
	   !match(BANSTR(ex), eid) ||
	   !match(eid,BANSTR(ex))))
	return -1;
      else if (!mycmp(BANSTR(ex), eid))
	return -1;
    }

  ex = make_link();
  memset((void *)ex, 0, sizeof(Link));
  ex->flags = CHFL_EXCEPTION;
  ex->next = chptr->exceptlist;

#ifdef BAN_INFO

  ex->value.banptr = (aBan *)MyMalloc(sizeof(aBan));
  ex->value.banptr->banstr = (char *)MyMalloc(strlen(eid)+1);
  (void)strcpy(ex->value.banptr->banstr, eid);

#ifdef USE_UH
  if (IsPerson(cptr))
    {
      ex->value.banptr->who =
	(char *)MyMalloc(strlen(cptr->name)+
			 strlen(cptr->user->username)+
			 strlen(cptr->user->host)+3);
      (void)sprintf(ex->value.banptr->who, "%s!%s@%s",
		    cptr->name, cptr->user->username, cptr->user->host);
    }
  else
    {
#endif
      ex->value.banptr->who = (char *)MyMalloc(strlen(cptr->name)+1);
      (void)strcpy(ex->value.banptr->who, cptr->name);
#ifdef USE_UH
    }
#endif

  ex->value.banptr->when = timeofday;

#else

  ex->value.cp = (char *)MyMalloc(strlen(eid)+1);
  (void)strcpy(ex->value.cp, eid);

#endif	/* #ifdef BAN_INFO */

  chptr->exceptlist = ex;
  return 0;
}

/*
 * This original comment makes no sense to me -Dianora
 *
 * "del_banid - delete an id belonging to cptr
 * if banid is null, deleteall banids belonging to cptr."
 *
 * "cptr" what cptr? if banid is null,it just returns with an -1
 * -Dianora
 *
 * from orabidoo
 */
static	int	del_banid(aChannel *chptr, char *banid)
{
  register Link **ban;
  register Link *tmp;

  if (!banid)
    return -1;
  for (ban = &(chptr->banlist); *ban; ban = &((*ban)->next))
#ifdef BAN_INFO
    if (mycmp(banid, (*ban)->value.banptr->banstr)==0)
#else
      if (mycmp(banid, (*ban)->value.cp)==0)
#endif
	{
	  tmp = *ban;
	  *ban = tmp->next;
#ifdef BAN_INFO
	  MyFree(tmp->value.banptr->banstr);
	  MyFree(tmp->value.banptr->who);
	  MyFree(tmp->value.banptr);
#else
	  MyFree(tmp->value.cp);
#endif
	  free_link(tmp);
	  break;
	}
  return 0;
}

/*
 * del_exceptid - delete an id belonging to cptr
 *
 * from orabidoo
 */
static	int	del_exceptid(aChannel *chptr, char *eid)
{
  register Link **ex;
  register Link *tmp;

  if (!eid)
    return -1;
  for (ex = &(chptr->exceptlist); *ex; ex = &((*ex)->next))
    if (mycmp(eid, BANSTR(*ex)) == 0)
      {
	tmp = *ex;
	*ex = tmp->next;
#ifdef BAN_INFO
	MyFree(tmp->value.banptr->banstr);
	MyFree(tmp->value.banptr->who);
	MyFree(tmp->value.banptr);
#else
	MyFree(tmp->value.cp);
#endif
	free_link(tmp);
	break;
      }
  return 0;
}

/*
 * is_banned - returns a pointer to the ban structure if banned else NULL
 *
 * IP_BAN_ALL from comstud
 * always on...
 *
 * +e code from orabidoo
 */

static	Link	*is_banned(aClient *cptr,aChannel *chptr)
{
  register Link	*tmp;
  register Link *t2;
  char	s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  if (!IsPerson(cptr))
    return ((Link *)NULL);

  strcpy(s,make_nick_user_host(cptr->name, cptr->user->username,
			       cptr->user->host));
  s2 = make_nick_user_host(cptr->name, cptr->user->username,
			   cptr->hostip);

  for (tmp = chptr->banlist; tmp; tmp = tmp->next)
    if ((match(BANSTR(tmp), s) == 0) ||
	(match(BANSTR(tmp), s2) == 0) )
      break;

  if (tmp)
    {
      for (t2 = chptr->exceptlist; t2; t2 = t2->next)
	if ((match(BANSTR(t2), s) == 0) ||
	    (match(BANSTR(t2), s2) == 0))
	  return NULL;
    }
  return (tmp);
}

/*
 * adds a user to a channel by adding another link to the channels member
 * chain.
 */
static	void	add_user_to_channel(aChannel *chptr, aClient *who, int flags)
{
  Reg	Link *ptr;

  if (who->user)
    {
      ptr = make_link();
      ptr->flags = flags;
      ptr->value.cptr = who;
      ptr->next = chptr->members;
      chptr->members = ptr;
      chptr->users++;

      ptr = make_link();
      ptr->value.chptr = chptr;
      ptr->next = who->user->channel;
      who->user->channel = ptr;
      who->user->joined++;
    }
}

void	remove_user_from_channel(aClient *sptr,aChannel *chptr)
{
  Reg	Link	**curr;
  Reg	Link	*tmp;

  for (curr = &chptr->members; (tmp = *curr); curr = &tmp->next)
    if (tmp->value.cptr == sptr)
      {
	*curr = tmp->next;
	free_link(tmp);
	break;
      }
  for (curr = &sptr->user->channel; (tmp = *curr); curr = &tmp->next)
    if (tmp->value.chptr == chptr)
      {
	*curr = tmp->next;
	free_link(tmp);
	break;
      }
  sptr->user->joined--;

  sub1_from_channel(chptr);

}

static	void	change_chan_flag(aChannel *chptr,aClient *cptr, int flag)
{
  Reg	Link *tmp;

  if ((tmp = find_user_link(chptr->members, cptr)))
   {
    if (flag & MODE_ADD)
      {
	tmp->flags |= flag & MODE_FLAGS;
	if (flag & MODE_CHANOP)
	  tmp->flags &= ~MODE_DEOPPED;
      }
    else
      {
	tmp->flags &= ~flag & MODE_FLAGS;
      }
   }
}

static	void	set_deopped(aClient *cptr, aChannel *chptr,int flag)
{
  Reg	Link	*tmp;

  if ((tmp = find_user_link(chptr->members, cptr)))
    if ((tmp->flags & flag) == 0)
      tmp->flags |= MODE_DEOPPED;
}

int	is_chan_op(aClient *cptr, aChannel *chptr)
{
  Reg	Link	*lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_CHANOP);
  
  return 0;
}

int	is_deopped(aClient *cptr, aChannel *chptr)
{
  Reg	Link	*lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_DEOPPED);
  
  return 0;
}

int	has_voice(aClient *cptr, aChannel *chptr)
{
  Reg	Link	*lp;

  if (chptr)
    if ((lp = find_user_link(chptr->members, cptr)))
      return (lp->flags & CHFL_VOICE);

  return 0;
}

int	can_send(aClient *cptr, aChannel *chptr)
{
  Reg	Link	*lp;
  Reg	int	member;

  member = IsMember(cptr, chptr);
  lp = find_user_link(chptr->members, cptr);

  if (chptr->mode.mode & MODE_MODERATED &&
      (!lp || !(lp->flags & (CHFL_CHANOP|CHFL_VOICE))))
    return (MODE_MODERATED);

  if (chptr->mode.mode & MODE_NOPRIVMSGS && !member)
    return (MODE_NOPRIVMSGS);

  return 0;
}

aChannel *find_channel(char *chname, aChannel *chptr)
{
  return hash_find_channel(chname, chptr);
}

/*
 * write the "simple" list of channel modes for channel chptr onto buffer mbuf
 * with the parameters in pbuf.
 */
static	void	channel_modes(aClient *cptr,
			      char *mbuf,
			      char *pbuf,
			      aChannel *chptr)
{
  *mbuf++ = '+';
  if (chptr->mode.mode & MODE_SECRET)
    *mbuf++ = 's';
  else if (chptr->mode.mode & MODE_PRIVATE)
    *mbuf++ = 'p';
  if (chptr->mode.mode & MODE_MODERATED)
    *mbuf++ = 'm';
  if (chptr->mode.mode & MODE_TOPICLIMIT)
    *mbuf++ = 't';
  if (chptr->mode.mode & MODE_INVITEONLY)
    *mbuf++ = 'i';
  if (chptr->mode.mode & MODE_NOPRIVMSGS)
    *mbuf++ = 'n';
  if (chptr->mode.limit)
    {
      *mbuf++ = 'l';
      if (IsMember(cptr, chptr) || IsServer(cptr))
	(void)ircsprintf(pbuf, "%d ", chptr->mode.limit);
    }
  if (*chptr->mode.key)
    {
      *mbuf++ = 'k';
      if (IsMember(cptr, chptr) || IsServer(cptr))
	(void)strcat(pbuf, chptr->mode.key);
    }
  *mbuf++ = '\0';
  return;
}

/*
 * only used to send +b and +e now 
 *
 */

static	void	send_mode_list(aClient *cptr,
			       char *chname,
			       Link *top,
			       int mask,
			       char flag)
{
  Reg	Link	*lp;
  Reg	char	*cp, *name;
  int	count = 0, send = 0;
  
  cp = modebuf + strlen(modebuf);
  if (*parabuf)	/* mode +l or +k xx */
    count = 1;
  for (lp = top; lp; lp = lp->next)
    {
      if (!(lp->flags & mask))
	continue;
      name = BANSTR(lp);
	
      if (strlen(parabuf) + strlen(name) + 10 < (size_t) MODEBUFLEN)
	{
	  (void)strcat(parabuf, " ");
	  (void)strcat(parabuf, name);
	  count++;
	  *cp++ = flag;
	  *cp = '\0';
	}
      else if (*parabuf)
	send = 1;
      if (count == 3)
	send = 1;
      if (send)
	{
	  sendto_one(cptr, ":%s MODE %s %s %s",
		     me.name, chname, modebuf, parabuf);
	  send = 0;
	  *parabuf = '\0';
	  cp = modebuf;
	  *cp++ = '+';
	  if (count != 3)
	    {
	      (void)strcpy(parabuf, name);
	      *cp++ = flag;
	    }
	  count = 0;
	  *cp = '\0';
	}
    }
}

/*
 * send "cptr" a full list of the modes for channel chptr.
 */
void	send_channel_modes(aClient *cptr, aChannel *chptr)
{
  Link	*l, *anop = NULL, *skip = NULL;
  int	n = 0;
  char	*t;

  if (*chptr->chname != '#')
    return;

  *modebuf = *parabuf = '\0';
  channel_modes(cptr, modebuf, parabuf, chptr);
  

  if (*parabuf)
    strcat(parabuf, " ");
  sprintf(buf, ":%s SJOIN %ld %s %s %s:", me.name,
	  chptr->channelts, chptr->chname, modebuf, parabuf);
  t = buf + strlen(buf);
  for (l = chptr->members; l && l->value.cptr; l = l->next)
    if (l->flags & MODE_CHANOP)
      {
	anop = l;
	break;
      }
  /* follow the channel, but doing anop first if it's defined
  **  -orabidoo
  */
  l = NULL;
  for (;;)
    {
      if (anop)
	{
	  l = skip = anop;
	  anop = NULL;
	}
      else 
	{
	  if (l == NULL || l == skip)
	    l = chptr->members;
	  else
	    l = l->next;
	  if (l && l == skip)
	    l = l->next;
	  if (l == NULL)
	    break;
	}
      if (l->flags & MODE_CHANOP)
	*t++ = '@';
      if (l->flags & MODE_VOICE)
	*t++ = '+';
      strcpy(t, l->value.cptr->name);
      t += strlen(t);
      *t++ = ' ';
      n++;
      if (t - buf > BUFSIZE - 80)
	{
	  *t++ = '\0';
	  if (t[-1] == ' ') t[-1] = '\0';
	  sendto_one(cptr, "%s", buf);
	  sprintf(buf, ":%s SJOIN %ld %s 0 :",
		  me.name, chptr->channelts,
		  chptr->chname);
	  t = buf + strlen(buf);
	  n = 0;
	}
    }
      
  if (n)
    {
      *t++ = '\0';
      if (t[-1] == ' ') t[-1] = '\0';
      sendto_one(cptr, "%s", buf);
    }
  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->banlist, CHFL_BAN,'b');

  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
	       me.name, chptr->chname, modebuf, parabuf);

  if(!IsCapable(cptr,CAP_EX))
    return;

  *parabuf = '\0';
  *modebuf = '+';
  modebuf[1] = '\0';
  send_mode_list(cptr, chptr->chname, chptr->exceptlist, CHFL_EXCEPTION,'e');

  if (modebuf[1] || *parabuf)
    sendto_one(cptr, ":%s MODE %s %s %s",
	       me.name, chptr->chname, modebuf, parabuf);
}

/*
 * m_mode
 * parv[0] - sender
 * parv[1] - channel
 */

int	m_mode(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aChannel *chptr;

  /* Now, try to find the channel in question */
  if (parc > 1)
    {
      clean_channelname((unsigned char *)parv[1]);
      chptr = find_channel(parv[1], NullChn);
      if (chptr == NullChn)
	return m_umode(cptr, sptr, parc, parv);
    }
  else
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "MODE");
      return 0;
    }

  if (parc < 3)
    {
      *modebuf = *parabuf = '\0';
      modebuf[1] = '\0';
      channel_modes(sptr, modebuf, parabuf, chptr);
      sendto_one(sptr, rpl_str(RPL_CHANNELMODEIS), me.name, parv[0],
		 chptr->chname, modebuf, parabuf);
      sendto_one(sptr, rpl_str(RPL_CREATIONTIME), me.name, parv[0],
		 chptr->chname, chptr->channelts);
      return 0;
    }

  set_mode(cptr, sptr, chptr, parc - 2, parv + 2);

  return 0;
}

/* stolen from Undernet's ircd  -orabidoo
 *
 */

static	char	*pretty_mask(char *mask)
{
  register	char	*cp, *user, *host;

  if ((user = strchr((cp = mask), '!')))
    *user++ = '\0';
  if ((host = strrchr(user ? user : cp, '@')))
    {
      *host++ = '\0';
      if (!user)
	return make_nick_user_host(NULL, cp, host);
    }
  else if (!user && strchr(cp, '.'))
    return make_nick_user_host(NULL, NULL, cp);
  return make_nick_user_host(cp, user, host);
}

static	char	*fix_key(char *arg)
{
  Reg	u_char	*s, *t, c;

  /* No more stripping the 8th bit or checking
  ** for the +k bug... it's long dead.  -orab
  */
  for (s = t = (u_char *)arg; (c = *s); s++)
    {
      if (c != ':' && c > 0x20 && (c < 0x7f || c > 0xa0))
	*t++ = c;
    }
  *t = '\0';
  return arg;
}

/*
 * like the name says...  take out the redundant signs in a modechange list
 */
static	void	collapse_signs(char *s)
{
  char	plus = '\0', *t = s, c;
  while ((c = *s++))
    {
      if (c != plus)
	*t++ = c;
      if (c == '+' || c == '-')
	plus = c;
    }
  *t = '\0';
}

/* little helper function to avoid returning duplicate errors */
static	int	errsent(int err, int *errs)
{
  if (err & *errs)
    return 1;
  *errs |= err;
  return 0;
}

/* bitmasks for various error returns that set_mode should only return
 * once per call  -orabidoo
 */

#define SM_ERR_NOTS		0x00000001	/* No TS on channel */
#define SM_ERR_NOOPS		0x00000002	/* No chan ops */
#define SM_ERR_UNKNOWN		0x00000004
#define SM_ERR_RPL_C		0x00000008
#define SM_ERR_RPL_B		0x00000010
#define SM_ERR_RPL_E		0x00000020
#define SM_ERR_NOTONCHANNEL	0x00000040	/* Not on channel */
#define SM_ERR_RESTRICTED	0x00000080	/* Restricted chanop */

/*
** Apply the mode changes passed in parv to chptr, sending any error
** messages and MODE commands out.  Rewritten to do the whole thing in
** one pass, in a desperate attempt to keep the code sane.  -orabidoo
*/
/*
 * rewritten to remove +h/+c/z 
 * in spirit with the one pass idea, I've re-written how "imnspt"
 * handling was done
 *
 * I've also left some "remnants" of the +h code in for possible
 * later addition.
 * For example, isok could be replaced witout half ops, with ischop() or
 * chan_op depending.
 *
 * -Dianora
 */

static  void     set_mode(aClient *cptr,
                         aClient *sptr,
                         aChannel *chptr,
                         int parc,
                         char *parv[])
{
  int	errors_sent = 0, opcnt = 0, len = 0, tmp, nusers;
  int	keychange = 0, limitset = 0;
  int	whatt = MODE_ADD, the_mode = 0;
#ifdef OLD_P_S
  int   done_s_or_p = NO;
#else
  int	done_s = NO, done_p = NO;
#endif
  int	done_i = NO, done_m = NO, done_n = NO, done_t = NO;
  aClient *who;
  Link	*lp;
  char	*curr = parv[0], c, *arg, plus = '+', *tmpc;
  char	numeric[16];
  /* mbufw gets the param-less mode chars, always with their sign
   * mbuf2w gets the paramed mode chars, always with their sign
   * pbufw gets the params, in ID form whenever possible
   * pbuf2w gets the params, no ID's
   */
  /* no ID code at the moment
   * pbufw gets the params, no ID's
   * grrrr for now I'll stick the params into pbufw without ID's
   * -Dianora
   */
  /* *sigh* FOR YOU Roger, and ONLY for you ;-)
   * lets stick mode/params that only the newer servers will understand
   * into modebuf_new/parabuf_new 
   */

  char	modebuf_new[MODEBUFLEN];
  char	parabuf_new[MODEBUFLEN];

  char	*mbufw = modebuf, *mbuf2w = modebuf2;
  char	*pbufw = parabuf, *pbuf2w = parabuf2;

  char  *mbufw_new = modebuf_new;
  char  *pbufw_new = parabuf_new;

  int	ischop, isok, isdeop, chan_op, self_lose_ops;

  self_lose_ops = 0;

  chan_op = is_chan_op(sptr, chptr);

  /* has ops or is a server */
  ischop = IsServer(sptr) || (chan_op & MODE_CHANOP);

  /* is client marked as deopped */
  isdeop = !ischop && !IsServer(sptr) && is_deopped(sptr, chptr);

  /* is an op or server or remote user on a TS channel */
  isok = ischop || (!isdeop && IsServer(cptr) && chptr->channelts);

  if(isok)
    chptr->keep_their_modes = YES;

  /* isok_c calculated later, only if needed */

  /* parc is the number of _remaining_ args (where <0 means 0);
  ** parv points to the first remaining argument
  */
  parc--;
  parv++;

  FOREVER
    {
      if (BadPtr(curr))
	{
	  /*
	   * Deal with mode strings like "+m +o blah +i"
	   */
	  if (parc-- > 0)
	    {
	      curr = *parv++;
	      continue;
	    }
	  break;
	}
      c = *curr++;

      switch (c)
	{
	case '+' :
	  whatt = MODE_ADD;
	  plus = '+';
	  continue;
	  /* NOT REACHED */
	  break;

	case '-' :
	  whatt = MODE_DEL;
	  plus = '-';
	  continue;
	  /* NOT REACHED */
	  break;

	case '=' :
	  whatt = MODE_QUERY;
	  plus = '=';	
	  continue;
	  /* NOT REACHED */
	  break;

	case 'o' :
	case 'v' :
	  if (MyClient(sptr))
	    {
	      if(!IsMember(sptr, chptr))
		{
		  if(!errsent(SM_ERR_NOTONCHANNEL, &errors_sent))
		    sendto_one(sptr, err_str(ERR_NOTONCHANNEL),
			       me.name, sptr, chptr->chname);
		  /* eat the parameter */
		  parc--;
		  parv++;
		  break;
		}
#ifdef LITTLE_I_LINES
	      else
		{
		  if(IsRestricted(sptr) && (whatt == MODE_ADD))
		    {
		      if(!errsent(SM_ERR_RESTRICTED, &errors_sent))
			{
			  sendto_one(sptr,
	    ":%s NOTICE %s :*** Notice -- You are restricted and cannot chanop others",
				 me.name,
				 sptr->name);
			}
		      /* eat the parameter */
		      parc--;
		      parv++;
		      break;
		    }
		}
#endif
	    }
	  if (whatt == MODE_QUERY)
	    break;
	  if (parc-- <= 0)
	    break;
	  arg = check_string(*parv++);

	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;

	  if (!(who = find_chasing(sptr, arg, NULL)))
	    break;

	  /* there is always the remote possibility of picking up
	   * a bogus user, be nasty to core for that. -Dianora
	   */

	  if (!who->user)
	    break;

	  /* no more of that mode bouncing crap */
	  if (!IsMember(who, chptr))
	    {
	      if (MyClient(sptr))
		sendto_one(sptr, err_str(ERR_USERNOTINCHANNEL), me.name, 
			   sptr->name, arg, chptr->chname);
	      break;
	    }

	  if ((who == sptr) && (c == 'o'))
	    {
	      if(whatt == MODE_ADD)
		break;
	      
	      if(whatt == MODE_DEL)
		self_lose_ops = 1;
	      }

	  /* ignore server-generated MODE +-ovh */
	  if (IsServer(sptr))
	    {
	      ts_warn( "MODE %c%c on %s for %s from server %s (ignored)", 
		       (whatt == MODE_ADD ? '+' : '-'), c, chptr->chname, 
		       who->name,sptr->name);
	      break;
	    }

	  if (c == 'o')
	    the_mode = MODE_CHANOP;
	  else if (c == 'v')
	    the_mode = MODE_VOICE;

	  if (isdeop && (c == 'o') && whatt == MODE_ADD)
	    set_deopped(who, chptr, the_mode);

	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }
	
	  tmp = strlen(arg);
	  if (len + tmp + 2 >= MODEBUFLEN)
	    break;

	  *mbufw++ = plus;
	  *mbufw++ = c;
	  strcpy(pbufw, who->name);
	  pbufw += strlen(pbufw);
	  *pbufw++ = ' ';
	  len += tmp + 1;
	  opcnt++;

	  change_chan_flag(chptr, who, the_mode|whatt);

	  if (self_lose_ops)
	    isok = 0;

	  break;

	case 'k':
	  if (whatt == MODE_QUERY)
	    break;
	  if (parc-- <= 0)
	    {
	      /* allow arg-less mode -k */
	      if (whatt == MODE_DEL)
		arg = "*";
	      else
		break;
	    }
	  else
	    arg = fix_key(check_string(*parv++));

	  if (keychange++)
	    break;
	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;
	  if (!*arg)
	    break;

	  if (!isok)
	    {
	      if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

#ifdef OLD_MODE_K
	  if ((whatt == MODE_ADD) && (*chptr->mode.key))
	    {
	      sendto_one(sptr, err_str(ERR_KEYSET), me.name, 
			 sptr->name, chptr->chname);
	      break;
	    }
#endif
	  if ( (tmp = strlen(arg)) > KEYLEN)
	    {
	      arg[KEYLEN] = '\0';
	      tmp = KEYLEN;
	    }

	  if (len + tmp + 2 >= MODEBUFLEN)
	    break;

#ifndef OLD_MODE_K
	  /* if there is already a key, and the client is adding one
	   * remove the old one, then add the new one
	   */

	  if((whatt == MODE_ADD) && *chptr->mode.key)
	    {
	      /* If the key is the same, don't do anything */

	      if(!strcmp(chptr->mode.key,arg))
		break;

	      sendto_channel_butserv(chptr, sptr, ":%s MODE %s -k %s", 
				     sptr->name, chptr->chname,
				     chptr->mode.key);

	      sendto_match_servs(chptr, cptr, ":%s MODE %s -k %s",
				 sptr->name, chptr->chname,
				 chptr->mode.key);
	    }
#endif
	  *mbufw++ = plus;
	  *mbufw++ = 'k';
	  strcpy(pbufw, arg);
	  pbufw += strlen(pbufw);
	  *pbufw++ = ' ';
	  len += tmp + 1;
	  opcnt++;

	  if (whatt == MODE_DEL)
	    *chptr->mode.key = '\0';
	  else
	    strncpyzt(chptr->mode.key, arg, KEYLEN+1);

	  break;

	  /* There is a nasty here... I'm supposed to have
	   * CAP_EX before I can send exceptions to bans to a server.
	   * But that would mean I'd have to keep two strings
	   * one for local clients, and one for remote servers,
	   * one with the 'e' strings, one without.
	   * I added another parameter buf and mode buf for "new"
	   * capabilities.
	   *
	   * -Dianora
	   */

	case 'e':
	  if (whatt == MODE_QUERY || parc-- <= 0)
	    {
	      if (!MyClient(sptr))
		break;
	      if (errsent(SM_ERR_RPL_E, &errors_sent))
		break;
	      /* don't allow a non chanop to see the exception list
	       * suggested by Matt on operlist nov 25 1998
	       */
	      if(isok)
		{
#ifdef BAN_INFO
		  for (lp = chptr->exceptlist; lp; lp = lp->next)
		    sendto_one(cptr, rpl_str(RPL_EXCEPTLIST),
			       me.name, cptr->name,
			       chptr->chname,
			       lp->value.banptr->banstr,
			       lp->value.banptr->who,
			       lp->value.banptr->when);
#else 
		  for (lp = chptr->exceptlist; lp; lp = lp->next)
		    sendto_one(cptr, rpl_str(RPL_EXCEPTLIST),
			       me.name, cptr->name,
			       chptr->chname,
			       lp->value.cp);
#endif
		  sendto_one(sptr, rpl_str(RPL_ENDOFEXCEPTLIST),
			     me.name, sptr->name, 
			     chptr->chname);
		}
	      break;
	    }
	  arg = check_string(*parv++);

	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;

	  if (!isok)
	    {
	      if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
			   me.name, sptr->name, 
			   chptr->chname);
	      break;
	    }
	  
	  if(MyClient(sptr))
	    chptr->keep_their_modes = YES;
	  else if(!chptr->keep_their_modes)
	    {
	      parc--;
	      parv++;
	      break;
	    }

	  /* user-friendly ban mask generation, taken
	  ** from Undernet's ircd  -orabidoo
	  */
	  if (MyClient(sptr))
	    arg = collapse(pretty_mask(arg));

	  if(*arg == ':')
	    {
	      parc--;
	      parv++;
	      break;
	    }

	  tmp = strlen(arg);
	  if (len + tmp + 2 >= MODEBUFLEN)
	    break;

	  if (!(((whatt & MODE_ADD) && !add_exceptid(sptr, chptr, arg)) ||
		((whatt & MODE_DEL) && !del_exceptid(chptr, arg))))
	    break;

	  /* This stuff can go back in when all servers understand +e 
	   * with the pbufw_new nonsense removed -Dianora
	   */

	  /*
	  *mbufw++ = plus;
	  *mbufw++ = 'e';
	  strcpy(pbufw, arg);
	  pbufw += strlen(pbufw);
	  *pbufw++ = ' ';
	  */
	  len += tmp + 1;
	  opcnt++;

	  *mbufw_new++ = plus;
	  *mbufw_new++ = 'e';
	  strcpy(pbufw_new, arg);
	  pbufw_new += strlen(pbufw_new);
	  *pbufw_new++ = ' ';

	  break;

	case 'b':
	  if (whatt == MODE_QUERY || parc-- <= 0)
	    {
	      if (!MyClient(sptr))
		break;

	      if (errsent(SM_ERR_RPL_B, &errors_sent))
		break;
#ifdef BAN_INFO
	      for (lp = chptr->banlist; lp; lp = lp->next)
		sendto_one(cptr, rpl_str(RPL_BANLIST),
			   me.name, cptr->name,
			   chptr->chname,
			   lp->value.banptr->banstr,
			   lp->value.banptr->who,
			   lp->value.banptr->when);
#else 
	      for (lp = chptr->banlist; lp; lp = lp->next)
		sendto_one(cptr, rpl_str(RPL_BANLIST),
			   me.name, cptr->name,
			   chptr->chname,
			   lp->value.cp);
#endif
	      sendto_one(sptr, rpl_str(RPL_ENDOFBANLIST),
			 me.name, sptr->name, 
			 chptr->chname);
	      break;
	    }

	  if(MyClient(sptr))
	    chptr->keep_their_modes = YES;
	  else if(!chptr->keep_their_modes)
	    {
	      parc--;
	      parv++;
	      break;
	    }

	  arg = check_string(*parv++);

	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;

	  if (!isok)
	    {
	      if (!errsent(SM_ERR_NOOPS, &errors_sent) && MyClient(sptr))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
			   me.name, sptr->name, 
			   chptr->chname);
	      break;
	    }


	  /* Ignore colon at beginning of ban string.
	   * Unfortunately, I can't ignore all such strings,
	   * because otherwise the channel could get desynced.
	   * I can at least, stop local clients from placing a ban
	   * with a leading colon.
	   *
	   * Roger uses check_string() combined with an earlier test
	   * in his TS4 code. The problem is, this means on a mixed net
	   * one can't =remove= a colon prefixed ban if set from
	   * an older server.
	   * His code is more efficient though ;-/ Perhaps
	   * when we've all upgraded this code can be moved up.
	   *
	   * -Dianora
	   */

	  /* user-friendly ban mask generation, taken
	  ** from Undernet's ircd  -orabidoo
	  */
	  if (MyClient(sptr))
	    {
	      if( (*arg == ':') && (whatt & MODE_ADD) )
		{
		  parc--;
		  parv++;
		  break;
		}
	      arg = collapse(pretty_mask(arg));
	    }

	  tmp = strlen(arg);
	  if (len + tmp + 2 >= MODEBUFLEN)
	    break;

	  if (!(((whatt & MODE_ADD) && !add_banid(sptr, chptr, arg)) ||
		((whatt & MODE_DEL) && !del_banid(chptr, arg))))
	    break;

	  *mbufw++ = plus;
	  *mbufw++ = 'b';
	  strcpy(pbufw, arg);
	  pbufw += strlen(pbufw);
	  *pbufw++ = ' ';
	  len += tmp + 1;
	  opcnt++;

	  break;

	case 'l':
	  if (whatt == MODE_QUERY)
	    break;
	  if (!isok || limitset++)
	    {
	      if (whatt == MODE_ADD && parc-- > 0)
		parv++;
	      break;
	    }

	  if (whatt == MODE_ADD)
	    {
	      if (parc-- <= 0)
		{
		  if (MyClient(sptr))
		    sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
			       me.name, sptr->name, "MODE +l");
		  break;
		}
	      
	      arg = check_string(*parv++);
	      if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
		break;
	      if (!(nusers = atoi(arg)))
		break;
	      ircsprintf(numeric, "%-15d", nusers);
	      if ((tmpc = strchr(numeric, ' ')))
		*tmpc = '\0';
	      arg = numeric;

	      tmp = strlen(arg);
	      if (len + tmp + 2 >= MODEBUFLEN)
		break;

	      chptr->mode.limit = nusers;
	      chptr->mode.mode |= MODE_LIMIT;

	      *mbufw++ = '+';
	      *mbufw++ = 'l';
	      strcpy(pbufw, arg);
	      pbufw += strlen(pbufw);
	      *pbufw++ = ' ';
	      len += tmp + 1;
	      opcnt++;
	    }
	  else
	    {
	      chptr->mode.limit = 0;
	      chptr->mode.mode &= ~MODE_LIMIT;
	      *mbufw++ = '-';
	      *mbufw++ = 'l';
	    }

	  break;

	  /* Traditionally, these are handled separately
	   * but I decided to combine them all into this one case
	   * statement keeping it all sane
	   *
	   * The disadvantage is a lot more code duplicated ;-/
	   *
	   * -Dianora
	   */

	case 'i' :
	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

	  if(MyClient(sptr))
	    {
	      if(done_i)
		break;
	      else
		done_i = YES;

	      if ( opcnt >= MAXMODEPARAMS)
		break;
	    }

	  if(whatt == MODE_ADD)
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(!(chptr->mode.mode & MODE_INVITEONLY))
#endif
		{
		  chptr->mode.mode |= MODE_INVITEONLY;
		  *mbufw++ = '+';
		  *mbufw++ = 'i';
		  len += 2;
		  opcnt++;
		}
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(chptr->mode.mode & MODE_INVITEONLY)
#endif
		{
		  chptr->mode.mode &= ~MODE_INVITEONLY;
		  *mbufw++ = '-';
		  *mbufw++ = 'i';
		  len += 2;
		  opcnt++;
		}
	    }
	  break;

	case 'm' :
	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

	  if(MyClient(sptr))
	    {
	      if(done_m)
		break;
	      else
		done_m = YES;

	      if ( opcnt >= MAXMODEPARAMS)
		break;
	    }

	  if(whatt == MODE_ADD)
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(!(chptr->mode.mode & MODE_MODERATED))
#endif
		{
		  chptr->mode.mode |= MODE_MODERATED;
		  *mbufw++ = '+';
		  *mbufw++ = 'm';
		  len += 2;
		  opcnt++;
		}
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(chptr->mode.mode & MODE_MODERATED)
#endif
		{
		  chptr->mode.mode &= ~MODE_MODERATED;
		  *mbufw++ = '-';
		  *mbufw++ = 'm';
		  len += 2;
		  opcnt++;
		}
	    }
	  break;

	case 'n' :
	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

	  if(MyClient(sptr))
	    {
	      if(done_n)
		break;
	      else
		done_n = YES;

	      if ( opcnt >= MAXMODEPARAMS)
		break;
	    }

	  if(whatt == MODE_ADD)
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(!(chptr->mode.mode & MODE_NOPRIVMSGS))
#endif
		{
		  chptr->mode.mode |= MODE_NOPRIVMSGS;
		  *mbufw++ = '+';
		  *mbufw++ = 'n';
		  len += 2;
		  opcnt++;
		}
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(chptr->mode.mode & MODE_NOPRIVMSGS)
#endif
		{
		  chptr->mode.mode &= ~MODE_NOPRIVMSGS;
		  *mbufw++ = '-';
		  *mbufw++ = 'n';
		  len += 2;
		  opcnt++;
		}
	    }
	  break;

	case 'p' :
	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

	  if(MyClient(sptr))
	    {
#ifdef OLD_P_S
	      if(done_s_or_p)
		break;
	      else
		done_s_or_p = YES;
#else
	      if(done_p)
		break;
	      else
		done_p = YES;
#endif
	      if ( opcnt >= MAXMODEPARAMS)
		break;
	    }

	  if(whatt == MODE_ADD)
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_P_S
	      if(chptr->mode.mode & MODE_SECRET)
		{
		  if (len + 2 >= MODEBUFLEN)
		    break;
		  *mbufw++ = '-';
		  *mbufw++ = 's';
		  len += 2;
		  chptr->mode.mode &= ~MODE_SECRET;
		}
#endif
#ifdef OLD_NON_RED
	      if(!(chptr->mode.mode & MODE_PRIVATE))
#endif
		{
		  chptr->mode.mode |= MODE_PRIVATE;
		  *mbufw++ = '+';
		  *mbufw++ = 'p';
		  len += 2;
		  opcnt++;
		}
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(chptr->mode.mode & MODE_PRIVATE)
#endif
		{
		  chptr->mode.mode &= ~MODE_PRIVATE;
		  *mbufw++ = '-';
		  *mbufw++ = 'p';
		  len += 2;
		  opcnt++;
		}
	    }
	  break;

	case 's' :
	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

	  /* ickity poo, traditional +p-s nonsense */

	  if(MyClient(sptr))
	    {
#ifdef OLD_P_S
	      if(done_s_or_p)
		break;
	      else
		done_s_or_p = YES;
#else
	      if(done_s)
		break;
	      else
		done_s = YES;
#endif
	      if ( opcnt >= MAXMODEPARAMS)
		break;
	    }

	  if(whatt == MODE_ADD)
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_P_S
	      if(chptr->mode.mode & MODE_PRIVATE)
		{
		  if (len + 2 >= MODEBUFLEN)
		    break;
		  *mbufw++ = '-';
		  *mbufw++ = 'p';
		  len += 2;
		  chptr->mode.mode &= ~MODE_PRIVATE;
		}
#endif
#ifdef OLD_NON_RED
	      if(!(chptr->mode.mode & MODE_SECRET))
#endif
		{
		  chptr->mode.mode |= MODE_SECRET;
		  *mbufw++ = '+';
		  *mbufw++ = 's';
		  len += 2;
		  opcnt++;
		}
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(chptr->mode.mode & MODE_SECRET)
#endif
		{
		  chptr->mode.mode &= ~MODE_SECRET;
		  *mbufw++ = '-';
		  *mbufw++ = 's';
		  len += 2;
		  opcnt++;
		}
	    }
	  break;

	case 't' :
	  if (!isok)
	    {
	      if (MyClient(sptr) && !errsent(SM_ERR_NOOPS, &errors_sent))
		sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED), me.name, 
			   sptr->name, chptr->chname);
	      break;
	    }

	  if(MyClient(sptr))
	    {
	      if(done_t)
		break;
	      else
		done_t = YES;

	      if ( opcnt >= MAXMODEPARAMS)
		break;
	    }

	  if(whatt == MODE_ADD)
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(!(chptr->mode.mode & MODE_TOPICLIMIT))
#endif
		{
		  chptr->mode.mode |= MODE_TOPICLIMIT;
		  *mbufw++ = '+';
		  *mbufw++ = 't';
		  len += 2;
		  opcnt++;
		}
	    }
	  else
	    {
	      if (len + 2 >= MODEBUFLEN)
		break;
#ifdef OLD_NON_RED
	      if(chptr->mode.mode & MODE_TOPICLIMIT)
#endif
		{
		  chptr->mode.mode &= ~MODE_TOPICLIMIT;
		  *mbufw++ = '-';
		  *mbufw++ = 't';
		  len += 2;
		  opcnt++;
		}
	    }
	  break;

	default:
	  if (whatt == MODE_QUERY)
	    break;

	  /* only one "UNKNOWNMODE" per mode... we don't want
	  ** to generate a storm, even if it's just to a 
	  ** local client  -orabidoo
	  */
	  if (MyClient(sptr) && !errsent(SM_ERR_UNKNOWN, &errors_sent))
	    sendto_one(sptr, err_str(ERR_UNKNOWNMODE), me.name, sptr->name, c);
	  break;
	}
    }

  /*
  ** WHEW!!  now all that's left to do is put the various bufs
  ** together and send it along.
  */

  *mbufw = *mbuf2w = *pbufw = *pbuf2w = *mbufw_new = *pbufw_new = '\0';
  collapse_signs(modebuf);
/*  collapse_signs(modebuf2); */
  collapse_signs(modebuf_new);

  if(*modebuf)
    {
      sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", 
			   sptr->name, chptr->chname,
			   modebuf, parabuf);

      sendto_match_servs(chptr, cptr, ":%s MODE %s %s %s",
			 sptr->name, chptr->chname,
			 modebuf, parabuf);
    }

  if(*modebuf_new)
    {
      sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", 
			     sptr->name, chptr->chname,
			     modebuf_new, parabuf_new);

      sendto_match_cap_servs(chptr, cptr, CAP_EX, ":%s MODE %s %s %s",
			     sptr->name, chptr->chname,
			     modebuf_new, parabuf_new);
    }

		     
  return;
}

static	int	can_join(aClient *sptr, aChannel *chptr, char *key)
{
  Reg	Link	*lp;

  /*
   * If a server split happens, and the channel isn't marked as in
   * split mode, mark it as split.. if the split is over, and the 
   * channel block is marked as split, remove the split mode bit
   */

#ifdef PRESERVE_CHANNEL_ON_SPLIT
  if(chptr->mode.mode & MODE_SPLIT)
    {
      if(!server_was_split)
	{
	  chptr->mode.mode &= ~MODE_SPLIT;
	  if(chptr->users == 0)
	    chptr->mode.mode = 0;
	}
    }
  else
    {
      if(server_was_split)
	{
	  chptr->mode.mode |= MODE_SPLIT;
	}
    }
#endif

  if (*chptr->mode.key && (BadPtr(key) || mycmp(chptr->mode.key, key)))
    return (ERR_BADCHANNELKEY);

  for (lp = sptr->user->invited; lp; lp = lp->next)
    if (lp->value.chptr == chptr)
      return 0;

  if (is_banned(sptr, chptr))
    return (ERR_BANNEDFROMCHAN);

  if (chptr->mode.mode & MODE_INVITEONLY)
    return (ERR_INVITEONLYCHAN);
  
  if (chptr->mode.limit && chptr->users >= chptr->mode.limit)
    return (ERR_CHANNELISFULL);

  return 0;
}

/*
** Remove bells and commas from channel name
*/

void	clean_channelname(unsigned char *name)
{
  unsigned char *cn;
  
  for (cn = name; *cn; cn++)
    /*
     * Find bad characters and remove them, also check for
     * characters in the '\0' -> ' ' range, but +127   -Taner
     */
    if (*cn == '\007' || *cn == ' ' || *cn == ',' || (*cn > 127 && *cn <= 160))
      {
	*cn = '\0';
	/* pathological case only on longest channel name.
	** If not dealt with here, causes desynced channel ops
	** since ChannelExists() doesn't see the same channel
	** as one being joined. cute bug. Oct 11 1997, Dianora/comstud
	*/

	if(strlen((char *) name) >  CHANNELLEN) /* same thing is done in get_channel()*/
	name[CHANNELLEN] = '\0';

	return;
      }
}

/*
**  Get Channel block for chname (and allocate a new channel
**  block, if it didn't exist before).
*/
static	aChannel *get_channel(aClient *cptr,
			      char *chname,
			      int flag)
{
  Reg	aChannel *chptr;
  int	len;

  if (BadPtr(chname))
    return NULL;

  len = strlen(chname);
  if (MyClient(cptr) && len > CHANNELLEN)
    {
      len = CHANNELLEN;
      *(chname+CHANNELLEN) = '\0';
    }
  if ((chptr = find_channel(chname, (aChannel *)NULL)))
    return (chptr);

  /*
   * If a channel is created during a split make sure its marked
   * as created locally 
   */

  if (flag == CREATE)
    {
      chptr = (aChannel *)MyMalloc(sizeof(aChannel) + len);
      memset((void *)chptr, 0, sizeof(aChannel));
      strncpyzt(chptr->chname, chname, len+1);
      if (channel)
	channel->prevch = chptr;
      chptr->prevch = NULL;
      chptr->nextch = channel;
      channel = chptr;
      if(Count.myserver == 0)
	chptr->locally_created = YES;
      chptr->keep_their_modes = YES;
      (void)add_to_channel_hash_table(chname, chptr);
      Count.chan++;
    }
  return chptr;
}

static	void	add_invite(aClient *cptr,aChannel *chptr)
{
  Reg	Link	*inv, **tmp;

  del_invite(cptr, chptr);
  /*
   * delete last link in chain if the list is max length
   */
  if (list_length(cptr->user->invited) >= MAXCHANNELSPERUSER)
    {
      /*		This forgets the channel side of invitation     -Vesa
			inv = cptr->user->invited;
			cptr->user->invited = inv->next;
			free_link(inv);
*/
      del_invite(cptr, cptr->user->invited->value.chptr);
 
    }
  /*
   * add client to channel invite list
   */
  inv = make_link();
  inv->value.cptr = cptr;
  inv->next = chptr->invites;
  chptr->invites = inv;
  /*
   * add channel to the end of the client invite list
   */
  for (tmp = &(cptr->user->invited); *tmp; tmp = &((*tmp)->next))
    ;
  inv = make_link();
  inv->value.chptr = chptr;
  inv->next = NULL;
  (*tmp) = inv;
}

/*
 * Delete Invite block from channel invite list and client invite list
 */
void	del_invite(aClient *cptr,aChannel *chptr)
{
  Reg	Link	**inv, *tmp;

  for (inv = &(chptr->invites); (tmp = *inv); inv = &tmp->next)
    if (tmp->value.cptr == cptr)
      {
	*inv = tmp->next;
	free_link(tmp);
	break;
      }

  for (inv = &(cptr->user->invited); (tmp = *inv); inv = &tmp->next)
    if (tmp->value.chptr == chptr)
      {
	*inv = tmp->next;
	free_link(tmp);
	break;
      }
}

/*
**  Subtract one user from channel (and free channel
**  block, if channel became empty).
*/
static	void	sub1_from_channel(aChannel *chptr)
{
  Reg	Link *tmp;
  Link	*obtmp;

  if (--chptr->users <= 0)
    {
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
      if(server_was_split)
	{
	  chptr->mode.mode |= MODE_SPLIT;
	  /*
	   * Now, find all invite links from channel structure
	   */
	  while ((tmp = chptr->invites))
	    del_invite(tmp->value.cptr, chptr);
	}
      else
#endif
	{
	  tmp = chptr->banlist;
	  while (tmp)
	    {
	      obtmp = tmp;
	      tmp = tmp->next;
#ifdef BAN_INFO
	      MyFree(obtmp->value.banptr->banstr);
	      MyFree(obtmp->value.banptr->who);
	      MyFree(obtmp->value.banptr);
#else
	      MyFree(obtmp->value.cp);
#endif
	      free_link(obtmp);
	    }

	  tmp = chptr->exceptlist;
	  while (tmp)
	    {
	      obtmp = tmp;
	      tmp = tmp->next;
#ifdef BAN_INFO
	      MyFree(obtmp->value.banptr->banstr);
	      MyFree(obtmp->value.banptr->who);
	      MyFree(obtmp->value.banptr);
#else
	      MyFree(obtmp->value.cp);
#endif
	      free_link(obtmp);
	    }
	  chptr->banlist = chptr->exceptlist = NULL;

	  if (chptr->prevch)
	    chptr->prevch->nextch = chptr->nextch;
	  else
	    channel = chptr->nextch;
	  if (chptr->nextch)
	    chptr->nextch->prevch = chptr->prevch;

#ifdef FLUD
	  free_fluders(NULL, chptr);
#endif
	  (void)del_from_channel_hash_table(chptr->chname, chptr);
	  MyFree((char *)chptr);
	  Count.chan--;
	}
    }
}

/*
 * clear_bans_exceptions
 *
 * I could have re-written del_banid/del_exceptid to do this
 *
 * still need a bit of cleanup on the MODE -b stuff...
 * -Dianora
 */

static void clear_bans_exceptions(aClient *sptr, aChannel *chptr)
{
  static char modebuf[MODEBUFLEN];
  register Link *next_ban;
  register Link *ban;
  char *b1,*b2,*b3,*b4;
  char *mp;

  b1="";
  b2="";
  b3="";
  b4="";

  mp= modebuf;
  *mp = '\0';

  for(ban = chptr->banlist; ban; ban = ban->next)
    {
      if(!*b1)
	{
	  b1 = BANSTR(ban);
	  *mp++ = '-';
	  *mp++ = 'b';
	  *mp = '\0';
	}
      else if(!*b2)
	{
	  b2 = BANSTR(ban);
	  *mp++ = 'b';
	  *mp = '\0';
	}
      else if(!*b3)
	{
	  b3 = BANSTR(ban);
	  *mp++ = 'b';
	  *mp = '\0';
	}
      else if(!*b4)
	{
	  b4 = BANSTR(ban);
	  *mp++ = 'b';
	  *mp = '\0';

	  sendto_channel_butserv(chptr, &me,
				 ":%s MODE %s %s %s %s %s %s",
				 sptr->name,chptr->chname,modebuf,b1,b2,b3,b4);
	  b1="";
	  b2="";
	  b3="";
	  b4="";

	  mp = modebuf;
	  *mp = '\0';
	}
    }

  if(*modebuf)
    sendto_channel_butserv(chptr, &me,
			   ":%s MODE %s %s %s %s %s %s",
			   sptr->name,chptr->chname,modebuf,b1,b2,b3,b4);
  b1="";
  b2="";
  b3="";
  b4="";

  mp= modebuf;
  *mp = '\0';

  for(ban = chptr->exceptlist; ban; ban = ban->next)
    {
      if(!*b1)
	{
	  b1 = BANSTR(ban);
	  *mp++ = '-';
	  *mp++ = 'e';
	  *mp = '\0';
	}
      else if(!*b2)
	{
	  b2 = BANSTR(ban);
	  *mp++ = 'e';
	  *mp = '\0';
	}
      else if(!*b3)
	{
	  b3 = BANSTR(ban);
	  *mp++ = 'e';
	  *mp = '\0';
	}
      else if(!*b4)
	{
	  b4 = BANSTR(ban);
	  *mp++ = 'e';
	  *mp = '\0';

	  sendto_channel_butserv(chptr, &me,
				 ":%s MODE %s %s %s %s %s %s",
				 sptr->name,chptr->chname,modebuf,b1,b2,b3,b4);
	  b1="";
	  b2="";
	  b3="";
	  b4="";
	  mp = modebuf;
	  *mp = '\0';
	}
    }

  if(*modebuf)
    sendto_channel_butserv(chptr, &me,
			   ":%s MODE %s %s %s %s %s %s",
			   sptr->name,chptr->chname,modebuf,b1,b2,b3,b4);

  for(ban = chptr->banlist; ban; ban = next_ban)
    {
      next_ban = ban->next;
#ifdef BAN_INFO
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
#else
      MyFree(ban->value.cp);
#endif
      free_link(ban);
    }

  for(ban = chptr->exceptlist; ban; ban = next_ban)
    {
      next_ban = ban->next;
#ifdef BAN_INFO
      MyFree(ban->value.banptr->banstr);
      MyFree(ban->value.banptr->who);
      MyFree(ban->value.banptr);
#else
      MyFree(ban->value.cp);
#endif
      free_link(ban);
    }

  chptr->banlist = chptr->exceptlist = NULL;
}


#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)

/*
 * check_still_split()
 *
 * inputs	-NONE
 * output	-NONE
 * side effects -
 * Check to see if the server split timer has expired, if so
 * check to see if there are now a decent number of servers connected
 * so I can consider this split over.
 *
 * I'm hoping that also including the network wide count of users
 * will be unnecessary, (as the "smallnet patch" does) but if
 * its demonstrated that its needed, this is where it should go.
 *
 * -Dianora
 */

static void check_still_split()
{
  if((server_split_time + server_split_recovery_time) < NOW)
    {
      /* If needed..., split_smallnet_users will also
      * have to be defined */

      /* if((Count.server > split_smallnet_size) &&
	    (Count.total > split_smallnet_users))
	    */

      if(Count.server > split_smallnet_size)
	{
	  /* server hasn't been split for a while.
	   * -Dianora
	   */
	  server_was_split = NO;
	  cold_start = NO;
	}
      else
	{
	  server_split_time = NOW; /* still split */
	  server_was_split = YES;
	}
    }
}
#endif

/*
** m_join
**	parv[0] = sender prefix
**	parv[1] = channel
**	parv[2] = channel password (key)
*/
int	m_join(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  static char	jbuf[BUFSIZE];
  Reg	Link	*lp;
  Reg	aChannel *chptr;
  Reg	char	*name, *key = NULL;
  int	i, flags = 0;
#ifdef USE_ALLOW_OP
  int   allow_op=YES;
#endif
  char	*p = NULL, *p2 = NULL;
#ifdef ANTI_SPAMBOT
  int   successful_join_count = 0; /* Number of channels successfully joined */
#endif
  
  if (!(sptr->user))
    {
      /* something is *fucked* - bail */
      return 0;
    }

  if (parc < 2 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "JOIN");
      return 0;
    }


#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
	defined(NO_JOIN_ON_SPLIT)

  /* Check to see if the timer has timed out, and if so, see if
   * there are a decent number of servers now connected 
   * to consider any possible split over.
   * -Dianora
   */

  check_still_split();

#endif

  *jbuf = '\0';
  /*
  ** Rebuild list of channels joined to be the actual result of the
  ** JOIN.  Note that "JOIN 0" is the destructive problem.
  */
  for (i = 0, name = strtoken(&p, parv[1], ","); name;
       name = strtoken(&p, (char *)NULL, ","))
    {
      clean_channelname((unsigned char *)name);
      if (*name == '&' && !MyConnect(sptr))
	continue;
      if (*name == '0' && !atoi(name))
        *jbuf = '\0';
      else if (!IsChannelName(name))
	{
	  if (MyClient(sptr))
	    sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL),
		       me.name, parv[0], name);
	  continue;
	}

#ifdef PRESERVE_CHANNEL_ON_SPLIT
      /* If from a cold start, there were never any channels
       * joined, hence all of them must be considered off limits
       * until this server joins the network
       */
      if(cold_start && MyClient(sptr))
	{
	  if(server_was_split && (*name == '#') && !IsAnOper(sptr))
	    {
	      sendto_one(sptr, err_str(ERR_NOJOINSPLIT),
			 me.name, parv[0], name);
	      continue;
	    }
	}
#endif
#ifdef NO_JOIN_ON_SPLIT
      if(server_was_split && MyClient(sptr) &&
	 (*name == '#') && !IsAnOper(sptr))
	{
	  sendto_one(sptr, err_str(ERR_NOJOINSPLIT),
		     me.name, parv[0], name);
	  continue;
	}
#endif
      if (*jbuf)
	(void)strcat(jbuf, ",");
      (void)strncat(jbuf, name, sizeof(jbuf) - i - 1);
      i += strlen(name)+1;
    }
  /*	(void)strcpy(parv[1], jbuf);*/

  p = NULL;
  if (parv[2])
    key = strtoken(&p2, parv[2], ",");
  parv[2] = NULL;	/* for m_names call later, parv[parc] must == NULL */
  for (name = strtoken(&p, jbuf, ","); name;
       key = (key) ? strtoken(&p2, NULL, ",") : NULL,
	 name = strtoken(&p, NULL, ","))
    {
      /*
      ** JOIN 0 sends out a part for all channels a user
      ** has joined.
      */
      if (*name == '0' && !atoi(name))
	{
	  if (sptr->user->channel == NULL)
	    continue;
	  while ((lp = sptr->user->channel))
	    {
	      chptr = lp->value.chptr;
	      sendto_channel_butserv(chptr, sptr, PartFmt,
				     parv[0], chptr->chname);
	      remove_user_from_channel(sptr, chptr);
	    }
/*
  Added /quote set for SPAMBOT

int spam_time = MIN_JOIN_LEAVE_TIME;
int spam_num = MAX_JOIN_LEAVE_COUNT;
*/
#ifdef ANTI_SPAMBOT 	  /* Dianora */

	  if( MyConnect(sptr) && !IsAnOper(sptr) )
	    {
	      if(spam_num && (sptr->join_leave_count >= spam_num))
		{
		  sendto_ops_lev(SPY_LEV,
				     "User %s (%s@%s) is a possible spambot",
				     sptr->name,
				     sptr->user->username, sptr->user->host);
		  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
		}
	      else
		{
		  int t_delta;

		  if( (t_delta = (NOW - sptr->last_leave_time)) >
		      JOIN_LEAVE_COUNT_EXPIRE_TIME)
		    {
		      int decrement_count;
		      decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

		      if(decrement_count > sptr->join_leave_count)
			sptr->join_leave_count = 0;
		      else
			sptr->join_leave_count -= decrement_count;
		    }
		  else
		    {
		      if((NOW - (sptr->last_join_time)) < spam_time)
			{
			  /* oh, its a possible spambot */
			  sptr->join_leave_count++;
			}
		    }
		  sptr->last_leave_time = NOW;
		}
	    }
#endif
	  sendto_match_servs(NULL, cptr, ":%s JOIN 0", parv[0]);
	  continue;
	}
      
      if (MyConnect(sptr))
	{
	  /*
	  ** local client is first to enter previously nonexistent
	  ** channel so make them (rightfully) the Channel
	  ** Operator.
	  */
	  flags = (ChannelExists(name)) ? 0 : CHFL_CHANOP;

	/* if its not a local channel, or isn't an oper
	 * and server has been split
	 */

#ifdef NO_CHANOPS_WHEN_SPLIT
	  if((*name != '&') && !IsAnOper(sptr) && server_was_split)
	    {
	      if(server_was_split)
		{
		  allow_op = NO;

		  if(!IsRestricted(sptr) && (flags == CHFL_CHANOP))
		sendto_one(sptr,":%s NOTICE %s :*** Notice -- Due to a network split, you can not obtain channel operator status in a new channel at this time.",
			   me.name,
			   sptr->name);
		}
	    }
#endif

	  if ((sptr->user->joined >= MAXCHANNELSPERUSER) &&
	     (!IsAnOper(sptr) || (sptr->user->joined >= MAXCHANNELSPERUSER*3)))
	    {
	      sendto_one(sptr, err_str(ERR_TOOMANYCHANNELS),
			 me.name, parv[0], name);
#ifdef ANTI_SPAMBOT
	      if(successful_join_count)
		sptr->last_join_time = NOW;
#endif
	      return 0;
	    }


#ifdef ANTI_SPAMBOT 	  /* Dianora */
          if(flags == 0)	/* if channel doesn't exist, don't penalize */
            successful_join_count++;
          if( spam_num && (sptr->join_leave_count >= spam_num))
            { 
              /* Its already known as a possible spambot */
 
              if(sptr->oper_warn_count_down > 0)  /* my general paranoia */
                sptr->oper_warn_count_down--;
              else
                sptr->oper_warn_count_down = 0;
 
              if(sptr->oper_warn_count_down == 0)
                {
                  sendto_ops_lev(SPY_LEV,
		    "User %s (%s@%s) trying to join %s is a possible spambot",
                             sptr->name,
                             sptr->user->username,
			     sptr->user->host,
			     name);	
                  sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
                }
#ifndef ANTI_SPAMBOT_WARN_ONLY
              return 0; /* Don't actually JOIN anything, but don't let
                           spambot know that */
#endif
            }
#endif
	}
      else
	{
	  /*
	  ** complain for remote JOINs to existing channels
	  ** (they should be SJOINs) -orabidoo
	  */
	  if (!ChannelExists(name))
	    ts_warn("User on %s remotely JOINing new channel", 
		    sptr->user->server);
	}

      chptr = get_channel(sptr, name, CREATE);

      if(chptr)
	{
	  if (IsMember(sptr, chptr))	/* already a member, ignore this */
	    continue;
	}
      else
	{
	  sendto_one(sptr,
		     ":%s %d %s %s :Sorry, cannot join channel.",
		     me.name, i, parv[0], name);
#ifdef ANTI_SPAMBOT
	  if(successful_join_count > 0)
	    successful_join_count--;
#endif
	  continue;
	}

      if (MyConnect(sptr) && (i = can_join(sptr, chptr, key)))
	{
	  sendto_one(sptr,
		     ":%s %d %s %s :Sorry, cannot join channel.",
		     me.name, i, parv[0], name);

#ifdef ANTI_SPAMBOT
	  if(successful_join_count > 0)
	    successful_join_count--;
#endif
	  continue;
	}

      /*
      **  Complete user entry to the new channel (if any)
      */

#ifdef USE_ALLOW_OP
      if(allow_op)
	add_user_to_channel(chptr, sptr, flags);
      else
	add_user_to_channel(chptr, sptr, 0);
#else
      add_user_to_channel(chptr, sptr, flags);
#endif
      /*
      **  Set timestamp if appropriate, and propagate
      */
      if (MyClient(sptr) && flags == CHFL_CHANOP)
	{
	  chptr->channelts = timeofday;
#ifdef USE_ALLOW_OP
	  if(allow_op)
	    sendto_match_servs(chptr, cptr,
			       ":%s SJOIN %ld %s + :@%s", me.name,
			       chptr->channelts, name, parv[0]);
	  else
	    sendto_match_servs(chptr, cptr,
			       ":%s SJOIN %ld %s + :%s", me.name,
			       chptr->channelts, name, parv[0]);
#else
	  sendto_match_servs(chptr, cptr,
			     ":%s SJOIN %ld %s + :@%s", me.name,
			     chptr->channelts, name, parv[0]);

#endif
	}
      else if (MyClient(sptr))
	{
	  sendto_match_servs(chptr, cptr,
			     ":%s SJOIN %ld %s + :%s", me.name,
			     chptr->channelts, name, parv[0]);
	}
      else
	sendto_match_servs(chptr, cptr, ":%s JOIN :%s", parv[0],
			   name);

      /*
      ** notify all other users on the new channel
      */
      sendto_channel_butserv(chptr, sptr, ":%s JOIN :%s",
			     parv[0], name);

      if (MyClient(sptr))
	{
	  del_invite(sptr, chptr);
        /*  call m_names BEFORE spewing the topic, so people actually see
        **  the topic, and stop whining.  --SuperTaz
        */
          parv[1] = name;
          (void)m_names(cptr, sptr, 2, parv);
	  if (chptr->topic[0] != '\0')
	    {
	      sendto_one(sptr, rpl_str(RPL_TOPIC), me.name,
			 parv[0], name, chptr->topic);
#ifdef TOPIC_INFO
	      sendto_one(sptr, rpl_str(RPL_TOPICWHOTIME),
			 me.name, parv[0], name,
			 chptr->topic_nick,
			 chptr->topic_time);
#endif
	    }
	}
    }

#ifdef ANTI_SPAMBOT
  if(MyConnect(sptr) && successful_join_count)
    sptr->last_join_time = NOW;
#endif
  return 0;
}

/*
** m_part
**	parv[0] = sender prefix
**	parv[1] = channel
*/
int	m_part(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  Reg	aChannel *chptr;
  char	*p, *name;

  if (parc < 2 || parv[1][0] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "PART");
      return 0;
    }

  name = strtoken( &p, parv[1], ",");

#ifdef ANTI_SPAMBOT	/* Dianora */
      /* if its my client, and isn't an oper */

      if (name && MyConnect(sptr) && !IsAnOper(sptr))
	{
	  if(spam_num && (sptr->join_leave_count >= spam_num))
	    {
	      sendto_ops_lev(SPY_LEV,"User %s (%s@%s) is a possible spambot",
			 sptr->name,
			 sptr->user->username, sptr->user->host);
	      sptr->oper_warn_count_down = OPER_SPAM_COUNTDOWN;
	    }
	  else
	    {
	      int t_delta;

	      if( (t_delta = (NOW - sptr->last_leave_time)) >
		  JOIN_LEAVE_COUNT_EXPIRE_TIME)
		{
		  int decrement_count;
		  decrement_count = (t_delta/JOIN_LEAVE_COUNT_EXPIRE_TIME);

		  if(decrement_count > sptr->join_leave_count)
		    sptr->join_leave_count = 0;
		  else
		    sptr->join_leave_count -= decrement_count;
		}
	      else
		{
		  if( (NOW - (sptr->last_join_time)) < spam_time)
		    {
		      /* oh, its a possible spambot */
		      sptr->join_leave_count++;
		    }
		}
	      sptr->last_leave_time = NOW;
	    }
	}
#endif

  while ( name )
    {
      chptr = get_channel(sptr, name, 0);
      if (!chptr)
	{
	  sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL),
		     me.name, parv[0], name);
	  name = strtoken(&p, (char *)NULL, ",");
	  continue;
	}

      if (!IsMember(sptr, chptr))
	{
	  sendto_one(sptr, err_str(ERR_NOTONCHANNEL),
		     me.name, parv[0], name);
	  name = strtoken(&p, (char *)NULL, ",");
	  continue;
	}
      /*
      **  Remove user from the old channel (if any)
      */
	    
      sendto_match_servs(chptr, cptr, PartFmt, parv[0], name);
	    
      sendto_channel_butserv(chptr, sptr, PartFmt, parv[0], name);
      remove_user_from_channel(sptr, chptr);
      name = strtoken(&p, (char *)NULL, ",");
    }
  return 0;
}

/*
** m_kick
**	parv[0] = sender prefix
**	parv[1] = channel
**	parv[2] = client to kick
**	parv[3] = kick comment
*/
/*
 * I've removed the multi channel kick, and the multi user kick
 * though, there are still remnants left ie..
 * "name = strtoken(&p, parv[1], ",");" in a normal kick
 * it will just be "KICK #channel nick"
 * A strchr() is going to be faster than a strtoken(), so rewritten
 * to use a strchr()
 *
 * It appears the original code was supposed to support 
 * "kick #channel1,#channel2 nick1,nick2,nick3." For example, look at
 * the original code for m_topic(), where 
 * "topic #channel1,#channel2,#channel3... topic" was supported.
 *
 * -Dianora
 */
int	m_kick(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aClient *who;
  aChannel *chptr;
  int	chasing = 0;
  char	*comment;
  char  *name;
  char  *p = (char *)NULL;
  char  *user;
  char  *p2 = (char *)NULL;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "KICK");
      return 0;
    }
  if (IsServer(sptr))
    sendto_ops("KICK from %s for %s %s",
	       parv[0], parv[1], parv[2]);
  comment = (BadPtr(parv[3])) ? parv[0] : parv[3];
  if (strlen(comment) > (size_t) TOPICLEN)
    comment[TOPICLEN] = '\0';

  *buf = '\0';
  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  chptr = get_channel(sptr, name, !CREATE);
  if (!chptr)
    {
      sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL),
		 me.name, parv[0], name);
      return(0);
    }

  /* You either have chan op privs, or you don't -Dianora */
  /* orabidoo and I discussed this one for a while...
   * I hope he approves of this code, (he did) users can get quite confused...
   *    -Dianora
   */

  if (!IsServer(sptr) && !is_chan_op(sptr, chptr) ) 
    { 
      /* was a user, not a server, and user isn't seen as a chanop here */
      
      if(MyConnect(sptr))
	{
	  /* user on _my_ server, with no chanops.. so go away */
	  
	  sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
		     me.name, parv[0], chptr->chname);
	  return(0);
	}

      if(chptr->channelts == 0)
	{
	  /* If its a TS 0 channel, do it the old way */
	  
	  sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
		     me.name, parv[0], chptr->chname);
	  return(0);
	}

      /* Its a user doing a kick, but is not showing as chanop locally
       * its also not a user ON -my- server, and the channel has a TS.
       * There are two cases we can get to this point then...
       *
       *     1) connect burst is happening, and for some reason a legit
       *        op has sent a KICK, but the SJOIN hasn't happened yet or 
       *        been seen. (who knows.. due to lag...)
       *
       *     2) The channel is desynced. That can STILL happen with TS
       *	
       *     Now, the old code roger wrote, would allow the KICK to 
       *     go through. Thats quite legit, but lets weird things like
       *     KICKS by users who appear not to be chanopped happen,
       *     or even neater, they appear not to be on the channel.
       *     This fits every definition of a desync, doesn't it? ;-)
       *     So I will allow the KICK, otherwise, things are MUCH worse.
       *     But I will warn it as a possible desync.
       *
       *     -Dianora
       */

      /*	  sendto_one(sptr, err_str(ERR_DESYNC),
       * 	   me.name, parv[0], chptr->chname);
       */

      /*
       * After more discussion with orabidoo...
       *
       * The code was sound, however, what happens if we have +h (TS4)
       * and some servers don't understand it yet? 
       * we will be seeing servers with users who appear to have
       * no chanops at all, merrily kicking users....
       * -Dianora
       */
    }

  p = strchr(parv[2],',');
  if(p)
    *p = '\0';
  user = parv[2]; /* strtoken(&p2, parv[2], ","); */

  if (!(who = find_chasing(sptr, user, &chasing)))
    {
      sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		 me.name, parv[0], user, name);
      return(0);
    }

  if (IsMember(who, chptr))
    {
      sendto_channel_butserv(chptr, sptr,
			     ":%s KICK %s %s :%s", parv[0],
			     name, who->name, comment);
      sendto_match_servs(chptr, cptr,
			 ":%s KICK %s %s :%s",
			 parv[0], name,
			 who->name, comment);
      remove_user_from_channel(who, chptr);
    }
  else
    sendto_one(sptr, err_str(ERR_USERNOTINCHANNEL),
	       me.name, parv[0], user, name);

  return (0);
}

int	count_channels(aClient *sptr)
{
  Reg	aChannel	*chptr;
  Reg	int	count = 0;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    count++;
  return (count);
}

/* m_knock
**    parv[0] = sender prefix
**    parv[1] = channel
**  The KNOCK command has the following syntax:
**   :<sender> KNOCK <channel>
**  If a user is not banned from the channel they can use the KNOCK
**  command to have the server NOTICE the channel operators notifying
**  they would like to join.  Helpful if the channel is invite-only, the
**  key is forgotten, or the channel is full (INVITE can bypass each one
**  of these conditions.  Concept by Dianora <db@db.net> and written by
**  David-R <rientjes@mail.whidbey.net>
**
** Just some flood control added here, five minute delay between each
** KNOCK -Dianora
**/
int	m_knock(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  Reg	aChannel	*chptr;
  char	*p, *name;

  /* anti flooding code,
   * I did have this in parse.c with a table lookup
   * but I think this will be less inefficient doing it in each
   * function that absolutely needs it
   *
   * -Dianora
   */
  static time_t last_used=0L;

  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS), me.name, parv[0],
		 "KNOCK");
      return 0;
    }


  /* We will cut at the first comma reached, however we will not *
   * process anything afterwards.  -- David-R                    */

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  if (!IsChannelName(name) || !(chptr = find_channel(name, NullChn)))
    {
      sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
		 name);
      return 0;
    }

  if(!((chptr->mode.mode & MODE_INVITEONLY) ||
       (*chptr->mode.key) ||
       (chptr->mode.limit && chptr->users >= chptr->mode.limit )
       ))
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- Channel is open!",
		 me.name,
		 sptr->name);
      return 0;
    }

  /* don't allow a knock if the user is banned, or the channel is secret */
  if ((chptr->mode.mode & MODE_SECRET) || is_banned(sptr, chptr))
    {
      sendto_one(sptr, err_str(ERR_CANNOTSENDTOCHAN), me.name, parv[0],
		 name);
      return 0;
    }

  /* if the user is already on channel, then a knock is pointless! */
  if (IsMember(sptr, chptr))
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- You are on channel already!",
		 me.name,
		 sptr->name);
      return 0;
    }

  /* flood control server wide, clients on KNOCK
   * opers are not flood controlled.
   */

  if(!IsAnOper(sptr))
    {
      if((last_used + PACE_WAIT) > NOW)
	return 0;
      else
	last_used = NOW;
    }

  /* flood control individual clients on KNOCK
   * the ugly possibility still exists, 400 clones could all KNOCK
   * on a channel at once, flooding all the ops. *ugh*
   * Remember when life was simpler?
   * -Dianora
   */

  /* opers are not flow controlled here */
  if( !IsAnOper(sptr) && (sptr->last_knock + KNOCK_DELAY) > NOW)
    {
      sendto_one(sptr,":%s NOTICE %s :*** Notice -- Wait %d seconds before another knock",
		 me.name,
		 sptr->name,
		 KNOCK_DELAY - (NOW - sptr->last_knock));
      return 0;
    }

  sptr->last_knock = NOW;

  sendto_one(sptr,":%s NOTICE %s :*** Notice -- Your KNOCK has been delivered",
		 me.name,
		 sptr->name);

  /* using &me and me.name won't deliver to clients not on this server
   * so, the notice will have to appear from the "knocker" ick.
   *
   * Ideally, KNOCK would be routable. Also it would be nice to add
   * a new channel mode. Perhaps some day.
   * For now, clients that don't want to see KNOCK requests will have
   * to use client side filtering. 
   *
   * -Dianora
   */

  {
    char message[350];

    /* bit of paranoid, be a shame if it cored for this -Dianora */
    if(sptr->user)
      {
	ircsprintf(message,"KNOCK: %s (%s [%s@%s] has asked for an invite)",
		   chptr->chname,
		   sptr->name,
		   sptr->user->username,
		   sptr->user->host);
	sendto_channel_type_notice(cptr, chptr, MODE_CHANOP, message);
      }

    /* There is a problem with the code fragment below...
     * The problem is, s_user.c checks to see if the sender
     * is actually chanop on the channel
     */

    /* bit of paranoia, be a shame if it cored for this -Dianora */
    /*
    if(sptr->user)
      sendto_channel_type(cptr, sptr, chptr, MODE_CHANOP,
	  ":%s PRIVMSG @%s :KNOCK: %s (%s [%s@%s] has asked for an invite)",
			  parv[0],
			  chptr->chname,
			  chptr->chname,
			  sptr->name,
			  sptr->user->username,
			  sptr->user->host);
			  */
  }
  return 0;
}

/*
** m_topic
**	parv[0] = sender prefix
**	parv[1] = topic text
*/
int	m_topic(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  aChannel *chptr = NullChn;
  char	*topic = (char *)NULL, *name, *p = (char *)NULL;
  
  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "TOPIC");
      return 0;
    }

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  /* multi channel topic's are now known to be used by cloners
   * trying to flood off servers.. so disable it *sigh* - Dianora
   */

  if (name && IsChannelName(name))
    {
      chptr = find_channel(name, NullChn);
      if (!chptr)
        {
          sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (!IsMember(sptr, chptr))
        {
          sendto_one(sptr, err_str(ERR_NOTONCHANNEL), me.name, parv[0],
              name);
          return 0;
        }

      if (parc > 2) /* setting topic */
	topic = parv[2];

      if(topic)	/* a little extra paranoia never hurt */
	{
	  if ((chptr->mode.mode & MODE_TOPICLIMIT) == 0 ||
	       is_chan_op(sptr, chptr))
	    {
	      /* setting a topic */
	      strncpyzt(chptr->topic, topic, sizeof(chptr->topic));
#ifdef TOPIC_INFO
	      strcpy(chptr->topic_nick, sptr->name);
	      chptr->topic_time = timeofday;
#endif
	      sendto_match_servs(chptr, cptr,":%s TOPIC %s :%s",
				 parv[0], chptr->chname,
				 chptr->topic);
	      sendto_channel_butserv(chptr, sptr, ":%s TOPIC %s :%s",
				     parv[0],
				     chptr->chname, chptr->topic);
	    }
	  else
	    sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
		       me.name, parv[0], chptr->chname);
	}
      else  /* only asking  for topic  */
	{
	  if (chptr->topic[0] == '\0')
	    sendto_one(sptr, rpl_str(RPL_NOTOPIC),
		       me.name, parv[0], chptr->chname);
	  else
	    {
	      sendto_one(sptr, rpl_str(RPL_TOPIC),
			 me.name, parv[0],
			 chptr->chname, chptr->topic);
#ifdef TOPIC_INFO
	      sendto_one(sptr, rpl_str(RPL_TOPICWHOTIME),
			 me.name, parv[0], chptr->chname,
			 chptr->topic_nick,
			 chptr->topic_time);
#endif
	    }
	}
    }
  else
    {
      sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL),
		 me.name, parv[0], name);
    }

  return 0;
}

/*
** m_invite
**	parv[0] - sender prefix
**	parv[1] - user to invite
**	parv[2] - channel number
*/
int	m_invite(aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{
  aClient *acptr;
  aChannel *chptr;
  int need_invite=NO;

  if (parc < 3 || *parv[1] == '\0')
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "INVITE");
      return -1;
    }

  if (!(acptr = find_person(parv[1], (aClient *)NULL)))
    {
      sendto_one(sptr, err_str(ERR_NOSUCHNICK),
		 me.name, parv[0], parv[1]);
      return 0;
    }
  clean_channelname((unsigned char *)parv[2]);

  if (!IsChannelName(parv[2]))
    {
	if (MyClient(sptr))
	   sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
		      parv[2]);
	return 0;
    }

  if (!(chptr = find_channel(parv[2], NullChn)))
    {
      sendto_prefix_one(acptr, sptr, ":%s INVITE %s :%s",
			parv[0], parv[1], parv[2]);
      return 0;
    }
  
  if (chptr && !IsMember(sptr, chptr))
    {
      sendto_one(sptr, err_str(ERR_NOTONCHANNEL),
		 me.name, parv[0], parv[2]);
      return -1;
    }

  if (IsMember(acptr, chptr))
    {
      sendto_one(sptr, err_str(ERR_USERONCHANNEL),
		 me.name, parv[0], parv[1], parv[2]);
      return 0;
    }

  if (chptr && (chptr->mode.mode & MODE_INVITEONLY))
    {
      need_invite = YES;
      if (!is_chan_op(sptr, chptr))
	{
	  sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
		     me.name, parv[0], chptr->chname);
	  return -1;
	}
      else if (!IsMember(sptr, chptr))
	{
	  sendto_one(sptr, err_str(ERR_CHANOPRIVSNEEDED),
		     me.name, parv[0],
		     ((chptr) ? (chptr->chname) : parv[2]));
	  return -1;
	}
    }

  if (MyConnect(sptr))
    {
      sendto_one(sptr, rpl_str(RPL_INVITING), me.name, parv[0],
		 acptr->name, ((chptr) ? (chptr->chname) : parv[2]));
      if (acptr->user->away)
	sendto_one(sptr, rpl_str(RPL_AWAY), me.name, parv[0],
		   acptr->name, acptr->user->away);

      /* Send a NOTICE to all channel operators concerning chanops who  *
       * INVITE other users to the channel when it is invite-only (+i). *
       * The NOTICE is sent from the local server.  -- David-R          */

      /* Only allow this invite notice if the channel is also not +p
       * -Dianora
       */

      if (chptr && (chptr->mode.mode & MODE_INVITEONLY)
	  && !(chptr->mode.mode & MODE_PRIVATE))
	{ 
	  char message[300];

	  /*
	  sprintf(message, "INVITE: %s (%s invited %s)", chptr->chname, sptr->name, acptr->name);
	  sendto_channel_type_notice(cptr, chptr, MODE_CHANOP,
				     message);
				     */
	  /* bit of paranoia, be a shame if it cored for this -Dianora */
	  if(acptr->user)
	    {
	      (void)ircsprintf(message,
		      "INVITE: %s (%s invited %s [%s@%s])",
				chptr->chname,
				sptr->name,
				acptr->name,
				acptr->user->username,
				acptr->user->host);

	      /* Note the horrible kludge here of "PRIVMSG"
	       * in the arguments, this is to ensure that p4 in 
	       * sendto_channel_type() in send.c is the message payload
	       * for non CHW type servers
	       * -Dianora
	       */
	      sendto_channel_type(cptr, sptr, chptr, MODE_CHANOP,
				  ":%s %s @%s :%s",
				  parv[0], "PRIVMSG", chptr->chname,
				  message);
	    }
	}
    }

  /* don't attach anything to the invite links if don't need_invite
   * to channel, i.e. channel is not +i. If it goes +i after the invite
   * tough. -Dianora
   */

  /* Do not send local channel invites to users if they are not on the *
   * same server as the person sending the INVITE message.  -- David-R */
  if (!MyConnect(acptr) && (*chptr->chname == '&'))
    return 0;

  if (MyConnect(acptr))
    if (chptr && sptr->user && is_chan_op(sptr, chptr) && need_invite)
      add_invite(acptr, chptr);
  sendto_prefix_one(acptr, sptr, ":%s INVITE %s :%s",parv[0],
		    acptr->name, ((chptr) ? (chptr->chname) : parv[2]));
  return 0;
}


/*
** m_list
**      parv[0] = sender prefix
**      parv[1] = channel
*/
int	m_list(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  aChannel *chptr;
  char	*name, *p = NULL;

  /* throw away non local list requests that do get here -Dianora */
  if(!MyConnect(sptr))
    return 0;

  sendto_one(sptr, rpl_str(RPL_LISTSTART), me.name, parv[0]);

  if (parc < 2 || BadPtr(parv[1]))
    {
      SetDoingList(sptr);     /* only set if its a full list */
      for (chptr = channel; chptr; chptr = chptr->nextch)
	{
	  if (!sptr->user ||
	      (SecretChannel(chptr) && !IsMember(sptr, chptr)))
	    continue;
	  sendto_one(sptr, rpl_str(RPL_LIST), me.name, parv[0],
		     ShowChannel(sptr, chptr)?chptr->chname:"*",
		     chptr->users,
		     ShowChannel(sptr, chptr)?chptr->topic:"");
	}
      sendto_one(sptr, rpl_str(RPL_LISTEND), me.name, parv[0]);
      ClearDoingList(sptr);   /* yupo, its over */
      return 0;
    }

  /* Don't route list, no need for it - Dianora */
  /*
    if (hunt_server(cptr, sptr, ":%s LIST %s %s", 2, parc, parv))
      return 0;
      */

  p = strchr(parv[1],',');
  if(p)
    *p = '\0';
  name = parv[1]; /* strtoken(&p, parv[1], ","); */

  /* while(name) */
  if(name)
    {
      chptr = find_channel(name, NullChn);
      if (chptr && ShowChannel(sptr, chptr) && sptr->user)
	sendto_one(sptr, rpl_str(RPL_LIST), me.name, parv[0],
		   ShowChannel(sptr,chptr) ? name : "*",
		   chptr->users, chptr->topic);
      /*      name = strtoken(&p, (char *)NULL, ","); */
    }
  sendto_one(sptr, rpl_str(RPL_LISTEND), me.name, parv[0]);
  return 0;
}


/************************************************************************
 * m_names() - Added by Jto 27 Apr 1989
 ************************************************************************/

/*
** m_names
**	parv[0] = sender prefix
**	parv[1] = channel
*/
/*
 * Modified to report possible names abuse
 * drastically modified to not show all names, just names
 * on given channel names.
 *
 * -Dianora
 */
/* maximum names para to show to opers when abuse occurs */
#define TRUNCATED_NAMES 20

int	m_names( aClient *cptr,
		 aClient *sptr,
		 int parc,
		 char *parv[])
{ 
  Reg	aChannel *chptr;
  Reg	aClient *c2ptr;
  Reg	Link	*lp;
  aChannel *ch2ptr = NULL;
  int	idx, flag, len, mlen;
  char	*s, *para = parc > 1 ? parv[1] : NULL;
  int comma_count=0;
  int char_count=0;

  /* Don't route names, no need for it -Dianora */
  /*
  if (parc > 1 &&
      hunt_server(cptr, sptr, ":%s NAMES %s %s", 2, parc, parv))
    return 0;
    */

  /* And throw away non local names requests that do get here -Dianora */
  if(!MyConnect(sptr))
    return 0;

  /*
   * names is called by m_join() when client joins a channel,
   * hence I cannot easily rate limit it.. perhaps that won't
   * be necessary now that remote names is prohibited.
   *
   * -Dianora
   */

  mlen = strlen(me.name) + NICKLEN + 7;

  if (!BadPtr(para))
    {
      /* Here is the lamer detection code
       * P.S. meta, GROW UP
       * -Dianora 
       */
      for(s = para; *s; s++)
	{
	  char_count++;
	  if(*s == ',')
	    comma_count++;
	  if(comma_count > 1)
	    {
	      if(char_count > TRUNCATED_NAMES)
		para[TRUNCATED_NAMES] = '\0';
	      else
		{
		  s++;
		  *s = '\0';
		}
	      sendto_realops("/names abuser %s [%s]",
			     para,
			     get_client_name(sptr,FALSE));
	      sendto_one(sptr, err_str(ERR_TOOMANYTARGETS),
			 me.name, sptr->name, "NAMES");
	      return 0;
	    }
	}

      s = strchr(para, ',');
      if (s)
	*s = '\0';
      clean_channelname((unsigned char *)para);
      chptr = find_channel(para, (aChannel *)NULL);
    }
  else
    {
      sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
		 "NAMES");
      return(1);
    }

  /* If no channel found, report it and return */
  if (!chptr)
    {
      sendto_one(sptr, err_str(ERR_NOSUCHCHANNEL), me.name, parv[0],
		 para);
      return 1;
    }

  /* Just do the channel name given */
  if (!ShowChannel(sptr, chptr))
    {
      /* -- users on these are not listed, and sptr is not on the channel */
      sendto_one(sptr, rpl_str(RPL_ENDOFNAMES), me.name, parv[0],
		 para);
      return(1);
    }

  /* Find users on same channel (defined by chptr) */
  buf[0] = '*';
  buf[1] = ' ';
  buf[2] = '\0';
  len = strlen(chptr->chname);
  (void)strcpy(buf + 2, chptr->chname);
  (void)strcpy(buf + 2 + len, " :");

  if (PubChannel(chptr))
    *buf = '=';
  else if (SecretChannel(chptr))
    *buf = '@';
  idx = len + 4;

  for (lp = chptr->members; lp; lp = lp->next)
    {
      c2ptr = lp->value.cptr;
      if (IsInvisible(c2ptr) && !IsMember(sptr,chptr))
	continue;
      if (lp->flags & CHFL_CHANOP)
	{
	  (void)strcat(buf, "@");
	  idx++;
	}
      else if (lp->flags & CHFL_VOICE)
	{
	  (void)strcat(buf, "+");
	  idx++;
	}
      (void)strncat(buf, c2ptr->name, NICKLEN);
      idx += strlen(c2ptr->name) + 1;
      flag = 1;
      (void)strcat(buf," ");
      if (mlen + idx + NICKLEN > BUFSIZE - 3)
	{
	  sendto_one(sptr, rpl_str(RPL_NAMREPLY),
		     me.name, parv[0], buf);
	  buf[0] = '*';
	  buf[1] = ' ';
	  buf[2] = '\0';
	  (void)strncpy(buf+2, chptr->chname, len + 1);
	  (void)strcat(buf, " :");
	  if (PubChannel(chptr))
	    *buf = '=';
	  else if (SecretChannel(chptr))
	    *buf = '@';
	  idx = len + 4;
	  flag = 0;
	}
    }

  if (flag)
    sendto_one(sptr, rpl_str(RPL_NAMREPLY),
	       me.name, parv[0], buf);

  sendto_one(sptr, rpl_str(RPL_ENDOFNAMES), me.name, parv[0],
	     para);
  return(1);
}


void	send_user_joins(aClient *cptr, aClient *user)
{
  Reg	Link	*lp;
  Reg	aChannel *chptr;
  Reg	int	cnt = 0, len = 0, clen;
  char	 *mask;

  *buf = ':';
  (void)strcpy(buf+1, user->name);
  (void)strcat(buf, " JOIN ");
  len = strlen(user->name) + 7;

  for (lp = user->user->channel; lp; lp = lp->next)
    {
      chptr = lp->value.chptr;
      if (*chptr->chname == '&')
	continue;
      if ((mask = strchr(chptr->chname, ':')))
	if (matches(++mask, cptr->name))
	  continue;
      clen = strlen(chptr->chname);
      if (clen > (size_t) BUFSIZE - 7 - len)
	{
	  if (cnt)
	    sendto_one(cptr, "%s", buf);
	  *buf = ':';
	  (void)strcpy(buf+1, user->name);
	  (void)strcat(buf, " JOIN ");
	  len = strlen(user->name) + 7;
	  cnt = 0;
	}
      (void)strcpy(buf + len, chptr->chname);
      cnt++;
      len += clen;
      if (lp->next)
	{
	  len++;
	  (void)strcat(buf, ",");
	}
    }
  if (*buf && cnt)
    sendto_one(cptr, "%s", buf);

  return;
}

static	void sjoin_sendit(aClient *cptr,
			  aClient *sptr,
			  aChannel *chptr,
			  char *from)
{
  sendto_channel_butserv(chptr, sptr, ":%s MODE %s %s %s", from,
			 chptr->chname, modebuf, parabuf);
}

/*
 * m_sjoin
 * parv[0] - sender
 * parv[1] - TS
 * parv[2] - channel
 * parv[3] - modes + n arguments (key and/or limit)
 * parv[4+n] - flags+nick list (all in one parameter)

 * 
 * process a SJOIN, taking the TS's into account to either ignore the
 * incoming modes or undo the existing ones or merge them, and JOIN
 * all the specified users while sending JOIN/MODEs to non-TS servers
 * and to clients
 */


int	m_sjoin(aClient *cptr,
		aClient *sptr,
		int parc,
		char *parv[])
{
  aChannel *chptr;
  aClient	*acptr;
  ts_val	newts, oldts, tstosend;
  static	Mode mode, *oldmode;
  Link	*l;
  int	args = 0, haveops = 0, keep_our_modes = 1, keep_new_modes = 1;
  int   doesop = 0, what = 0, pargs = 0, fl, people = 0, isnew;
  int ip;
  register	char *s, *s0;
  static	char numeric[16], sjbuf[BUFSIZE];
  char	*mbuf = modebuf, *t = sjbuf, *p;


  static	FLAG_ITEM	flags[] = {
    {MODE_PRIVATE,    'p'},
    {MODE_SECRET,     's'},
    {MODE_MODERATED,  'm'},
    {MODE_NOPRIVMSGS, 'n'},
    {MODE_TOPICLIMIT, 't'},
    {MODE_INVITEONLY, 'i'},
    {0x0, 0x0} };

  if (IsClient(sptr) || parc < 5)
    return 0;
  if (!IsChannelName(parv[2]))
    return 0;
  newts = atol(parv[1]);
  memset((void *)&mode, 0, sizeof(mode));

  s = parv[3];
  while (*s)
    switch(*(s++))
      {
      case 'i':
	mode.mode |= MODE_INVITEONLY;
	break;
      case 'n':
	mode.mode |= MODE_NOPRIVMSGS;
	break;
      case 'p':
	mode.mode |= MODE_PRIVATE;
	break;
      case 's':
	mode.mode |= MODE_SECRET;
	break;
      case 'm':
	mode.mode |= MODE_MODERATED;
	break;
      case 't':
	mode.mode |= MODE_TOPICLIMIT;
	break;
      case 'k':
	strncpyzt(mode.key, parv[4+args], KEYLEN+1);
	args++;
	if (parc < 5+args) return 0;
	break;
      case 'l':
	mode.limit = atoi(parv[4+args]);
	args++;
	if (parc < 5+args) return 0;
	break;
      }

  *parabuf = '\0';

  isnew = ChannelExists(parv[2]) ? 0 : 1;
  chptr = get_channel(sptr, parv[2], CREATE);
  /* locally created channels do not get created from SJOIN's
   * any SJOIN destroys the locally_created flag
   *
   * -Dianora
   */

  /*
   * bogus ban removal code.
   * If I see that this SJOIN will mean I keep my ops, but lose
   * the ops from the joining server, I keep track of that in the channel
   * structure. I set keep_their_modes to NO
   * since the joining server will not be keeping their ops, I can
   * ignore any of the bans sent from that server. The moment
   * I see a chanop MODE being sent, I can set this flag back to YES.
   *
   * There is one degenerate case. Two servers connect bursting
   * at the same time. It might cause a problem, or it might not.
   * In the case that it becomes an issue, then a short list
   * of servers having their modes ignored would have to be linked
   * into the channel structure. This would be only an issue
   * on hubs.
   * Hopefully, it will be much of a problem.
   *
   * Bogus bans on the server losing its chanops is trivial. All
   * bans placed on the local server during its split, with bogus chanops
   * I can just remove.
   *
   * -Dianora
   */
  
  chptr->keep_their_modes = YES;

  chptr->locally_created = NO;
  oldts = chptr->channelts;

  /*
   * If an SJOIN ever happens on a channel, assume the split is over
   * for this channel. best I think we can do for now -Dianora
   */

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
  if( (chptr->mode.mode & MODE_SPLIT) && (chptr->users == 0))
    chptr->mode.mode = 0;
#endif

  /* If the TS goes to 0 for whatever reason, flag it
   * ya, I know its an invasion of privacy for those channels that
   * want to keep TS 0 *shrug* sorry
   * -Dianora
   */

  if(!newts && oldts)
    {
      sendto_channel_butserv(chptr, &me,
	     ":%s NOTICE %s :*** Notice -- TS for %s changed from %ld to 0",
	      me.name, chptr->chname, chptr->chname, oldts);
      sendto_realops("Server %s changing TS on %s from %ld to 0",
		     sptr->name,parv[2],oldts);
    }

  doesop = (parv[4+args][0] == '@' || parv[4+args][1] == '@');

  for (l = chptr->members; l && l->value.cptr; l = l->next)
    if (l->flags & MODE_CHANOP)
      {
	haveops++;
	break;
      }

  oldmode = &chptr->mode;

  if (isnew)
    chptr->channelts = tstosend = newts;
  else if (newts == 0 || oldts == 0)
    chptr->channelts = tstosend = 0;
  else if (newts == oldts)
    tstosend = oldts;
  else if (newts < oldts)
    {
      if (doesop)
	keep_our_modes = NO;

      clear_bans_exceptions(sptr,chptr);

      if (haveops && !doesop)
	tstosend = oldts;
      else
	chptr->channelts = tstosend = newts;
    }
  else
    {
      chptr->keep_their_modes = NO;

      if (haveops)
	keep_new_modes = NO;

      if (doesop && !haveops)
	{
	  chptr->channelts = tstosend = newts;
	  if (MyConnect(sptr))
	    ts_warn("Hacked ops on opless channel: %s",
		    chptr->chname);
	}
      else
	tstosend = oldts;
    }

  if (!keep_new_modes)
    mode = *oldmode;
  else if (keep_our_modes)
    {
      mode.mode |= oldmode->mode;
      if (oldmode->limit > mode.limit)
	mode.limit = oldmode->limit;
      if (strcmp(mode.key, oldmode->key) < 0)
	strcpy(mode.key, oldmode->key);
    }

  for (ip = 0; flags[ip].mode; ip++)
    if ((flags[ip].mode & mode.mode) && !(flags[ip].mode & oldmode->mode))
      {
	if (what != 1)
	  {
	    *mbuf++ = '+';
	    what = 1;
	  }
	*mbuf++ = flags[ip].letter;
      }

  for (ip = 0; flags[ip].mode; ip++)
    if ((flags[ip].mode & oldmode->mode) && !(flags[ip].mode & mode.mode))
      {
	if (what != -1)
	  {
	    *mbuf++ = '-';
	    what = -1;
	  }
	*mbuf++ = flags[ip].letter;
      }
  if (oldmode->limit && !mode.limit)
    {
      if (what != -1)
	{
	  *mbuf++ = '-';
	  what = -1;
	}
      *mbuf++ = 'l';
    }
  if (oldmode->key[0] && !mode.key[0])
    {
      if (what != -1)
	{
	  *mbuf++ = '-';
	  what = -1;
	}
      *mbuf++ = 'k';
      strcat(parabuf, oldmode->key);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode.limit && oldmode->limit != mode.limit)
    {
      if (what != 1)
	{
	  *mbuf++ = '+';
	  what = 1;
	}
      *mbuf++ = 'l';
      (void)sprintf(numeric, "%-15d", mode.limit);
      if ((s = strchr(numeric, ' ')))
	*s = '\0';
      strcat(parabuf, numeric);
      strcat(parabuf, " ");
      pargs++;
    }
  if (mode.key[0] && strcmp(oldmode->key, mode.key))
    {
      if (what != 1)
	{
	  *mbuf++ = '+';
	  what = 1;
	}
      *mbuf++ = 'k';
      strcat(parabuf, mode.key);
      strcat(parabuf, " ");
      pargs++;
    }
  
  chptr->mode = mode;

  if (!keep_our_modes)
    {
      what = 0;
      for (l = chptr->members; l && l->value.cptr; l = l->next)
	{
	  if (l->flags & MODE_CHANOP)
	    {
	      if (what != -1)
		{
		  *mbuf++ = '-';
		  what = -1;
		}
	      *mbuf++ = 'o';
	      strcat(parabuf, l->value.cptr->name);
	      strcat(parabuf, " ");
	      pargs++;
	      if (pargs >= MAXMODEPARAMS)
		{
		  *mbuf = '\0';
		  sjoin_sendit(cptr, sptr, chptr,
			       parv[0]);
		  mbuf = modebuf;
		  *mbuf = parabuf[0] = '\0';
		  pargs = what = 0;
		}
	      l->flags &= ~MODE_CHANOP;
	    }
	  if (l->flags & MODE_VOICE)
	    {
	      if (what != -1)
		{
		  *mbuf++ = '-';
		  what = -1;
		}
	      *mbuf++ = 'v';
	      strcat(parabuf, l->value.cptr->name);
	      strcat(parabuf, " ");
	      pargs++;
	      if (pargs >= MAXMODEPARAMS)
		{
		  *mbuf = '\0';
		  sjoin_sendit(cptr, sptr, chptr,
			       parv[0]);
		  mbuf = modebuf;
		  *mbuf = parabuf[0] = '\0';
		  pargs = what = 0;
		}
	      l->flags &= ~MODE_VOICE;
	    }
	}
        sendto_channel_butserv(chptr, &me,
	    ":%s NOTICE %s :*** Notice -- TS for %s changed from %ld to %ld",
	    me.name, chptr->chname, chptr->chname, oldts, newts);
    }
  if (mbuf != modebuf)
    {
      *mbuf = '\0';
      sjoin_sendit(cptr, sptr, chptr, parv[0]);
    }

  *modebuf = *parabuf = '\0';
  if (parv[3][0] != '0' && keep_new_modes)
    channel_modes(sptr, modebuf, parabuf, chptr);
  else
    {
      modebuf[0] = '0';
      modebuf[1] = '\0';
    }

  sprintf(t, ":%s SJOIN %ld %s %s %s :", parv[0], tstosend, parv[2],
	  modebuf, parabuf);
  t += strlen(t);

  mbuf = modebuf;
  parabuf[0] = '\0';
  pargs = 0;
  *mbuf++ = '+';

  for (s = s0 = strtoken(&p, parv[args+4], " "); s;
       s = s0 = strtoken(&p, (char *)NULL, " "))
    {
      fl = 0;
      if (*s == '@' || s[1] == '@')
	fl |= MODE_CHANOP;
      if (*s == '+' || s[1] == '+')
	fl |= MODE_VOICE;
      if (!keep_new_modes)
       {
	if (fl & MODE_CHANOP)
	  {
	    fl = MODE_DEOPPED;
	  }
	else
	  {
	    fl = 0;
	  }
       }
      while (*s == '@' || *s == '+')
	s++;
      if (!(acptr = find_chasing(sptr, s, NULL)))
	continue;
      if (acptr->from != cptr)
	continue;
      people++;
      if (!IsMember(acptr, chptr))
	{
	  add_user_to_channel(chptr, acptr, fl);
	  sendto_channel_butserv(chptr, acptr, ":%s JOIN :%s",
				 s, parv[2]);
	}
      if (keep_new_modes)
	strcpy(t, s0);
      else
	strcpy(t, s);
      t += strlen(t);
      *t++ = ' ';
      if (fl & MODE_CHANOP)
	{
	  *mbuf++ = 'o';
	  strcat(parabuf, s);
	  strcat(parabuf, " ");
	  pargs++;
	  if (pargs >= MAXMODEPARAMS)
	    {
	      *mbuf = '\0';
	      sjoin_sendit(cptr, sptr, chptr, parv[0]);
	      mbuf = modebuf;
	      *mbuf++ = '+';
	      parabuf[0] = '\0';
	      pargs = 0;
	    }
	}
      if (fl & MODE_VOICE)
	{
	  *mbuf++ = 'v';
	  strcat(parabuf, s);
	  strcat(parabuf, " ");
	  pargs++;
	  if (pargs >= MAXMODEPARAMS)
	    {
	      *mbuf = '\0';
	      sjoin_sendit(cptr, sptr, chptr, parv[0]);
	      mbuf = modebuf;
	      *mbuf++ = '+';
	      parabuf[0] = '\0';
	      pargs = 0;
	    }
	}
    }
  
  *mbuf = '\0';
  if (pargs)
    sjoin_sendit(cptr, sptr, chptr, parv[0]);
  if (people)
    {
      if (t[-1] == ' ')
	t[-1] = '\0';
      else
	*t = '\0';
      sendto_match_servs(chptr, cptr, "%s", sjbuf);
    }
  return 0;
}



/*
 * Only called from ircd.c if the clock is discovered to be running forwards
 * The clock can be running forwards for a number of reasons
 * the most likely one, being a reset of the system time.
 * in these cases, its a legit reset of the clock time, but all the channels
 * locally created now have a bogus channel timestamp
 * so, I scan all the current channels, looking for channels I locally
 * created... and reset their creation time...
 * I can only do this =before= I have joined any servers... 
 * Channels that existed =before= this server split from the rest of the net
 * will not have the locally_created flag set, and hence will not be
 * affected. 
 * Remember, the server will not link with a very bogus TS so all that
 * is necessary is to make sure any locally created channel TS's are fixed.
 *
 * -Dianora
 */

void sync_channels(time_t delta)
{
  register	aChannel	*chptr;
  time_t newts;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      if(chptr->locally_created)
	{
	  newts = chptr->channelts + delta;
	  sendto_realops("*** resetting TS on locally created %s from %d to %d",
			 chptr->chname,chptr->channelts,
			 newts);

	  chptr->channelts = newts;
	}
    }
 
}
