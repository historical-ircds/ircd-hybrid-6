/*
 * $Id$ 
 */

#ifndef SPRINTF_IRC
#define SPRINTF_IRC

#include <stdarg.h>

/*=============================================================================
 * Proto types
 */

extern int vsprintf_irc(char *str, const char *format, va_list);
extern int ircsprintf(char *str, const char *format, ...);

#endif /* SPRINTF_IRC */
