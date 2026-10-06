#include "../port/core/gdi_memory.h"
#include "../port/core/guest_heap.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint64_t base=0x100000000;static uint32_t exited_dc,exited_bitmap;
static void *exiting(void *arg){(void)arg;exited_dc=tribes_create_memory_dc(base,0);exited_bitmap=tribes_create_compatible_bitmap(base,exited_dc,32,8);assert(tribes_select_gdi_object(base,exited_dc,exited_bitmap));return NULL;}
static void *brush_worker(void *arg){(void)arg;tribes_set_last_error(123);for(unsigned i=0;i<10000;i++){uint32_t black=tribes_get_stock_object(base,4),gray=tribes_get_stock_object(base,2),style,color;assert(black==0xd6000004&&gray==0xd6000002);assert(tribes_brush_snapshot(base,black,&style,&color)&&style==0&&color==0);assert(tribes_brush_snapshot(base,gray,&style,&color)&&style==0&&color==0x808080);assert(tribes_delete_gdi_object(base,black)&&tribes_get_stock_object(base,4)==black&&tribes_get_last_error()==123);}return NULL;}
static void *worker(void *arg){uint64_t process=base+(uintptr_t)arg;for(unsigned i=0;i<1000;i++){uint32_t dc=tribes_create_memory_dc(process,0),b=tribes_create_compatible_bitmap(process,dc,33,5),stock=tribes_select_gdi_object(process,dc,b);assert(dc&&b&&stock);assert(!tribes_delete_gdi_object(process,b));assert(tribes_select_gdi_object(process,dc,stock)==b);assert(tribes_delete_gdi_object(process,b)&&tribes_delete_memory_dc(process,dc));}return NULL;}
int main(void){
    pthread_t brush_threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&brush_threads[i],NULL,brush_worker,NULL));for(unsigned i=0;i<8;i++)assert(!pthread_join(brush_threads[i],NULL));
    const uint32_t colors[]={0xffffff,0xc0c0c0,0x808080,0x404040,0,0};for(unsigned i=0;i<6;i++){uint32_t brush=tribes_get_stock_object(base,i),style=99,color=99;assert(brush&&tribes_brush_snapshot(base,brush,&style,&color)&&style==(i==5)&&color==colors[i]);assert(tribes_delete_gdi_object(base,brush)&&tribes_get_stock_object(base,i)==brush);style=color=99;assert(!tribes_brush_snapshot(base+100,brush,&style,&color)&&style==99&&color==99);}
    assert(!tribes_get_stock_object(base,9)&&!tribes_get_stock_object(base,UINT32_MAX));
    tribes_set_last_error(123);uint32_t a=tribes_create_memory_dc(base,0),b=tribes_create_memory_dc(base,a);assert(a&&b&&a!=b&&tribes_get_last_error()==123);
    uint32_t black=tribes_get_stock_object(base,4),gray=tribes_get_stock_object(base,2),white=tribes_get_stock_object(base,0);assert(tribes_select_gdi_object(base,a,black)==white);assert(tribes_select_gdi_object(base,b,gray)==white);assert(tribes_select_gdi_object(base,a,gray)==black);assert(tribes_delete_gdi_object(base,gray));assert(tribes_select_gdi_object(base,a,white)==gray);assert(tribes_select_gdi_object(base,b,white)==gray);
    uint32_t image=tribes_create_compatible_bitmap(base,a,256,256),stock=tribes_select_gdi_object(base,a,image);assert(image&&stock);
    assert(!tribes_select_gdi_object(base,b,image)&&!tribes_delete_gdi_object(base,image));assert(!tribes_select_gdi_object(base+1,a,image));
    uint32_t w,h,stride,bits;unsigned char pixels[8194];memset(pixels,0xa5,sizeof pixels);assert(tribes_bitmap_snapshot(base,image,&w,&h,&stride,&bits,pixels+1,8192));assert(w==256&&h==256&&stride==32&&bits==1&&pixels[0]==0xa5&&pixels[8193]==0xa5);for(unsigned i=1;i<=8192;i++)assert(pixels[i]==0);
    assert(!tribes_bitmap_snapshot(base,image,&w,&h,&stride,&bits,pixels,8191));assert(tribes_select_gdi_object(base,a,stock)==image);assert(tribes_select_gdi_object(base,b,image));assert(tribes_delete_gdi_object(base,b));assert(tribes_delete_gdi_object(base,image));assert(tribes_delete_gdi_object(base,a));assert(!tribes_delete_gdi_object(base,a));assert(!tribes_bitmap_snapshot(base,stock,&w,&h,&stride,&bits,pixels,sizeof pixels));
    a=tribes_create_memory_dc(base,0);image=tribes_create_compatible_bitmap(base,a,0,256);assert(tribes_bitmap_snapshot(base,image,&w,&h,&stride,&bits,pixels,sizeof pixels)&&w==1&&h==1&&stride==4);assert(tribes_delete_gdi_object(base,image));assert(!tribes_create_compatible_bitmap(base,a,UINT32_MAX,2)&&tribes_get_last_error()==87);assert(!tribes_create_compatible_bitmap(base,a,INT32_MAX,INT32_MAX)&&tribes_get_last_error()==8);assert(tribes_delete_memory_dc(base,a));
    pthread_t t;assert(!pthread_create(&t,NULL,exiting,NULL)&&!pthread_join(t,NULL));assert(!tribes_create_compatible_bitmap(base,exited_dc,1,1));assert(tribes_delete_gdi_object(base,exited_bitmap));
    pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i));for(unsigned i=0;i<8;i++)assert(!pthread_join(threads[i],NULL));
    puts("PASS: 80000 concurrent stock-brush read/delete/cache cycles, immutable six-brush descriptors, guarded/cross-process inspection, independent shared brush/bitmap DC selections; actual monochrome pixels, compatibility/zero dimensions, selection ownership, stale handles, creating-thread exit and 8000 concurrent lifetimes");return 0;
}
