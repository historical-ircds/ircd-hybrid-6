/* dline_conf.h  -- lets muse over dlines, shall we? */

#ifndef __dline_conf_h__
#define __dline_conf_h__

extern void clear_Dline_table();
extern void zap_Dlines();
extern void add_Dline(aConfItem *conf_ptr);
extern void add_ip_Kline(aConfItem *conf_ptr);

extern void add_dline(aConfItem *conf_ptr);

extern aConfItem *match_Dline(unsigned long ip);
extern aConfItem *match_ip_Kline(unsigned long ip, char *);

extern void report_dlines(aClient *sptr);
extern void report_ip_Klines(aClient *sptr);
#endif







