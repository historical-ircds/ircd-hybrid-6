/************************************************************************
 *   IRC - Internet Relay Chat, src/match.h
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
#ifndef INCLUDED_match_h
#define INCLUDED_match_h

/*
 * match - compare name with mask, mask may contain * and ? as wildcards
 * match - returns 1 on successful match, 0 otherwise
 */
extern int match(const char *mask, const char *name);
/*
 * collapse - collapse a string in place, converts multiple adjacent *'s 
 * into a single *.
 * collapse - modifies the contents of pattern 
 */
extern char* collapse(char *pattern);
/*
 * mycmp - case insensitive comparison of s1 and s2
 */
extern int irccmp(const char *s1, const char *s2);
/*
 * myncmp - counted case insensitive comparison of s1 and s2
 */
extern int ircncmp(const char *s1, const char *s2, int n);


/*
 * character macros
 */
extern unsigned char tolowertab[];
#undef tolower
#define tolower(c) (tolowertab[(unsigned char)(c)])

extern unsigned char touppertab[];

#undef toupper
#define toupper(c) (touppertab[(unsigned char)(c)])

#undef isalpha
#undef isdigit
#undef isxdigit
#undef isalnum
#undef isprint
#undef isascii
#undef isgraph
#undef ispunct
#undef islower
#undef isupper
#undef isspace
#undef iscntrl
extern unsigned char char_atribs[];

#define PRINT 1
#define CNTRL 2
#define ALPHA 4
#define PUNCT 8
#define DIGIT 16
#define SPACE 32
#define iscntrl(c) (char_atribs[(unsigned char)(c)]&CNTRL)
#define isalpha(c) (char_atribs[(unsigned char)(c)]&ALPHA)
#define isspace(c) (char_atribs[(unsigned char)(c)]&SPACE)
#define islower(c) ((char_atribs[(unsigned char)(c)]&ALPHA) && ((unsigned char)(c) > 0x5f))
#define isupper(c) ((char_atribs[(unsigned char)(c)]&ALPHA) && ((unsigned char)(c) < 0x60))
#define isdigit(c) (char_atribs[(unsigned char)(c)]&DIGIT)
#define isxdigit(c) (isdigit(c) || 'a' <= (c) && (c) <= 'f' || \
        'A' <= (c) && (c) <= 'F')
#define isalnum(c) (char_atribs[(unsigned char)(c)]&(DIGIT|ALPHA))
#define isprint(c) (char_atribs[(unsigned char)(c)]&PRINT)
#define isascii(c) ((unsigned char)(c) >= 0 && (unsigned char)(c) <= 0x7f)
#define isgraph(c) ((char_atribs[(unsigned char)(c)]&PRINT) && ((unsigned char)(c) != 0x32))
#define ispunct(c) (!(char_atribs[(unsigned char)(c)]&(CNTRL|ALPHA|DIGIT)))


#endif /* INCLUDED_match_h */
