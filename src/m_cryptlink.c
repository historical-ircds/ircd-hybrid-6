/************************************************************************
 *   IRC - Internet Relay Chat, src/m_cryptlink.c
 *   Copyright (C) 2001 einride <einride@einride.net>
 *   Modified by jdc
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
#include "s_bsd.h"
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


static int bogus_host(char *host);
static int cryptlink_auth(struct Client *, struct Client *, int, char **);
static int cryptlink_serv(struct Client *, struct Client *, int, char **);


struct CryptLinkStruct
{
  char *cmd;                    /* CRYPTLINK <command> to match */
  int  (*handler)();            /* Function to call */
};

static struct CryptLinkStruct cryptlink_cmd_table[] =
{
  /* command       function           */
  { "AUTH",        cryptlink_auth,    },
  { "SERV",        cryptlink_serv,    },
  /* End of table */
  { (char *) 0,   (int (*)()) 0,      }
};

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
 * m_cryptlink - CRYPTLINK message handler
 *      parv[0] = CRYPTLINK
 *      parv[1] = command (SERV or AUTH):
 *                   SERV - parc must be >= 5
 *                          parv[0] == CRYPTLINK
 *                          parv[1] == SERV
 *                          parv[2] == server name
 *                          parv[3] == keyphrase
 *                          parv[4] == :server info (M-line)
 *                   AUTH - parc must be >= 4
 *                          parv[0] == CRYPTLINK
 *                          parv[1] == AUTH
 *                          parv[2] == cipher (eg. BF/256)
 *                          parv[3] == keyphrase
 *
 * NOTE:  This handler simply calls the old functions cryptlink_serv and
 *        cryptlink_auth, passing all the arguments over.  This could be
 *        considered a "wrapper" of sorts, but be sure to note that
 *        the parv[] values in cryptlink_serv and cryptlink_auth were
 *        changed too.
 */
int m_cryptlink(struct Client *cptr, struct Client *sptr,
                int parc, char *parv[])
{
  unsigned int i;

  for (i = 0; cryptlink_cmd_table[i].handler; i++)
  {
    /* Traverse through the command table */
    if (!irccmp(cryptlink_cmd_table[i].cmd, parv[1]))
    {
      /* Match found.  Time to execute the function */
      return cryptlink_cmd_table[i].handler(cptr, sptr, parc, parv);
      break;
    }
  }
  return(0);
}


/*
 * cryptlink_auth - CRYPTLINK AUTH message handler
 *                    parv[0] = CRYPTLINK
 *                    parv[1] = AUTH
 *                    parv[2] = Selected cipher
 *                    parv[3] = RSA encoded session key data
 */
int cryptlink_auth(struct Client *cptr, struct Client *sptr,
                   int parc, char *parv[])
{
  struct CipherDef *cdef;
  int keylen;
  unsigned char key[CRYPT_RSASIZE+1];

  if (cptr != sptr)
  {
#ifndef HIDE_SERVERS_IPS
    char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */
#endif
    sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0],
               "CRYPTLINK AUTH");

#ifdef HIDE_SERVERS_IPS
    sendto_realops("CRYPTLINK AUTH command from %s -- %s is a hacked server",
                   sptr->name, cptr->name);
#else
    strcpy(nbuf, get_client_name(sptr, SHOW_IP));
    sendto_realops("CRYPTLINK AUTH command from %s -- %s is a hacked server",
                   nbuf, get_client_name(cptr,SHOW_IP));
#endif

    cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;

    return exit_client(cptr, cptr, cptr,
                       "Hacked server");
  }

  /* Never from users */
  if (IsPerson(cptr))
  {
    sendto_one(cptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0],
               "CRYPTLINK AUTH");
    return(0);
  }

  /* And never from known servers */
  if (IsServer(cptr))
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("CRYPTLINK AUTH from server %s -- it's hacked",
                   cptr->name);
#else
    sendto_realops("CRYPTLINK AUTH from server %s -- it's hacked",
                   get_client_name(cptr, SHOW_IP));
