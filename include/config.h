/************************************************************************
 *   IRC - Internet Relay Chat, include/config.h
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
#ifndef INCLUDED_config_h
#define INCLUDED_config_h
#ifndef INCLUDED_setup_h
#include "setup.h"
#define INCLUDED_setup_h
#endif

/* PLEASE READ SECTION:
 *
 * I have commented out WHOIS_NOTICE and STATS_NOTICE
 * Personally, I feel opers do not need to know the information
 * returned by having those two defines defined, it is an invasion
 * of privacy. The original need/use of showing STATS_NOTICE
 * it seems to me, was to find stats flooders. They no longer
 * can do much damage with stats flooding, so why show it?
 * whois notice is just an invasion of privacy. Who cares?
 * I personally hope you agree with me, and leave these undef'ed
 * but at least you have been warned.
 *
 */

/***************** MAKE SURE THIS IS CORRECT!!!!!!!!! **************/
/* ONLY EDIT "HARD_FDLIMIT_" and "INIT_MAXCLIENTS" */

/* You may also need to hand edit the Makefile to increase
 * the value of FD_SETSIZE 
 */

/* These ultra low values pretty much guarantee the server will
 * come up initially, then you can increase these values to fit your
 * system limits. If you know what you are doing, increase them now
 */

#define HARD_FDLIMIT_   256
#define INIT_MAXCLIENTS 180

/*
 * This is how many 'buffer connections' we allow...
 * Remember, MAX_BUFFER + MAX_CLIENTS can't exceed HARD_FDLIMIT :)
 */
#define MAX_BUFFER      60

#if defined(__CYGWIN__)
#undef HARD_FDLIMIT_
#define HARD_FDLIMIT_   64
#undef INIT_MAXCLIENTS
#define INIT_MAXCLIENTS 20
#undef MAX_BUFFER
#define MAX_BUFFER      10
#endif /* __CYGWIN__ */

/* NICKNAMEHISTORYLENGTH - size of WHOWAS array
 * this defines the length of the nickname history.  each time a user changes
 * nickname or signs off, their old nickname is added to the top of the list.
 * NOTE: this is directly related to the amount of memory ircd will use whilst
 *       resident and running - it hardly ever gets swapped to disk!  Memory
 *       will be preallocated for the entire whowas array when ircd is started.
 *       You will want to crank this down if you are on a small net.
 */
#define NICKNAMEHISTORYLENGTH 15000

/* Don't change this... */
#define HARD_FDLIMIT    (HARD_FDLIMIT_ - 10)
#define MASTER_MAX      (HARD_FDLIMIT - MAX_BUFFER)
/*******************************************************************/

/* DPATH SPATH CPATH MPATH KPATH - directory and files locations
 * Full pathnames and defaults of IRC system's support files. Please note that
 * these are only the recommended names and paths. Change as needed.
 * You must define these to something, even if you don't really want them.
 *
 * DPATH = directory,
 * SPATH = server executable,
 * CPATH = conf file,
 * MPATH = MOTD
 * KPATH = kline conf file
 * DLPATH = dline conf file
 *
 * OMOTD = path to MOTD for opers
 * 
 * For /restart to work, SPATH needs to be a full pathname
 * (unless "." is in your exec path). -Rodder
 * Leave KPATH undefined if you want klines in main conf file.
 * HPATH is the opers help file, seen by opers on /quote help.
 * -Dianora
 *
 * DPATH must have a trailing /
 * Do not use ~'s in any of these paths
 *
 */

#define DPATH   "/usr/local/ircd/"
#define SPATH   "/usr/local/ircd/ircd"
#define CPATH   "ircd.conf"
#define KPATH   "kline.conf"
#define DLPATH  "kline.conf"
#define MPATH   "ircd.motd"
#define LPATH   "ircd.log"
#define PPATH   "ircd.pid"
#define HPATH   "opers.txt"
#define OPATH   "opers.motd"

/* HIDE_OPS
 * Define this to prevent non chanops from seeing what ops a channel has
 * NOT ADEQUATELY TESTED YET, DON'T USE ON PRODUCTION NETWORK --Rodder
 */
/* #undef HIDE_OPS */

/* SERVERHIDE
 * Define this to prevent users from seeing what server a user is on.
 * It also hide IP address in STATS commands and various notices, disables
 * TRACE and LTRACE, and prevents hunting by nickname for non-opers.
 * NOT ADEQUATELY TESTED YET, DON'T USE ON PRODUCTION NETWORK --Rodder
 */
/* #undef SERVERHIDE */

/* HIDE_SERVERS_IPS
 * Define this to prevent opers from seeing the IP of a server.
 * This will not show IPs of any server to anyone, to protect hidden
 * hubs from untrustable opers.
 */
#define HIDE_SERVERS_IPS

/* NETWORK_NAME
 * Displayed in place of the servername when SERVERHIDE is enabled
 */   
#define NETWORK_NAME "EFnet"

/* NETWORK_DESC
 * Displayed in place of the server info when SERVERHIDE is enabled
 */
#define NETWORK_DESC "Eris Free Network"


