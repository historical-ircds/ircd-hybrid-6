#include "s_crypt.h"
#include "client.h"
#include "s_conf.h"
#include "send.h"
#include "common.h"
#include "s_serv.h"
#include "ircd.h"
#include "irc_string.h"
#include <string.h>
#include "s_log.h"
#include "s_bsd.h"


#ifdef CRYPT_LINKS

#ifdef HAVE_CRYPT_BLOWFISH
typedef struct
{
  BF_KEY key;
  unsigned char ivec[8];
  int ivecnum;
} BFState;
int do_bf_encrypt(struct Client * cptr, char * Data, int Length);
int do_bf_decrypt(struct Client * cptr, char * Data, int Length);
int do_bf128_init(void * State, unsigned char * keydata);
int do_bf256_init(void * State, unsigned char * keydata);
#endif

#ifdef HAVE_CRYPT_CAST
typedef struct {
  CAST_KEY key;
  unsigned char ivec[8];
  int ivecnum;
} CASTState;
int do_cast_init(void * State, unsigned char * keydata);
int do_cast_encrypt(struct Client * cptr, char * Data, int Length);
int do_cast_decrypt(struct Client * cptr, char * Data, int Length);
#endif

#ifdef HAVE_CRYPT_IDEA
typedef struct {
  IDEA_KEY_SCHEDULE key;
  unsigned char ivec[8];
  int ivecnum;
} IDEAState;
int do_idea_init(void * State, unsigned char * keydata);
int do_idea_encrypt(struct Client * cptr, char * Data, int Length);
int do_idea_decrypt(struct Client * cptr, char * Data, int Length);
#endif

#ifdef HAVE_CRYPT_RC5
typedef struct {
  RC5_32_KEY key;
  unsigned char ivec[8];
  int ivecnum, rounds;
} RC5State;
int do_rc5_8_init(void * State, unsigned char * keydata);
int do_rc5_12_init(void * State, unsigned char * keydata);
int do_rc5_16_init(void * State, unsigned char * keydata);
int do_rc5_encrypt(struct Client * cptr, char * Data, int Length);
int do_rc5_decrypt(struct Client * cptr, char * Data, int Length);
#endif

#ifdef HAVE_CRYPT_3DES
typedef struct {
  des_key_schedule key1, key2, key3;
  des_cblock ivec;
  int ivecnum;
} TDESState;
int do_tdes_init(void * State, unsigned char * keydata);
int do_tdes_encrypt(struct Client * cptr, char * Data, int Length);
int do_tdes_decrypt(struct Client * cptr, char * Data, int Length);
#endif

#ifdef HAVE_CRYPT_DES
typedef struct {
  des_key_schedule key;
  des_cblock ivec;
  int ivecnum;
} DESState;
int do_des_init(void * State, unsigned char * keydata);
int do_des_encrypt(struct Client * cptr, char * Data, int Length);
int do_des_decrypt(struct Client * cptr, char * Data, int Length);
#endif



struct CipherDef Ciphers[] =
{
#ifdef HAVE_CRYPT_DES
  { "DES/56", 56, sizeof(DESState), do_des_init, do_des_encrypt, do_des_decrypt},
#endif
#ifdef HAVE_CRYPT_3DES
  { "3DES/168", 168, sizeof(TDESState), do_tdes_init, do_tdes_encrypt, do_tdes_decrypt},
#endif
#ifdef HAVE_CRYPT_RC5
  { "RC5.8/128", 128, sizeof(RC5State), do_rc5_8_init, do_rc5_encrypt, do_rc5_decrypt},
  { "RC5.12/128", 128, sizeof(RC5State), do_rc5_12_init, do_rc5_encrypt, do_rc5_decrypt},
  { "RC5.16/128", 128, sizeof(RC5State), do_rc5_16_init, do_rc5_encrypt, do_rc5_decrypt},
#endif
#ifdef HAVE_CRYPT_IDEA
  { "IDEA/128", 128, sizeof(IDEAState), do_idea_init, do_idea_encrypt, do_idea_decrypt},
#endif
#ifdef HAVE_CRYPT_CAST
  { "CAST/128", 128, sizeof(CASTState), do_cast_init, do_cast_encrypt, do_cast_decrypt},
#endif
#ifdef HAVE_CRYPT_BLOWFISH
  { "BF/128", 128, sizeof(BFState), do_bf128_init, do_bf_encrypt, do_bf_decrypt},
  { "BF/256", 256, sizeof(BFState), do_bf256_init, do_bf_encrypt, do_bf_decrypt},
#endif
  { "", 0, 0, 0, 0, 0}
};


