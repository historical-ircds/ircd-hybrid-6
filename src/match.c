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
 */
/*
 * Changes:
 * Thomas Helvey <tomh@inxpress.net> June 23, 1999
 * Const correctness changes
 * Cleanup of collapse and match
 * Moved static calls variable to match
 * Added asserts for null pointers
 */
#ifndef lint
static  char sccsid[] = "%W% %G% (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";

static char *rcs_version = "$Id$";
#endif

#include "match.h"
#include <assert.h>

#define	MAX_CALLS	512  /* ACK! This dies when it's less that this
				and we have long lines to parse */
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

  while (1) {
    if (calls++ > MAX_CALLS)
      return 0;
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
  if (!pattern || !*pattern)
     return pattern;

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


unsigned char tolowertab[] = { 
  0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa,
  0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14,
  0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
  0x1e, 0x1f,
  ' ', '!', '"', '#', '$', '%', '&', 0x27, '(', ')',
  '*', '+', ',', '-', '.', '/',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  ':', ';', '<', '=', '>', '?',
  '@', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
  'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
  't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~',
  '_',
  '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',
  'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's',
  't', 'u', 'v', 'w', 'x', 'y', 'z', '{', '|', '}', '~',
  0x7f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
  0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
  0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
  0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
  0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
  0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
  0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

unsigned char touppertab[] = { 
  0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa,
  0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x13, 0x14,
  0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
  0x1e, 0x1f,
  ' ', '!', '"', '#', '$', '%', '&', 0x27, '(', ')',
  '*', '+', ',', '-', '.', '/',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  ':', ';', '<', '=', '>', '?',
  '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
  'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', '\\', ']', '^',
  0x5f,
  '`', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
  'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
  'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '[', '\\', ']', '^',
  0x7f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
  0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
  0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9,
  0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
  0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9,
  0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9,
  0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff 
};

unsigned char char_atribs[] = {
/* 0-7 */	CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL,
/* 8-12 */	CNTRL, CNTRL|SPACE, CNTRL|SPACE, CNTRL|SPACE, CNTRL|SPACE,
/* 13-15 */	CNTRL|SPACE, CNTRL, CNTRL,
/* 16-23 */	CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL,
/* 24-31 */	CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL, CNTRL,
/* space */	PRINT|SPACE,
/* !"#$%&'( */	PRINT, PRINT, PRINT, PRINT, PRINT, PRINT, PRINT, PRINT,
/* )*+,-./ */	PRINT, PRINT, PRINT, PRINT, PRINT, PRINT, PRINT,
/* 0123 */	PRINT|DIGIT, PRINT|DIGIT, PRINT|DIGIT, PRINT|DIGIT,
/* 4567 */	PRINT|DIGIT, PRINT|DIGIT, PRINT|DIGIT, PRINT|DIGIT,
/* 89:; */	PRINT|DIGIT, PRINT|DIGIT, PRINT, PRINT,
/* <=>? */	PRINT, PRINT, PRINT, PRINT,
/* @ */		PRINT,
/* ABC */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* DEF */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* GHI */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* JKL */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* MNO */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* PQR */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* STU */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* VWX */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* YZ[ */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* \]^ */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* _   */	PRINT,
/* abc */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* def */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* ghi */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* jkl */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* mno */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* pqr */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* stu */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* vwx */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* yz{ */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* \}~ */	PRINT|ALPHA, PRINT|ALPHA, PRINT|ALPHA,
/* del */	0,
/* 80-8f */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 90-9f */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* a0-af */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* b0-bf */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* c0-cf */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* d0-df */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* e0-ef */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* f0-ff */	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
		};

