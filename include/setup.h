/* include/setup.h.  Generated automatically by configure.  */
/* include/setup.h.in.  Generated automatically from configure.in by autoheader.  */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define to <malloc.h> if you need it */
#if !defined(STDC_HEADERS)
/* #undef MALLOCH */
#endif

/* Chose only one of POSIX, BSD, or SYSV signals */
/* Define if you have reliable posix signals */
#define POSIX_SIGNALS 1

/* Define if you have reliable BSD signals */
/* #undef BSD_RELIABLE_SIGNALS */

/* Define if you have unreliable SYSV signals */
/* #undef SYSV_UNRELIABLE_SIGNALS */

/* Chose only one of NBLOCK_POSIX, NBLOCK_BSD, and NBLOCK_SYSV */
/* Define if you have posix non-blocking sockets (O_NONBLOCK) */
#define NBLOCK_POSIX 1

/* Define if you have BSD non-blocking sockets (O_NDELAY) */
/* #undef NBLOCK_BSD */

/* Define if you have SYSV non-blocking sockets (FIONBIO) */
/* #undef NBLOCK_SYSV */

/* Define if you have the getrusage function.  */
/* #undef HAVE_GETRUSAGE */

/* Define if you have the gettimeofday function.  */
#define HAVE_GETTIMEOFDAY 1

/* Define if you have the index function.  */
#define HAVE_INDEX 1

/* Define if you have the inet_aton function.  */
#define HAVE_INET_ATON 1

/* Define if you have the inet_addr function.  */
#define HAVE_INET_ADDR 1

/* Define if you have the inet_netof function.  */
#define HAVE_INET_NETOF 1

/* Define if you have the inet_ntoa function.  */
#define HAVE_INET_NTOA 1

/* Define if you have the lrand48 function.  */
#define HAVE_LRAND48 1

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strtok function.  */
#define HAVE_STRTOK 1

/* Define if you have the strtoken function.  */
/* #undef HAVE_STRTOKEN */

/* Define if you have the dn_skipname function.  */
/* #undef HAVE_DN_SKIPNAME */

/* Define if you have the __dn_skipname function.  */
#define HAVE___DN_SKIPNAME 1

/* Define if you have the times function.  */
/* #undef HAVE_TIMES */

/* Define if you have the bcopy function.  */
#define HAVE_BCOPY 1

/* Define if you have the bcmp function.  */
#define HAVE_BCMP 1

/* Define if you have the bzero function.  */
#define HAVE_BZERO 1

/* Define if you have the <param.h> header file.  */
#define HAVE_SYS_PARAM_H 1

/* Define if you have MIN and MAX in sys/param.h */
#if defined( HAVE_SYS_PARAM_H )
#define HAVE_MINMAX 1
#endif

/* Define if you have the <stddef.h> header file.  */
#define HAVE_STDDEF_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <strings.h> header file.  */
#define HAVE_STRINGS_H 1

/* Define if you have the <sys/syslog.h> header file.  */
#define HAVE_SYS_SYSLOG_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Obviously only define one of these.  */
/* Define for Solaris 2.x.  */
/* #undef OS_SOLARIS2 */

/* Define for AIX.  */
/* #undef OS_AIX */

/* Define for a MIPS machine.  */
/* #undef OS_MIPS */

/* Define for Sequent Dynix/PTX.  */
/* #undef OS_DYNIXPTX */

/* Define for ESIX.  */
/* #undef OS_ESIX */

/* Define for a NeXT OS.  */
/* #undef OS_NEXT */

/* Define if using HPUX.  */
/* #undef OS_HPUX */
