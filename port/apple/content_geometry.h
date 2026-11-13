#ifndef TRIBESPAD_APPLE_CONTENT_GEOMETRY_H
#define TRIBESPAD_APPLE_CONTENT_GEOMETRY_H
#include <math.h>
#include <stdbool.h>
/* Game-area geometry of one window in points, from that window's own size,
   side safe-area insets and display scale (content_area.m). Pure C so the
   numbers can be tested without UIKit.
   Phones are under 480 points tall. The original UI needs 480 lines at its
   scale, and QoL only uses whole scales, so render the game 960 pixels tall:
   scale 2 then draws the UI over exactly 480 lines, as large as it can be.
   iPad keeps the native resolution unless Settings > Render Scale lowers it:
   the game then draws fewer pixels and the system scales them up. The menus
   keep their size, because the UI scale follows the pixel height
   (guest_sdl_backend.c). A fixed game frame is a centred area with its
   aspect, drawn at its pixel size. */
typedef struct {float inset,density;} TBContentGeometry;
static inline TBContentGeometry tb_content_geometry(double width,double height,double safe_left,double safe_right,
    double display_scale,bool avoid_notch,double render_scale,int fixed_width,int fixed_height){
    TBContentGeometry g={avoid_notch?(float)ceil(fmax(safe_left,safe_right)):0.0f,0.0f};
    if(fixed_width>0&&fixed_height>0){
        double area=fmin(width,height*fixed_width/fixed_height);
        g.inset=(float)floor((width-area)/2);g.density=(float)(fixed_height/height);return g;
    }
    if(render_scale<=0||render_scale>1)render_scale=1;
    render_scale=fmax(render_scale,.5);
    if(height<480)g.density=(float)fmin(display_scale,960.0/height);
    else g.density=render_scale<.99?(float)fmax(display_scale*render_scale,480.0/height):0.0f;
    return g;
}
#endif
