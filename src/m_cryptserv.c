/************************************************************************
 *   IRC - Internet Relay Chat, src/m_cryptserv.c
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
#include "s_bsd.h"

#include <string.h>
#include <stdlib.h>

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
 * m_cryptserv - CRYPTSERV message handler
 *      parv[0] = sender prefix
 *      parv[1] = servername
 *      parv[2] = RSA encoded session key data
 *      parv[3] = serverinfo
 */
int m_cryptserv(struct Client *cptr, struct Client *sptr, int parc, char *parv[])
{
  char             info[REALLEN + 1];
  char*            host;
  struct Client*   acptr;
  struct Client*   bcptr;
  struct ConfItem  *nline, *cline;
  int cipherIndex, keylen, authlen;
  int bogus_server = 0;
  int dots = 0;
  char *s;
  char tmp[CRYPT_RSASIZE * 2];
  char key[CRYPT_RSASIZE * 2];
  
  info[0] = '\0';

  /* We should only get this from local clients */
  if (cptr != sptr) {
    sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0], "CRYPTSERV");

    sendto_realops("CRYPTSERV command from %s -- %s is a hacked server",
		   get_client_name(sptr,SHOW_IP),
		   get_client_name(cptr,SHOW_IP));
    return exit_client(cptr, cptr, cptr, "Hacked server");
  }

  /* Never from users */
  if (IsPerson(cptr)) {
    sendto_one(cptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0], "CRYPTSERV");
    return 0;
  }
  
  /* And never from known servers */
  if (IsServer(cptr)) {
    sendto_realops("CRYPTSERV from server %s -- it's hacked",
		   get_client_name(cptr, SHOW_IP));
    return exit_client(cptr, cptr, cptr, "Hacked server");
  }

  if (parc < 2 || *parv[1] == '\0') {
    sendto_one(cptr,"ERROR :No servername");
    return 0;
  }
  host = parv[1];

  if (parc < 3 || *parv[2] == '\0') {
    sendto_one(cptr, "ERROR :No session key");
    return 0;
  }

  if (parc > 3) {
    strncpy_irc(info, parv[3], REALLEN);
    info[REALLEN] = '\0';
  }

  if (strlen(host) > HOSTLEN)
    host[HOSTLEN] = '\0';

  /* Copied some checks from m_server */
  s = host;
  while (*s)
    {
      if (!IsServChar(*s)) {
	bogus_server = 1;
	break;
      }
      if ('.' == *s)
	++dots;
      ++s;
    }
  
  if (!dots || bogus_server )
    {
      char clean_host[2 * HOSTLEN + 4];
      sendto_one(sptr,"ERROR :Bogus server name (%s)", 
		 clean_string(clean_host, (const unsigned char *) host, 2 * HOSTLEN));
      return exit_client(cptr, cptr, cptr, "Bogus server name");
    }
  

  if (MyConnect(cptr) && (GlobalSetOptions.autoconn == 0)) {
    sendto_realops("WARNING AUTOCONN is 0, Closing %s",
		   get_client_name(cptr, TRUE));
    return exit_client(cptr, cptr, cptr, "AUTOCONNS off");
  }

  /* The following if statement would be nice to remove
   * since user nicks never have '.' in them and servers
   * must always have '.' in them. There should never be a 
   * server/nick name collision, but it is possible a capricious
   * server admin could deliberately do something strange.
   *
   * -Dianora
   */
  if ((acptr = find_client(host, NULL)) && acptr != cptr)
    {
      /*
       * Server trying to use the same name as a person. Would
       * cause a fair bit of confusion. Enough to make it hellish
       * for a while and servers to send stuff to the wrong place.
       */
      sendto_one(cptr,"ERROR :Nickname %s already exists!", host);
      sendto_ops("Link %s cancelled: Server/nick collision on %s",
                 /* inpath */ get_client_name(cptr,FALSE), host);
      return exit_client(cptr, cptr, cptr, "Nick as Server");
    }

  if ((acptr = find_server(host)))
    {
      /*
       * This link is trying feed me a server that I already have
       * access through another path -- multiple paths not accepted
       * currently, kill this link immediately!!
       *
       * Rather than KILL the link which introduced it, KILL the
       * youngest of the two links. -avalon
       */
      char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */

      bcptr = (cptr->firsttime > acptr->from->firsttime) ? cptr : acptr->from;
      sendto_one(bcptr,"ERROR :Server %s already exists", host);
      if (bcptr == cptr)
      {
        sendto_realops("Link %s cancelled, server %s already exists",
                   get_client_name(bcptr, TRUE), host);
        return exit_client(bcptr, bcptr, &me, "Server Exists");
      }
      /*
       * in this case, we are not dropping the link from
       * which we got the SERVER message.  Thus we canNOT
       * `return' yet! -krys
       *
       *
       * get_client_name() can return ptr to static buffer...can't use
       * 2 times in same sendto_ops(), so we have to strcpy one =(
       *  - comstud
       */
      strcpy(nbuf, get_client_name(bcptr, TRUE));
      sendto_realops("Link %s cancelled, server %s reintroduced by %s",
                nbuf, host, get_client_name(cptr, TRUE));
      exit_client(bcptr, bcptr, &me, "Server Exists");
    }

  strncpy_irc(cptr->name, host, HOSTLEN);
  strncpy_irc(cptr->info, info[0] ? info : me.name, REALLEN);
  cptr->hopcount = 0;



  nline = find_conf_by_name(cptr->name, CONF_NOCONNECT_SERVER);
  cline = find_conf_by_name(cptr->name, CONF_CONNECT_SERVER);

  if (!nline || !cline) {
#ifdef WARN_NO_NLINE
    sendto_realops("No N/C line for %s - dropping link", get_client_name(cptr, TRUE));
#endif
    return exit_client(cptr, cptr, cptr, "Missing C/N-line");
  }
 

  if (!cptr->crypt) {
    if (crypt_initserver(cptr, cline, nline) != CRYPT_ENCRYPTED)
      return CRYPT_ERROR;

    if (crypt_rsa_encode(cptr->crypt->RSAKey, cptr->crypt->inkey, tmp, sizeof(cptr->crypt->inkey)) != CRYPT_ENCRYPTED) {
      return exit_client(cptr, cptr, cptr, "Failed to generate session key data");
    }
    send_capabilities(cptr, (cline->flags & CONF_FLAGS_ZIP_LINK));
    sendto_one(cptr, "CRYPTSERV %s %s :%s", my_name_for_link(me.name, nline), tmp, me.info);
  }

  if (!cptr->ciphers) {
    sendto_realops("%s wanted an encrypted link without supplying cipher list", get_client_name(cptr, TRUE));
    return exit_client(cptr, cptr, cptr, "Need supported cipher list");
  }

  cipherIndex = crypt_selectcipher(cptr->ciphers);
  if (cipherIndex < 0) {
    sendto_realops("No ciphers in common with %s - can't link", cptr->name);    
    return exit_client(cptr, cptr, cptr, "No common ciphers");
  }

  if (crypt_rsa_decode(parv[2], key, &keylen) != CRYPT_DECRYPTED) {
    sendto_realops("Failed decrypting session key received from %s", get_client_name(cptr, TRUE));
    return exit_client(cptr, cptr, cptr, "Invalid session key data");
  }

  authlen = Ciphers[cipherIndex].keysize / 8;
  authlen = ((authlen + 7) / 8) * 8;

  if (keylen * 8 < authlen) {
    return exit_client(cptr, cptr, cptr, "Not enough session key data");
  }

  cptr->crypt->OutCipher = &Ciphers[cipherIndex];
  cptr->crypt->OutState = (void *) malloc(cptr->crypt->OutCipher->state_data_size);
  memset(cptr->crypt->OutState, 0, cptr->crypt->OutCipher->state_data_size);
  cptr->crypt->OutCipher->init(cptr->crypt->OutState, key);

 
  if (crypt_rsa_encode(cptr->crypt->RSAKey, key, tmp, authlen ) != CRYPT_ENCRYPTED) {
    return exit_client(cptr, cptr, cptr, "Couldn't generate session key authentication");
  }
  sendto_one(cptr, "CRYPTAUTH %s/%i %s", cptr->crypt->OutCipher->name, cptr->crypt->OutCipher->keysize, tmp);
  send_queued(cptr);
  cptr->crypt->flags |= CRYPTFLAG_ENCRYPT;
  return 1;
}


