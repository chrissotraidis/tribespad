/* The release's ZIP: the central directory names each member, its method,
   sizes and local header; Deflate64 (method 9) goes through zlib's infback9. */
#include "vg_installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "infback9.h"

/* zlib's default allocator, which infback9 (built outside zlib) names; the
   system zlib keeps its own copy internal. */
void *zcalloc(void *opaque,unsigned items,unsigned size){(void)opaque;return calloc(items,size);}
void zcfree(void *opaque,void *ptr){(void)opaque;free(ptr);}

static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t u16(const unsigned char *p){return (uint16_t)(p[0]|p[1]<<8);}
static int fail(char *error,size_t n,const char *what,const char *name){snprintf(error,n,"%s%s%s",what,name?": ":"",name?name:"");return -1;}

typedef struct {FILE *f;uint64_t left;unsigned char buf[1<<16];} Source;
typedef struct {FILE *f;uint32_t crc;uint64_t written;volatile uint64_t *done;} Sink;
static unsigned pull(void *d,unsigned char **next){Source *s=d;size_t want=s->left<sizeof s->buf?(size_t)s->left:sizeof s->buf;
    size_t got=want?fread(s->buf,1,want,s->f):0;s->left-=got;*next=s->buf;return (unsigned)got;}
static int push(void *d,unsigned char *data,unsigned len){Sink *s=d;s->crc=(uint32_t)crc32(s->crc,data,len);s->written+=len;
    if(s->done)*s->done+=len;return fwrite(data,1,len,s->f)!=len;}

int vg_installer_unzip(const char *zip,const char *dir,const char *const names[],size_t count,volatile uint64_t *done,char *error,size_t n){
    FILE *f=fopen(zip,"rb");if(!f)return fail(error,n,"cannot open the download",NULL);
    /* End of central directory: the last 22 bytes when there is no comment, else searched back up to 64 KB. */
    if(fseeko(f,0,SEEK_END)){fclose(f);return fail(error,n,"cannot read the download",NULL);}
    off_t size=ftello(f);unsigned char tail[65536+22];size_t span=size<(off_t)sizeof tail?(size_t)size:sizeof tail;
    fseeko(f,size-(off_t)span,SEEK_SET);if(fread(tail,1,span,f)!=span){fclose(f);return fail(error,n,"cannot read the download",NULL);}
    long e=-1;for(long i=(long)span-22;i>=0;i--)if(u32(tail+i)==0x06054b50u){e=i;break;}
    if(e<0){fclose(f);return fail(error,n,"not a ZIP file",NULL);}
    uint16_t entries=u16(tail+e+10);uint32_t cd_size=u32(tail+e+12),cd_off=u32(tail+e+16);
    unsigned char *cd=malloc(cd_size?cd_size:1);fseeko(f,cd_off,SEEK_SET);
    if(!cd||fread(cd,1,cd_size,f)!=cd_size){free(cd);fclose(f);return fail(error,n,"cannot read the ZIP directory",NULL);}
    size_t found=0;int result=0;unsigned char *window=malloc(65536);
    for(uint32_t at=0,i=0;i<entries&&!result;i++){
        if(at+46>cd_size||u32(cd+at)!=0x02014b50u){result=fail(error,n,"damaged ZIP directory",NULL);break;}
        uint16_t method=u16(cd+at+10),nlen=u16(cd+at+28),xlen=u16(cd+at+30),clen=u16(cd+at+32);
        uint32_t crc=u32(cd+at+16),csize=u32(cd+at+20),usize=u32(cd+at+24),local=u32(cd+at+42);
        char name[256];if(nlen>=sizeof name||at+46+nlen>cd_size){result=fail(error,n,"damaged ZIP directory",NULL);break;}
        memcpy(name,cd+at+46,nlen);name[nlen]=0;at+=46u+nlen+xlen+clen;
        if(name[nlen-1]=='/')continue;
        int wanted=0;for(size_t k=0;k<count;k++)if(!strcmp(names[k],name))wanted=1;
        if(!wanted||strchr(name,'/')||strchr(name,'\\')||strstr(name,"..")){result=fail(error,n,"unexpected file in the ZIP",name);break;}
        if(csize==0xffffffffu||usize==0xffffffffu){result=fail(error,n,"ZIP64 member",name);break;}
        unsigned char lh[30];fseeko(f,local,SEEK_SET);
        if(fread(lh,1,30,f)!=30||u32(lh)!=0x04034b50u){result=fail(error,n,"damaged ZIP member",name);break;}
        fseeko(f,(off_t)local+30+u16(lh+26)+u16(lh+28),SEEK_SET);
        char path[4096];snprintf(path,sizeof path,"%s/%s",dir,name);FILE *out=fopen(path,"wb");
        if(!out){result=fail(error,n,"cannot write",name);break;}
        Source src={f,csize,{0}};Sink sink={out,(uint32_t)crc32(0,NULL,0),0,done};
        if(method==0){unsigned char *p;unsigned got;while((got=pull(&src,&p))>0)if(push(&sink,p,got)){result=fail(error,n,"cannot write",name);break;}}
        else if(method==9){z_stream z;memset(&z,0,sizeof z);
            if(inflateBack9Init(&z,window)!=Z_OK){result=fail(error,n,"no memory",NULL);}
            else{int r=inflateBack9(&z,pull,&src,push,&sink);inflateBack9End(&z);
                if(r!=Z_STREAM_END)result=fail(error,n,"damaged Deflate64 data",name);}}
        else if(method==8){z_stream z;memset(&z,0,sizeof z);
            if(inflateBackInit(&z,15,window)!=Z_OK){result=fail(error,n,"no memory",NULL);}
            else{int r=inflateBack(&z,pull,&src,push,&sink);inflateBackEnd(&z);
                if(r!=Z_STREAM_END)result=fail(error,n,"damaged Deflate data",name);}}
        else result=fail(error,n,"unsupported ZIP compression",name);
        if(fclose(out)&&!result)result=fail(error,n,"cannot write",name);
        if(!result&&(sink.written!=usize||sink.crc!=crc))result=fail(error,n,"ZIP member failed its check",name);
        if(!result)found++;
    }
    free(window);free(cd);fclose(f);
    if(!result&&found!=count)result=fail(error,n,"the ZIP is missing an installer file",NULL);
    return result;
}
