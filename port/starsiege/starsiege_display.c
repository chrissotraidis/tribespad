/* Display modes for Starsiege: Tribes, which picks and switches its own
 * fullscreen mode through EnumDisplaySettingsA/ChangeDisplaySettingsA. The
 * desktop follows the Mac's active display or the configured guest display
 * (guest_display.c). Modes include the Mac's pixel sizes plus legacy sizes,
 * at 16 and 32 bits.
 * A mode switch changes the guest render size inside the Apple window. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <CoreGraphics/CoreGraphics.h>
#endif
#include "../core/guest_display.h"
#include "../core/guest_memory_checks.h"

typedef struct { uint32_t width,height,bits; } Mode;
static Mode *modes;
static size_t mode_count,mode_capacity;
static Mode desktop,current;

static void add_mode(uint32_t width,uint32_t height,uint32_t bits){
 if(!width||!height)return;
 for(size_t i=0;i<mode_count;i++)
  if(modes[i].width==width&&modes[i].height==height&&modes[i].bits==bits)return;
 if(mode_count==mode_capacity){
  mode_capacity=mode_capacity?2*mode_capacity:16;
  Mode *grown=realloc(modes,mode_capacity*sizeof *modes);
  if(!grown)abort();
  modes=grown;
 }
 modes[mode_count++]=(Mode){width,height,bits};
}

#if TARGET_OS_OSX
static int compare_modes(const void *a,const void *b){
 const Mode *left=a,*right=b;
 if(left->bits!=right->bits)return left->bits<right->bits?-1:1;
 if(left->width!=right->width)return left->width<right->width?-1:1;
 if(left->height!=right->height)return left->height<right->height?-1:1;
 return 0;
}

void ss_mac_display_modes(uint32_t *width,uint32_t *height){
 CGDirectDisplayID display=CGMainDisplayID();
 CGDisplayModeRef active=CGDisplayCopyDisplayMode(display);
 if(!active)return;
 uint32_t active_width=(uint32_t)CGDisplayModeGetPixelWidth(active);
 uint32_t active_height=(uint32_t)CGDisplayModeGetPixelHeight(active);
 CGDisplayModeRelease(active);
 if(!active_width||!active_height)return;
 *width=active_width;
 *height=active_height;

 const void *key=kCGDisplayShowDuplicateLowResolutionModes,*value=kCFBooleanTrue;
 CFDictionaryRef options=CFDictionaryCreate(NULL,&key,&value,1,
     &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFArrayRef available=CGDisplayCopyAllDisplayModes(display,options);
 if(options)CFRelease(options);
 if(available){
  for(CFIndex i=0;i<CFArrayGetCount(available);i++){
   CGDisplayModeRef mode=(CGDisplayModeRef)CFArrayGetValueAtIndex(available,i);
   uint32_t w=(uint32_t)CGDisplayModeGetPixelWidth(mode),h=(uint32_t)CGDisplayModeGetPixelHeight(mode);
   add_mode(w,h,16);
   add_mode(w,h,32);
  }
  CFRelease(available);
 }
}
#endif

static void build_modes(uint64_t base){
 if(desktop.width)return;
 uint32_t width=tribes_get_system_metrics(base,0),height=tribes_get_system_metrics(base,1);
 if(!width||!height){fprintf(stderr,"TribesPad: display not configured\n");abort();}
 static const uint32_t sizes[][2]={{640,480},{800,600},{1024,768},{1280,960},{1600,1200}};
 for(unsigned bits=16;bits<=32;bits+=16){
  for(unsigned i=0;i<sizeof sizes/sizeof sizes[0];i++)
   if(sizes[i][0]<=width&&sizes[i][1]<=height)add_mode(sizes[i][0],sizes[i][1],bits);
  add_mode(width,height,bits);
 }
 desktop=current=(Mode){width,height,32};
#if TARGET_OS_OSX
 qsort(modes,mode_count,sizeof *modes,compare_modes);
#endif
}
static int device_ok(uint64_t base,uint32_t device){
 if(!device)return 1;
 const char *name=(const char *)(uintptr_t)(base+device);uint32_t token=tribes_memory_access_begin(name,1,1);tribes_memory_access_end(token);
 return !strcmp(name,"\\\\.\\DISPLAY1");
}
/* DEVMODEA offsets: dmSize 36, dmDriverExtra 38, dmFields 40, dmBitsPerPel 104,
   dmPelsWidth 108, dmPelsHeight 112, dmDisplayFlags 116, dmDisplayFrequency 120. */
