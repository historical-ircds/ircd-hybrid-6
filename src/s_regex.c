/************************************************************************
 *   IRC - Internet Relay Chat, src/s_regex.c
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
 *   s_regex.c
 *   code to deal with regex stuff (ie. channel modes +d / +a)
 *   (would we ever want regex parsing for ircd.conf as well? put it here!)
 *
 *
 */

#include "s_regex.h"
#ifdef RE_BSD
# include <unistd.h>
#endif
#ifdef RE_SOLARIS
# include <re_comp.h>
#endif
#ifdef RE_LINUX
# include <regex.h>
#endif
