/************************************************************************
 *   IRC - Internet Relay Chat, include/gline.h
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
 * "m_gline.h". - Headers file.
 *
 * $Id$
 *
 */

#ifndef INCLUDED_m_gline_h
#define INCLUDED_m_gline_h

struct Client;
struct ConfItem;

#ifdef  GLINES
extern struct ConfItem* find_gkill(struct Client* client);
extern struct ConfItem* find_is_glined(const char* host, const char* name);
extern void   flush_glines(void);             
extern void   report_glines(struct Client *); 
#endif


#endif
