#ifndef INCLUDED_defs_h
#define INCLUDED_defs_h

#if 0
/* None of the following should need to be changed by hand.  */
#if !defined(HAVE_DN_SKIPNAME)
#  if defined(HAVE___DN_SKIPNAME)
#    define dn_skipname __dn_skipname
#  else
#    error Could not find dn_skipname() or __dn_skipname()
#  endif
#endif
#endif

/*
 * The following OS specific stuff is a compatibility kludge
 * it would be nice to get rid of all of this eventually.
 */
#if defined(OS_SOLARIS2) && !defined( SOL20 )
#  define SOL20 1
#  define USE_POLL 1	/* Get around stupid select() limitations */
#endif

#if defined( BSD_RELIABLE_SIGNALS ) \
	&& (defined( SYSV_UNRELIABLE_SIGNALS ) || defined( POSIX_SIGNALS ))
error You defined too many signal types in setup.h
#elif defined( SYSV_UNRELIABLE_SIGNALS ) && defined( POSIX_SIGNALS )
error You defined too many signal types in setup.h
#endif

#if defined( BSD_RELIABLE_SIGNALS ) || defined( POSIX_SIGNALS )
#  define HAVE_RELIABLE_SIGNALS
#endif

#endif /* INCLUDED_defs_h */
