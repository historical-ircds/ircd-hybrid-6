/************************************************************************
 *   IRC - Internet Relay Chat, ircd/m_info.h
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
 *
 * $Id$
 */
#ifndef INCLUDED_m_info_h
#define INCLUDED_m_info_h
#ifndef INCLUDED_config_h
#include "config.h"
#endif

typedef struct Information
{
  char* name;        /* name of item */
  char* strvalue;    /* value of item if it's a boolean */
  int   intvalue;    /* value of item if it's an integer */
  char* desc;        /* short description of item */
} Info;

/*
 * only define MyInformation if we are compiling m_info.c
 */
#ifdef DEFINE_M_INFO_DATA

Info MyInformation[] = {

#ifdef ANTI_DRONE_FLOOD
  { "ANTI_DRONE_FLOOD", "ON", 0, "Anti Flood for Drones" },
#else
  { "ANTI_DRONE_FLOOD", "OFF", 0, "Anti Flood for Drones" },
#endif /* ANTI_DRONE_FLOOD */

#ifdef ANTI_NICK_FLOOD
  { "ANTI_NICK_FLOOD", "ON", 0, "Nickname Flood Protection" },
#else
  { "ANTI_NICK_FLOOD", "OFF", 0, "Nickname Flood Protection" },
#endif /* ANTI_NICK_FLOOD */

#ifdef ANTI_SPAMBOT
  { "ANTI_SPAMBOT", "ON", 0, "Spam Bot Detection" },
#else
  { "ANTI_SPAMBOT", "OFF", 0, "Spam Bot Detection" },
#endif /* ANTI_SPAMBOT */

#ifdef ANTI_SPAMBOT_WARN_ONLY
  { "ANTI_SPAMBOT_WARN_ONLY", "ON", 0, "Warn Operators of Possible Spam Bots" },
#else
  { "ANTI_SPAMBOT_WARN_ONLY", "OFF", 0, "Warn Operators of Possible Spam Bots" },
#endif /* ANTI_SPAMBOT_WARN_ONLY */

#ifdef ANTI_SPAM_EXIT_MESSAGE
  { "ANTI_SPAM_EXIT_MESSAGE", "ON", 0, "Do not broadcast Spam Bots' exit messages" },
#else
  { "ANTI_SPAM_EXIT_MESSAGE", "OFF", 0, "Do not broadcast Spam Bots' exit messages" },
#endif /* ANTI_SPAM_EXIT_MESSAGE */

#ifdef ANTI_SPAM_EXIT_MESSAGE_TIME
  { "ANTI_SPAM_EXIT_MESSAGE_TIME", "", ANTI_SPAM_EXIT_MESSAGE_TIME, "Delay before Allowing Spam Bot Exit Messages" },
#else
  { "ANTI_SPAM_EXIT_MESSAGE_TIME", "NONE", 0, "Delay before Allowing Spam Bot Exit Messages" },
#endif /* ANTI_SPAM_EXIT_MESSAGE_TIME */

#ifdef B_LINES_OPER_ONLY
  { "B_LINES_OPER_ONLY", "ON", 0, "Allow only Operators to use STATS B" },
#else
  { "B_LINES_OPER_ONLY", "OFF", 0, "Allow only Operators to use STATS B" },
#endif /* B_LINES_OPER_ONLY */

#ifdef BAN_INFO
  { "BAN_INFO", "ON", 0, "Displays who set a ban and when" },
#else
  { "BAN_INFO", "OFF", 0, "Displays who set a ban and when" },
#endif /* BAN_INFO */

#ifdef BOTCHECK
  { "BOTCHECK", "ON", 0, "Rudimentary Bot Checking" },
#else
  { "BOTCHECK", "OFF", 0, "Rudimentary Bot Checking" },
#endif /* BOTCHECK */

  { "BUFFERPOOL", "", BUFFERPOOL, "Maximum size of all SendQs" },

#ifdef CHROOTDIR
  { "CHROOTDIR", "ON", 0, "chroot() before reading Configuration File" },
#else
  { "CHROOTDIR", "OFF", 0, "chroot() before reading Configuration File" },
#endif

#ifdef CLIENT_FLOOD
  { "CLIENT_FLOOD", "", CLIENT_FLOOD, "Client Excess Flood Threshold" },
#else
  { "CLIENT_FLOOD", "OFF", 0, "Client Excess Flood Threshold" },
#endif /* CLIENT_FLOOD */

#ifdef CMDLINE_CONFIG
  { "CMDLINE_CONFIG", "ON", 0, "Allow Command Line Specification of Config File" },
#else
  { "CMDLINE_CONFIG", "OFF", 0, "Allow Command Line Specification of Config File" },
#endif /* CMDLINE_CONFIG */

#ifdef CPATH
  { "CPATH", CPATH, 0, "Path to Main Configuration File" },
#else
  { "CPATH", "NONE", 0, "Path to Main Configuration File" },
#endif /* CPATH */

#ifdef CRYPT_OPER_PASSWORD
  { "CRYPT_OPER_PASSWORD", "ON", 0, "Encrypt Operator Passwords" },
#else
  { "CRYPT_OPER_PASSWORD", "OFF", 0, "Encrypt Operator Passwords" },
#endif /* CRYPT_OPER_PASSWORD */

#ifdef CRYPT_LINK_PASSWORD
  { "CRYPT_LINK_PASSWORD", "ON", 0, "Encrypt Server Passwords" },
#else
  { "CRYPT_LINK_PASSWORD", "OFF", 0, "Encrypt Server Passwords" },
#endif /* CRYPT_LINK_PASSWORD */

#ifdef CUSTOM_ERR
  { "CUSTOM_ERR", "ON", 0, "Customized error messages" },
#else
  { "CUSTOM_ERR", "OFF", 0, "Customized error messages" },
#endif /* CUSTOM_ERR */

#ifdef DEBUGMODE
  { "DEBUGMODE", "ON", 0, "Debugging Mode" },
#else
  { "DEBUGMODE", "OFF", 0, "Debugging Mode" },
#endif /* DEBUGMODE */

#ifdef DEFAULT_SERVER_SPLIT_RECOVERY_TIME
  { "DEFAULT_SERVER_SPLIT_RECOVERY_TIME", "", DEFAULT_SERVER_SPLIT_RECOVERY_TIME, "Time to Delay Split Status After Resynch" },
#else
  { "DEFAULT_SERVER_SPLIT_RECOVERY_TIME", "NONE", 0, "Time to Delay Split Status After Resynch" },
#endif /* DEFAULT_SERVER_SPLIT_RECOVERY_TIME */

#ifdef DNS_DEBUG
  { "DNS_DEBUG", "ON", 0, "Dns Debugging" },
#else
  { "DNS_DEBUG", "OFF", 0, "Dns Debugging" },
#endif /* DNS_DEBUG */

#ifdef DO_IDENTD
  { "DO_IDENTD", "ON", 0, "Perform identd checks" },
#else
  { "DO_IDENTD", "OFF", 0, "Perform identd checks" },
#endif /* DO_IDENTD */

#ifdef DPATH
  { "DPATH", DPATH, 0, "Directory Containing Configuration Files" },
#else
  { "DPATH", "NONE", 0, "Directory Containing Configuration Files" },
#endif /* DPATH */

#ifdef DLPATH
  { "DLPATH", DLPATH, 0, "Path to D-line File" },
#else
  { "DLPATH", "NONE", 0, "Path to D-line File" },
#endif /* DLPATH */

#ifdef E_LINES_OPER_ONLY
  { "E_LINES_OPER_ONLY", "ON", 0, "Allow only Operators to use STATS E" },
#else
  { "E_LINES_OPER_ONLY", "OFF", 0, "Allow only Operators to use STATS E" },
#endif /* E_LINES_OPER_ONLY */

#ifdef FAILED_OPER_NOTICE
  { "FAILED_OPER_NOTICE", "ON", 0, "Display invalid OPER attempts" },
#else
  { "FAILED_OPER_NOTICE", "OFF", 0, "Display invalid OPER attempts" },
#endif /* FAILED_OPER_NOTICE */

#ifdef FLUD
  { "FLUD", "ON", 0, "CTCP Flood Detection and Protection" },
#else
  { "FLUD", "OFF", 0, "CTCP Flood Detection and Protection" },
#endif /* FLUD */

#ifdef FLUD_NUM
  { "FLUD_NUM", "", FLUD_NUM, "Number of Messages to Trip Alarm" },
#else
  { "FLUD_NUM", "NONE", 0, "Number of Messages to Trip Alarm" },
#endif /* FLUD_NUM */

#ifdef FLUD_TIME
  { "FLUD_TIME", "", FLUD_TIME, "Time Window in which a Flud occurs" },
#else
  { "FLUD_TIME", "NONE", 0, "Time Window in which a Flud occurs" },
#endif /* FLUD_TIME */

#ifdef FLUD_BLOCK
  { "FLUD_BLOCK", "", FLUD_BLOCK, "Seconds to Block Fluds" },
#else
  { "FLUD_BLOCK", "NONE", 0, "Seconds to Block Fluds" },
#endif /* FLUD_BLOCK */

#ifdef F_LINES_OPER_ONLY
  { "F_LINES_OPER_ONLY", "ON", 0, "Allow only Operators to use STATS F" },
#else
  { "F_LINES_OPER_ONLY", "OFF", 0, "Allow only Operators to use STATS F" },
#endif /* F_LINES_OPER_ONLY */

#ifdef GLINE_TIME
  { "GLINE_TIME", "", GLINE_TIME, "Expire Time for Glines" },
#else
  { "GLINE_TIME", "NONE", 0, "Expire Time for Glines" },
#endif /* GLINE_TIME */

#ifdef GLINEFILE
  { "GLINEFILE", GLINEFILE, 0, "Path to G-line File" },
#else
  { "GLINEFILE", "NONE", 0, "Path to G-line File" },
#endif /* GLINEFILE */

#ifdef GLINES
  { "GLINES", "ON", 0, "Network wide K-lines" },
#else
  { "GLINES", "OFF", 0, "Network wide K-lines" },
#endif /* GLINES */

  { "HARD_FDLIMIT_", "", HARD_FDLIMIT_, "Maximum Number of File Descriptors Available" },

#ifdef HPATH
  { "HPATH", HPATH, 0, "Path to Operator Help File" },
#else
  { "HPATH", "NONE", 0, "Path to Operator Help File" },
#endif /* HPATH */

#ifdef HUB
  { "HUB", "ON", 0, "Configured as a HUB Server" },
#else
  { "HUB", "OFF", 0, "Configured as a HUB Server" },
#endif /* HUB */

#ifdef SOMAXCONN
  { "HYBRID_SOMAXCONN", "", SOMAXCONN, "Maximum Queue Length of Pending Connections" },
#else
  { "HYBRID_SOMAXCONN", "", HYBRID_SOMAXCONN, "Maximum Queue Length of Pending Connections" },
#endif /* SOMAXCONN */

#ifdef IDLE_CHECK
  { "IDLE_CHECK", "ON", 0, "Check Clients for Excessive Idleness" },
#else
  { "IDLE_CHECK", "OFF", 0, "Check Clients for Excessive Idleness" },
#endif /* IDLE_CHECK */

#ifdef IDLE_TIME
  { "IDLE_TIME", "", IDLE_TIME, "Delay (in minutes) before a client is considered idle" },
#else
  { "IDLE_TIME", "OFF", 0, "Delay (in minutes) before a client is considered idle" },
#endif /* IDLE_TIME */

#ifdef IDLE_FROM_MSG
  { "IDLE_FROM_MSG", "ON", 0, "Reset idle time after a PRIVMSG" },
#else
  { "IDLE_FROM_MSG", "OFF", 0, "Reset idle time after a PRIVMSG" },
#endif /* IDLE_FROM_MSG */

  { "INIT_MAXCLIENTS", "", INIT_MAXCLIENTS, "Maximum Clients" },
  { "INITIAL_DBUFS", "", INITIAL_DBUFS, "Number of Dbufs to PreAllocate" },

#ifdef ANTI_SPAMBOT
  { "JOIN_LEAVE_COUNT_EXPIRE_TIME", "", JOIN_LEAVE_COUNT_EXPIRE_TIME, "Anti SpamBot Parameter" },
#endif /* ANTI_SPAMBOT */

#ifdef JUPE_CHANNEL
  { "JUPE_CHANNEL", "ON", 0, "Locally jupe channels" },
#else
  { "JUPE_CHANNEL", "OFF", 0, "Locally jupe channels" },
#endif /* JUPE_CHANNEL */

  { "KILLCHASETIMELIMIT", "", KILLCHASETIMELIMIT, "Nick Change Tracker for KILL" },

#ifdef KLINE_WITH_CONNECTION_CLOSED
  { "KLINE_WITH_CONNECTION_CLOSED", "ON", 0, "Signoff reason: Connection closed" },
#else
  { "KLINE_WITH_CONNECTION_CLOSED", "OFF", 0, "Signoff reason: Connection closed" },
#endif /* KLINE_WITH_CONNECTION_CLOSED */

#ifdef KLINE_WITH_REASON
  { "KLINE_WITH_REASON", "ON", 0, "Show K-line Reason to Client on Exit" },
#else
  { "KLINE_WITH_REASON", "OFF", 0, "Show K-line Reason to Client on Exit" },
#endif /* KLINE_WITH_REASON */

  { "KNOCK_DELAY", "", KNOCK_DELAY, "Delay between KNOCK Attempts" },

#ifdef KPATH
  { "KPATH", KPATH, 0, "Path to K-line File" },
#else
  { "KPATH", "NONE", 0, "Path to K-line File" },
#endif /* KPATH */

#ifdef LIMIT_UH
  { "LIMIT_UH", "ON", 0, "Make Y: lines limit username instead of hostname" },
#else
  { "LIMIT_UH", "OFF", 0, "Make Y: lines limit username instead of hostname" },
#endif /* LIMIT_UH */

#ifdef LITTLE_I_LINES
  { "LITTLE_I_LINES", "ON", 0, "\"i\" lines prevent matching clients from channel opping" },
#else
  { "LITTLE_I_LINES", "OFF", 0, "\"i\" lines prevent matching clients from channel opping" },
#endif /* LITTLE_I_LINES */

#ifdef LPATH
  { "LPATH", LPATH, 0, "Path to Log File" },
#else
  { "LPATH", "NONE", 0, "Path to Log File" },
#endif /* LPATH */

#ifdef LTRACE
  { "LTRACE", "ON", 0, "Limited Trace Output" },
#else
  { "LTRACE", "OFF", 0, "Limited Trace Output" },
#endif /* LTRACE */

#ifdef LWALLOPS
  { "LWALLOPS", "ON", 0, "Local Wallops Support" },
#else
  { "LWALLOPS", "OFF", 0, "Local Wallops Support" },
#endif /* LWALLOPS */

  { "MAX_BUFFER", "", MAX_BUFFER, "Maximum Buffer Connections Allowed" },

#ifdef ANTI_SPAMBOT
  { "MAX_JOIN_LEAVE_COUNT", "", MAX_JOIN_LEAVE_COUNT, "Anti SpamBot Parameter" },
#endif /* ANTI_SPAMBOT */

#ifdef ANTI_NICK_FLOOD
  { "MAX_NICK_CHANGES", "", MAX_NICK_CHANGES, "Maximum Nick Changes Allowed" },
  { "MAX_NICK_TIME", "", MAX_NICK_TIME, "Time Window for MAX_NICK_CHANGES" },
#endif /* ANTI_NICK_FLOOD */

  { "MAXCHANNELSPERUSER", "", MAXCHANNELSPERUSER, "Maximum Channels per User" },
  { "MAXIMUM_LINKS", "", MAXIMUM_LINKS, "Maximum Links for Class 0" },

#ifdef ANTI_SPAMBOT
  { "MIN_JOIN_LEAVE_TIME", "", MIN_JOIN_LEAVE_TIME, "Anti SpamBot Parameter" },
#endif /* ANTI_SPAMBOT */

#ifdef MPATH
  { "MPATH", MPATH, 0, "Path to MOTD File" },
#else
  { "MPATH", "NONE", 0, "Path to MOTD File" },
#endif /* MPATH */

  { "NICKNAMEHISTORYLENGTH", "", NICKNAMEHISTORYLENGTH, "Size of WHOWAS Array" },

#ifdef NON_REDUNDANT_KLINES
  { "NON_REDUNDANT_KLINES", "ON", 0, "Check for and Disallow Redundant K-lines" },
#else
  { "NON_REDUNDANT_KLINES", "OFF", 0, "Check for and Disallow Redundant K-lines" },
#endif /* NON_REDUNDANT_KLINES */

#ifdef NO_CHANOPS_WHEN_SPLIT
  { "NO_CHANOPS_WHEN_SPLIT", "ON", 0, "Do not Allow Channel Ops During a NetSplit" },
#else
  { "NO_CHANOPS_WHEN_SPLIT", "OFF", 0, "Do not Allow Channel Ops During a NetSplit" },
#endif /* NO_CHANOPS_WHEN_SPLIT */

#ifdef NO_DEFAULT_INVISIBLE
  { "NO_DEFAULT_INVISIBLE", "ON", 0, "Do not Give Clients +i Mode Upon Connection" },
#else
  { "NO_DEFAULT_INVISIBLE", "OFF", 0, "Do not Give Clients +i Mode Upon Connection" },
#endif /* NO_DEFAULT_INVISIBLE */

#ifdef NO_JOIN_ON_SPLIT
  { "NO_JOIN_ON_SPLIT", "ON", 0, "Users Cannot Join Channels Present before a NetSplit" },
#else
  { "NO_JOIN_ON_SPLIT", "OFF", 0, "Users Cannot Join Channels Present before a NetSplit" },
#endif /* NO_JOIN_ON_SPLIT */

#ifdef NO_JOIN_ON_SPLIT_SIMPLE
  { "NO_JOIN_ON_SPLIT_SIMPLE", "ON", 0, "Users Cannot Join Channels During a NetSplit" },
#else
  { "NO_JOIN_ON_SPLIT_SIMPLE", "OFF", 0, "Users Cannot Join Channels During a NetSplit" },
#endif /* NO_JOIN_ON_SPLIT_SIMPLE */

#ifdef NO_OPER_FLOOD
  { "NO_OPER_FLOOD", "ON", 0, "Disable Flood Control for Operators" },
#else
  { "NO_OPER_FLOOD", "OFF", 0, "Disable Flood Control for Operators" },
#endif /* NO_OPER_FLOOD */

#ifdef NO_PRIORITY
  { "NO_PRIORITY", "ON", 0, "Do not Prioritize Socket File Descriptors" },
#else
  { "NO_PRIORITY", "OFF", 0, "Do not Prioritize Socket File Descriptors" },
#endif /* NO_PRIORITY */

#ifdef NOISY_HTM
  { "NOISY_HTM", "ON", 0, "Notify Operators of HTM (De)activation" },
#else
  { "NOISY_HTM", "OFF", 0, "Notify Operators of HTM (De)activation" },
#endif /* NOISY_HTM */

#ifdef OLD_Y_LIMIT
  { "OLD_Y_LIMIT", "ON", 0, "Use Old Y: line Limit Behavior" },
#else
  { "OLD_Y_LIMIT", "OFF", 0, "Use Old Y: line Limit Behavior" },
#endif /* OLD_Y_LIMIT */

#ifdef OPATH
  { "OPATH", OPATH, 0, "Path to Operator MOTD File" },
#else
  { "OPATH", "NONE", 0, "Path to Operator MOTD File" },
#endif /* OPATH */

#ifdef OPER_IDLE
  { "OPER_IDLE", "ON", 0, "Allow Operators to remain idle" },
#else
  { "OPER_IDLE", "OFF", 0, "Allow Operators to remain idle" },
#endif /* OPER_IDLE */

#ifdef ANTI_SPAMBOT
  { "OPER_SPAM_COUNTDOWN", "", OPER_SPAM_COUNTDOWN, "Anti SpamBot Parameter" },
#endif /* ANTI_SPAMBOT */

  { "PACE_WAIT", "", PACE_WAIT, "Minimum Delay between uses of certain commands" },

#ifdef PPATH
  { "PPATH", PPATH, 0, "Path to Pid File" },
#else
  { "PPATH", "NONE", 0, "Path to Pid File" },
#endif /* PPATH */

#ifdef PRESERVE_CHANNEL_ON_SPLIT
  { "PRESERVE_CHANNEL_ON_SPLIT", "ON", 0, "Preserve Channel Modes During a NetSplit" },
#else
  { "PRESERVE_CHANNEL_ON_SPLIT", "OFF", 0, "Preserve Channel Modes During a NetSplit" },
#endif /* PRESERVE_CHANNEL_ON_SPLIT */

#ifdef REJECT_HOLD
  { "REJECT_HOLD", "ON", 0, "Do not Dump a K-lined Client immediately" },
#else
  { "REJECT_HOLD", "OFF", 0, "Do not Dump a K-lined Client immediately" },
#endif /* REJECT_HOLE */

#ifdef REJECT_HOLD_TIME
  { "REJECT_HOLD_TIME", "", REJECT_HOLD_TIME, "Amount of Time to Hold a K-lined Client" },
#else
  { "REJECT_HOLD_TIME", "OFF", 0, "Amount of Time to Hold a K-lined Client" },
#endif /* REJECT_HOLD_TIME */

#ifdef REPORT_DLINE_TO_USER
  { "REPORT_DLINE_TO_USER", "ON", 0, "Inform Clients They are D-lined" },
#else
  { "REPORT_DLINE_TO_USER", "OFF", 0, "Inform Clients They are D-lined" },
#endif /* REPORT_DLINE_TO_USER */

#ifdef SEND_FAKE_KILL_TO_CLIENT
  { "SEND_FAKE_KILL_TO_CLIENT", "ON", 0, "Make Client think they were KILLed" },
#else
  { "SEND_FAKE_KILL_TO_CLIENT", "OFF", 0, "Make Client think they were KILLed" },
#endif /* SEND_FAKE_KILL_TO_CLIENT */

#ifdef SENDQ_ALWAYS
  { "SENDQ_ALWAYS", "ON", 0, "Put All OutBound data into a SendQ" },
#else
  { "SENDQ_ALWAYS", "OFF", 0, "Put All OutBound data into a SendQ" },
#endif /* SENDQ_ALWAYS */

#ifdef SEPARATE_QUOTE_KLINES_BY_DATE
  { "SEPARATE_QUOTE_KLINES_BY_DATE", "ON", 0, "Read/Write K-lines According to Date" },
#else
  { "SEPARATE_QUOTE_KLINES_BY_DATE", "OFF", 0, "Read/Write K-lines According to Date" },
#endif /* SEPARATE_QUOTE_KLINES_BY_DATE */

#ifdef SHORT_MOTD
  { "SHORT_MOTD", "ON", 0, "Notice Clients They should Read MOTD" },
#else
  { "SHORT_MOTD", "OFF", 0, "Notice Clients They should Read MOTD" },
#endif /* SHORT_MOTD */

#ifdef SHOW_FAILED_OPER_ID
  { "SHOW_FAILED_OPER_ID", "ON", 0, "Show Failed OPER Attempts due to Identity Mismatch" },
#else
  { "SHOW_FAILED_OPER_ID", "OFF", 0, "Show Failed OPER Attempts due to Identity Mismatch" },
#endif /* SHOW_FAILED_OPER_ID */

#ifdef SHOW_FAILED_OPER_PASSWD
  { "SHOW_FAILED_OPER_PASSWD", "ON", 0, "Show the Attempted OPER Password" },
#else
  { "SHOW_FAILED_OPER_PASSWD", "OFF", 0, "Show the Attempted OPER Password" },
#endif /* SHOW_FAILED_OPER_PASSWD */

#ifdef SHOW_INVISIBLE_LUSERS
  { "SHOW_INVISIBLE_LUSERS", "ON", 0, "Show Invisible Clients in LUSERS" },
#else
  { "SHOW_INVISIBLE_LUSERS", "OFF", 0, "Show Invisible Clients in LUSERS" },
#endif /* SHOW_INVISIBLE_LUSERS */

#ifdef SLAVE_SERVERS
  { "SLAVE_SERVERS", "ON", 0, "Send LOCOPS and K-lines to U: lined Servers" },
#else
  { "SLAVE_SERVERS", "OFF", 0, "Send LOCOPS and K-lines to U: lined Servers" },
#endif /* SLAVE_SERVERS */

#ifdef SPATH
  { "SPATH", SPATH, 0, "Path to Server Executable" },
#else
  { "SPATH", "NONE", 0, "Path to Server Executable" },
#endif /* SPATH */

#ifdef SPLIT_PONG
  { "SPLIT_PONG", "ON", 0, "Send a Special PING to Determine end of a NetSplit" },
#else
  { "SPLIT_PONG", "OFF", 0, "Send a Special PING to Determine end of a NetSplit" },
#endif /* SPLIT_PONG */

#ifdef SPLIT_SMALLNET_SIZE
  { "SPLIT_SMALLNET_SIZE", "", SPLIT_SMALLNET_SIZE, "Minimum Servers that Constitutes a NetSplit" },
#endif /* SPLIT_SMALLNET_SIZE */

#ifdef SPLIT_SMALLNET_USER_SIZE
  { "SPLIT_SMALLNET_USER_SIZE", "", SPLIT_SMALLNET_USER_SIZE, "Normal amount of Users" },
#endif /* SPLIT_SMALLNET_USER_SIZE */

#ifdef STATS_NOTICE
  { "STATS_NOTICE", "ON", 0, "Show Operators when a Client uses STATS" },
#else
  { "STATS_NOTICE", "OFF", 0, "Show Operators when a Client uses STATS" },
#endif /* STATS_NOTICE */

  { "TIMESEC", "", TIMESEC, "Time Interval to Wait Before Checking Pings" },

#ifdef TOPIC_INFO
  { "TOPIC_INFO", "ON", 0, "Show Who Set a Topic and When" },
#else
  { "TOPIC_INFO", "OFF", 0, "Show Who Set a Topic and When" },
#endif /* TOPIC_INFO */

  { "TS_MAX_DELTA", "", TS_MAX_DELTA, "Maximum Allowed TS Delta from another Server" },
  { "TS_WARN_DELTA", "", TS_WARN_DELTA, "Maximum TS Delta before Sending Warning" },

#ifdef USE_IP_I_LINE_FIRST
  { "USE_IP_I_LINE_FIRST", "ON", 0, "Search IP Ilines First" },
#else
  { "USE_IP_I_LINE_FIRST", "OFF", 0, "Search IP Ilines First" },
#endif /* USE_IP_I_LINE_FIRST */

#ifdef USE_RCS
  { "USE_RCS", "ON", 0, "Use \"ci\" to Keep RCS Control" },
#else
  { "USE_RCS", "OFF", 0, "Use \"ci\" to Keep RCS Control" },
#endif /* USE_RCS */

#ifdef USE_SYSLOG
  { "USE_SYSLOG", "ON", 0, "Log Errors to syslog file" },
#else
  { "USE_SYSLOG", "OFF", 0, "Log Errors to syslog file" },
#endif /* USE_SYSLOG */

#ifdef USE_UH
  { "USE_UH", "ON", 0, "Include user@host for BAN_INFO" },
#else
  { "USE_UH", "OFF", 0, "Include user@host for BAN_INFO" },
#endif /* USE_UH */

#ifdef WALLOPS_WAIT
  { "WALLOPS_WAIT", "", WALLOPS_WAIT, "Delay between successive uses of the WALLOPS command" },
#endif /* WALLOPS_WAIT */

#ifdef WARN_NO_NLINE
  { "WARN_NO_NLINE", "ON", 0, "Show Operators of Servers without an N: line" },
#else
  { "WARN_NO_NLINE", "OFF", 0, "Show Operators of Servers without an N: line" },
#endif /* WARN_NO_NLINE */

#ifdef WHOIS_NOTICE
  { "WHOIS_NOTICE", "ON", 0, "Show Operators when they are WHOIS'd" },
#else
  { "WHOIS_NOTICE", "OFF", 0, "Show Operators when they are WHOIS'd" },
#endif /* WHOIS_NOTICE */

  { "WHOIS_WAIT", "", WHOIS_WAIT, "Delay between Remote uses of WHOIS" },

#ifdef WINTRHAWK
  { "WINTRHAWK", "ON", 0, "Enable Wintrhawk Styling" },
#else
  { "WINTRHAWK", "OFF", 0, "Enable Wintrhawk Styling" },
#endif /* WINTRHAWK */

#ifdef ZIP_LEVEL
  { "ZIP_LEVEL", "", ZIP_LEVEL, "Compression Value for Zipped Links" },
#else
  { "ZIP_LEVEL", "NONE", 0, "Compression Value for Zipped Links" },
#endif /* ZIP_LEVEL */

#ifdef ZIP_LINKS
  { "ZIP_LINKS", "ON", 0, "Compress Server to Server Links" },
#else
  { "ZIP_LINKS", "OFF", 0, "Compress Server to Server Links" },
#endif /* ZIP_LINKS */

  /*
   * since we don't want to include the world here, NULL probably
   * isn't defined by the time we read this, just use plain 0 instead
   * 0 is guaranteed by the language to be assignable to ALL built
   * in types with the correct results.
   */
  { 0, 0, 0, 0 }
};


#endif /* DEFINE_M_INFO_DATA */
#endif /* INCLUDED_m_info_h */

