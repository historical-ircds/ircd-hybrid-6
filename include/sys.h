/************************************************************************
 *   IRC - Internet Relay Chat, include/sys.h
 *   Copyright (C) 1990 University of Oulu, Computing Center
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
/*
 * ARGH!!!! this file is a dependency nightmare... --Bleep
 */
#ifndef	INCLUDED_sys_h
#define INCLUDED_sys_h
/*
 * XXX - All of this stuff is a good way to slow down builds, we should be
 * including stuff only where we need it to resolve dependencies.
 */
#ifndef INCLUDED_setup_h
#include "setup.h"
#endif

#if 0
#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif
#endif
#if 0
#ifndef INCLUDED_sys_param_h
#include <sys/param.h>
#define INCLUDED_sys_param_h
#endif
#endif

#if 0
#if defined( HAVE_UNISTD_H )
#ifndef INCLUDED_unistd_h
#include <unistd.h>
#define INCLUDED_unistd_h
#endif
#endif
#endif

#if 0
#if defined( HAVE_STDLIB_H )
#ifndef INCLUDED_stdlib_h
#include <stdlib.h>
#define INCLUDED_stdlib_h
#endif
#endif
#endif

#if 0
#ifdef __EMX__
#  include <os2.h>
#endif
#endif

#if 0
#ifdef	NO_U_TYPES
typedef	unsigned char	u_char;
typedef	unsigned short	u_short;
typedef	unsigned long	u_long;
typedef	unsigned int	u_int;
#endif
#endif

#endif /* INCLUDED_sys_h */