/* TS_MAX_DELTA and TS_WARN_DELTA -  allowed delta for TS when another
 * server connects.
 *
 * If the difference between my clock and the other server's clock is
 * greater than TS_MAX_DELTA, I send out a warning and drop the links.
 * 
 * If the difference is less than TS_MAX_DELTA, I just sends out a warning
 * but don't drop the link.
 *
 * TS_MAX_DELTA currently set to 30 minutes to deal with older timedelta
 * implementation.  Once pre-hybrid5.2 servers are eradicated, we can drop this
 * down to 90 seconds or so. --Rodder
 */
#define TS_MAX_DELTA 300        /* seconds */
#define TS_WARN_DELTA 30        /* seconds */

/* SLAVE_SERVERS - Use this to send LOCOPS and KLINES to servers you define
 * uses U: lines in ircd.conf, each server defined in an U: line
 * is sent a copy of the locally placed K-line, and will also
 * accept K-lines from those servers.
 * This is useful for sites with more than one client server.
 */
#undef SLAVE_SERVERS

/* FNAME_USERLOG and FNAME_OPERLOG - logs of local USERS and OPERS
 * Define this filename to maintain a list of persons who log
 * into this server. Logging will stop when the file does not exist.
 * Logging will be disable also if you do not define this.
 * FNAME_USERLOG just logs user connections, FNAME_OPERLOG logs every
 * successful use of /oper.  These are either full paths or files within DPATH.
 *
 * These need to be defined if you want to use SYSLOG logging, too.
 */
#define FNAME_USERLOG "/usr/local/ircd/users"
#define FNAME_OPERLOG "/usr/local/ircd/opers"

/* RFC1035_ANAL
 * Defining this causes ircd to reject hostnames with non-compliant chars.
 * undef'ing it will allow hostnames with _ or / to connect
 */
#define RFC1035_ANAL

/* ALLOW_DOT_IN_IDENT
 * Defining this will allow periods in ident replies.  Use of this is
 * strongly discouraged on public networks
 */
#undef ALLOW_DOT_IN_IDENT

/* MAX_MULTI_MESSAGES
 * Maximum number of recipients to a PRIVMSG.  Any more than MAX_MULTI_MESSAGES
 * will not be sent.  If MAX_MULTI_MESSAGES is 1, then any PRIVMSG with a ',' in
 * the target will be rejected
 */
#define MAX_MULTI_MESSAGES 2

/* NO_DUPE_MULTI_MESSAGES
 * Define this to check for duplicate recipients in PRIVMSG, at the expense
 * of noticeable CPU cycles.
 */
#define NO_DUPE_MULTI_MESSAGES

/* WARN_NO_NLINE
 * Define this if you want ops to get noticed about "things" trying to
 * connect as servers that don't have N: lines.  Twits with misconfigured
 * servers can get really annoying with this enabled.
 */
#define WARN_NO_NLINE

/* CUSTOM_ERR - colorful notice/error/messages
 * Defining this will use custom notice/error/messages from
 * src/messages.tab instead of stock ones in src/messages_cust.tab
 * If you prefer the "colorful" messages that Hybrid was known for,
 * or if you wish to customize the  messages, define this.
 * Otherwise leave it undef'd for plain ole boring messages.
 */
#undef CUSTOM_ERR

/* FAILED_OPER_NOTICE - send a notice to all opers when someone
 * tries to /oper and uses an incorrect password.
 */
#define FAILED_OPER_NOTICE

/* SHOW_FAILED_OPER_ID - if FAILED_OPER_NOTICE is defined, also notify when
 * a client fails to oper because of a identity mismatch (wrong host or nick)
 */
#undef SHOW_FAILED_OPER_ID

/* SHOW_FAILED_OPER_PASSWD - if FAILED_OPER_NOTICE is defined, also show the
 * attempted passwd
 */
#undef SHOW_FAILED_OPER_PASSWD

/* CLIENT_SERVER - Don't be so fascist about idle clients ;)
 * changes behaviour of HTM code to make clients lag less.
 */
#define CLIENT_SERVER

/* BAN_INFO - Shows you who and when someone did a ban
 */
#define BAN_INFO

/* USE_UH - include user@host for BAN_INFO
 * define this if you want to use n!u@h for BAN_INFO
 */
#define USE_UH

/* TOPIC_INFO - Shows you who and when someone set the topic
 */
#define TOPIC_INFO

/* ANTI_NICK_FLOOD - prevents nick flooding
 * define if you want to block local clients from nickflooding
 */
#define ANTI_NICK_FLOOD
/* defaults allow 5 nick changes in 20 seconds */
#define MAX_NICK_TIME 20
#define MAX_NICK_CHANGES 5 

/* DO_IDENTD - check identd
 * if you undefine this, ircd will never check identd regardless of
 * @'s in I:lines.  You must still use @'s in your I: lines to get
 * ircd to do ident lookup even if you define this.
 */
#define DO_IDENTD

