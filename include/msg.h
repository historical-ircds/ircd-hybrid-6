/************************************************************************
 *   IRC - Internet Relay Chat, include/msg.h
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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
 * $Id$
 */

#ifndef	__msg_include__
#define __msg_include__

#define MSG_PRIVATE  "PRIVMSG"	/* PRIV */
#define MSG_WHO      "WHO"	/* WHO  -> WHOC */
#define MSG_WHOIS    "WHOIS"	/* WHOI */
#define MSG_WHOWAS   "WHOWAS"	/* WHOW */
#define MSG_USER     "USER"	/* USER */
#define MSG_NICK     "NICK"	/* NICK */
#define MSG_SERVER   "SERVER"	/* SERV */
#define MSG_LIST     "LIST"	/* LIST */
#define MSG_TOPIC    "TOPIC"	/* TOPI */
#define MSG_INVITE   "INVITE"	/* INVI */
#define MSG_VERSION  "VERSION"	/* VERS */
#define MSG_QUIT     "QUIT"	/* QUIT */
#define MSG_SQUIT    "SQUIT"	/* SQUI */
#define MSG_KILL     "KILL"	/* KILL */
#define MSG_INFO     "INFO"	/* INFO */
#define MSG_LINKS    "LINKS"	/* LINK */
#define MSG_STATS    "STATS"	/* STAT */
#define MSG_USERS    "USERS"	/* USER -> USRS */
#define MSG_HELP     "HELP"	/* HELP */
#define MSG_ERROR    "ERROR"	/* ERRO */
#define MSG_AWAY     "AWAY"	/* AWAY */
#define MSG_CONNECT  "CONNECT"	/* CONN */
#define MSG_PING     "PING"	/* PING */
#define MSG_PONG     "PONG"	/* PONG */
#define MSG_OPER     "OPER"	/* OPER */
#define MSG_PASS     "PASS"	/* PASS */
#define MSG_WALLOPS  "WALLOPS"	/* WALL */
#define MSG_TIME     "TIME"	/* TIME */
#define MSG_NAMES    "NAMES"	/* NAME */
#define MSG_ADMIN    "ADMIN"	/* ADMI */
#define MSG_TRACE    "TRACE"	/* TRAC */
#define MSG_LTRACE   "LTRACE"   /* LTRA */
#define MSG_NOTICE   "NOTICE"	/* NOTI */
#define MSG_JOIN     "JOIN"	/* JOIN */
#define MSG_PART     "PART"	/* PART */
#define MSG_LUSERS   "LUSERS"	/* LUSE */
#define MSG_MOTD     "MOTD"	/* MOTD */
#define MSG_MODE     "MODE"	/* MODE */
#define MSG_KICK     "KICK"	/* KICK */
#define MSG_USERHOST "USERHOST"	/* USER -> USRH */
#define MSG_ISON     "ISON"	/* ISON */
#define MSG_USRIP    "USRIP"	/* USRIP */
#define	MSG_REHASH   "REHASH"	/* REHA */
#define	MSG_RESTART  "RESTART"	/* REST */
#define	MSG_CLOSE    "CLOSE"	/* CLOS */
#define	MSG_SVINFO   "SVINFO"	/* SVINFO */
#define	MSG_SJOIN    "SJOIN"	/* SJOIN */
#define MSG_CAPAB    "CAPAB"	/* CAPAB */
#define	MSG_DIE	     "DIE"      /* DIE */
#define	MSG_HASH     "HASH"	/* HASH */
#define	MSG_DNS      "DNS"	/* DNS  -> DNSS */
#define MSG_OPERWALL "OPERWALL" /* OPERWALL */
#define MSG_KLINE    "KLINE"    /* KLINE */
#ifdef UNKLINE
#define MSG_UNKLINE  "UNKLINE"	/* UNKLINE */
#endif
#define MSG_DLINE    "DLINE"	/* DLINE */
#define	MSG_HTM      "HTM"	/* HTM */
#define MSG_SET      "SET"	/* SET */

#ifdef GLINES
#define MSG_GLINE    "GLINE"    /* GLINE */
#endif

#define MSG_LOCOPS   "LOCOPS"	/* LOCOPS */
#ifdef LWALLOPS
#define MSG_LWALLOPS "LWALLOPS"	/* Same as LOCOPS */
#endif /* LWALLOPS */
#define MSG_KNOCK	   "KNOCK"  /* KNOCK */

#define MAXPARA    15 

extern int m_admin(aClient *,aClient *,int,char **);
extern int m_kline(aClient *,aClient *,int,char **);
extern int m_unkline(aClient *,aClient *,int,char **);
extern int m_dline(aClient *,aClient *,int,char **);

#ifdef GLINES
extern int m_gline(aClient *,aClient *,int,char **);
#endif

extern int m_locops(aClient *,aClient *,int,char **);

