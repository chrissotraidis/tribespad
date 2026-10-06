/* Fixed, readonly US software layout. Does not inspect host locale or Unicode
   input. Legacy MapVirtualKey modes 0..3; unknown keys/modes return zero. */
#include "keyboard_translation.h"
#include "keyboard_layout_us.h"
#include "keyboard_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned generic(unsigned vk) {if(vk==0xa0||vk==0xa1)return 0x10;if(vk==0xa2||vk==0xa3)return 0x11;if(vk==0xa4||vk==0xa5)return 0x12;return vk;}
uint32_t tribes_map_virtual_key(uint32_t code,uint32_t type) {
    if(type==1||type==3){unsigned scan=code&255,prefix=code>>8,vk=0;if(scan>=128)return 0;
        if(!prefix)vk=tb_us_base[scan];else if(prefix==0xe0)vk=tb_us_e0[scan];else if(prefix==0xe1&&scan==0x1d)vk=0x13;
        return type==1?generic(vk):vk;
    }
    if(code>255)return 0;
    if(type==2)return tb_us_chars[code];
    if(type!=0||!code)return 0;
    if(code==0x10)code=0xa0;else if(code==0x11)code=0xa2;else if(code==0x12)code=0xa4;
    if(code==0x13)return 0x45; /* Pause and NumLock share this legacy scan. */
    if(code>=0x60&&code<=0x69){static const unsigned char digits[]={0x52,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49};return digits[code-0x60];}
    if(code==0x6e)return 0x53;
    for(unsigned scan=1;scan<128;scan++)if(tb_us_base[scan]==code)return scan;
    for(unsigned scan=1;scan<128;scan++)if(tb_us_e0[scan]==code)return scan;
    return 0;
}

static int key_down(const unsigned char *keys,unsigned generic_vk,unsigned left,unsigned right) {return (keys[generic_vk]|keys[left]|keys[right])&128;}
uint32_t tribes_to_ascii(uint64_t base,uint32_t vk,uint32_t scan,uint32_t keys,uint32_t output,uint32_t flags) {
    (void)scan;
    if(!output||keys>UINT32_MAX-255||output>UINT32_MAX-1||base>UINTPTR_MAX-keys-255||base>UINTPTR_MAX-output-1||flags>1){fprintf(stderr,"TribesPad: unsupported ToAscii pointer/thread-state/flags path\n");abort();}
    unsigned char state[256];if(keys)memcpy(state,(const void *)(uintptr_t)(base+keys),256);else tribes_keyboard_snapshot(base,state);
    int shift=!!key_down(state,0x10,0xa0,0xa1),control=!!key_down(state,0x11,0xa2,0xa3),alt=!!key_down(state,0x12,0xa4,0xa5);
    if(alt&&!control){fprintf(stderr,"TribesPad: Alt decimal character composition is not implemented\n");abort();}
    if(vk>255||alt)return 0;
    if(!control&&vk>='A'&&vk<='Z')shift^=state[0x14]&1;
    uint16_t character=tb_us_ascii[vk][shift+2*control];if(character==65535)return 0;
    /* US produces one byte in a WORD, including the NUL character. Other output
       bytes and the caller's 256-byte key-state array remain unchanged. */
    memcpy((void *)(uintptr_t)(base+output),&character,2);return 1;
}