/* KLINE_WITH_REASON - show comment to client on exit
 * define this if you want users to exit with the kline/dline reason
 * (i.e. instead of "You have been K-lined" they will see the reason
 * and to see the kline/dline reason when they try to connect
 * It's a neat feature except for one thing... If you use a TCM
 * and it shows the nick of the oper doing the kline (as it does by default)
 * Your opers can be hit with retaliation... Or if your opers use
 * scripts that stick an ID into the comment field. etc. It's up to you
 * whether you want to use it or not.
 */
#define KLINE_WITH_REASON

/*
 * If KLINE_WITH_CONNECTION_CLOSED is defined and KLINE_WITH_REASON
 * above is undefined then the signoff reason will be "Connection
 * closed". This prevents other users seeing the client disconnect
 * from harassing the IRCops.
 * However, the client will still see the real reason upon connect attempts.
 */
#define KLINE_WITH_CONNECTION_CLOSED

/* NON_REDUNDANT_KLINES - If you want the server to flag and not apply
 * redundant klines
 */
#define NON_REDUNDANT_KLINES

/* BOTCHECK - rudimentary bot checking
 */
#define BOTCHECK

/* x_LINES_OPER_ONLY - Allow only local opers to see these stats
 *
 *  Any one with an F line can almost always get on the server, as
 *  some file descriptors are reserved for people with this F line
 *  especially useful for your opers
 *
 *  Note that B, E, and F lines are no longer explicit lines in the
 *  ircd.conf file, but rather flags in the I line.  The
 *  B/E/F_LINES_OPER_ONLY defines block non-opers from seeing I: lines
 *  with those flags
 *
 *  P_LINES_OPER_ONLY blocks STATS P (Capital P, NOT lower case p) from
 *  non-opers, denying them permission to see configured ports
 *
 *  U_LINES_OPER_ONLY only applies to STATS U (capital U, not lowercase).
 *  U-lines are used to share K/D-lines between servers.
 */
#define B_LINES_OPER_ONLY
#define E_LINES_OPER_ONLY
#define F_LINES_OPER_ONLY
#undef  I_LINES_OPER_ONLY
#undef  K_LINES_OPER_ONLY
#undef  O_LINES_OPER_ONLY
#define P_LINES_OPER_ONLY
#undef  U_LINES_OPER_ONLY

/* SPOOF_NOTICE - See a notice when a user connects with a
 * spoofed I: line
 * err, this also will now not show spoofed i-lines to opers who
 * /stats i.
 */
#define SPOOF_NOTICE

/* STATS_NOTICE - See a notice when a user does a /stats
 *
 * This is a violation of privacy.  Arguments for this have been
 * seeing STATS p requests, look below for the answer to that
 * argument.
 */
#undef STATS_NOTICE

/* STATS_P_NOTICE - See STATS p requests only, when STATS_NOTICE
 * is undefined.  This allows opers to see requests by users for
 * assistance, while not violating their privacy by spying on other
 * STATS requests.
 */
#define STATS_P_NOTICE

/* WHOIS_NOTICE - Shows a notice to an oper when a user does a
 * /whois on them
 * Why do opers need this at all? Its an invasion of privacy. bah.
 * you don't need this. -Dianora
 */
#undef WHOIS_NOTICE

/* WHOIS_WAIT - minimum seconds between remote use of WHOIS before
 * max use count is reset
 */
#define WHOIS_WAIT 1

/* PACE_WAIT - minimum seconds between use of MOTD, INFO, HELP, LINKS, TRACE
 * -Dianora
 */
#define PACE_WAIT 10

/* KNOCK_DELAY 5 minutes per each KNOCK should be enough
 */
#define KNOCK_DELAY 300

/* If you are an admin that does not think operwall/wallops
 * should be used instead of a channel, define this.
 * Undefine PACE_WALLOPS to disable
 */
#define PACE_WALLOPS
#define WALLOPS_WAIT 10 

/* SHORT_MOTD
 * There are client ignoring the FORCE_MOTD MOTD numeric, there is
 * no point forcing MOTD on connecting clients IMO. Give them a short
 * NOTICE telling them they should read the motd, and leave it at that.
 */
#undef SHORT_MOTD

/* NO_OPER_FLOOD - disable flood control for opers
 * define this to remove flood control for opers
 */
#define NO_OPER_FLOOD

/* TRUE_NO_OPER_FLOOD - absolutely remove all flood
 * control for opered clients. for this to work, the above define
 * must be enabled as well. typically we delay oper floods after
 * an initial burst, this removes such a delay. BE CAREFUL WITH
 * THIS.
 */
#undef TRUE_NO_OPER_FLOOD

/* CRYPT_LINKS - Enable encrypted links between one server and another by
 * use of a defineable password prefix. Requires the other defines below.
 */
/* Warning: As of 8/7/2001 this is not allowed on EFnet, and it was
 * implemented due to confusion relating to what was approved by the CFV
 * -A1kmm */
#undef CRYPT_LINKS

#ifndef CRYPTLINKS
/* A less CPU intensive alternative to cryptlinks... */
#define USE_KSERVER
#endif

/* CRYPT_LINKS_CNPREFIX - This is the prefix that will go in a c/n password
 * field to indicate this will be an encrypted link.
 */
#define CRYPT_LINKS_CNPREFIX '@'
/* KSERVER_CNPREFIX - The same but for c/n password field for KSERVERs.
 */
