/* - Internet Relay Chat, include/listener.h
 *   Copyright (C) 1999 Thomas Helvey <tomh@inxpress.net>
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
 * $Id$
 */
#ifndef INCLUDED_listener_h
#define INCLUDED_listener_h

#ifndef INCLUDED_sys_types_h
#include <sys/types.h>     /* size_t */
#define INCLUDED_sys_types_h
#endif

enum ListenerState {
  LISTENER_CLOSED,
  LISTENER_ACCEPTING,
  LISTENER_CLOSING
};

/*
 * bahhh... C, feh :-)
 */
typedef enum ListenerState ListenerState;

struct Listener {
  struct Listener* next;            /* list node pointer */
  struct ConfItem* conf;            /* conf line for the port */
  int              fd;              /* file descriptor */
  int              port;            /* listener IP port */
  ListenerState    state;           /* current state */
  int              ref_count;       /* number of connection references */

};

#endif /* INCLUDED_listener_h */
