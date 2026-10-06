#include "../port/apple/display.h"
#include <stdio.h>
int main(void){TBAppleDisplay value;if(!tb_apple_display_snapshot(&value)){fputs("Apple display unavailable\n",stderr);return 1;}printf("{\"width\":%u,\"height\":%u,\"native_width\":%u,\"native_height\":%u,\"scale\":%.6f,\"native_scale\":%.6f,\"maximum_refresh_rate\":%u}\n",value.width,value.height,value.native_width,value.native_height,value.scale,value.native_scale,value.maximum_refresh_rate);return 0;}