#define KSERVER_CNPREFIX '%'

/* CRYPT_LINKS_CIPHERPREFIX - This is the character that delimits the public
 * key filename for a server and the prefered crypt preference.
 */
#define CRYPT_LINKS_CIPHERPREFIX ','

/* CRYPT_LINKS_PRIVATEKEYFILE - This is a filename of the RSA private key
 * for this binary. There is no configuration option that will override this,
 * so this will be binary-specific. This is deprecated. The filename now
 * defaults to servername.key if this is left undefined.
 * NOTE: LEAVE UNDEF IF YOU WANT THE DEFAULT BEHAVIOUR (you do)
 #define CRYPT_LINKS_PRIVATEKEYFILE "private.key"
 */

/* USE_EGD -  if your system does not have *random devices and you want to
 * be able to use OpenSSL and CRYPT_LINKS, define this and option below if
 * necessary. Beware - EGD is *very* CPU intensitive process when
 * gathering data for its pool. */
#undef USE_EGD

/* EGD_POOL - define location of EGD pool. Not necessary for OpenSSL >=
 * 0.9.7 since it will automagically check locations. */
#undef EGD_POOL "/var/run/egd-pool"

/* SHOW_INVISIBLE_LUSERS - show invisible clients in LUSERS
 * As defined this will show the correct invisible count for anyone who does
 * LUSERS on your server. On a large net this doesnt mean much, but on a
 * small net it might be an advantage to undefine it.
 */
#define SHOW_INVISIBLE_LUSERS

/* ZIP_LINKS - Compress server-to-server links
 * Use c: lines in the conf to specify a zipped connection.
 *
 * Note that you may have to increase your sendQ size between server
 * if you have problems during particularly heavy bursts
 */
#define ZIP_LINKS

/* NO_DEFAULT_INVISIBLE - clients not +i by default
 * When defined, your users will not automatically be attributed with user
 * mode "i" (i == invisible). Invisibility means people dont showup in
 * WHO or NAMES unless they are on the same channel as you.
 */
#undef  NO_DEFAULT_INVISIBLE

/*
 * The compression level used for zipped links. (Suggested values: 1 to 5)
 * Above 4 will only give a rather marginal increase in compression for a
 * large increase in CPU usage.
 */
#define ZIP_LEVEL       2

/*
 * OPER_UMODES LOCOP_UMODES - set these to be the initial umodes when OPER'ing
 * These can be over-ridden in ircd.conf file, with flags in last O field
 */
#define OPER_UMODES   (FLAGS_OPER|FLAGS_WALLOP|FLAGS_SERVNOTICE|FLAGS_OPERWALL)
#define LOCOP_UMODES   (FLAGS_LOCOP|FLAGS_WALLOP|FLAGS_SERVNOTICE)

/* MAXIMUM LINKS - max links for class 0 if no Y: line configured
 *
 * This value is only used if you don't have server classes defined, and
 * a server is in class 0 (the default class if none is set).
 *
 * The Hybrid team STRONGLY recommends configuring proper Y: lines
 *
 */
#define MAXIMUM_LINKS 1

/* HUB - enable server-server routing
 * If your server is running as a a HUB Server then define this.
 * A HUB Server has many servers connect to it at the same as opposed
 * to a leaf which just has 1 server (typically the uplink). Define this
 * correctly for performance reasons.
 */
#undef  HUB

/* CMDLINE_CONFIG - allow conf-file to be specified on command line
 * NOTE: defining CMDLINE_CONFIG and installing ircd SUID or SGID is a MAJOR
 * security problem - they can use the "-f" option to read any files
 * that the 'new' access lets them.
 */
#define CMDLINE_CONFIG

/* INIT_LOG_LEVEL - what level of information is logged to ircd.log
 * options are:
 *   L_CRIT, L_ERROR, L_WARN, L_NOTICE, L_TRACE, L_INFO, L_DEBUG
 */
#define INIT_LOG_LEVEL L_NOTICE

/* USE_LOGFILE - log errors and such to LPATH
 * If you wish to have the server send 'vital' messages about server
 * to a logfile, define USE_LOGFILE.
 */
#define USE_LOGFILE

/* USE_SYSLOG - log errors and such to syslog()
 * If you wish to have the server send 'vital' messages about server
 * through syslog, define USE_SYSLOG. Only system errors and events critical
 * to the server are logged although if this is defined with FNAME_USERLOG,
 * syslog() is used instead of the above file. It is not recommended that
 * this option is used unless you tell the system administrator beforehand
 * and obtain their permission to send messages to the system log files.
 */
#undef USE_SYSLOG

#ifdef  USE_SYSLOG
/* SYSLOG_KILL SYSLOG_SQUIT SYSLOG_CONNECT SYSLOG_USERS SYSLOG_OPER
 * If you use syslog above, you may want to turn some (none) of the
 * spurious log messages for KILL,SQUIT,etc off.
 */