#endif
    log(L_WARN, "CRYPTLINK AUTH from server %s -- it's hacked",
        get_client_name(cptr, SHOW_IP));

    cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;

    return exit_client(cptr, cptr, cptr,
                       "Hacked server");
  }

  if (parc < 4)
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("Invalid CRYPTLINK AUTH data from %s", cptr->name);
#else
    sendto_realops("Invalid CRYPTLINK AUTH data from %s",
                   get_client_name(cptr, SHOW_IP));
#endif
    log(L_WARN, "Invalid CRYPTLINK AUTH data from %s",
        get_client_name(cptr, SHOW_IP));

    if (cptr->crypt)
    {
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
    }

    return exit_client(cptr, cptr, cptr,
                       "Invalid encrypted link authentication");
  }

  if (!cptr->crypt || !cptr->crypt->OutCipher || !cptr->crypt->OutState)
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("Got CRYPTLINK AUTH but no CRYPTLINK SERV from %s",
                   cptr->name);
#else
    sendto_realops("Got CRYPTLINK AUTH but no CRYPTLINK SERV from %s",
                   get_client_name(cptr, SHOW_IP));
#endif
    log(L_WARN, "Got CRYPTLINK AUTH but no CRYPTLINK SERV from %s",
        get_client_name(cptr, SHOW_IP));

    if (cptr->crypt)
    {
      cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;
    }

    return exit_client(cptr, cptr, cptr,
                       "No CRYPTLINK SERV received");
  }

  if (cptr->crypt->InCipher || cptr->crypt->InState)
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("Got multiple CRYPTLINK AUTHs from %s", cptr->name);
#else
    sendto_realops("Got multiple CRYPTLINK AUTHs from %s",
                   get_client_name(cptr, SHOW_IP));
#endif
    log(L_WARN, "Got multiple CRYPTLINK AUTHs from %s",
                get_client_name(cptr, SHOW_IP));

    cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;

    return exit_client(cptr, cptr, cptr, "Multiple CRYPTAUTH received");
  }

  cdef = crypt_selectcipher(parv[2]);

  if (cdef == NULL)
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("Unsupported cipher '%s' received from %s",
                   parv[2], cptr->name);
#else
    sendto_realops("Unsupported cipher '%s' received from %s"
                   parv[2], get_client_name(cptr, SHOW_IP));
#endif
    log(L_WARN, "Unsupported cipher '%s' received from %s", parv[2],
        get_client_name(cptr, SHOW_IP));

    cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;

    return exit_client(cptr, cptr, cptr, "Unsupported cipher");
  }

  cptr->crypt->InCipher = cdef;
  cptr->crypt->InState = (void *) malloc(cptr->crypt->InCipher->state_data_size);
  memset(cptr->crypt->InState, 0, cptr->crypt->InCipher->state_data_size);
  cptr->crypt->InCipher->init(cptr->crypt->InState, cptr->crypt->inkey);

  if (crypt_rsa_decode(parv[3], key, &keylen) != CRYPT_DECRYPTED)
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("Couldn't decrypt or base64 decode session key from %s",
                   cptr->name);
#else
    sendto_realops("Couldn't decrypt or base64 decode session key from %s",
                   get_client_name(cptr, SHOW_IP));
#endif
    log(L_WARN, "Couldn't decrypt or base64 decode session key from %s",
        get_client_name(cptr, SHOW_IP));

    cptr->crypt->flags &= ~CRYPTFLAG_ENCRYPT;

    return exit_client(cptr, cptr, cptr,
                       "Session key authentication failed");
  }

  if ((keylen * 8 < cdef->keysize) ||
      (memcmp(key, cptr->crypt->inkey, cdef->keysize / 8)))
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

    return exit_client(cptr, cptr, cptr,
                       "Session key authentication failed");
  }

  if (check_server(cptr))
  {
    cptr->crypt->flags |= CRYPTFLAG_DECRYPT;
    return server_estab(cptr);
  }

  ServerStats->is_ref++;
  sendto_ops("Received unauthorized connection from %s.",
             get_client_host(cptr));

  return exit_client(cptr, cptr, cptr, "No C/N conf lines");
}



/*
 * cryptlink_serv - CRYPTLINK SERV message handler
 *                    parv[0] = CRYPTLINK
 *                    parv[1] = SERV
 *                    parv[2] = servername
 *                    parv[3] = RSA encoded session key data
 *                    parv[4] = serverinfo
 */
