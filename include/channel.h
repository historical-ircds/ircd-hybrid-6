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

struct Client;
struct Channel;

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

#endif