#undef  SYSLOG_KILL     /* log all operator kills to syslog */
#undef  SYSLOG_SQUIT    /* log all remote squits for all servers to syslog */
#undef  SYSLOG_CONNECT  /* log remote connect messages for other all servs */
#undef  SYSLOG_USERS    /* send userlog stuff to syslog */
#undef  SYSLOG_OPER     /* log all users who successfully become an Op */
#undef  SYSLOG_BLOCK_ALLOCATOR /* debug block allocator */

/* LOG_FACILITY - facility to use for syslog()
 * Define the facility you want to use for syslog().  Ask your
 * sysadmin which one you should use.
 */
#define LOG_FACILITY LOG_LOCAL4

#endif /* USE_SYSLOG */

/* CRYPT_OPER_PASSWORD - use crypted oper passwords in the ircd.conf
 * define this if you want to use crypted passwords for operators in your
 * ircd.conf file.
 */
#define CRYPT_OPER_PASSWORD

/* CRYPT_LINK_PASSWORD - use crypted N-line passwords in the ircd.conf
 * If you want to store encrypted passwords in N-lines for server links,
 * define this.  For a C/N pair in your ircd.conf file, the password
 * need not be the same for both, as long as the opposite end has the
 * right password in the opposite line.
 */
#undef  CRYPT_LINK_PASSWORD

/* IDLE_FROM_MSG - Idle-time reset only from privmsg
 * Idle-time reset only from privmsg, if undefined idle-time
 * is reset from everything except ping/pong.
 * 
 */
#define IDLE_FROM_MSG

/* MAXSENDQLENGTH - Max amount of internal send buffering
 * Max amount of internal send buffering when socket is stuck (bytes)
 */
#define MAXSENDQLENGTH 9000000    /* Recommended value: 9000000 for EFnet */

/*  BUFFERPOOL - the maximum size of the total of all sendq's.
 *  Recommended value is 4 times MAXSENDQLENGTH.
 */
#define BUFFERPOOL (MAXSENDQLENGTH * 4)

/* IRC_UID IRC_GID - user and group id ircd should switch to if run as root
 * If you start the server as root but wish to have it run as another user,
 * define IRC_UID to that UID.  This should only be defined if you are running
 * as root and even then perhaps not.
 */
#define IRC_UID 1001
#define IRC_GID 31

/* CLIENT_FLOOD - client excess flood threshold
 * this controls the number of bytes the server will allow a client to
 * send to the server without processing before disconnecting the client for
 * flooding it.  Values greater than 8000 make no difference to the server.
 */
#define CLIENT_FLOOD    2560

/* NOISY_HTM - should HTM be noisy by default
 * should be YES or NO
 */
#define NOISY_HTM YES

/*
 * define either NO_CHANOPS_ON_SPLIT or NO_JOIN_ON_SPLIT
 *
 * choose =one= only or undef on small networks
 *
 */

/* NO_CHANOPS_ON_SPLIT
 * When this is defined, users will not be chanopped on empty channels
 * if there are no servers presently connected to this server
 * opers are not affected. 
 */
#undef NO_CHANOPS_ON_SPLIT

/* 
 * NO_JOIN_ON_SPLIT
 * 
 * When this is defined, users will not be allowed to join channels
 * while the server is split.
 */
#undef NO_JOIN_ON_SPLIT

/*
 * SPLIT_SMALLNET_SIZE defines what constitutes a split from 
 * the net. for a leaf, 5 is fine. If the number of servers seen
 * on the net gets less than 5, a split is deemed to have happened.
 */
#define SPLIT_SMALLNET_SIZE 5

/*
 * SPLIT_SMALLNET_USER_SIZE defines how many global users on the
 * net constitute a "normal" net size. It's used in conjunction
 * with SPLIT_SMALLNET_SIZE to help determine the end of a split.
 * if number of server seen on net > SPLIT_SMALLNET_SIZE &&
 * number of users seen on net > SPLIT_SMALLNET_USER_SIZE start
 * counting down the SERVER_SPLIT_RECOVERY_TIME
 */
#define SPLIT_SMALLNET_USER_SIZE 15000

/*
 * SPLIT_PONG will send a PING to a server after the connect burst.
 * It will stay in "split" mode until it receives a PONG in addition
 * to meeting the other conditions.  This is very useful for true
 * leafs, less useful for "clustered" servers.  If this is enabled,
 * you should be able to crank DEFAULT_SERVER_SPLIT_RECOVERY_TIME
 * down to 1.
 */
#define SPLIT_PONG

/*
 * DEFAULT_SERVER_SPLIT_RECOVERY_TIME - determines how long to delay split
 * status after resyncing
 */
#define DEFAULT_SERVER_SPLIT_RECOVERY_TIME 1

/* LIMIT_UH
 * If this is defined, Y line limit is made against the actual
 * username not the ip. i.e. if you limit the connect frequency line
 * to 1, that allows only 1 username to connect instead of 1 client per ip
 * i.e. you can have 10 clients all with different usernames, but each user
 * can only connect once. Each non-idented client counts as the same user
 * i.e. ~a and ~b result in a count of two.
 *
 */
#undef LIMIT_UH