static int cryptlink_serv(struct Client *cptr, struct Client *sptr,
                          int parc, char *parv[])
{
  char              info[REALLEN + 1];
  char             *host;
  struct Client    *acptr;
  struct Client    *bcptr;
  struct ConfItem  *nline;
  struct ConfItem  *cline;
  struct CipherDef *cdef;
  int keylen, authlen;
  char tmp[CRYPT_RSASIZE * 2];
  char key[CRYPT_RSASIZE * 2];

  info[0] = '\0';

  /* We should only get this from local clients */
  if (cptr != sptr)
  {
#ifndef HIDE_SERVERS_IPS
    char nbuf[HOSTLEN * 2 + USERLEN + 5]; /* same size as in s_misc.c */
#endif
    sendto_one(sptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0],
               "CRYPTLINK SERV");
#ifdef HIDE_SERVERS_IPS
    sendto_realops("CRYPTLINK SERV command from %s -- %s is a hacked server",
                   sptr->name, cptr->name);
#else
    strcpy(nbuf,get_client_name(sptr,SHOW_IP));
    sendto_realops("CRYPTLINK SERV command from %s -- %s is a hacked server",
                   nbuf, get_client_name(cptr,SHOW_IP));
#endif
    return exit_client(cptr, cptr, cptr, "Hacked server");
  }

  /* Never from users */
  if (IsPerson(cptr))
  {
    sendto_one(cptr, form_str(ERR_UNKNOWNCOMMAND), me.name, parv[0],
               "CRYPTLINK SERV");
    return 0;
  }

  /* And never from known servers */
  if (IsServer(cptr))
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("CRYPTLINK SERV from server %s -- it's hacked",
                   cptr->name);
#else
    sendto_realops("CRYPTLINK SERV from server %s -- it's hacked",
                   get_client_name(cptr, SHOW_IP));
#endif
    return exit_client(cptr, cptr, cptr, "Hacked server");
  }

  if (parc < 3 || *parv[2] == '\0')
  {
    sendto_one(cptr,"ERROR :No servername");
    return 0;
  }
  host = parv[2];

  if (parc < 4 || *parv[3] == '\0')
  {
    sendto_one(cptr, "ERROR :No session key");
    return 0;
  }

  if (parc > 4)
  {
    strncpy_irc(info, parv[4], REALLEN);
    info[REALLEN] = '\0';
  }

  if (strlen(host) > HOSTLEN)
  {
    host[HOSTLEN] = '\0';
  }

  if (bogus_host(host))
  {
    return exit_client(cptr, cptr, cptr, "Bogus server name");
  }

  if ( (MyConnect(cptr)) && (GlobalSetOptions.autoconn == 0) )
  {
#ifdef HIDE_SERVERS_IPS 
    sendto_realops("WARNING AUTOCONN is 0, Closing %s",
                   cptr->name);
#else
    sendto_realops("WARNING AUTOCONN is 0, Closing %s",
                   get_client_name(cptr, TRUE));
#endif
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
#ifdef HIDE_SERVERS_IPS
    sendto_ops("Link %s cancelled: Server/nick collision on %s",
               cptr->name, host);
#else
    sendto_ops("Link %s cancelled: Server/nick collision on %s",
               get_client_name(cptr,FALSE), host);
#endif
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
#ifdef HIDE_SERVERS_IPS
      sendto_realops("Link %s cancelled, server %s already exists",
                     bcptr->name, host);
#else
      sendto_realops("Link %s cancelled, server %s already exists",
                     get_client_name(bcptr, TRUE), host);
#endif
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
#ifdef HIDE_SERVERS_IPS
    strcpy(nbuf, get_client_name(bcptr, MASK_IP));
    sendto_realops("Link %s cancelled, server %s reintroduced by %s",
                   nbuf, host, cptr->name);
#else
    strcpy(nbuf, get_client_name(bcptr, TRUE));
    sendto_realops("Link %s cancelled, server %s reintroduced by %s",
                   nbuf, host, get_client_name(cptr, TRUE));
#endif
    exit_client(bcptr, bcptr, &me, "Server Exists");
  }

  strncpy_irc(cptr->name, host, HOSTLEN);
  strncpy_irc(cptr->info, info[0] ? info : me.name, REALLEN);
  cptr->hopcount = 0;


  nline = find_conf_by_name(cptr->name, CONF_NOCONNECT_SERVER);
  cline = find_conf_by_name(cptr->name, CONF_CONNECT_SERVER);

  if ( (nline == NULL) || (cline == NULL) )
  {
#ifdef WARN_NO_NLINE
#ifdef HIDE_SERVERS_IPS
    sendto_realops("No C/N line for %s - dropping link",
                   get_client_name(cptr, MASK_IP));
#else
    sendto_realops("No C/N line for %s - dropping link",
                   get_client_name(cptr, TRUE));
#endif
    log(L_NOTICE, "Access denied. No C/N line for server %s",
        get_client_name(cptr, TRUE));
#endif
    return exit_client(cptr, cptr, cptr, "Missing C-line or N-line");
  }


  if (cptr->crypt == NULL)
  {
    if (crypt_initserver(cptr, cline, nline) != CRYPT_ENCRYPTED)
    {
      log(L_ERROR, "crypt_initserver() failed.");
      return CRYPT_ERROR;
    }

    if (crypt_rsa_encode(cptr->crypt->RSAKey, cptr->crypt->inkey,
                         tmp, sizeof(cptr->crypt->inkey)) != CRYPT_ENCRYPTED)
    {
      return exit_client(cptr, cptr, cptr,
                         "Failed to generate session key data");
    }
    send_capabilities(cptr, (cline->flags & CONF_FLAGS_ZIP_LINK));
    sendto_one(cptr, "CRYPTLINK SERV %s %s :%s",
               my_name_for_link(me.name, nline), tmp, me.info);
  }

  if (cptr->cipher == NULL)
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("%s wanted an encrypted link without supplying cipher",
                   cptr->name);
