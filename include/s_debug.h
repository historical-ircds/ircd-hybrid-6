#ifndef INCLUDED_s_debug_h
#define INCLUDED_s_debug_h
/************************************************************************
 *   IRC - Internet Relay Chat, include/s_conf.h
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 * $Id$
 *
 * $Log$
 * Revision 1.2  1999/07/21 05:20:52  db
 * - you guessed it, more cleanups. removed undefined function that had
 *   a prototype in h.h cool
 *
 * Revision 1.1  1999/07/16 02:40:34  db
 * - removed #ifdef HAVE_GET_RUSAGE, it still needs to be removed from configure
 * - removed some debug counters that weren't terribly useful
 * - always enabled stats r even without DEBUGMODE
 * - moved prototype for send_usage() into s_debug.h
 *
 */

struct Client;

extern void send_usage(struct Client*, char *);
extern	void	count_memory (struct Client *, char *);

extern	void	debug(int, char *, ...);

#endif /* INCLUDED_s_debug_h */