extern int m_private(aClient *,aClient *,int,char **);
extern int m_knock(aClient *,aClient *,int,char **);
extern int m_topic(aClient *,aClient *,int,char **);
extern int m_join(aClient *,aClient *,int,char **);
extern int m_part(aClient *,aClient *,int,char **);
extern int m_mode(aClient *,aClient *,int,char **);
extern int m_ping(aClient *,aClient *,int,char **);
extern int m_pong(aClient *,aClient *,int,char **);
extern int m_wallops(aClient *,aClient *,int,char **);
extern int m_kick(aClient *,aClient *,int,char **);
extern int m_nick(aClient *,aClient *,int,char **);
extern int m_error(aClient *,aClient *,int,char **);
extern int m_notice(aClient *,aClient *,int,char **);
extern int m_invite(aClient *,aClient *,int,char **);
extern int m_quit(aClient *,aClient *,int,char **);

extern int m_capab(aClient *,aClient *,int,char **);
extern int m_info(aClient *,aClient *,int,char **);
extern int m_kill(aClient *,aClient *,int,char **);
extern int m_list(aClient *,aClient *,int, char **);
extern int m_motd(aClient *,aClient *,int,char **);
extern int m_who(aClient *,aClient *,int,char **);
extern int m_whois(aClient *,aClient *,int,char **);
extern int m_server(aClient *,aClient *,int,char **);
extern int m_user(aClient *,aClient *,int, char **);
extern int m_links(aClient *,aClient *,int,char **);
extern int m_summon(aClient *,aClient *,int,char **);
extern int m_stats(aClient *,aClient *,int,char **);
extern int m_users(aClient *,aClient *,int,char **);
extern int m_version(aClient *,aClient *,int, char **);
extern int m_help(aClient *,aClient *,int, char**);
extern int m_squit(aClient *,aClient *,int, char **);
extern int m_away(aClient *,aClient *,int,char **);
extern int m_connect(aClient *,aClient *,int,char **);
extern int m_oper(aClient *,aClient *,int,char **);
extern int m_pass(aClient *,aClient *,int,char **);
extern int m_trace(aClient *,aClient *,int,char **);
#ifdef LTRACE
extern int m_ltrace(aClient *,aClient *,int,char **);
#endif /* LTRACE */
extern int m_time(aClient *,aClient *,int, char **);
extern int m_names(aClient *,aClient *,int,char **);

extern int m_lusers(aClient *,aClient *,int, char **);
extern int m_umode(aClient *,aClient *,int,char **);
extern int m_close(aClient *,aClient *,int,char **);

extern int m_whowas(aClient *,aClient *,int,char **);
extern int m_usrip(aClient *,aClient *,int,char **);
extern int m_userhost(aClient *,aClient *,int,char **);
extern int m_ison(aClient *,aClient *,int,char **);
extern int m_svinfo(aClient *,aClient *,int,char **);
extern int m_sjoin(aClient *,aClient *,int,char **);
extern int m_operwall(aClient *,aClient *,int,char **);
#if defined(OPER_REHASH) || defined(LOCOP_REHASH)
extern	int	m_rehash(aClient *,aClient *,int,char **);
#endif
#if defined(OPER_RESTART) || defined(LOCOP_RESTART)
extern	int	m_restart(aClient *,aClient *,int,char **);
#endif
#if defined(OPER_DIE) || defined(LOCOP_DIE)
extern	int	m_die(aClient *,aClient *,int,char **);
#endif
extern int m_hash(aClient *,aClient *,int,char **);
extern int m_dns(aClient *,aClient *,int,char **);
extern int m_htm(aClient *,aClient *,int,char **);
extern int m_set(aClient *,aClient *,int,char **);

#ifdef MSGTAB

struct Message msgtab[] = {
#ifdef IDLE_FROM_MSG	/* reset idle time only if privmsg used */
#ifdef IDLE_CHECK	/* reset idle time only if valid target for privmsg
			   and target is not source */

  /*                                        |-- allow use even when unreg.
					    v	yes/no			*/
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 0, 0L },
#else
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 1, 0L },
#endif

  /*                                           ^
					       |__ reset idle time when 1 */
#else	/* IDLE_FROM_MSG */
#ifdef	IDLE_CHECK	/* reset idle time on anything but privmsg */
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 1, 0L },
#else
  { MSG_PRIVATE, m_private,  0, MAXPARA, 1, 0, 0, 0L },
  /*                                           ^
					       |__ reset idle time when 0 */
#endif	/* IDLE_CHECK */
#endif	/* IDLE_FROM_MSG */

  { MSG_NICK,    m_nick,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_NOTICE,  m_notice,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_JOIN,    m_join,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_MODE,    m_mode,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_QUIT,    m_quit,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_PART,    m_part,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_KNOCK,   m_knock,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_TOPIC,   m_topic,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_INVITE,  m_invite,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_KICK,    m_kick,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WALLOPS, m_wallops,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_LOCOPS,  m_locops,   0, MAXPARA, 1, 0, 0, 0L },
#ifdef LWALLOPS
  { MSG_LWALLOPS,m_locops,   0, MAXPARA, 1, 0, 0, 0L },
#endif /* LWALLOPS */

#ifdef IDLE_FROM_MSG
  /* Only m_private has reset idle flag set */
#ifdef ANTI_IP_SPOOF
  /* if ANTI_IP_SPOOF, allow user to send PONG even when not registered */
  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 0, 0L },
