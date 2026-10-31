/* Windows services Tribes: Vengeance imports that Tribes 2 and Starsiege
 * never reached, mostly the Unicode (W) forms Unreal's Core uses. Bound through
 * config/vengeance/bindings.json; Tribes 2's reviewed bindings stay unchanged.
 * Paths go through TribesPad's mounted namespace (guest_filesystem.c and
 * Starsiege's cached copy), which accepts ASCII names: a W path with other
 * characters fails with ERROR_NOT_SUPPORTED, as TribesPad's other W services do.
 * Services that reuse an A service narrow their arguments into scratch space
 * below the guest stack pointer, which no guest code uses during the call. */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "../llasm-support/llasm_cpu.h"
#include "../core/guest_filesystem.h"

extern uint32_t tribes_set_last_error(uint32_t value);
extern uint32_t tribes_get_last_error(void);
extern uint32_t tribes_get_file_attributes(uint64_t base,uint32_t name);
extern uint32_t tribes_find_first(uint64_t base,uint32_t pattern,uint32_t output);
extern uint32_t tribes_find_next(uint64_t base,uint32_t handle,uint32_t output);
extern uint32_t tribes_move_file_ex_w(uint64_t base,uint32_t from,uint32_t to,uint32_t flags);

#define GUEST(base,address) ((void *)(uintptr_t)((base)+(address)))
enum {SCRATCH=0x3000};
/* Scratch below the stack pointer: [esp-SCRATCH, esp-16). */
static uint32_t scratch(_cpu *c,uint32_t offset){return ((c->_esp-SCRATCH)&~15u)+offset;}
static int in_guest(uint64_t base,uint32_t address,uint32_t bytes){
 return address&&address<=UINT32_MAX-bytes&&base<=UINTPTR_MAX-address-bytes;
}
/* Copies a guest string (wide or narrow) as ASCII into guest; 0 with the
   Windows error on failure. */
static int read_ascii(uint64_t base,uint32_t name,int wide,char *out,size_t capacity){
 if(!name){tribes_set_last_error(87);return 0;}
 for(size_t i=0;i<capacity;i++){
  uint32_t at=name+(uint32_t)(i*(wide?2:1));if(!in_guest(base,at,2)){tribes_set_last_error(87);return 0;}
  uint16_t v;if(wide)memcpy(&v,GUEST(base,at),2);else v=*(const unsigned char *)GUEST(base,at);
  if(v>127){tribes_set_last_error(50);return 0;}
  out[i]=(char)v;if(!v)return 1;
 }
 tribes_set_last_error(206);return 0;
}
static int narrow_to_scratch(_cpu *c,uint32_t name,uint32_t at){
 char text[PATH_MAX];if(!read_ascii(c->_pointer_offset,name,1,text,sizeof text))return 0;
 memcpy(GUEST(c->_pointer_offset,at),text,strlen(text)+1);return 1;
}
static void widen(uint64_t base,uint32_t output,const char *text,size_t count){
 for(size_t i=0;i<count;i++){uint16_t v=(unsigned char)text[i];memcpy(GUEST(base,output+2*i),&v,2);}
}
static uint32_t error_of(int e){return e==ENOENT?2:e==ENOTDIR?3:e==EACCES||e==EPERM||e==EISDIR?5:e==EEXIST?183:e==ENOTEMPTY?145:e==ENAMETOOLONG?206:e==ENOTSUP?50:5;}

/* DWORD GetCurrentDirectoryW(DWORD, LPWSTR): length without the NUL, or the
   size needed including it. */
uint32_t vg_get_current_directory_w(_cpu *c,uint32_t capacity,uint32_t output){
 uint64_t base=c->_pointer_offset;char cwd[PATH_MAX];uint32_t at=scratch(c,0);
 uint32_t n=tribes_get_current_directory(base,sizeof cwd,at);if(!n||n>=sizeof cwd)return 0;
 memcpy(cwd,GUEST(base,at),n+1);
 if(capacity<=n)return n+1;
 if(!in_guest(base,output,2*(n+1))){tribes_set_last_error(87);return 0;}
 widen(base,output,cwd,n+1);return n;
}
uint32_t vg_set_current_directory_w(_cpu *c,uint32_t name){
 uint32_t at=scratch(c,0);if(!narrow_to_scratch(c,name,at))return 0;
 return tribes_set_current_directory(c->_pointer_offset,at);
}

