/************************************************************************
 *   IRC - Internet Relay Chat, include/common.h
 *   Copyright (C) 1990 Armin Gruner
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
 * $Id$
 */
#ifndef	INCLUDED_common_h
#define INCLUDED_common_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

#ifndef NULL
#define NULL 0
#endif

#ifdef TRUE
#undef TRUE
#endif

#ifdef FALSE
#undef FALSE
#endif

#define FALSE  0
#define TRUE   1
#define HIDEME 2

/* Blah. I use these a lot. -Dianora */
#ifdef YES
#undef YES
#endif

#define YES 1

#ifdef NO
#undef NO
#endif

#define NO  0

/* Just blindly define our own MIN/MAX macro */

#define IRCD_MAX(a, b)	((a) > (b) ? (a) : (b))
#define IRCD_MIN(a, b)	((a) < (b) ? (a) : (b))

/*
 * irc_string.h contains character comparison and conversion macros and
 * string functions
 */
#ifndef INCLUDED_irc_string_h
#include "irc_string.h"
#endif

extern void flush_connections();
extern struct SLink *find_user_link(/* struct SLink *, struct Client * */);

/* argh where should this go? */

typedef struct
{
  int mode;
  char letter;
} FLAG_ITEM;


/*
 * XXX - ACK!!!
 */
/*
 * ZZZ - These can go away slowly as they are rewritten.
 * calm down Tom.
 * heh :) --Bleep
 *
 */
#define MAXCLIENTS GlobalSetOptions.maxclients
#define NOISYHTM   GlobalSetOptions.noisy_htm
#define LIFESUX    GlobalSetOptions.lifesux
#define AUTOCONN   GlobalSetOptions.autoconn
#define IDLETIME   GlobalSetOptions.idletime
#define FLUDNUM    GlobalSetOptions.fludnum
#define FLUDTIME   GlobalSetOptions.fludtime
#define FLUDBLOCK  GlobalSetOptions.fludblock
#define DRONETIME  GlobalSetOptions.dronetime
#define DRONECOUNT GlobalSetOptions.dronecount
#define SPLITDELAY GlobalSetOptions.server_split_recovery_time
#define SPLITNUM   GlobalSetOptions.split_smallnet_size
#define SPLITUSERS GlobalSetOptions.split_smallnet_users
#define SPAMNUM    GlobalSetOptions.spam_num
#define SPAMTIME   GlobalSetOptions.spam_time

#endif /* INCLUDED_common_h */