uint32_t ss_enum_display_settings(uint64_t base,uint32_t device,uint32_t index,uint32_t output){
 build_modes(base);
 if(!device_ok(base,device)||!output)return 0;
 Mode m;
 if(index==0xffffffffu)m=current;
 else if(index==0xfffffffeu)m=desktop;
 else if(index<mode_count)m=modes[index];
 else return 0;
 unsigned char *d=(unsigned char *)(uintptr_t)(base+output);
 uint32_t token=tribes_memory_access_begin(d,124,3);
 uint16_t size;memcpy(&size,d+36,2);
 if(size<124){tribes_memory_access_end(token);fprintf(stderr,"TribesPad: DEVMODEA size %u is too small\n",size);abort();}
 memset(d,0,32);memcpy(d,"DISPLAY",8);
 uint16_t spec=0x0401,extra=0;memcpy(d+32,&spec,2);memcpy(d+38,&extra,2);
 uint32_t fields=0x40000|0x80000|0x100000|0x200000|0x400000,flags=0,hz=60;
 memcpy(d+40,&fields,4);memcpy(d+104,&m.bits,4);memcpy(d+108,&m.width,4);memcpy(d+112,&m.height,4);memcpy(d+116,&flags,4);memcpy(d+120,&hz,4);
 tribes_memory_access_end(token);return 1;
}
/* LONG ChangeDisplaySettingsA(DEVMODEA *, DWORD). NULL restores the desktop
   mode. CDS_TEST (2) only checks. Unknown modes return DISP_CHANGE_BADMODE. */
uint32_t ss_change_display_settings(uint64_t base,uint32_t input,uint32_t flags){
 build_modes(base);
 if(flags&~(uint32_t)(1|2|4|8)){fprintf(stderr,"TribesPad: ChangeDisplaySettingsA flags 0x%x\n",flags);abort();}
 if(!input){current=desktop;return 0;}
 const unsigned char *d=(const unsigned char *)(uintptr_t)(base+input);
 uint32_t token=tribes_memory_access_begin(d,124,1),fields,bits,width,height;
 memcpy(&fields,d+40,4);memcpy(&bits,d+104,4);memcpy(&width,d+108,4);memcpy(&height,d+112,4);tribes_memory_access_end(token);
 Mode want=current;
 if(fields&0x40000)want.bits=bits;
 if(fields&0x80000)want.width=width;
 if(fields&0x100000)want.height=height;
 for(size_t i=0;i<mode_count;i++)if(modes[i].width==want.width&&modes[i].height==want.height&&modes[i].bits==want.bits){
  if(!(flags&2)){current=want;fprintf(stderr,"TribesPad: guest display mode %ux%u, %u bits\n",want.width,want.height,want.bits);}
  return 0;
 }
 return (uint32_t)-2;
}
/* The guest's current mode, for the host's presentation. */

/* int GetSystemMetrics(int). Window-frame and input metrics take the
   standard Windows values at 96 DPI (classic theme); screen size, SM_CLEANBOOT
   and SM_DBCSENABLED come from guest_display.c. Other metrics stay gates.
   https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getsystemmetrics */
uint32_t ss_get_system_metrics(uint64_t base,uint32_t index){
 switch(index){
 case 2:case 3:return 16;            /* SM_CXVSCROLL, SM_CYHSCROLL */
 case 4:return 19;                   /* SM_CYCAPTION */
 case 5:case 6:return 1;             /* SM_CXBORDER, SM_CYBORDER */
 case 7:case 8:return 3;             /* SM_CXDLGFRAME, SM_CYDLGFRAME */
 case 11:case 12:return 32;          /* SM_CXICON, SM_CYICON */
 case 13:case 14:return 32;          /* SM_CXCURSOR, SM_CYCURSOR */
 case 15:return 19;                  /* SM_CYMENU */
 case 16:return tribes_get_system_metrics(base,0);                 /* SM_CXFULLSCREEN */
 case 17:return tribes_get_system_metrics(base,1)-19;              /* SM_CYFULLSCREEN */
 case 19:return 1;                   /* SM_MOUSEPRESENT */
 case 23:return 0;                   /* SM_SWAPBUTTON */
 case 32:case 33:return 4;           /* SM_CXFRAME, SM_CYFRAME */
 case 36:case 37:return 4;           /* SM_CXDOUBLECLK, SM_CYDOUBLECLK */
 case 43:return 3;                   /* SM_CMOUSEBUTTONS */
 case 75:return 1;                   /* SM_MOUSEWHEELPRESENT */
 case 78:return tribes_get_system_metrics(base,0);                 /* SM_CXVIRTUALSCREEN */
 case 79:return tribes_get_system_metrics(base,1);                 /* SM_CYVIRTUALSCREEN */
 case 80:return 1;                   /* SM_CMONITORS */
 default:return tribes_get_system_metrics(base,index);
 }
}

void ss_display_mode(uint32_t *width,uint32_t *height){*width=current.width;*height=current.height;}