/* Named kernel objects, W forms: TribesPad's A services with the name narrowed. */
extern uint32_t tribes_create_mutex(uint64_t base,uint32_t attributes,uint32_t owned,uint32_t name);
extern uint32_t tribes_create_event(uint64_t base,uint32_t attributes,uint32_t manual,uint32_t initial,uint32_t name);
uint32_t vg_create_mutex_w(_cpu *c,uint32_t attributes,uint32_t owned,uint32_t name){
 /* One guest process: a named mutex is a process-local one, unless the name
    was already used (a second open of the same object is not modeled). Security
    attributes have no meaning here. */
 (void)attributes;
 if(name){
  static char names[16][128];static unsigned count;char text[128];
  if(!read_ascii(c->_pointer_offset,name,1,text,sizeof text))return 0;
  for(unsigned i=0;i<count;i++)if(!strcmp(names[i],text)){fprintf(stderr,"TribesPad: unsupported second CreateMutexW \"%s\"\n",text);abort();}
  if(count<16)strcpy(names[count++],text);
  fprintf(stderr,"vengeance: mutex \"%s\" (process-local)\n",text);
 }
 uint32_t h=tribes_create_mutex(c->_pointer_offset,0,owned,0);if(h)tribes_set_last_error(0);return h;
}
uint32_t vg_create_event_w(_cpu *c,uint32_t attributes,uint32_t manual,uint32_t initial,uint32_t name){
 uint32_t at=scratch(c,0);if(name&&!narrow_to_scratch(c,name,at))return 0;
 return tribes_create_event(c->_pointer_offset,attributes,manual,initial,name?at:0);
}
/* GetSystemDirectoryW / GetWindowsDirectoryW: the A answers, widened. */
extern uint32_t tribes_get_system_directory_a(uint64_t base,uint32_t output,uint32_t capacity);
extern uint32_t tribes_get_windows_directory_a(uint64_t base,uint32_t output,uint32_t capacity);
static uint32_t directory_w(_cpu *c,uint32_t output,uint32_t capacity,uint32_t (*a)(uint64_t,uint32_t,uint32_t)){
 uint64_t base=c->_pointer_offset;uint32_t at=scratch(c,0),n=a(base,at,PATH_MAX);if(!n||n>=PATH_MAX)return n;
 char text[PATH_MAX];memcpy(text,GUEST(base,at),n+1);
 if(capacity<=n)return n+1;if(!in_guest(base,output,2*(n+1))){tribes_set_last_error(87);return 0;}
 widen(base,output,text,n+1);return n;
}
uint32_t vg_get_system_directory_w(_cpu *c,uint32_t output,uint32_t capacity){return directory_w(c,output,capacity,tribes_get_system_directory_a);}
uint32_t vg_get_windows_directory_w(_cpu *c,uint32_t output,uint32_t capacity){return directory_w(c,output,capacity,tribes_get_windows_directory_a);}
/* GlobalLock / GlobalUnlock: TribesPad's GlobalAlloc memory is fixed, so the handle is the address. */
uint32_t vg_global_lock(uint32_t handle){if(!handle)tribes_set_last_error(6);return handle;}
uint32_t vg_global_unlock(uint32_t handle){(void)handle;tribes_set_last_error(0);return 0;}

/* Registry: TribesPad models the hardware keys and Tribes 2's own settings.
   The keys Vengeance looks for under Software (Direct3D tuning switches, its
   installer's records) do not exist on this system: opening them reports
   ERROR_FILE_NOT_FOUND, as on a machine where nothing set them. */
extern uint32_t tribes_reg_open_key_ex_a(uint64_t base,uint32_t key,uint32_t subkey,uint32_t options,uint32_t access,uint32_t output);
static int software_key(uint64_t base,uint32_t key,uint32_t subkey){
 if((key!=0x80000002u&&key!=0x80000001u)||!subkey)return 0;  /* HKLM, HKCU */
 char text[16];for(int i=0;i<9;i++){text[i]=*(const char *)GUEST(base,subkey+(uint32_t)i);if(!text[i])return 0;}text[9]=0;
 if(strncasecmp(text,"Software\\",9))return 0;
 fprintf(stderr,"vengeance: registry key %s\\%.200s is not present\n",key==0x80000002u?"HKLM":"HKCU",(const char *)GUEST(base,subkey));return 1;
}
uint32_t vg_reg_open_key_ex_a(uint64_t base,uint32_t key,uint32_t subkey,uint32_t options,uint32_t access,uint32_t output){
 if(software_key(base,key,subkey)){if(output)memset(GUEST(base,output),0,4);return 2;}
 return tribes_reg_open_key_ex_a(base,key,subkey,options,access,output);
}
uint32_t vg_reg_open_key_a(uint64_t base,uint32_t key,uint32_t subkey,uint32_t output){return vg_reg_open_key_ex_a(base,key,subkey,0,0x20019,output);}

/* IsProcessorFeaturePresent: the Direct3D renderer also asks for 3DNow! (7),
   which the guest CPU (an MMX Pentium Pro, TribesPad's CPUID) lacks; the FPU
   has neither the Pentium division erratum (0) nor emulation (1). */
extern uint32_t tribes_processor_feature_present(uint32_t feature);
uint32_t vg_processor_feature_present(uint32_t feature){
 if(feature==0||feature==1||feature==7)return 0;
 return tribes_processor_feature_present(feature);
}

/* Global atoms (WinDrv names its Alt+Esc hot key "UnrealAltEsc"): one process,
   so a counted table of names; integer atoms are themselves. */
static struct {char name[256];uint32_t refs;} atoms[64];
uint32_t vg_global_add_atom_w(_cpu *c,uint32_t name){
 if(name<0xc000u)return name;char text[256];if(!read_ascii(c->_pointer_offset,name,1,text,sizeof text))return 0;
 int empty=-1;for(int i=0;i<64;i++){if(atoms[i].refs&&!strcasecmp(atoms[i].name,text)){atoms[i].refs++;return 0xc100u+(uint32_t)i;}if(!atoms[i].refs&&empty<0)empty=i;}
 if(empty<0){tribes_set_last_error(8);return 0;}strcpy(atoms[empty].name,text);atoms[empty].refs=1;return 0xc100u+(uint32_t)empty;
}
uint32_t vg_global_delete_atom(uint32_t atom){
 if(atom>=0xc100u&&atom<0xc100u+64&&atoms[atom-0xc100u].refs)atoms[atom-0xc100u].refs--;tribes_set_last_error(0);return 0;
}
/* Hot keys: TribesPad has no system-wide shortcuts to reserve; registration succeeds and never fires. */
uint32_t vg_register_hot_key(uint32_t window,uint32_t id,uint32_t modifiers,uint32_t key){(void)window;(void)id;(void)modifiers;(void)key;return 1;}
uint32_t vg_unregister_hot_key(uint32_t window,uint32_t id){(void)window;(void)id;return 1;}
/* SetKeyboardState: WinDrv clears its keys when it loses focus; TribesPad's key state follows the host keyboard. */
uint32_t vg_set_keyboard_state(uint32_t state){(void)state;return 1;}