/* IDLE_CHECK
 * If this is defined, each client is checked for excessive idleness
 * This adds some CPU... you might not want to use this on a large server.
 * However, if defined, and a client is discovered idling more than
 * IDLE_TIME minutes, it is t-klined for 1 minute to discourage
 * reconnects. The idle time is settable via /quote set
 * /quote set idletime
 *
 * -Dianora
 *
 * OPER_IDLE allows operators to remain idle when they idle
 * beyond the idle limit
 */
#undef  IDLE_CHECK
#define IDLE_TIME 60
#define OPER_IDLE

/* SEND_FAKE_KILL_TO_CLIENT - make the client think it's being /KILL'ed
 * 
 * This was originally intended to prevent clients from reconnecting to the
 * server after being dropped for idleness.  It can probably be used for
 * other events too.
 *
 * This really only works if the
 * client was compiled with QUIT_ON_OPERATOR_KILL which was mandatory policy
 * on UMich.Edu hosts.
 */
#define SEND_FAKE_KILL_TO_CLIENT
 
/*
 * Limited Trace - Reports only link and oper traces even when O:line is
 * active.
 *
 * Displays only Oper, Serv, Link, and Class reports even if the O-line is
 * active.  Useful for just showing pertinent info of a specific server. 
 * Note however that if the target server is not running this option then
 * you will still receive a normal trace output. 
 */
#define LTRACE

/*
 * LWALLOPS - Like wallops but only local.
 *
 * This is actually a compatibility command which really calls m_locops().
 */
#define LWALLOPS

/*
 * Define this to enable WintrHawk "styling"
 * Currently this shows the actual quit message on the Client exiting 
 * notice, rather than the error or "Client Quit"
 */
#define WINTRHAWK

/*
 * comstud and I have both noted that Solaris 2.5 at least, takes a hissy
 * fit if you don't read a FD that becomes ready right away. Unfortunately
 * the dog3 priority code relies upon not having to read a ready FD right away.
 * If you have HTM mode set low as it is normally, the server will
 * eventually grind to a halt.
 * Don't complain if Solaris lags if you don't define this. I warned you.
 *
 * -Dianora
 */

#undef NO_PRIORITY

/* LITTLE_I_LINE support
 * clients with a little i instead of an I in their I line
 * can be chanopped, but cannot chanop anyone else.
 */
#define LITTLE_I_LINES

/* -----------------  approved on EFnet section -------------------- 
 *
 * if you are not an efnet admin, you might want to undefine some of
 * these.. -db
 */

/* If this is defined, hack ops is simply not allowed at all
 */
#define NO_HACK_OPS

/* ----------------- not approved on EFnet section -------------------- 
 *
 * if you are not an efnet admin, you might want to define some of
 * these.. --fl_
 */

/* GLINES - global Kline-like bans
 * Define this if you want GLINE support
 * when this is defined, 3 completely different opers from
 * three different servers must do the identical GLINE in order
 * for the G line to take effect.
 */
#undef GLINES
#define GLINEFILE       "gline.log"

/* GLINE_TIME - local expire time for GLINES
 * As configured here, a GLINE will last 12 hours
 */
#define GLINE_TIME      (12*3600)

/* NO_FAKE_GLINES - disable fake glines
 * Define this if you want to stop fake glines from users who
 * dont exist.  This could possibly have an impact with services,
 * so its #undef'd by default.
 */
#undef NO_FAKE_GLINES

/* Ignore bogus timestamps from other servers. Yes this will desync
 * the network, but it will allow chanops to resync with a valid non TS 0
 */
#undef IGNORE_BOGUS_TS

/* CHANMODE_E
 * /mode #channel +e allows you to add exceptions to bans.
 * very useful if a friend has to use an aol.com account for a few days.
 * e.g. You can continue to ban *.aol.com but allow an exception for them.
 *
 * Note, +e will NEVER be sent to a server that does not understand it.
 *
 * +e is supported by dalnet, ircnet, undernet but not EFNet atm. (june 9
 * 2001)
 */
/* If you are not on EFNet it is recommended you define this.. --fl_ */
/* #undef'ing this will NOT stop modes passing through your server,
 * it will only stop them being used/set on your server.  This is to
 * stop server "+e patches" and desyncs across a hub where leaf servers
 * +e lists differ.   --fl_
 */
#undef CHANMODE_E

/* USE_KNOCK
 * KNOCK allows users to send a "knock" to a +i channel.
 */
#undef USE_KNOCK

/* -------------- END NOT APPROVED ON EFNET SECTION --------------------- */


/*   STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP STOP  */

/* You shouldn't change anything below this line, unless absolutely needed. */

/* INITIAL_DBUFS - how many dbufs to preallocate
 */
#define INITIAL_DBUFS 4000 /* preallocate 16 megs of dbufs */ 

/* MAXBUFFERS - increase socket buffers
 *
 * Increase send & receive socket buffer up to 64k,
 * keeps clients at 8K and only raises servers to 64K
 */
#define MAXBUFFERS

/* PORTNUM - default port that ircd uses to connect to remote servers, if
 * a port is not specified in the M: line.
 */
#define PORTNUM 6667

/* MAXCONNECTIONS - don't touch - change the HARD_FDLIMIT_ instead
 * Maximum number of network connections your server will allow.  This should
 * never exceed max. number of open file descriptors and wont increase this.
 */
