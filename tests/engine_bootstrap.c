/* Diagnostic only: enter original CRT code, stop on every unimplemented API.
   This is not a Windows runtime or a playable client. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../port/llasm-support/llasm_cpu.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_dinput.h"
#include "../port/core/keyboard_state.h"
#include "../port/core/guest_icons.h"
#include "../port/core/guest_cursors.h"
#include "../port/core/guest_display.h"
#include "../port/apple/display.h"
#include "bootstrap_imports.h"
#include "../port/core/process_environment.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/process_startup.h"
#include "../port/core/guest_modules.h"
#include "../port/core/native_handles.h"
extern void tribes_enter(_cpu *, uint32_t);
extern void x87_fninit_void(_cpu *);
extern void tb_sse_init(_cpu *);
extern uint32_t tribes_exception_filter(void);
static _cpu state;
static TBAppleDisplay apple_display;
static FILE *find_trace,*icon_trace,*cursor_trace,*brush_trace;
void tribes_find_observation(uint32_t operation,uint32_t argument,uint32_t output,uint32_t result,uint32_t error,const void *data){uint32_t fields[]={operation,argument,output,result,error,data?320:0};if(fwrite(fields,4,6,find_trace)!=6||(data&&fwrite(data,1,320,find_trace)!=320))exit(73);fflush(find_trace);}
void tribes_brush_observation(uint32_t index,uint32_t handle,uint32_t style,uint32_t color) {uint32_t fields[]={index,handle,style,color};if(fwrite(fields,4,4,brush_trace)!=4)exit(73);}
void tribes_cursor_observation(uint32_t module,uint32_t name,uint32_t handle,const TribesCursorView *view) {
    uint32_t fields[]={module,name,handle,view->width,view->height,view->hotspot_x,view->hotspot_y,view->system_id};size_t pixels=(size_t)view->width*view->height;
    if(fwrite(fields,4,8,cursor_trace)!=8||fwrite(view->rgba,4,pixels,cursor_trace)!=pixels||fwrite(view->and_mask,1,pixels,cursor_trace)!=pixels)exit(73);
}
void tribes_icon_observation(uint32_t module,uint32_t name,uint32_t handle,const TribesIconView *view) {
    uint32_t fields[]={module,name,handle,view->width,view->height,view->resource_id,view->bits_per_pixel,view->uses_alpha};
    size_t pixels=(size_t)view->width*view->height;
    if(fwrite(fields,4,8,icon_trace)!=8||fwrite(view->rgba,4,pixels,icon_trace)!=pixels||fwrite(view->and_mask,1,pixels,icon_trace)!=pixels)exit(73);
}
static uint64_t guest_base;
static const char *stack_output;
static size_t image_size;
static FILE *clock_trace,*disk_trace,*window_trace,*input_trace,*object_trace,*format_trace,*property_trace;
void tribes_object_observation(uint32_t callback,uint32_t context,uint32_t descriptor,const void *bytes,uint32_t size) {
 uint32_t fields[]={callback,context,descriptor,size};if(fwrite(fields,4,4,object_trace)!=4||fwrite(bytes,1,size,object_trace)!=size)exit(73);fflush(object_trace);
}
void tribes_property_observation(uint32_t object,uint32_t property,uint32_t value,uint32_t usable) {
 uint32_t fields[]={object,property,value,usable};if(fwrite(fields,4,4,property_trace)!=4)exit(73);fflush(property_trace);
}
void tribes_format_observation(uint32_t object,uint32_t flags,uint32_t size,uint32_t count,const void *mapping) {
 uint32_t fields[]={object,flags,size,count};if(fwrite(fields,4,4,format_trace)!=4||(count&&fwrite(mapping,12,count,format_trace)!=count))exit(73);fflush(format_trace);
}
static int heap_snapshot(uint32_t address,uint32_t span,void *file) {uint32_t fields[]={address,span};return fwrite(fields,4,2,file)==2&&fwrite((void *)(uintptr_t)(guest_base+address),1,span,file)==span;}
void tribes_input_observation(uint32_t callback,uint32_t context,uint32_t descriptor,const void *bytes,uint32_t size) {
 uint32_t fields[]={callback,context,descriptor,size};if(fwrite(fields,4,4,input_trace)!=4||fwrite(bytes,1,size,input_trace)!=size)exit(73);fflush(input_trace);
}
void tribes_window_observation(uint32_t proc,uint32_t handle,uint32_t message,uint32_t wp,uint32_t lp) {
    uint32_t values[]={proc,handle,message,wp,lp};if(fwrite(values,4,5,window_trace)!=5)exit(73);fflush(window_trace);
}
void tribes_disk_observation(const uint32_t *args,const uint32_t *values) {
    if(fwrite(args,4,5,disk_trace)!=5||fwrite(values,4,4,disk_trace)!=4)exit(73);
}
void tribes_clock_observation(uint32_t address,uint64_t value) {
    if(fwrite(&address,4,1,clock_trace)!=1||fwrite(&value,8,1,clock_trace)!=1)exit(73);
}
static _Thread_local uint32_t seh_head = UINT32_MAX;
uint32_t X86_InterruptFlag;
uint32_t x86_read_fs_dword(uint32_t address) {
    if (address != 0) { fprintf(stderr,"Unsupported bootstrap FS read: %x\n",address); exit(72); }
    return seh_head;
}
void x86_write_fs_dword(uint32_t address, uint32_t value) {
    if (address != 0) { fprintf(stderr,"Unsupported bootstrap FS write: %x\n",address); exit(72); }
    seh_head = value;
}
static _Noreturn void diagnostic_stop(_cpu *cpu,uint32_t index,int hardware) {
    if (!hardware&&index >= TB_IMPORT_COUNT) abort();
    FILE *dump=fopen(stack_output,"wb");
    if (!dump || fwrite((void *)(uintptr_t)(guest_base+0x100000),1,0x200000,dump)!=0x200000 || fclose(dump)!=0) exit(73);
    size_t length=strlen(stack_output)+7;char *image_output=malloc(length);
    if(!image_output)exit(73);
    snprintf(image_output,length,"%s.image",stack_output);dump=fopen(image_output,"wb");free(image_output);
    if(!dump || fwrite((void *)(uintptr_t)(guest_base+0x400000),1,image_size,dump)!=image_size || fclose(dump)!=0)exit(73);
    if(hardware==2){
        uint32_t object;memcpy(&object,(void *)(uintptr_t)(guest_base+cpu->_esp+4),4);
        unsigned char snapshot[116];uint32_t object_size=index>=100?112:40;memcpy(snapshot,&object,4);memcpy(snapshot+4,(void *)(uintptr_t)(guest_base+object),object_size);
        size_t bytes=strlen(stack_output)+7;char *path=malloc(bytes);if(!path)exit(73);snprintf(path,bytes,"%s.input",stack_output);dump=fopen(path,"wb");free(path);
        if(!dump||fwrite(snapshot,1,object_size+4,dump)!=object_size+4||fclose(dump)!=0)exit(73);
    }
    size_t heap_path_size=strlen(stack_output)+9;char *heap_path=malloc(heap_path_size);if(!heap_path)exit(73);snprintf(heap_path,heap_path_size,"%s.heap",stack_output);dump=fopen(heap_path,"wb");free(heap_path);if(!dump||!tribes_heap_visit(guest_base,heap_snapshot,dump)||fclose(dump)!=0)exit(73);
    if(fclose(brush_trace)!=0)exit(73);
    if(fclose(cursor_trace)!=0)exit(73);
    if(fclose(icon_trace)!=0)exit(73);
    if(fclose(find_trace)!=0||fclose(clock_trace)!=0||fclose(disk_trace)!=0||fclose(window_trace)!=0||fclose(input_trace)!=0||fclose(object_trace)!=0||fclose(format_trace)!=0||fclose(property_trace)!=0)exit(73);
    printf("{\"diagnostic_stop\":true,\"%s\":%u,\"regs\":[",hardware==2?"dinput_method":hardware?"helper_index":"import_index",index);
    const uint32_t *registers=&cpu->_eax;
    for (unsigned i=0;i<9;i++) printf("%s%u",i?",":"",registers[i]);
    printf("],\"mxcsr\":%u,\"xmm_hex\":\"",cpu->_mxcsr);
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<16;j++)printf("%02x",cpu->_xmm[i][j]);
    printf("\",\"ymm_hi_hex\":\"");
    for(unsigned i=0;i<8;i++)for(unsigned j=0;j<16;j++)printf("%02x",cpu->_ymm_hi[i][j]);
    printf("\",\"apple_display\":{\"width\":%u,\"height\":%u,\"native_width\":%u,\"native_height\":%u,\"scale\":%.6f,\"native_scale\":%.6f,\"maximum_refresh_rate\":%u},\"seh_head\":%u,\"registered_filter\":%u}\n",apple_display.width,apple_display.height,apple_display.native_width,apple_display.native_height,apple_display.scale,apple_display.native_scale,apple_display.maximum_refresh_rate,seh_head,tribes_exception_filter());
    fflush(stdout);exit(hardware==2?80:hardware?72:71);
}
_Noreturn void tribes_unimplemented_dinput(void *cpu,uint32_t method) { diagnostic_stop(cpu,method,2); }
_Noreturn void tribes_missing_import(_cpu *cpu,uint32_t index) { diagnostic_stop(cpu,index,0); }
_Noreturn void tribes_missing_cpu_helper(_cpu *cpu,uint32_t index) { diagnostic_stop(cpu,index,1); }
_Noreturn void tribes_missing_helper(uint32_t identifier) {
    fprintf(stderr,"Unsupported bootstrap hardware/helper %u\n",identifier);exit(72);
}
static void commit(uint32_t address,uint32_t size) {
    uint64_t page=(uint64_t)sysconf(_SC_PAGESIZE);
    uint64_t lo=address&~(page-1),hi=((uint64_t)address+size+page-1)&~(page-1);
    if (hi>UINT64_C(0x100000000) || mprotect((void *)(uintptr_t)(guest_base+lo),(size_t)(hi-lo),PROT_READ|PROT_WRITE)!=0) { perror("commit");exit(74); }
}
int main(int argc,char **argv) {
    if(argc!=6)return 64;
    if(!tb_apple_display_snapshot(&apple_display))return 85;
    stack_output=argv[2];
    size_t brush_length=strlen(stack_output)+9;char *brush_path=malloc(brush_length);if(!brush_path)return 73;snprintf(brush_path,brush_length,"%s.brushes",stack_output);brush_trace=fopen(brush_path,"wb");free(brush_path);if(!brush_trace)return 73;
    size_t cursor_length=strlen(stack_output)+9;char *cursor_path=malloc(cursor_length);if(!cursor_path)return 73;snprintf(cursor_path,cursor_length,"%s.cursors",stack_output);cursor_trace=fopen(cursor_path,"wb");free(cursor_path);if(!cursor_trace)return 73;
    size_t icon_length=strlen(stack_output)+7;char *icon_path=malloc(icon_length);if(!icon_path)return 73;snprintf(icon_path,icon_length,"%s.icons",stack_output);icon_trace=fopen(icon_path,"wb");free(icon_path);if(!icon_trace)return 73;
    size_t trace_length=strlen(stack_output)+8;char *trace_path=malloc(trace_length);if(!trace_path)return 73;
    snprintf(trace_path,trace_length,"%s.clocks",stack_output);clock_trace=fopen(trace_path,"wb");free(trace_path);if(!clock_trace)return 73;
    trace_path=malloc(trace_length);if(!trace_path)return 73;
    snprintf(trace_path,trace_length,"%s.find",stack_output);find_trace=fopen(trace_path,"wb");if(!find_trace)return 73;
    snprintf(trace_path,trace_length,"%s.disks",stack_output);disk_trace=fopen(trace_path,"wb");free(trace_path);if(!disk_trace)return 73;
    trace_length=strlen(stack_output)+12;trace_path=malloc(trace_length);if(!trace_path)return 73;snprintf(trace_path,trace_length,"%s.properties",stack_output);property_trace=fopen(trace_path,"wb");free(trace_path);if(!property_trace)return 73;
    trace_length=strlen(stack_output)+10;trace_path=malloc(trace_length);if(!trace_path)return 73;snprintf(trace_path,trace_length,"%s.formats",stack_output);format_trace=fopen(trace_path,"wb");free(trace_path);if(!format_trace)return 73;
    trace_length=strlen(stack_output)+10;trace_path=malloc(trace_length);if(!trace_path)return 73;
    snprintf(trace_path,trace_length,"%s.windows",stack_output);window_trace=fopen(trace_path,"wb");free(trace_path);if(!window_trace)return 73;
    trace_path=malloc(trace_length);if(!trace_path)return 73;snprintf(trace_path,trace_length,"%s.enums",stack_output);input_trace=fopen(trace_path,"wb");free(trace_path);if(!input_trace)return 73;
    trace_length=strlen(stack_output)+10;trace_path=malloc(trace_length);if(!trace_path)return 73;snprintf(trace_path,trace_length,"%s.objects",stack_output);object_trace=fopen(trace_path,"wb");free(trace_path);if(!object_trace)return 73;
    void *mapping=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(mapping==MAP_FAILED){perror("reserve");return 74;}
    guest_base=(uint64_t)(uintptr_t)mapping;
    FILE *image=fopen(argv[1],"rb");if(!image)return 74;
    if(fseek(image,0,SEEK_END)!=0)return 74;
    long size=ftell(image);if(size<=0 || size>0x10000000 || fseek(image,0,SEEK_SET)!=0)return 74;
    image_size=(size_t)size;commit(0x400000,(uint32_t)size);
    if(fread((void *)(uintptr_t)(guest_base+0x400000),1,(size_t)size,image)!=(size_t)size || fclose(image)!=0)return 74;
    commit(0x100000,0x200000);
    for(unsigned i=0;i<TB_IMPORT_COUNT;i++) {
        uint32_t value=tb_import_values[i];
        memcpy((void *)(uintptr_t)(guest_base+tb_import_slots[i]),&value,4);
    }
    if(!tribes_environment_configure(guest_base,tb_environment,sizeof tb_environment)||!tribes_command_line_configure(guest_base,tb_command_line,sizeof tb_command_line))return 76;
    if(!tribes_filesystem_configure(guest_base,argv[3],argv[4],argv[5]))return 77;
    if(!tribes_temp_directory_configure(guest_base,"C:\\Windows"))return 76;
    uint32_t initial_handles[3];for(unsigned i=0;i<3;i++)initial_handles[i]=tribes_get_std_handle(0xfffffff6-i);
    if(!tribes_startup_configure(guest_base,1,initial_handles))return 78;
    if(!tribes_module_register(guest_base,0x400000,"C:\\TribesPad\\Game\\Tribes2.exe",1))return 79;
    if(!tribes_icons_configure(guest_base,0x400000,(void *)(uintptr_t)(guest_base+0x400000),(uint32_t)image_size))return 84;
    if(!tribes_display_configure(guest_base,apple_display.width,apple_display.height))return 86;
    if(!tribes_display_configure_work_area(guest_base,apple_display.work_left,apple_display.work_top,apple_display.work_right,apple_display.work_bottom))return 86;
    if(!tribes_keyboard_configure(guest_base,tb_initial_keyboard_state))return 83;
    if(!tribes_dinput_configure(guest_base)||!tribes_dinput_set_devices(guest_base,tb_input_devices,tb_input_device_count))return 81;
    state._pointer_offset=guest_base;state._esp=0x2fff00;state._eflags=0x202;
    state.stack_bottom=(void *)(uintptr_t)(guest_base+0x100000);state.stack_top=(void *)(uintptr_t)(guest_base+0x300000);
    x87_fninit_void(&state);
    tb_sse_init(&state);
    tribes_enter(&state,0x6fde60);
    fprintf(stderr,"Unexpected engine return\n");return 75;
}
