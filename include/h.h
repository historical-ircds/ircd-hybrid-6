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
 *
 *
 * "h.h". - Headers file.
 *
 * Most of the externs and prototypes thrown in here to 'cleanup' things.
 * -avalon
 *
 * $Id$
 *
 */
#ifndef INCLUDED_h_h
#define INCLUDED_h_h
#include "mtrie_conf.h"
#include "fdlist.h"

struct Class;

extern int lifesux;
extern fdlist serv_fdlist;
extern fdlist busycli_fdlist;
extern fdlist default_fdlist;
extern fdlist oper_fdlist;
extern	struct	Counter	Count;

extern time_t NOW;
extern time_t nextconnect;
extern time_t nextping;
extern time_t timeofday;
extern struct Client* GlobalClientList;
extern struct Client  me;
extern struct Client* local[];
extern	aChannel *channel;
extern	struct	stats	*ircstp;
extern	int	bootopt;
extern  int     spare_fd;

extern void     outofmemory(void);               /* list.c */
extern	char*   canonize (char *);
extern	time_t	check_fdlists (time_t);
extern	void	flush_server_connections(void);

extern  aClient *find_chasing (aClient *, char *, int *);
extern aClient* find_client(const char* name, aClient* client);
extern	aClient	*find_name (char *, aClient *);
extern	aClient	*find_person (char *, aClient *);
extern struct Client* find_server(const char* name, struct Client* dflt_client);
extern	aClient	*find_userhost (char *, char *, aClient *, int *);

extern aConfItem *find_is_klined(char*, char *,unsigned long);

/* hash d lines */
unsigned long host_name_to_ip(char *, unsigned long *);
extern aConfItem *find_dkill(aClient *cptr);

extern  void	add_temp_kline(aConfItem *);
extern  void	flush_temp_klines(void);
extern  void    report_temp_klines(aClient *);

#ifdef  GLINES
extern aConfItem* find_gkill(aClient* client);
extern aConfItem* find_is_glined(const char* host, const char* name);
extern  void	flush_glines(void);		
extern  void	report_glines(aClient *);	
#endif

extern	int	rehash (aClient *, aClient *, int);
extern  void    report_error_on_tty(const char* message); /* ircd.c */

extern void        clear_scache_hash_table(void);
extern const char* find_or_add(const char* name);
extern void        count_scache(int *,u_long *);
extern void        list_scache(aClient *, aClient *,int, char **);

extern void     dummy(int signo);
extern	char	*debugmode, *configfile, *sbrk0;

extern	char*   getfield(char *);
extern  char    *form_str (int);
extern	int	dbufalloc, dbufblocks, debuglevel, errno, h_errno;
extern	int	maxdbufalloc, maxdbufblocks;
extern	int	highest_fd, debuglevel, portnum, debugtty, maxusersperchannel;
extern	int	readcalls;
extern	void	get_my_name (aClient *, char *, int);
extern	int	setup_ping (void);

extern	void	send_channel_modes (aClient *, aChannel *);
extern	void	terminate (void);

extern	int	send_queued (aClient *);

/* Missing definitions */
/*VARARGS*/
extern  void    send_capabilities(aClient *,int);
extern  int	is_address(char *,unsigned long *,unsigned long *); 
extern  void	do_include_conf();
extern	aConfItem	*match_Dline(unsigned long);
extern	int	show_lusers(aClient *, aClient *, int, char **);
extern	int	nickkilldone(aClient*, aClient*, int, char**, time_t, char*);
extern	char	*show_iline_prefix(aClient *,aConfItem *,char *);
/* END Missing definitions */

extern	int	writecalls, writeb[];
extern	int	deliver_it (aClient *, char *, int);

extern	int	check_registered (aClient *);
extern	int	check_registered_user (aClient *);
extern const char* my_name_for_link(const char* name, aConfItem* conf);
extern  char*   date(time_t);
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


extern	void	_free_link (Link *);
extern	void	free_class(struct Class* c);
extern	void	_free_user (anUser *, aClient *);
extern	Link	*make_link (void);
extern	anUser	*make_user (aClient *);
extern	struct Class* make_class(void);
extern	aServer	*make_server (aClient *);
extern	Link	*find_user_link (Link *, aClient *);
extern	void	checklist (void);
extern	void	initlists (void);
extern  void	block_garbage_collect(void);	/* list.c */
extern  void	block_destroy(void);		/* list.c */

extern	void	add_class (int, int, int, int, long);
extern	void	fix_class (aConfItem *, aConfItem *);
extern  void    GetPrintableaConfItem(aConfItem *, char **, char **, char **,
				      char **, int *);
extern	void	report_classes (aClient *);


extern int     zip_init (aClient *);
extern void    zip_free (aClient *);
extern char    *unzip_packet (aClient *, char *, int *);
extern char    *zip_buffer (aClient *, char *, int *, int);

extern void	add_history (aClient *, int);
extern aClient	*get_history (char *, time_t);
extern void	initwhowas (void);

extern	int	dopacket (aClient *, char *, int);

/*VARARGS2*/
extern	void	debug(int, char *, ...);
#ifdef DEBUGMODE
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


#endif /* INCLUDED_h_h */