/* change the HARD_FDLIMIT_ instead */
#define MAXCONNECTIONS  HARD_FDLIMIT

/* TIMESEC - Time interval to wait and if no messages have been received,
 * then check for PINGFREQUENCY and CONNECTFREQUENCY 
 */
#define TIMESEC  5              /* Recommended value: 5 */

/* PINGFREQUENCY - ping frequency for idle connections
 * If daemon doesn't receive anything from any of its links within
 * PINGFREQUENCY seconds, then the server will attempt to check for
 * an active link with a PING message. If no reply is received within
 * (PINGFREQUENCY * 2) seconds, then the connection will be closed.
 *
 * This only specifies the default for class 0, only used for lines
 * not specifying a class, or specifying an invalid class
 */
#define PINGFREQUENCY    120    /* Recommended value: 120 */

/* CONNECTFREQUENCY - time to wait before auto-reconnecting
 * If the connection to to uphost is down, then attempt to reconnect every 
 * CONNECTFREQUENCY  seconds.
 *
 * This only specifies the default for class 0
 */
#define CONNECTFREQUENCY 600    /* Recommended value: 600 */

/* HANGONGOODLINK and HANGONRETRYDELAY
 * Often the net breaks for a short time and it's useful to try to
 * establishing the same connection again faster than CONNECTFREQUENCY
 * would allow. But, to keep trying on bad connection, we require
 * that connection has been open for certain minimum time
 * (HANGONGOODLINK) and we give the net few seconds to steady
 * (HANGONRETRYDELAY). This latter has to be long enough that the
 * other end of the connection has time to notice it broke too.
 * 1997/09/18 recommended values by ThemBones for modern EFnet
 */

#define HANGONGOODLINK 3600     /* Recommended value: 30-60 minutes */
#define HANGONRETRYDELAY 60     /* Recommended value: 30-60 seconds */

/* WRITEWAITDELAY - Number of seconds to wait for write to
 * complete if stuck.
 */
#define WRITEWAITDELAY     15   /* Recommended value: 15 */

/* CONNECTTIMEOUT -
 * Number of seconds to wait for a connect(2) call to complete.
 * NOTE: this must be at *LEAST* 10.  When a client connects, it has
 * CONNECTTIMEOUT - 10 seconds for its host to respond to an ident lookup
 * query and for a DNS answer to be retrieved.
 */
#define CONNECTTIMEOUT  30      /* Recommended value: 30 */

/* KILLCHASETIMELIMIT -
 * Max time from the nickname change that still causes KILL
 * automatically to switch for the current nick of that user. (seconds)
 */
#define KILLCHASETIMELIMIT 90   /* Recommended value: 90 */

/* MAXCHANNELSPERUSER -
 * Max number of channels a user is allowed to join.
 */
#define MAXCHANNELSPERUSER  20  /* Recommended value: 20 */

/* SENDQ_ALWAYS - should always be defined.
 * SendQ-Always causes the server to put all outbound data into the sendq and
 * flushing the sendq at the end of input processing. This should cause more
 * efficient write's to be made to the network.
 * There *shouldn't* be any problems with this method.
 * -avalon
 */
#define SENDQ_ALWAYS

/* FLUD - CTCP Flood Detection and Protection
 * 
 * This enables server CTCP flood detection and protection for local clients.
 * It works well against fludnets and flood clones.  The effect of this code
 * on server CPU and memory usage is minimal, however you may not wish to
 * take the risk, or be fundamentally opposed to checking the contents of
 * PRIVMSG's (though no privacy is breached).  This code is not useful for
 * routing only servers (ie, HUB's with little or no local client base), and
 * the hybrid team strongly recommends that you do not use FLUD with HUB.
 * The following default thresholds may be tweaked, but these seem to work
 * well.
 */
#define FLUD

/* ANTI_DRONE_FLOOD - anti flooding code for drones
 * This code adds server side ignore for a client who gets
 * messaged more than drone_count times within drone_time seconds
 * unfortunately, its a great DOS, but at least the client won't flood off.
 * I have no idea what to use for values here, trying 8 privmsgs
 * within 1 seconds. (I'm told it is usually that fast)
 * I'll do better next time, this is a Q&D -Dianora
 */
#define ANTI_DRONE_FLOOD
#define DEFAULT_DRONE_TIME 1
#define DEFAULT_DRONE_COUNT 8

/* JUPE_CHANNEL - jupes a channel from being joined on this server only
 * if added to Q lines e.g. Q:\#packet_channel:Tired of packets
 */
#undef JUPE_CHANNEL

/* 
 * ANTI_SPAMBOT
 * if ANTI_SPAMBOT is defined try to discourage spambots
 * The defaults =should= be fine for the timers/counters etc.
 * but you can play with them. -Dianora
 *
 * Defining this also does a quick check whether the client sends
 * us a "user foo x x :foo" where x is just a single char.  More
 * often than not, it's a bot if it did. -ThemBones
 */
#define ANTI_SPAMBOT

