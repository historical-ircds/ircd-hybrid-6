/************************************************************************
 *   IRC - Internet Relay Chat, include/h.h
 *   Copyright (C) 1992 Darren Reed
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
 */

/*
 * "h.h". - Headers file.
 *
 * Most of the externs and prototypes thrown in here to 'cleanup' things.
 * -avalon
 *
 * $Id$
 *
 */


extern aMessageFile	*motd;
#ifdef AMOTD
extern aMessageFile	*amotd;
#endif
extern struct tm	*motd_tm;


extern aMessageFile	*helpfile;

#include "mtrie_conf.h"
#include "fdlist.h"

extern int lifesux;
extern fdlist serv_fdlist;
extern fdlist busycli_fdlist;
extern fdlist default_fdlist;
extern fdlist oper_fdlist;
extern	int	MAXCLIENTS;
extern	struct	Counter	Count;

extern	time_t	NOW;
extern	time_t	nextconnect, nextdnscheck, nextping, timeofday;
extern	aClient	*client, me, *local[];
extern	aChannel *channel;
extern	struct	stats	*ircstp;
extern	int	bootopt;
extern  int     spare_fd;

extern	char	*canonize (char *);
extern	time_t	check_fdlists (time_t);
extern	void	flush_server_connections(void);
extern	aChannel *find_channel (char *, aChannel *);
extern	void	remove_user_from_channel (aClient *, aChannel *,int);
extern	void	del_invite (aClient *, aChannel *);
extern	void	send_user_joins (aClient *, aClient *);
extern	int	can_send (aClient *, aChannel *);
extern	int	is_chan_op (aClient *, aChannel *);
extern	int	has_voice (aClient *, aChannel *);
extern	int	count_channels (aClient *);

extern  aClient *find_chasing (aClient *, char *, int *);
extern	aClient	*find_client (char *, aClient *);
extern	aClient	*find_name (char *, aClient *);
extern	aClient	*find_person (char *, aClient *);
extern	aClient	*find_server (char *, aClient *);
extern	aClient	*find_service (char *, aClient *);
extern	aClient	*find_userhost (char *, char *, aClient *, int *);

extern	int	attach_conf (aClient *, aConfItem *);
extern	aConfItem *attach_confs (aClient*, char *, int);
extern	aConfItem *attach_confs_host (aClient*, char *, int);
extern	int	  attach_Iline (aClient *, struct hostent *,char *,
				char *,char **);
extern	aConfItem *conf, *find_me (void);
extern  aConfItem *find_admin (void);
extern	aConfItem *count_cnlines (Link *);
extern	void	  det_confs_butmask (aClient *, int);
extern	int	  detach_conf (aClient *, aConfItem *);
extern	aConfItem *det_confs_butone (aClient *, aConfItem *);
extern	aConfItem *find_conf (Link *, char*, int);
extern	aConfItem *find_conf_exact (char *, char *, char *, int);
extern	aConfItem *find_conf_host (Link *, char *, int);
extern	aConfItem *find_conf_ip (Link *, char *, char *, int);
extern	aConfItem *find_conf_name (char *, int);
extern	aConfItem *find_kill (aClient *);

#ifdef  GLINES
extern  aConfItem *find_gkill (aClient *);
extern  aConfItem *find_is_glined(char *,char *);
#endif
extern  aConfItem *find_is_klined(char *,char *,unsigned long);

/* hash d lines */
unsigned long host_name_to_ip(char *, unsigned long *);
extern aConfItem *find_dkill(aClient *cptr);

extern  void	add_temp_kline(aConfItem *);
extern  void	flush_temp_klines(void);
extern  void    report_temp_klines(aClient *);

#ifdef  GLINES
extern  void	flush_glines(void);		/* defined in s_conf.c */
extern  void	report_glines(aClient *);	/* defined in s_conf.c */
extern  void    add_gline(aConfItem *);		/* defined in s_conf.c */
#endif

extern	int	find_restrict (aClient *);
extern	int	rehash (aClient *, aClient *, int);
extern  int	rehash_dump (aClient *,char *);
extern	int	initconf (int, int,int );
extern  int	openconf (char *);
extern  int     lock_kline_file ();
extern  void    report_error_on_tty(char *);

extern  void    clear_scache_hash_table(void);
extern  char    *find_or_add(char *);
extern  void    count_scache(int *,u_long *);
extern  void    list_scache(aClient *, aClient *,int, char **);

extern	char	*MyMalloc (int);
extern  char    *MyRealloc (char *, int);
/* MyFree is defined as a macro in sys.h */
/* extern  void     MyFree (char *); */

