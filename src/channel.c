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
int server_was_split=NO;
time_t server_split_time;
int server_split_recovery_time = (DEFAULT_SERVER_SPLIT_RECOVERY_TIME * 60);
#define USE_ALLOW_OP
#endif

#ifdef LITTLE_I_LINES
#ifndef USE_ALLOW_OP
#define USE_ALLOW_OP
#endif
#endif

aChannel *channel = NullChn;

static	void	add_invite (aClient *, aChannel *);
static	int	add_banid (aClient *, aChannel *, char *);
static	int	can_join (aClient *, aChannel *, char *);
static	void	channel_modes (aClient *, char *, char *, aChannel *);
static	int	del_banid (aChannel *, char *);
static	Link	*is_banned (aClient *, aChannel *);
static	int	set_mode (aClient *, aClient *, aChannel *, int,
			        char **, char *,char *);
static	void	sub1_from_channel (aChannel *);

void	clean_channelname(unsigned char *);
void	del_invite (aClient *, aChannel *);

#ifdef ORATIMING
struct timeval tsdnow, tsdthen; 
unsigned long tsdms;
#endif

/*
** number of seconds to add to all readings of time() when making TS's
** -orabidoo
*/

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
static	char	modebuf[MODEBUFLEN], parabuf[MODEBUFLEN];

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
#ifdef BAN_INFO
      len += strlen(ban->value.banptr->banstr);
#else
      len += strlen(ban->value.cp);
#endif

#ifdef BAN_INFO
      if (MyClient(cptr))
	{
	  if((len > MAXBANLENGTH) || (++cnt >= MAXBANS))
	    {
	      sendto_one(cptr, err_str(ERR_BANLISTFULL),
			 me.name, cptr->name,
			 chptr->chname, banid);
	      return -1;
	    }
	  if(!match(ban->value.banptr->banstr, banid) ||
	     !match(banid,ban->value.banptr->banstr))
	    return -1;
	}
      else if (!mycmp(ban->value.banptr->banstr, banid))
	return -1;
#else
      if (MyClient(cptr))
	{
	  if((len > MAXBANLENGTH) || (++cnt >= MAXBANS))
	    {
	      sendto_one(cptr, err_str(ERR_BANLISTFULL),
			 me.name, cptr->name,
			 chptr->chname, banid);
	      return -1;
	    }
	  if(!match(ban->value.cp, banid) ||
	     !match(banid, ban->value.cp))
	    return -1;
	}
      else if (!mycmp(ban->value.cp, banid))
	return -1;
#endif
    }

  ban = make_link();
  bzero((char *)ban, sizeof(Link));
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

/*
 * del_banid - delete an id belonging to cptr
 * if banid is null, deleteall banids belonging to cptr.
 */
