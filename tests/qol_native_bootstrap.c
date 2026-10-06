/* Diagnostic native module entry. Guest PE pages are data, never executable.
 * No claim of full Windows page/SEH semantics or patch publication. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include "../port/core/guest_threads.h"
#include "../port/llasm-support/tribes_absolute_memory.h"
#include "../port/core/guest_teb.h"
#include "../port/core/guest_address_space.h"
#include "../port/core/guest_virtual_memory.h"
#include "../port/core/guest_heap.h"
#include "../port/core/guest_memory_checks.h"
#include "../port/core/guest_external_checks.h"
#include "../port/core/guest_modules.h"
#include "../port/core/native_system_providers.h"
#include "../port/core/process_environment.h"
#include "../port/core/guest_profile.h"
#include "../port/core/process_startup.h"
#include "../port/core/guest_filesystem.h"
#include "../port/core/native_handles.h"
#ifdef TB_PUBLISH_PATCHES
#include "miles_bootstrap.inc"
#include "codec_bootstrap.inc"
#include "../port/core/guest_dinput.h"
#include "../port/core/keyboard_state.h"
#include "../port/core/guest_icons.h"
#include "../port/core/guest_display.h"
#include "../port/core/guest_sdl_backend.h"
#include "../port/core/guest_graphics.h"
#include "../port/core/guest_dsound.h"
#include "../port/apple/display.h"
#include "engine_bootstrap.inc"
#endif
#include "qol_bootstrap.inc"
extern void tribes_enter(_cpu *,uint32_t);
extern void x87_fninit_void(_cpu *);
extern void tb_sse_init(_cpu *);
static _cpu state;
static uint64_t base;
static int stack_readable(_cpu *c,uint32_t bytes);
static unsigned stage;
static unsigned attach_return,engine_entered;
static int late_capture_requested;
static int startup_patch_capture_requested;
static int library_patch_capture_requested;
static int library_patch_verify_requested;
static int followup_patch_capture_requested;
static int followup_prefix_verify_requested;
static int network_patch_capture_requested;
static int http_patch_capture_requested;
static int console_patch_capture_requested;
static int console_animation_capture_requested;
static int post_file_patch_capture_requested;
static int post_file_patch_verify_requested;
static int collision_patch_verify_requested;
static int allocation_patch_verify_requested;
static int clock_patch_verify_requested;
static uint32_t protection_capture_address;
static int console_animation_verify_requested;
static int console_fade_verify_requested;
static int http_patch_verify_requested;
static int string_patch_verify_requested;
static int network_patch_verify_requested;
static unsigned adapter_size_queries,adapter_snapshots;
static int late_verify_requested;
#ifdef TB_PUBLISH_PATCHES
#include "patch_publication.inc"
#include "late_publication.inc"
#include "startup_input_data.inc"
#include "startup_input_code.inc"
#include "library_publication.inc"
#include "followup_publication.inc"
#include "network_publication.inc"
#include "http_publication.inc"
#include "string_publication.inc"
#include "console_fade_publication.inc"
#include "console_animation_publication.inc"
#include "post_file_patch_publication.inc"
#include "collision_patch_publication.inc"
#include "allocation_patch_publication.inc"
#include "font_size_publication.inc"
#include "clock_patch_publication.inc"
#include "post_video_prefix_publication.inc"
#include "post_video_nop_publication.inc"
#include "post_video_nop_second_publication.inc"
#include "post_video_tail_publication.inc"
#include "display_immediates_publication.inc"
#include "startup_calls_publication.inc"
static unsigned startup_calls_write_count;
#include "input_nop_publication.inc"
static unsigned input_nop_write_count;
#include "pending_hook_publication.inc"
#include "renderer_call_publication.inc"
#include "renderer_startup_publication.inc"
#include "renderer_redirects_publication.inc"
static unsigned renderer_redirects_write_count;
#include "renderer_dimensions_publication.inc"
static unsigned renderer_dimensions_write_count;
#include "renderer_batch_publication.inc"
static unsigned renderer_batch_write_count;
#include "renderer_nested_publication.inc"
static unsigned renderer_nested_write_count;
#include "renderer_outer_publication.inc"
#include "renderer_hook_publication.inc"
static unsigned renderer_hook_write_count,renderer_hook_flush_count;
#include "audio_tables_publication.inc"
static unsigned audio_table_write_count;
#include "audio_hook_publication.inc"
static unsigned audio_hook_write_count,audio_hook_flush_count;
static unsigned renderer_outer_write_count;
static unsigned renderer_startup_write_count,renderer_startup_flush_count;
static unsigned pending_hook_write_count,pending_hook_flush_count;
static unsigned display_immediates_write_count;
static uint32_t display_mode_address;
static uint64_t display_dimensions;
static unsigned post_video_tail_write_count;
static unsigned post_video_nop_second_write_count;
static unsigned post_video_nop_write_count;
static unsigned post_video_prefix_write_count;
static unsigned clock_patch_write_count;
static unsigned allocation_patch_write_count;
static unsigned collision_patch_write_count;
static unsigned post_file_patch_write_count;
static unsigned console_animation_write_count;
static unsigned console_fade_write_count;
static unsigned string_write_count,string_flush_count;
static TribesPublishedCodeRange publication_ranges[256];
static uint32_t publication_count,flush_count;
static unsigned late_write_count,late_flush_count,input_data_flush_count,input_code_write_count,library_write_count,library_flush_count,followup_write_count,followup_flush_count,network_write_count,http_write_count,http_flush_count;
static int input_data_matches(const unsigned char *expected){
 const volatile unsigned char *data=(const volatile unsigned char *)(uintptr_t)(base+TB_INPUT_DATA_ADDRESS);
 for(unsigned i=0;i<TB_INPUT_DATA_BYTES;i++)if(data[i]!=expected[i])return 0;return 1;
}
int qol_patch_ready(void){return tribes_memory_code_published(base);}
static uint32_t qol_prior_route_phase(void){
 uint32_t phase=tribes_memory_code_revision(base);
 /* Revisions14/15/16/17/18/19/20 change new, disjoint wrappers. Earlier completed routes
    remain usable during these publications; the locked completed query may
    observe either side of completion. Earlier overlapping revisions stop. */
 if(phase==3){uint32_t completed=tribes_memory_code_completed_revision(base);if(completed==26||completed==28||completed==30||completed==32||completed==34||completed==36||completed==38||completed==40||completed==42||completed==44||completed==46||completed==48||completed==50||completed==52||completed==54||completed==56||completed==58||completed==60||completed==62||completed==64||completed==66)phase=completed;}
 return phase;
}
int qol_late_route_state(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase==1?1:2;}
int qol_input_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=4;}
int qol_library_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=6;}
int qol_followup_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=8;}
int qol_network_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=10;}
int qol_http_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=12;}
_Noreturn void qol_http_unavailable_entry(void){fputs("TribesPad: unpublished HTTP continuation\n",stderr);abort();}
int qol_string_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=14;}
int qol_console_fade_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=16;}
int qol_console_animation_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=18;}
int qol_post_file_patch_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=20;}
void qol_post_file_patch_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&console_animation_write_count==2&&!post_file_patch_write_count);
 assert(tribes_memory_code_revision(base)==18);
 assert(tribes_memory_prepare_next_code_revision(base,&post_file_patch_code_write,1));
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));
 assert(tribes_memory_finish_code_revision(base));post_file_patch_write_count=1;
 assert(tribes_memory_code_revision(base)==20);
 fprintf(stderr,"Published post-file CALL pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
int qol_collision_patch_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=22;}
void qol_collision_patch_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&post_file_patch_write_count==1&&!collision_patch_write_count);
 assert(tribes_memory_code_revision(base)==20);
 assert(tribes_memory_prepare_next_code_revision(base,&collision_patch_code_write,1));
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));
 assert(tribes_memory_finish_code_revision(base));collision_patch_write_count=1;
 assert(tribes_memory_code_revision(base)==22);
 fprintf(stderr,"Published collision CALL pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
int qol_font_size_route_ready(uint32_t index){
 assert(index<2);return tribes_memory_font_size_published(base,index);
}
void qol_font_size_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_batch_write_count==23);
 unsigned index=pc==font_size_writes[0].pc?0:1;const TribesCodeWrite *w=font_size_writes+index;
 assert(pc==w->pc&&(uintptr_t)pointer==base+w->address&&size==w->size&&value==w->value&&!qol_font_size_route_ready(index));
 assert(tribes_memory_code_revision(base)==54);
 assert(tribes_memory_prepare_font_size_revision(base,index,w));
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));assert(tribes_memory_finish_code_revision(base));
 assert(tribes_memory_code_revision(base)==54&&qol_font_size_route_ready(index));
 fprintf(stderr,"Published font allocation %u after exact original size store at0x%08x\n",index,w->address);
}
int qol_allocation_patch_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=24;}
void qol_allocation_patch_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&collision_patch_write_count==1&&!allocation_patch_write_count);
 assert(tribes_memory_code_revision(base)==22);
 assert(tribes_memory_prepare_next_code_revision(base,&allocation_patch_code_write,1));
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));
 assert(tribes_memory_finish_code_revision(base));allocation_patch_write_count=1;
 assert(tribes_memory_code_revision(base)==24);
 fprintf(stderr,"Published allocation SIZE pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
int qol_clock_patch_route_ready(void){uint32_t phase=qol_prior_route_phase();assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=26;}
int qol_post_video_prefix_route_ready(void){uint32_t phase=tribes_memory_code_revision(base);if(phase==3){uint32_t completed=tribes_memory_code_completed_revision(base);if(completed==28||completed==30||completed==32||completed==34||completed==36||completed==38||completed==40||completed==42||completed==44||completed==46||completed==48||completed==50||completed==52||completed==54||completed==56||completed==58||completed==60||completed==62||completed==64||completed==66)phase=completed;}assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=28;}
int qol_post_video_nop_route_ready(void){uint32_t phase=tribes_memory_code_revision(base);if(phase==3){uint32_t completed=tribes_memory_code_completed_revision(base);if(completed==30||completed==32||completed==34||completed==36||completed==38||completed==40||completed==42||completed==44||completed==46||completed==48||completed==50||completed==52||completed==54||completed==56||completed==58||completed==60||completed==62||completed==64||completed==66)phase=completed;}assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=30;}
void qol_post_video_nop_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&post_video_prefix_write_count==2&&post_video_nop_write_count<8);
 if(!post_video_nop_write_count){assert(tribes_memory_code_revision(base)==28);assert(tribes_memory_prepare_next_code_revision(base,post_video_nop_writes,8));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));post_video_nop_write_count++;
 fprintf(stderr,"Validated post-video NOP write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",post_video_nop_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(post_video_nop_write_count==8){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==30);fputs("Published first post-video NOP region after eight exact stores\n",stderr);}
}
int qol_post_video_nop_second_route_ready(void){uint32_t phase=tribes_memory_code_revision(base);if(phase==3){uint32_t completed=tribes_memory_code_completed_revision(base);if(completed==32||completed==34||completed==36||completed==38||completed==40||completed==42||completed==44||completed==46||completed==48||completed==50||completed==52||completed==54||completed==56||completed==58||completed==60||completed==62||completed==64||completed==66)phase=completed;}assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=32;}
void qol_post_video_nop_second_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&post_video_nop_write_count==8&&post_video_nop_second_write_count<8);
 if(!post_video_nop_second_write_count){assert(tribes_memory_code_revision(base)==30);assert(tribes_memory_prepare_next_code_revision(base,post_video_nop_second_writes,8));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));post_video_nop_second_write_count++;
 fprintf(stderr,"Validated second post-video NOP write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",post_video_nop_second_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(post_video_nop_second_write_count==8){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==32);fputs("Published second post-video NOP region after eight exact stores\n",stderr);}
}
int qol_post_video_tail_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=34);assert(phase==1||phase==2||phase==4||phase==6||phase==8||phase==10||phase==12||phase==14||phase==16||phase==18||phase==20||phase==22||phase==24||phase==26||phase==28||phase==30||phase==32||phase==34||phase==36||phase==38||phase==40||phase==42||phase==44||phase==46||phase==48||phase==50||phase==52||phase==54||phase==56||phase==58||phase==60||phase==62||phase==64||phase==66);return phase>=34;}
int qol_display_immediates_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=36);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=36;}
int qol_startup_calls_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=38);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=38;}
_Noreturn void qol_startup_calls_unavailable(void){abort();}
int qol_input_nop_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=40);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=40;}
void qol_input_nop_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&startup_calls_write_count==5&&input_nop_write_count<3);
 if(!input_nop_write_count){assert(tribes_memory_code_revision(base)==38);assert(tribes_memory_prepare_next_code_revision(base,input_nop_writes,3));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));input_nop_write_count++;
 fprintf(stderr,"Validated input NOP write %u pc=0x%08x address=0x%08x bytes=%u\n",input_nop_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(input_nop_write_count==3){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==40);fputs("Published input NOP region after three exact stores\n",stderr);}
}