extern	char	*debugmode, *configfile, *sbrk0;
extern  char    *klinefile;
extern  char	*dlinefile;
#ifdef	GLINES
extern	char	*glinefile;
#endif
extern	char	*getfield (char *);
extern	void	get_sockhost (aClient *, char *);
extern	char	*rpl_str (int);
extern  char 	*err_str (int);
extern	char	*strerror (int);
extern	int	dgets (int, char *, int);
extern	char	*inetntoa (char *);
extern	void	ircsprintf ();
extern	int	dbufalloc, dbufblocks, debuglevel, errno, h_errno;
extern	int	maxdbufalloc, maxdbufblocks;
extern	int	highest_fd, debuglevel, portnum, debugtty, maxusersperchannel;
extern	int	readcalls, udpfd, resfd;
extern	aClient	*add_connection (aClient *, int);
extern	int	add_listener (aConfItem *);
extern	void	add_local_domain (char *, int);
extern	int	check_client (aClient *,char *,char **);
extern	int	check_server (aClient *, struct hostent *,
				    aConfItem *, aConfItem *, int);
extern	int	check_server_init (aClient *);
extern	void	close_connection (aClient *);
extern	void	close_listeners ();
extern	int	connect_server (aConfItem *, aClient *, struct hostent *);
extern	void	get_my_name (aClient *, char *, int);
extern	int	get_sockerr (aClient *);
extern	int	inetport (aClient *, int, u_long);
extern	void	init_sys ();
extern	int	read_message (time_t, fdlist *);
extern	void	report_error (char *, aClient *);
extern	void	set_non_blocking (int, aClient *);
extern	int	setup_ping (void);
extern	void	summon (aClient *, char *, char *, char *);
extern	int	unixport (aClient *, char *, int);
extern	int	utmp_open (void);
extern	int	utmp_read (int, char *, char *, char *, int);
extern	int	utmp_close (int);

extern	void	start_auth (aClient *);
extern	void	read_authports (aClient *);
extern	void	send_authports (aClient *);

extern	void	restart (char *);
extern	void	send_channel_modes (aClient *, aChannel *);
extern	void	server_reboot (void);
extern	void	terminate (void), write_pidfile (void);

extern	int	send_queued (aClient *);
extern  int     place_dline(aClient *,char *,char*, char*);

/* Missing definitions */
/*VARARGS*/
extern  void	sendto_ops_lev();		/* defined in send.c */
extern  void	sendto_realops_lev();		/* defined in send.c */
extern  void	sendto_realops();		/* defined in send.c */
extern  void	send_operwall(aClient *,char *,char *);	/* defined in send.c */
extern  void	sendto_wallops_butone();	/* defined in send.c */
extern  int     match(char *,char *);		/* defined in match.c */
extern	char    *collapse(char *);		/* match.c */
extern  void    send_capabilities(aClient *,int);
extern  int	host_is_legal_ip(char *); 
extern  void	do_include_conf();
extern  void	del_client_from_llist(aClient **, aClient *);
extern	void	sendto_channel_type_notice(aClient *, aChannel *, int, char *);
extern	aConfItem	*match_Dline(unsigned long);
extern	int	show_lusers(aClient *, aClient *, int, char **);
extern	int	nickkilldone(aClient *, aClient *, int, char **,ts_val,char *);
extern	char	*show_iline_prefix(aClient *,aConfItem *,char *);
extern	int sendto_slaves(aClient *, char *, char *, int, char **);
/* END Missing definitions */

/*VARARGS2*/
extern	void	sendto_one();
/*VARARGS4*/
extern	void	sendto_channel_butone();
/*VARARGS5*/
extern	void	sendto_channel_type();
/*VARARGS2*/
extern	void	sendto_serv_butone();

/*VARARGS3*/
extern	void	sendto_cap_serv_butone();

/*VARARGS3*/
extern  void	sendto_match_cap_servs();

/*VARARGS2*/
extern	void	sendto_common_channels();
/*VARARGS3*/
extern	void	sendto_channel_butserv();
/*VARARGS3*/
extern	void	sendto_match_servs();
/*VARARGS5*/
extern	void	sendto_match_butone();
/*VARARGS3*/
extern	void	sendto_all_butone();
/*VARARGS1*/
extern	void	sendto_ops();
/*VARARGS1*/
extern	void	ts_warn();
/*VARARGS3*/
extern	void	sendto_ops_butone();
/*VARARGS3*/
extern	void	sendto_prefix_one();
extern	void	vsendto_prefix_one();

extern	int	writecalls, writeb[];
extern	int	deliver_it (aClient *, char *, int);

