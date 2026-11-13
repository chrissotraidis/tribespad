#include "../port/apple/content_geometry.h"
#include <stdio.h>
#include <stdlib.h>
static int failures;
static void expect(int condition,const char *message){if(!condition){fprintf(stderr,"content geometry: %s\n",message);failures++;}}
static int near(float a,float b){return fabsf(a-b)<0.001f;}
int main(void){
    /* A notched phone window: inset by the larger side inset, 960-pixel tall game. */
    TBContentGeometry g=tb_content_geometry(874,402,62,62,3,true,1,0,0);
    expect(near(g.inset,62)&&near(g.density,(float)fmin(3,960.0/402)),"phone window");
    /* The same phone with notch avoidance off fills the window. */
    g=tb_content_geometry(874,402,62,62,3,false,1,0,0);expect(near(g.inset,0),"notch avoidance off");
    /* A full-screen iPad keeps native resolution; a lower render scale draws fewer pixels. */
    g=tb_content_geometry(1194,834,0,0,2,true,1,0,0);expect(near(g.inset,0)&&near(g.density,0),"iPad native");
    g=tb_content_geometry(1194,834,0,0,2,true,.5,0,0);expect(near(g.density,1),"iPad render scale");
    /* A resized iPad window or a 1x external display is measured from that
       window's own size and scale, not the device's main screen. */
    g=tb_content_geometry(700,600,0,0,2,true,.5,0,0);expect(near(g.density,1),"resized window");
    g=tb_content_geometry(1920,1080,0,0,1,true,.5,0,0);expect(near(g.density,.5f),"1x display");
    /* Starsiege's fixed 800x600 frame stays centred with its aspect in any window. */
    g=tb_content_geometry(1194,834,0,0,2,true,1,800,600);expect(near(g.inset,(float)floor((1194-834*800/600.0)/2))&&near(g.density,600.0f/834),"fixed frame");
    g=tb_content_geometry(800,700,0,0,2,true,1,800,600);expect(near(g.inset,0)&&near(g.density,600.0f/700),"fixed frame narrow window");
    if(failures)return 1;
    puts("content geometry tests passed");return 0;
}
