/************************************************************************
 *   IRC - Internet Relay Chat, include/ircd.h
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
 * "ircd.h". - Headers file.
 *
 * $Id$
 *
 */
#ifndef INCLUDED_ircd_h
#define INCLUDED_ircd_h
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif

struct Client;

extern void     report_error_on_tty(const char* message);
extern  int     debuglevel;
extern  int     debugtty;
extern  char*   debugmode;
extern  time_t  check_fdlists (time_t);
extern struct Counter Count;
extern time_t NOW;
extern time_t nextconnect;
extern time_t nextping;
extern time_t timeofday;
extern struct Client* GlobalClientList;
extern struct Client  me;
extern struct Client* local[];
extern int    bootopt;
extern int    cold_start;

#endif
