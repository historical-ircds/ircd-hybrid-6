#include <stdio.h>

main()
{
  char buffera[81];
  char bufferb[81];
  char bufferc[81];
  char output[256];

  FILE *inputfp;

  int  count=1;

  inputfp=fopen("defs","r");
  fgets(buffera, 80, inputfp);
  fgets(bufferc, 80, inputfp);
       
  while (!feof(inputfp))
  {
    buffera[strlen(buffera)-1]='\0';
    printf("#ifdef %s\n",buffera);
    printf("\tstrcpy(outstr,\"%s=1\");\n",buffera);
    printf("#else\n");
    printf("\tstrcpy(outstr,\"%s=0\");\n",buffera);
    printf("#endif\n");

    fgets(bufferb, 80, inputfp);
   if (!feof(inputfp)){
    bufferb[strlen(bufferb)-1]='\0';
    printf("#ifdef %s\n",bufferb);
    printf("\tstrcat(outstr,\" %s=1\");\n",bufferb);
    printf("#else\n");
    printf("\tstrcat(outstr,\" %s=0\");\n",bufferb);
    printf("#endif\n");
   }

    fgets(bufferc, 80, inputfp);
   if (!feof(inputfp)){
    bufferc[strlen(bufferc)-1]='\0';
    printf("#ifdef %s\n",bufferc);
    printf("\tstrcat(outstr,\" %s=1\");\n",bufferc);
    printf("#else\n");
    printf("\tstrcat(outstr,\" %s=0\");\n",bufferc);
    printf("#endif\n");
   }

    printf("\tsendto_one(sptr, rpl_str(RPL_INFO),\n\t\tme.name, parv[0], outstr);\n");
    fgets(buffera, 80, inputfp);
  }
  fclose(inputfp);

  inputfp=fopen("valdefs","r");
  fgets(buffera, 80, inputfp);
      
  while (!feof(inputfp))
  {
    buffera[strlen(buffera)-1]='\0';
    printf("\tircsprintf(outstr,\"%s=%%d\",%s);\n",buffera,buffera);

    fgets(bufferb, 80, inputfp);
   if (!feof(inputfp)){
    bufferb[strlen(bufferb)-1]='\0';
    printf("\tircsprintf(tmpstr,\" %s=%%d\",%s);\n",bufferb,bufferb);
    printf("\tstrcat(outstr,tmpstr);\n");
   }

    fgets(bufferc, 80, inputfp);
   if (!feof(inputfp)){
    bufferc[strlen(bufferc)-1]='\0';
    printf("\tircsprintf(tmpstr,\" %s=%%d\",%s);\n",bufferc,bufferc);
    printf("\tstrcat(outstr,tmpstr);\n");
   }

    printf("\tsendto_one(sptr, rpl_str(RPL_INFO),\n\t\tme.name, parv[0], outstr);\n");
    fgets(buffera, 80, inputfp);
  }
  fclose(inputfp);

}

