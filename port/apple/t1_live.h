/* Starsiege: Tribes population from the community masters (t1_live.c). */
#ifndef TB_T1_LIVE_H
#define TB_T1_LIVE_H
typedef struct {
    int masters_answered,servers_listed,servers_answered;
    int people,busy_servers;   /* players on servers that answered; servers with anyone */
    int top_players;char top_name[64];
} TBT1Live;
/* Blocking, up to timeout_ms (half for the masters, half for the servers).
   Call it off the main thread. 1 when at least one master answered. */
int tb_t1_live(TBT1Live *out,int timeout_ms);
#endif
