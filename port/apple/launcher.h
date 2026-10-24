/* The game picker shown when TribesPad opens, before any engine starts. */
#ifndef TB_LAUNCHER_H
#define TB_LAUNCHER_H
/* Shows the picker (unless Settings turned it off) and returns once the
   player chose a game that can start. Main thread, before the engine. */
void tb_launcher_run(void);
#endif