/* GetFullPathName: the current directory joined with a relative name, "." and
   ".." resolved, "/" read as "\". Nothing is checked on disk, as on Windows.
   https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew */
static uint32_t full_path(_cpu *c,uint32_t name,uint32_t capacity,uint32_t output,uint32_t file_part,int wide){
 uint64_t base=c->_pointer_offset;char in[PATH_MAX],cwd[PATH_MAX],joined[2*PATH_MAX],out[PATH_MAX];
 if(!read_ascii(base,name,wide,in,sizeof in))return 0;
 if(!in[0]){tribes_set_last_error(123);return 0;}
 for(char *p=in;*p;p++)if(*p=='/')*p='\\';
 uint32_t at=scratch(c,0),n=tribes_get_current_directory(base,sizeof cwd,at);if(!n||n>=sizeof cwd)return 0;
 memcpy(cwd,GUEST(base,at),n+1);
 if(in[0]&&in[1]==':'&&in[2]=='\\')snprintf(joined,sizeof joined,"%s",in);
 else if(in[0]&&in[1]==':'){tribes_set_last_error(50);return 0;} /* drive-relative: per-drive directories are not modeled */
 else if(in[0]=='\\'){if(in[1]=='\\'){tribes_set_last_error(50);return 0;}snprintf(joined,sizeof joined,"%.2s%s",cwd,in);}
 else snprintf(joined,sizeof joined,"%s\\%s",cwd,in);
 /* Rebuild component by component after "X:". */
 size_t used=2;memcpy(out,joined,2);int trailing=joined[strlen(joined)-1]=='\\';
 char *save=NULL;for(char *part=strtok_r(joined+2,"\\",&save);part;part=strtok_r(NULL,"\\",&save)){
  if(!strcmp(part,"."))continue;
  if(!strcmp(part,"..")){while(used>2&&out[used-1]!='\\')used--;if(used>2)used--;continue;}
  size_t k=strlen(part);if(used+1+k+2>=sizeof out){tribes_set_last_error(206);return 0;}
  out[used++]='\\';memcpy(out+used,part,k);used+=k;
 }
 if(used==2||trailing)out[used++]='\\';
 out[used]=0;
 uint32_t length=(uint32_t)used,unit=wide?2:1;
 if(capacity<=length)return length+1;
 if(!in_guest(base,output,unit*(length+1))){tribes_set_last_error(87);return 0;}
 if(wide)widen(base,output,out,length+1);else memcpy(GUEST(base,output),out,length+1);
 if(file_part&&in_guest(base,file_part,4)){
  const char *slash=strrchr(out,'\\');uint32_t value=slash&&slash[1]?output+unit*(uint32_t)(slash+1-out):0;
  memcpy(GUEST(base,file_part),&value,4);
 }
 return length;
}
uint32_t vg_get_full_path_name_a(_cpu *c,uint32_t name,uint32_t capacity,uint32_t output,uint32_t file_part){return full_path(c,name,capacity,output,file_part,0);}
uint32_t vg_get_full_path_name_w(_cpu *c,uint32_t name,uint32_t capacity,uint32_t output,uint32_t file_part){return full_path(c,name,capacity,output,file_part,1);}

/* FindFirstFileW / FindNextFileW over TribesPad's A search: WIN32_FIND_DATAA
   (320 bytes) becomes WIN32_FIND_DATAW (592 bytes); the first 44 bytes are the
   same, then cFileName[260] and cAlternateFileName[14] widen. */
static uint32_t find_widen(uint64_t base,uint32_t from,uint32_t output){
 if(!in_guest(base,output,592)){tribes_set_last_error(87);return 0;}
 const unsigned char *a=GUEST(base,from);unsigned char record[592];memset(record,0,sizeof record);memcpy(record,a,44);
 for(int i=0;i<260;i++){uint16_t v=a[44+i];memcpy(record+44+2*i,&v,2);}
 for(int i=0;i<14;i++){uint16_t v=a[304+i];memcpy(record+564+2*i,&v,2);}
 memcpy(GUEST(base,output),record,sizeof record);return 1;
}
uint32_t vg_find_first_w(_cpu *c,uint32_t pattern,uint32_t output){
 uint32_t name=scratch(c,0),data=scratch(c,PATH_MAX);if(!narrow_to_scratch(c,pattern,name))return UINT32_MAX;
 uint32_t handle=tribes_find_first(c->_pointer_offset,name,data);
 if(handle!=UINT32_MAX&&!find_widen(c->_pointer_offset,data,output))return UINT32_MAX;
 return handle;
}
uint32_t vg_find_next_w(_cpu *c,uint32_t handle,uint32_t output){
 uint32_t data=scratch(c,0);if(!tribes_find_next(c->_pointer_offset,handle,data))return 0;
 return find_widen(c->_pointer_offset,data,output);
}

