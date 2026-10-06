/* Native legacy DirectInput root, original 32-bit COM layout. No interpreter.
   Device methods are explicitly gated until backed by actual input data. */
#include "guest_dinput.h"
#include "guest_heap.h"
#include "input_buffer.h"
#include "guest_modules.h"
#include "guest_callback.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct Object { unsigned char bytes[316]; } Object;
typedef struct FormatObject { uint32_t index,offset,width; } FormatObject;
typedef struct Input { uint64_t base;uint32_t object,refs,version,root;int is_device;unsigned char info[580];uint32_t axes,buttons,povs,object_count;Object *objects;FormatObject *format;uint32_t format_count,data_size,format_flags;int format_set,acquired;uint32_t axis_mode;TribesInputBuffer buffer;struct Input *next; } Input;
static Input *inputs;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
extern void *tribes_native_lookup(uint32_t);
#ifdef TRIBESPAD_INPUT_OBSERVER
extern void tribes_property_observation(uint32_t object,uint32_t property,uint32_t value,uint32_t usable);
extern void tribes_format_observation(uint32_t object,uint32_t flags,uint32_t size,uint32_t count,const void *mapping);
#endif
int tribes_dinput_configure(uint64_t base) {
    if(!tribes_native_lookup(TB_DI_CREATE))abort();
    for(unsigned i=0;i<9;i++)if(!tribes_native_lookup(TB_DI_METHOD(i)))abort();
    const TribesNativeExport exports[]={{"DirectInputCreateA",1,TB_DI_CREATE}};
    return tribes_module_define_native(base,TB_DINPUT_MODULE,"C:\\Windows\\System32\\DInput.dll",exports,1);
}
static void *pointer(uint64_t base,uint32_t address,uint32_t size) {
    if(!address||!size||address>UINT32_MAX-(size-1)||base>UINTPTR_MAX-address-(size-1))return NULL;
    return (void *)(uintptr_t)(base+address);
}
static Input *find(uint64_t base,uint32_t object) {for(Input *p=inputs;p;p=p->next)if(p->base==base&&p->object==object)return p;fprintf(stderr,"TribesPad: invalid DirectInput object %x\n",object);abort();}
static uint32_t version_result(uint32_t instance,uint32_t version) {
    if(!instance)return 0x80070057;if(!version)return 0x80070015;
    if(version>0x700)return 0x8007047e;
    if(version!=0x300&&version!=0x500){fprintf(stderr,"TribesPad: unsupported DirectInput version %x\n",version);abort();}
    return 0;
}
uint32_t tribes_dinput_create(uint64_t base,uint32_t instance,uint32_t version,uint32_t output,uint32_t outer) {
    void *out=pointer(base,output,4);if(!out)return 0x80004003;
    uint32_t zero=0;memcpy(out,&zero,4);if(outer)return 0x80040110;
    uint32_t hr=version_result(instance,version);if(hr)return hr;
    Input *entry=calloc(1,sizeof *entry);if(!entry)return 0x8007000e;
    uint32_t old_error=tribes_get_last_error(),object=tribes_global_alloc(base,0x40,40);tribes_set_last_error(old_error);
    if(!object){free(entry);return 0x8007000e;}
    uint32_t table[10];table[0]=object+4;for(unsigned i=0;i<9;i++)table[i+1]=TB_DI_METHOD(i);
    memcpy(pointer(base,object,40),table,40);entry->base=base;entry->object=object;entry->refs=1;entry->version=version;
    pthread_mutex_lock(&lock);entry->next=inputs;inputs=entry;pthread_mutex_unlock(&lock);memcpy(out,&object,4);return 0;
}
uint32_t tribes_dinput_query(uint64_t base,uint32_t object,uint32_t iid,uint32_t output) {
    void *out=pointer(base,output,4);if(!out)return 0x80004003;uint32_t zero=0;memcpy(out,&zero,4);
    const unsigned char unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    const unsigned char device_tail[12]={0x2e,0xc9,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
    const unsigned char tail[12]={0x8a,0xaa,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
    const unsigned char *guid=pointer(base,iid,16);if(!guid)return 0x80004003;uint32_t first;memcpy(&first,guid,4);
    pthread_mutex_lock(&lock);Input *entry=find(base,object);
    int supported=!memcmp(guid,unknown,16)||(entry->is_device?((first==0x5944e680||first==0x5944e682)&&!memcmp(guid+4,device_tail,12)):((first==0x89521360||first==0x5944e662)&&!memcmp(guid+4,tail,12)));
    if(supported){if(entry->refs==UINT32_MAX)abort();entry->refs++;memcpy(out,&object,4);}
    pthread_mutex_unlock(&lock);return supported?0:0x80004002;
}
uint32_t tribes_dinput_addref(uint64_t base,uint32_t object) {pthread_mutex_lock(&lock);Input *entry=find(base,object);if(entry->refs==UINT32_MAX)abort();uint32_t value=++entry->refs;pthread_mutex_unlock(&lock);return value;}
static uint32_t drop_locked(Input *entry) {
    uint32_t value=--entry->refs;
    if(!value){uint64_t base=entry->base;uint32_t root=entry->root;Input **p=&inputs;while(*p!=entry)p=&(*p)->next;*p=entry->next;uint32_t error=tribes_get_last_error();if(tribes_global_free(base,entry->object))abort();tribes_set_last_error(error);free(entry->objects);free(entry->format);tribes_input_buffer_destroy(&entry->buffer);free(entry);if(root)(void)drop_locked(find(base,root));}
    return value;
}
uint32_t tribes_dinput_release(uint64_t base,uint32_t object) {pthread_mutex_lock(&lock);uint32_t value=drop_locked(find(base,object));pthread_mutex_unlock(&lock);return value;}
uint32_t tribes_dinput_initialize(uint64_t base,uint32_t object,uint32_t instance,uint32_t version) {
    uint32_t result=version_result(instance,version);pthread_mutex_lock(&lock);(void)find(base,object);pthread_mutex_unlock(&lock);return result;
}

__attribute__((weak,noreturn)) void tribes_unimplemented_dinput(void *cpu,uint32_t method) {
    (void)cpu;fprintf(stderr,"TribesPad: DirectInput device method %u is not implemented\n",method);abort();
}

typedef struct Device { unsigned char bytes[580];int attached;uint32_t axes,buttons,povs,object_count;Object *objects; } Device;
typedef struct Catalog {uint64_t base;uint32_t count;Device *devices;struct Catalog *next;} Catalog;
static Catalog *catalogs;
static void free_devices(Device *devices,uint32_t count){for(uint32_t i=0;i<count;i++)free(devices[i].objects);free(devices);}
int tribes_dinput_set_devices(uint64_t base,const TribesInputDevice *devices,uint32_t count) {
    if(count>64||(count&&!devices)){errno=EINVAL;return 0;}
    Device *copy=count?calloc(count,sizeof *copy):NULL;if(count&&!copy)return 0;
    for(uint32_t i=0;i<count;i++) {
        const TribesInputDevice *d=&devices[i];unsigned type=d->type&255;
        if(d->axes>8||d->buttons>256||d->povs>4||d->object_count>512||(d->object_count&&!d->objects)||type<2||type>4||!d->instance_name||!d->product_name||strnlen(d->instance_name,260)==260||strnlen(d->product_name,260)==260){free_devices(copy,count);errno=EINVAL;return 0;}
        for(uint32_t j=0;j<i;j++)if(!memcmp(copy[j].bytes+4,d->instance_guid,16)){free_devices(copy,count);errno=EINVAL;return 0;}
        uint32_t size=580;memcpy(copy[i].bytes,&size,4);memcpy(copy[i].bytes+4,d->instance_guid,16);memcpy(copy[i].bytes+20,d->product_guid,16);memcpy(copy[i].bytes+36,&d->type,4);
        strcpy((char *)copy[i].bytes+40,d->instance_name);strcpy((char *)copy[i].bytes+300,d->product_name);copy[i].attached=!!d->attached;copy[i].axes=d->axes;copy[i].buttons=d->buttons;copy[i].povs=d->povs;
        copy[i].object_count=d->object_count;copy[i].objects=d->object_count?calloc(d->object_count,sizeof(Object)):NULL;
        if(d->object_count&&!copy[i].objects){free_devices(copy,count);return 0;}
        unsigned axes_count=0,button_count=0,pov_count=0;
        for(uint32_t j=0;j<d->object_count;j++) {
            const TribesInputObject *o=&d->objects[j];uint32_t cls=o->type&255;
            if(!o->name||strnlen(o->name,260)==260||(cls!=1&&cls!=2&&cls!=4&&cls!=8&&cls!=16)||o->type&~0x00ffff1fu){free_devices(copy,count);errno=EINVAL;return 0;}
            for(uint32_t k=0;k<j;k++)if(o->type==d->objects[k].type||o->offset==d->objects[k].offset){free_devices(copy,count);errno=EINVAL;return 0;}
            if(cls&3)axes_count++;else if(cls&12)button_count++;else pov_count++;
            unsigned char *bytes=copy[i].objects[j].bytes;uint32_t size=316;memcpy(bytes,&size,4);memcpy(bytes+4,o->guid,16);memcpy(bytes+20,&o->offset,4);memcpy(bytes+24,&o->type,4);memcpy(bytes+28,&o->flags,4);strcpy((char *)bytes+32,o->name);memcpy(bytes+304,&o->usage_page,2);memcpy(bytes+306,&o->usage,2);
        }
        if(axes_count>d->axes||button_count>d->buttons||pov_count>d->povs){free_devices(copy,count);errno=EINVAL;return 0;}

    }
    Catalog *created=calloc(1,sizeof *created);if(!created){free_devices(copy,count);return 0;}
    pthread_mutex_lock(&lock);Catalog *entry=catalogs;while(entry&&entry->base!=base)entry=entry->next;
    if(!entry){entry=created;created=NULL;entry->base=base;entry->next=catalogs;catalogs=entry;}
    uint32_t old_count=entry->count;Device *old=entry->devices;entry->devices=copy;entry->count=count;pthread_mutex_unlock(&lock);free_devices(old,old_count);free(created);return 1;
}
extern void (*tribes_lookup(uint32_t))(_cpu *);
#ifdef TRIBESPAD_INPUT_OBSERVER
extern void tribes_input_observation(uint32_t callback,uint32_t context,uint32_t descriptor,const void *bytes,uint32_t size);
#endif
uint32_t tribes_dinput_enumerate(void *state,uint32_t object,uint32_t type,uint32_t callback,uint32_t context,uint32_t flags) {
    _cpu *cpu=state;if(!cpu||!callback||type>4||flags&~0x101u)return 0x80070057;
    (void)tribes_lookup(callback);uint64_t base=cpu->_pointer_offset;
    pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t size=input->version<0x500?560:580;
    Catalog *entry=catalogs;while(entry&&entry->base!=base)entry=entry->next;
    if(!entry){pthread_mutex_unlock(&lock);fprintf(stderr,"TribesPad: native input catalog is not configured\n");abort();}
    uint32_t count=entry->count;Device *snapshot=count?malloc(count*sizeof *snapshot):NULL;
    if(count&&!snapshot){pthread_mutex_unlock(&lock);return 0x8007000e;}
    if(count)memcpy(snapshot,entry->devices,count*sizeof *snapshot);
    if(input->refs==UINT32_MAX)abort();input->refs++;pthread_mutex_unlock(&lock);
    if(cpu->_esp<620)abort();uint32_t descriptor=cpu->_esp-608;
    void *destination=pointer(base,descriptor,size);if(!destination)abort();
    if(cpu->stack_bottom&&base+descriptor-12<(uintptr_t)cpu->stack_bottom)abort();
    if(cpu->stack_top&&base+descriptor+size>(uintptr_t)cpu->stack_top)abort();
    for(uint32_t i=0;i<count;i++) {
        uint32_t device_type;memcpy(&device_type,snapshot[i].bytes+36,4);
        if((type&&(device_type&255)!=type)||((flags&1)&&!snapshot[i].attached)||(flags&0x100))continue;
        memcpy(destination,snapshot[i].bytes,size);memcpy(destination,&size,4);
#ifdef TRIBESPAD_INPUT_OBSERVER
        tribes_input_observation(callback,context,descriptor,destination,size);
#endif
        uint32_t args[]={descriptor,context};if(!tribes_call_guest(cpu,callback,2,args,descriptor))break;
    }
    free(snapshot);(void)tribes_dinput_release(base,object);return 0;
}

static Catalog *catalog(uint64_t base) {Catalog *entry=catalogs;while(entry&&entry->base!=base)entry=entry->next;if(!entry){fprintf(stderr,"TribesPad: native input catalog is not configured\n");abort();}return entry;}
uint32_t tribes_dinput_create_device(uint64_t base,uint32_t root,uint32_t guid,uint32_t output,uint32_t outer) {
    void *out=pointer(base,output,4);const void *id=pointer(base,guid,16);if(!out)return 0x80004003;uint32_t zero=0;memcpy(out,&zero,4);if(!id)return 0x80004003;if(outer)return 0x80040110;
    pthread_mutex_lock(&lock);Input *parent=find(base,root);if(parent->is_device){pthread_mutex_unlock(&lock);return 0x80070057;}Catalog *entry=catalog(base);Device *device=NULL;
    for(uint32_t i=0;i<entry->count;i++)if(!memcmp(id,entry->devices[i].bytes+4,16)){device=&entry->devices[i];break;}
    if(!device){pthread_mutex_unlock(&lock);return 0x80040154;}
    Input *created=calloc(1,sizeof *created);if(!created){pthread_mutex_unlock(&lock);return 0x8007000e;}
    created->base=base;created->version=parent->version;created->root=root;created->refs=1;created->is_device=1;uint32_t native_type;memcpy(&native_type,device->bytes+36,4);created->axis_mode=(native_type&255)==2?1:0;memcpy(created->info,device->bytes,580);created->axes=device->axes;created->buttons=device->buttons;created->povs=device->povs;created->object_count=device->object_count;created->objects=device->object_count?malloc(device->object_count*sizeof(Object)):NULL;
    if(device->object_count&&!created->objects){free(created);pthread_mutex_unlock(&lock);return 0x8007000e;}if(device->object_count)memcpy(created->objects,device->objects,device->object_count*sizeof(Object));
    if(parent->refs==UINT32_MAX)abort();parent->refs++;pthread_mutex_unlock(&lock);
    uint32_t old_error=tribes_get_last_error(),object=tribes_global_alloc(base,0x40,112);tribes_set_last_error(old_error);
    if(!object){free(created->objects);free(created);(void)tribes_dinput_release(base,root);return 0x8007000e;}
    uint32_t table[28];table[0]=object+4;for(unsigned i=0;i<27;i++){if(!tribes_native_lookup(TB_DIDEV_METHOD(i)))abort();table[i+1]=TB_DIDEV_METHOD(i);}memcpy(pointer(base,object,112),table,112);created->object=object;
    pthread_mutex_lock(&lock);created->next=inputs;inputs=created;pthread_mutex_unlock(&lock);memcpy(out,&object,4);return 0;
}
uint32_t tribes_dinput_device_caps(uint64_t base,uint32_t object,uint32_t output) {
    void *destination=pointer(base,output,4);if(!destination)return 0x80004003;uint32_t size;memcpy(&size,destination,4);if((size!=24&&size!=44)||!pointer(base,output,size))return 0x80070057;
    pthread_mutex_lock(&lock);Input *input=find(base,object);if(!input->is_device){pthread_mutex_unlock(&lock);return 0x80070057;}
    int attached=0;Catalog *entry=catalog(base);for(uint32_t i=0;i<entry->count;i++)if(!memcmp(input->info+4,entry->devices[i].bytes+4,16)){attached=entry->devices[i].attached;break;}
    uint32_t type;memcpy(&type,input->info+36,4);uint32_t fields[11]={size,4u|(attached?1u:0u),type,input->axes,input->buttons,input->povs,0,0,0,0,0};memcpy(destination,fields,size);pthread_mutex_unlock(&lock);return 0;
}
uint32_t tribes_dinput_device_info(uint64_t base,uint32_t object,uint32_t output) {
    void *destination=pointer(base,output,4);if(!destination)return 0x80004003;uint32_t size;memcpy(&size,destination,4);if((size!=560&&size!=580)||!pointer(base,output,size))return 0x80070057;
    pthread_mutex_lock(&lock);Input *input=find(base,object);if(!input->is_device){pthread_mutex_unlock(&lock);return 0x80070057;}memcpy(destination,input->info,size);memcpy(destination,&size,4);pthread_mutex_unlock(&lock);return 0;
}

#ifdef TRIBESPAD_INPUT_OBSERVER
extern void tribes_object_observation(uint32_t callback,uint32_t context,uint32_t descriptor,const void *bytes,uint32_t size);
#endif
uint32_t tribes_dinput_enum_objects(void *state,uint32_t object,uint32_t callback,uint32_t context,uint32_t flags) {
    _cpu *cpu=state;if(!cpu||!callback||flags&~0x130000dfu)return 0x80070057;(void)tribes_lookup(callback);uint64_t base=cpu->_pointer_offset;
    pthread_mutex_lock(&lock);Input *input=find(base,object);if(!input->is_device){pthread_mutex_unlock(&lock);return 0x80070057;}
    uint32_t count=input->object_count,size=input->version<0x500?292:316;Object *snapshot=count?malloc(count*sizeof *snapshot):NULL;
    if(count&&!snapshot){pthread_mutex_unlock(&lock);return 0x8007000e;}if(count)memcpy(snapshot,input->objects,count*sizeof *snapshot);if(input->refs==UINT32_MAX)abort();input->refs++;pthread_mutex_unlock(&lock);
    if(cpu->_esp<364)abort();uint32_t descriptor=cpu->_esp-352;void *destination=pointer(base,descriptor,316);if(!destination)abort();
    if(cpu->stack_bottom&&base+descriptor-12<(uintptr_t)cpu->stack_bottom)abort();if(cpu->stack_top&&base+descriptor+316>(uintptr_t)cpu->stack_top)abort();
    for(uint32_t i=0;i<count;i++) {
        uint32_t type;memcpy(&type,snapshot[i].bytes+24,4);if((flags&255)&&!(flags&type&255))continue;if((flags&0x13000000u)&&(type&flags&0x13000000u)!=(flags&0x13000000u))continue;
        /* Complete padded scratch storage prevents a DX3 client from observing
           leftover API stack bytes if it reads the newer structure tail. */
        memcpy(destination,snapshot[i].bytes,316);memcpy(destination,&size,4);
#ifdef TRIBESPAD_INPUT_OBSERVER
        tribes_object_observation(callback,context,descriptor,destination,size);
#endif
        uint32_t args[]={descriptor,context};if(!tribes_call_guest(cpu,callback,2,args,descriptor))break;
    }
    free(snapshot);(void)tribes_dinput_release(base,object);return 0;
}

/* Native proxy range contract: relative axes wrap through signed LONG, absolute
   software axes use 0..65535 until explicit calibration/range support is added.
   Numeric MAKEDIPROP identifiers are tokens, never dereferenced as GUID memory. */
uint32_t tribes_dinput_get_property(void *state,uint32_t object,uint32_t property,uint32_t header) {
    _cpu *cpu=state;if(!cpu)return 0x80070057;uint64_t base=cpu->_pointer_offset;
    void *destination=pointer(base,header,16);if(!destination)return 0x80004003;
    uint32_t fields[4];memcpy(fields,destination,16);
    if(fields[1]!=16||fields[3]>3||(fields[3]==0&&fields[2]))return 0x80070057;
    if(property==1||property==2){
        if(fields[0]!=20||fields[2]||fields[3]||!pointer(base,header,20))return 0x80070057;
        pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t hr=input->is_device?0:0x80070057;
        if(!hr){uint32_t value=property==1?input->buffer.size:input->axis_mode;memcpy((unsigned char *)destination+16,&value,4);}pthread_mutex_unlock(&lock);return hr;
    }
    if(property!=4)tribes_unimplemented_dinput(cpu,105);
    if(fields[0]!=24||!pointer(base,header,24))return 0x80070057;
    pthread_mutex_lock(&lock);Input *input=find(base,object);
    if(!input->is_device){pthread_mutex_unlock(&lock);return 0x80070057;}
    uint32_t hr=0x80070002;Object *selected=NULL;
    if(fields[3]==0)hr=0x80004001;
    else if(fields[3]==1){if(!input->format_set)hr=0x80070015;else for(uint32_t i=0;i<input->format_count;i++)if(input->format[i].offset==fields[2]&&input->format[i].index!=UINT32_MAX){selected=&input->objects[input->format[i].index];break;}}
    else if(fields[3]==3){uint32_t device_type;memcpy(&device_type,input->info+36,4);if(!(device_type&0x10000u))hr=0x80004001;else for(uint32_t i=0;i<input->object_count;i++){uint16_t page,usage;memcpy(&page,input->objects[i].bytes+304,2);memcpy(&usage,input->objects[i].bytes+306,2);if(fields[2]==((uint32_t)page<<16|usage)){selected=&input->objects[i];break;}}}
    else for(uint32_t i=0;i<input->object_count;i++){uint32_t id;memcpy(&id,input->objects[i].bytes+24,4);if(id==fields[2]){selected=&input->objects[i];break;}}
    if(selected){uint32_t type;memcpy(&type,selected->bytes+24,4);type&=255;
        if(type!=1&&type!=2)hr=0x80004001;
        else {uint32_t range[2]={type==1?0x80000000u:0u,type==1?0x7fffffffu:65535u};memcpy((unsigned char *)destination+16,range,8);hr=0;}
    }
    pthread_mutex_unlock(&lock);return hr;
}

/* Retain only resolved host-owned mappings. Guest GUID/data-format storage can
   be freed or changed immediately after this call without affecting input. */
uint32_t tribes_dinput_set_format(uint64_t base,uint32_t object,uint32_t format) {
    const void *source=pointer(base,format,24);if(!source)return 0x80004003;uint32_t header[6];memcpy(header,source,24);
    if(header[0]!=24||header[1]!=16||header[2]>2||!header[3]||(header[3]&3)||header[3]>0x100000||header[4]>1024||(header[4]&&!pointer(base,header[5],header[4]*16)))return 0x80070057;
    FormatObject *mapped=header[4]?calloc(header[4],sizeof *mapped):NULL;if(header[4]&&!mapped)return 0x8007000e;
    uint32_t hr=0;pthread_mutex_lock(&lock);Input *input=find(base,object);
    if(!input->is_device)hr=0x80070057;else if(input->acquired)hr=0x800700aa;
    for(uint32_t i=0;!hr&&i<header[4];i++) {
        uint32_t row[4];memcpy(row,(const unsigned char *)pointer(base,header[5],header[4]*16)+i*16,16);uint32_t cls=row[2]&255;
        if((cls!=1&&cls!=2&&cls!=3&&cls!=4&&cls!=8&&cls!=12&&cls!=16)||(row[2]&~0x83ffff1fu)||(row[3]&~0x00000f00u)){hr=0x80070057;break;}
        uint32_t width=cls&12?1:4;if(row[1]>header[3]-width||(width==4&&(row[1]&3))){hr=0x80070057;break;}
        for(uint32_t j=0;j<i;j++)if(row[1]<mapped[j].offset+mapped[j].width&&mapped[j].offset<row[1]+width){hr=0x80070057;break;}
        if(hr)break;const void *guid=row[0]?pointer(base,row[0],16):NULL;if(row[0]&&!guid){hr=0x80070057;break;}
        uint32_t selected=UINT32_MAX;
        for(uint32_t j=0;j<input->object_count;j++) {
            uint32_t type;memcpy(&type,input->objects[j].bytes+24,4);
            if(!(type&cls&255)||((row[2]&0xffff00u)!=0xffff00u&&(row[2]&0xffff00u)!=(type&0xffff00u))||((row[2]&0x03000000u)&type)!=(row[2]&0x03000000u)||(guid&&memcmp(guid,input->objects[j].bytes+4,16))||(row[3]&&row[3]!=0x100))continue;
            int used=0;for(uint32_t k=0;k<i;k++)if(mapped[k].index==j){used=1;break;}if(!used){selected=j;break;}
        }
        if(selected==UINT32_MAX&&!(row[2]&0x80000000u)){hr=0x80070057;break;}
        mapped[i]=(FormatObject){selected,row[1],width};
    }
    if(!hr){free(input->format);input->format=mapped;mapped=NULL;input->format_count=header[4];input->data_size=header[3];input->format_flags=header[2];input->format_set=1;if(header[2])input->axis_mode=header[2]-1;tribes_input_buffer_clear(&input->buffer);
#ifdef TRIBESPAD_INPUT_OBSERVER
        tribes_format_observation(object,input->format_flags,input->data_size,input->format_count,input->format);
#endif
    }
    pthread_mutex_unlock(&lock);free(mapped);return hr;
}

uint32_t tribes_dinput_set_property(void *state,uint32_t object,uint32_t property,uint32_t header) {
    _cpu *cpu=state;if(!cpu)return 0x80070057;uint64_t base=cpu->_pointer_offset;
    const void *source=pointer(base,header,16);if(!source)return 0x80004003;
    uint32_t fields[4];memcpy(fields,source,16);if(fields[1]!=16||fields[3]>3||(fields[3]==0&&fields[2]))return 0x80070057;
    if(property!=1&&property!=2)tribes_unimplemented_dinput(cpu,106);
    if(fields[0]!=20||fields[2]||fields[3]||!pointer(base,header,20))return 0x80070057;
    uint32_t requested;memcpy(&requested,(const unsigned char *)source+16,4);
    pthread_mutex_lock(&lock);Input *input=find(base,object);uint32_t hr=0;
    if(!input->is_device)hr=0x80070057;else if(input->acquired)hr=0x800700aa;
    else if(property==2){if(requested>1)hr=0x80070057;else{input->axis_mode=requested;tribes_input_buffer_clear(&input->buffer);}}
    else if(!tribes_input_buffer_resize(&input->buffer,requested))hr=0x8007000e;
#ifdef TRIBESPAD_INPUT_OBSERVER
    if(!hr)tribes_property_observation(object,property,property==1?input->buffer.size:input->axis_mode,property==1&&input->buffer.size?input->buffer.size-1:0);
#endif
    pthread_mutex_unlock(&lock);return hr;
}
