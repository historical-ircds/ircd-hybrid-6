#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "../include/setup.h"
#ifdef HAVE_LIBCRYPTO

#include <openssl/ssl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>

#ifdef HAVE_BF_CFB64_ENCRYPT
#include <openssl/blowfish.h>
#endif

#endif

#define BLOCKSIZE  1000
#define BLOCKCOUNT 20000
#define TICKINT 1000

struct timeval tvstart, tvstop;

void starttimer() {
  gettimeofday(&tvstart, 0);
}

double stoptimer() {
  gettimeofday(&tvstop, 0);
  return (tvstop.tv_sec - tvstart.tv_sec) + ( (tvstop.tv_usec - tvstart.tv_usec) / 1000000.0);  
}

#define DATASIZE ( (double) (BLOCKSIZE * BLOCKCOUNT) / (1024.0*1024.0))

void bf128_test() {
#ifdef HAVE_BF_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];
  BF_KEY key;
  unsigned char keydata[16], ivec[8];
  int ivecnum = 0, i;
  double elapsed;
  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  RAND_pseudo_bytes(keydata, 16);
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  BF_set_key(&key, 16, keydata);
  printf("Blowfish 128-bit encryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    BF_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, BF_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);

  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  ivecnum = 0;
  RAND_pseudo_bytes(keydata, 16);
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  BF_set_key(&key, 16, keydata);
  printf("Blowfish 128-bit decryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    BF_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, BF_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#else
  printf("Blowfish not supported by crypto library\n");
#endif
}

void bf256_test() {
#ifdef HAVE_BF_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];
  BF_KEY key;
  unsigned char keydata[16], ivec[8];
  int ivecnum = 0, i;
  double elapsed;
  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  RAND_pseudo_bytes(keydata, 32);
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  BF_set_key(&key, 32, keydata);
  printf("Blowfish 256-bit encryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    BF_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, BF_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);

  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  ivecnum = 0;
  RAND_pseudo_bytes(keydata, 32);
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  BF_set_key(&key, 32, keydata);
  printf("Blowfish 256-bit decryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    BF_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, BF_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#endif
}


void tdes_test() {
#ifdef HAVE_DES_EDE3_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];
  des_cblock k1, k2, k3, ivec;
  des_key_schedule s1, s2, s3;
  double elapsed;
  int i, ivecnum;

  printf("Triple DES encryption:");
  fflush(stdout);
  memset(&ivec, 0, sizeof(ivec));
  ivecnum = 0;
  RAND_pseudo_bytes(k1, sizeof(k1));
  RAND_pseudo_bytes(k2, sizeof(k2));
  RAND_pseudo_bytes(k3, sizeof(k3));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  des_set_odd_parity(&k1);
  des_set_odd_parity(&k2);
  des_set_odd_parity(&k3);
  if (des_set_key_checked(&k1, s1)<0) {
    printf("key 1 setup failed\n");
    return;
  }
  if (des_set_key_checked(&k2, s2)<0) {
    printf("key 2 setup failed\n");
    return;
  }
  if (des_set_key_checked(&k3, s3)<0) {
    printf("key 3 setup failed\n");
    return;
  }

  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    des_ede3_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, s1, s2, s3, &ivec, &ivecnum, DES_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);


  printf("Triple DES decryption:");
  fflush(stdout);
  memset(&ivec, 0, sizeof(ivec));
  ivecnum = 0;
  RAND_pseudo_bytes(k1, sizeof(k1));
  RAND_pseudo_bytes(k2, sizeof(k2));
  RAND_pseudo_bytes(k3, sizeof(k3));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  des_set_odd_parity(&k1);
  des_set_odd_parity(&k2);
  des_set_odd_parity(&k3);
  if (des_set_key_checked(&k1, s1)<0) {
    printf("key 1 setup failed\n");
    return;
  }
  if (des_set_key_checked(&k2, s2)<0) {
    printf("key 2 setup failed\n");
    return;
  }
  if (des_set_key_checked(&k3, s3)<0) {
    printf("key 3 setup failed\n");
    return;
  }

  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    des_ede3_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, s1, s2, s3, &ivec, &ivecnum, DES_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#else
  printf("Triple DES cfb64 mode not supported by crypto library\n");
#endif
}

void des_test() {
#ifdef HAVE_DES_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];
  des_cblock k1, ivec;
  des_key_schedule s1;
  double elapsed;
  int i, ivecnum;

  printf("DES encryption:");
  fflush(stdout);
  memset(&ivec, 0, sizeof(ivec));
  ivecnum = 0;
  RAND_pseudo_bytes(k1, sizeof(k1));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  des_set_odd_parity(&k1);
  if (des_set_key_checked(&k1, s1)<0) {
    printf("key 1 setup failed\n");
    return;
  }

  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    des_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, s1, &ivec, &ivecnum, DES_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);


  printf("DES decryption:");
  fflush(stdout);
  memset(&ivec, 0, sizeof(ivec));
  ivecnum = 0;
  RAND_pseudo_bytes(k1, sizeof(k1));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  des_set_odd_parity(&k1);
  if (des_set_key_checked(&k1, s1)<0) {
    printf("key 1 setup failed\n");
    return;
  }

  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    des_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, s1, &ivec, &ivecnum, DES_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#else
  printf("DES cfb64 mode not supported by crypto library\n");
#endif
}

void cast_test() {
#ifdef HAVE_CAST_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];
  CAST_KEY key;
  unsigned char keydata[CAST_KEY_LENGTH], ivec[8];
  int ivecnum = 0, i;
  double elapsed;
  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  RAND_pseudo_bytes(keydata, sizeof(keydata));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  CAST_set_key(&key, sizeof(keydata), keydata);
  printf("CAST encryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    CAST_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, CAST_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);

  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  ivecnum = 0;
  RAND_pseudo_bytes(keydata, sizeof(keydata));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  CAST_set_key(&key, sizeof(keydata), keydata);
  printf("CAST decryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    CAST_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, CAST_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#else
  printf("CAST cfb64 mode not supported by crypto library\n");
#endif
}

void idea_test() {
#ifdef HAVE_IDEA_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];

  IDEA_KEY_SCHEDULE key;
  unsigned char keydata[IDEA_KEY_LENGTH], ivec[8];
  int ivecnum = 0, i;
  double elapsed;
  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  RAND_pseudo_bytes(keydata, sizeof(keydata));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  idea_set_encrypt_key(keydata, &key);
  printf("IDEA encryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    idea_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, IDEA_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);

  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  ivecnum = 0;
  RAND_pseudo_bytes(keydata, sizeof(keydata));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  idea_set_encrypt_key(keydata, &key);
  printf("IDEA decryption:");
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    idea_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, IDEA_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#else
  printf("IDEA cfb64 mode not supported by crypto library\n");
#endif
}

void rc5_test(int rounds) {
#ifdef HAVE_RC5_32_CFB64_ENCRYPT
  char ciphertext[BLOCKSIZE];
  char plaintext[BLOCKSIZE];
  RC5_32_KEY key;
  unsigned char keydata[RC5_32_KEY_LENGTH], ivec[8];
  int ivecnum = 0, i;
  double elapsed;
  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  RAND_pseudo_bytes(keydata, sizeof(keydata));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  RC5_32_set_key(&key, sizeof(keydata), keydata, rounds);
  printf("RC5 %i round encryption:", rounds);
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    RC5_32_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, RC5_ENCRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);

  memset(&ivec, 0, sizeof(ivec));
  memset(&key, 0, sizeof(key));
  ivecnum = 0;
  RAND_pseudo_bytes(keydata, sizeof(keydata));
  RAND_pseudo_bytes(ciphertext, BLOCKSIZE);
  RC5_32_set_key(&key, sizeof(keydata), keydata, rounds);
  printf("RC5 %i round decryption:", rounds);
  fflush(stdout);
  starttimer();
  for (i=1;i<=BLOCKCOUNT;i++) {
    if (!(i % TICKINT)) {
      printf(".");
      fflush(stdout);
    }
    RC5_32_cfb64_encrypt(ciphertext, plaintext, BLOCKSIZE, &key, ivec, &ivecnum, RC5_DECRYPT);
  }
  elapsed=stoptimer();
  printf("done, %f MB/sec\n", DATASIZE / elapsed);
#else
  printf("RC5 32 cfb64 mode not supported by crypto library\n");
#endif
}
 
int main(int argc, char * argv[]) {
  printf("Encryption speed test\n");
  bf128_test();
  bf256_test();
  tdes_test();
  des_test();
  cast_test();
  idea_test();
  rc5_test(8);
#ifdef HAVE_RC5_32_CFB64_ENCRYPT
  rc5_test(12);
  rc5_test(16);
#endif
  return 0;
}

