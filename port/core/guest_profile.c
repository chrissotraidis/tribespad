/* Scalar ASCII legacy Win.ini reads in an explicitly file-backed guest profile.
   No host Windows registry or environment is consulted. Enumeration, non-ASCII
   encoding and registry-backed profiles remain unsupported.
   https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprofilestringa */
#include "guest_profile.h"
#include "guest_filesystem.h"
#include "guest_heap.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct Profile {uint64_t base;char path[PATH_MAX];struct Profile *next;} Profile;
static Profile *profiles;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void unsupported(const char *why){fprintf(stderr,"TribesPad: unsupported legacy profile %s\n",why);abort();}
int tribes_profile_configure_file(uint64_t base,const char *path){
 if(!path||strnlen(path,PATH_MAX)==PATH_MAX||strncasecmp(path,"C:\\",3))return 0;
 for(const unsigned char *p=(const unsigned char *)path;*p;p++)if(*p>=128)return 0;
 Profile *p=calloc(1,sizeof *p);if(!p)return 0;p->base=base;strcpy(p->path,path);
 pthread_mutex_lock(&lock);for(Profile *q=profiles;q;q=q->next)if(q->base==base){int same=!strcmp(q->path,path);pthread_mutex_unlock(&lock);free(p);return same;}
 p->next=profiles;profiles=p;pthread_mutex_unlock(&lock);return 1;
}
static int guest_text(uint64_t base,uint32_t pointer,char text[65536]){
 if(!pointer){text[0]=0;return 1;}
 for(uint32_t i=0;i<65536;i++){
  uint64_t address=(uint64_t)pointer+i;if(address>UINT32_MAX||base>UINTPTR_MAX-address){tribes_set_last_error(87);return 0;}
  unsigned char c=*(const unsigned char *)(uintptr_t)(base+address);if(c>=128)unsupported("non-ASCII input");text[i]=(char)c;if(!c)return 1;
 }
 unsupported("oversized input");return 0;
}
static char *trim(char *s){while(*s==' '||*s=='\t')++s;size_t n=strlen(s);while(n&&(s[n-1]==' '||s[n-1]=='\t'))s[--n]=0;return s;}
/* First matching key in the first matching section. Values preserve embedded
   '=' and semicolons. Quotes delimit values, not comments. */
static char *lookup(char *file,const char *section,const char *key){
 int in=0,seen=0;char *line=file;
 while(line){char *next=strpbrk(line,"\r\n");if(next){char c=*next;*next++=0;if(c=='\r'&&*next=='\n')next++;}char *s=trim(line);
  if(*s=='['){char *end=strchr(s+1,']');if(end){*end=0;if(seen)break;in=!strcasecmp(s+1,section);if(in)seen=1;}}
  else if(in&&*s&&*s!=';'){char *equals=strchr(s,'=');if(equals){*equals=0;if(!strcasecmp(trim(s),key)){char *value=trim(equals+1);size_t n=strlen(value);if(n>=2&&(value[0]=='\''||value[0]=='"')&&value[n-1]==value[0]){value[n-1]=0;value++;}return value;}}}
  line=next;
 }
 return NULL;
}
uint32_t tribes_get_profile_string_a(uint64_t base,uint32_t section,uint32_t key,uint32_t fallback,uint32_t output,uint32_t capacity){
 if(!section||!key)unsupported("section/key enumeration");
 if(!capacity)return 0;
 if(!output||(uint64_t)output+capacity>UINT64_C(0x100000000)||base>UINTPTR_MAX-((uint64_t)output+capacity-1)){tribes_set_last_error(87);return 0;}
 char name[65536],entry[65536],def[65536];if(!guest_text(base,section,name)||!guest_text(base,key,entry)||!guest_text(base,fallback,def))return 0;
 size_t n=strlen(def);while(n&&def[n-1]==' ')def[--n]=0;
 char path[PATH_MAX];pthread_mutex_lock(&lock);Profile *p=profiles;while(p&&p->base!=base)p=p->next;if(p)strcpy(path,p->path);pthread_mutex_unlock(&lock);if(!p)unsupported("not configured");
 char host[PATH_MAX],*file=NULL,*value=def;int failure=0;
 if(!tribes_filesystem_resolve(base,path,host,sizeof host)){if(errno!=ENOENT&&errno!=ENOTDIR)failure=errno;}
 else{
  int fd=open(host,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0){if(errno!=ENOENT)failure=errno;}
  else{struct stat st;if(fstat(fd,&st))failure=errno;else if(!S_ISREG(st.st_mode))failure=EINVAL;else if(st.st_size<0||st.st_size>1048576){close(fd);unsupported("file over 1MiB");}
   else{size_t size=(size_t)st.st_size;file=malloc(size+1);if(!file)failure=ENOMEM;else{size_t used=0;while(used<size){ssize_t got=read(fd,file+used,size-used);if(got<0&&errno==EINTR)continue;if(got<0){failure=errno;break;}if(!got)break;used+=(size_t)got;}file[used]=0;
     if(!failure){for(size_t i=0;i<used;i++)if(!file[i]||(unsigned char)file[i]>=128){close(fd);free(file);unsupported("non-ASCII file");}char *found=lookup(file,name,entry);if(found)value=found;}
   }}close(fd);
  }
 }
 if(failure){free(file);*(unsigned char *)(uintptr_t)(base+output)=0;tribes_set_last_error(failure==ENOMEM?8:failure==EACCES||failure==EPERM?5:13);return 0;}
 n=strlen(value);if(n>=capacity)n=capacity-1;memcpy((void *)(uintptr_t)(base+output),value,n);*(unsigned char *)(uintptr_t)(base+output+n)=0;free(file);return (uint32_t)n;
}
