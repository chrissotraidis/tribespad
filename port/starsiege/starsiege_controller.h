#ifndef SS_CONTROLLER_H
#define SS_CONTROLLER_H
/* Sticks and triggers are -1..1 / 0..1 (stick up is +1); buttons 0 or 1. */
typedef struct {float lx,ly,rx,ry,lt,rt;int a,b,x,y,lb,rb,ls,rs,menu,options;} SsPad;
/* 1 and the current state when a controller with an extended gamepad is connected. */
int ss_controller_poll(SsPad *pad);
#endif
