/************************************************************************
 *   IRC - Internet Relay Chat, include/s_bsd.h
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
 *   $Id$
 *
 */
#ifndef INCLUDED_s_bsd_h
#define INCLUDED_s_bsd_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;
struct ConfItem;
struct hostent;
struct FDList;

extern	int	highest_fd;
extern	int	readcalls;
extern	void    add_connection(struct Client *, int);
extern	int	add_listener (struct ConfItem *);
extern	int	check_client (struct Client *,char *,char **);
extern	int	check_server (struct Client *, struct hostent *,
				    struct ConfItem *, struct ConfItem *, int);
extern	int	check_server_init (struct Client *);
extern	void	close_connection (struct Client *);
extern	void	close_listeners ();
extern	int	connect_server (struct ConfItem *, struct Client *, struct hostent *);
extern	void	get_my_name (struct Client *, char *, int);
extern	int	get_sockerr (struct Client *);
extern	int	inetport (struct Client *, int, unsigned long);
extern	void	init_sys ();
extern  void    remove_hostent_references(const struct hostent* hp);
extern	int	read_message (time_t, struct FDList*);
extern	void	report_error (char *, struct Client *);
extern	void	set_non_blocking (int, struct Client *);

#endif /* INCLUDED_s_bsd_h */

