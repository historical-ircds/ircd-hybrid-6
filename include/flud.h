#ifndef INCLUDED_FLUD_H
#define INCLUDED_FLUD_H

#ifdef FLUD
struct SLink;
struct Client;
struct Channel;
struct BlockHeap;

struct fludbot {
        struct Client   *fluder;
        int             count;
        time_t          first_msg;
        time_t          last_msg;
        struct fludbot  *next;
};

extern struct BlockHeap *free_fludbots;
extern struct BlockHeap *free_Links;

extern void announce_fluder(struct Client *,struct Client *,struct Channel *,int );
extern struct fludbot *remove_fluder_reference(struct fludbot **,
                                                        struct Client *);

extern struct SLink *remove_fludee_reference(struct SLink **,void *);
extern int check_for_ctcp(char *);
extern int check_for_flud(struct Client *,struct Client *,struct Channel *,int);
extern void free_fluders(struct Client *,struct Channel *);
extern void free_fludees(struct Client *);
#endif

#endif
