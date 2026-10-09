#include "mobile_controls.h"
#include <stdlib.h>

static void send_event(SDL_Event *event) {
    event->common.timestamp=SDL_GetTicksNS();
    if(!SDL_PushEvent(event)) abort();
}
void tb_mobile_controls_emit(TBMobileControls *controls) {
    TBInputFrame frame=tb_input_frame(&controls->input);
    const SDL_Scancode keys[]={SDL_SCANCODE_E,SDL_SCANCODE_D,SDL_SCANCODE_S,
        SDL_SCANCODE_F,SDL_SCANCODE_SPACE,SDL_SCANCODE_G,SDL_SCANCODE_W,
        SDL_SCANCODE_R,SDL_SCANCODE_KP_ENTER};
    uint32_t held=(frame.y>.22f)|((frame.y<-.22f)<<1)|((frame.x<-.22f)<<2)|
        ((frame.x>.22f)<<3)|((!!(frame.actions&TBJump))<<4)|
        ((!!(frame.actions&TBGrenade))<<5)|((!!(frame.actions&TBWeapon))<<6)|
        ((!!(frame.actions&TBUse))<<7)|((!!(frame.actions&TBInventory))<<8)|
        ((!!(frame.actions&TBFire))<<9)|((!!(frame.actions&TBJet))<<10);
    uint32_t changed=held^controls->held;
    for(unsigned i=0;i<11;i++) if(changed&(1u<<i)) {
        bool down=(held&(1u<<i))!=0; SDL_Event event={0};
        if(i<9) {
            event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
            event.key.windowID=controls->window;event.key.scancode=keys[i];
            event.key.key=SDL_GetKeyFromScancode(keys[i],SDL_KMOD_NONE,false);
            event.key.down=down;
        } else {
            event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.windowID=controls->window;
            event.button.button=i==9?SDL_BUTTON_LEFT:SDL_BUTTON_RIGHT;
            event.button.down=down;event.button.clicks=1;
        }
        send_event(&event);
    }
    controls->held=held;
    if(frame.lookX||frame.lookY) {
        SDL_Event event={0};event.type=SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID=controls->window;
        event.motion.state=((held&(1u<<9))?SDL_BUTTON_LMASK:0)|
            ((held&(1u<<10))?SDL_BUTTON_RMASK:0);
        event.motion.xrel=frame.lookX;event.motion.yrel=frame.lookY;
        send_event(&event);
    }
}
void tb_mobile_controls_release(TBMobileControls *controls) {
    tb_input_reset(&controls->input);tb_mobile_controls_emit(controls);
}