/* Host path of an existing guest path, or 0 with the Windows error. */
static int existing(uint64_t base,uint32_t name,int wide,char guest[PATH_MAX],char host[PATH_MAX]){
 if(!read_ascii(base,name,wide,guest,PATH_MAX))return 0;
 if(!tribes_filesystem_resolve_link(base,guest,host,PATH_MAX)){tribes_set_last_error(error_of(errno));return 0;}
 return 1;
}
static void filetime(unsigned char *at,struct timespec t){
 uint64_t v=(uint64_t)t.tv_sec*10000000u+(uint64_t)t.tv_nsec/100u+UINT64_C(116444736000000000);memcpy(at,&v,8);
}
/* BOOL GetFileAttributesEx(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID): level 0,
   WIN32_FILE_ATTRIBUTE_DATA (36 bytes); attributes as GetFileAttributes reports. */
static uint32_t attributes_ex(_cpu *c,uint32_t name,uint32_t level,uint32_t output,int wide){
 uint64_t base=c->_pointer_offset;char guest[PATH_MAX],host[PATH_MAX];
 if(level){tribes_set_last_error(87);return 0;}
 if(!in_guest(base,output,36)){tribes_set_last_error(87);return 0;}
 if(!existing(base,name,wide,guest,host))return 0;
 uint32_t at=scratch(c,0);memcpy(GUEST(base,at),guest,strlen(guest)+1);
 uint32_t attributes=tribes_get_file_attributes(base,at);if(attributes==UINT32_MAX)return 0;
 struct stat st;if(lstat(host,&st)){tribes_set_last_error(error_of(errno));return 0;}
 unsigned char record[36];memcpy(record,&attributes,4);
 filetime(record+4,st.st_birthtimespec);filetime(record+12,st.st_atimespec);filetime(record+20,st.st_mtimespec);
 uint64_t size=S_ISDIR(st.st_mode)?0:(uint64_t)st.st_size;uint32_t high=(uint32_t)(size>>32),low=(uint32_t)size;
 memcpy(record+28,&high,4);memcpy(record+32,&low,4);memcpy(GUEST(base,output),record,36);return 1;
}
uint32_t vg_get_file_attributes_ex_a(_cpu *c,uint32_t name,uint32_t level,uint32_t output){return attributes_ex(c,name,level,output,0);}
uint32_t vg_get_file_attributes_ex_w(_cpu *c,uint32_t name,uint32_t level,uint32_t output){return attributes_ex(c,name,level,output,1);}

/* DeleteFile: files only (a directory is ERROR_ACCESS_DENIED, as on Windows). */
static uint32_t delete_file(uint64_t base,uint32_t name,int wide){
 char guest[PATH_MAX],host[PATH_MAX];if(!existing(base,name,wide,guest,host))return 0;
 struct stat st;if(lstat(host,&st)){tribes_set_last_error(error_of(errno));return 0;}
 if(S_ISDIR(st.st_mode)){tribes_set_last_error(5);return 0;}
 if(unlink(host)){tribes_set_last_error(error_of(errno));return 0;}return 1;
}
uint32_t vg_delete_file_a(uint64_t base,uint32_t name){return delete_file(base,name,0);}
uint32_t vg_delete_file_w(uint64_t base,uint32_t name){return delete_file(base,name,1);}
static uint32_t remove_directory(uint64_t base,uint32_t name,int wide){
 char guest[PATH_MAX],host[PATH_MAX];if(!existing(base,name,wide,guest,host))return 0;
 if(rmdir(host)){tribes_set_last_error(errno==ENOTDIR?267:error_of(errno));return 0;}return 1;
}
uint32_t vg_remove_directory_a(uint64_t base,uint32_t name){return remove_directory(base,name,0);}
uint32_t vg_remove_directory_w(uint64_t base,uint32_t name){return remove_directory(base,name,1);}
/* MoveFileW: MoveFileExW without flags (an existing target fails). */
uint32_t vg_move_file_w(uint64_t base,uint32_t from,uint32_t to){return tribes_move_file_ex_w(base,from,to,0);}
uint32_t vg_move_file_a(_cpu *c,uint32_t from,uint32_t to){
 uint64_t base=c->_pointer_offset;char a[PATH_MAX],b[PATH_MAX];
 if(!read_ascii(base,from,0,a,sizeof a)||!read_ascii(base,to,0,b,sizeof b))return 0;
 uint32_t x=scratch(c,0),y=scratch(c,2*PATH_MAX);widen(base,x,a,strlen(a)+1);widen(base,y,b,strlen(b)+1);
 return tribes_move_file_ex_w(base,x,y,0);
}
/* SetFileAttributes: read-only maps to the owner's write permission; archive,
   normal, hidden and the rest have no host equivalent and are accepted. */
