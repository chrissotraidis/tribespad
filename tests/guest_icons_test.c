#include "../port/core/guest_icons.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static unsigned char image[8192];static uint64_t base;static uint32_t shared;
static void w16(unsigned at,unsigned value){image[at]=(unsigned char)value;image[at+1]=(unsigned char)(value>>8);}
static void w32(unsigned at,unsigned value){w16(at,value);w16(at+2,value>>16);}
static void directory(unsigned at,unsigned id,unsigned target){w16(at+14,1);w32(at+16,id);w32(at+20,target);}
static void fixture(unsigned bits) {
    memset(image,0,sizeof image);w16(0,0x5a4d);w32(60,0x80);w32(0x80,0x4550);w16(0x94,224);w16(0x98,0x10b);w32(0xf4,16);w32(0x108,0x200);w32(0x10c,0x1e00);
    w16(0x20e,2);w32(0x210,3);w32(0x214,0x80000030);w32(0x218,14);w32(0x21c,0x80000050);
    directory(0x230,1,0x80000070);directory(0x250,107,0x80000090);directory(0x270,1033,0xb0);directory(0x290,1033,0xc0);
    unsigned colors=bits<=8?1u<<bits:0,offset=40+colors*4,stride=(32*bits+31)/32*4,length=offset+stride*32+128;
    w32(0x2b0,0x400);w32(0x2b4,length);w32(0x2c0,0x300);w32(0x2c4,20);
    w16(0x302,1);w16(0x304,1);image[0x306]=image[0x307]=32;w16(0x30a,1);w16(0x30c,bits);w32(0x30e,length);w16(0x312,1);
    w32(0x400,40);w32(0x404,32);w32(0x408,64);w16(0x40c,1);w16(0x40e,bits);w32(0x420,colors);
    for(unsigned i=0;i<colors;i++){image[0x428+i*4]=(unsigned char)i;image[0x429+i*4]=(unsigned char)(255-i);image[0x42a+i*4]=(unsigned char)(i^55);}
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
        unsigned at=0x400+offset+y*stride,index=(x+y)&(colors?colors-1:255);
        if(bits==1)image[at+x/8]|=(unsigned char)(index<<(7-x%8));else if(bits==4)image[at+x/2]|=(unsigned char)(index<<((x&1)?0:4));else if(bits==8)image[at+x]=(unsigned char)index;
        else{image[at+x*(bits/8)]=(unsigned char)index;image[at+x*(bits/8)+1]=(unsigned char)(255-index);image[at+x*(bits/8)+2]=(unsigned char)(index^55);if(bits==32)image[at+x*4+3]=128;}
    }
    image[0x400+offset+stride*32+31*4]=0x80;
}
static void *worker(void *argument){(void)argument;tribes_set_last_error(123);for(unsigned i=0;i<10000;i++){assert(tribes_load_icon(base,0x400000,107)==shared);TribesIconView view;assert(tribes_icon_view(base,shared,&view)&&view.width==32&&view.resource_id==1);assert(tribes_get_last_error()==123);}return NULL;}
int main(void) {
    base=(uintptr_t)image;
    for(unsigned mode=0;mode<5;mode++){unsigned bits=(unsigned[]){1,4,8,24,32}[mode];fixture(bits);uint64_t key=base+mode;
        assert(tribes_icons_configure(key,0x400000,image,sizeof image));assert(!tribes_icons_configure(key,0x400000,image,sizeof image)&&errno==EEXIST);
        tribes_set_last_error(123);uint32_t handle=tribes_load_icon(key,0x400000,107);assert(handle&&tribes_get_last_error()==123);TribesIconView view;assert(tribes_icon_view(key,handle,&view));assert(view.width==32&&view.height==32&&view.bits_per_pixel==bits&&view.resource_id==1&&view.uses_alpha==(bits==32));
        unsigned index=31&(bits<=8?(1u<<bits)-1:255);assert(view.rgba[0]==(index^55)&&view.rgba[1]==255-index&&view.rgba[2]==index&&view.rgba[3]==(bits==32?128:0)&&view.and_mask[0]==1&&view.and_mask[1]==0);
        index=32&(bits<=8?(1u<<bits)-1:255);assert(view.rgba[4]==(index^55)&&view.rgba[5]==255-index&&view.rgba[6]==index&&view.rgba[7]==(bits==32?128:255));
        memset(image,0,sizeof image);assert(tribes_load_icon(key,0x400000,107)==handle&&view.rgba[3]==(bits==32?128:0));if(!mode)shared=handle;
        assert(!tribes_load_icon(key,0x400000,108)&&tribes_get_last_error()==1814);assert(!tribes_load_icon(key,0x500000,107)&&tribes_get_last_error()==6);
    }
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    TribesIconView guarded;memset(&guarded,0xa5,sizeof guarded);TribesIconView before=guarded;assert(!tribes_icon_view(base+50,shared,&guarded)&&!memcmp(&guarded,&before,sizeof guarded));assert(!tribes_icon_view(base,shared,NULL));
    fixture(8);w32(0x108,0xfffffff0);assert(!tribes_icons_configure(base+20,0x400000,image,sizeof image)&&errno==EINVAL);
    fixture(8);w32(0x214,0x80001fff);assert(tribes_icons_configure(base+21,0x400000,image,sizeof image));assert(!tribes_load_icon(base+21,0x400000,107)&&tribes_get_last_error()==13);
    fixture(8);w32(0x2b4,1);assert(tribes_icons_configure(base+22,0x400000,image,sizeof image));assert(!tribes_load_icon(base+22,0x400000,107)&&tribes_get_last_error()==13);
    fixture(8);image[0x407]=0x80;assert(tribes_icons_configure(base+23,0x400000,image,sizeof image));assert(!tribes_load_icon(base+23,0x400000,107)&&tribes_get_last_error()==13);
    fixture(8);w32(0x420,2);image[0x400+48]=255;assert(tribes_icons_configure(base+24,0x400000,image,sizeof image));assert(!tribes_load_icon(base+24,0x400000,107)&&tribes_get_last_error()==13);
    puts("PASS: 80000 concurrent shared icon/cache reads; bounded mapped-PE resource directories, immutable module snapshots, 1/4/8/24/32-bit DIB decoding, bottom-up colors, legacy AND/XOR retention, alpha, missing/cross-process handles and malformed RVA/directory/payload/palette guards");
}
