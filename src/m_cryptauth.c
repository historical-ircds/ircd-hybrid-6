/************************************************************************
 *   IRC - Internet Relay Chat, src/m_cryptauth.c
 *   Copyright (C) 2001 einride <einride@einride.net>
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
 */
#include "m_commands.h"  /* m_server prototype */
#include "client.h"      /* client struct */
#include "common.h"      /* TRUE bleah */
#include "hash.h"        /* add_to_client_hash_table */
#include "irc_string.h"  /* strncpy_irc */
#include "ircd.h"        /* me */
#include "list.h"        /* make_server */
#include "numeric.h"     /* ERR_xxx */
#include "s_conf.h"      /* struct ConfItem */
#include "s_serv.h"      /* server_estab, check_server, my_name_for_link */
#include "s_stats.h"     /* ServerStats */
#include "scache.h"      /* find_or_add */
#include "send.h"        /* sendto_one */
#include "struct.h"      /* bleah */
#include "s_log.h"
#include "s_crypt.h"

#include <string.h>
#include <stdlib.h>

#ifdef CRYPT_LINKS

/*
 * m_functions execute protocol messages on this server:
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
 * m_cryptauth - CRYPTAUTH message handler
 *      parv[0] = sender prefix
 *      parv[1] = Selected cipher
 *      parv[2] = RSA encoded session key data
 */
int m_cryptauth(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  int cipherIndex, keylen;
  unsigned char key[CRYPT_RSASIZE+1];

  if (cptr != sptr)
    {
      sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0], "CRYPTAUTH");

#ifdef HIDE_SERVERS_IPS
      sendto_realops("CRYPTAUTH command from %s -- %s is a hacked server",
      		     sptr->name, cptr->name);
#else		     
      sendto_realops("CRYPTAUTH command from %s -- %s is a hacked server",
		     get_client_name(sptr,SHOW_IP),
		     get_client_name(cptr,SHOW_IP));
#endif		     
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Hacked server");
    }

  /* Never from users */
  if (IsPerson(cptr))
    {
      sendto_one(cptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0], "CRYPTAUTH");
      return 0;
    }
  
  /* And never from known servers */
  if (IsServer(cptr))
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("CRYPTAUTH from server %s -- it's hacked", cptr->name);
#else      
      sendto_realops("CRYPTAUTH from server %s -- it's hacked",
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN, "CRYPTAUTH from server %s -- it's hacked",
	  get_client_name(cptr, SHOW_IP));
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Hacked server");
    }

  if (parc < 3)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Invalid CRYPTAUTH data from %s", cptr->name);
#else      
      sendto_realops("Invalid CRYPTAUTH data from %s",
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN, "Invalid CRYPTAUTH data from %s",
	  get_client_name(cptr, SHOW_IP));
      if (cptr->crypt)
	cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Invalid encrypted link authentication");
    }
  
  if (!cptr->crypt || !cptr->crypt->OutCipher || !cptr->crypt->OutState)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Got CRYPTAUTH but no CRYPTSERV from %s", cptr->name);
#else      
      sendto_realops("Got CRYPTAUTH but no CRYPTSERV from %s",
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN, "Got CRYPTAUTH but no CRYPTSERV from %s",
	  get_client_name(cptr, SHOW_IP));
      if (cptr->crypt)
	cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "No CRYPTSERV received");
    }

  if (cptr->crypt->InCipher || cptr->crypt->InState)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Got multiple CRYPTAUTH from %s", cptr->name);
#else      
      sendto_realops("Got multiple CRYPTAUTH from %s",
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN, "Got multiple CRYPTAUTH from %s",
	  get_client_name(cptr, SHOW_IP));
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Multiple CRYPTAUTH received");
    }

  cipherIndex = crypt_selectcipher(parv[1]);

  if (cipherIndex < 0)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Unsupported cipher %s selected by %s", parv[1],
                     cptr->name);
#else		     
      sendto_realops("Unsupported cipher %s selected by %s", parv[1], 
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN, "Unsupported cipher %s selected by %s", parv[1], 
	  get_client_name(cptr, SHOW_IP));
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Unsupported cipher");
    }

  cptr->crypt->InCipher = &Ciphers[cipherIndex];
  cptr->crypt->InState = (void *) malloc(cptr->crypt->InCipher->state_data_size);
  memset(cptr->crypt->InState, 0, cptr->crypt->InCipher->state_data_size);
  cptr->crypt->InCipher->init(cptr->crypt->InState, cptr->crypt->inkey);

  if (crypt_rsa_decode(parv[2], key, &keylen) != CRYPT_DECRYPTED)
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Couldn't decrypt session key authentication data from %s",
      		     cptr->name);
#else		     
      sendto_realops("Couldn't decrypt session key authentication data from %s", 
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN,"Couldn't decrypt session key authentication data from %s", 
	  get_client_name(cptr, SHOW_IP));
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Session key authentication failed");
    }

  if ((keylen * 8 < Ciphers[cipherIndex].keysize) || 
      (memcmp(key, cptr->crypt->inkey, Ciphers[cipherIndex].keysize / 8)))
    {
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Invalid session key authentication data from %s",
                     cptr->name);
#else		     
      sendto_realops("Invalid session key authentication data from %s", 
		     get_client_name(cptr, SHOW_IP));
#endif		     
      log(L_WARN,"Invalid session key authentication data from %s", 
	  get_client_name(cptr, SHOW_IP));
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
      return exit_client(cptr, cptr, cptr, "Session key authentication failed");
    }

  if (check_server(cptr))
    {
      cptr->crypt->flags |= CRYPTFLAG_DECRYPT;
      return server_estab(cptr);
    }

  ++ServerStats->is_ref;
  sendto_ops("Received unauthorized connection from %s.",
	     get_client_host(cptr));
  return exit_client(cptr, cptr, cptr, "No C/N conf lines");
}


#endif /* CRYPT_LINKS */

