/* The first game controller, read through Apple's GameController framework
 * (Mac and iOS), for Starsiege: Tribes (starsiege_input.c maps it onto the
 * game's own keyboard and mouse controls). Polled once per input pump. */
#import <GameController/GameController.h>
#include "starsiege_controller.h"

int ss_controller_poll(SsPad *pad){
    @autoreleasepool{
        GCController *controller=GCController.current;
        if(!controller)for(GCController *c in GCController.controllers)if(c.extendedGamepad){controller=c;break;}
        GCExtendedGamepad *p=controller.extendedGamepad;if(!p)return 0;
        pad->lx=p.leftThumbstick.xAxis.value;pad->ly=p.leftThumbstick.yAxis.value;
        pad->up=p.dpad.up.pressed;pad->down=p.dpad.down.pressed;pad->left=p.dpad.left.pressed;pad->right=p.dpad.right.pressed;
        pad->rx=p.rightThumbstick.xAxis.value;pad->ry=p.rightThumbstick.yAxis.value;
        pad->lt=p.leftTrigger.value;pad->rt=p.rightTrigger.value;
        pad->a=p.buttonA.pressed;pad->b=p.buttonB.pressed;pad->x=p.buttonX.pressed;pad->y=p.buttonY.pressed;
        pad->lb=p.leftShoulder.pressed;pad->rb=p.rightShoulder.pressed;
        pad->ls=p.leftThumbstickButton.pressed;pad->rs=p.rightThumbstickButton.pressed;
        pad->menu=p.buttonMenu.pressed;pad->options=p.buttonOptions.pressed;
        return 1;
    }
}
