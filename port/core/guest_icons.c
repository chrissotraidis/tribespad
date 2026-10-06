#include "guest_icons.h"
#include "guest_heap.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Image {uint64_t base;uint32_t module,size,resource,resource_size;unsigned char *data;struct Image *next;} Image;
typedef struct Icon {uint64_t base;uint32_t module,name,handle;TribesIconView view;struct Icon *next;} Icon;
static Image *images;static Icon *icons;static uint32_t next_handle=0xd3000001;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static uint16_t u16(const unsigned char *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int within(uint32_t offset,uint64_t count,uint32_t size){return offset<=size&&count<=(uint64_t)size-offset;}
int tribes_icons_configure(uint64_t base,uint32_t module,const void *mapped_image,uint32_t size) {
    const unsigned char *data=mapped_image;if(!module||!data||size<64||size>0x10000000||!within(module,size,UINT32_MAX)){errno=EINVAL;return 0;}
    if(u16(data)!=0x5a4d){errno=EINVAL;return 0;}uint32_t pe=u32(data+60);
    if(!within(pe,24,size)||u32(data+pe)!=0x4550){errno=EINVAL;return 0;}
    uint32_t optional=pe+24;uint16_t optional_size=u16(data+pe+20);
    if(optional_size<120||!within(optional,optional_size,size)||u16(data+optional)!=0x10b||u32(data+optional+92)<3){errno=EINVAL;return 0;}
    uint32_t resource=u32(data+optional+112),resource_size=u32(data+optional+116);
    if(!resource||resource_size<16||!within(resource,resource_size,size)){errno=EINVAL;return 0;}
    Image *image=calloc(1,sizeof *image);if(!image)return 0;image->data=malloc(size);if(!image->data){free(image);return 0;}memcpy(image->data,data,size);
    image->base=base;image->module=module;image->size=size;image->resource=resource;image->resource_size=resource_size;
    pthread_mutex_lock(&lock);for(Image *p=images;p;p=p->next)if(p->base==base&&p->module==module){pthread_mutex_unlock(&lock);free(image->data);free(image);errno=EEXIST;return 0;}
    image->next=images;images=image;pthread_mutex_unlock(&lock);return 1;
}
/* Directory offsets are relative to the resource root, payload RVAs to image. */
static int entry(Image *image,uint32_t directory,uint32_t id,int language,uint32_t *target) {
    if(!within(directory,16,image->resource_size))return -1;
    const unsigned char *root=image->data+image->resource,*d=root+directory;uint32_t count=(uint32_t)u16(d+12)+u16(d+14);
    if(count>4096||!within(directory,(uint64_t)16+count*8,image->resource_size))return -1;
    int fallback=-1;for(uint32_t i=0;i<count;i++){const unsigned char *row=d+16+i*8;uint32_t name=u32(row);if(name&0x80000000)continue;
        if(name==id){*target=u32(row+4);return 1;}if(language&&fallback<0)fallback=(int)i;
    }
    if(fallback>=0){*target=u32(d+20+(uint32_t)fallback*8);return 1;}return 0;
}
static int payload(Image *image,uint32_t type,uint32_t name,const unsigned char **bytes,uint32_t *size) {
    uint32_t target;int result=entry(image,0,type,0,&target);if(result<=0)return result;if(!(target&0x80000000))return -1;
    result=entry(image,target&0x7fffffff,name,0,&target);if(result<=0)return result;if(!(target&0x80000000))return -1;
    result=entry(image,target&0x7fffffff,1033,1,&target);if(result<=0)return result;if(target&0x80000000||!within(target,16,image->resource_size))return -1;
    const unsigned char *data=image->data+image->resource+target;uint32_t rva=u32(data),length=u32(data+4);if(!length||!within(rva,length,image->size))return -1;
    *bytes=image->data+rva;*size=length;return 1;
}
static int decode(const unsigned char *data,uint32_t length,uint32_t width,uint32_t height,uint32_t bits,TribesIconView *view) {
    if(length<40||u32(data)!=40||u32(data+4)!=width||u32(data+8)!=height*2||u16(data+12)!=1||u16(data+14)!=bits)return 0;
    if(u32(data+16)||!(bits==1||bits==4||bits==8||bits==24||bits==32)){fprintf(stderr,"TribesPad: unsupported icon DIB encoding\n");abort();}
    uint32_t colors=bits<=8?u32(data+32):0;if(bits<=8&&!colors)colors=1u<<bits;if(colors>(1u<<((bits<=8)?bits:0)))return 0;
    uint32_t offset=40+colors*4,stride=(width*bits+31)/32*4,mask_stride=(width+31)/32*4;
    if(!within(offset,(uint64_t)(stride+mask_stride)*height,length))return 0;
    unsigned char *rgba=malloc((size_t)width*height*4),*mask=malloc((size_t)width*height);if(!rgba||!mask){free(rgba);free(mask);return -1;}
    int has_alpha=0;for(uint32_t y=0;y<height;y++)for(uint32_t x=0;x<width;x++) {
        const unsigned char *row=data+offset+(height-1-y)*stride,*color;unsigned index=0;
        if(bits==1)index=(row[x/8]>>(7-x%8))&1;else if(bits==4)index=(row[x/2]>>((x&1)?0:4))&15;else if(bits==8)index=row[x];
        if(bits<=8){if(index>=colors){free(rgba);free(mask);return 0;}color=data+40+index*4;}else color=row+x*(bits/8);
        uint32_t pixel=y*width+x;unsigned char *out=rgba+pixel*4;out[0]=color[2];out[1]=color[1];out[2]=color[0];out[3]=bits==32?color[3]:255;
        if(bits==32&&out[3])has_alpha=1;mask[pixel]=(data[offset+stride*height+(height-1-y)*mask_stride+x/8]>>(7-x%8))&1;
    }
    if(!has_alpha)for(uint32_t i=0;i<width*height;i++)rgba[i*4+3]=mask[i]?0:255;
    view->uses_alpha=(uint32_t)has_alpha;view->width=width;view->height=height;view->bits_per_pixel=bits;view->rgba=rgba;view->and_mask=mask;return 1;
}
uint32_t tribes_load_icon(uint64_t base,uint32_t module,uint32_t name) {
    if(!module||name>0xffff){fprintf(stderr,"TribesPad: system/named icon loading is not implemented\n");abort();}
    pthread_mutex_lock(&lock);for(Icon *p=icons;p;p=p->next)if(p->base==base&&p->module==module&&p->name==name){uint32_t result=p->handle;pthread_mutex_unlock(&lock);return result;}
    Image *image=images;while(image&&(image->base!=base||image->module!=module))image=image->next;
    if(!image){pthread_mutex_unlock(&lock);tribes_set_last_error(6);return 0;}
    const unsigned char *group;uint32_t length;int found=payload(image,14,name,&group,&length);uint32_t error=found<0?13:1814;
    if(found!=1||length<6||u16(group)||u16(group+2)!=1){pthread_mutex_unlock(&lock);tribes_set_last_error(found==1?13:error);return 0;}
    uint32_t count=u16(group+4);if(!count||count>256||length!=(uint32_t)(6+count*14)){pthread_mutex_unlock(&lock);tribes_set_last_error(13);return 0;}
    /* Exact legacy size, then highest color depth.
       Scaling is a separate strict gate until a non-32px request is observed. */
    const unsigned char *selected=NULL;unsigned best_bits=0;for(uint32_t i=0;i<count;i++){const unsigned char *row=group+6+i*14;unsigned w=row[0]?row[0]:256,h=row[1]?row[1]:256,bits=u16(row+6);
        if(w==32&&h==32&&(!selected||bits>best_bits)){selected=row;best_bits=bits;}}
    if(!selected){pthread_mutex_unlock(&lock);fprintf(stderr,"TribesPad: icon scaling is not implemented\n");abort();}
    uint32_t id=u16(selected+12),size;const unsigned char *data;found=payload(image,3,id,&data,&size);
    if(found!=1||size!=u32(selected+8)){pthread_mutex_unlock(&lock);tribes_set_last_error(13);return 0;}
    Icon *icon=calloc(1,sizeof *icon);if(!icon){pthread_mutex_unlock(&lock);tribes_set_last_error(8);return 0;}
    int decoded=decode(data,size,32,32,best_bits,&icon->view);if(decoded!=1||next_handle>=0xd4000000){free((void *)icon->view.rgba);free((void *)icon->view.and_mask);free(icon);pthread_mutex_unlock(&lock);tribes_set_last_error(decoded<0?8:13);return 0;}
    icon->view.resource_id=id;icon->base=base;icon->module=module;icon->name=name;icon->handle=next_handle++;icon->next=icons;icons=icon;
#ifdef TRIBESPAD_ICON_OBSERVER
    extern void tribes_icon_observation(uint32_t,uint32_t,uint32_t,const TribesIconView *);
    tribes_icon_observation(module,name,icon->handle,&icon->view);
#endif
    uint32_t result=icon->handle;pthread_mutex_unlock(&lock);return result;
}
int tribes_icon_view(uint64_t base,uint32_t handle,TribesIconView *view) {
    if(!view)return 0;pthread_mutex_lock(&lock);Icon *icon=icons;while(icon&&(icon->base!=base||icon->handle!=handle))icon=icon->next;
    if(icon)*view=icon->view;pthread_mutex_unlock(&lock);return !!icon;
}
