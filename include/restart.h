#ifndef INCLUDED_restart_h
#define INCLUDED_restart_h

void restart(char *);
void s_restart(void);
void server_reboot(void);
void s_die();
void s_rehash();
void setup_signals();

#endif