extern	int	check_registered (aClient *);
extern	int	check_registered_user (aClient *);
extern	char	*get_client_name (aClient *, int);
extern	char	*get_client_host (aClient *);
extern	char	*my_name_for_link (char *, aConfItem *);
extern	char	*myctime (time_t), *date (time_t);
extern	int	exit_client (aClient *, aClient *, aClient *, char *);
extern	void	initstats (void), tstats (aClient *, char *);
extern	void	serv_info (aClient *, char *);

extern	int	parse (aClient *, char *, char *);
extern	void	init_tree_parse (struct Message *);

extern	int	do_numeric (int, aClient *, aClient *, int, char **);
extern	int	hunt_server (aClient *,aClient *,char *,int,int,char **);
extern	aClient	*next_client (aClient *, char *);
extern	aClient	*next_client_double (aClient *, char *);

extern	int	m_umode (aClient *, aClient *, int, char **);
extern	int	m_names (aClient *, aClient *, int, char **);
extern	int	m_server_estab (aClient *);
extern	void	send_umode (aClient *, aClient *, int, int, char *);
extern	void	send_umode_out (aClient*, aClient *, int);


extern	void	free_client (aClient *);
extern	void	free_link (Link *);
extern	void	free_conf (aConfItem *);
extern	void	free_class (aClass *);
extern	void	free_user (anUser *, aClient *);
extern	Link	*make_link (void);
extern	anUser	*make_user (aClient *);
extern	aConfItem *make_conf (void);
extern	aClass	*make_class (void);
extern	aServer	*make_server (aClient *);
extern	aClient	*make_client (aClient *);
extern	Link	*find_user_link (Link *, aClient *);
extern	void	add_client_to_list (aClient *);
extern	void	add_client_to_llist(aClient **, aClient *);
extern	void	checklist (void);
extern	void	remove_client_from_list (aClient *);
extern	void	initlists (void);
extern  void	block_garbage_collect(void);	/* list.c */
extern  void	block_destroy(void);		/* list.c */

extern	void	add_class (int, int, int, int, long);
extern	void	fix_class (aConfItem *, aConfItem *);
extern	long	get_sendq (aClient *);
extern	int	get_con_freq (aClass *);
extern	int	get_client_ping (aClient *);
extern	int	get_client_class (aClient *);
extern	int	get_conf_class (aConfItem *);
extern	void	report_classes (aClient *);

extern	struct	hostent	*get_res (char *);
extern	struct	hostent	*gethost_byaddr (char *, Link *);
extern	struct	hostent	*gethost_byname (char *, Link *);
extern	void	flush_cache (void);
extern	int	init_resolver (int);
extern	time_t	timeout_query_list (time_t);
extern	time_t	expire_cache (time_t);
extern	void    del_queries (char *);

extern  int     zip_init (aClient *);
extern  void    zip_free (aClient *);
extern  char    *unzip_packet (aClient *, char *, int *);
extern  char    *zip_buffer (aClient *, char *, int *, int);

extern	void	clear_channel_hash_table (void);
extern	void	clear_client_hash_table (void);
extern	int	add_to_client_hash_table (char *, aClient *);
extern	int	del_from_client_hash_table (char *, aClient *);
extern	int	add_to_channel_hash_table (char *, aChannel *);
extern	int	del_from_channel_hash_table (char *, aChannel *);
extern	aChannel *hash_find_channel (char *, aChannel *);
extern	aClient	*hash_find_client (char *, aClient *);
extern	aClient	*hash_find_nickserver (char *, aClient *);
extern	aClient	*hash_find_server (char *, aClient *);

extern	void	add_history (aClient *, int);
extern	aClient	*get_history (char *, time_t);
extern	void	initwhowas (void);
extern	void	off_history (aClient *);

extern	int	dopacket (aClient *, char *, int);

/*VARARGS2*/
extern	void	debug();
#ifdef DEBUGMODE
extern	void	send_usage (aClient *, char *);
extern	void	send_listinfo (aClient *, char *);
extern	void	count_memory (aClient *, char *);
#endif

/* iphash code */
extern void iphash_stats(aClient *,aClient *,int,char **,int);
extern void clear_ip_hash_table(void);

#ifdef LIMIT_UH
void remove_one_ip(aClient *);
#else
void remove_one_ip(unsigned long);
#endif


#ifdef FLUD
int	check_for_ctcp();
int	check_for_flood();
void	free_fluders();
void	free_fludees();
#define MyFludConnect(x)	(((x)->fd >= 0) || ((x)->fd == -2))
#endif /* FLUD */

#ifdef ANTI_SPAMBOT
#define MIN_SPAM_NUM 5
#define MIN_SPAM_TIME 60
#endif

#ifdef IDLE_CHECK
#define MIN_IDLETIME 1800
#endif
