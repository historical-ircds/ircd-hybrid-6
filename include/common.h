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

#ifndef	__common_include__
#define __common_include__

#if defined( HAVE_PARAM_H )
#include <sys/param.h>
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

#define FALSE (0)
#define TRUE  (!FALSE)

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

#ifdef FOREVER
#undef FOREVER
#endif

#define FOREVER for(;;)
/* -Dianora */

#if !defined(STDC_HEADERS)
char	*malloc(), *calloc();
void	free();
#else
#ifdef MALLOCH
#include MALLOCH
#endif
#endif
#if !defined( HAVE_STRTOK )
extern	char	*strtok (char *, char *); 
#endif
#if !defined( HAVE_STRTOKEN )
extern	char	*strtoken (char **, char *, char *);
#endif
#if !defined( HAVE_INET_ADDR )
extern unsigned long inet_addr (char *);
#endif

#if !defined(HAVE_INET_NTOA) || !defined(HAVE_INET_NETOF)
#include <netinet/in.h>
#endif

#if !defined( HAVE_INET_NTOA )
extern char *inet_ntoa (struct in_addr);
#endif

#if !defined( HAVE_INET_NETOF )
extern int inet_netof (struct in_addr);
#endif

extern char *myctime (time_t);
extern char *strtoken (char **, char *, char *);

#if !defined(HAVE_MINMAX)
#ifndef MAX
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#endif /* !HAVE_MINMAX */

#define DupString(x,y) do{x=MyMalloc(strlen(y)+1);(void)strcpy(x,y);}while(0)

/*
 * match.h contains character comparison and conversion macros and
 * string comparison functions
 */
#ifndef INCLUDED_match_h
#include "match.h"
#endif

extern void flush_connections();
extern struct SLink *find_user_link(/* struct SLink *, struct Client * */);

#endif /* __common_includ_e_ */
