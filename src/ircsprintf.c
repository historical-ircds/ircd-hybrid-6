/*
 * ircsprintf.c
 *
 *
 * $Id$
 */
#include <stdio.h>
#include "struct.h"
#include "sys.h"
#include "send.h"

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#else
#include <varargs.h>
#endif /* HAVE_STDARG_H */


#ifdef HAVE_STDARG_H

void
ircsprintf(char *outp, char *formp, ...)

#else

void
ircsprintf(outp, formp, va_alist)

char *outp, *formp;
va_dcl

#endif /* HAVE_STDARG_H */

{
	va_list args;
	char *inp[11]; /* we could hack this if we know the format of the stack */
	/* rp for Reading, wp for Writing, fp for the Format string */
	register char *rp,*fp,*wp;
	register char f;
	register int i=0;

	MyVaStart(args, formp);

	inp[0] = va_arg(args, char *);
	inp[1] = va_arg(args, char *);
	inp[2] = va_arg(args, char *);
	inp[3] = va_arg(args, char *);
	inp[4] = va_arg(args, char *);
	inp[5] = va_arg(args, char *);
	inp[6] = va_arg(args, char *);
	inp[7] = va_arg(args, char *);
	inp[8] = va_arg(args, char *);
	inp[9] = va_arg(args, char *);
	inp[10] = va_arg(args, char *);

  va_end(args);

	fp = formp;
	wp = outp;

	rp = inp[i]; /* start with the first input string */

	/*
	 * just scan the format string and puke out whatever is necessary
	 * along the way...
	 */

	while ((f = *(fp++)))
	{
		if (f != '%')
			*(wp++) = f;
		else
		{
			switch (*(fp++))
			{
				case 's': /* put the most common case at the top */
				{
					if (rp)
					{
						while(*rp)
							*wp++ = *rp++;

						*wp = '\0';
					}
					else
					{
						*wp++ = '{';
						*wp++ = 'n';
						*wp++ = 'u';
						*wp++ = 'l';
						*wp++ = 'l';
						*wp++ = '}';
						*wp++ = '\0';
					}

					/* get the next parameter */
					rp = inp[++i];

					break;
				} /* case 's' */

				case 'c':
				{
					*wp++ = (char) ((long) rp);
					rp = inp[++i];

					break;
				} /* case 'c' */

				case 'd':
				{
					register long myint;
					myint = (long)rp;

					if (myint < 100 || myint > 999)
					{
						sprintf(outp, formp,
							inp[0],
							inp[1],
							inp[2],
							inp[3],
							inp[4],
							inp[5],
							inp[6],
							inp[7],
							inp[8],
							inp[9],
							inp[10]);
						return;
					}

				/*
				 * leading 0's are not suppressed unlike format()
				 * -Dianora
				 */

					*(wp++) = (char) ((myint / 100) + (int) '0');
					myint %= 100;
					*(wp++) = (char) ((myint / 10) + (int) '0');
					myint %= 10;
					*(wp++) = (char) ((myint) + (int) '0');

					rp = inp[++i];

					break;
				} /* case 'd' */

				case 'u':
				{
					register unsigned long myuint;
					myuint = (unsigned long)rp;

					if (myuint < 100 || myuint > 999)
					{
						sprintf(outp, formp,
							inp[0],
							inp[1],
							inp[2],
							inp[3],
							inp[4],
							inp[5],
							inp[6],
							inp[7],
							inp[8],
							inp[9],
							inp[10]);
						return;
					}

					*(wp++) = (char) ((myuint / 100) + (unsigned int) '0');
					myuint %= 100;
					*(wp++) = (char) ((myuint / 10) + (unsigned int) '0');
					myuint %= 10;
					*(wp++) = (char) ((myuint) + (unsigned int) '0');

					rp = inp[++i];

					break;
				} /* case 'u' */

				case '%':
				{
					*(wp++) = '%';
					break;
				} /* case '%' */

				default:
				{
					/* oh shit */
					sprintf(outp, formp,
						inp[0],
						inp[1],
						inp[2],
						inp[3],
						inp[4],
						inp[5],
						inp[6],
						inp[7],
						inp[8],
						inp[9],
						inp[10]);
					return;

					break;
				}
			} /* switch (*(fp++)) */
		}
	} /* while ((f = *(fp++))) */

	*wp = '\0';
} /* ircsprintf() */