static	int	del_banid(aChannel *chptr, char *banid)
{
	Reg Link **ban;
	Reg Link *tmp;

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
 * is_banned - returns a pointer to the ban structure if banned else NULL
 *
 * IP_BAN_ALL from comstud
 * always on...
 */

static	Link	*is_banned(aClient *cptr,aChannel *chptr)
{
  Reg	Link	*tmp;
  char	s[NICKLEN+USERLEN+HOSTLEN+6];
  char  *s2;

  if (!IsPerson(cptr))
    return ((Link *)NULL);

  strcpy(s,make_nick_user_host(cptr->name, cptr->user->username,
			       cptr->user->host));
  s2 = make_nick_user_host(cptr->name, cptr->user->username,
			   cptr->hostip);

#ifdef BAN_INFO
  for (tmp = chptr->banlist; tmp; tmp = tmp->next)
    if ((match(tmp->value.banptr->banstr, s) == 0) ||
	(match(tmp->value.banptr->banstr, s2) == 0) )
      break;
#else
  for (tmp = chptr->banlist; tmp; tmp = tmp->next)
    if ((match(tmp->value.cp, s) == 0) ||
	(match(tmp->value.cp, s2) == 0) )
      break;
#endif
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

static	void	change_chan_flag(Link *lp, aChannel *chptr)
{
  Reg	Link *tmp;

  if ((tmp = find_user_link(chptr->members, lp->value.cptr)))
    if (lp->flags & MODE_ADD)
      {
	tmp->flags |= lp->flags & MODE_FLAGS;
	if (lp->flags & MODE_CHANOP)
	  tmp->flags &= ~MODE_DEOPPED;
      }
    else
      tmp->flags &= ~lp->flags & MODE_FLAGS;
}

static	void	set_deopped(Link *lp, aChannel *chptr)
{
  Reg	Link	*tmp;

  if ((tmp = find_user_link(chptr->members, lp->value.cptr)))
    if ((tmp->flags & MODE_CHANOP) == 0)
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
      if (mask == CHFL_BAN)
#ifdef BAN_INFO
	name = lp->value.banptr->banstr;
#else
        name = lp->value.cp;
#endif
      else
	name = lp->value.cptr->name;
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
  send_mode_list(cptr, chptr->chname, chptr->banlist, CHFL_BAN,
		 'b');
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
  int	mcount = 0, chanop;
  aChannel *chptr;

  /* Now, try to find the channel in question */
  if (parc > 1)
    {
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

  clean_channelname((unsigned char *)parv[1]);
  chanop = is_chan_op(sptr, chptr) || IsServer(sptr);

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
  mcount = set_mode(cptr, sptr, chptr, parc - 2, parv + 2,
		    modebuf, parabuf);

  if (strlen(modebuf) > (size_t)1)
    switch (mcount)
      {
      case 0:
	break;
      case -1:
	if (MyClient(sptr))
	  sendto_one(sptr,
		     err_str(ERR_CHANOPRIVSNEEDED),
		     me.name, parv[0], chptr->chname);
	else
	  {
#ifndef DONT_SEND_FAKES
	    sendto_ops_lev(REJ_LEV,"Fake: %s MODE %s %s %s",
		       parv[0], parv[1], modebuf, parabuf);
#endif
	    ircstp->is_fake++;
	  }
	break;
      default:
	sendto_channel_butserv(chptr, sptr,
			       ":%s MODE %s %s %s", parv[0],
			       chptr->chname, modebuf,
			       parabuf);
	sendto_match_servs(chptr, cptr,
			   ":%s MODE %s %s %s",
			   parv[0], chptr->chname,
			   modebuf, parabuf);
      }
  return 0;
}

/*
 * Check and try to apply the channel modes passed in the parv array for
 * the client ccptr to channel chptr.  The resultant changes are printed
 * into mbuf and pbuf (if any) and applied to the channel.
 */
static	int	set_mode(aClient *cptr,
			 aClient *sptr,
			 aChannel *chptr,
			 int parc,
			 char *parv[],
			 char *mbuf,
			 char *pbuf)
{
  static	Link	chops[MAXMODEPARAMS];
  static	int	flags[] = {
    MODE_PRIVATE,    'p', MODE_SECRET,     's',
    MODE_MODERATED,  'm', MODE_NOPRIVMSGS, 'n',
    MODE_TOPICLIMIT, 't', MODE_INVITEONLY, 'i',
    MODE_VOICE,	 'v', MODE_KEY,	       'k',
    0x0, 0x0 };

  Reg	Link	*lp;
  Reg	char	*curr = parv[0], *cp = (char *)NULL;
  Reg	int	*ip;
  u_int	whatt = MODE_ADD;
  int	limitset = 0, count = 0, chasing = 0;
  int	nusers = 0, ischop, isok, isdeop, new, len;
  int	keychange = 0, opcnt = 0;
  char	fm = '\0';
  aClient *who;
  Mode	*mode, oldm;

  *mbuf = *pbuf = '\0';
  if (parc < 1)
    return 0;

  mode = &(chptr->mode);
  bcopy((char *)mode, (char *)&oldm, sizeof(Mode));
  ischop = IsServer(sptr) || is_chan_op(sptr, chptr);
  isdeop = !ischop && !IsServer(sptr) && is_deopped(sptr, chptr);
  isok = ischop || (!isdeop && IsServer(cptr) && 
		    chptr->channelts);
  new = mode->mode;

  while (curr && *curr && count >= 0)
    {
      switch (*curr)
	{
	case '+':
	  whatt = MODE_ADD;
	  break;
	case '-':
	  whatt = MODE_DEL;
	  break;
	case 'o' :
	case 'v' :
	  if (--parc <= 0)
	    break;
	  parv++;
	  *parv = check_string(*parv);
	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;
	  /*
	   * Check for nickname changes and try to follow these
	   * to make sure the right client is affected by the
	   * mode change.
	   */
	  if (!(who = find_chasing(sptr, parv[0], &chasing)))
	    break;
	  if (!IsMember(who, chptr))
	    {
	      sendto_one(cptr, err_str(ERR_USERNOTINCHANNEL),
			 me.name, cptr->name,
			 parv[0], chptr->chname);
	      break;
	    }
	  /*
	  ** If this server noticed the nick change, the
	  ** information must be propagated back upstream.
	  ** This is a bit early, but at most this will generate
	  ** just some extra messages if nick appeared more than
	  ** once in the MODE message... --msa
	  */
	  /*
	  ** This is actually useless - if we notice a nick
	  ** change it means the server the mode comes from       
	  ** dealt with the mode with the old nick, and already
	  ** applied it, and the nick change must be coming
	  ** from another server; besides, we don't want to
	  ** be resetting the channel's TS -orabidoo
	  */

	  if (who == cptr && whatt == MODE_ADD && *curr == 'o')
	    break;
	  /*
	   * to stop problems, don't allow +v and +o to mix
	   * into the one message if from a client.
	   */
	  if (!fm)
	    fm = *curr;
	  else if (MyClient(sptr) && (*curr != fm))
	    break;
	  if (whatt == MODE_ADD)
	    {
#ifdef LITTLE_I_LINES
	      if(MyClient(who) && !IsAnOper(who) 
		 && isok && (*curr=='o') && IsRestricted(who))
		{
		  sendto_one(who, ":%s NOTICE %s :*** Notice -- %s attempted to chanop you. You are restricted and cannot be chanopped",
			     me.name,
			     who->name,
			     sptr->name);
		  sendto_one(sptr, ":%s NOTICE %s :*** Notice -- %s is restricted and cannot be chanopped",
			     me.name,
			     sptr->name,
			     who->name);
		}
	      else
#endif
		{
		  lp = &chops[opcnt++];
		  lp->value.cptr = who;
		  lp->flags = (*curr == 'o') ? MODE_CHANOP:
		    MODE_VOICE;
		  lp->flags |= MODE_ADD;
		}
	    }
	  else if (whatt == MODE_DEL)
	    {
	      lp = &chops[opcnt++];
	      lp->value.cptr = who;
	      lp->flags = (*curr == 'o') ? MODE_CHANOP:
		MODE_VOICE;
	      lp->flags |= MODE_DEL;
	    }
	  count++;
	  break;
	case 'k':
	  if (--parc <= 0)
	    break;
	  parv++;
	  /* check now so we eat the parameter if present */
	  if (keychange)
	    break;
	  *parv = check_string(*parv);
	  {
	    u_char	*s, *t;
	    int abuse = NO;
	    
	    for (s = t = (u_char *)*parv; *s; s++)
	      {
		if (*s > 0x7f && *s <= 0xa0)
		  abuse = YES;
		*s &= 0x7f;
		if (*s > (u_char)' ' && *s != ':')
		  *t++ = *s;
	      }
	    *t = '\0';
	    if (abuse)
	      {
                if(MyClient(sptr))
		  {
                    return exit_client(sptr, sptr, &me,
                                  "Trying to abuse +k bug");
 		  }
		else
	          sendto_ops("User %s trying to abuse +k bug",
			 sptr->name);
                return 0;
              }
	    if (t == (u_char *)*parv) break;
	  }
	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;
	  if (!fm)
	    fm = *curr;
	  else if (MyClient(sptr) && (*curr != fm))
	    break;
	  if (whatt == MODE_ADD)
	    {
	      if (*mode->key && !IsServer(cptr))
		sendto_one(cptr, err_str(ERR_KEYSET),
			   me.name, cptr->name,
			   chptr->chname);
	      else if (isok &&
		       (!*mode->key || IsServer(cptr)))
		{
		  lp = &chops[opcnt++];
		  lp->value.cp = *parv;
		  if (strlen(lp->value.cp) >
		      (size_t) KEYLEN)
		    lp->value.cp[KEYLEN] = '\0';
		  lp->flags = MODE_KEY|MODE_ADD;
		  keychange = 1;
		}
	    }
	  else if (whatt == MODE_DEL)
	    {
	      if (isok && (mycmp(mode->key, *parv) == 0 ||
			     IsServer(cptr)))
		{
		  lp = &chops[opcnt++];
		  lp->value.cp = mode->key;
		  lp->flags = MODE_KEY|MODE_DEL;
		  keychange = 1;
	        }
	    }
	  count++;
	  break;
	case 'b':
	  if (--parc <= 0)
	    {
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
	      sendto_one(cptr, rpl_str(RPL_ENDOFBANLIST),
			 me.name, cptr->name, chptr->chname);
	      break;
	    }
       
	  parv++;
	  if (BadPtr(*parv))
	    break;
	  if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
	    break;
	  if (whatt == MODE_ADD)
	    {
	      lp = &chops[opcnt++];
	      lp->value.cp = *parv;
	      lp->flags = MODE_ADD|MODE_BAN;
	    }
	  else if (whatt == MODE_DEL)
	    {
	      lp = &chops[opcnt++];
	      lp->value.cp = *parv;
	      lp->flags = MODE_DEL|MODE_BAN;
	    }
	  count++;
	  break;
	case 'l':
	  /*
	   * limit 'l' to only *1* change per mode command but
	   * eat up others.
	   */
	  if (limitset || !isok)
	    {
	      if (whatt == MODE_ADD && --parc > 0)
		parv++;
	      break;
	    }
	  if (whatt == MODE_DEL)
	    {
	      limitset = 1;
	      nusers = 0;
	      count++;
	      break;
	    }
	  if (--parc > 0)
	    {
	      if (BadPtr(*parv))
		break;
	      if (MyClient(sptr) && opcnt >= MAXMODEPARAMS)
		break;
	      if ( (nusers = atoi(*++parv)) <= 0)
	        break;
	      lp = &chops[opcnt++];
	      lp->flags = MODE_ADD|MODE_LIMIT;
	      limitset = 1;
	      count++;
	      break;
	    }
	  sendto_one(cptr, err_str(ERR_NEEDMOREPARAMS),
		     me.name, cptr->name, "MODE +l");
	  break;
	case 'i' : /* falls through for default case */
	  if ((whatt == MODE_DEL) && isok)
	    while ( (lp = chptr->invites) )
	      del_invite(lp->value.cptr, chptr);
	default:
	  for (ip = flags; *ip; ip += 2)
	    if (*(ip+1) == *curr)
	      break;
      
	  if (*ip)
	    {
	      if (whatt == MODE_ADD)
		{
		  if (*ip == MODE_PRIVATE)
		    new &= ~MODE_SECRET;
		  else if (*ip == MODE_SECRET)
		    new &= ~MODE_PRIVATE;
		  new |= *ip;
		}
	      else
		new &= ~*ip;
	      count++;
	    }
	  else
	    sendto_one(cptr, err_str(ERR_UNKNOWNMODE),
		       me.name, cptr->name, *curr);
	  break;
	}
      curr++;
      /*
       * Make sure modes strings such as "+m +t +p +i" are parsed
       * fully.
       */
      if (!*curr && parc > 0)
	{
	  curr = *++parv;
	  parc--;
	}
    }/* end of while loop for MODE processing */

  whatt = 0;

  for (ip = flags; *ip; ip += 2)
    if ((*ip & new) && !(*ip & oldm.mode))
      {
	if (whatt == 0)
	  {
	    *mbuf++ = '+';
	    whatt = 1;
	  }
	if (isok)
	  mode->mode |= *ip;
	*mbuf++ = *(ip+1);
      }

  for (ip = flags; *ip; ip += 2)
    if ((*ip & oldm.mode) && !(*ip & new))
      {
	if (whatt != -1)
	  {
	    *mbuf++ = '-';
	    whatt = -1;
	  }
	if (ischop)
	  mode->mode &= ~*ip;
	*mbuf++ = *(ip+1);
      }
  
  if (limitset && !nusers && mode->limit)
    {
      if (whatt != -1)
	{
	  *mbuf++ = '-';
	  whatt = -1;
	}
      mode->mode &= ~MODE_LIMIT;
      mode->limit = 0;
      *mbuf++ = 'l';
    }

  /*
   * Reconstruct "+bkov" chain.
   */
  if (opcnt)
    {
      Reg	int	i = 0;
      Reg	char	c = '\0';
      char	*user, *host, numeric[16];

      /* restriction for 2.7 servers */
      if (!IsServer(cptr) && opcnt > (MAXMODEPARAMS-2))
	opcnt = (MAXMODEPARAMS-2);

      for (; i < opcnt; i++)
	{
	  lp = &chops[i];
	  /*
	   * make sure we have correct mode change sign
	   */
	  if (whatt != (lp->flags & (MODE_ADD|MODE_DEL)))
	    if (lp->flags & MODE_ADD)
	      {
		*mbuf++ = '+';
		whatt = MODE_ADD;
	      }
	    else
	      {
		*mbuf++ = '-';
		whatt = MODE_DEL;
	      }
	  len = strlen(pbuf);
	  /*
	   * get c as the mode char and tmp as a pointer to
	   * the paramter for this mode change.
	   */
	  switch(lp->flags & MODE_WPARAS)
	    {
	    case MODE_CHANOP :
	      c = 'o';
	      cp = lp->value.cptr->name;
	      break;
	    case MODE_VOICE :
	      c = 'v';
	      cp = lp->value.cptr->name;
	      break;
	    case MODE_BAN :
	      c = 'b';
	      cp = lp->value.cp;
	      if ((user = index(cp, '!')))
		*user++ = '\0';
	      if ((host = rindex(user ? user : cp, '@')))
		*host++ = '\0';
	      cp = make_nick_user_host(cp, user, host);
	      break;
	    case MODE_KEY :
	      c = 'k';
	      cp = lp->value.cp;
	      break;
	    case MODE_LIMIT :
	      c = 'l';
	      (void)ircsprintf(numeric, "%-15d", nusers);
	      if ((cp = index(numeric, ' ')))
		*cp = '\0';
	      cp = numeric;
	      break;
	    }

	  if (len + strlen(cp) + 2 > (size_t) MODEBUFLEN)
				break;
	  /*
	   * pass on +/-o/v regardless of whether they are
	   * redundant or effective but check +b's to see if
	   * it existed before we created it.
	   */
	  switch(lp->flags & MODE_WPARAS)
	    {
	    case MODE_KEY :
	      *mbuf++ = c;
	      (void)strcat(pbuf, cp);
	      len += strlen(cp);
	      (void)strcat(pbuf, " ");
	      len++;
	      if (!isok)
		break;
	      if (strlen(cp) > (size_t) KEYLEN)
		*(cp+KEYLEN) = '\0';
	      if (whatt == MODE_ADD)
		{	
		  strncpyzt(mode->key, cp,
			    sizeof(mode->key));
		}
	      else
		*mode->key = '\0';
	      break;
	    case MODE_LIMIT :
	      *mbuf++ = c;
	      (void)strcat(pbuf, cp);
	      len += strlen(cp);
	      (void)strcat(pbuf, " ");
	      len++;
	      if (!isok)
		break;
	      mode->limit = nusers;
	      break;
	    case MODE_CHANOP :
	    case MODE_VOICE :
	      *mbuf++ = c;
	      (void)strcat(pbuf, cp);
	      len += strlen(cp);
	      (void)strcat(pbuf, " ");
	      len++;
	      if (isok)
		change_chan_flag(lp, chptr);
	      if (IsServer(sptr) && c == 'o' && 
		  whatt == MODE_ADD)
		{
		  chptr->channelts = 0;
		  ts_warn("Server %s setting +o and blasting TS on %s", sptr->name,
			  chptr->chname);
		}
	      if (c == 'o' && whatt == MODE_ADD && isdeop &&
		  !is_chan_op(lp->value.cptr, chptr))
		set_deopped(lp, chptr);
	      break;
	    case MODE_BAN :
	      if (isok && (((whatt & MODE_ADD) &&
			     !add_banid(sptr, chptr, cp)) ||
			     ((whatt & MODE_DEL) &&
			     !del_banid(chptr, cp))))
		{
		  *mbuf++ = c;
		  (void)strcat(pbuf, cp);
		  len += strlen(cp);
		  (void)strcat(pbuf, " ");
		  len++;
		}
	      break;
	    }
	} /* for (; i < opcnt; i++) */
    } /* if (opcnt) */
  
  *mbuf++ = '\0';

/* returns:
  ** -1  = mode changes were generated but ignored, and a FAKE
  **       must be sent for remote clients and a CHANOPRIVSNEEDED
  **       for local ones
  **  0  = ignore whatever was generated, do not propagate anything
  ** >0  = modes were accepted, propagate
  */

  if (isok)
    return count;
  if (isdeop && !MyClient(sptr))
    return 0;
  else
    return -1;
}

static	int	can_join(aClient *sptr, aChannel *chptr, char *key)
{
  Reg	Link	*lp;

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
  if(Count.myserver == 0)
    {
#ifdef NO_JOIN_ON_SPLIT 
      if(chptr->mode.mode & MODE_SPLIT)
	return (ERR_NOJOINSPLIT);
#endif
    }
  else
    {
      if((chptr->mode.mode & MODE_SPLIT) &&
	 server_was_split && server_split_recovery_time)
	{
	  if((server_split_time + server_split_recovery_time) < NOW)
	    {
	      server_was_split = NO;
	      chptr->mode.mode &= ~MODE_SPLIT;
	      if(chptr->users == 0)
		chptr->mode.mode = 0;
	    }
	}
    }
#endif

  if (is_banned(sptr, chptr))
    return (ERR_BANNEDFROMCHAN);
  if (chptr->mode.mode & MODE_INVITEONLY)
    {
      for (lp = sptr->user->invited; lp; lp = lp->next)
	if (lp->value.chptr == chptr)
	  break;
      if (!lp)
	return (ERR_INVITEONLYCHAN);
    }
  
  if (*chptr->mode.key && (BadPtr(key) || mycmp(chptr->mode.key, key)))
    return (ERR_BADCHANNELKEY);
  
  if (chptr->mode.limit && chptr->users >= chptr->mode.limit)
    return (ERR_CHANNELISFULL);

  return 0;
}

/*
** Remove bells and commas from channel name
*/

void	clean_channelname(unsigned char *cn)
{
  for (; *cn; cn++)
    /*
     * Find bad characters and remove them, also check for
     * characters in the '\0' -> ' ' range, but +127   -Taner
     */
    if (*cn == '\007' || *cn == ' ' || *cn == ',' || (*cn > 127 && *cn <= 160))
      {
	*cn = '\0';
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
      bzero((char *)chptr, sizeof(aChannel));
      strncpyzt(chptr->chname, chname, len+1);
      if (channel)
	channel->prevch = chptr;
      chptr->prevch = NULL;
      chptr->nextch = channel;
      channel = chptr;
      if(Count.myserver == 0)
	chptr->locally_created = YES;
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
**  Subtract one user from channel i (and free channel
**  block, if channel became empty).
*/
static	void	sub1_from_channel(aChannel *chptr)
{
  Reg	Link *tmp;
  Link	*obtmp;

  if (--chptr->users <= 0)
    {
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
      if(!(chptr->locally_created) && (Count.myserver == 0))
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

  *jbuf = '\0';
  /*
  ** Rebuild list of channels joined to be the actual result of the
  ** JOIN.  Note that "JOIN 0" is the destructive problem.
  */
  for (i = 0, name = strtoken(&p, parv[1], ","); name;
       name = strtoken(&p, (char *)NULL, ","))
    {
      /* pathological case only on longest channel name.
      ** If not dealt with here, causes desynced channel ops
      ** since ChannelExists() doesn't see the same channel
      ** as one being joined. cute bug. Oct 11 1997, Dianora/comstud
      */

      if(strlen(name) >  CHANNELLEN)  /* same thing is done in get_channel() */
	name[CHANNELLEN] = '\0';
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
	      if(sptr->join_leave_count >= spam_num)
		{
		  sendto_realops("User %s (%s@%s) is a possible spambot",
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
#ifdef NO_CHANOPS_WHEN_SPLIT
	  /* if its not a local channel, or isn't an oper
	     and server has been split */

	  if((*name != '&') && !IsAnOper(sptr)
	     && server_was_split && server_split_recovery_time)
	    {
	      if( (server_split_time + server_split_recovery_time) < NOW)
		{
		  if(Count.myserver > 0)
		    server_was_split = NO;
		  else
		    {
		      server_split_time = NOW;	/* still split */
		      allow_op = NO;
		    }
		}
	      else
		{
		  allow_op = NO;
		}
		  if(!IsRestricted(sptr) && (flags == CHFL_CHANOP) && !allow_op)
		      sendto_one(sptr,":%s NOTICE %s :*** Notice -- Due to a network split, you can not obtain channel operator status in a new channel at this time.",
				 me.name,
				 sptr->name);
	    }
#endif

#ifdef LITTLE_I_LINES
	  if(!IsAnOper(sptr) && IsRestricted(sptr))
	    {
	      allow_op = NO;
		  sendto_one(sptr, ":%s NOTICE %s :*** Notice -- You are restricted and cannot be chanopped",
			     me.name,
			     sptr->name);
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
          if( sptr->join_leave_count >= spam_num)
            { 
              /* Its already known as a possible spambot */
 
              if(sptr->oper_warn_count_down > 0)  /* my general paranoia */
                sptr->oper_warn_count_down--;
              else
                sptr->oper_warn_count_down = 0;
 
              if(sptr->oper_warn_count_down == 0)
                {
                  sendto_realops("User %s (%s@%s) trying to join %s is a possible spambot",
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

      if (!chptr ||
	  (MyConnect(sptr) && (i = can_join(sptr, chptr, key))))
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
      if (IsMember(sptr, chptr))
	continue;

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
	  if(sptr->join_leave_count >= spam_num)
	    {
	      sendto_realops("User %s (%s@%s) is a possible spambot",
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
  name = strtoken(&p, parv[1], ",");

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

  user = strtoken(&p2, parv[2], ",");

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
  char	*topic = NULL, *name, *p = NULL;
  
  if (parc < 2)
    {
      sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		 me.name, parv[0], "TOPIC");
      return 0;
    }

  name = strtoken(&p, parv[1], ",");

  /* multi channel topic's are known now to be used by cloners
   * trying to flood off servers.. so disable it *sigh* - Dianora
   */

  /* disabled multi channel topic */
  /*  while(name) */

    {
      if (parc > 1 && IsChannelName(name))
	{
	  chptr = find_channel(name, NullChn);
	  if (!chptr || !IsMember(sptr, chptr))
	    {
	      sendto_one(sptr, err_str(ERR_NOTONCHANNEL),
			 me.name, parv[0], name);
	      name = strtoken(&p, (char *)NULL, ",");
	      /* disabled multi channel topic */
	      /*	      continue; */
	      return 0;
	    }
	  if (parc > 2)
	    topic = parv[2];
	}

      if (!chptr)
	{
	  sendto_one(sptr, rpl_str(RPL_NOTOPIC),
		     me.name, parv[0], name);
	  return 0;
	}

      if (!topic)  /* only asking  for topic  */
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
      else if (((chptr->mode.mode & MODE_TOPICLIMIT) == 0 ||
		is_chan_op(sptr, chptr)) && topic)
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

      /* disabled multi channel topics */
      /*      name = strtoken(&p, (char *)NULL, ","); */
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
    }
  if (MyConnect(acptr))
    if (chptr && (chptr->mode.mode & MODE_INVITEONLY) &&
	sptr->user && is_chan_op(sptr, chptr))
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

  sendto_one(sptr, rpl_str(RPL_LISTSTART), me.name, parv[0]);

  if (parc < 2 || BadPtr(parv[1]))
    {
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
      return 0;
    }

  if (hunt_server(cptr, sptr, ":%s LIST %s %s", 2, parc, parv))
    return 0;

  name = strtoken(&p, parv[1], ",");

  while(name)
    {
      chptr = find_channel(name, NullChn);
      if (chptr && ShowChannel(sptr, chptr) && sptr->user)
	sendto_one(sptr, rpl_str(RPL_LIST), me.name, parv[0],
		   ShowChannel(sptr,chptr) ? name : "*",
		   chptr->users, chptr->topic);
      name = strtoken(&p, (char *)NULL, ",");
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

  if (parc > 1 &&
      hunt_server(cptr, sptr, ":%s NAMES %s %s", 2, parc, parv))
    return 0;

  mlen = strlen(me.name) + NICKLEN + 7;

  if (!BadPtr(para))
    {
      s = index(para, ',');
      if (s)
	{
	  parv[1] = ++s;
	  (void)m_names(cptr, sptr, parc, parv);
	}
      clean_channelname((unsigned char *)para);
      ch2ptr = find_channel(para, (aChannel *)NULL);
    }

  *buf = '\0';
  
  /* Allow NAMES without registering
   *
   * First, do all visible channels (public and the one user self is)
   */

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      if ((chptr != ch2ptr) && !BadPtr(para))
	continue; /* -- wanted a specific channel */
      if (!MyConnect(sptr) && BadPtr(para))
	continue;
      if (!ShowChannel(sptr, chptr))
	continue; /* -- users on this are not listed */
      
      /* Find users on same channel (defined by chptr) */

      (void)strcpy(buf, "* ");
      len = strlen(chptr->chname);
      (void)strcpy(buf + 2, chptr->chname);
      (void)strcpy(buf + 2 + len, " :");

      if (PubChannel(chptr))
	*buf = '=';
      else if (SecretChannel(chptr))
	*buf = '@';
      idx = len + 4;
      flag = 1;
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
	      (void)strncpy(buf, "* ", 3);
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
    }
  if (!BadPtr(para))
    {
      sendto_one(sptr, rpl_str(RPL_ENDOFNAMES), me.name, parv[0],
		 para);
      return(1);
    }

  /* Second, do all non-public, non-secret channels in one big sweep */

  (void)strncpy(buf, "* * :", 6);
  idx = 5;
  flag = 0;
  for (c2ptr = client; c2ptr; c2ptr = c2ptr->next)
    {
      aChannel *ch3ptr;
      int	showflag = 0, secret = 0;

      if (!IsPerson(c2ptr) || IsInvisible(c2ptr))
	continue;
      lp = c2ptr->user->channel;
      /*
       * dont show a client if they are on a secret channel or
       * they are on a channel sptr is on since they have already
       * been show earlier. -avalon
       */
      while (lp)
	{
	  ch3ptr = lp->value.chptr;
	  if (PubChannel(ch3ptr) || IsMember(sptr, ch3ptr))
	    showflag = 1;
	  if (SecretChannel(ch3ptr))
	    secret = 1;
	  lp = lp->next;
	}
      if (showflag) /* have we already shown them ? */
	continue;
      if (secret) /* on any secret channels ? */
	continue;
      (void)strncat(buf, c2ptr->name, NICKLEN);
      idx += strlen(c2ptr->name) + 1;
      (void)strcat(buf," ");
      flag = 1;
      if (mlen + idx + NICKLEN > BUFSIZE - 3)
	{
	  sendto_one(sptr, rpl_str(RPL_NAMREPLY),
		     me.name, parv[0], buf);
	  (void)strncpy(buf, "* * :", 6);
	  idx = 5;
	  flag = 0;
	}
    }
  if (flag)
    sendto_one(sptr, rpl_str(RPL_NAMREPLY), me.name, parv[0], buf);
  sendto_one(sptr, rpl_str(RPL_ENDOFNAMES), me.name, parv[0], "*");
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
      if ((mask = index(chptr->chname, ':')))
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
  int	args = 0, haveops = 0, keepourmodes = 1, keepnewmodes = 1,
    doesop = 0, what = 0, pargs = 0, *ip, fl, people = 0, isnew;
  Reg	char *s, *s0;
  static	char numeric[16], sjbuf[BUFSIZE];
  char	*mbuf = modebuf, *t = sjbuf, *p;
  
  static	int	flags[] = {
    MODE_PRIVATE,    'p', MODE_SECRET,     's',
    MODE_MODERATED,  'm', MODE_NOPRIVMSGS, 'n',
    MODE_TOPICLIMIT, 't', MODE_INVITEONLY, 'i',
    0x0, 0x0 };

  if (IsClient(sptr) || parc < 5)
    return 0;
  if (!IsChannelName(parv[2]))
    return 0;
  newts = atol(parv[1]);
  bzero((char *)&mode, sizeof(mode));

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


  chptr->locally_created = NO;
  oldts = chptr->channelts;

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
	keepourmodes = 0;
      if (haveops && !doesop)
	tstosend = oldts;
      else
	chptr->channelts = tstosend = newts;
    }
  else
    {
      if (haveops)
	keepnewmodes = 0;
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

  if (!keepnewmodes)
    mode = *oldmode;
  else if (keepourmodes)
    {
      mode.mode |= oldmode->mode;
      if (oldmode->limit > mode.limit)
	mode.limit = oldmode->limit;
      if (strcmp(mode.key, oldmode->key) < 0)
	strcpy(mode.key, oldmode->key);
    }

  for (ip = flags; *ip; ip += 2)
    if ((*ip & mode.mode) && !(*ip & oldmode->mode))
      {
	if (what != 1)
	  {
	    *mbuf++ = '+';
	    what = 1;
	  }
	*mbuf++ = *(ip+1);
      }
  for (ip = flags; *ip; ip += 2)
    if ((*ip & oldmode->mode) && !(*ip & mode.mode))
      {
	if (what != -1)
	  {
	    *mbuf++ = '-';
	    what = -1;
	  }
	*mbuf++ = *(ip+1);
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
      if ((s = index(numeric, ' ')))
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

  if (!keepourmodes)
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
	      if (pargs >= (MAXMODEPARAMS-2))
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
	      if (pargs >= (MAXMODEPARAMS-2))
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
  if (parv[3][0] != '0' && keepnewmodes)
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
      if (!keepnewmodes)
	if (fl & MODE_CHANOP)
	  fl = MODE_DEOPPED;
	else
	  fl = 0;
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
      if (keepnewmodes)
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
	  if (pargs >= (MAXMODEPARAMS-2))
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
	  if (pargs >= (MAXMODEPARAMS-2))
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

void sync_channels()
{
  Reg	aChannel	*chptr;

  for (chptr = channel; chptr; chptr = chptr->nextch)
    {
      if(chptr->locally_created)
	{
	  chptr->channelts = timeofday;
	}
    }
 
}