void qol_startup_calls_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&display_immediates_write_count==2&&startup_calls_write_count<5);
 if(!startup_calls_write_count){uint32_t skip;memcpy(&skip,(void *)(uintptr_t)(base+0x10470a54),4);assert(!skip);assert(tribes_memory_code_revision(base)==36);assert(tribes_memory_prepare_next_code_revision(base,startup_calls_writes,5));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));startup_calls_write_count++;
 fprintf(stderr,"Validated startup CALL write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",startup_calls_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(startup_calls_write_count==5){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==38);fputs("Published startup CALL group after five exact stores\n",stderr);}
}
void qol_display_immediates_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&post_video_tail_write_count==3&&display_immediates_write_count<2);
 uint32_t mode=*(const uint32_t *)(uintptr_t)(base+0x1046d540);
 uint64_t dimensions=tribes_sdl_desktop_dimensions(c,mode);assert(dimensions);
 if(!display_immediates_write_count){
  assert(tribes_memory_code_revision(base)==34);display_mode_address=mode;display_dimensions=dimensions;
  display_immediates_writes[0].value=(uint32_t)dimensions;display_immediates_writes[1].value=dimensions>>32;
  assert(tribes_memory_prepare_next_code_revision(base,display_immediates_writes,2));
 } else assert(mode==display_mode_address&&dimensions==display_dimensions);
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));display_immediates_write_count++;
 fprintf(stderr,"Validated display immediate write %u pc=0x%08x address=0x%08x bytes=%u value=%llu\n",display_immediates_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(display_immediates_write_count==2){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==36);fputs("Published display immediates after two native-derived stores\n",stderr);}
}
void qol_post_video_tail_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&post_video_nop_second_write_count==8&&post_video_tail_write_count<3);
 if(!post_video_tail_write_count){assert(tribes_memory_code_revision(base)==32);assert(tribes_memory_prepare_next_code_revision(base,post_video_tail_writes,3));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));post_video_tail_write_count++;
 fprintf(stderr,"Validated post-video tail write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",post_video_tail_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(post_video_tail_write_count==3){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==34);fputs("Published post-video tail after three exact stores\n",stderr);}
}
void qol_post_video_prefix_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&clock_patch_write_count==2&&post_video_prefix_write_count<2);
 if(!post_video_prefix_write_count){assert(tribes_memory_code_revision(base)==26);assert(tribes_memory_prepare_next_code_revision(base,post_video_prefix_writes,2));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));post_video_prefix_write_count++;
 fprintf(stderr,"Validated post-video prefix write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",post_video_prefix_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(post_video_prefix_write_count==2){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==28);fputs("Published post-video prefix after two exact stores\n",stderr);}
}
void qol_clock_patch_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&allocation_patch_write_count==1&&clock_patch_write_count<2);
 if(!clock_patch_write_count){assert(tribes_memory_code_revision(base)==24);assert(tribes_memory_prepare_next_code_revision(base,clock_patch_writes,2));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));clock_patch_write_count++;
 fprintf(stderr,"Validated clock patch write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",clock_patch_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(clock_patch_write_count==2){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==26);fputs("Published clock patch after two exact CALL stores\n",stderr);}
}
void qol_console_animation_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&console_fade_write_count==1&&console_animation_write_count<2);
 if(!console_animation_write_count){assert(tribes_memory_code_revision(base)==16);assert(tribes_memory_prepare_next_code_revision(base,console_animation_writes,2));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));console_animation_write_count++;
 fprintf(stderr,"Validated console animation write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",console_animation_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(console_animation_write_count==2){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==18);fputs("Published console animation after two exact CALL stores\n",stderr);}
}
void qol_console_fade_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&string_write_count==114&&string_flush_count==12&&!console_fade_write_count);
 assert(tribes_memory_code_revision(base)==14);
 assert(tribes_memory_prepare_next_code_revision(base,&console_fade_code_write,1));
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==16);console_fade_write_count=1;
 fprintf(stderr,"Published console fade CALL pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
_Noreturn void qol_string_unavailable_entry(void){fputs("TribesPad: unpublished string/utility continuation\n",stderr);abort();}
void qol_string_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&http_write_count==17&&http_flush_count==1&&string_write_count<114);
 if(!string_write_count){assert(pc==string_code_writes[0].pc);assert(tribes_memory_prepare_next_code_revision(base,string_code_writes,114));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));string_write_count++;
 fprintf(stderr,"Validated string AOT write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",string_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
int qol_pending_hooks_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=42);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=42;}
int qol_renderer_call_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=44);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=44;}
int qol_renderer_startup_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=46);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=46;}
int qol_renderer_redirects_route_ready(uint32_t group){assert(group<2);uint32_t threshold=48+2*group,phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=threshold);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=threshold;}
int qol_renderer_dimensions_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=52);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=52;}
int qol_audio_tables_route_ready(void){
 uint32_t phase=qol_prior_route_phase();
 if(tribes_memory_code_revision(base)==3)assert(phase>=64);
 /* Operand routes switch after the complete table batch; later hook stores
    do not overlap these operand bytes (verified by integration binding). */
 assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));
 return phase>=64;
}
void qol_audio_tables_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_hook_write_count==61&&renderer_hook_flush_count==27&&audio_table_write_count<160);
 if(!audio_table_write_count){assert(tribes_memory_code_revision(base)==62);assert(tribes_memory_prepare_next_code_revision(base,audio_table_writes,160));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));audio_table_write_count++;
 if(audio_table_write_count==160){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==64);fputs("Published audio tables after 160 exact original stores\n",stderr);}
}
int qol_renderer_batch_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=54);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=54;}
void qol_renderer_batch_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_dimensions_write_count==6&&renderer_batch_write_count<23);
 if(!renderer_batch_write_count){assert(tribes_memory_code_revision(base)==52);assert(tribes_memory_prepare_next_code_revision(base,renderer_batch_writes,23));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_batch_write_count++;
 fprintf(stderr,"Validated renderer batch store %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_batch_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(renderer_batch_write_count==23){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==54);fputs("Published renderer batch after 23 stores\n",stderr);}
}

int qol_renderer_nested_route_ready(uint32_t group){assert(group<2);uint32_t phase=qol_prior_route_phase(),threshold=56+2*group;if(tribes_memory_code_revision(base)==3)assert(phase>=threshold);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=threshold;}
void qol_renderer_nested_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_batch_write_count==23&&renderer_nested_write_count<22);
 if(!renderer_nested_write_count){assert(tribes_memory_code_revision(base)==54);assert(tribes_memory_prepare_next_code_revision(base,renderer_nested_writes,5));}
 if(renderer_nested_write_count==5){assert(tribes_memory_code_revision(base)==56);assert(tribes_memory_prepare_next_code_revision(base,renderer_nested_writes+5,17));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_nested_write_count++;
 fprintf(stderr,"Validated nested renderer store %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_nested_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(renderer_nested_write_count==5||renderer_nested_write_count==22){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==(renderer_nested_write_count==5?56:58));fprintf(stderr,"Published nested renderer group after %u stores\n",renderer_nested_write_count);}
}

int qol_renderer_outer_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=60);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=60;}
void qol_renderer_outer_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_nested_write_count==22&&renderer_outer_write_count<20);
 if(!renderer_outer_write_count){uint32_t feature;memcpy(&feature,(void *)(uintptr_t)(base+0x101d5d98),4);assert(feature==1);assert(tribes_memory_code_revision(base)==58);assert(tribes_memory_prepare_next_code_revision(base,renderer_outer_writes,20));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_outer_write_count++;
 fprintf(stderr,"Validated outer renderer store %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_outer_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(renderer_outer_write_count==20){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==60);fputs("Published outer renderer after 20 stores\n",stderr);}
}

void qol_renderer_dimensions_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_redirects_write_count==5&&renderer_dimensions_write_count<6);
 if(!renderer_dimensions_write_count){assert(tribes_memory_code_revision(base)==50);assert(tribes_memory_prepare_next_code_revision(base,renderer_dimensions_writes,6));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_dimensions_write_count++;
 fprintf(stderr,"Validated renderer dimension store %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_dimensions_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(renderer_dimensions_write_count==6){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==52);fputs("Published renderer dimensions after six stores\n",stderr);}
}
void qol_renderer_redirects_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_startup_write_count==7&&renderer_redirects_write_count<5);
 unsigned i=renderer_redirects_write_count;
 if(i==0){assert(tribes_memory_code_revision(base)==46);assert(tribes_memory_prepare_next_code_revision(base,renderer_redirects_writes,3));}
 if(i==3){assert(tribes_memory_code_revision(base)==48);assert(tribes_memory_prepare_next_code_revision(base,renderer_redirects_writes+3,2));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_redirects_write_count++;
 fprintf(stderr,"Validated renderer redirect %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_redirects_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(i==2||i==4){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==(i==2?48:50));fprintf(stderr,"Published renderer redirect group %u\n",i==2?1:2);}
}
void qol_renderer_startup_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_startup_write_count<7);
 if(!renderer_startup_write_count){assert(tribes_memory_code_revision(base)==44);assert(tribes_memory_prepare_next_code_revision(base,renderer_startup_writes,7));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_startup_write_count++;
 fprintf(stderr,"Validated renderer startup store %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_startup_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(renderer_startup_write_count==7){assert(renderer_startup_flush_count==1);assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==46);fputs("Published renderer startup after seven exact stores and original JMP flush\n",stderr);}
}

void qol_renderer_call_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&pending_hook_write_count==86&&pending_hook_flush_count==35);
 assert(tribes_memory_code_revision(base)==42);
 assert(tribes_memory_prepare_next_code_revision(base,&renderer_call_write,1));
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));
 assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==44);
 fprintf(stderr,"Published renderer CALL pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
int qol_renderer_hooks_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=62);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=62;}
int qol_renderer_nested_exact_ready(void){return qol_renderer_nested_route_ready(1);}
_Noreturn void qol_renderer_hooks_unavailable(void){fputs("TribesPad: unpublished renderer hook entry\n",stderr);abort();}
int qol_audio_hooks_route_ready(void){uint32_t phase=qol_prior_route_phase();if(tribes_memory_code_revision(base)==3)assert(phase>=66);assert(phase==1||(phase>=2&&phase<=66&&!(phase&1)));return phase>=66;}
_Noreturn void qol_audio_hooks_unavailable(void){fputs("TribesPad: unpublished audio hook entry\n",stderr);abort();}
static void audio_hook_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&audio_table_write_count==160&&audio_hook_write_count<21);
 if(!audio_hook_write_count){assert(tribes_memory_code_revision(base)==64);assert(tribes_memory_publish_audio_bank(base,audio_bank_ranges,7));assert(tribes_memory_prepare_next_code_revision(base,audio_hook_writes,21));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));audio_hook_write_count++;
 fprintf(stderr,"Validated audio hook store %u pc=0x%08x address=0x%08x bytes=%u\n",audio_hook_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
}
static void renderer_hook_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&renderer_outer_write_count==20&&renderer_hook_write_count<61);
 if(!renderer_hook_write_count){assert(tribes_memory_code_revision(base)==60);assert(tribes_memory_publish_renderer_bank(base,renderer_bank_ranges,26));assert(tribes_memory_prepare_next_code_revision(base,renderer_hook_writes,61));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));renderer_hook_write_count++;
 fprintf(stderr,"Validated renderer hook store %u pc=0x%08x address=0x%08x bytes=%u\n",renderer_hook_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
}
_Noreturn void qol_pending_hooks_unavailable(void){fputs("TribesPad: unpublished pending hook entry\n",stderr);abort();}
static void pending_hook_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&input_nop_write_count==3&&pending_hook_write_count<86);
 if(!pending_hook_write_count){
  assert(tribes_memory_code_revision(base)==40);
  assert(tribes_memory_publish_pending_bank(base,pending_bank_ranges,34));
  assert(tribes_memory_prepare_next_code_revision(base,pending_hook_writes,86));
 }
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));pending_hook_write_count++;
 fprintf(stderr,"Validated pending hook store %u pc=0x%08x address=0x%08x bytes=%u\n",pending_hook_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
}
void qol_pending_hooks_shared_write(_cpu *c,uint32_t pc,void *pointer,uint32_t size,uint64_t value){
 assert(c->_pointer_offset==base&&((pc==0x10072860&&size==1)||(pc==0x10072865&&size==4)));
 if(stage==2&&audio_table_write_count==160){audio_hook_write(c,pc,pointer,size,value);return;}
 if(stage==2&&renderer_outer_write_count==20){renderer_hook_write(c,pc,pointer,size,value);return;}
 if(stage==2&&input_nop_write_count==3){pending_hook_write(c,pc,pointer,size,value);return;}
 /* The same original installer also runs before initial code publication. */
 memcpy(pointer,&value,size);
}
void qol_string_shared_write(_cpu *c,uint32_t pc,void *pointer,uint32_t size,uint64_t value){
 assert(c->_pointer_offset==base&&(pc==0x100fbcdd||pc==0x100fbcf3||pc==0x100fbd10||pc==0x100fbd12)&&(size==1||size==4));
 if(string_write_count&&string_write_count<114){
  const TribesCodeWrite *next=string_code_writes+string_write_count;
  if(pc==next->pc&&(uintptr_t)pointer==base+next->address){qol_string_code_write(c,pc,pointer,size,value);return;}
 }
 if(audio_hook_write_count&&audio_hook_write_count<21){
  const TribesCodeWrite *next=audio_hook_writes+audio_hook_write_count;
  if(pc==next->pc&&(uintptr_t)pointer==base+next->address){audio_hook_write(c,pc,pointer,size,value);return;}
 }
 if(renderer_hook_write_count&&renderer_hook_write_count<61){
  const TribesCodeWrite *next=renderer_hook_writes+renderer_hook_write_count;
  if(pc==next->pc&&(uintptr_t)pointer==base+next->address){renderer_hook_write(c,pc,pointer,size,value);return;}
 }
 if(pending_hook_write_count&&pending_hook_write_count<86){
  const TribesCodeWrite *next=pending_hook_writes+pending_hook_write_count;
  if(pc==next->pc&&(uintptr_t)pointer==base+next->address){pending_hook_write(c,pc,pointer,size,value);return;}
 }
 if(c==&state&&engine_entered&&stage==2&&renderer_nested_write_count==22&&renderer_outer_write_count<13){
  const TribesCodeWrite *next=renderer_outer_writes+renderer_outer_write_count;
  if(pc==next->pc&&(uintptr_t)pointer==base+next->address){qol_renderer_outer_write(c,pc,pointer,size,value);return;}
 }
 /* This helper is compiled through both guards. Ordinary memset stores,
    including unexpected attempts to change published code, keep the checks. */
 memcpy(pointer,&value,size);
}
void qol_string_vector_write(_cpu *c,uint32_t pc,uint32_t address,uint32_t reg,uint32_t mode){
 assert(c==&state&&reg==0&&((pc==0x10012272&&address==0x44b7b3&&mode==4)||(pc==0x10012c53&&address==0x689d0f&&mode==4)||(pc==0x10012c5e&&address==0x689d1f&&mode==2)));
 uint64_t values[2];memcpy(values,c->_xmm[reg],sizeof values);
 qol_string_code_write(c,pc,(void *)(uintptr_t)(base+address),8,values[0]);
 if(mode==4)qol_string_code_write(c,pc,(void *)(uintptr_t)(base+address+8),8,values[1]);
}
void qol_http_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&network_write_count==11&&http_write_count<17);
 if(!http_write_count){assert(pc==http_code_writes[0].pc);assert(tribes_memory_prepare_next_code_revision(base,http_code_writes,17));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));http_write_count++;
 fprintf(stderr,"Validated HTTP AOT write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",http_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(http_write_count==17){assert(http_flush_count==1);assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==12);fputs("Published HTTP phase after 17 exact writes\n",stderr);}
}
void qol_http_vector_write(_cpu *c,uint32_t pc,uint32_t address,uint32_t reg,uint32_t mode){
 assert(c==&state&&pc==0x10024090&&address==0x44aca0&&reg==0&&mode==4&&http_write_count==13);
 uint64_t values[2];memcpy(values,c->_xmm[reg],sizeof values);
 qol_http_code_write(c,pc,(void *)(uintptr_t)(base+address),8,values[0]);
 qol_http_code_write(c,pc,(void *)(uintptr_t)(base+address+8),8,values[1]);
}
_Noreturn void qol_network_unavailable_entry(void){fputs("TribesPad: unpublished network continuation\n",stderr);abort();}
void qol_network_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&followup_write_count==104&&followup_flush_count==2&&network_write_count<11);
 if(!network_write_count){assert(pc==network_code_writes[0].pc);assert(tribes_memory_prepare_next_code_revision(base,network_code_writes,11));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));network_write_count++;
 fprintf(stderr,"Validated network AOT write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",network_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(network_write_count==11){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==10);fputs("Published network suffix after 11 exact writes\n",stderr);}
}
_Noreturn void qol_followup_unavailable_entry(void){fputs("TribesPad: unpublished followup continuation\n",stderr);abort();}
void qol_followup_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&library_write_count==25&&library_flush_count==3&&followup_write_count<104);
 if(!followup_write_count){assert(pc==followup_code_writes[0].pc);assert(tribes_memory_prepare_next_code_revision(base,followup_code_writes,104));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));followup_write_count++;
 fprintf(stderr,"Validated followup AOT write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",followup_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(followup_write_count==104){assert(followup_flush_count==2);assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==8);fputs("Published followup prefix after 104 exact writes\n",stderr);}
}
void qol_library_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&late_flush_count==1&&input_code_write_count==2);
 assert(library_write_count<25);
 if(!library_write_count){assert(pc==library_code_writes[0].pc);assert(tribes_memory_prepare_next_code_revision(base,library_code_writes,25));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));library_write_count++;
 fprintf(stderr,"Validated library AOT write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",library_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
 if(library_write_count==25){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==6);fputs("Published library AOT revision after 25 exact writes\n",stderr);}
}
void qol_input_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2&&late_flush_count==1&&input_data_flush_count==1);
 assert(input_code_write_count<2&&input_data_matches(input_data_after));
 if(!input_code_write_count){assert(pc==input_code_writes[0].pc);assert(tribes_memory_prepare_next_code_revision(base,input_code_writes,2));}
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));input_code_write_count++;
 fprintf(stderr,"Validated input AOT write %u pc=0x%08x address=0x%08x bytes=%u\n",input_code_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size);
 if(input_code_write_count==2){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==4);fputs("Published input teardown AOT variant after both original stores\n",stderr);}
}
void qol_late_code_write(_cpu *c,uint32_t pc,const void *pointer,uint32_t size,uint64_t value){
 assert(c==&state&&c->_pointer_offset==base&&engine_entered&&stage==2);
 assert(tribes_memory_apply_code_write(base,pc,pointer,size,value));late_write_count++;
 fprintf(stderr,"Validated late AOT write %u pc=0x%08x address=0x%08x bytes=%u value=0x%llx\n",late_write_count,pc,(uint32_t)((uintptr_t)pointer-base),size,(unsigned long long)value);
}
int tribes_dispatch_permitted(uint32_t address){
 if((address>=TB_PATCH_ADDRESS&&address<TB_PATCH_ADDRESS+65536u)||(address>=0x400000u&&address<0x400000u+TB_ENGINE_BYTES))return qol_patch_ready()&&tribes_memory_code_revision(base)!=3;
 return 1;
}
static void load_publication(const char *directory){
 char path[4096];int n=snprintf(path,sizeof path,"%s/published-code.bin",directory);assert(n>0&&(size_t)n<sizeof path);
 FILE *f=fopen(path,"rb");assert(f&&fread(&publication_count,4,1,f)==1&&publication_count>0&&publication_count<=256);
 for(uint32_t i=0;i<publication_count;i++){
  TribesPublishedCodeRange *r=publication_ranges+i;assert(fread(&r->address,4,1,f)==1&&fread(&r->size,4,1,f)==1&&r->size>0&&r->size<0x1000000);
  unsigned char *p=malloc(r->size);assert(p&&fread(p,1,r->size,f)==r->size);r->expected=p;
 }
 assert(fgetc(f)==EOF&&!ferror(f)&&!fclose(f));
}
/* Original abort and empty-commit paths restore RX and flush the bank.
   The observed commit returns at10072aed from the bank loop, with no pending
   operations or threads. Both paths must retain every published bank byte;
   neither publishes code or authorizes a nonempty transaction. */