#else
  /* if not ANTI_IP_SPOOF don't allow user to send PONG when not registered */
  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 0, 0L },
#endif

#else
  /* else for IDLE_FROM_MSG */
  /* reset idle flag sense is reversed, only reset idle time
   * when its 0, for IDLE_FROM_MSG ping/pong do not reset idle time
   */

#ifdef ANTI_IP_SPOOF
  /* if ANTI_IP_SPOOF, allow user to send PONG even when not registered */
  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 1, 1, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 1, 0L },
#else
  /* if not ANTI_IP_SPOOF don't allow user to send PONG when not registered */
  { MSG_PONG,    m_pong,     0, MAXPARA, 1, 0, 1, 0L },
  { MSG_PING,    m_ping,     0, MAXPARA, 1, 0, 1, 0L },
#endif

#endif	/* IDLE_FROM_MSG */

  { MSG_ERROR,   m_error,    0, MAXPARA, 1, 1, 0, 0L },
  { MSG_KILL,    m_kill,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_USER,    m_user,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_AWAY,    m_away,     0, MAXPARA, 1, 0, 0, 0L },
#ifdef IDLE_FROM_MSG
  { MSG_ISON,    m_ison,     0, 1,       1, 0, 0, 0L },
#else
  /* ISON should not reset idle time ever
   * remember idle flag sense is reversed when IDLE_FROM_MSG is undefined
   */
  { MSG_ISON,    m_ison,     0, 1,       1, 0, 1, 0L },
#endif
  { MSG_USRIP,   m_usrip,    0, 1,       1, 0, 0, 0L },
  { MSG_SERVER,  m_server,   0, MAXPARA, 1, 1, 0, 0L },
  { MSG_SQUIT,   m_squit,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WHOIS,   m_whois,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WHO,     m_who,      0, MAXPARA, 1, 0, 0, 0L },
  { MSG_WHOWAS,  m_whowas,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_LIST,    m_list,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_NAMES,   m_names,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_USERHOST,m_userhost, 0, 1,       1, 0, 0, 0L },
  { MSG_TRACE,   m_trace,    0, MAXPARA, 1, 0, 0, 0L },
#ifdef LTRACE
  { MSG_LTRACE,  m_ltrace,   0, MAXPARA, 1, 0, 0, 0L },
#endif /* LTRACE */
  { MSG_PASS,    m_pass,     0, MAXPARA, 1, 1, 0, 0L },
  { MSG_LUSERS,  m_lusers,   0, MAXPARA, 1, 0, 0, 0L },
  { MSG_TIME,    m_time,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_OPER,    m_oper,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_CONNECT, m_connect,  0, MAXPARA, 1, 0, 0, 0L },
  { MSG_VERSION, m_version,  0, MAXPARA, 1, 1, 0, 0L },
  { MSG_STATS,   m_stats,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_LINKS,   m_links,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_ADMIN,   m_admin,    0, MAXPARA, 1, 1, 0, 0L },
  { MSG_USERS,   m_users,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_HELP,    m_help,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_INFO,    m_info,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_MOTD,    m_motd,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_SVINFO,  m_svinfo,   0, MAXPARA, 1, 1, 0, 0L },
  { MSG_SJOIN,   m_sjoin,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_CAPAB,   m_capab,    0, MAXPARA, 1, 1, 0, 0L },
  { MSG_OPERWALL, m_operwall,0, MAXPARA, 1, 0, 0, 0L },
  { MSG_CLOSE,   m_close,    0, MAXPARA, 1, 0, 0, 0L },
  { MSG_KLINE,   m_kline,    0, MAXPARA, 1, 0, 0, 0L },
#ifdef UNKLINE 
  { MSG_UNKLINE, m_unkline,  0, MAXPARA, 1, 0, 0, 0L },
#endif
  { MSG_DLINE,   m_dline,    0, MAXPARA, 1, 0, 0, 0L },
#ifdef GLINES
  { MSG_GLINE,   m_gline,    0, MAXPARA, 1, 0, 0, 0L },
#endif
  { MSG_HASH,    m_hash,     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_DNS,     m_dns,      0, MAXPARA, 1, 0, 0, 0L },
  { MSG_REHASH,  m_rehash,   0, MAXPARA, 1, 0, 0, 0L },
#if defined(OPER_RESTART) || defined(LOCOP_RESTART)
  { MSG_RESTART, m_restart,  0, MAXPARA, 1, 0, 0, 0L },
#endif
#if defined(OPER_DIE) || defined(LOCOP_DIE)
  { MSG_DIE, m_die,          0, MAXPARA, 1, 0, 0, 0L },
#endif
  { MSG_HTM,	m_htm,	     0, MAXPARA, 1, 0, 0, 0L },
  { MSG_SET,	m_set,	     0, MAXPARA, 1, 0, 0, 0L },
  { (char *) 0, (int (*)()) 0 , 0, 0,    0, 0, 0, 0L }
};

MESSAGE_TREE *msg_tree_root;

#else

extern struct Message msgtab[];

extern MESSAGE_TREE *msg_tree_root;

#endif
#endif /* __msg_include__ */

