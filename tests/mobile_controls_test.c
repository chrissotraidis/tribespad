#include "../port/apple/mobile_controls.h"
#include <assert.h>
#include <stdio.h>
static unsigned drain(SDL_Event out[32]) {
    unsigned count=0;SDL_Event event;
    while(SDL_PollEvent(&event)) {
        if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP||
           event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP||
           event.type==SDL_EVENT_MOUSE_MOTION) {assert(count<32);out[count++]=event;}
    }
    return count;
}
int main(void) {
    assert(SDL_Init(SDL_INIT_EVENTS));TBMobileControls c={.window=42};SDL_Event events[32];drain(events);
    tb_input_move(&c.input,0,1,1,.12);tb_input_action(&c.input,2,TBFire,1);
    tb_input_action(&c.input,3,TBJet,1);tb_input_action(&c.input,4,TBJump,1);
    tb_mobile_controls_emit(&c);assert(drain(events)==5);
    assert(events[0].key.scancode==SDL_SCANCODE_E&&events[0].key.down);
    assert(events[1].key.scancode==SDL_SCANCODE_F&&events[1].key.down);
    assert(events[2].key.scancode==SDL_SCANCODE_SPACE&&events[2].key.down);
    assert(events[3].button.button==SDL_BUTTON_LEFT&&events[3].button.down);
    assert(events[4].button.button==SDL_BUTTON_RIGHT&&events[4].button.down);
    for(unsigned i=0;i<5;i++)assert(events[i].key.windowID==42);
    tb_mobile_controls_emit(&c);assert(!drain(events));
    tb_input_look(&c.input,1,12,-8);tb_input_look(&c.input,2,3,2);
    tb_mobile_controls_emit(&c);assert(drain(events)==1);
    assert(events[0].motion.xrel==15&&events[0].motion.yrel==-6);
    assert(events[0].motion.state==(SDL_BUTTON_LMASK|SDL_BUTTON_RMASK));
    tb_input_action(&c.input,5,TBFire,1);tb_input_release(&c.input,2);
    tb_mobile_controls_emit(&c);assert(!drain(events)); // other finger still firing
    tb_mobile_controls_release(&c);assert(drain(events)==5);
    for(unsigned i=0;i<3;i++)assert(events[i].type==SDL_EVENT_KEY_UP&&!events[i].key.down);
    for(unsigned i=3;i<5;i++)assert(events[i].type==SDL_EVENT_MOUSE_BUTTON_UP&&!events[i].button.down);
    tb_mobile_controls_release(&c);assert(!drain(events));
    tb_input_move(&c.input,0,.01,.01,.12);tb_mobile_controls_emit(&c);assert(!drain(events));
    // A quick tap retains down/up even before the original loop polls.
    tb_input_action(&c.input,4,TBJump,1);tb_mobile_controls_emit(&c);
    tb_input_release(&c.input,4);tb_mobile_controls_emit(&c);assert(drain(events)==2);
    assert(events[0].key.down&&!events[1].key.down);
    SDL_Quit();puts("PASS: simultaneous original controls, relative aim, tap edges and cancellation");
}
