/************************************************************************
 *   IRC - Internet Relay Chat, src/m_userhost.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers. 
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
 */

#include "m_commands.h"
#include "client.h"
#include "ircd.h"
#include "numeric.h"
#include "s_serv.h"
#include "send.h"
#include "irc_string.h"
#include <string.h>

static char buf[BUFSIZE];

/* m_functions execute protocol messages on this server:
 *
 *      cptr    is always NON-NULL, pointing to a *LOCAL* client
 *              structure (with an open socket connected!). This
 *              identifies the physical socket where the message
 *              originated (or which caused the m_function to be
 *              executed--some m_functions may call others...).
 *
 *      sptr    is the source of the message, defined by the
 *              prefix part of the message if present. If not
 *              or prefix not found, then sptr==cptr.
 *
 *              (!IsServer(cptr)) => (cptr == sptr), because
 *              prefixes are taken *only* from servers...
 *
 *              (IsServer(cptr))
 *                      (sptr == cptr) => the message didn't
 *                      have the prefix.
 *
 *                      (sptr != cptr && IsServer(sptr) means
 *                      the prefix specified servername. (?)
 *
 *                      (sptr != cptr && !IsServer(sptr) means
 *                      that message originated from a remote
 *                      user (not local).
 *
 *              combining
 *
 *              (!IsServer(sptr)) means that, sptr can safely
 *              taken as defining the target structure of the
 *              message in this server.
 *
 *      *Always* true (if 'parse' and others are working correct):
 *
 *      1)      sptr->from == cptr  (note: cptr->from == cptr)
 *
 *      2)      MyConnect(sptr) <=> sptr == cptr (e.g. sptr
 *              *cannot* be a local connection, unless it's
 *              actually cptr!). [MyConnect(x) should probably
 *              be defined as (x == x->from) --msa ]
 *
 *      parc    number of variable parameter strings (if zero,
 *              parv is allowed to be NULL)
 *
 *      parv    a NULL terminated list of parameter pointers,
 *
 *                      parv[0], sender (prefix string), if not present
 *                              this points to an empty string.
 *                      parv[1]...parv[parc-1]
 *                              pointers to additional parameters
 *                      parv[parc] == NULL, *always*
 *
 *              note:   it is guaranteed that parv[0]..parv[parc-1] are all
 *                      non-NULL pointers.
 */

/*
 * m_userhost added by Darren Reed 13/8/91 to aid clients and reduce
 * the need for complicated requests like WHOIS. It returns user/host
 * information only (no spurious AWAY labels or channels).
 */
/* rewritten Diane Bruce 1999 */

int     m_userhost(struct Client *cptr,
                   struct Client *sptr,
                   int parc,
                   char *parv[])
{
  char  *p;            /* scratch end pointer */
  char  *cn;           /* current name */
  struct Client *acptr;
  char response[5][NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30];
  int i;               /* loop counter */

  if (parc < 2)
    {
      sendto_one(sptr, form_str(ERR_NEEDMOREPARAMS),
                 me.name, parv[0], "USERHOST");
      return 0;
    }

  /* The idea is to build up the response string out of pieces
   * none of this strlen() nonsense.
   * 5 * (NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30) is still << sizeof(buf)
   * and our ircsprintf() truncates it to fit anyway. There is
   * no danger of an overflow here. -Dianora
   */

  response[0][0] = response[1][0] = response[2][0] = 
    response[3][0] = response[4][0] = '\0';

  cn = parv[1];

  for(i=0; (i < 5) && cn; i++ )
    {
      if((p = strchr(cn, ' ')))
        *p = '\0';

      if ((acptr = find_person(cn, NULL)))
        {
          ircsprintf(response[i], "%s%s=%c%s@%s",
		     acptr->name,
		     IsAnOper(acptr) ? "*" : "",
		     (acptr->user->away) ? '-' : '+',
		     acptr->username,
		     acptr->host);
        }
      if(p)
        p++;
      cn = p;
    }

  ircsprintf(buf, "%s%s %s %s %s %s",
    form_str(RPL_USERHOST),
    response[0], response[1], response[2], response[3], response[4] );
  sendto_one(sptr, buf, me.name, parv[0]);

  return 0;
}