static uint32_t qol_unchanged_bank_flush(uint32_t process,uint32_t address,uint32_t bytes){
 assert(engine_entered&&stage==2&&process==UINT32_MAX&&address==TB_PATCH_ADDRESS&&bytes==65536);
 assert(renderer_hook_write_count==61&&renderer_hook_flush_count==27&&tribes_memory_code_revision(base)==62);
 assert(stack_readable(&state,60));
 const volatile uint32_t *stack=(const volatile uint32_t *)(uintptr_t)(base+state._esp);
 int abort_path=stack[0]==0x1007202b&&stack[9]==0x10072609&&stack[14]==0x1005a31f;
 /* Commit owns a 0x2dc-byte local frame: abort's small-frame caller
    indices are not caller addresses on this path. Validate its actual flush
    return, arguments and bank-loop registers instead. */
 int empty_commit=stack[0]==0x10072aed&&stack[1]==process&&stack[2]==address&&stack[3]==bytes;
 assert(abort_path||empty_commit);
 if(empty_commit)assert(state._esi==TB_PATCH_ADDRESS&&state._edi==UINT32_MAX);
 assert(*(const volatile uint32_t *)(uintptr_t)(base+0x101d6e20)==0);
 assert(*(const volatile uint32_t *)(uintptr_t)(base+0x101d6e1c)==0);
 assert(*(const volatile uint32_t *)(uintptr_t)(base+0x101d6e00)==TB_PATCH_ADDRESS);
 assert(*(const volatile uint32_t *)(uintptr_t)(base+TB_PATCH_ADDRESS+4)==0);
 for(unsigned group=0;group<3;group++){
  const TribesPublishedCodeRange *ranges=group==0?publication_ranges:group==1?pending_bank_ranges:renderer_bank_ranges;
  unsigned count=group==0?publication_count:group==1?34:26;
  for(unsigned i=0;i<count;i++){
   const TribesPublishedCodeRange *r=ranges+i;
   if(group==0&&(r->address<TB_PATCH_ADDRESS||r->address>=TB_PATCH_ADDRESS+65536))continue;
   assert(r->address>=TB_PATCH_ADDRESS&&r->size&&r->address+r->size<=TB_PATCH_ADDRESS+65536);
   const volatile unsigned char *code=(const volatile unsigned char *)(uintptr_t)(base+r->address);
   for(unsigned k=0;k<r->size;k++)assert(code[k]==r->expected[k]);
  }
 }
 fputs(empty_commit?"Validated unchanged AOT bank flush at empty transaction commit\n":"Validated unchanged AOT bank flush after failed audio transaction\n",stderr);return 1;
}
uint32_t qol_publish_patches(uint32_t process,uint32_t address,uint32_t bytes){
 if(stage==2&&audio_hook_write_count){
  assert(engine_entered&&process==UINT32_MAX&&audio_hook_write_count==21&&audio_hook_flush_count<8&&tribes_memory_code_revision(base)==3);
  unsigned i=audio_hook_flush_count;assert(address==audio_hook_flushes[i][0]&&bytes==audio_hook_flushes[i][1]);
  for(unsigned j=0;j<7;j++){
   uint32_t callback;memcpy(&callback,(void *)(uintptr_t)(base+audio_hook_callbacks[j][0]),4);assert(callback==audio_hook_callbacks[j][1]);
   const TribesPublishedCodeRange *r=audio_bank_ranges+j;const volatile unsigned char *code=(const volatile unsigned char *)(uintptr_t)(base+r->address);for(unsigned k=0;k<r->size;k++)assert(code[k]==r->expected[k]);
  }
  audio_hook_flush_count++;
  fprintf(stderr,"Validated audio hook flush %u address=0x%08x bytes=%u\n",audio_hook_flush_count,address,bytes);
  if(audio_hook_flush_count==8){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==66);fputs("Published seven audio hooks after 21 exact stores and eight original flushes\n",stderr);}
  return 1;
 }

 if(stage==2&&tribes_memory_code_revision(base)==62)return qol_unchanged_bank_flush(process,address,bytes);
 if(stage==2&&renderer_hook_write_count){
  assert(engine_entered&&process==UINT32_MAX&&renderer_hook_write_count==61&&renderer_hook_flush_count<27&&tribes_memory_code_revision(base)==3);
  unsigned i=renderer_hook_flush_count;assert(address==renderer_hook_flushes[i][0]&&bytes==renderer_hook_flushes[i][1]);
  for(unsigned j=0;j<26;j++){
   uint32_t callback;memcpy(&callback,(void *)(uintptr_t)(base+renderer_hook_callbacks[j][0]),4);assert(callback==renderer_hook_callbacks[j][1]);
   const TribesPublishedCodeRange *r=renderer_bank_ranges+j;const volatile unsigned char *code=(const volatile unsigned char *)(uintptr_t)(base+r->address);for(unsigned k=0;k<r->size;k++)assert(code[k]==r->expected[k]);
  }
  renderer_hook_flush_count++;
  fprintf(stderr,"Validated renderer hook flush %u address=0x%08x bytes=%u\n",renderer_hook_flush_count,address,bytes);
  if(renderer_hook_flush_count==27){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==62);fputs("Published 26 renderer hooks after 61 exact stores and 27 original flushes\n",stderr);}
  return 1;
 }

 if(stage==2&&renderer_startup_write_count){
  assert(engine_entered&&process==UINT32_MAX&&renderer_startup_write_count==2&&!renderer_startup_flush_count&&tribes_memory_code_revision(base)==3);
  assert(address==0x5a1130&&bytes==5);const unsigned char expected[]={0xe9,0x5b,1,0,0};const volatile unsigned char *data=(const volatile unsigned char *)(uintptr_t)(base+address);for(unsigned i=0;i<5;i++)assert(data[i]==expected[i]);renderer_startup_flush_count=1;
  fputs("Validated renderer startup JMP flush\n",stderr);return 1;
 }

 if(stage==2&&pending_hook_write_count){
  assert(engine_entered&&process==UINT32_MAX&&pending_hook_write_count==86&&pending_hook_flush_count<35&&tribes_memory_code_revision(base)==3);
  unsigned i=pending_hook_flush_count;assert(address==pending_hook_flushes[i][0]&&bytes==pending_hook_flushes[i][1]);
  for(unsigned j=0;j<34;j++){
   /* Original registrations can reuse a pointer slot; only its last writer
      remains observable after the entire ordered installation. */
   int last=1;for(unsigned k=j+1;k<34;k++)if(pending_hook_callbacks[k][0]==pending_hook_callbacks[j][0])last=0;
   if(last){uint32_t callback;memcpy(&callback,(void *)(uintptr_t)(base+pending_hook_callbacks[j][0]),4);if(callback!=pending_hook_callbacks[j][1]){fprintf(stderr,"Pending callback mismatch slot=0x%08x actual=0x%08x expected=0x%08x\n",pending_hook_callbacks[j][0],callback,pending_hook_callbacks[j][1]);abort();}}
  }
  pending_hook_flush_count++;
  fprintf(stderr,"Validated pending hook flush %u address=0x%08x bytes=%u\n",pending_hook_flush_count,address,bytes);
  if(pending_hook_flush_count==35){assert(tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==42);fputs("Published 34 pending hooks after 86 exact stores and 35 original flushes\n",stderr);}
  return 1;
 }
 if(stage==2&&string_write_count){
  assert(engine_entered&&process==UINT32_MAX&&string_flush_count<12&&tribes_memory_code_revision(base)==3);
  unsigned i=string_flush_count;assert(address==string_flushes[i].address&&bytes==string_flushes[i].size&&string_write_count==string_flushes[i].writes);
  const volatile unsigned char *data=(const volatile unsigned char *)(uintptr_t)(base+address);
  for(unsigned j=0;j<bytes;j++)assert(data[j]==string_flushes[i].bytes[j]);string_flush_count++;
  fprintf(stderr,"Validated string flush %u address=0x%08x bytes=%u\n",string_flush_count,address,bytes);
  if(string_flush_count==12){assert(string_write_count==114&&tribes_memory_finish_code_revision(base));assert(tribes_memory_code_revision(base)==14);fputs("Published string phase after 114 exact writes and 12 flushes\n",stderr);}
  return 1;
 }
 if(stage==2&&http_write_count){
  assert(engine_entered&&process==UINT32_MAX&&http_write_count==15&&!http_flush_count&&tribes_memory_code_revision(base)==3);
  assert(address==0x44aca0&&bytes==sizeof http_flush_bytes);
  const volatile unsigned char *data=(const volatile unsigned char *)(uintptr_t)(base+address);
  for(unsigned i=0;i<bytes;i++)assert(data[i]==http_flush_bytes[i]);http_flush_count++;
  fputs("Validated intermediate HTTP code flush\n",stderr);return 1;
 }

 if(stage==2&&followup_write_count&&followup_write_count<104){
  assert(process==UINT32_MAX&&tribes_memory_code_revision(base)==3&&followup_flush_count<2);
  const unsigned char *expected=followup_flush_count?followup_flush_1:followup_flush_0;
  assert(address==(followup_flush_count?0x5c0cfeu:0x798ca8u)&&bytes==(followup_flush_count?6u:43u));
  assert(followup_write_count==(followup_flush_count?6u:2u));
  const volatile unsigned char *data=(const volatile unsigned char *)(uintptr_t)(base+address);
  for(unsigned i=0;i<bytes;i++)assert(data[i]==expected[i]);followup_flush_count++;
  fprintf(stderr,"Validated followup prefix flush %u address=0x%08x bytes=%u\n",followup_flush_count,address,bytes);return 1;
 }

 if(stage==2&&bytes==sizeof library_data_after&&library_flush_count<3&&address==library_data_addresses[library_flush_count]){
  assert(engine_entered&&process==UINT32_MAX&&library_write_count==25&&tribes_memory_code_revision(base)==6);
  const volatile unsigned char *data=(const volatile unsigned char *)(uintptr_t)(base+address);
  for(unsigned i=0;i<sizeof library_data_after;i++)assert(data[i]==library_data_after[i]);library_flush_count++;
  fprintf(stderr,"Validated library version data flush %u address=0x%08x\n",library_flush_count,address);return 1;
 }

 if(stage==2&&address==TB_INPUT_DATA_ADDRESS&&bytes==TB_INPUT_DATA_BYTES){
  assert(engine_entered&&process==UINT32_MAX&&late_flush_count==1&&!input_data_flush_count);
  assert(tribes_memory_code_revision(base)==2);
  assert(input_data_matches(input_data_after));
  input_data_flush_count=1;
  fprintf(stderr,"Validated original nonexecuting input-provider data update at 0x%08x\n",address);return 1;
 }
 if(stage==2){
  assert(engine_entered&&process==UINT32_MAX&&address==TB_PATCH_ADDRESS&&bytes==65536&&!late_flush_count);
  assert(late_write_count==sizeof late_code_writes/sizeof late_code_writes[0]);
  uint32_t callback;memcpy(&callback,(void *)(uintptr_t)(base+0x799b24),4);assert(callback==0x100600c0);
  assert(tribes_memory_finish_code_revision(base));late_flush_count=1;
  fprintf(stderr,"Published late AOT revision after %u exact writes\n",late_write_count);return 1;
 }
 assert(stage==1&&process==UINT32_MAX&&flush_count<sizeof allowed_flushes/sizeof allowed_flushes[0]);
 assert(address==allowed_flushes[flush_count][0]&&bytes==allowed_flushes[flush_count][1]);
 if(!flush_count)assert(tribes_memory_publish_code(base,publication_ranges,publication_count));
 assert(qol_patch_ready());flush_count++;
 fprintf(stderr,"Published AOT flush %u address=0x%08x bytes=%u\n",flush_count,address,bytes);return 1;
}
#else
static int qol_patch_ready(void){return 0;}
#endif
#ifdef TB_CAPTURE_PATCHES
static const char *capture_directory;
static int capture_allocated;
static void snapshot(const char *name,uint32_t address,uint32_t size){
 char path[4096];int n=snprintf(path,sizeof path,"%s/%s",capture_directory,name);assert(n>0&&(size_t)n<sizeof path);
 void *copy=malloc(size);assert(copy);memcpy(copy,(void *)(uintptr_t)(base+address),size);
 FILE *f=fopen(path,"wb");assert(f&&fwrite(copy,1,size,f)==size&&!fclose(f));free(copy);
}
static void capture_patches(void){if(!capture_directory)return;snapshot("engine-after.bin",0x400000,TB_ENGINE_BYTES);snapshot("qol-after.bin",0x10000000,TB_QOL_BYTES);if(capture_allocated)snapshot("patch-after.bin",TB_PATCH_ADDRESS,65536);}
#else
static void capture_patches(void){}
#endif
void qol_alloc_result(uint32_t address,uint32_t bytes,uint32_t protection,uint32_t result){
#ifdef TB_CAPTURE_PATCHES
 if(address==TB_PATCH_ADDRESS&&bytes==65536&&protection==0x40&&result==address)capture_allocated=1;
#else
 (void)address;(void)bytes;(void)protection;(void)result;
#endif
}
void qol_alloc_request(_cpu *c,uint32_t address,uint32_t bytes,uint32_t type,uint32_t protection){uint32_t ret=0;if(stack_readable(c,4))memcpy(&ret,(void *)(uintptr_t)(base+c->_esp),4);fprintf(stderr,"VirtualAlloc caller=0x%08x address=0x%08x size=%u type=0x%x protection=0x%x\n",ret,address,bytes,type,protection);}
void qol_module_load_result(_cpu *c,uint32_t name,uint32_t result,uint32_t operation){
 char text[4096];unsigned n=0;const char *kind=operation==0?"LoadLibraryA":operation==1?"GetModuleHandleA":"FreeLibrary";
 if(operation==2)snprintf(text,sizeof text,"0x%08x",name);
 else if(!name)strcpy(text,"<main>");
 else {assert(name<=UINT32_MAX-4095);for(;n<sizeof text-1;n++){text[n]=*(const char *)(uintptr_t)(base+name+n);if(!text[n])break;}text[n]=0;}
 uint32_t caller;memcpy(&caller,(void *)(uintptr_t)(base+c->_esp),4);
 fprintf(stderr,"Observed %s caller=0x%08x name=%s result=0x%08x last_error=%u\n",kind,caller,text,result,tribes_get_last_error());
}
void qol_protect_result(uint32_t address,uint32_t bytes,uint32_t protection,uint32_t output,uint32_t result){fprintf(stderr,"VirtualProtect address=0x%08x size=%u protection=0x%x output=0x%08x result=%u last_error=%u\n",address,bytes,protection,output,result,tribes_get_last_error());}
#ifdef TB_PUBLISH_PATCHES
static unsigned write_late_checkpoint(const _cpu *c,uint32_t pc,const char *prefix){
 char path[4096];int n=snprintf(path,sizeof path,"%s/%s-cpu.bin",capture_directory,prefix);assert(n>0&&(size_t)n<sizeof path);
 FILE *f=fopen(path,"wb");assert(f&&fwrite(c,1,sizeof *c,f)==sizeof *c&&!fclose(f));
 n=snprintf(path,sizeof path,"%s/%s-pages.json",capture_directory,prefix);assert(n>0&&(size_t)n<sizeof path);
 f=fopen(path,"w");assert(f);fprintf(f,"{\"resume_pc\":%u,\"cpu_bytes\":%zu,\"ranges\":[",pc,sizeof *c);
 /* VirtualQuery's output is temporary guest scratch. Restore it before any
    page is copied so the checkpoint preserves the exact original state. */
 unsigned char saved[28];memcpy(saved,(void *)(uintptr_t)(base+0x100000),sizeof saved);unsigned count=0;
 for(uint64_t at=0;at<0x7fff0000;){
  uint32_t fields[7];assert(tribes_virtual_query(base,(uint32_t)at,0x100000,28)==28);
  memcpy(fields,(void *)(uintptr_t)(base+0x100000),sizeof fields);memcpy((void *)(uintptr_t)(base+0x100000),saved,sizeof saved);
  assert(fields[0]==at&&fields[3]&&fields[3]%4096==0);uint64_t next=at+fields[3];assert(next>at&&next<=UINT64_C(0x100000000));
  if(fields[4]==0x1000&&fields[5]!=1&&fields[5]!=0x10){
   char name[80];int k=snprintf(name,sizeof name,"%s-page-%08x-%08x.bin",prefix,fields[0],fields[3]);assert(k>0&&(size_t)k<sizeof name);
   snapshot(name,fields[0],fields[3]);fprintf(f,"%s{\"address\":%u,\"size\":%u,\"protect\":%u,\"allocation\":%u,\"type\":%u,\"file\":\"%s\"}",count?",":"",fields[0],fields[3],fields[5],fields[1],fields[6],name);count++;
  }
  at=next;
 }
 assert(count);fputs("]}\n",f);assert(!fclose(f));
 return count;
}
#endif
void qol_http_checkpoint_return(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(string_patch_verify_requested){assert(c==&state&&http_write_count==17&&tribes_memory_code_revision(base)==12);write_late_checkpoint(c,0x10061031,"string-patch");}
 if(!http_patch_verify_requested)return;
 assert(c==&state&&http_write_count==17&&tribes_memory_code_revision(base)==12);
 unsigned count=write_late_checkpoint(c,0x10061031,"http-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"http_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x10061031u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_string_checkpoint_return(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!string_patch_verify_requested)return;
 assert(c==&state&&string_write_count==114&&string_flush_count==12&&tribes_memory_code_revision(base)==14);
 unsigned count=write_late_checkpoint(c,0x10061036,"string-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"string_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x10061036u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_network_checkpoint_return(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!network_patch_verify_requested)return;
 assert(c==&state&&network_write_count==11&&tribes_memory_code_revision(base)==10);
 unsigned count=write_late_checkpoint(c,0x1006102c,"network-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"network_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x1006102cu,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_library_checkpoint_return(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!library_patch_verify_requested)return;
 assert(c==&state&&library_write_count==25&&library_flush_count==3&&tribes_memory_code_revision(base)==6);
 unsigned count=write_late_checkpoint(c,0x10061027,"library-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"library_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x10061027u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_post_file_patch_checkpoint(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!post_file_patch_verify_requested)return;
 assert(c==&state&&post_file_patch_write_count==1&&tribes_memory_code_revision(base)==20);
 unsigned count=write_late_checkpoint(c,0x10015d15,"post-file-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"post_file_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x10015d15u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_collision_patch_checkpoint(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!collision_patch_verify_requested)return;
 assert(c==&state&&collision_patch_write_count==1&&tribes_memory_code_revision(base)==22);
 unsigned count=write_late_checkpoint(c,0x1000c3a7,"collision-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"collision_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x1000c3a7u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_allocation_patch_checkpoint(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!allocation_patch_verify_requested)return;
 assert(c==&state&&allocation_patch_write_count==1&&tribes_memory_code_revision(base)==24);
 unsigned count=write_late_checkpoint(c,0x1000c43d,"allocation-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"allocation_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x1000c43du,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_clock_patch_checkpoint(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!clock_patch_verify_requested)return;
 assert(c==&state&&clock_patch_write_count==2&&tribes_memory_code_revision(base)==26);
 unsigned count=write_late_checkpoint(c,0x1006086e,"clock-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"clock_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x1006086eu,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_console_animation_checkpoint(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!console_animation_verify_requested)return;
 assert(c==&state&&console_animation_write_count==2&&tribes_memory_code_revision(base)==18);
 unsigned count=write_late_checkpoint(c,0x10002c23,"console-animation-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"console_animation_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x10002c23u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_console_fade_checkpoint(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!console_fade_verify_requested)return;
 assert(c==&state&&console_fade_write_count==1&&tribes_memory_code_revision(base)==16);
 unsigned count=write_late_checkpoint(c,0x10002938,"console-fade-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"console_fade_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,0x10002938u,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
void qol_protect_checkpoint(_cpu *c,uint32_t address,uint32_t bytes,uint32_t protection,uint32_t result){
#ifdef TB_PUBLISH_PATCHES
 if(protection_capture_address&&address==protection_capture_address&&engine_entered&&result){
  assert(result==1&&qol_patch_ready()&&c->_esp>=0x100000&&c->_esp<=0x2fffec);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"protection");
  printf("{\"diagnostic_stop\":true,\"kind\":\"protection_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u,\"address\":%u,\"bytes\":%u,\"protection\":%u}\n",attach_return,pc,count,address,bytes,protection);fflush(stdout);_Exit(71);
 }
 if(clock_patch_verify_requested&&engine_entered&&address==0x58a966&&bytes==4&&protection==0x40){
  assert(result==1&&allocation_patch_write_count==1&&tribes_memory_code_revision(base)==24);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10060806);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  write_late_checkpoint(&resumed,pc,"clock-patch");return;
 }
 if(allocation_patch_verify_requested&&engine_entered&&address==0x615f14&&bytes==4&&protection==0x40){
  assert(result==1&&collision_patch_write_count==1&&tribes_memory_code_revision(base)==22);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x1000c42f);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  write_late_checkpoint(&resumed,pc,"allocation-patch");return;
 }
 if(collision_patch_verify_requested&&engine_entered&&address==0x617c03&&bytes==4&&protection==0x40){
  assert(result==1&&post_file_patch_write_count==1&&tribes_memory_code_revision(base)==20);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x1000c3a1);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  write_late_checkpoint(&resumed,pc,"collision-patch");return;
 }
 if((post_file_patch_capture_requested||post_file_patch_verify_requested)&&engine_entered&&address==0x5bff79&&bytes==4&&protection==0x40){
  assert(result==1&&console_animation_write_count==2&&tribes_memory_code_revision(base)==18);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10015d0a);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"post-file-patch");
  if(post_file_patch_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"post_file_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((console_animation_capture_requested||console_animation_verify_requested)&&engine_entered&&address==0x67cc95&&bytes==4&&protection==0x40){
  assert(result==1&&console_fade_write_count==1&&tribes_memory_code_revision(base)==16);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10002bc0);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"console-animation");
  if(console_animation_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"console_animation_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((console_patch_capture_requested||console_fade_verify_requested)&&engine_entered&&address==0x5d2860&&bytes==4&&protection==0x40){
  assert(result==1&&qol_patch_ready()&&string_write_count==114&&string_flush_count==12&&tribes_memory_code_revision(base)==14);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x1000292d);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"console-patch");
  if(console_fade_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"console_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((http_patch_capture_requested||http_patch_verify_requested)&&engine_entered&&address==0x5bdfc4&&bytes==4&&protection==0x40){
  assert(result==1&&qol_patch_ready()&&network_write_count==11&&tribes_memory_code_revision(base)==10);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10023c80);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"http-patch");
  if(http_patch_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"http_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((network_patch_capture_requested||network_patch_verify_requested)&&engine_entered&&address==0x6985b6&&bytes==4&&protection==0x40){
  assert(result==1&&qol_patch_ready()&&followup_write_count==104&&followup_flush_count==2&&tribes_memory_code_revision(base)==8);
  assert(adapter_size_queries&&adapter_snapshots);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x100203f6);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"network-patch");
  if(network_patch_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"network_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((followup_patch_capture_requested||followup_prefix_verify_requested)&&engine_entered&&address==0x5ae363&&bytes==4&&protection==0x40){
  assert(result==1&&qol_patch_ready()&&library_write_count==25&&library_flush_count==3&&tribes_memory_code_revision(base)==6);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10027e5e);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"followup-patch");
  if(followup_prefix_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"followup_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((library_patch_capture_requested||library_patch_verify_requested)&&engine_entered&&address==0x539065&&bytes==4&&protection==0x40){
  assert(result==1&&qol_patch_ready()&&late_flush_count==1&&input_code_write_count==2&&tribes_memory_code_revision(base)==4);
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10012cf6);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"library-patch");
  if(library_patch_verify_requested)return;
  printf("{\"diagnostic_stop\":true,\"kind\":\"library_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if(startup_patch_capture_requested&&engine_entered&&address==0x556d00&&bytes==6&&protection==0x40){
  assert(result==1&&qol_patch_ready()&&late_flush_count==1&&tribes_memory_code_revision(base)==2);
  assert(input_data_flush_count==1&&input_data_matches(input_data_after));
  uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10060c9a);
  _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
  unsigned count=write_late_checkpoint(&resumed,pc,"startup-patch");
  printf("{\"diagnostic_stop\":true,\"kind\":\"startup_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
 }
 if((!late_capture_requested&&!late_verify_requested)||!engine_entered||address!=0x5c43b5||bytes!=4||protection!=0x40)return;
 assert(result==1&&qol_patch_ready()&&c->_esp>=0x100000&&c->_esp<=0x2fffec);
 uint32_t pc;memcpy(&pc,(void *)(uintptr_t)(base+c->_esp),4);assert(pc==0x10061347);
 _cpu resumed=*c;resumed._eax=result;resumed._esp+=20;
 unsigned count=write_late_checkpoint(&resumed,pc,"late");
 if(late_verify_requested)return;
 printf("{\"diagnostic_stop\":true,\"kind\":\"late_patch_checkpoint\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,pc,count);fflush(stdout);_Exit(71);
#else
 (void)c;(void)address;(void)bytes;(void)protection;(void)result;
#endif
}
void qol_late_checkpoint_return(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(!late_verify_requested)return;
 assert(c==&state&&late_flush_count==1&&tribes_memory_code_revision(base)==2);
 unsigned count=write_late_checkpoint(c,0x1006174d,"late-final");
 printf("{\"diagnostic_stop\":true,\"kind\":\"late_patch_native_return\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"late_patch_publication\":true,\"return_pc\":%u,\"ranges\":%u}\n",attach_return,0x1006174d,count);fflush(stdout);_Exit(71);
#else
 (void)c;
#endif
}
static pthread_mutex_t diagnostic_lock=PTHREAD_MUTEX_INITIALIZER;
static int stack_readable(_cpu *c,uint32_t bytes){
 uint64_t address=base+(uint64_t)c->_esp;
 return c->_pointer_offset==base&&c->stack_bottom&&c->stack_top&&address>=(uintptr_t)c->stack_bottom&&address<=(uintptr_t)c->stack_top&&bytes<=(uintptr_t)c->stack_top-address;
}
static _cpu *diagnostic_cpu(void){_cpu *c=tribes_guest_callback_context();return c?c:&state;}
static _Noreturn void stop(_cpu *c,const char *kind,uint32_t index){
 assert(!pthread_mutex_lock(&diagnostic_lock));
 uint32_t threads=tribes_guest_threads_live_count();
 if(!threads)capture_patches();
 else fprintf(stderr,"Concurrent guest threads active: CPU/stack diagnostic only; no coherent full-memory checkpoint.\n");
#ifdef TB_PUBLISH_PATCHES
 if(!threads&&engine_entered&&!strcmp(kind,"import")){
  assert(c->_pointer_offset==base&&index<sizeof imports/sizeof imports[0]);
  write_late_checkpoint(c,imports[index][1],"import");
 }
#endif
 uint32_t ret=0;if(stack_readable(c,4))memcpy(&ret,(void *)(uintptr_t)(base+c->_esp),4);
 printf("{\"diagnostic_stop\":true,\"native_patch_publication\":%s,\"attach_return\":%u,\"engine_entered\":%s,\"kind\":\"%s\",\"index\":%u,\"stage\":%u,\"return_pc\":%u,\"esp\":%u,\"eax\":%u,\"stack_arguments\":[",qol_patch_ready()?"true":"false",attach_return,engine_entered?"true":"false",kind,index,stage,ret,c->_esp,c->_eax);for(unsigned i=0;i<11;i++){uint32_t arg=0;if(stack_readable(c,4*(i+2)))memcpy(&arg,(void *)(uintptr_t)(base+c->_esp+4*(i+1)),4);printf("%s%u",i?",":"",arg);}printf("],\"registers\":{\"eax\":%u,\"ecx\":%u,\"edx\":%u,\"ebx\":%u,\"esp\":%u,\"ebp\":%u,\"esi\":%u,\"edi\":%u}}\n",c->_eax,c->_ecx,c->_edx,c->_ebx,c->_esp,c->_ebp,c->_esi,c->_edi);fflush(stdout);_Exit(71);}
void qol_curl_result(_cpu *c,uint32_t operation,uint32_t result){
 uint32_t caller;memcpy(&caller,(void *)(uintptr_t)(c->_pointer_offset+c->_esp),4);
 fprintf(stderr,"Observed curl caller=0x%x operation=%u result=0x%x\n",caller,operation,result);
}
void qol_adapter_request(_cpu *c){
#ifdef TB_PUBLISH_PATCHES
 if(followup_prefix_verify_requested){
  uint32_t token=0;for(unsigned i=0;i<sizeof imports/sizeof imports[0];i++)if(imports[i][0]==0x10129038)token=imports[i][1];assert(token);
  assert(c==&state&&followup_write_count==104&&followup_flush_count==2&&tribes_memory_code_revision(base)==8);
  uint32_t args[6];memcpy(args,(void *)(uintptr_t)(base+c->_esp),sizeof args);assert(args[0]==0x1007faa8&&args[1]==0&&args[2]==14&&args[3]==0&&args[4]==0&&c->_ebx==token);
  unsigned count=write_late_checkpoint(c,token,"followup-final");
  printf("{\"diagnostic_stop\":true,\"kind\":\"followup_prefix_native_boundary\",\"attach_return\":%u,\"engine_entered\":true,\"native_patch_publication\":true,\"resume_pc\":%u,\"ranges\":%u}\n",attach_return,token,count);fflush(stdout);_Exit(71);
 }
#else
 (void)c;
#endif
}
void qol_adapter_result(_cpu *c,uint32_t output,uint32_t size_pointer,uint32_t result){
 uint32_t caller,size=0;memcpy(&caller,(void *)(uintptr_t)(base+c->_esp),4);if(result==0||result==111)memcpy(&size,(void *)(uintptr_t)(base+size_pointer),4);
 if(result==111&&!output)adapter_size_queries++;if(result==0&&output)adapter_snapshots++;
 fprintf(stderr,"Observed GetAdaptersAddresses caller=0x%08x output=0x%08x size=%u result=%u\n",caller,output,size,result);
}
void qol_registry_result(_cpu *c,uint32_t operation,uint32_t key,uint32_t auxiliary,uint32_t result){
 uint32_t caller,handle=key;memcpy(&caller,(void *)(uintptr_t)(base+c->_esp),4);
 if(operation==0&&result==0)memcpy(&handle,(void *)(uintptr_t)(base+auxiliary),4);
 fprintf(stderr,"Observed registry operation=%u caller=0x%08x key=0x%08x auxiliary=0x%08x handle=0x%08x result=%u\n",operation,caller,key,auxiliary,handle,result);
}
void qol_scheduling_result(_cpu *c,uint32_t operation,uint32_t value,uint32_t result){
 uint32_t caller;memcpy(&caller,(void *)(uintptr_t)(base+c->_esp),4);
 fprintf(stderr,"Observed scheduling operation=%u caller=0x%08x value=0x%08x result=%u last_error=%u\n",operation,caller,value,result,tribes_get_last_error());
}
void qol_monitor_result(_cpu *c,uint32_t operation,uint32_t value,uint32_t result){
 uint32_t caller,words[10]={0};memcpy(&caller,(void *)(uintptr_t)(base+c->_esp),4);
 if(operation&&result)memcpy(words,(void *)(uintptr_t)(base+value),40);
 fprintf(stderr,"Observed monitor operation=%u caller=0x%08x result=0x%08x size=%u width=%u height=%u work=%u,%u,%u,%u\n",operation,caller,result,words[0],words[3],words[4],words[5],words[6],words[7],words[8]);
}
_Noreturn void qol_missing_import(_cpu *c,uint32_t index){stop(c,"import",index);}
_Noreturn void qol_ntdll_probe(_cpu *c,uint32_t index){stop(c,"ntdll_probe",index);}
_Noreturn void halopad_trap_unimplemented(uint32_t pc){
#ifdef TB_PUBLISH_PATCHES
 if(!tribes_guest_threads_live_count()&&engine_entered&&qol_patch_ready())write_late_checkpoint(diagnostic_cpu(),pc,"instruction");
#endif
 stop(diagnostic_cpu(),"instruction",pc);
}
_Noreturn void tribes_unimplemented_dinput(void *c,uint32_t method){stop(c,"dinput",method);}
_Noreturn void tribes_missing_helper(uint32_t identifier){stop(diagnostic_cpu(),"helper",identifier);}
void x86_rdtsc(_cpu *c){stop(c,"rdtsc",0);}
void x86_repe_cmpsw_prep(_cpu *c){stop(c,"repe_cmpsw",0);}
/* Missing runtime helpers stay explicit diagnostic stops; no success stubs. */
uint32_t X86_InterruptFlag=1;
void x86_int(_cpu *c,uint32_t number){stop(c,"interrupt",number);}
void x87_fldl2e_void(_cpu *c){stop(c,"fldl2e",0);}
void x87_fxam_void(_cpu *c){stop(c,"fxam",0);}
void x87_fnsave_void(_cpu *c,uint8_t *memory){(void)memory;stop(c,"fnsave",0);}
void x87_frstor_void(_cpu *c,const uint8_t *memory){(void)memory;stop(c,"frstor",0);}
static void commit(uint32_t address,uint32_t bytes,uint32_t type){uint64_t page=(uint64_t)sysconf(_SC_PAGESIZE),lo=address&~(page-1),hi=((uint64_t)address+bytes+page-1)&~(page-1);assert(hi<=UINT64_C(0x100000000));assert(!mprotect((void *)(uintptr_t)(base+lo),(size_t)(hi-lo),PROT_READ|PROT_WRITE));assert(tribes_address_space_declare(base,(uint32_t)lo,(uint32_t)(hi-lo),(uint32_t)lo,4,0x1000,4,type));}
static void load(const char *path,uint32_t address){FILE *f=fopen(path,"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0&&size<0x10000000&&!fseek(f,0,SEEK_SET));commit(address,(uint32_t)size,0x1000000);assert(fread((void *)(uintptr_t)(base+address),1,(size_t)size,f)==(size_t)size&&!fclose(f));}
static void put(uint32_t address,uint32_t value){memcpy((void *)(uintptr_t)(base+address),&value,4);}
int main(int argc,char **argv){
 /* This harness is macOS-only and enters the guest on process main. Cocoa's
    native SDL driver owns NSApplication setup. Mobile hosts use SDL_RunApp. */
 extern void tribes_sdl_host_application_ready(void);
 tribes_sdl_host_application_ready();
#ifdef TB_CAPTURE_PATCHES
 assert(argc==7||argc==8);capture_directory=argv[6];
#ifdef TB_PUBLISH_PATCHES
 load_publication(capture_directory);
#endif
#else
 assert(argc==6);
#endif
 void *memory=mmap(NULL,UINT64_C(0x100000000),PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(memory!=MAP_FAILED);base=(uintptr_t)memory;assert(tribes_absolute_memory_bind(base));
#ifdef TB_CHECKED_IMAGE
 assert(tribes_memory_checks_configure(base,UINT64_C(0x100000000)));
#ifdef TB_PUBLISH_PATCHES
 assert(tribes_memory_prepare_code_revision(base,late_code_writes,sizeof late_code_writes/sizeof late_code_writes[0]));
#endif
#endif
#ifdef TB_GUARD_EXTERNALS
 tribes_memory_external_configure(base,UINT64_C(0x100000000));
#endif
 assert(tribes_address_space_begin(base));load(argv[1],0x400000);load(argv[2],0x10000000);
#ifdef TB_PUBLISH_PATCHES
 char miles_path[4096];int miles_path_bytes=snprintf(miles_path,sizeof miles_path,"%s/miles.bin",capture_directory);assert(miles_path_bytes>0&&(size_t)miles_path_bytes<sizeof miles_path);load(miles_path,TB_MILES_IMAGE_BASE);
 for(unsigned i=0;i<sizeof tb_codecs/sizeof tb_codecs[0];i++){int n=snprintf(miles_path,sizeof miles_path,"%s/%s",capture_directory,tb_codecs[i].file);assert(n>0&&(size_t)n<sizeof miles_path);load(miles_path,tb_codecs[i].image);}
#endif
 commit(0x100000,0x200000,0x20000);commit(0x3b0000,0x1000,0x20000);commit(0x3a0000,0x10000,0x20000);commit(0x3d0000,0x1000,0x20000);commit(0x3e0000,0x1000,0x20000);commit(0x3f0000,sizeof tls_template+TB_TLS_ZERO_FILL,0x20000);assert(tribes_address_space_seal(base));
#ifdef TB_PUBLISH_PATCHES
 assert(input_data_matches(input_data_before));
#endif
#ifdef TB_CHECKED_SUBPAGES
 const TribesCheckedMemoryBackend backend={tribes_memory_initialize_static,tribes_memory_protect_pages};
 const void *control_targets[]={(const void *)tribes_memory_initialize_static,(const void *)tribes_memory_protect_pages};
 tribes_memory_register_targets(control_targets,2);assert(tribes_address_space_enable_checked(base,&backend));
 const TribesVirtualMemoryBackend vm_backend={tribes_memory_vm_initialize,tribes_memory_vm_change};
 const void *vm_targets[]={(const void *)tribes_memory_vm_initialize,(const void *)tribes_memory_vm_change};
 tribes_memory_register_targets(vm_targets,2);assert(tribes_virtual_memory_enable_checked(base,&vm_backend));
#ifdef TB_CAPTURE_PATCHES
 assert(tribes_address_space_enable_patch_capture(base,TB_PATCH_ADDRESS,patch_image_ranges,sizeof patch_image_ranges/sizeof patch_image_ranges[0]));
#endif
#endif
 for(unsigned i=0;i<sizeof imports/sizeof imports[0];++i)put(imports[i][0],imports[i][1]);
 memcpy((void *)(uintptr_t)(base+0x3f0000),tls_template,sizeof tls_template);put(TB_TLS_INDEX,0);put(0x3e0000,0x3f0000);
 uint64_t tid;assert(!pthread_threadid_np(NULL,&tid));assert((uint32_t)tid);assert(tribes_teb_configure(base,0x3d0000,0x300000,0x100000,(uint32_t)getpid(),(uint32_t)tid,0x3e0000));
 assert(tribes_module_register_image(base,0x400000,TB_ENGINE_IMAGE_SIZE,"C:\\TribesPad\\Game\\Tribes2.exe",1));assert(tribes_module_register_image(base,0x10000000,TB_QOL_IMAGE_SIZE,"C:\\TribesPad\\Game\\IFC22.dll",0));
 assert(tribes_module_register_resources(base,0x10000000,qol_resources,sizeof qol_resources/sizeof qol_resources[0]));
 TribesThreadModule thread_modules[]={
#ifdef TB_PUBLISH_PATCHES
  {TB_MILES_IMAGE_BASE,TB_MILES_ENTRY,UINT32_MAX,0,0,0,NULL,NULL,0},
#endif
  {0x10000000,stages[sizeof stages/sizeof stages[0]-1],0,sizeof tls_template,TB_TLS_ZERO_FILL,sizeof stages/sizeof stages[0]-1,tls_template,stages,0},
#ifdef TB_PUBLISH_PATCHES
  TB_CODEC_THREAD_MODULES
#endif
 };
#ifdef TB_PUBLISH_PATCHES
 assert(tribes_module_register_image(base,TB_MILES_IMAGE_BASE,TB_MILES_IMAGE_SIZE,"C:\\TribesPad\\Game\\Mss32.dll",0));
#endif
 #ifdef TB_PUBLISH_PATCHES
 for(unsigned i=0;i<sizeof tb_codecs/sizeof tb_codecs[0];i++){assert(tribes_module_register_image(base,tb_codecs[i].image,tb_codecs[i].size,tb_codecs[i].path,0));assert(tribes_module_set_pending(base,tb_codecs[i].image,1));}
#endif
 assert(tribes_threads_configure(base,(uint32_t)getpid(),(uint32_t)tid,TB_THREAD_STACK_RESERVE,thread_modules,sizeof thread_modules/sizeof thread_modules[0]));
 assert(tribes_system_providers_configure(base));
 assert(tribes_graphics_configure(base));
 assert(tribes_dsound_configure(base));
 /* The launch script audits both mounted roots and providers immediately
  * before execution; this is a real absent-module result, not an export stub. */
 assert(tribes_module_define_absent(base,"dbghelp.dll"));
#ifdef TB_PUBLISH_PATCHES
 /* The isolated Apple profile physically excludes this optional SDK; the
    original reference retains it. Launch-time inventories verify absence. */
 assert(tribes_module_define_absent(base,"discord_game_sdk.dll"));
 /* The legacy audio DLL is absent; preserve the original client fallback. */
 assert(tribes_module_define_absent(base,"OpenAL.dll"));
#endif
 assert(tribes_environment_configure(base,environment,sizeof environment));assert(tribes_command_line_configure(base,command_line,sizeof command_line));assert(tribes_filesystem_configure(base,argv[3],argv[4],argv[5]));assert(tribes_named_mutex_configure(argv[5]));assert(tribes_temp_directory_configure(base,TB_WINDOWS_DIRECTORY));assert(tribes_profile_configure_file(base,TB_PROFILE_FILE));
 assert(tribes_command_line_configure_w(base,wide_command_line,sizeof wide_command_line));
 uint32_t handles[3];for(unsigned i=0;i<3;++i)handles[i]=tribes_get_std_handle(0xfffffff6-i);assert(tribes_startup_configure(base,1,handles));
 uint32_t image_name_chars=tribes_get_module_filename_w(base,0,0x3a1000,1024);assert(image_name_chars&&image_name_chars<1024);
 uint32_t wide_environment=tribes_get_environment_strings_w(base),environment_bytes=tribes_environment_snapshot_size(base,wide_environment,1);assert(wide_environment&&environment_bytes<=0xe000);
 memcpy((void *)(uintptr_t)(base+0x3a2000),(void *)(uintptr_t)(base+wide_environment),environment_bytes);assert(tribes_free_environment_strings_w(base,wide_environment));
 TribesProcessMetadata process_metadata={0x3b0000,0x3a0000,0x400000,0x3a1000,image_name_chars*2,tribes_get_command_line_w(base),sizeof wide_command_line-2,0x3a2000,{handles[0],handles[1],handles[2]}};
 assert(tribes_teb_configure_process(base,&process_metadata));
 state._pointer_offset=base;state.stack_bottom=(void *)(uintptr_t)(base+0x100000);state.stack_top=(void *)(uintptr_t)(base+0x300000);x87_fninit_void(&state);tb_sse_init(&state);
#ifdef TB_PUBLISH_PATCHES
 if(argc==8){
  const char capture_prefix[]="engine-protection-capture=";
  if(!strncmp(argv[7],capture_prefix,sizeof capture_prefix-1)){
   const char *text=argv[7]+sizeof capture_prefix-1;assert(strlen(text)==8);
   for(unsigned i=0;i<8;i++){unsigned digit=text[i]>='0'&&text[i]<='9'?(unsigned)(text[i]-'0'):text[i]>='a'&&text[i]<='f'?(unsigned)(text[i]-'a'+10):16;assert(digit<16);protection_capture_address=(protection_capture_address<<4)|digit;}
   assert(protection_capture_address);
  }
  clock_patch_verify_requested=!strcmp(argv[7],"engine-clock-patch-verify");allocation_patch_verify_requested=!strcmp(argv[7],"engine-allocation-patch-verify");collision_patch_verify_requested=!strcmp(argv[7],"engine-collision-patch-verify");post_file_patch_verify_requested=!strcmp(argv[7],"engine-post-file-patch-verify");post_file_patch_capture_requested=!strcmp(argv[7],"engine-post-file-patch-capture");console_animation_verify_requested=!strcmp(argv[7],"engine-console-animation-verify");console_animation_capture_requested=!strcmp(argv[7],"engine-console-animation-capture");console_fade_verify_requested=!strcmp(argv[7],"engine-console-fade-verify");console_patch_capture_requested=!strcmp(argv[7],"engine-console-patch-capture");string_patch_verify_requested=!strcmp(argv[7],"engine-string-patch-verify");http_patch_verify_requested=!strcmp(argv[7],"engine-http-patch-verify");http_patch_capture_requested=!strcmp(argv[7],"engine-http-patch-capture");network_patch_verify_requested=!strcmp(argv[7],"engine-network-patch-verify");network_patch_capture_requested=!strcmp(argv[7],"engine-network-patch-capture");followup_prefix_verify_requested=!strcmp(argv[7],"engine-followup-prefix-verify");followup_patch_capture_requested=!strcmp(argv[7],"engine-followup-patch-capture");library_patch_verify_requested=!strcmp(argv[7],"engine-library-patch-verify");library_patch_capture_requested=!strcmp(argv[7],"engine-library-patch-capture");startup_patch_capture_requested=!strcmp(argv[7],"engine-startup-patch-capture");late_capture_requested=!strcmp(argv[7],"engine-late-capture");late_verify_requested=!strcmp(argv[7],"engine-late-verify");assert(protection_capture_address||clock_patch_verify_requested||allocation_patch_verify_requested||collision_patch_verify_requested||post_file_patch_verify_requested||post_file_patch_capture_requested||console_animation_verify_requested||console_animation_capture_requested||console_fade_verify_requested||console_patch_capture_requested||string_patch_verify_requested||http_patch_verify_requested||http_patch_capture_requested||network_patch_verify_requested||network_patch_capture_requested||followup_prefix_verify_requested||followup_patch_capture_requested||library_patch_verify_requested||library_patch_capture_requested||startup_patch_capture_requested||late_capture_requested||late_verify_requested||!strcmp(argv[7],"engine"));TBAppleDisplay display;assert(tb_apple_display_snapshot(&display));
  assert(tribes_icons_configure(base,0x400000,(void *)(uintptr_t)(base+0x400000),TB_ENGINE_BYTES));
  assert(tribes_display_configure(base,display.width,display.height));assert(tribes_display_configure_work_area(base,display.work_left,display.work_top,display.work_right,display.work_bottom));assert(tribes_keyboard_configure(base,tb_initial_keyboard_state));
  assert(tribes_dinput_configure(base));assert(tribes_dinput_set_devices(base,tb_input_devices,sizeof tb_input_devices/sizeof tb_input_devices[0]));
 }
#endif
#ifdef TB_GUARD_EXTERNALS
 tribes_memory_external_enable();
#endif
#ifdef TB_PUBLISH_PATCHES
 stage=UINT32_MAX;state._esp=0x2fff04;state._eflags=0x202;put(state._esp,TB_MILES_IMAGE_BASE);put(state._esp+4,1);put(state._esp+8,0);
 fputs("Entering original Miles process attach\n",stderr);tribes_enter(&state,TB_MILES_ENTRY);assert(state._esp==0x2fff10);fprintf(stderr,"Returned original Miles process attach EAX=%u\n",state._eax);assert(state._eax);
#endif
 for(stage=0;stage<sizeof stages/sizeof stages[0];++stage){state._esp=0x2fff04;state._eflags=0x202;put(state._esp,0x10000000);put(state._esp+4,1);put(state._esp+8,0);fprintf(stderr,"Entering original DLL stage %u at 0x%08x\n",stage,stages[stage]);tribes_enter(&state,stages[stage]);assert(state._esp==0x2fff10);fprintf(stderr,"Returned original DLL stage %u EAX=%u\n",stage,state._eax);}
 attach_return=state._eax;
 if(attach_return)tribes_threads_process_ready(base);
#ifdef TB_PUBLISH_PATCHES
 assert(flush_count==sizeof allowed_flushes/sizeof allowed_flushes[0]&&qol_patch_ready());
 if(argc==8){assert(attach_return);state._esp=0x2fff00;state._eflags=0x202;engine_entered=1;fprintf(stderr,"Entering original patched engine at 0x006fde60\n");tribes_enter(&state,0x6fde60);stop(&state,"engine_return",0);}
#endif
 capture_patches();printf("{\"diagnostic_stop\":false,\"native_patch_publication\":%s,\"attach_return\":%u,\"stages_returned\":%u}\n",qol_patch_ready()?"true":"false",state._eax,stage);return 0;}
