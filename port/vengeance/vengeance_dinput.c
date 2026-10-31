/* DirectInput 8 for Tribes: Vengeance's WinDrv (Unicode: IDirectInput8W). WinDrv
 * reads the system mouse through it; game controllers come through TribesPad's
 * own controller layer instead, so enumerating game controllers finds none.
 *
 * The root and its devices are TribesPad's DirectInput objects (guest_dinput.c,
 * with Starsiege's device methods in starsiege_input.c, fed from SDL). Their
 * root gets the IDirectInput8W method table (port/vengeance/d3d9's COM table:
 * hpcom_IDirectInput8W_*). A device keeps TribesPad's 27 methods, which are the
 * first 27 of IDirectInputDevice8 in the same order, and gets the five methods
 * DirectInput 8 added after them, which stop with their names if called. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/guest_dinput.h"
#include "../core/guest_heap.h"

extern _Thread_local _cpu *halopad_cpu;
extern uint32_t halopad_com_vtable(const char *iface);
extern uint64_t vengeance_guest_base(void);
extern uint32_t tribes_get_last_error(void);
extern uint32_t tribes_set_last_error(uint32_t value);
#define GUEST(address) ((void *)(uintptr_t)(vengeance_guest_base()+(address)))
static uint32_t rd(uint32_t a){uint32_t v;memcpy(&v,GUEST(a),4);return v;}
static void wr(uint32_t a,uint32_t v){memcpy(GUEST(a),&v,4);}
static int guid_is(uint32_t g,uint32_t first,const char tail[12]){return g&&rd(g)==first&&!memcmp((const char *)GUEST(g)+4,tail,12);}
static const char di8_tail[12]={0x3a,0x48,0xa2,0x4d,(char)0xaa,(char)0x99,0x5d,0x64,(char)0xed,0x36,(char)0x97,0x00};
static const char unknown_tail[12]={0,0,0,0,(char)0xc0,0,0,0,0,0,0,0x46};

/* HRESULT DirectInput8Create(HINSTANCE, DWORD version, REFIID, LPVOID *, LPUNKNOWN) */
uint32_t vg_direct_input8_create(_cpu *c,uint32_t instance,uint32_t version,uint32_t iid,uint32_t out,uint32_t outer){
 halopad_cpu=c;
 if(!out)return 0x80004003;wr(out,0);
 if(!guid_is(iid,0xbf798031u,di8_tail)){fprintf(stderr,"vengeance: DirectInput8Create for an interface other than IDirectInput8W\n");return 0x80004002;}
 if(version!=0x800){fprintf(stderr,"vengeance: DirectInput8Create version 0x%x\n",version);return 0x8007047e;}
 uint32_t hr=tribes_dinput_create(c->_pointer_offset,instance,0x500,out,outer);if(hr)return hr;
 wr(rd(out),halopad_com_vtable("IDirectInput8W"));
 return 0;
}
uint32_t hpcom_IDirectInput8W_QueryInterface_c(uint32_t g,uint32_t iid,uint32_t out){
 if(!out)return 0x80004003;wr(out,0);
 if(!guid_is(iid,0xbf798031u,di8_tail)&&!guid_is(iid,0,unknown_tail))return 0x80004002;
 tribes_dinput_addref(vengeance_guest_base(),g);wr(out,g);return 0;
}
uint32_t hpcom_IDirectInput8W_AddRef_c(uint32_t g){return tribes_dinput_addref(vengeance_guest_base(),g);}
uint32_t hpcom_IDirectInput8W_Release_c(uint32_t g){return tribes_dinput_release(vengeance_guest_base(),g);}
uint32_t hpcom_IDirectInput8W_CreateDevice_c(uint32_t g,uint32_t guid,uint32_t out,uint32_t outer){
 uint64_t base=vengeance_guest_base();uint32_t hr=tribes_dinput_create_device(base,g,guid,out,outer);if(hr)return hr;
 uint32_t device=rd(out),di8=halopad_com_vtable("IDirectInputDevice8W");
 uint32_t error=tribes_get_last_error(),table=tribes_global_alloc(base,0x40,32*4);tribes_set_last_error(error);
 if(!table){tribes_dinput_release(base,device);wr(out,0);return 0x8007000e;}
 for(uint32_t i=0;i<32;i++)wr(table+4*i,i<27?rd(rd(device)+4*i):rd(di8+4*i));
 wr(device,table);return 0;
}
/* EnumDevices(type, callback, context, flags): DI8DEVCLASS_GAMECTRL finds no device. */
uint32_t hpcom_IDirectInput8W_EnumDevices_c(uint32_t g,uint32_t type,uint32_t callback,uint32_t context,uint32_t flags){
 (void)g;(void)callback;(void)context;(void)flags;
 if(type==4)return 0;
 fprintf(stderr,"TribesPad: unsupported Vengeance DirectInput enumeration of device class %u\n",type);abort();
}

