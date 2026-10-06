#include "input.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){TBInput i={0};TBInputFrame f;
 tb_input_move(&i,0,1,1,0);f=tb_input_frame(&i);assert(fabsf(hypotf(f.x,f.y)-1)<1e-6);
 tb_input_action(&i,1,TBJet,1);tb_input_action(&i,2,TBJump,1);tb_input_action(&i,3,TBFire,1);f=tb_input_frame(&i);assert(f.actions==(TBJet|TBJump|TBFire));
 tb_input_action(&i,4,TBJet,1);tb_input_release(&i,1);f=tb_input_frame(&i);assert(f.actions&TBJet);tb_input_release(&i,4);assert(!(tb_input_frame(&i).actions&TBJet));
 tb_input_look(&i,3,20,-5);tb_input_look(&i,5,-3,2);f=tb_input_frame(&i);assert(f.lookX==17&&f.lookY==-3);assert(tb_input_frame(&i).lookX==0);
 tb_input_move(&i,6,0.05,0,0.15);assert(i.sources[6].x==0);tb_input_move(&i,6,NAN,1,0);assert(i.sources[6].x==0&&i.sources[6].y==0);
 tb_input_look(&i,2,7,3);tb_input_end(&i,2);f=tb_input_frame(&i);assert(f.lookX==7&&f.lookY==3);tb_input_look(&i,2,7,3);tb_input_release(&i,2);assert(tb_input_frame(&i).lookX==0);
 tb_input_key_move(&i,10,1,1);tb_input_key_move(&i,10,2,1);assert(i.sources[10].y==0);tb_input_key_move(&i,10,2,0);assert(i.sources[10].y==1);tb_input_key_move(&i,10,8,1);assert(fabsf(hypotf(i.sources[10].x,i.sources[10].y)-1)<1e-6);
 tb_input_reset(&i);f=tb_input_frame(&i);assert(f.actions==0&&f.x==0&&f.y==0);puts("PASS: simultaneous ski/jet/fire, source isolation, normalization, look consumption, cancellation");}
