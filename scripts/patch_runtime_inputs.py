"""Source-bound finite patch inputs for the checked integrated startup harness."""
import hashlib,importlib.util,json,pathlib,re,struct
import pefile
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()

def translate_startup_pause(dll,image):
 # PAUSE changes no guest architectural state. Keep the spin hint as an
 # ARM64 YIELD, rather than dropping it or trapping in the original loop.
 assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
 assert pe.get_data(0x5fcbd,2)==bytes.fromhex('f390')
 needle='call void @halopad_trap_unimplemented(i32 268827837) nounwind'
 assert dll.count(needle)==1
 assert '@llvm.aarch64.hint' not in dll
 return dll.replace(needle,'call void @llvm.aarch64.hint(i32 1)',1)+'\ndeclare void @llvm.aarch64.hint(i32)\n'

def translate_startup_memory_bits(dll,image):
 assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
 for pc,raw,helper in [(0x10105216,'0fab06','tb_startup_bts_10105216'),(0x1010523b,'0fa306','tb_startup_bt_1010523b'),(0x10105269,'0fa302','tb_startup_bt_10105269')]:
  assert pe.get_data(pc-0x10000000,3)==bytes.fromhex(raw)
  needle='call void @halopad_trap_unimplemented(i32 %d) nounwind'%pc
  assert dll.count(needle)==1 and '@'+helper not in dll
  dll=dll.replace(needle,'call void @%s(%%_cpu* %%cpu)'%helper,1)+'\ndeclare void @%s(%%_cpu*)\n'%helper
 return dll

# Original compression code uses TZCNT32 register forms. These sixteen
# pinned instructions are compiled through the existing checked support path.
# Intel SDM: https://cdrdv2-public.intel.com/789581/325383-sdm-vol-2abcd.pdf
COMPRESSION_TZCNT_SITES=[
 (0x100c17bf,'f30fbcc0',0,0),
 (0x100c2c8c,'f30fbcc2',0,2),
 (0x100c2fdd,'f30fbcc0',0,0),
 (0x100c34bd,'f30fbcc0',0,0),
 (0x100c4085,'f30fbcc2',0,2),
 (0x100c4104,'f30fbcc0',0,0),
 (0x100c4115,'f30fbcc7',0,7),
 (0x100c458d,'f30fbcc2',0,2),
 (0x100c4611,'f30fbcc0',0,0),
 (0x100c461e,'f30fbcc7',0,7),
 (0x100c4be3,'f30fbcc2',0,2),
 (0x100c4c64,'f30fbcc0',0,0),
 (0x100c4c71,'f30fbcc7',0,7),
 (0x100c6933,'f30fbcce',1,6),
 (0x100c6ce0,'f30fbcce',1,6),
 (0x100c7231,'f30fbcc6',0,6),
]
def compression_tzcnt_body(destination,source,suffix):
 assert destination in range(8) and source in range(8)
 assert re.fullmatch('[A-Za-z0-9_]+',suffix)
 return '''%%tz_cpu_%s = bitcast %%_cpu* %%cpu to i8*
%%tz_source_%s = getelementptr i8, i8* %%tz_cpu_%s, i64 %d
%%tz_i32_%s = bitcast i8* %%tz_source_%s to i32*
%%tz_value_%s = load i32, i32* %%tz_i32_%s, align 4
call void @tb_tzcnt32(%%_cpu* %%cpu, i32 %d, i32 %%tz_value_%s)'''%(suffix,suffix,suffix,source*4,suffix,suffix,suffix,suffix,destination,suffix)

def translate_compression_tzcnt(dll,image):
 assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
 assert '@tb_tzcnt32' not in dll
 for pc,raw,destination,source in COMPRESSION_TZCNT_SITES:
  opcode=bytes.fromhex(raw)
  assert opcode==bytes([0xf3,0x0f,0xbc,0xc0|(destination<<3)|source])
  assert pe.get_data(pc-0x10000000,4)==opcode,hex(pc)
  section=pe.get_section_by_rva(pc-0x10000000);assert section.Characteristics&0x20000000
  needle='call void @halopad_trap_unimplemented(i32 %d) nounwind'%pc
  assert dll.count(needle)==1,hex(pc)
  dll=dll.replace(needle,compression_tzcnt_body(destination,source,'%x'%pc),1)
 return dll+'\ndeclare void @tb_tzcnt32(%_cpu*, i32, i32)\n'

def translate_math_mxcsr_save(dll,image):
 assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
 assert pe.get_data(0x10cef3,5)==bytes.fromhex('0fae5c2404')
 needle='call void @halopad_trap_unimplemented(i32 269537011) nounwind'
 helper='tb_math_stmxcsr_1010cef3'
 assert dll.count(needle)==1 and '@'+helper not in dll
 return dll.replace(needle,'call void @%s(%%_cpu* %%cpu)'%helper,1)+'\ndeclare void @%s(%%_cpu*)\n'%helper

