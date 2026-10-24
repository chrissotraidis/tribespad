/* The game picker shown when TribesPad opens, before any engine starts. */
#ifndef TB_LAUNCHER_H
#define TB_LAUNCHER_H
typedef enum {TBGameTribes2,TBGameStarsiege} TBGame;
/* Shows the picker (unless Settings turned it off) and returns the game the
   player chose. Main thread, before either engine. Starsiege is offered only
   when the app carries it (build-tribespad.sh --games tribes2,starsiege). */
TBGame tb_launcher_run(void);
#endif
