#include <stdio.h>

#ifndef lint
static char *rcs_version = "$Id$";
#endif

#ifndef USE_VARARGS
void ircsprintf(
	       char *outp,
	       char *formp,
	       char *in0p,char *in1p,char *in2p,char *in3p,
	       char *in4p,char *in5p,char *in6p,char *in7p,
	       char *in8p,char *in9p,char *in10p
	       )
{
#else
void ircsprintf(outp, formp, va_alist)
char *outp;
char *formp;
va_dcl
{
  va_list vl;
#endif
  /* rp for Reading, wp for Writing, fp for the Format string */
  char *inp[11]; /* we could hack this if we know the format of the stack */
  register char *rp,*fp,*wp;
  register char f;
  register int i=0;

#ifndef USE_VARARGS
  inp[0]=in0p; inp[1]=in1p; inp[2]=in2p; inp[3]=in3p; inp[4]=in4p; 
  inp[5]=in5p; inp[6]=in6p; inp[7]=in7p; inp[8]=in8p; inp[9]=in9p; 
  inp[10]=in10p; 
#else
  va_start(vl);
  inp[0] = va_arg(vl,char *); inp[1] = va_arg(vl,char *);
  inp[2] = va_arg(vl,char *); inp[3] = va_arg(vl,char *);
  inp[4] = va_arg(vl,char *); inp[5] = va_arg(vl,char *);
  inp[6] = va_arg(vl,char *); inp[7] = va_arg(vl,char *);
  inp[8] = va_arg(vl,char *); inp[9] = va_arg(vl,char *);
  inp[10] = va_arg(vl,char *);
  va_end(vl);
#endif

  fp = formp;
  wp = outp;
  
  rp = inp[i]; /* start with the first input string */

  /* just scan the format string and puke out whatever is necessary
     along the way... */

  while ( (f = *(fp++)) )
    {
    
      if (f!= '%') *(wp++) = f;
      else
	switch (*(fp++))
	  {

	  case 's': /* put the most common case at the top */
	    if(rp)
	      {
		while(*rp)
		  *wp++ = *rp++;
		*wp = '\0';
	      }
	    else
	      {
		*wp++ = '{'; *wp++ = 'n'; *wp++ = 'u'; *wp++ = 'l';
		*wp++ = 'l'; *wp++ = '}'; *wp++ = '\0';
	      }
	    rp = inp[++i];                  /* get the next parameter */
	    break;
	  case 'c':
	    *wp++ = (char)rp;
	    rp = inp[++i];
	    break;
	  case 'd':
	    {
	      register int myint;
	      myint = (int)rp;
	      
	      if (myint < 100 || myint > 999)
		{
		  sprintf(outp,formp,in0p,in1p,in2p,in3p,
			  in4p,in5p,in6p,in7p,in8p,
			  in9p,in10p);
		  return;
		}
	      /* leading 0's are not suppressed unlike format()
		 -Dianora */
	  
	      *(wp++) = (char) ((myint / 100) + (int) '0');
	      myint %=100;
	      *(wp++) = (char) ((myint / 10) + (int) '0');
	      myint %=10;
	      *(wp++) = (char) ((myint) + (int) '0');

	      rp = inp[++i];
	    }
	  break;
	  case 'u':
	    {
	      register unsigned int myuint;
	      myuint = (unsigned int)rp;
	  
	      if (myuint < 100 || myuint > 999)
		{
		  sprintf(outp,formp,in0p,in1p,in2p,in3p,
			  in4p,in5p,in6p,in7p,in8p,
			  in9p,in10p);
		  return;
		}
	  
	      *(wp++) = (char) ((myuint / 100) + (unsigned int) '0');
	      myuint %=100;
	      *(wp++) = (char) ((myuint / 10) + (unsigned int) '0');
	      myuint %=10;
	      *(wp++) = (char) ((myuint) + (unsigned int) '0');
	      
	      rp = inp[++i];
	    }
	  break;
	  case '%':
	    *(wp++) = '%';
	    break;
	  default:
	    /* oh shit */
	    sprintf(outp,formp,in0p,in1p,in2p,in3p,
		    in4p,in5p,in6p,in7p,in8p,
		    in9p,in10p);
	    return;
	    break;
	  }
    }
  *wp = '\0';

  return;
}