static uint32_t set_attributes(uint64_t base,uint32_t name,uint32_t attributes,int wide){
 char guest[PATH_MAX],host[PATH_MAX];if(!existing(base,name,wide,guest,host))return 0;
 struct stat st;if(stat(host,&st)){tribes_set_last_error(error_of(errno));return 0;}
 mode_t mode=attributes&1?st.st_mode&~(mode_t)(S_IWUSR|S_IWGRP|S_IWOTH):st.st_mode|S_IWUSR;
 if(chmod(host,mode&07777)){tribes_set_last_error(error_of(errno));return 0;}return 1;
}
uint32_t vg_set_file_attributes_a(uint64_t base,uint32_t name,uint32_t attributes){return set_attributes(base,name,attributes,0);}
uint32_t vg_set_file_attributes_w(uint64_t base,uint32_t name,uint32_t attributes){return set_attributes(base,name,attributes,1);}
uint32_t vg_get_drive_type_w(_cpu *c,uint32_t root){
 if(!root)return tribes_get_drive_type(c->_pointer_offset,0);
 uint32_t at=scratch(c,0);if(!narrow_to_scratch(c,root,at))return 1;
 return tribes_get_drive_type(c->_pointer_offset,at);
}
/* BOOL GetDiskFreeSpaceExA(LPCSTR, PULARGE_INTEGER x3): bytes from statvfs. */
uint32_t vg_get_disk_free_space_ex_a(uint64_t base,uint32_t root,uint32_t available,uint32_t total,uint32_t free_bytes){
 char guest[PATH_MAX]="C:\\",host[PATH_MAX];
 if(root&&!read_ascii(base,root,0,guest,sizeof guest))return 0;
 if(!tribes_filesystem_resolve(base,guest,host,sizeof host)){tribes_set_last_error(error_of(errno));return 0;}
 struct statvfs s;if(statvfs(host,&s)){tribes_set_last_error(5);return 0;}
 uint64_t unit=s.f_frsize?s.f_frsize:s.f_bsize,values[]={unit*s.f_bavail,unit*s.f_blocks,unit*s.f_bfree};uint32_t at[]={available,total,free_bytes};
 for(int i=0;i<3;i++)if(at[i]){if(!in_guest(base,at[i],8)){tribes_set_last_error(87);return 0;}memcpy(GUEST(base,at[i]),&values[i],8);}
 return 1;
}

/* IsBadReadPtr / IsBadWritePtr / IsBadCodePtr: whether every page of the range
   is committed with the access, from TribesPad's VirtualQuery. MSVC's C++
   exception handler checks thrown objects and type information this way.
   https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-isbadreadptr */
extern uint32_t tribes_virtual_query(uint64_t base,uint32_t address,uint32_t output,uint32_t length);
static uint32_t bad_range(_cpu *c,uint32_t address,uint32_t size,int write){
 if(!size)return 0;
 if(!address||address>UINT32_MAX-(size-1))return 1;
 uint64_t base=c->_pointer_offset;uint32_t info=scratch(c,0),end=address+(size-1),at=address;
 for(;;){
  if(!tribes_virtual_query(base,at,info,28))return 1;
  uint32_t m[7];memcpy(m,GUEST(base,info),28);
  uint32_t protect=m[5]&0xff;
  int readable=protect==2||protect==4||protect==8||protect==0x20||protect==0x40||protect==0x80;
  int writable=protect==4||protect==8||protect==0x40||protect==0x80;
  if(m[4]!=0x1000||(m[5]&0x100)||!(write?writable:readable))return 1;
  uint64_t next=(uint64_t)m[0]+m[3];if(next>end)return 0;at=(uint32_t)next;
 }
}
uint32_t vg_is_bad_read_ptr(_cpu *c,uint32_t address,uint32_t size){return bad_range(c,address,size,0);}
uint32_t vg_is_bad_write_ptr(_cpu *c,uint32_t address,uint32_t size){return bad_range(c,address,size,1);}
uint32_t vg_is_bad_code_ptr(_cpu *c,uint32_t address){return bad_range(c,address,1,0);}

/* HMODULE LoadLibraryW(LPCWSTR): LoadLibraryExW without flags (TribesPad's
   loader: ".dll" appended, full paths matched, references counted, and an
   unknown library still a hard stop that names it). */
extern uint32_t tribes_load_library_ex_w_cpu(_cpu *cpu,uint32_t pointer,uint32_t file,uint32_t flags);
/* Unreal asks for a native DLL for every package it loads (Transient, Engine,
   ...); for a script-only package the file does not exist and Windows returns
   NULL with ERROR_MOD_NOT_FOUND. That is answered here. A DLL that exists in the
   game but is not part of this build still stops in TribesPad's loader. */
uint32_t vg_load_library_w(_cpu *c,uint32_t name){
 uint64_t base=c->_pointer_offset;char guest[PATH_MAX],path[PATH_MAX+8],host[PATH_MAX];
 if(!read_ascii(base,name,1,guest,sizeof guest))return 0;
 const char *leaf=strrchr(guest,'\\');leaf=leaf?leaf+1:guest;
 /* ALAudio's OpenAL32.dll is TribesPad's native OpenAL (OpenAL Soft), as for Tribes 2. */
 if(!strcasecmp(leaf,"OpenAL32.dll")||!strcasecmp(leaf,"OpenAL32")){
  extern uint32_t tribes_openal_load(uint64_t base,const char *name);
  extern int tribes_module_retain_native(uint64_t base,uint32_t module);
  uint32_t module=tribes_openal_load(base,"openal32.dll");
  if(module&&tribes_module_retain_native(base,module))return module;
  tribes_set_last_error(126);return 0;
 }
 snprintf(path,sizeof path,"%s%s%s",strchr(guest,'\\')?"":"C:\\TribesPad\\Game\\Program\\Bin\\",guest,strchr(leaf,'.')?"":".dll");
 struct stat st;
 if(!tribes_filesystem_resolve(base,path,host,sizeof host)||stat(host,&st)){tribes_set_last_error(126);return 0;}
 extern void vengeance_load_dependencies(_cpu *c,const char *leaf,uint32_t scratch);
 const char *file=strrchr(path,'\\');vengeance_load_dependencies(c,file?file+1:path,scratch(c,0));
 return tribes_load_library_ex_w_cpu(c,name,0,0);
}