# Remaining traps in the entered original math kernel and adjacent fallback.
# These are compiler translations of pinned instructions, not guest patches.
MATH_MODEL_SITES=[
 (0x1010cf4c,'f20f2dd1','cvtsd',2,1),
 (0x1010cf90,'660f59d1','mul',2,1),
 (0x1010cfb5,'660f59e8','mul',5,0),
 (0x1010cfb9,'660f5cc2','sub',0,2),
 (0x1010cfcd,'660f59e8','mul',5,0),
 (0x1010cfd1,'660f59c0','mul',0,0),
 (0x1010cff2,'660f59f0','mul',6,0),
 (0x1010cffa,'660f59d0','mul',2,0),
 (0x1010cffe,'660f59c0','mul',0,0),
 (0x1010d016,'660f59e8','mul',5,0),
 (0x1010d045,'660f59f2','mul',6,2),
 (0x1010d079,'660fc4c003','insert',0,3),
 (0x1010d0b3,'0fae5c2404','save',0,0),
 (0x1010d167,'660ffbc1','subq',0,1),
 # Adjacent kernel called immediately after the original first return.
 (0x1010c803,'0fae5c2404','save',0,0),
 (0x1010c85c,'f20f2dd1','cvtsd',2,1),
 (0x1010c8a0,'660f59d1','mul',2,1),
 (0x1010c8c5,'660f59e8','mul',5,0),
 (0x1010c8c9,'660f5cc2','sub',0,2),
 (0x1010c8dd,'660f59e8','mul',5,0),
 (0x1010c8e1,'660f59c0','mul',0,0),
 (0x1010c902,'660f59f0','mul',6,0),
 (0x1010c90a,'660f59d0','mul',2,0),
 (0x1010c90e,'660f59c0','mul',0,0),
 (0x1010c926,'660f59e8','mul',5,0),
 (0x1010c955,'660f59f2','mul',6,2),
 (0x1010c9d3,'0fae5c2404','save',0,0),
 # Scene projection kernel reached by original CALL0x10040ffa. Its complete
 # finite trap group shares the existing math semantics, including table reads.
 (0x10126173,'0fae5c2404','save',0,0),
 (0x101261c4,'660f59c8','mul',1,0),
 (0x101261c8,'f20f2dd1','cvtsd',2,1),
 (0x101261e0,'660f5cca','sub',1,2),
 (0x101261fa,'660f59d9','mul',3,1),
 (0x10126207,'660f59e1','mul',4,1),
 (0x1012620d,'660f5cc3','sub',0,3),
 (0x10126211,'660f590d20b41c10','mulmemabs',1,0x101cb420),
 (0x10126228,'660f5cc4','sub',0,4),
 (0x1012624b,'660f5cd0','sub',2,0),
 (0x10126257,'660f5cd4','sub',2,4),
 (0x10126264,'660f59f8','mul',7,0),
 (0x10126268,'660f5cd1','sub',2,1),
 (0x10126271,'660f59c8','mul',1,0),
 (0x1012627a,'660f59e0','mul',4,0),
 (0x10126286,'660f59c0','mul',0,0),
 (0x10126293,'660f59c8','mul',1,0),
 (0x101262a5,'660f59c8','mul',1,0),
 (0x101262a9,'660f59c0','mul',0,0),
 (0x101262b6,'660f59c8','mul',1,0),
 (0x101262c2,'660f59d8','mul',3,0),
 (0x101262ca,'660f598890000000','mulmemeax',1,0x90),
 (0x101262d2,'660f59e3','mul',4,3),
 # Second math call in the original scene continuation at0x10041021.
 (0x10125e83,'0fae5c2404','save',0,0),
 (0x10125ef6,'660f59ca','mul',1,2),
 (0x10125efe,'660f59d9','mul',3,1),
 (0x10125f0a,'660f59eb','mul',5,3),
 (0x10125f16,'660f59eb','mul',5,3),
 (0x10125f22,'660f59eb','mul',5,3),
 (0x10125f36,'660fc6db01','swapq',3,3),
 (0x10125f5d,'660f59ca','mul',1,2),
 (0x10125f65,'660f59d9','mul',3,1),
 (0x10125f71,'660f59eb','mul',5,3),
 (0x10125f7d,'660f59eb','mul',5,3),
 (0x10125f89,'660f59eb','mul',5,3),
 (0x10125f95,'660f59eb','mul',5,3),
 (0x10125fa1,'660f59eb','mul',5,3),
 (0x10125fad,'660f59eb','mul',5,3),
 (0x10125fb9,'660f59eb','mul',5,3),
 (0x10125fcd,'660fc6db01','swapq',3,3),
 (0x10126011,'660ffadd','subd',3,5),
 (0x10126072,'660f59ca','mul',1,2),
 (0x1012607a,'660f59d9','mul',3,1),
 (0x10126086,'660f59eb','mul',5,3),
 (0x10126092,'660f59eb','mul',5,3),
 (0x1012609e,'660f59eb','mul',5,3),
 (0x101260b2,'660fc6db01','swapq',3,3),
]
def translate_math_model(dll,image):
 assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 pe=pefile.PE(str(image));assert pe.OPTIONAL_HEADER.ImageBase==0x10000000
 declarations={}
 for pc,raw,op,dest,source in MATH_MODEL_SITES:
  assert pe.get_data(pc-0x10000000,len(bytes.fromhex(raw)))==bytes.fromhex(raw)
  needle='call void @halopad_trap_unimplemented(i32 %d) nounwind'%pc
  assert dll.count(needle)==1,hex(pc)
  if op=='cvtsd':
   helper='tb_sse_cvtsd2si';declarations[helper]='declare i32 @'+helper+'(%_cpu*, i32)'
   body='''%math_int = call i32 @tb_sse_cvtsd2si(%_cpu* %cpu, i32 1)
%math_cpu = bitcast %_cpu* %cpu to i8*
%math_dest = getelementptr i8, i8* %math_cpu, i64 8
%math_i32 = bitcast i8* %math_dest to i32*
store i32 %math_int, i32* %math_i32, align 4'''
  elif op=='insert':
   helper='tb_sse_insertw';declarations[helper]='declare void @'+helper+'(%_cpu*, i32, i32, i32)'
   body='''%math_eax = bitcast %_cpu* %cpu to i32*
%math_word = load i32, i32* %math_eax, align 4
call void @tb_sse_insertw(%_cpu* %cpu, i32 0, i32 %math_word, i32 3)'''
  elif op=='save':
   helper='tb_math_stmxcsr_1010cef3';declarations[helper]='declare void @'+helper+'(%_cpu*)'
   body='call void @'+helper+'(%_cpu* %cpu)'
  elif op in ['swapq','subd']:
   # These finite register-only instructions do not modify flags/MXCSR/YMM.
   # Stage both operands before publishing; vector subtraction wraps per lane.
   vector='<2 x i64>' if op=='swapq' else '<4 x i32>'
   body=f'''%math_cpu = bitcast %_cpu* %cpu to i8*
%math_left_bytes = getelementptr i8, i8* %math_cpu, i64 {176+16*dest}
%math_left_pointer = bitcast i8* %math_left_bytes to {vector}*
%math_left = load {vector}, {vector}* %math_left_pointer, align 4'''
   if op=='swapq':
    assert dest==source and raw=='660fc6db01'
    body+='\n%math_result = shufflevector <2 x i64> %math_left, <2 x i64> %math_left, <2 x i32> <i32 1, i32 0>'
   else:
    body+=f'''
%math_right_bytes = getelementptr i8, i8* %math_cpu, i64 {176+16*source}
%math_right_pointer = bitcast i8* %math_right_bytes to {vector}*
%math_right = load {vector}, {vector}* %math_right_pointer, align 4
%math_result = sub {vector} %math_left, %math_right'''
   body+=f'\nstore {vector} %math_result, {vector}* %math_left_pointer, align 4'
  elif op in ['mulmemabs','mulmemeax']:
   helper='tb_sse_mulpdmem';declarations[helper]='declare void @'+helper+'(%_cpu*, i32, i32)'
   if op=='mulmemabs':
    body='call void @tb_sse_mulpdmem(%%_cpu* %%cpu, i32 %d, i32 %d)'%(dest,source)
   else:
    body=f'''%math_eax_pointer = bitcast %_cpu* %cpu to i32*
%math_eax_value = load i32, i32* %math_eax_pointer, align 4
%math_memory_address = add i32 %math_eax_value, {source}
call void @tb_sse_mulpdmem(%_cpu* %cpu, i32 {dest}, i32 %math_memory_address)'''
  else:
   helper={'mul':'tb_sse_mulpd','sub':'tb_sse_subpd','subq':'tb_sse_psubq'}[op]
   declarations[helper]='declare void @'+helper+'(%_cpu*, i32, i32)'
   body='call void @%s(%%_cpu* %%cpu, i32 %d, i32 %d)'%(helper,dest,source)
  dll=dll.replace(needle,body,1)
 for helper,decl in sorted(declarations.items()):
  existing=re.findall(r'^declare [^\n]*@'+helper+r'\([^\n]*',dll,re.M)
  assert not existing or existing==[decl],helper
  if not existing:dll+='\n'+decl+'\n'
 return dll

