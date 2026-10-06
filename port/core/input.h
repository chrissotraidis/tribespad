#pragma once
#include <stdint.h>
#define TB_INPUT_SOURCES 16
enum TBAction {TBJump=1u<<0,TBJet=1u<<1,TBFire=1u<<2,TBGrenade=1u<<3,TBWeapon=1u<<4,TBUse=1u<<5,TBInventory=1u<<6,TBZoom=1u<<7};
typedef struct {float x,y,lookX,lookY;uint32_t actions,moveKeys;} TBInputSource;
typedef struct {TBInputSource sources[TB_INPUT_SOURCES];} TBInput;
typedef struct {float x,y,lookX,lookY;uint32_t actions;} TBInputFrame;
void tb_input_move(TBInput*,unsigned,float,float,float);
void tb_input_key_move(TBInput*,unsigned,unsigned,int);
void tb_input_look(TBInput*,unsigned,float,float);
void tb_input_action(TBInput*,unsigned,uint32_t,int);
void tb_input_end(TBInput*,unsigned);
void tb_input_release(TBInput*,unsigned);
void tb_input_reset(TBInput*);
TBInputFrame tb_input_frame(TBInput*);
