#include "limits.h"
#ifndef INCLUDED_motd_h
#define INCLUDED_motd_h

#define MESSAGELINELEN	90

typedef enum {
  USER_MOTD,
  OPER_MOTD,
  HELP_MOTD
}MotdType;

typedef struct MessageFileLineStruct
{
  char	line[MESSAGELINELEN];
  struct MessageFileLineStruct *next;
}MessageFileLine;

typedef struct
{
  char fileName[PATH_MAX+1];
  MotdType motdType;
  MessageFileLine *contentsOfFile;
  char lastChangedDate[MAX_DATE_STRING];
}MessageFile;


struct Client;

void InitMessageFile(MotdType, char *, MessageFile *);
int SendMessageFile(struct Client *, MessageFile *);
int ReadMessageFile(MessageFile *);

#endif /* INCLUDED_motd_h */