def prepare(native,out):
 plan_path=R/'generated/integration-review/native-patch-current.json';plan=json.loads(plan_path.read_text())
 assert plan['verified'] and plan['all_reference_engine_changes_observed'] and plan['all_reference_qol_changes_observed']
 capture_path=R/plan['capture'];assert sha(capture_path)==plan['capture_sha256'];capture=json.loads(capture_path.read_text())
 snapshots={}
 for name,row in capture['patch_snapshots'].items():
  path=R/row['path'];assert sha(path)==row['sha256'];snapshots[name]=path.read_bytes()
 engine=R/'generated/integration-review/patched-engine';em=json.loads((engine/'result.json').read_text());ec=json.loads((engine/'va/compile.json').read_text());ev=json.loads((engine/'va/result.json').read_text())
 assert em['native_plan_sha256']==sha(plan_path) and ec['exit']==0 and sha(engine/'va/engine.o')==ec['object_sha256'] and sha(engine/'va/engine.ll')==ec['input_sha256']==ev['engine_ll_sha256']
 bank=R/'generated/integration-review/native-trampolines';bm=json.loads((bank/'result.json').read_text());assert bm['plan_sha256']==sha(plan_path) and sha(bank/'module.ll')==bm['module_sha256'] and sha(bank/'dispatch-entries.json')==bm['dispatch_entries_sha256']
 cont=R/'generated/integration-review/native-continuations';cm=json.loads((cont/'result.json').read_text());assert sha(cont/'bridges.ll')==cm['bridges_sha256'] and sha(cont/'qol/module.ll')==cm['modules']['qol']['module_sha256']
 nm=json.loads((native/'result.json').read_text());assert sha(native/'module.ll')==nm['module_ll_sha256']
 dll=(native/'module.ll').read_text();needle='define hidden fastcc void @loc_10075E20('
 assert dll.count(needle)==1
 dll=dll.replace(needle,'define hidden fastcc void @tribes_qol_unpatched_10075e20(',1)
 self_patch=next(r for r in plan['trampolines'] if r['original_target']==0x10075e20)
 assert self_patch['detour_target']==0x10061870
 dll+='''
declare i32 @qol_patch_ready()
define hidden fastcc void @loc_10075E20(%_cpu* %cpu) {
 %ready = call i32 @qol_patch_ready()
 %published = icmp ne i32 %ready, 0
 br i1 %published, label %patched, label %original
patched:
 musttail call fastcc void @loc_10061870(%_cpu* %cpu)
 ret void
original:
 musttail call fastcc void @tribes_qol_unpatched_10075e20(%_cpu* %cpu)
 ret void
}
'''
 from late_patch_runtime_inputs import prepare_late
 dll,engine_source,late_source,late_metadata=prepare_late(dll,engine/'va/engine.ll',out)
 from startup_input_variant_inputs import prepare as prepare_input_variant
 dll,input_variant_metadata=prepare_input_variant(dll,engine_source,out)
 from library_patch_runtime_inputs import prepare as prepare_library
 dll,library_metadata=prepare_library(dll,engine_source,out)
 from followup_patch_runtime_inputs import prepare as prepare_followup
 dll,followup_source,followup_metadata=prepare_followup(dll,engine_source,out)
 from network_patch_runtime_inputs import prepare as prepare_network
 dll,network_source,network_metadata=prepare_network(dll,engine_source,out)
 from http_patch_runtime_inputs import prepare as prepare_http
 dll,http_source,http_metadata=prepare_http(dll,engine_source,out)
 from string_patch_runtime_inputs import prepare as prepare_string
 dll,string_source,string_metadata=prepare_string(dll,engine_source,out)
 from console_fade_runtime_inputs import prepare as prepare_console_fade
 dll,console_fade_source,console_fade_metadata=prepare_console_fade(dll,engine_source,out)
 from console_animation_runtime_inputs import prepare as prepare_console_animation
 dll,console_animation_source,console_animation_metadata=prepare_console_animation(dll,engine_source,out)
 from post_file_patch_runtime_inputs import prepare as prepare_post_file_patch
 dll,post_file_patch_source,post_file_patch_metadata=prepare_post_file_patch(dll,engine_source,out)
 from collision_patch_runtime_inputs import prepare as prepare_collision_patch
 dll,collision_patch_source,collision_patch_metadata=prepare_collision_patch(dll,engine_source,out)
 from allocation_patch_runtime_inputs import prepare as prepare_allocation_patch
 dll,allocation_patch_source,allocation_patch_metadata=prepare_allocation_patch(dll,engine_source,out)
 from clock_patch_runtime_inputs import prepare as prepare_clock_patch
 dll,clock_patch_source,clock_patch_metadata=prepare_clock_patch(dll,engine_source,out)
 from post_video_prefix_runtime_inputs import prepare as prepare_post_video_prefix
 dll,post_video_prefix_source,post_video_prefix_metadata=prepare_post_video_prefix(dll,engine_source,out)
 from post_video_nop_runtime_inputs import prepare as prepare_post_video_nop
 dll,post_video_nop_source,post_video_nop_metadata=prepare_post_video_nop(dll,engine_source,out)
 from post_video_nop_second_runtime_inputs import prepare as prepare_post_video_nop_second
 dll,post_video_nop_second_source,post_video_nop_second_metadata=prepare_post_video_nop_second(dll,engine_source,out)
 from post_video_tail_runtime_inputs import prepare as prepare_post_video_tail
 dll,post_video_tail_source,post_video_tail_metadata=prepare_post_video_tail(dll,engine_source,out)
 from display_immediates_runtime_inputs import prepare as prepare_display_immediates
 dll,display_immediates_source,display_immediates_metadata=prepare_display_immediates(dll,engine_source,out)
 from startup_calls_runtime_inputs import prepare as prepare_startup_calls
 dll,startup_calls_source,startup_calls_metadata=prepare_startup_calls(dll,engine_source,out)
 from font_size_runtime_inputs import prepare as prepare_font_size
 dll,font_size_source,font_size_metadata=prepare_font_size(dll,engine_source,out)
 from input_nop_runtime_inputs import prepare as prepare_input_nop
 dll,input_nop_source,input_nop_metadata=prepare_input_nop(dll,engine_source,out)
 from pending_hook_runtime_inputs import prepare as prepare_pending_hooks
 dll,pending_hook_sources,pending_hook_metadata=prepare_pending_hooks(dll,engine_source,out)
 from renderer_call_runtime_inputs import prepare as prepare_renderer_call
 dll,renderer_call_source,renderer_call_metadata=prepare_renderer_call(dll,engine_source,out)
 from renderer_startup_runtime_inputs import prepare as prepare_renderer_startup
 dll,renderer_startup_source,renderer_startup_metadata=prepare_renderer_startup(dll,engine_source,out)
 from renderer_redirects_runtime_inputs import prepare as prepare_renderer_redirects
 dll,renderer_redirects_source,renderer_redirects_metadata=prepare_renderer_redirects(dll,engine_source,out)
 from renderer_dimensions_runtime_inputs import prepare as prepare_renderer_dimensions
 dll,renderer_dimensions_source,renderer_dimensions_metadata=prepare_renderer_dimensions(dll,engine_source,out)
 from renderer_batch_runtime_inputs import prepare as prepare_renderer_batch
 dll,renderer_batch_source,renderer_batch_metadata=prepare_renderer_batch(dll,engine_source,out)
 from renderer_nested_runtime_inputs import prepare as prepare_renderer_nested
 dll,renderer_nested_source,renderer_nested_metadata=prepare_renderer_nested(dll,engine_source,out)
 from renderer_outer_runtime_inputs import prepare as prepare_renderer_outer
 dll,renderer_outer_source,renderer_outer_metadata=prepare_renderer_outer(dll,engine_source,out)
 from renderer_hook_runtime_inputs import prepare as prepare_renderer_hooks
 dll,renderer_hook_sources,renderer_hook_metadata=prepare_renderer_hooks(dll,engine_source,out)
 from audio_table_runtime_inputs import prepare as prepare_audio_tables
 dll,audio_tables_source,audio_tables_metadata=prepare_audio_tables(dll,engine_source,out)
 from audio_hook_runtime_inputs import prepare as prepare_audio_hooks
 dll,audio_hook_sources,audio_hook_metadata=prepare_audio_hooks(dll,engine_source,out)
 from mission_literal_runtime_inputs import prepare as prepare_mission_literals
 dll,mission_literal_metadata=prepare_mission_literals(dll,engine_source,renderer_batch_source,out)
 from render_job_runtime_inputs import prepare as prepare_render_job
 dll,render_job_metadata=prepare_render_job(dll,out)
 from mission_entry_runtime_inputs import prepare as prepare_mission_entry
 mission_entry_sources,mission_entry_metadata=prepare_mission_entry(native,out)
 input_variant_metadata["engine_before_library_sha256"]=input_variant_metadata["engine_sha256"]
 input_variant_metadata["engine_sha256"]=sha(engine_source)
 (out/"startup-input-code.json").write_text(json.dumps(input_variant_metadata,indent=2)+"\n")
 late_metadata['outputs_sha256'][str(engine_source.relative_to(R))]=sha(engine_source)
 (out/'late-patch-inputs.json').write_text(json.dumps(late_metadata,indent=2)+'\n')
 dll=translate_startup_pause(dll,R/'ref/qol/IFC22.dll')
 dll=translate_startup_memory_bits(dll,R/'ref/qol/IFC22.dll')
 dll=translate_compression_tzcnt(dll,R/'ref/qol/IFC22.dll')
 dll=translate_math_mxcsr_save(dll,R/'ref/qol/IFC22.dll')
 dll=translate_math_model(dll,R/'ref/qol/IFC22.dll')
 from fp_control_query_inputs import translate as translate_fp_control_query
 dll,fp_control_query_metadata=translate_fp_control_query(dll)
 from apple_render_dimensions import prepare as prepare_apple_render_dimensions
 dll,apple_render_dimensions_metadata=prepare_apple_render_dimensions(dll)
 from apple_render_present import prepare as prepare_apple_render_present
 dll,apple_render_present_metadata=prepare_apple_render_present(dll)
 from apple_primitive_restart import prepare as prepare_apple_primitive_restart
 dll,apple_primitive_restart_metadata=prepare_apple_primitive_restart(dll)
 (out/'qol-patched.ll').write_text(dll)
 spec=importlib.util.spec_from_file_location('patch_va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
 entries=[]
 for file in [native/'module.ll',engine/'va/engine.ll',cont/'qol/module.ll']:
  entries += [(int(n[4:],16),n) for n in re.findall(r'^define (?:hidden|protected) fastcc void @(loc_[0-9A-F]+)\(',file.read_text(),re.M)]
 entries += [(int(n[4:],16),n) for n in followup_metadata['added']+network_metadata['added']+http_metadata['added']+string_metadata['added']+startup_calls_metadata['added']]
 entries += [(int(n[4:],16),n) for n in pending_hook_metadata['added']+pending_hook_metadata['detour_entries']]
 entries += [(r['address'],r['symbol']) for r in pending_hook_metadata['bank_entries']]
 entries += [(int(n[4:],16),n) for n in renderer_hook_metadata['added']]
 entries += [(r['address'],r['symbol']) for r in renderer_hook_metadata['bank_entries']]
 entries += [(int(n[4:],16),n) for n in audio_hook_metadata['added']]
 entries += [(r['address'],r['symbol']) for r in audio_hook_metadata['bank_entries']]
 entries += [(int(n[4:],16),n) for n in mission_entry_metadata['added']]
 imports=nm['imports']+ev['imports']
 from codec_runtime_inputs import prepare as prepare_codecs
 codec_sources,codec_metadata,codec_imports=prepare_codecs(out)
 imports+=codec_imports
 for module in codec_metadata:entries += [(v,n) for v,n in module['entries']]
 from miles_runtime_inputs import prepare as prepare_miles
 miles_sources,miles_metadata,miles_linked=prepare_miles(out,imports)
 imports+=miles_metadata['imports']
 entries += [(v,n) for v,n in miles_metadata['entries']]
 entries += [(row['guest_va'],row['symbol']) for row in imports]
 entries += [(row['address'],row['symbol']) for row in json.loads((bank/'dispatch-entries.json').read_text())]
 assert len({v for v,n in entries})==len(entries)
 (out/'merged-dispatch.ll').write_text(va.dispatch_ir(sorted(entries)))
 # Compare and freeze entire original executable sections, plus exact moved
 # trampoline instruction intervals. Bank allocator bookkeeping stays mutable.
 ranges=[]
 for owner,path in [('engine',R/'ref/control/Tribes2.exe'),('qol',R/'ref/product/IFC22.dll')]:
  pe=pefile.PE(str(path));snapshot=snapshots[owner+'-after.bin']
  for section in pe.sections:
   if not section.Characteristics&0x20000000:continue
   size=max(section.Misc_VirtualSize,section.SizeOfRawData);rva=section.VirtualAddress
   data=snapshot[rva:rva+size];assert len(data)==size
   ranges.append((pe.OPTIONAL_HEADER.ImageBase+rva,data))
 for row in plan['trampolines']:ranges.append((row['address'],bytes.fromhex(row['code_hex'])))
 miles_pe=pefile.PE(str(R/'ref/product/Mss32.dll'))
 for section in miles_pe.sections:
  if section.Characteristics&0x20000000:
   rva=section.VirtualAddress;size=max(section.Misc_VirtualSize,section.SizeOfRawData)
   ranges.append((miles_pe.OPTIONAL_HEADER.ImageBase+rva,miles_pe.get_data(rva,size)))
 for module in codec_metadata:
  codec_pe=pefile.PE(str(R/module['config']['product_path']))
  for section in codec_pe.sections:
   if section.Characteristics&0x20000000:
    rva=section.VirtualAddress;size=max(section.Misc_VirtualSize,section.SizeOfRawData)
    ranges.append((codec_pe.OPTIONAL_HEADER.ImageBase+rva,codec_pe.get_data(rva,size)))
 ranges.sort();assert len(ranges)<=256
 manifest=bytearray(struct.pack('<I',len(ranges)))
 for address,data in ranges:manifest+=struct.pack('<II',address,len(data))+data
 (out/'published-code.bin').write_bytes(manifest)
 flushes=[(r['original_target'],len(bytes.fromhex(r['restore_hex']))) for r in reversed(plan['trampolines'])]+[(plan['native_allocation']['address'],plan['native_allocation']['size'])]
 reference=json.loads((R/plan['reference']).read_text());assert sha(R/plan['reference'])==plan['reference_sha256']
 reference_trace=json.loads((R/reference['capture']).read_text());assert sha(R/reference['capture'])==reference['capture_sha256']
 expected=[(e['address'],e['bytes']) for e in reference_trace['instruction_cache_reference']['events']]
 assert flushes[:-1]==expected[:-1] and expected[-1][1]==flushes[-1][1]
 (out/'patch_publication.inc').write_text('static const uint32_t allowed_flushes[][2]={'+','.join('{%du,%du}'%r for r in flushes)+'};\n')
 # Standard Win32 stdcall return/dispatch; the C implementation performs real
 # finite publication and rejects every request outside this captured contract.
 wrapper='''declare i32 @qol_publish_patches(i32, i32, i32)
declare ptr @tribes_lookup(i32)
define hidden fastcc void @tbimp_FlushInstructionCache(ptr %cpu) {
 %sp = getelementptr i8, ptr %cpu, i64 16
 %esp = load i32, ptr %sp
 %basep = getelementptr i8, ptr %cpu, i64 112
 %base = load i64, ptr %basep
 %offset = zext i32 %esp to i64
 %addr = add i64 %base, %offset
 %retp = inttoptr i64 %addr to ptr
 %target = load i32, ptr %retp, align 1
 %p0 = getelementptr i8, ptr %retp, i64 4
 %a0 = load i32, ptr %p0, align 1
 %p1 = getelementptr i8, ptr %retp, i64 8
 %a1 = load i32, ptr %p1, align 1
 %p2 = getelementptr i8, ptr %retp, i64 12
 %a2 = load i32, ptr %p2, align 1
 %result = call i32 @qol_publish_patches(i32 %a0, i32 %a1, i32 %a2)
 store i32 %result, ptr %cpu
 %next = add i32 %esp, 16
 store i32 %next, ptr %sp
 %fn = call ptr @tribes_lookup(i32 %target)
 musttail call fastcc void %fn(ptr %cpu)
 ret void
}
'''
 (out/'publication-bindings.ll').write_text(wrapper)
 linked=[];cross=[]
 for row in ev['imports']:
  if row['dll']!='ifc22.dll':continue
  target=nm['exports'][row['name']];assert any(v==target for v,n in entries)
  linked.append(dict(symbol=row['symbol'],name=row['name'],target=target))
  cross+=['define hidden fastcc void @%s(ptr %%cpu) { musttail call fastcc void @loc_%X(ptr %%cpu)\n ret void\n}'%(row['symbol'],target)]
 assert len(linked)==11
 (out/'cross-imports.ll').write_text('\n'.join(['declare hidden fastcc void @loc_%X(ptr)'%v for v in sorted({r['target'] for r in linked})]+cross)+'\n')
 linked+=miles_linked
 keyboard=R/'config/runtime/keyboard-state.json';devices=R/'config/runtime/input-devices.json'
 initial=json.loads(keyboard.read_text())['initial_virtual_keys'];assert len(initial)==256
 config=['static const unsigned char tb_initial_keyboard_state[]={'+','.join(map(str,initial))+'};'];records=[]
 for index,device in enumerate(json.loads(devices.read_text())['devices']):
  objects=[]
  for obj in device['objects']:
   guid=bytes.fromhex(obj['guid_hex']);assert len(guid)==16
   objects.append('{'+','.join(['{'+','.join(map(str,guid))+'}',str(obj['offset']),str(obj['type']),str(obj['flags']),json.dumps(obj['name']),'0','0'])+'}')
  config.append('static const TribesInputObject tb_objects_%d[]={%s};'%(index,','.join(objects)))
  instance=bytes.fromhex(device['instance_guid_hex']);product=bytes.fromhex(device['product_guid_hex']);assert len(instance)==len(product)==16
  records.append('{'+','.join(['{'+','.join(map(str,instance))+'}','{'+','.join(map(str,product))+'}',str(device['type']),json.dumps(device['instance_name']),json.dumps(device['product_name']),str(int(device['attached'])),str(device['axes']),str(device['buttons']),str(device['povs']),'tb_objects_%d'%index,str(len(objects))])+'}')
 config.append('static const TribesInputDevice tb_input_devices[]={'+','.join(records)+'};')
 (out/'engine_bootstrap.inc').write_text('\n'.join(config)+'\n')
 sources=[out/'qol-patched.ll',engine_source,bank/'module.ll',cont/'qol/module.ll',cont/'bridges.ll',out/'publication-bindings.ll',out/'cross-imports.ll',late_source,followup_source,network_source,http_source,string_source,console_fade_source,console_animation_source,post_file_patch_source,collision_patch_source,allocation_patch_source,clock_patch_source,post_video_prefix_source,post_video_nop_source,post_video_nop_second_source,post_video_tail_source,display_immediates_source,startup_calls_source,input_nop_source,renderer_call_source,renderer_startup_source,renderer_redirects_source,renderer_dimensions_source,renderer_batch_source,renderer_nested_source,renderer_outer_source]+pending_hook_sources+renderer_hook_sources
 sources+=[font_size_source,audio_tables_source]+miles_sources+codec_sources+audio_hook_sources+mission_entry_sources
 sources += [R/'port/llasm-support/tribes_fp_query.c']
 metadata=dict(plan_sha256=sha(plan_path),linked_imports=linked,miles=miles_metadata,codecs=codec_metadata,whole_engine_object_sha256=ec['object_sha256'],manifest_sha256=sha(out/'published-code.bin'),ranges=[dict(address=v,bytes=len(b)) for v,b in ranges],dispatch_entries=len(entries),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[out/'merged-dispatch.ll',out/'patch_publication.inc',out/'engine_bootstrap.inc',out/'miles_bootstrap.inc',out/'codec_bootstrap.inc',R/'scripts/codec_runtime_inputs.py',R/'scripts/miles_runtime_inputs.py',keyboard,devices,pathlib.Path(__file__).resolve()]})
 metadata['late_patch']=late_metadata
 metadata['mission_literals']=mission_literal_metadata
 metadata['render_job']=render_job_metadata
 metadata['mission_entry']=mission_entry_metadata
 metadata['apple_render_dimensions']=apple_render_dimensions_metadata
 metadata['apple_render_present']=apple_render_present_metadata
 metadata['apple_primitive_restart']=apple_primitive_restart_metadata
 metadata['startup_input_code']=input_variant_metadata
 metadata['library_patch']=library_metadata
 metadata['followup_patch']=followup_metadata
 metadata['network_patch']=network_metadata
 metadata['http_patch']=http_metadata
 metadata['string_patch']=string_metadata
 metadata['console_fade_patch']=console_fade_metadata
 metadata['console_animation_patch']=console_animation_metadata
 metadata['post_file_patch']=post_file_patch_metadata
 metadata['collision_patch']=collision_patch_metadata
 metadata['allocation_patch']=allocation_patch_metadata
 metadata['clock_patch']=clock_patch_metadata
 metadata['post_video_prefix']=post_video_prefix_metadata
 metadata['post_video_nop']=post_video_nop_metadata
 metadata['post_video_nop_second']=post_video_nop_second_metadata
 metadata['post_video_tail']=post_video_tail_metadata
 metadata['display_immediates']=display_immediates_metadata
 metadata['startup_calls']=startup_calls_metadata
 metadata['pending_hooks']=pending_hook_metadata
 metadata['input_nop']=input_nop_metadata
 metadata['font_size']=font_size_metadata
 metadata['renderer_call']=renderer_call_metadata
 metadata['renderer_startup']=renderer_startup_metadata
 metadata['renderer_redirects']=renderer_redirects_metadata
 metadata['renderer_dimensions']=renderer_dimensions_metadata
 metadata['renderer_batch']=renderer_batch_metadata
 metadata['renderer_nested']=renderer_nested_metadata
 metadata['renderer_outer']=renderer_outer_metadata
 metadata['renderer_hooks']=renderer_hook_metadata
 metadata['audio_tables']=audio_tables_metadata
 metadata['audio_hooks']=audio_hook_metadata
 metadata['startup_pause']=dict(pc=0x1005fcbd,bytes_hex='f390',native='yield',image_sha256=sha(R/'ref/qol/IFC22.dll'))
 metadata['math_model_instructions']=dict(image_sha256=sha(R/'ref/qol/IFC22.dll'),sites=[dict(pc=pc,bytes_hex=raw,operation=op,destination=dest,source=src) for pc,raw,op,dest,src in MATH_MODEL_SITES])
 metadata['fp_control_query']=fp_control_query_metadata
 (out/'patch-inputs.json').write_text(json.dumps(metadata,indent=2)+'\n')
 return sources,out/'merged-dispatch.ll',imports,metadata