/* BOOL FreeLibrary(HMODULE). The game loads and frees some of its own DLLs
   (RTCShader probes Direct3D at start-up, then is freed). TribesPad maps every
   translated image once and cannot unmap one, so a translated DLL is pinned
   instead (GetModuleHandleEx PIN): it stays attached and a later LoadLibrary
   finds it as it was, as when another reference keeps a DLL loaded on Windows. */
extern uint32_t tribes_get_module_handle_ex_w(uint64_t base,uint32_t flags,uint32_t name,uint32_t output);
extern uint32_t tribes_free_library_cpu(_cpu *cpu,uint32_t image);
uint32_t vg_free_library(_cpu *c,uint32_t image){
 if(image>=0x10000000u&&image<0x20000000u){   /* the translated modules (config/vengeance/layout.json) */
  uint32_t error=tribes_get_last_error();
  int pinned=tribes_get_module_handle_ex_w(c->_pointer_offset,5,image,scratch(c,0));   /* PIN | FROM_ADDRESS */
  fprintf(stderr,"vengeance: FreeLibrary 0x%08x: %s\n",image,pinned?"pinned, stays attached":"not a mapped image");
  tribes_set_last_error(error);
 }
 return tribes_free_library_cpu(c,image);
}

/* int MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type): TribesPad
   has no desktop to show it on, so the box goes to the log and its default
   button is the answer (the first button unless MB_DEFBUTTON2/3 says otherwise).
   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-messageboxw */
static void wide_text(uint64_t base,uint32_t at,char *out,size_t n){
 size_t i=0;if(at)for(;i+1<n;i++){uint16_t v;if(!in_guest(base,at+2*(uint32_t)i,2))break;memcpy(&v,GUEST(base,at+2*(uint32_t)i),2);if(!v)break;out[i]=v<128?(char)v:'?';}
 out[i]=0;
}
uint32_t vg_message_box_w(uint64_t base,uint32_t window,uint32_t text,uint32_t caption,uint32_t type){
 (void)window;char t[2048],c[256];wide_text(base,text,t,sizeof t);wide_text(base,caption,c,sizeof c);
 fprintf(stderr,"vengeance MessageBox [%s]: %s\n",c,t);
 static const uint32_t buttons[][3]={{1,0,0},{1,2,0},{3,4,5},{6,7,2},{6,7,0},{4,2,0},{2,10,11}};
 uint32_t kind=type&0xf,which=(type>>8)&0xf;if(kind>6)kind=0;if(which>2||!buttons[kind][which])which=0;
 return buttons[kind][which];
}

/* CreateFileW: TribesPad's service, plus one case Unreal's start-up uses to
   test that it may write to its folder (Core, appInit): OPEN_EXISTING on the
   folder itself with FILE_FLAG_BACKUP_SEMANTICS and GENERIC_WRITE. Windows
   returns a directory handle when the account may write there. Here the host
   folder's write permission is checked and the handle is opened for
   attributes only; nothing writes through a directory handle. */
extern uint32_t tribes_create_file_w(uint64_t base,uint32_t name,uint32_t access,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle);
uint32_t vg_create_file_w(uint64_t base,uint32_t name,uint32_t rights,uint32_t share,uint32_t security,uint32_t creation,uint32_t flags,uint32_t template_handle){
 const uint32_t write=UINT32_C(0x40000000)|2u|4u|0x100u|0x10u;
 if(creation==3&&(flags&0x02000000u)&&(rights&write)){
  char guest[PATH_MAX],host[PATH_MAX];struct stat st;
  if(read_ascii(base,name,1,guest,sizeof guest)&&tribes_filesystem_resolve(base,guest,host,sizeof host)&&!stat(host,&st)&&S_ISDIR(st.st_mode)){
   if(access(host,W_OK)){tribes_set_last_error(5);return UINT32_MAX;}
   rights&=~write;
  }
 }
 return tribes_create_file_w(base,name,rights,share,security,creation,flags,template_handle);
}

/* Identity and system information, the values a single-user Windows XP
   machine reports. */
static uint32_t copy_name(uint64_t base,const char *name,uint32_t output,uint32_t size_pointer,int wide,int user){
 if(!in_guest(base,size_pointer,4)){tribes_set_last_error(87);return 0;}
 uint32_t capacity;memcpy(&capacity,GUEST(base,size_pointer),4);uint32_t n=(uint32_t)strlen(name);
 if(capacity<n+1){uint32_t need=n+1;memcpy(GUEST(base,size_pointer),&need,4);tribes_set_last_error(user?122:111);return 0;}
 if(!in_guest(base,output,(n+1)*(wide?2:1))){tribes_set_last_error(87);return 0;}
 if(wide)widen(base,output,name,n+1);else memcpy(GUEST(base,output),name,n+1);
 uint32_t reported=user?n+1:n;memcpy(GUEST(base,size_pointer),&reported,4);return 1;
}
/* GetComputerName: length without the NUL on success; GetUserName: with it. */
uint32_t vg_get_computer_name_a(uint64_t base,uint32_t output,uint32_t size){return copy_name(base,"TRIBESPAD",output,size,0,0);}
uint32_t vg_get_computer_name_w(uint64_t base,uint32_t output,uint32_t size){return copy_name(base,"TRIBESPAD",output,size,1,0);}
uint32_t vg_get_user_name_a(uint64_t base,uint32_t output,uint32_t size){return copy_name(base,"Player",output,size,0,1);}
uint32_t vg_get_user_name_w(uint64_t base,uint32_t output,uint32_t size){return copy_name(base,"Player",output,size,1,1);}
/* BOOL GetVersionExW(LPOSVERSIONINFOW): the profile TribesPad's GetVersionExA
   reports (Windows XP 5.1.2600, Service Pack 3), as OSVERSIONINFOW (276) or
   OSVERSIONINFOEXW (284 bytes). */
