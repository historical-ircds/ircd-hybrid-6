/*
 * motd.h
 *
 * $Id$
 */
#ifndef INCLUDED_motd_h
#define INCLUDED_motd_h

#ifndef INCLUDED_limits_h
#include <limits.h>       /* PATH_MAX */
#define INCLUDED_limits_h
#endif

#define MESSAGELINELEN 89	

typedef enum {
  USER_MOTD,
  OPER_MOTD,
  HELP_MOTD
}MotdType;

typedef struct MessageFileLineStruct
{
  char	line[MESSAGELINELEN + 1];
  struct MessageFileLineStruct *next;
}MessageFileLine;

typedef struct
{
  char fileName[PATH_MAX + 1];
  MotdType motdType;
  MessageFileLine *contentsOfFile;
  char lastChangedDate[MAX_DATE_STRING + 1];
}MessageFile;


struct Client;

void InitMessageFile(MotdType, char *, MessageFile *);
int SendMessageFile(struct Client *, MessageFile *);
int ReadMessageFile(MessageFile *);

#endif /* INCLUDED_motd_h */