#else
    sendto_realops("%s wanted an encrypted link without supplying cipher",
                   get_client_name(cptr, TRUE));
#endif
    return exit_client(cptr, cptr, cptr, "Need supported cipher");
  }

  cdef = crypt_selectcipher(cptr->cipher->name);

  /* Compare ciphers */
  if (cdef == NULL)
  {
    sendto_realops("No ciphers in common with %s - can't link", cptr->name);
    return exit_client(cptr, cptr, cptr, "No common ciphers");
  }

  if (crypt_rsa_decode(parv[3], key, &keylen) != CRYPT_DECRYPTED) 
  {
#ifdef HIDE_SERVERS_IPS
    sendto_realops("Failed decrypting session key received from %s",
                   cptr->name);
#else
    sendto_realops("Failed decrypting session key received from %s",
                   get_client_name(cptr, TRUE));
#endif
    return exit_client(cptr, cptr, cptr, "Invalid session key data");
  }

  authlen = cdef->keysize / 8;
  authlen = ((authlen + 7) / 8) * 8;

  if (keylen * 8 < authlen)
  {
    return exit_client(cptr, cptr, cptr, "Not enough session key data");
  }

  cptr->crypt->OutCipher = cdef;
  cptr->crypt->OutState = (void *) malloc(cptr->crypt->OutCipher->state_data_size);
  memset(cptr->crypt->OutState, 0, cptr->crypt->OutCipher->state_data_size);
  cptr->crypt->OutCipher->init(cptr->crypt->OutState, key);

  if (crypt_rsa_encode(cptr->crypt->RSAKey,
                       key, tmp, authlen) != CRYPT_ENCRYPTED)
  {
    return exit_client(cptr, cptr, cptr,
                       "Couldn't generate session key authentication");
  }
  sendto_one(cptr, "CRYPTLINK AUTH %s %s",
             cptr->crypt->OutCipher->name, tmp);

  send_queued(cptr);
  cptr->crypt->flags |= CRYPTFLAG_ENCRYPT;
  return(1);
}





static int bogus_host(char *host)
{
  int bogus_server = 0;
  int dots = 0;
  char *s;

  for (s = host; *s; s++)
  {
    if (!IsServChar(*s))
    {
      bogus_server = 1;
      break;
    }
    if ('.' == *s)
    {
      ++dots;
    }
  }

  if ( (!dots) || (bogus_server) )
    return 1;

  return 0;
}

#endif /* CRYPT_LINKS */