uint32_t vg_get_version_ex_w(uint64_t base,uint32_t output){
 if(!in_guest(base,output,4)){tribes_set_last_error(87);return 0;}
 uint32_t size;memcpy(&size,GUEST(base,output),4);
 if((size!=276&&size!=284)||!in_guest(base,output,size)){tribes_set_last_error(87);return 0;}
 unsigned char value[284]={0};uint32_t fields[]={size,5,1,2600,2};memcpy(value,fields,sizeof fields);
 const char *pack="Service Pack 3";for(size_t i=0;pack[i];i++){uint16_t v=(unsigned char)pack[i];memcpy(value+20+2*i,&v,2);}
 if(size==284){uint16_t sp=3,suite=0x100;memcpy(value+276,&sp,2);memcpy(value+280,&suite,2);value[282]=1;}
 memcpy(GUEST(base,output),value,size);return 1;
}
/* void GlobalMemoryStatus(LPMEMORYSTATUS): a 1 GiB machine of the game's era
   with 768 MiB free and the 2 GiB user address space of 32-bit Windows. */
void vg_global_memory_status(uint64_t base,uint32_t output){
 if(!in_guest(base,output,32))return;
 uint32_t v[8]={32,25,1u<<30,768u<<20,2u<<30,1536u<<20,0x7ffe0000u,0x7f000000u};memcpy(GUEST(base,output),v,32);
}
uint32_t vg_get_user_default_lcid(void){return 0x409;}
static int english(uint32_t lcid){return lcid==0x409||lcid==0x400||lcid==0x800||lcid==0x7f;}
uint32_t vg_is_valid_locale(uint32_t lcid,uint32_t flags){(void)flags;return lcid==0x409;}
/* CompareStringW / GetLocaleInfoW by LCID: TribesPad's Ex services for en-US,
   the only locale this profile has; any other LCID is ERROR_INVALID_PARAMETER. */
extern uint32_t tribes_compare_string_ex(uint64_t base,uint32_t locale,uint32_t flags,uint32_t a,uint32_t na,uint32_t b,uint32_t nb,uint32_t version,uint32_t reserved,uint32_t handle);
extern uint32_t tribes_get_locale_info_ex(uint64_t base,uint32_t locale,uint32_t type,uint32_t output,uint32_t capacity);
uint32_t vg_compare_string_w(uint64_t base,uint32_t lcid,uint32_t flags,uint32_t a,uint32_t na,uint32_t b,uint32_t nb){
 if(!english(lcid)){tribes_set_last_error(87);return 0;}
 return tribes_compare_string_ex(base,0,flags,a,na,b,nb,0,0,0);
}
uint32_t vg_get_locale_info_w(uint64_t base,uint32_t lcid,uint32_t type,uint32_t output,uint32_t capacity){
 if(!english(lcid)){tribes_set_last_error(87);return 0;}
 return tribes_get_locale_info_ex(base,0,type,output,capacity);
}
/* DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION): the host's current
   offset as the bias, without daylight-saving rules (TIME_ZONE_ID_UNKNOWN). */
#include <time.h>
uint32_t vg_get_time_zone_information(uint64_t base,uint32_t output){
 if(!in_guest(base,output,172)){tribes_set_last_error(87);return UINT32_MAX;}
 time_t now=time(NULL);struct tm local;localtime_r(&now,&local);
 unsigned char value[172]={0};int32_t bias=(int32_t)(-local.tm_gmtoff/60);memcpy(value,&bias,4);
 memcpy(GUEST(base,output),value,sizeof value);return 0;
}
/* BOOL GetProcessWorkingSetSize(HANDLE, PSIZE_T, PSIZE_T): Windows' defaults. */
uint32_t vg_get_process_working_set_size(uint64_t base,uint32_t process,uint32_t minimum,uint32_t maximum){
 (void)process;if(!in_guest(base,minimum,4)||!in_guest(base,maximum,4)){tribes_set_last_error(87);return 0;}
 uint32_t lo=204800,hi=1413120;memcpy(GUEST(base,minimum),&lo,4);memcpy(GUEST(base,maximum),&hi,4);return 1;
}

/* LPVOID VirtualAlloc: TribesPad's service, with commits rounded out to whole
   host pages (16 KiB on Apple silicon, four Windows pages) inside the same
   reservation. MSVC's runtime commits 4 and 8 KiB pieces; TribesPad backs and
   protects memory per host page. A fresh allocation already reserves 64 KiB,
   so its rounded commit stays inside it. Windows allows committing pages that
   are already committed; the address returned is the one Windows returns.
   https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc */
