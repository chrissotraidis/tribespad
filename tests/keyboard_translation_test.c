#include "../port/core/keyboard_translation.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void ascii_case(uint32_t vk,uint32_t modifiers,uint32_t expected,uint32_t result) {
    unsigned char storage[400],before[256];memset(storage,0xa5,sizeof storage);unsigned char *keys=storage+16;memset(keys,0,256);keys[16]=(modifiers&1)?128:0;keys[17]=(modifiers&2)?128:0;keys[18]=(modifiers&4)?128:0;keys[20]=(modifiers&8)?1:0;keys[144]=(modifiers&16)?1:0;keys[145]=(modifiers&32)?1:0;
    if(modifiers&64){keys[16]=0;keys[161]=128;}if(modifiers&128){keys[17]=0;keys[163]=128;}
    memcpy(before,keys,256);uint64_t base=(uintptr_t)storage;assert(tribes_to_ascii(base,vk,0,16,301,0)==result);uint16_t word;memcpy(&word,storage+301,2);assert(word==(result?expected:0xa5a5)&&storage[300]==0xa5&&storage[303]==0xa5&&!memcmp(keys,before,256));
    memset(storage+301,0xa5,2);assert(tribes_to_ascii(base,vk,0x8000,16,301,1)==result);memcpy(&word,storage+301,2);assert(word==(result?expected:0xa5a5)&&storage[300]==0xa5&&storage[303]==0xa5&&!memcmp(keys,before,256));
}
static void *worker(void *argument) {
    (void)argument;const unsigned scans[]={0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    for(unsigned round=0;round<1000;round++)for(unsigned i=0;i<26;i++){assert(tribes_map_virtual_key(scans[i],1)=='A'+i);assert(tribes_map_virtual_key('A'+i,0)==scans[i]);assert(tribes_map_virtual_key('A'+i,2)=='A'+i);ascii_case('A'+i,0,'a'+i,1);ascii_case('A'+i,1,'A'+i,1);ascii_case('A'+i,8,'A'+i,1);ascii_case('A'+i,9,'a'+i,1);ascii_case('A'+i,2,i+1,1);ascii_case('A'+i,6,0,0);}
    return NULL;
}
int main(void) {
    const unsigned modifiers[][4]={{0x2a,0xa0,0x10,0x2a},{0x36,0xa1,0x10,0x36},{0x1d,0xa2,0x11,0x1d},{0xe01d,0xa3,0x11,0x1d},{0x38,0xa4,0x12,0x38},{0xe038,0xa5,0x12,0x38}};
    for(unsigned i=0;i<6;i++){assert(tribes_map_virtual_key(modifiers[i][0],3)==modifiers[i][1]);assert(tribes_map_virtual_key(modifiers[i][0],1)==modifiers[i][2]);assert(tribes_map_virtual_key(modifiers[i][1],0)==modifiers[i][3]);}
    assert(tribes_map_virtual_key(0x10,0)==0x2a&&tribes_map_virtual_key(0x11,0)==0x1d&&tribes_map_virtual_key(0x12,0)==0x38);
    const unsigned navigation[][2]={{0x47,0x24},{0x48,0x26},{0x49,0x21},{0x4b,0x25},{0x4d,0x27},{0x4f,0x23},{0x50,0x28},{0x51,0x22},{0x52,0x2d},{0x53,0x2e}};
    for(unsigned i=0;i<10;i++){assert(tribes_map_virtual_key(navigation[i][0],1)==navigation[i][1]);assert(tribes_map_virtual_key(0xe000|navigation[i][0],3)==navigation[i][1]);assert(tribes_map_virtual_key(navigation[i][1],0)==navigation[i][0]);}
    assert(tribes_map_virtual_key(0x1b,2)==0x1b&&tribes_map_virtual_key(3,2)==3);
    assert(tribes_map_virtual_key(0xe11d,3)==0x13&&tribes_map_virtual_key(0x13,0)==0x45);assert(tribes_map_virtual_key(0x45,1)==0x90&&tribes_map_virtual_key(0x90,0)==0x45);
    assert(tribes_map_virtual_key(0x1c,1)==13&&tribes_map_virtual_key(0xe01c,1)==13);assert(tribes_map_virtual_key(0xe035,1)==0x6f&&tribes_map_virtual_key(0x6f,0)==0x35);
    for(unsigned i=0;i<10;i++){assert(tribes_map_virtual_key(0x30+i,2)==0x30+i);assert(tribes_map_virtual_key(0x60+i,2)==0x30+i);}
    const unsigned punctuation[][2]={{0xba,';'},{0xbb,'='},{0xbc,','},{0xbd,'-'},{0xbe,'.'},{0xbf,'/'},{0xc0,'`'},{0xdb,'['},{0xdc,'\\'},{0xdd,']'},{0xde,'\''}};for(unsigned i=0;i<11;i++)assert(tribes_map_virtual_key(punctuation[i][0],2)==punctuation[i][1]);
    for(unsigned i=0;i<10;i++)assert(tribes_map_virtual_key(0x3b+i,1)==0x70+i);assert(tribes_map_virtual_key(0x57,1)==0x7a&&tribes_map_virtual_key(0x58,1)==0x7b);for(unsigned i=0;i<12;i++)assert(tribes_map_virtual_key(0x64+i,1)==0x7c+i);
    assert(tribes_map_virtual_key(0xe05b,1)==0x5b&&tribes_map_virtual_key(0xe05c,1)==0x5c&&tribes_map_virtual_key(0xe05d,1)==0x5d);assert(tribes_map_virtual_key(0xe010,1)==0xb1&&tribes_map_virtual_key(0xe019,1)==0xb0);
    assert(!tribes_map_virtual_key(0,0)&&!tribes_map_virtual_key(0,1)&&!tribes_map_virtual_key(0x7f,1)&&!tribes_map_virtual_key(0x9d,1)&&!tribes_map_virtual_key(0xff1d,3)&&!tribes_map_virtual_key(UINT32_MAX,1)&&!tribes_map_virtual_key(UINT32_MAX,0));assert(!tribes_map_virtual_key('A',4)&&!tribes_map_virtual_key('A',UINT32_MAX));
    const unsigned shifted[][2]={{0xba,':'},{0xbb,'+'},{0xbc,'<'},{0xbd,'_'},{0xbe,'>'},{0xbf,'?'},{0xc0,'~'},{0xdb,'{'},{0xdc,'|'},{0xdd,'}'},{0xde,'"'}};for(unsigned i=0;i<11;i++){ascii_case(punctuation[i][0],0,punctuation[i][1],1);ascii_case(shifted[i][0],1,shifted[i][1],1);ascii_case(shifted[i][0],9,shifted[i][1],1);}
    const char shifted_digits[]=")!@#$%^&*(";for(unsigned i=0;i<10;i++){ascii_case('0'+i,1,shifted_digits[i],1);ascii_case(0x60+i,0,'0'+i,1);ascii_case(0x60+i,16,'0'+i,1);}
    ascii_case('2',3,0,1);ascii_case('6',3,30,1);ascii_case(0xbd,3,31,1);ascii_case('2',2,0,0);ascii_case(8,2,127,1);ascii_case(13,2,10,1);ascii_case(27,2,27,1);ascii_case(0xdb,2,27,1);ascii_case(0xdc,2,28,1);ascii_case(0xdd,2,29,1);ascii_case('A',64,'A',1);ascii_case('A',128,1,1);ascii_case(0x25,0,0,0);ascii_case(0x70,0,0,0);ascii_case(UINT32_MAX,0,0,0);ascii_case('A',16|32,'a',1);
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 624000 concurrent readonly US letter mappings and 2496000 guarded character translations; Shift/Caps XOR, controls including NUL, Ctrl+Alt absence, punctuation/numpad/extended-modifier states, ignored Num/Scroll toggle bits, unaligned WORD guards, unchanged inputs/noncharacter outputs and menu/release flags; legacy scan/VK/character modes, left/right modifier identity, E0/E1 keys, Pause/NumLock and navigation/numpad ambiguity, punctuation, functions/media keys and unmapped/reserved modes");
}
