#include "struct.h"

#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#ifndef __EMX__
#include <utmp.h> /* old slackware utmp.h defines BYTE_ORDER */
#endif /* __EMX__ */

#if defined(AIX) || defined(DYNIXPTX) || defined(SVR3)
#include <time.h>
#endif
#include "h.h"
#if defined( HAVE_STRING_H )
#include <string.h>
#else
/* older unices don't have strchr/strrchr .. help them out */
#include <strings.h>
#undef strchr
#define strchr index
#endif
#include "fdlist.h"

/*
** m_info
**	parv[0] = sender prefix
**	parv[1] = servername
*/
int	m_info(aClient *cptr,
	       aClient *sptr,
	       int parc,
	       char *parv[])
{
  char **text = infotext;
  char outstr[241];
  static time_t last_used=0L;

  if (hunt_server(cptr,sptr,":%s INFO :%s",1,parc,parv) == HUNTED_ISME)
    {
      sendto_realops_lev(SPY_LEV, "info requested by %s (%s@%s) [%s]",
		         sptr->name, sptr->user->username, sptr->user->host,
                         sptr->user->server);
      if(!IsAnOper(sptr))
        {
          /* reject non local requests */
          if(!MyConnect(sptr))
            return 0;
          if((last_used + PACE_WAIT) > NOW)
            {
	      /* safe enough to give this on a local connect only */
	      sendto_one(sptr,rpl_str(RPL_LOAD2HI),me.name,parv[0]);
              return 0;
            }
          else
            {
              last_used = NOW;
            }
        }

      while (*text)
	sendto_one(sptr, rpl_str(RPL_INFO), me.name, parv[0], *text++);
      
      sendto_one(sptr, rpl_str(RPL_INFO), me.name, parv[0], "");

      if (IsAnOper(sptr))
      {
#ifdef ANTI_DRONE_FLOOD
#define OUT1     "ANTI_DRONE_FLOOD=1"
#else
#define OUT1     "ANTI_DRONE_FLOOD=0"
#endif

#ifdef ANTI_NICK_FLOOD
#define OUT2     " ANTI_NICK_FLOOD=1"
#else
#define OUT2     " ANTI_NICK_FLOOD=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], "switches: " OUT1 OUT2 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef ANTI_SPAMBOT
#define OUT1    "ANTI_SPAMBOT=1"
#else
#define OUT1    "ANTI_SPAMBOT=0"
#endif

#ifdef ANTI_SPAMBOT_WARN_ONLY
#define OUT2	" ANTI_SPAMBOT_WARN_ONLY=1"
#else
#define OUT2	" ANTI_SPAMBOT_WARN_ONLY=0"
#endif
#ifdef ANTI_SPAM_EXIT_MESSAGE
#define OUT3    " ANTI_SPAM_EXIT_MESSAGE=1"
#else
#define OUT3    " ANTI_SPAM_EXIT_MESSAGE=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef B_LINES_OPER_ONLY
#define OUT1    "B_LINES_OPER_ONLY=1"
#else
#define OUT1    "B_LINES_OPER_ONLY=0"
#endif
#ifdef BAN_INFO
#define OUT2 " BAN_INFO=1"
#else
#define OUT2 " BAN_INFO=0"
#endif
#ifdef BOTCHECK
#define OUT3 " BOTCHECK=1"
#else
#define OUT3 " BOTCHECK=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef CHROOTDIR
#define OUT1 "CHROOTDIR=1"
#else
#define OUT1 "CHROOTDIR=0"
#endif
#ifdef CLIENT_FLOOD
#define OUT2 " CLIENT_FLOOD=1"
#else
#define OUT2 " CLIENT_FLOOD=0"
#endif
#ifdef CMDLINE_CONFIG
#define OUT3 " CMDLINE_CONFIG=1"
#else
#define OUT3 " CMDLINE_CONFIG=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef CUSTOM_ERR
#define OUT1 "CUSTOM_ERR=1"
#else
#define OUT1 "CUSTOM_ERR=0"
#endif
#ifdef DEBUGMODE
#define OUT2 " DEBUGMODE=1"
#else
#define OUT2 " DEBUGMODE=0"
#endif
#ifdef DLINES_IN_KPATH
#define OUT3 " DLINES_IN_KPATH=1"
#else
#define OUT3 " DLINES_IN_KPATH=0"
#endif
#ifdef DNS_DEBUG
#define OUT4 " DNS_DEBUG=1"
#else
#define OUT4 " DNS_DEBUG=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 OUT4);
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef DO_IDENTD
#define OUT1 "DO_IDENTD=1"
#else
#define OUT1 "DO_IDENTD=0"
#endif
#ifdef E_LINES_OPER_ONLY
#define OUT2 " E_LINES_OPER_ONLY=1"
#else
#define OUT2 " E_LINES_OPER_ONLY=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef FAILED_OPER_NOTICE
#define OUT1 "FAILED_OPER_NOTICE=1"
#else
#define OUT1 "FAILED_OPER_NOTICE=0"
#endif
#ifdef FLUD
#define OUT2 " FLUD=1"
#else
#define OUT2 " FLUD=0"
#endif
#ifdef FOLLOW_IDENT_RFC
#define OUT3 " FOLLOW_IDENT_RFC=1"
#else
#define OUT3 " FOLLOW_IDENT_RFC=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef F_LINES_OPER_ONLY
#define OUT1 "F_LINES_OPER_ONLY=1"
#else
#define OUT1 "F_LINES_OPER_ONLY=0"
#endif
#ifdef GLINES
#define OUT2 " GLINES=1"
#else
#define OUT2 " GLINES=0"
#endif
#ifdef HUB
#define OUT3 " HUB=1"
#else
#define OUT3 " HUB=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef IDLE_CHECK
#define OUT1 "IDLE_CHECK=1"
#else
#define OUT1 "IDLE_CHECK=0"
#endif
#ifdef IDLE_FROM_MSG
#define OUT2 " IDLE_FROM_MSG=1"
#else
#define OUT2 " IDLE_FROM_MSG=0"
#endif
#ifdef IGNORE_FIRST_CHAR
#define OUT3 " IGNORE_FIRST_CHAR=1"
#else
#define OUT3 " IGNORE_FIRST_CHAR=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef KLINE_WITH_CONNECTION_CLOSED
#define OUT1 "KLINE_WITH_CONNECTION_CLOSED=1"
#else
#define OUT1 "KLINE_WITH_CONNECTION_CLOSED=0"
#endif
#ifdef KLINE_WITH_REASON
#define OUT2 " KLINE_WITH_REASON=1"
#else
#define OUT2 " KLINE_WITH_REASON=0"
#endif
#ifdef KPATH
#define OUT3 " KPATH=1"
#else
#define OUT3 " KPATH=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef LIMIT_UH
#define OUT1 "LIMIT_UH=1"
#else
#define OUT1 "LIMIT_UH=0"
#endif
#ifdef LITTLE_I_LINES
#define OUT2 " LITTLE_I_LINES=1"
#else
#define OUT2 " LITTLE_I_LINES=0"
#endif
#ifdef LOCKFILE
#define OUT3 " LOCKFILE=1"
#else
#define OUT3 " LOCKFILE=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef LTRACE
#define OUT1 "LTRACE=1"
#else
#define OUT1 "LTRACE=0"
#endif
#ifdef LWALLOPS
#define OUT2 " LWALLOPS=1"
#else
#define OUT2 " LWALLOPS=0"
#endif
#ifdef MAXBUFFERS
#define OUT3 " MAXBUFFERS=1"
#else
#define OUT3 " MAXBUFFERS=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef NON_REDUNDANT_KLINES
#define OUT1 "NON_REDUNDANT_KLINES=1"
#else
#define OUT1 "NON_REDUNDANT_KLINES=0"
#endif
#ifdef NO_CHANOPS_WHEN_SPLIT
#define OUT2 " NO_CHANOPS_WHEN_SPLIT=1"
#else
#define OUT2 " NO_CHANOPS_WHEN_SPLIT=0"
#endif
#ifdef NO_DEFAULT_INVISIBLE
#define OUT3 " NO_DEFAULT_INVISIBLE=1"
#else
#define OUT3 " NO_DEFAULT_INVISIBLE=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef NO_JOIN_ON_SPLIT
#define OUT1 "NO_JOIN_ON_SPLIT=1"
#else
#define OUT1 "NO_JOIN_ON_SPLIT=0"
#endif
#ifdef NO_JOIN_ON_SPLIT_SIMPLE
#define OUT2 " NO_JOIN_ON_SPLIT_SIMPLE=1"
#else
#define OUT2 " NO_JOIN_ON_SPLIT_SIMPLE=0"
#endif 
#ifdef NO_LOCAL_KLINE
#define OUT3 " NO_LOCAL_KLINE=1"
#else
#define OUT3 " NO_LOCAL_KLINE=0"
#endif
#ifdef NO_MIXED_CASE
#define OUT4 " NO_MIXED_CASE=1"
#else
#define OUT4 " NO_MIXED_CASE=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 OUT4);
#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef NO_OPER_FLOOD
#define OUT1 "NO_OPER_FLOOD=1"
#else
#define OUT1 "NO_OPER_FLOOD=0"
#endif
#ifdef NO_PRIORITY
#define OUT2 " NO_PRIORITY=1"
#else
#define OUT2 " NO_PRIORITY=0"
#endif
#ifdef NO_SPECIAL
#define OUT3 " NO_SPECIAL=1"
#else
#define OUT3 " NO_SPECIAL=0"
#endif
#ifdef NOISY_HTM
#define OUT4 " NOISY_HTM=1"
#else
#define OUT4 " NOISY_HTM=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef OLD_Y_LIMIT
#define OUT1 "OLD_Y_LIMIT=1"
#else
#define OUT1 "OLD_Y_LIMIT=0"
#endif
#ifdef PRESERVE_CHANNEL_ON_SPLIT
#define OUT2 " PRESERVE_CHANNEL_ON_SPLIT=1"
#else
#define OUT2 " PRESERVE_CHANNEL_ON_SPLIT=0"
#endif
#ifdef REJECT_HOLD
#define OUT3 " REJECT_HOLD=1"
#else
#define OUT3 " REJECT_HOLD=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef REPORT_DLINE_TO_USER
#define OUT1 "REPORT_DLINE_TO_USER=1"
#else
#define OUT1 "REPORT_DLINE_TO_USER=1"
#endif
#ifdef RFC1035_ANAL
#define OUT2 " RFC1035_ANAL=1"
#else
#define OUT2 " RFC1035_ANAL=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef R_LINES
#define OUT1 "R_LINES=1"
#else
#define OUT1 "R_LINES=0"
#endif
#ifdef R_LINES_OFTEN
#define OUT2 " R_LINES_OFTEN=1"
#else
#define OUT2 " R_LINES_OFTEN=0"
#endif
#ifdef R_LINES_REHASH
#define OUT3 " R_LINES_REHASH=1"
#else
#define OUT3 " R_LINES_REHASH=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef SEND_FAKE_KILL_TO_CLIENT
#define OUT1 "SEND_FAKE_KILL_TO_CLIENT=1"
#else
#define OUT1 "SEND_FAKE_KILL_TO_CLIENT=0"
#endif
#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
#define OUT2 " SEPARATE_QUOTE_KLINES_BY_DATE=1"
#else
#define OUT2 " SEPARATE_QUOTE_KLINES_BY_DATE=0"
#endif
#ifdef SHORT_MOTD
#define OUT3 " SHORT_MOTD=1"
#else
#define OUT3 " SHORT_MOTD=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef SHOW_FAILED_OPER_ID
#define OUT1 "SHOW_FAILED_OPER_ID=1"
#else
#define OUT1 "SHOW_FAILED_OPER_ID=0"
#endif
#ifdef SHOW_FAILED_OPER_PASSWD
#define OUT2 " SHOW_FAILED_OPER_PASSWD=1"
#else
#define OUT2 " SHOW_FAILED_OPER_PASSWD=0"
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef SHOW_INVISIBLE_LUSERS
#define OUT1 "SHOW_INVISIBLE_LUSERS=1"
#else
#define OUT1 "SHOW_INVISIBLE_LUSERS=0"
#endif
#ifdef SLAVE_SERVERS
#define OUT2 " SLAVE_SERVERS=1"
#else
#define OUT2 " SLAVE_SERVERS=0"
#endif
#ifdef SPLIT_PONG
#define OUT3 " SPLIT_PONG=1"
#else
#define OUT3 " SPLIT_PONG=0"
#endif
#ifdef STATS_NOTICE
#define OUT4 " STATS_NOTICE=1"
#else
#define OUT4 " STATS_NOTICE=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef TOPIC_INFO
#define OUT1 "TOPIC_INFO=1"
#else
#define OUT1 "TOPIC_INFO=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 );

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef USE_IP_I_LINE_FIRST
#define OUT1 "USE_IP_I_LINE_FIRST=1"
#else
#define OUT1 "USE_IP_I_LINE_FIRST=0"
#endif
#ifdef USE_RCS
#define OUT2 " USE_RCS=1"
#else
#define OUT2 " USE_RCS=0"
#endif
#ifdef USE_SYSLOG
#define OUT3 " USE_SYSLOG=1"
#else
#define OUT3 " USE_SYSLOG=0"
#endif
#ifdef USE_UH
#define OUT4 " USE_UH=1"
#else
#define OUT4 " USE_UH=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef WARN_NO_NLINE
#define OUT1 "WARN_NO_NLINE=1"
#else
#define OUT1 "WARN_NO_NLINE=0"
#endif
#ifdef WHOIS_NOTICE
#define OUT2 " WHOIS_NOTICE=1"
#else
#define OUT2 " WHOIS_NOTICE=0"
#endif
#ifdef WINTRHAWK
#define OUT3 " WINTRHAWK=1"
#else
#define OUT3 " WINTRHAWK=0"
#endif
#ifdef ZIP_LINKS
#define OUT4 " ZIP_LINKS=1"
#else
#define OUT4 " ZIP_LINKS=0"
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], OUT1 OUT2 OUT3 OUT4);

#undef OUT1
#undef OUT2
#undef OUT3
#undef OUT4

#ifdef ANTI_SPAMBOT
        ircsprintf(outstr,"values: ANTI_SPAM_EXIT_MESSAGE_TIME=%d BUFFERPOOL=%d",
                   ANTI_SPAM_EXIT_MESSAGE_TIME, BUFFERPOOL);
#else
	ircsprintf(outstr,"values: BUFFERPOOL=%d",
                   BUFFERPOOL);
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

#ifdef LOCKFILE
        ircsprintf(outstr,"CHECK_PENDING_KLINES=%d", CHECK_PENDING_KLINES);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

#ifdef NEED_SPLITCODE
	ircsprintf(outstr,"DEFAULT_SERVER_SPLIT_RECOVERY_TIME=%d",
                  DEFAULT_SERVER_SPLIT_RECOVERY_TIME);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

#ifdef FLUD
	ircsprintf(outstr,"FLUD_BLOCK=%d FLUD_NUM=%d FLUD_TIME=%d",
	  FLUD_BLOCK,FLUD_NUM,FLUD_TIME);
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif
#ifdef GLINES
	ircsprintf(outstr,"GLINE_TIME=%d",GLINE_TIME);
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif
#ifdef SOMAXCONN
	ircsprintf(outstr,"HARD_FDLIMIT_=%d HYBRID_SOMAXCONN=SOMAXCONN=%d",
		   HARD_FDLIMIT_,SOMAXCONN);
#else
	ircsprintf(outstr,"HARD_FDLIMIT_=%d HYBRID_SOMAXCONN=%d",
		   HARD_FDLIMIT_,HYBRID_SOMAXCONN);
#endif
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

#ifdef IDLE_CHECK
        ircsprintf(outstr,"IDLE_TIME=%d INITIAL_DBUFS=%d INIT_MAXCLIENTS=%d",
                   IDLE_TIME, INITIAL_DBUFS, INIT_MAXCLIENTS);
#else
        ircsprintf(outstr,"INITIAL_DBUFS=%d INIT_MAXCLIENTS=%d",
                   INITIAL_DBUFS, INIT_MAXCLIENTS);
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

#ifdef ANTI_SPAMBOT
	ircsprintf(outstr,"JOIN_LEAVE_COUNT_EXPIRE_TIME=%d KILLCHASETIMELIMIT=%d KNOCK_DELAY=%d",
                   JOIN_LEAVE_COUNT_EXPIRE_TIME,KILLCHASETIMELIMIT,KNOCK_DELAY);
#else
        ircsprintf(outstr,"KILLCHASETIMELIMIT=%d KNOCK_DELAY=%d",
                   KILLCHASETIMELIMIT, KNOCK_DELAY);
#endif
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

	ircsprintf(outstr,"MAXCHANNELSPERUSER=%d MAXIMUM_LINKS=%d MAX_BUFFER=%d",
                   MAXCHANNELSPERUSER,MAXIMUM_LINKS,MAX_BUFFER);
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

#ifdef ANTI_SPAMBOT
        ircsprintf(outstr,"MAX_JOIN_LEAVE_COUNT=%d MIN_JOIN_LEAVE_TIME=%d",
                  MAX_JOIN_LEAVE_COUNT,MIN_JOIN_LEAVE_TIME);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

#ifdef ANTI_NICK_FLOOD
	ircsprintf(outstr,"MAX_NICK_CHANGES=%d MAX_NICK_TIME=%d",MAX_NICK_CHANGES,MAX_NICK_TIME);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

	ircsprintf(outstr,"NICKNAMEHISTORYLENGTH=%d NOISY_HTM=%d",
                   NICKNAMEHISTORYLENGTH,NOISY_HTM);
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

#ifdef ANTI_SPAMBOT
        ircsprintf(outstr,"OPER_SPAM_COUNTDOWN=%d PACE_WAIT=%d",
                  OPER_SPAM_COUNTDOWN,PACE_WAIT);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#else
        ircsprintf(outstr,"PACE_WAIT=%d",PACE_WAIT);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

#ifdef REJECT_HOLD
        ircsprintf(outstr,"REJECT_HOLD_TIME=%d",REJECT_HOLD_TIME);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

#if defined(NO_CHANOPS_WHEN_SPLIT) || defined(PRESERVE_CHANNEL_ON_SPLIT) || \
        defined(NO_JOIN_ON_SPLIT)  || defined(NO_JOIN_ON_SPLIT_SIMPLE)

        ircsprintf(outstr,"SPLIT_SMALLNET_SIZE=%d SPLIT_SMALLNET_USER_SIZE=%d",
                   SPLIT_SMALLNET_SIZE,SPLIT_SMALLNET_USER_SIZE) ;
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);
#endif

        ircsprintf(outstr,"TS_MAX_DELTA=%d TS_WARN_DELTA=%d WHOIS_WAIT=%d",
                   TS_MAX_DELTA,TS_WARN_DELTA,WHOIS_WAIT);
	sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr);

#ifdef ZIP_LINKS
        ircsprintf(outstr,"ZIP_LEVEL=%d",ZIP_LEVEL);
        sendto_one(sptr, rpl_str(RPL_INFO),
                   me.name, parv[0], outstr); 
#endif

      }

      sendto_one(sptr,
		 ":%s %d %s :Birth Date: %s, compile # %s",
		 me.name, RPL_INFO, parv[0], creation, generation);
      sendto_one(sptr, ":%s %d %s :On-line since %s",
		 me.name, RPL_INFO, parv[0],
		 myctime(me.firsttime));
      sendto_one(sptr, rpl_str(RPL_ENDOFINFO), me.name, parv[0]);
    }

  return 0;
}

