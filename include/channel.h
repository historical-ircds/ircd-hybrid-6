/************************************************************************
 *   IRC - Internet Relay Chat, ircd/channel.h
 *   Copyright (C) 1990 Jarkko Oikarinen
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

#ifndef __channel_include__
#define __channel_include__
#ifndef INCLUDED_struct_h
#include "struct.h"
#endif

typedef struct  Channel aChannel;

/* mode structure for channels */

struct  SMode
{
  unsigned int  mode;
  int   limit;
  char  key[KEYLEN + 1];
};

/* channel structure */

struct Channel
{
  struct Channel* nextch;
  struct Channel* prevch;
  struct Channel* hnextch;
  Mode            mode;
  char            topic[TOPICLEN + 1];
#ifdef TOPIC_INFO
  char            topic_nick[NICKLEN + 1];
  time_t          topic_time;
#endif
  int             users;
  Link*           members;
  Link*           invites;
  Link*           banlist;
  Link*           exceptlist;
  time_t          channelts;
  int             locally_created;  /* used to flag a locally created channel */
  int             keep_their_modes; /* used only on mode after sjoin */
#ifdef FLUD
  time_t          fludblock;
  struct fludbot* fluders;
#endif
#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
  struct Channel* last_empty_channel;
  struct Channel* next_empty_channel;
#endif
  char            chname[1];
};

#define CREATE 1        /* whether a channel should be
                           created or just tested for existance */

#define MODEBUFLEN      200

#define NullChn ((aChannel *)0)

#define ChannelExists(n)        (find_channel(n, NullChn) != NullChn)

/* Maximum mode changes allowed per client, per server is different */
#define MAXMODEPARAMS   4

extern  void sync_channels();
extern  struct  Channel *find_channel (char *, struct Channel *);
extern  void    remove_user_from_channel(struct Client *,struct Channel *,int);
extern  void    del_invite (struct Client *, struct Channel *);
extern  void    send_user_joins (struct Client *, struct Client *);
extern  int     can_send (struct Client *, struct Channel *);
extern  int     is_chan_op (struct Client *, struct Channel *);
extern  int     has_voice (struct Client *, struct Channel *);
extern  int     count_channels (struct Client *);
extern  int     m_names(struct Client *, struct Client *,int, char **);
extern  void    send_channel_modes (struct Client *, struct Channel *);
extern  struct  Channel *channel;


/* this should eliminate a lot of ifdef's in the main code... -orabidoo */
#ifdef BAN_INFO
#  define BANSTR(l)  ((l)->value.banptr->banstr)
#else
#  define BANSTR(l)  ((l)->value.cp)
#endif

/*
** Channel Related macros follow
*/

/* Channel related flags */

#define CHFL_CHANOP     0x0001 /* Channel operator */
#define CHFL_VOICE      0x0002 /* the power to speak */
#define CHFL_DEOPPED    0x0004 /* deopped by us, modes need to be bounced */
#define CHFL_BAN        0x0008 /* ban channel flag */
#define CHFL_EXCEPTION  0x0010 /* exception to ban channel flag */

/* Channel Visibility macros */

#define MODE_CHANOP     CHFL_CHANOP
#define MODE_VOICE      CHFL_VOICE
#define MODE_DEOPPED    CHFL_DEOPPED
#define MODE_PRIVATE    0x0008
#define MODE_SECRET     0x0010
#define MODE_MODERATED  0x0020
#define MODE_TOPICLIMIT 0x0040
#define MODE_INVITEONLY 0x0080
#define MODE_NOPRIVMSGS 0x0100
#define MODE_KEY        0x0200
#define MODE_BAN        0x0400
#define MODE_EXCEPTION  0x0800

#define MODE_LIMIT      0x1000  /* was 0x0800 */
#define MODE_FLAGS      0x1fff  /* was 0x0fff */

#if defined(PRESERVE_CHANNEL_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
#define MODE_SPLIT      0x1000
#endif

#ifdef JUPE_CHANNEL
#define MODE_JUPED      0x2000
#endif

/*
 * mode flags which take another parameter (With PARAmeterS)
 */
#define MODE_WPARAS (MODE_CHANOP|MODE_VOICE|MODE_BAN|\
                     MODE_EXCEPTION|MODE_KEY|MODE_LIMIT)

/*
 * Undefined here, these are used in conjunction with the above modes in
 * the source.
#define MODE_QUERY      0x10000000
#define MODE_DEL       0x40000000
#define MODE_ADD       0x80000000
 */

/* used in SetMode() in channel.c and m_umode() in s_msg.c */

#define MODE_NULL      0
#define MODE_QUERY     0x10000000
#define MODE_ADD       0x40000000
#define MODE_DEL       0x20000000

#define HoldChannel(x)          (!(x))
/* name invisible */
#define SecretChannel(x)        ((x) && ((x)->mode.mode & MODE_SECRET))
/* channel not shown but names are */
#define HiddenChannel(x)        ((x) && ((x)->mode.mode & MODE_PRIVATE))
/* channel visible */
#define ShowChannel(v,c)        (PubChannel(c) || IsMember((v),(c)))
#define PubChannel(x)           ((!x) || ((x)->mode.mode &\
                                 (MODE_PRIVATE | MODE_SECRET)) == 0)

#define IsMember(blah,chan) ((blah && blah->user && \
                find_channel_link((blah->user)->channel, chan)) ? 1 : 0)

#define IsChannelName(name) ((name) && (*(name) == '#' || *(name) == '&'))

#endif

