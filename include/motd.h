#include "limits.h"
#ifndef INCLUDED_motd_h
#define INCLUDED_motd_h

#define MESSAGELINELEN	90

typedef struct MessageFileLineStruct
{
  char	line[MESSAGELINELEN];
  struct MessageFileLineStruct *next;
}MessageFileLine;

typedef struct
{
  char fileName[PATH_MAX+1];
  int motdType;
  MessageFileLine *contentsOfFile;
  char lastChangedDate[MAX_DATE_STRING];
}MessageFile;

#define  USER_MOTD 0
#define  OPER_MOTD 1
#define  HELP_MOTD 2

struct Client;

void InitMessageFile(int, char *, MessageFile *);
int SendMessageFile(struct Client *, MessageFile *);
int ReadMessageFile(MessageFile *);

#endif /* INCLUDED_motd_h */
