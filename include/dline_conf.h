/* dline_conf.h  -- lets muse over dlines, shall we? */

#ifndef __dline_conf_h__
#define __dline_conf_h__

void clear_Dline_table();
void zap_Dlines();
void add_Dline(aConfItem *conf_ptr);
void add_ip_Kline(aConfItem *conf_ptr);

void add_dline(aConfItem *conf_ptr);

aConfItem *match_Dline(unsigned long ip);
aConfItem *match_ip_Kline(unsigned long ip, char *);

void report_dlines(aClient *sptr);
void report_ip_Klines(aClient *sptr);
#endif