#include "../core/guest_virtual_memory.h"
uint32_t vg_virtual_alloc(uint64_t base,uint32_t address,uint32_t size,uint32_t type,uint32_t protection){
 const uint32_t host=(uint32_t)getpagesize();
 if(!(type&0x1000)||!size||host<=4096)return tribes_virtual_alloc(base,address,size,type,protection);
 if(!address){
  uint32_t rounded=(size+host-1)&~(host-1);if(rounded<size)return tribes_virtual_alloc(base,address,size,type,protection);
  return tribes_virtual_alloc(base,0,rounded,type,protection);
 }
 uint32_t start=address&~(host-1);uint64_t end=((uint64_t)address+size+host-1)&~(uint64_t)(host-1);
 if(end>UINT32_MAX)return tribes_virtual_alloc(base,address,size,type,protection);
 uint32_t result=tribes_virtual_alloc(base,start,(uint32_t)(end-start),type,protection);
 return result?address&~4095u:0;
}

/* HRESULT DirectDrawCreate(GUID *, LPDIRECTDRAW *, IUnknown *): TribesPad has no
   DirectDraw. The game only uses it to measure local video memory before
   Direct3D starts; on failure it logs "Couldn't query amount of local video
   memory" and continues with no limit. DDERR_NODIRECTDRAWHW. */
uint32_t vg_direct_draw_create(uint64_t base,uint32_t guid,uint32_t output,uint32_t outer){
 (void)guid;(void)outer;if(output&&in_guest(base,output,4)){uint32_t zero=0;memcpy(GUEST(base,output),&zero,4);}
 return 0x887600ffu;
}

/* COM start-up for a machine with no registered COM servers: per-thread
   initialization counts (S_OK the first time, S_FALSE after), CoCreateInstance
   answers REGDB_E_CLASSNOTREG and logs the class asked for, CoCreateGuid makes
   random (version 4) GUIDs. IMM: no input method editor is installed, so there
   is no input context (NULL), as on such Windows systems. */
#include <stdlib.h>
static _Thread_local uint32_t com_count;
uint32_t vg_co_initialize(uint32_t reserved){(void)reserved;return com_count++?1u:0u;}
uint32_t vg_co_initialize_ex(uint32_t reserved,uint32_t model){(void)reserved;(void)model;return com_count++?1u:0u;}
void vg_co_uninitialize(void){if(com_count)com_count--;}
uint32_t vg_co_create_instance(uint64_t base,uint32_t clsid,uint32_t outer,uint32_t context,uint32_t iid,uint32_t output){
 (void)outer;(void)context;(void)iid;
 if(output&&in_guest(base,output,4)){uint32_t zero=0;memcpy(GUEST(base,output),&zero,4);}
 if(clsid&&in_guest(base,clsid,16)){const uint8_t *g=GUEST(base,clsid);
  fprintf(stderr,"vengeance: CoCreateInstance {%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}: class not registered\n",
   g[3],g[2],g[1],g[0],g[5],g[4],g[7],g[6],g[8],g[9],g[10],g[11],g[12],g[13],g[14],g[15]);}
 return 0x80040154u;
}
uint32_t vg_co_create_guid(uint64_t base,uint32_t output){
 if(!in_guest(base,output,16))return 0x80070057u;
 uint8_t g[16];arc4random_buf(g,sizeof g);g[7]=(uint8_t)((g[7]&0x0f)|0x40);g[8]=(uint8_t)((g[8]&0x3f)|0x80);
 memcpy(GUEST(base,output),g,16);return 0;
}
void vg_init_common_controls(void){}
uint32_t vg_imm_get_context(uint32_t window){(void)window;return 0;}
uint32_t vg_imm_release_context(uint32_t window,uint32_t context){(void)window;(void)context;return 1;}
uint32_t vg_imm_associate_context(uint32_t window,uint32_t context){(void)window;(void)context;return 0;}
uint32_t vg_imm_create_context(void){return 0;}
uint32_t vg_imm_destroy_context(uint32_t context){(void)context;return 0;}

/* OLE automation strings and variants, for the client's WMI clean-up at exit
   (its WMI query itself finds no class: CoCreateInstance reports it unregistered).
   A BSTR is a length-prefixed wide string; the pointer is to its characters. */
extern uint32_t tribes_global_alloc(uint64_t base,uint32_t flags,uint32_t bytes);
extern uint32_t tribes_global_free(uint64_t base,uint32_t address);
uint32_t vg_sys_alloc_string(uint64_t base,uint32_t text){
 if(!text)return 0;uint32_t n=0;uint16_t v;do{memcpy(&v,GUEST(base,text+2*n),2);n++;}while(v&&n<0x100000);n--;
 uint32_t error=tribes_get_last_error(),block=tribes_global_alloc(base,0,4+2*n+2);tribes_set_last_error(error);if(!block)return 0;
 uint32_t bytes=2*n;memcpy(GUEST(base,block),&bytes,4);memcpy(GUEST(base,block+4),GUEST(base,text),2*n+2);return block+4;
}
void vg_sys_free_string(uint64_t base,uint32_t bstr){if(bstr){uint32_t error=tribes_get_last_error();tribes_global_free(base,bstr-4);tribes_set_last_error(error);}}
void vg_variant_init(uint64_t base,uint32_t variant){if(variant)memset(GUEST(base,variant),0,2);}
uint32_t vg_variant_clear(uint64_t base,uint32_t variant){
 if(!variant)return 0x80070057;uint16_t vt;memcpy(&vt,GUEST(base,variant),2);
 if(vt==8){uint32_t bstr;memcpy(&bstr,GUEST(base,variant+8),4);vg_sys_free_string(base,bstr);}
 else if(vt==9||vt==13||(vt&0x6000)){fprintf(stderr,"TribesPad: unsupported VariantClear of type 0x%x\n",vt);abort();}
 memset(GUEST(base,variant),0,2);return 0;
}
