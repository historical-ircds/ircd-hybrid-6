/************************************************************************
 *   IRC - Internet Relay Chat, include/serv.h
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
 * $Id$
 *
 */
#ifndef INCLUDED_serv_h
#define INCLUDED_serv_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif

struct Client;

struct Capability
{
  char*        name;      /* name of capability */
  unsigned int cap;       /* mask value */
};

#define CAP_CAP         0x00000001      /* received a CAP to begin with */
#define CAP_QS          0x00000002      /* Can handle quit storm removal */
#define CAP_ZIP         0x00000004      /* Can do server compresion */
#define CAP_EX          0x00000008      /* Can do channel +e exemptions */
#define CAP_CHW         0x00000010      /* Can do channel wall @# */
#define CAP_DE          0x00000020      /* Can do channel +d (regex deny) */

#define DoesCAP(x)      ((x)->caps)

/*
 * Capability macros.
 */
#define IsCapable(x, cap)       ((x)->caps & (cap))
#define SetCapable(x, cap)      ((x)->caps |= (cap))
#define ClearCap(x, cap)        ((x)->caps &= ~(cap))

/*
** list of recognized server capabilities.  "TS" is not on the list
** because all servers that we talk to already do TS, and the kludged
** extra argument to "PASS" takes care of checking that.  -orabidoo
*/

#ifdef DEFINE_CAPTAB
struct Capability captab[] = 
{
/*  name        cap     */  

#ifdef ZIP_LINKS
  { "ZIP",      CAP_ZIP },
#endif
  { "QS",       CAP_QS },
  { "EX",       CAP_EX },
  { "CHW",      CAP_CHW },
  { "DE",       CAP_DE },
  { 0,   0 }
};
#else
extern struct Capability captab[];
#endif

/* 
 * allow DEFAULT_SERVER_SPLIT_RECOVERY_TIME minutes after server rejoins
 * the network before allowing chanops new channels,
 *  but allow it to be set to a maximum of MAX_SERVER_SPLIT_RECOVERY_TIME 
 */
#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
        defined(NO_JOIN_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT_SIMPLE)
#define MAX_SERVER_SPLIT_RECOVERY_TIME 30
#ifndef DEFAULT_SERVER_SPLIT_RECOVERY_TIME
#define DEFAULT_SERVER_SPLIT_RECOVERY_TIME 15
#endif /* DEFAULT_SERVER_SPLIT_RECOVERY_TIME */
#endif

/*
 * return values for hunt_server() 
 */
#define HUNTED_NOSUCH   (-1)    /* if the hunted server is not found */
#define HUNTED_ISME     0       /* if this server should execute the command */
#define HUNTED_PASS     1       /* if message passed onwards successfully */

extern void send_capabilities(struct Client* client, int use_zip);
extern int  hunt_server(struct Client* cptr, struct Client* sptr,
                        char* command, int server, int parc, char** parv);
extern int  server_estab(struct Client* cptr);

#endif /* INCLUDED_s_serv_h */



