/************************************************************************
 *   IRC - Internet Relay Chat, src/match.c
 *   Copyright (C) 1990 Jarkko Oikarinen
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
 * Changes:
 * Thomas Helvey <tomh@inxpress.net> June 23, 1999
 * Const correctness changes
 * Cleanup of collapse and match
 * Moved static calls variable to match
 * Added asserts for null pointers
 * $Id$
 *
 */
#include "match.h"
#include <assert.h>

/*
**  Compare if a given string (name) matches the given
**  mask (which can contain wild cards: '*' - match any
**  number of chars, '?' - match any single character.
**
**	return	1, if match
**		0, if no match
*/
/*
** match()
** Iterative matching function, rather than recursive.
** Written by Douglas A Lewis (dalewis@acsu.buffalo.edu)
*/
/* behavior change - (Thomas Helvey <tomh@inxpress.net>)
 * removed escape handling, none of the masks used with this
 * function should contain an escape '\\' unless you are searching
 * for one, it is no longer possible to escape * and ?. 
 * Moved calls rollup to function body, since match isn't recursive
 * there isn't any reason to have it exposed to the file, this change
 * also has the added benefit of making match reentrant. :)
 * Added asserts, mask and name cannot be null.
 * Changed ma and na to unsigned to get rid of casting.
 *
 * NOTICE: match is now a boolean operation, not a lexical comparison
 * if a line matches a mask, true (1) is returned, otherwise false (0)
 * is returned.
 */
#define	MATCH_MAX_CALLS	512  /* ACK! This dies when it's less that this
				and we have long lines to parse */
int match(const char *mask, const char *name)
{
  const unsigned char* m = (const unsigned char*)  mask;
  const unsigned char* n = (const unsigned char*)  name;
  const unsigned char* ma = (const unsigned char*) mask;
  const unsigned char* na = (const unsigned char*) name;
  int   wild  = 0;
  int   calls = 0;
  assert(0 != mask);
  assert(0 != name);

  while (calls++ < MATCH_MAX_CALLS) {
    if (*m == '*') {
      /*
       * XXX - shouldn't need to spin here, the mask should have been
       * collapsed before match is called
       */
      while (*m == '*')
        m++;
      wild = 1;
      ma = m;
      na = n;
    }

    if (!*m) {
      if (!*n)
        return 1;
      for (m--; (m > (const unsigned char*) mask) && (*m == '?'); m--)
        ;
      if ((*m == '*') && (m > (const unsigned char*) mask))
        return 1;
      if (!wild)
        return 0;
      m = ma;
      n = ++na;
    }
    else if (!*n) {
      /*
       * XXX - shouldn't need to spin here, the mask should have been
       * collapsed before match is called
       */
      while (*m == '*')
        m++;
      return (*m == 0);
    }
    if (tolower(*m) != tolower(*n) && *m != '?') {
      if (!wild)
        return 0;
      m = ma;
      n = ++na;
    }
    else {
      if (*m)
        m++;
      if (*n)
        n++;
    }
  }
  return 0;
}


/*
** collapse a pattern string into minimal components.
** This particular version is "in place", so that it changes the pattern
** which is to be reduced to a "minimal" size.
*/
/* collapse - behavior modification (Thomas Helvey <tomh@inxpress.net>)
 * Removed mask escapes, we don't escape wildcards or call match
 * on a mask. This change is somewhat subtle, the old version converted
 * \\*** to \\**, the new version blindly converts it to \\*.
 * Removed code that did a lot of work but achieved nothing, testing
 * showed that the code in test for '?' produced exactly the same results
 * as code that ignored '?'. The only thing you can do with a mask is to
 * remove adjacent '*' characters, attempting anything else breaks the re.
 *
 * collapse - convert adjacent *'s to a single *
 */
char* collapse(char *pattern)
{
  char* s = pattern;
  char* s1;
  char* t;

  /*
   * XXX - null pointers ok?
   */
  if (s) {
    for (; *s; s++) {
      if ('*' == *s) {
	t = s1 = s + 1;
	while ('*' == *t)
	  ++t;
	if (s1 != t) {
	  while ((*s1++ = *t++))
	    ;
	}
      }
    }
  }
  return pattern;
}

/*
**  Case insensitive comparison of two NULL terminated strings.
**
**	returns	 0, if s1 equal to s2
**		<0, if s1 lexicographically less than s2
**		>0, if s1 lexicographically greater than s2
*/
int irccmp(const char *s1, const char *s2)
{
  const unsigned char* str1 = (const unsigned char*) s1;
  const unsigned char* str2 = (const unsigned char*) s2;
  int	res;
  assert(0 != s1);
  assert(0 != s2);

  while ((res = toupper(*str1) - toupper(*str2)) == 0) {
    if (*str1 == '\0')
      return 0;
    str1++;
    str2++;
  }
  return (res);
}

int ircncmp(const char* s1, const char *s2, int n)
{
  const unsigned char* str1 = (const unsigned char*) s1;
  const unsigned char* str2 = (const unsigned char*) s2;
  int res;
  assert(0 != s1);
  assert(0 != s2);

  while ((res = toupper(*str1) - toupper(*str2)) == 0) {
    str1++; str2++; n--;
    if (n == 0 || (*str1 == '\0' && *str2 == '\0'))
      return 0;
  }
  return (res);
}
