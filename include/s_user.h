/************************************************************************
 *   IRC - Internet Relay Chat, include/h.h
 *   Copyright (C) 1992 Darren Reed
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
 *
 * "s_user.h". - Headers file.
 *
 * Most of the externs and prototypes thrown in here to 'cleanup' things.
 * -avalon
 *
 * $Id$
 *
 */
#ifndef INCLUDED_s_user_h
#define INCLUDED_s_user_h

struct Client;

extern  void    send_umode (struct Client *, struct Client *,
                            int, int, char *);
extern  void    send_umode_out (struct Client*, struct Client *, int);
extern  int     m_umode(struct Client *, struct Client *, int, char **);
extern  int     show_lusers(struct Client *, struct Client *, int, char **);
extern  int     hunt_server (struct Client *,struct Client *,char *,int,int,char **);
extern  struct Client   *next_client (struct Client *, char *);
extern  struct Client   *next_client_double (struct Client *, char *);

#endif
