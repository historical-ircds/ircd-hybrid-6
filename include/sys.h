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
#ifndef INCLUDED_errno_h
#include <errno.h>
#define INCLUDED_errno_h
#endif
#endif

#if 0
#ifndef INCLUDED_stdio_h
#include <stdio.h>
#define INCLUDED_stdio_h
#endif
#endif

#ifndef INCLUDED_sys_types_h
#include <sys/types.h>
#define INCLUDED_sys_types_h
#endif
#ifndef INCLUDED_sys_param_h
#include <sys/param.h>
#define INCLUDED_sys_param_h
#endif

#if defined( HAVE_UNISTD_H )
#ifndef INCLUDED_unistd_h
#include <unistd.h>
#define INCLUDED_unistd_h
#endif
#endif

#if defined( HAVE_STDLIB_H )
#ifndef INCLUDED_stdlib_h
#include <stdlib.h>
#define INCLUDED_stdlib_h
#endif
#endif

#if 0
#ifndef INCLUDED_zlib_h
#include "zlib.h"
#endif
#endif

#if 0
#if defined( HAVE_STRINGS_H ) && !defined( __GLIBC__ )
#include <strings.h>
#else
# if defined( HAVE_STRING_H )
# include <string.h>
# endif
#endif
#define	strcasecmp	irccmp
#define	strncasecmp	ircncmp
#if !defined( HAVE_INDEX )
#define   index   strchr
#define   rindex  strrchr
/*
extern	char	*index (char *, char);
extern	char	*rindex (char *, char);
*/
#endif

#endif

/*
 * XXX - ahem, why do we need to expose <sys/select.h> to every file in
 * the server? feh
 */
#ifdef AIX
#include <sys/select.h>
#endif
#if defined(HPUX )|| defined(AIX)
#include <time.h>
#ifdef AIX
#include <sys/time.h>
#endif
#else
#include <sys/time.h>
#endif

#ifdef __EMX__
#  include <os2.h>
#endif

/*
 * XXX - hide this in signal code
 */
#ifdef NEXT
#define VOIDSIG int	/* whether signal() returns int of void */
#else
#define VOIDSIG void	/* whether signal() returns int of void */
#endif

/*
 * XXX - MOVE THIS
 */
extern	VOIDSIG	dummy();

#ifdef	DYNIXPTX
#define	NO_U_TYPES
#endif

/*
 * XXX --- ARGH!!!!!
 */
#ifdef	OS_SOLARIS2
extern	int	gethostname(char *, int);
extern	long	random();
extern	void	srandom(unsigned);
#endif /* OS_SOLARIS2 */

#ifdef	NO_U_TYPES
typedef	unsigned char	u_char;
typedef	unsigned short	u_short;
typedef	unsigned long	u_long;
typedef	unsigned int	u_int;
#endif

#endif /* INCLUDED_sys_h */

