/************************************************************************
 *   IRC - Internet Relay Chat, include/s_crypt.h
 *   Copyright (C) 2001 einride <einride@einride.net>
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
 * "s_crypt.h". - Headers file.
 *
 *
 */
#ifndef INCLUDED_s_crypt_h
#define INCLUDED_s_crypt_h

#include "config.h"

#if defined(CRYPT_LINKS) || defined(USE_KSERVER)

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>


#ifdef HAVE_BF_CFB64_ENCRYPT
#define HAVE_CRYPT_BLOWFISH
#include <openssl/blowfish.h>
#endif

#ifdef HAVE_CAST_CFB64_ENCRYPT
#define HAVE_CRYPT_CAST
#include <openssl/cast.h>
#endif

#ifdef HAVE_IDEA_CFB64_ENCRYPT
#define HAVE_CRYPT_IDEA
#include <openssl/idea.h>
#endif

#ifdef HAVE_DES_CFB64_ENCRYPT
#define HAVE_CRYPT_DES
#include <openssl/des.h>
#endif

#ifdef HAVE_DES_EDE3_CFB64_ENCRYPT
#define HAVE_CRYPT_3DES
#include <openssl/des.h>
#endif

#ifdef HAVE_RC5_32_CFB64_ENCRYPT
#define HAVE_CRYPT_RC5
#include <openssl/rc5.h>
#endif

struct Client;
struct ConfItem;

/* Function return values */
#define CRYPT_OK               0  /* Everything fine */
#define CRYPT_BADPARAM         1  /* Something wasn't right :) */
#define CRYPT_ENCRYPTED        2  /* Passed data have been encrypted */
#define CRYPT_DECRYPTED        3  /* Passed data have been decrypted */
#define CRYPT_NOT_ENCRYPTED    4  /* Passed data haven't been altered */
#define CRYPT_NOT_DECRYPTED    5  /* Passed data haven't been altered */
#define CRYPT_ERROR            6

/* State flags */
#define CRYPTFLAG_ENCRYPT      1
#define CRYPTFLAG_DECRYPT      2


#define CRYPT_RSASIZE         256 /* RSA Keys must be exact this size */

typedef int (*cipher_encryptproc) (struct Client *, char *, int);
typedef int (*cipher_initproc)    (void * state, unsigned char * keydata);

struct CipherDef
{
  char *name;           /* Cipher name (eg. BF/256) */
  int keysize;          /* Cipher keysize (eg. BF/256 keysize = 256 */
  int state_data_size;  /* No idea -- hey einride!  :-) */
  cipher_initproc init;
  cipher_encryptproc encrypt;
  cipher_encryptproc decrypt;
};

extern struct CipherDef Ciphers[];

#endif /* CRYPT_LINKS*/
#if defined(CRYPT_LINKS) || defined(USE_KSERVER)
struct CryptData
{
#ifdef CRYPT_LINKS
  struct CipherDef *InCipher;
  void *            InState;
  unsigned char     inkey[64];
  struct CipherDef *OutCipher;
  void *            OutState;
  unsigned char     outkey[64];
  int flags; /* XXX Do we even use these? */
#endif
  RSA *             RSAKey;
};
#endif
#ifdef CRYPT_LINKS


struct CipherDef *crypt_selectcipher(char *);
int crypt_rsa_decode(char *, unsigned char *, int *);
int crypt_rsa_encode(RSA *, unsigned char *, char *, int);

int crypt_init(void);
int crypt_initserver(struct Client *, struct ConfItem *, struct ConfItem *);
int crypt_encrypt(struct Client *, const char *, int);
int crypt_decrypt(struct Client *, const char *, int);
void crypt_free(struct Client *);
int crypt_parse_conf(struct ConfItem *);

#endif /* CRYPT_LINKS */
#endif /* INCLUDED_s_crypt_h */

