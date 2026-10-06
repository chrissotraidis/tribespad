#include "input.h"
#include <math.h>
#include <string.h>
void tb_input_move(TBInput*i,unsigned s,float x,float y,float dead){
 if(s>=TB_INPUT_SOURCES)return;
 if(!isfinite(x)||!isfinite(y)||!isfinite(dead)||dead<0||dead>=1){x=y=0;}
 else {float m=hypotf(x,y);if(m<=dead){x=y=0;}else{float a=(fminf(m,1)-dead)/(1-dead);x=x/m*a;y=y/m*a;}}
 i->sources[s].x=x;i->sources[s].y=y;
}
void tb_input_key_move(TBInput*i,unsigned s,unsigned key,int down){if(s>=TB_INPUT_SOURCES)return;uint32_t*k=&i->sources[s].moveKeys;if(down)*k|=key;else *k&=~key;tb_input_move(i,s,!!(*k&8)-!!(*k&4),!!(*k&1)-!!(*k&2),0);}
void tb_input_look(TBInput*i,unsigned s,float x,float y){if(s>=TB_INPUT_SOURCES||!isfinite(x)||!isfinite(y))return;i->sources[s].lookX+=x;i->sources[s].lookY+=y;}
void tb_input_action(TBInput*i,unsigned s,uint32_t a,int down){if(s>=TB_INPUT_SOURCES)return;if(down)i->sources[s].actions|=a;else i->sources[s].actions&=~a;}
void tb_input_end(TBInput*i,unsigned s){if(s<TB_INPUT_SOURCES){i->sources[s].x=i->sources[s].y=0;i->sources[s].actions=0;i->sources[s].moveKeys=0;}}
void tb_input_release(TBInput*i,unsigned s){if(s<TB_INPUT_SOURCES)memset(&i->sources[s],0,sizeof(i->sources[s]));}
void tb_input_reset(TBInput*i){memset(i,0,sizeof(*i));}
TBInputFrame tb_input_frame(TBInput*i){TBInputFrame f={0};float max=0;for(unsigned n=0;n<TB_INPUT_SOURCES;n++){TBInputSource*s=&i->sources[n];float m=hypotf(s->x,s->y);if(m>max){f.x=s->x;f.y=s->y;max=m;}f.actions|=s->actions;f.lookX+=s->lookX;f.lookY+=s->lookY;s->lookX=s->lookY=0;}return f;}