/* ANTI_SPAMBOT parameters, don't touch these if you don't
 * understand what is going on.
 *
 * if a client joins MAX_JOIN_LEAVE_COUNT channels in a row,
 * but spends less than MIN_JOIN_LEAVE_TIME seconds
 * on each one, flag it as a possible spambot.
 * disable JOIN for it and PRIVMSG but give no indication to the client
 * that this is happening.
 * every time it tries to JOIN OPER_SPAM_COUNTDOWN times, flag
 * all opers on local server.
 * If a client doesn't LEAVE a channel for at least 2 minutes
 * the join/leave counter is decremented each time a LEAVE is done
 *
 */
#define MIN_JOIN_LEAVE_TIME  60
#define MAX_JOIN_LEAVE_COUNT  25
#define OPER_SPAM_COUNTDOWN   5 
#define JOIN_LEAVE_COUNT_EXPIRE_TIME 120

/*
 * If ANTI_SPAMBOT_WARN_ONLY is #define'd 
 * Warn opers about possible spambots only, do not disable
 * JOIN and PRIVMSG if possible spambot is noticed
 * Depends on your policies.
 */
#undef ANTI_SPAMBOT_WARN_ONLY

/* ANTI_SPAM_EXIT_MESSAGE
 *
 * If this is defined, do not allow the clients exit message to be
 * sent to a channel if the client has been on for less than
 * ANTI_SPAM_EXIT_MESSAGE_TIME.
 * The idea is, some spambots exit with their spam, thus advertising
 * this way.
 * (idea due to ThaDragon, I just couldn't find =his= code)
 * - Dianora
 */
#undef ANTI_SPAM_EXIT_MESSAGE
/* 300 is five minutes, seems reasonable */
#define ANTI_SPAM_EXIT_MESSAGE_TIME 300

#ifdef FLUD
#define FLUD_NUM        4       /* Number of flud messages to trip alarm */
#define FLUD_TIME       3       /* Seconds in which FLUD_NUM msgs must occur */
#define FLUD_BLOCK      15      /* Seconds to block fluds */
#endif

/* REJECT_HOLD 
 * clients that reconnect but are k-lined will have their connections
 * "held" for REJECT_HOLD_TIME seconds, they cannot PRIVMSG. The idea
 * is to keep a reconnecting client from forcing the ircd to re-scan
 * mtrie_conf.
 *
 */
  
#undef REJECT_HOLD
#define REJECT_HOLD_TIME 30 

/* maximum number of fd's that will be used for reject holding */
#define REJECT_HELD_MAX 25

/*
 * OLD_Y_LIMIT
 *
 * #define this if you prefer the old behaviour of I lines
 * the default behaviour is to limit the total number of clients
 * using the max client limit in the corresponding Y line (class)
 * The old behaviour was to limit the client count per I line
 * without regard to the total class limit. Each have advantages
 * and disadvantages. In an open I line server, the default behaviour
 * i.e. #undef OLD_Y_LIMIT makes more sense, because you can limit
 * the total number of clients in a class. In a closed I line server
 * The old behaviour can make more sense.
 *
 * -Dianora
*/
#undef OLD_Y_LIMIT

/*
 * If the OS has SOMAXCONN use that value, otherwise
 * Use the value in HYBRID_SOMAXCONN for the listen(); backlog
 * try 5 or 25. 5 for AIX and SUNOS, 25 should work better for other OS's
*/
#define HYBRID_SOMAXCONN 25

/* DEBUGMODE is used mostly for internal development, it is likely
 * to make your client server very sluggish.
 * You usually shouldn't need this. -Dianora
*/
#undef DEBUGMODE               /* define DEBUGMODE to enable debugging mode.*/

/* ------------------------- END CONFIGURATION SECTION -------------------- */
#define MAX_CLIENTS INIT_MAXCLIENTS

#if defined(CLIENT_FLOOD) && ((CLIENT_FLOOD > 8000) || (CLIENT_FLOOD < 512))
#error CLIENT_FLOOD needs redefining.
#endif

#if !defined(CLIENT_FLOOD)
#error CLIENT_FLOOD undefined.
#endif

#if defined(DEBUGMODE)
#  define Debug(x) debug x
#  define LOGFILE LPATH
#else
#  define Debug(x) ;
#  define LOGFILE "/dev/null"
#endif

#define REPORT_DLINE_TO_USER

#if defined(NO_CHANOPS_ON_SPLIT) || defined(NO_JOIN_ON_SPLIT)
#define NEED_SPLITCODE
#endif

#ifdef FLUD
void    free_fluders();
void    free_fludees();
#endif /* FLUD */

#ifdef ANTI_SPAMBOT
#define MIN_SPAM_NUM 5
#define MIN_SPAM_TIME 60
#endif

#ifdef IDLE_CHECK
#define MIN_IDLETIME 1800
#endif

#define CONFIG_H_LEVEL_6_1

#ifdef CRYPT_LINKS
#  ifndef HAVE_LIBCRYPTO
#    error CRYPT_LINKS is defined, but you lack OpenSSL.  Try using --enable-openssl during configure, or #undef CRYPT_LINKS.
#  endif
#endif

#endif /* INCLUDED_config_h */