static char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

static  char base64_values[] =
{
  /* 00-15   */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 16-31   */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 32-47   */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
  /* 48-63   */ 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1,  0, -1, -1,
  /* 64-79   */ -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
  /* 80-95   */ 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
  /* 96-111  */ -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  /* 112-127 */ 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
  /* 128-143 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 144-159 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 160-175 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 186-191 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 192-207 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 208-223 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 224-239 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 240-255 */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/*
 * base64_block will allocate and return a new block of memory
 * using malloc().  It should be freed after use.
 */
int base64_block(char **output, char *data, int len)
{
  unsigned char *out;
  unsigned char *in = (unsigned char*)data;
  unsigned long int q_in;
  int i;
  int count = 0;

  out = (unsigned char *) malloc(((((len + 2) - ((len + 2) % 3)) / 3) * 4) + 1);

  /* process 24 bits at a time */
  for( i = 0; i < len; i += 3)
  {
    q_in = 0;

    if ( i + 2 < len )
    {
      q_in  = (in[i+2] & 0xc0) << 2;
      q_in |=  in[i+2];
    }

    if ( i + 1 < len )
    {
      q_in |= (in[i+1] & 0x0f) << 10;
      q_in |= (in[i+1] & 0xf0) << 12;
    }

    q_in |= (in[i]   & 0x03) << 20;
    q_in |=  in[i]           << 22;

    q_in &= 0x3f3f3f3f;

    out[count++] = base64_chars[((q_in >> 24)       )];
    out[count++] = base64_chars[((q_in >> 16) & 0xff)];
    out[count++] = base64_chars[((q_in >>  8) & 0xff)];
    out[count++] = base64_chars[((q_in      ) & 0xff)];
  }
  if ( (i - len) > 0 )
  {
    out[count-1] = '=';
    if ( (i - len) > 1 )
      out[count-2] = '=';
  }

  out[count] = '\0';
  *output = (char *)out;
  return count;
}

/*
 * unbase64_block will allocate and return a new block of memory
 * using malloc().  It should be freed after use.
 */
int unbase64_block(char **output, char *data, int len)
{
  unsigned char *out;
  unsigned char *in = (unsigned char*)data;
  unsigned long int q_in;
  int i;
  int count = 0;

  if ( ( len % 4 ) != 0 )
    return 0;

  out = (unsigned char *) malloc(((len / 4) * 3) + 1);

  /* process 32 bits at a time */
  for( i = 0; (i + 3) < len; i+=4)
  {
    /* compress input (chars a, b, c and d) as follows:
     * (after converting ascii -> base64 value)
     *
     * |00000000aaaaaabbbbbbccccccdddddd|
     * |  765432  107654  321076  543210|
     */

    q_in = 0;

    if (base64_values[in[i+3]] > -1)
      q_in |= base64_values[in[i+3]]      ;
    if (base64_values[in[i+2]] > -1)
      q_in |= base64_values[in[i+2]] <<  6;
    if (base64_values[in[i+1]] > -1)
      q_in |= base64_values[in[i+1]] << 12;
    if (base64_values[in[i  ]] > -1)
      q_in |= base64_values[in[i  ]] << 18;

    out[count++] = (q_in >> 16) & 0xff;
    out[count++] = (q_in >>  8) & 0xff;
    out[count++] = (q_in      ) & 0xff;
  }

  if (in[i-1] == '=') count--;
  if (in[i-2] == '=') count--;

  out[count] = '\0';
  *output = (char *)out;
  return count;
}



#ifdef HAVE_CRYPT_BLOWFISH
int do_bf128_init(void * StateData, unsigned char * keydata)
{
  BFState * State = (BFState *) StateData;
  memset(State, sizeof(BFState), 0);
  BF_set_key(&State->key, 16, keydata);
  return CRYPT_OK;
}

int do_bf256_init(void * StateData, unsigned char * keydata)
{
  BFState * State = (BFState *) StateData;
  memset(State, sizeof(BFState), 0);
  BF_set_key(&State->key, 32, keydata);
  return CRYPT_OK;
}

int do_bf_encrypt(struct Client * cptr, char * Data, int Length)
{
  BFState * State = (BFState *) cptr->crypt->OutState;
  char * work = malloc(Length);
  BF_cfb64_encrypt(Data, work, Length, &State->key, State->ivec, &State->ivecnum, BF_ENCRYPT);
  memcpy(Data, work, Length);
  free(work);
  return CRYPT_ENCRYPTED;
}

int do_bf_decrypt(struct Client * cptr, char * Data, int Length)
{
  BFState * State = (BFState *) cptr->crypt->InState;
  char * work = malloc(Length);
  BF_cfb64_encrypt(Data, work, Length, &State->key, State->ivec, &State->ivecnum, BF_DECRYPT);
  memcpy(Data, work, Length);
  free(work);
  return CRYPT_DECRYPTED;
}
#endif /* HAVE_CRYPT_BLOWFISH */

#ifdef HAVE_CRYPT_CAST
int do_cast_init(void * State, unsigned char * keydata)
{
  CASTState * S = (CASTState *) State;
  CAST_set_key(&S->key, 16, keydata);
  return CRYPT_OK;
}

int do_cast_encrypt(struct Client * cptr, char * Data, int Length)
{
  CASTState * S = (CASTState *) cptr->crypt->OutState;
  char * work = (char *) malloc(Length);
  CAST_cfb64_encrypt(Data, work, Length, &S->key, S->ivec, &S->ivecnum, CAST_ENCRYPT);
  memcpy(Data, work, Length);
  return CRYPT_ENCRYPTED;
}

int do_cast_decrypt(struct Client * cptr, char * Data, int Length)
{
  CASTState * S = (CASTState *) cptr->crypt->InState;
  char * work = (char *) malloc(Length);
  CAST_cfb64_encrypt(Data, work, Length, &S->key, S->ivec, &S->ivecnum, CAST_DECRYPT);
  memcpy(Data, work, Length);
  return CRYPT_DECRYPTED;
}
#endif /* HAVE_CRYPT_CAST */

#ifdef HAVE_CRYPT_IDEA
int do_idea_init(void * State, unsigned char * keydata)
{
  IDEAState * S = (IDEAState *) State;
  idea_set_encrypt_key(keydata, &S->key);
  /*   idea_set_decrypt_key(&S->ekey, &S->dkey); */
  return CRYPT_OK;
}

int do_idea_encrypt(struct Client * cptr, char * Data, int Length)
{
  IDEAState * S = (IDEAState *) cptr->crypt->OutState;
  char * work = (char *) malloc(Length);
  idea_cfb64_encrypt(Data, work, Length, &S->key, S->ivec, &S->ivecnum, IDEA_ENCRYPT);
  memcpy(Data, work, Length);
  return CRYPT_ENCRYPTED;
}

int do_idea_decrypt(struct Client * cptr, char * Data, int Length)
{
  IDEAState * S = (IDEAState *) cptr->crypt->InState;
  char * work = (char *) malloc(Length);
  idea_cfb64_encrypt(Data, work, Length, &S->key, S->ivec, &S->ivecnum, IDEA_DECRYPT);
  memcpy(Data, work, Length);
  return CRYPT_DECRYPTED;
}
#endif /* HAVE_CRYPT_IDEA */

#ifdef HAVE_CRYPT_DES
int do_des_init(void * State, unsigned char * keydata) 
{
  DESState * S = (DESState *) State;
  des_cblock c;
  memcpy(&c, keydata, sizeof(c));
  des_set_odd_parity(&c);
  if (des_set_key_checked(&c, S->key))
    return CRYPT_ERROR;
  return CRYPT_OK;
}

int do_des_encrypt(struct Client * cptr, char * Data, int Length)
{
  DESState * S = (DESState *) cptr->crypt->OutState;
  char * work = (char *) malloc(Length);
  des_cfb64_encrypt(Data, work, Length, S->key, &S->ivec, &S->ivecnum, DES_ENCRYPT);
  memcpy(Data, work, Length);
  return CRYPT_ENCRYPTED;
}

int do_des_decrypt(struct Client * cptr, char * Data, int Length)
{
  DESState * S = (DESState *) cptr->crypt->InState;
  char * work = (char *) malloc(Length);
  des_cfb64_encrypt(Data, work, Length, S->key, &S->ivec, &S->ivecnum, DES_DECRYPT);
  memcpy(Data, work, Length);
  return CRYPT_DECRYPTED;
}
#endif /* HAVE_CRYPT_DES */

#ifdef HAVE_CRYPT_3DES
int do_tdes_init(void * State, unsigned char * keydata)
{
  TDESState * S = (TDESState *) State;
  des_cblock c1, c2, c3;
  memcpy(&c1, keydata, sizeof(c1));
  memcpy(&c2, keydata + sizeof(c1), sizeof(c1));
  memcpy(&c3, keydata + 2*sizeof(c1), sizeof(c1));

  des_set_odd_parity(&c1);
  des_set_odd_parity(&c2);
  des_set_odd_parity(&c3);
  if (des_set_key_checked(&c1, S->key1))
    return CRYPT_ERROR;
  if (des_set_key_checked(&c2, S->key2))
    return CRYPT_ERROR;
  if (des_set_key_checked(&c3, S->key3))
    return CRYPT_ERROR;
  return CRYPT_OK;
}

int do_tdes_encrypt(struct Client * cptr, char * Data, int Length)
{
  TDESState * S = (TDESState *) cptr->crypt->OutState;
  char * work = (char *) malloc(Length);
  des_ede3_cfb64_encrypt(Data, work, Length, S->key1, S->key2, S->key3, &S->ivec, &S->ivecnum, DES_ENCRYPT);
  memcpy(Data, work, Length);
  return CRYPT_ENCRYPTED;
}

int do_tdes_decrypt(struct Client * cptr, char * Data, int Length)
{
  TDESState * S = (TDESState *) cptr->crypt->InState;
  char * work = (char *) malloc(Length);
  des_ede3_cfb64_encrypt(Data, work, Length, S->key1, S->key2, S->key3, &S->ivec, &S->ivecnum, DES_DECRYPT);
  memcpy(Data, work, Length);
  return CRYPT_DECRYPTED;
}
#endif /* HAVE_CRYPT_3DES */

#ifdef HAVE_CRYPT_RC5
int do_rc5_8_init(void * State, unsigned char * keydata)
{
  RC5State * S = (RC5State *) State;
  RC5_32_set_key(&S->key, 16, keydata, 8);
  return CRYPT_OK;
}

int do_rc5_12_init(void * State, unsigned char * keydata)
{
  RC5State * S = (RC5State *) State;
  RC5_32_set_key(&S->key, 16, keydata, 12);
  return CRYPT_OK;
}

int do_rc5_16_init(void * State, unsigned char * keydata)
{
  RC5State * S = (RC5State *) State;
  RC5_32_set_key(&S->key, 16, keydata, 16);
  return CRYPT_OK;
}

int do_rc5_encrypt(struct Client * cptr, char * Data, int Length)
{
  RC5State * S = (RC5State *) cptr->crypt->OutState;
  char * work = (char *) malloc(Length);
  RC5_32_cfb64_encrypt(Data, work, Length, &S->key, S->ivec, &S->ivecnum, RC5_ENCRYPT);
  memcpy(Data, work, Length);
  return CRYPT_ENCRYPTED;
}

int do_rc5_decrypt(struct Client * cptr, char * Data, int Length)
{
  RC5State * S = (RC5State *) cptr->crypt->InState;
  char * work = (char *) malloc(Length);
  RC5_32_cfb64_encrypt(Data, work, Length, &S->key, S->ivec, &S->ivecnum, RC5_DECRYPT);
  memcpy(Data, work, Length);
  return CRYPT_DECRYPTED;
}
#endif /* HAVE_CRYPT_RC5 */

/*
 * crypt_selectcipher
 *   - Find a matching cipher by it's name, and return a pointer to it's
 *     entry in Ciphers[].
 *   - If not found, returns NULL.
 */
struct CipherDef *crypt_selectcipher(char *ciphername)
{
  struct CipherDef *cdef;

  for (cdef = Ciphers; cdef->name; cdef++)
  {
    if (!irccmp(cdef->name, ciphername))
    {
      return(cdef);
      break;
    }
  }
  return(NULL);
}

int crypt_rsa_decode(char * base64text, unsigned char * data, int * length)
{
  unsigned char *ciphertext=0;
  if (unbase64_block( (char **) &ciphertext, base64text, strlen(base64text))
      != CRYPT_RSASIZE)
    {
      if (ciphertext)
	free(ciphertext);
      return CRYPT_ERROR;
    }
  *length = RSA_private_decrypt(CRYPT_RSASIZE, ciphertext,
				data, me.crypt->RSAKey, RSA_PKCS1_PADDING);
  free(ciphertext);

  if (*length == (-1))
    return CRYPT_ERROR;

  return CRYPT_DECRYPTED;
}

int crypt_rsa_encode(RSA * rsakey, unsigned char * data,
		     char * base64text, int length)
{
  int cipherlen;
  unsigned char ciphertext[CRYPT_RSASIZE+1];
  char * tmp = 0;

  cipherlen = RSA_public_encrypt(length, data, ciphertext, rsakey,
				 RSA_PKCS1_PADDING);
  if (cipherlen == (-1)) 
    return CRYPT_ERROR;

  base64_block(&tmp, ciphertext, cipherlen);
  if (!tmp)
    return CRYPT_ERROR;
  strcpy(base64text, tmp);
  free(tmp);
  return CRYPT_ENCRYPTED;
}

void crypt_free(struct Client * cptr)
{
  if (cptr->crypt)
    {
      if (cptr->crypt->OutState)
	free(cptr->crypt->OutState);
      if (cptr->crypt->InState)
	free(cptr->crypt->InState);
      if (cptr->crypt->RSAKey)
	RSA_free(cptr->crypt->RSAKey);
      free(cptr->crypt);
      cptr->crypt = 0;
    }
  cptr->cipher = NULL;
}

int crypt_initserver(struct Client * cptr,
                     struct ConfItem * cline,
                     struct ConfItem * nline)
{
  FILE * keyfile;
  RSA * rsakey = 0;

  if ( (cptr == NULL) || (cline == NULL) || (nline == NULL) )
    return CRYPT_BADPARAM;

  /* Make sure rsa_public_keyfile and cipher fields aren't NULL. */
  if ( (cline->rsa_public_keyfile == NULL) || (cline->cipher == NULL) ||
       (nline->rsa_public_keyfile == NULL) || (nline->cipher == NULL) )
  {
    log(L_ERROR, "RSA public keyfile or cipher not defined for %s",
         cptr->name);
    return CRYPT_NOT_ENCRYPTED;
  }

  /* Make sure rsa_public_keyfile are the same for both the C and N. */
  if (strcmp(cline->rsa_public_keyfile, nline->rsa_public_keyfile))
  {
    log(L_ERROR, "RSA public keyfile entries are not identical for %s",
         cptr->name);
    return CRYPT_NOT_ENCRYPTED;
  }

  /* Make sure cipher fields are the same for both the C and N. */
  if (cline->cipher != nline->cipher)
  {
    log(L_ERROR, "Cipher types are not identical for %s",
         cptr->name);
    return CRYPT_NOT_ENCRYPTED;
  }

  /* Link should be encrypted, so lets initialize */
  cptr->crypt = (struct CryptData *) malloc(sizeof(struct CryptData));
  memset(cptr->crypt, 0, sizeof(struct CryptData));

  /* Load the servers RSA key */
  keyfile = fopen(nline->rsa_public_keyfile, "r");

  if (keyfile == NULL)
  {
    log(L_ERROR, "Failed loading public key file for %s", cptr->name);
    sendto_realops("Failed loading public key file for %s", cptr->name);
    exit_client(cptr, cptr, cptr, "Failed to initialize RSA");
    return CRYPT_ERROR;
  }

  rsakey = (RSA *) PEM_read_RSA_PUBKEY(keyfile, &rsakey, 0, 0);
  fclose(keyfile);

  if (!rsakey)
  {
    log(L_ERROR, "RSA public keyfiles or ciphers do not match for %s",
         cptr->name);
    sendto_realops("Failed reading public key file for %s", cptr->name);
    exit_client(cptr, cptr, cptr, "Failed to initialize RSA");
    return CRYPT_ERROR;
  }
  cptr->crypt->RSAKey = rsakey;

  /* Seed OpenSSL PRNG with EGD enthropy pool -kre */
#if defined USE_EGD && defined EGD_POOL
  if (RAND_egd(EGD_POOL) == -1)
    return CRYPT_ERROR;
#endif /* USE_EGD */

  /* Generate our inkey */
  if (!RAND_pseudo_bytes(cptr->crypt->inkey, sizeof(cptr->crypt->inkey)))
    return CRYPT_ERROR;

  cptr->cipher = nline->cipher;

  return CRYPT_ENCRYPTED;
}



int crypt_encrypt(struct Client * cptr, const char * Data, int Length)
{
  if (!cptr || !Data)
    return CRYPT_BADPARAM;
  if (!cptr->crypt)
    return CRYPT_NOT_ENCRYPTED;

#ifndef NDEBUG
  log(L_DEBUG, "crypt_encrypt %i bytes to %s: ENCRYPT flag is %s, Cipher is %sinitialized",
      Length, cptr->name, cptr->crypt->flags & CRYPTFLAG_ENCRYPT ? "set" : "not set",
      cptr->crypt->OutCipher && cptr->crypt->OutState ? "" : "not ");
#endif

  if (cptr->crypt->flags & CRYPTFLAG_ENCRYPT)
    {
      if (Length)
	{
	  return cptr->crypt->OutCipher->encrypt(cptr, (char *) Data, Length);
	}
      else
	{
	  return CRYPT_ENCRYPTED;
	}
    }
  else
    {
      return CRYPT_NOT_ENCRYPTED;
    }
}



int crypt_decrypt(struct Client * cptr, const char * Data, int Length)
{
  if (!cptr || !Data)
    return CRYPT_BADPARAM;

  if (!cptr->crypt)
    return CRYPT_NOT_DECRYPTED;

#ifndef NDEBUG
  log(L_DEBUG, "crypt_decrypt %i bytes from %s: DECRYPT flag is %s, Cipher is %sinitialized",
      Length, cptr->name, cptr->crypt->flags & CRYPTFLAG_DECRYPT ? "set" : "not set",
      cptr->crypt->InCipher && cptr->crypt->InState ? "" : "not ");
#endif

  if (cptr->crypt->flags & CRYPTFLAG_DECRYPT)
    {
      if (Length)
	{
	  return cptr->crypt->InCipher->decrypt(cptr, (char *) Data, Length);
	}
      else
	{
	  return CRYPT_DECRYPTED;
	}
    }
  else
    {
      return CRYPT_NOT_DECRYPTED;
    }
}

int crypt_init()
{
  RSA * rsakey = 0;
  FILE * keyfile;
#ifndef CRYPT_LINKS_PRIVATEKEYFILE
  char privkeyname[BUFSIZE+1];

  ircsprintf(privkeyname, "%s.key", me.name);
  keyfile = fopen(privkeyname, "r");
#else /* !CRYPT_LINKS_PRIVATEKEYFILE */
  keyfile = fopen(CRYPT_LINKS_PRIVATEKEYFILE, "r");
#endif /* !CRYPT_LINKS_PRIVATEKEYFILE */

  if (!keyfile)
    {
#ifndef CRYPT_LINKS_PRIVATEKEYFILE
      log(L_CRIT, "Failed to open private key file %s", privkeyname);
#else /* !CRYPT_LINKS_PRIVATEKEYFILE */
      log(L_CRIT, "Failed to open private key file %s", CRYPT_LINKS_PRIVATEKEYFILE);
#endif /* !CRYPT_LINKS_PRIVATEKEYFILE */
      return CRYPT_ERROR;
    }
  rsakey = PEM_read_RSAPrivateKey(keyfile, &rsakey, 0, 0);
  fclose(keyfile);
  if (!rsakey)
    {
#ifndef CRYPT_LINKS_PRIVATEKEYFILE
      log(L_CRIT, "Failed to load private key file %s", privkeyname);
#else /* !CRYPT_LINKS_PRIVATEKEYFILE */
      log(L_CRIT, "Failed to load private key file %s", CRYPT_LINKS_PRIVATEKEYFILE);
#endif /* !CRYPT_LINKS_PRIVATEKEYFILE */
      return CRYPT_ERROR;
    }
  crypt_free(&me);
  me.crypt = (struct CryptData *) malloc(sizeof(struct CryptData));
  memset(me.crypt, 0, sizeof(struct CryptData));
  me.crypt->RSAKey = rsakey;
  return CRYPT_OK;
}


/*
 * crypt_parse_conf()
 *   - Parses the passwd field of a conf structure, and assigns the
 *     rsa_pub_keyfile and cipher elements to the proper data.
 *   - Returns -1 on error, 0 on success.
 */
int crypt_parse_conf(struct ConfItem *aconf)
{
  FILE *fp = NULL;
  char *filename_str;
  char *cipher_str;
  struct CipherDef *cdef = NULL;

  /* Make sure the passwd field is at least used */
  if (aconf->passwd == NULL)
  {
    log(L_ERROR, "passwd field undefined/unused for %s", aconf->name);
    return(-1);
  }

  /* Make sure passwd[0] contains the CRYPT_LINKS_CNPREFIX character */
  if (aconf->passwd[0] != CRYPT_LINKS_CNPREFIX)
  {
    log(L_ERROR, "passwd field lacks CNPREFIX character %c for %s",
        CRYPT_LINKS_CNPREFIX, aconf->name);
    return(-1);
  }

  /* Assign the filename.  Use +1 to skip the CNPREFIX character. */
  DupString(filename_str, aconf->passwd + 1);

  /* Now to get the cipher. Find the last occurance of CIPHERPREFIX and
   * go forward from there.
   */
  cipher_str = strrchr(filename_str, CRYPT_LINKS_CIPHERPREFIX);

  if (cipher_str == NULL)
  {
    /* We couldn't find the CIPHERPREFIX character.  Someone forgot to
     * specify the dang thing in the conf.
     */
    MyFree(filename_str);
    log(L_ERROR, "Cipher not specified for %s", aconf->host);
    sendto_realops("Cipher not specified for %s", aconf->host);
    return(-1);
  }

  /* The null assignment zeros out the CIPHERPREFIX character... */
  *cipher_str = '\0';
  cipher_str++;

  cdef = crypt_selectcipher(cipher_str);

  if (cdef == NULL)
  {
    MyFree(filename_str);
    log(L_ERROR, "Bad cipher '%s' selected selected for %s",
        cipher_str, aconf->host);
    sendto_realops("Bad cipher '%s' selected for %s",
        cipher_str, aconf->host);
    return(-1);
  }

  /* Check to see if the RSA public key file in question is legit */
  fp = fopen(filename_str, "r");

  if (fp == NULL)
  {
    MyFree(filename_str);
    log(L_ERROR, "Failed loading public key file for %s", aconf->host);
    sendto_realops("Failed loading public key file for %s", aconf->host);
    return(-1);
  }
  fclose(fp);

  /* Success.  Assign the new conf values, and let's go! */
  DupString(aconf->rsa_public_keyfile, filename_str);
  aconf->cipher = cdef;

  MyFree(filename_str);

  return(0);
}

#endif /* CRYPT_LINKS */

