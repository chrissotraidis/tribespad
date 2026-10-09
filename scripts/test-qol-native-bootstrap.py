#!/usr/bin/env python3
"""Enter a diagnostic-only native IFC22 image and record its first boundary."""
import argparse,hashlib,json,pathlib,re,subprocess
import pefile
import capstone
from sse_build_inputs import sse_inputs
from runtime_module_catalog import audit_absent_module
from image_resources import image_resources,resource_header
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);p.add_argument('--prepare-only',action='store_true',help='Verify and prepare original sources without compiling or running the Mac engine');p.add_argument('--checked-image',action='store_true',help='Use checked image only; independent subpage protection remains disabled until helper coverage is complete');p.add_argument('--checked-runtime',action='store_true',help='Use checked native service objects as well; remaining helpers/libraries are not attested');p.add_argument('--checked-support',action='store_true',help='Also instrument supplementary helpers, ABI wrappers and dispatch');p.add_argument('--guard-externals',action='store_true',help='Stop unverified external calls and enforce a finite indirect target set');p.add_argument('--checked-subpages',action='store_true',help='Enable enforced 4 KiB protection with complete checked support and external gates');p.add_argument('--capture-patches',action='store_true',help='Diagnostic capture in nonexecuting backing; no native patch publication');p.add_argument('--publish-patches',action='store_true',help='Link the complete patched engine and verified finite routes, validate and freeze captured executable bytes at publication');p.add_argument('--enter-engine',action='store_true',help='After successful published DLL attach, enter the original patched engine');p.add_argument('--capture-late-patch',action='store_true',help='Stop after the observed later VirtualProtect and save actual CPU/readable guest pages for isolated x86 inspection');p.add_argument('--verify-late-patch',action='store_true',help='Capture both ends of the original native late patch routine');p.add_argument('--verify-followup-prefix',action='store_true',help='Capture both native endpoints through the first adapter query, without fabricating its result');p.add_argument('--verify-network-patch',action='store_true',help='Capture both native endpoints of the11-write networking suffix');p.add_argument('--verify-string-patch',action='store_true',help='Capture both endpoints of the114-write string/utility patch');p.add_argument('--verify-http-patch',action='store_true',help='Capture both endpoints of the17-write HTTP patch phase');p.add_argument('--capture-http-patch',action='store_true',help='Capture original CPU/readable pages before the HTTP integration patch at0x5bdfc4');p.add_argument('--capture-network-patch',action='store_true',help='Capture original CPU/pages after adapter enumeration before the patch at0x6985b6');p.add_argument('--capture-followup-patch',action='store_true',help='Capture original CPU/readable pages before the later patch at0x5ae363');p.add_argument('--verify-library-patch',action='store_true',help='Capture both endpoints of native call-redirection routine');p.add_argument('--capture-library-patch',action='store_true',help='Capture original CPU/readable pages before the library redirect patch phase');p.add_argument('--capture-startup-patch',action='store_true',help='Capture original CPU/readable pages at the next startup patch, without permitting writes');p.add_argument('--verify-console-animation',action='store_true',help='Capture native endpoints of both animation CALL redirect stores');p.add_argument('--capture-console-animation',action='store_true',help='Capture the next original animation CALL patch at0x67cc95 without admitting it');p.add_argument('--verify-console-fade',action='store_true',help='Compare native endpoints of the first console CALL displacement store');p.add_argument('--capture-console-patch',action='store_true',help='Capture original CPU/pages before the newly reached console CALL patch at0x5d2860');p.add_argument('--capture-protection',type=lambda value:int(value,0),help='Diagnostic-only: save native state after the first successful engine VirtualProtect for this exact guest address');p.add_argument('--verify-clock-patch',action='store_true',help='Capture native endpoints of both clock CALL stores');p.add_argument('--verify-allocation-patch',action='store_true',help='Capture native endpoints of the allocation-size immediate store');p.add_argument('--verify-collision-patch',action='store_true',help='Capture both native endpoints of the collision-query CALL displacement');p.add_argument('--verify-post-file-patch',action='store_true',help='Capture native endpoints of the single post-file CALL displacement');p.add_argument('--capture-post-file-patch',action='store_true',help='Capture native CPU/pages before the newly reached CALL displacement at0x5bff79');p.add_argument('--probe-ntdll',action='store_true',help='Diagnostic-only missing NTDLL export routes: stop on first call, never return a service result');a=p.parse_args();assert a.capture_protection is None or 0<a.capture_protection<=0xffffffff;assert not (a.capture_late_patch and a.verify_late_patch);assert not (a.capture_startup_patch and (a.capture_late_patch or a.verify_late_patch));assert sum([a.capture_protection is not None,a.verify_clock_patch,a.verify_allocation_patch,a.verify_collision_patch,a.verify_post_file_patch,a.capture_post_file_patch,a.verify_console_animation,a.capture_console_animation,a.verify_console_fade,a.capture_console_patch,a.verify_string_patch,a.verify_http_patch,a.capture_http_patch,a.verify_network_patch,a.capture_network_patch,a.verify_followup_prefix,a.capture_followup_patch,a.verify_library_patch,a.capture_library_patch,a.capture_startup_patch,a.capture_late_patch,a.verify_late_patch])<=1;a.enter_engine=a.capture_protection is not None or a.verify_clock_patch or a.verify_allocation_patch or a.verify_collision_patch or a.verify_post_file_patch or a.capture_post_file_patch or a.verify_console_animation or a.capture_console_animation or a.verify_console_fade or a.capture_console_patch or a.verify_string_patch or a.verify_http_patch or a.capture_http_patch or a.verify_network_patch or a.capture_network_patch or a.verify_followup_prefix or a.capture_followup_patch or a.verify_library_patch or a.capture_library_patch or a.enter_engine or a.capture_late_patch or a.verify_late_patch or a.capture_startup_patch;a.publish_patches=a.publish_patches or a.enter_engine;a.capture_patches=a.capture_patches or a.publish_patches;a.checked_subpages=a.checked_subpages or a.capture_patches;a.guard_externals=a.guard_externals or a.checked_subpages;a.checked_support=a.checked_support or a.guard_externals;a.checked_runtime=a.checked_runtime or a.checked_support;a.checked_image=a.checked_image or a.checked_runtime;work=a.lift.resolve();native=work/'native-diagnostic';meta=json.loads((native/'result.json').read_text());assert meta['diagnostic_only'] and meta['state']=='compiled' and meta['exit']==0
assert sha(native/'module.o')==meta['object_sha256'] and sha(native/'module.ll')==meta['module_ll_sha256'] and sha(native/'dispatch.ll')==meta['dispatch_sha256']
for name,h in meta['support_sha256'].items():assert sha(R/name)==h,name
module=native/'module.o';checked_meta=None
if a.checked_image:
    checked_meta=json.loads((native/'checked/result.json').read_text());module=native/'checked/module.o'
    assert checked_meta['state']=='compiled' and checked_meta['diagnostic_only'] and not checked_meta['link_coverage_complete']
    assert sha(module)==checked_meta['object_sha256'] and sha(native/'module.ll')==checked_meta['source_sha256']
out=native/('checked/bootstrap-published' if a.publish_patches else 'checked/bootstrap-patches' if a.capture_patches else 'checked/bootstrap-subpages' if a.checked_subpages else 'checked/bootstrap-guarded' if a.guard_externals else 'checked/bootstrap-support' if a.checked_support else 'checked/bootstrap-runtime' if a.checked_runtime else 'checked/bootstrap' if a.checked_image else 'bootstrap');out=out.with_name(out.name+'-ntdll-probe') if a.probe_ntdll else out;out.mkdir(exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps(dict(state='building',diagnostic_only=True,playable=False))+'\n')
# Check the dependencies that otherwise fail only after minutes of image
# preparation. Their full checks remain at the point of use below.
runtime_receipt=R/'generated/native-win32/macos/result.json'
assert json.loads(runtime_receipt.read_text())['sse_header_sha256']==sha(R/'port/llasm-support/tribes_sse.h'),'Rebuild native Win32 objects after an SSE header change'
if a.publish_patches:
    for check in ['mission-literals','mission-entry-test','render-job-handoff',
                  'apple-render-dimensions','apple-render-present',
                  'apple-primitive-restart','packed-compare']:
        proof=json.loads((R/'generated/integration-review'/check/'result.json').read_text())
        assert proof['passed'],check
        for name,digest in proof['sources_sha256'].items():
            assert sha(R/name)==digest,(check,'rerun its test for changed source',name)
game_root=R/'ref/product';game_profile=None
if a.publish_patches:
    from apple_game_profile import prepare as prepare_game_profile
    game_root,game_profile=prepare_game_profile()
published_sources=[];dispatch_source=native/'dispatch.ll';all_imports=meta['imports'];patch_inputs=None
if a.publish_patches:
    from patch_runtime_inputs import prepare
    published_sources,dispatch_source,all_imports,patch_inputs=prepare(native,out)
from startup_input_patch_inputs import prepare as prepare_startup_input
startup_input=prepare_startup_input(out) if a.publish_patches else None
legacy_gl_profile=None
resource_catalogs={};image_extents={};image_sizes={};image_bounds=[];patch_ranges=[]
for name,path in [('engine',R/'ref/control/Tribes2.exe'),('qol',work/'IFC22.dll')]:
    if name=='qol':assert sha(path)==meta['image_sha256']
    else:
        manifest=json.loads((R/'inputs-manifest.json').read_text())
        assert sha(path)==next(m['sha256'] for m in manifest['modules'] if m['path']=='ref/control/Tribes2.exe')
    image=pefile.PE(str(path));
    if name=='qol':resource_catalogs[name]=image_resources(image)
    data=image.get_memory_mapped_image();image_extents[name]=image.OPTIONAL_HEADER.SizeOfImage;
    if name=='engine':
        assert not hasattr(image,'DIRECTORY_ENTRY_TLS'), 'Engine TLS needs explicit thread-module configuration'
        engine_stack_reserve=image.OPTIONAL_HEADER.SizeOfStackReserve
    if name=='qol' and a.publish_patches:
        from apple_legacy_gl_profile import prepare as prepare_legacy_gl
        data,legacy_gl_profile=prepare_legacy_gl(path,data)
        (out/'apple-legacy-gl-profile.json').write_text(json.dumps(legacy_gl_profile,indent=2)+'\n')
    assert 0<len(data)<=image_extents[name];(out/(name+'.bin')).write_bytes(data);image_sizes[name]=len(data);image_bounds.append((image.OPTIONAL_HEADER.ImageBase,image.OPTIONAL_HEADER.ImageBase+image.OPTIONAL_HEADER.SizeOfImage))
    for section in image.sections:
        if section.Characteristics&0x20000000:patch_ranges.append([image.OPTIONAL_HEADER.ImageBase+section.VirtualAddress,(max(section.Misc_VirtualSize,section.SizeOfRawData)+4095)&~4095])
profile=json.loads((R/'config/runtime/environment.json').read_text());env=b''.join((name+'='+value).encode(profile['encoding'])+b'\0' for name,value in sorted(profile['variables'].items(),key=lambda kv:kv[0].casefold()))+b'\0';command=profile['command_line']['value'].encode(profile['command_line']['encoding'])+b'\0'
wide_command=profile['command_line']['value'].encode('utf-16le')+b'\0\0'
header=lambda name,data:'static const unsigned char '+name+'[]={'+','.join(map(str,data))+'};\n'
tls=meta['tls'];stages=tls['callbacks']+[meta['entry']]
(out/'qol_bootstrap.inc').write_text('#define TB_ENGINE_IMAGE_SIZE %du\n#define TB_QOL_IMAGE_SIZE %du\n#define TB_THREAD_STACK_RESERVE %du\n'%(image_extents['engine'],image_extents['qol'],engine_stack_reserve)+header('environment',env)+header('command_line',command)+header('wide_command_line',wide_command)+header('tls_template',bytes.fromhex(tls['template_hex']))+'#define TB_TLS_INDEX '+str(tls['index_address'])+'u\n#define TB_TLS_ZERO_FILL '+str(tls['zero_fill'])+'u\nstatic const uint32_t stages[]={'+','.join(map(str,stages))+'};\nstatic const uint32_t imports[][2]={'+','.join('{%du,%du}'%(r['slot'],r['guest_va']) for r in all_imports)+'};\n')
legacy_profile=profile['legacy_profile'];assert legacy_profile['backend']=='file' and legacy_profile['ini_file_mapping']=={}
with (out/'qol_bootstrap.inc').open('a') as f:f.write('#define TB_WINDOWS_DIRECTORY '+json.dumps(legacy_profile['windows_directory'])+'\n#define TB_PROFILE_FILE '+json.dumps(legacy_profile['file'])+'\n')
with (out/'qol_bootstrap.inc').open('a') as f:f.write(resource_header('qol_resources',resource_catalogs['qol']))
(out/'resource-catalog.json').write_text(json.dumps(resource_catalogs,indent=2)+'\n')
capture_reference_sha256=None
if a.capture_patches:
    reference_path=R/'generated/qol-initialization/executable-capture-no-sse2.json';reference=json.loads(reference_path.read_text());capture_reference_sha256=sha(reference_path)
    for name,path in [('engine',R/'ref/control/Tribes2.exe'),('qol',work/'IFC22.dll')]:assert sha(path)==reference['inputs_sha256'][name]
    # The patcher also requests RWX for a reviewed function-pointer data page.
    # Admit only image pages observed in this pinned reference, still as native
    # nonexecuting data, in addition to the executable image sections.
    for event in reference['service_calls']:
        if event['name']!='VirtualProtect' or event['value']['new_protection'] not in [0x20,0x40]:continue
        value=event['value']
        if value['reservation'] not in ['engine','qol']:continue
        for page in value['pages']:
            if not any(lo<=page<lo+size for lo,size in patch_ranges):patch_ranges.append([page,4096])
if a.publish_patches:
    for page in patch_inputs['late_patch']['extra_image_pages']+patch_inputs['library_patch']['extra_image_pages']+patch_inputs['followup_patch']['extra_image_pages']+patch_inputs['network_patch']['extra_image_pages']+patch_inputs['http_patch']['extra_image_pages']+patch_inputs['string_patch']['extra_image_pages']+patch_inputs['renderer_batch']['extra_image_pages']+patch_inputs['renderer_nested']['extra_image_pages']+patch_inputs['audio_tables']['extra_image_pages']:
        assert page%4096==0 and any(lo<=page and page+4096<=hi for lo,hi in image_bounds)
        if not any(lo<=page<lo+size for lo,size in patch_ranges):patch_ranges.append([page,4096])
if startup_input:patch_ranges.append([startup_input['image_page'],4096])
if a.capture_patches:
    with (out/'qol_bootstrap.inc').open('a') as f:f.write('#define TB_PATCH_ADDRESS 0x3c0000u\n#define TB_ENGINE_BYTES %du\n#define TB_QOL_BYTES %du\nstatic const uint32_t patch_image_ranges[][2]={%s};\n'%(image_sizes['engine'],image_sizes['qol'],','.join('{%du,%du}'%tuple(row) for row in patch_ranges)))
from curl_backend_inputs import prepare as prepare_curl
curl_sources,curl_flags,curl_libraries,curl_bindings,curl_metadata=prepare_curl(out)
from sdl_backend_inputs import prepare as prepare_sdl
sdl_sources,sdl_flags,sdl_libraries,sdl_bindings,sdl_metadata=prepare_sdl(out)
bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text());bindings.update(curl_bindings);bindings.update(sdl_bindings);missing=[];ir=['declare void @qol_missing_import(ptr, i32) noreturn']
seen_imports=set()
linked_import_symbols={r['symbol'] for r in patch_inputs['linked_imports']} if a.publish_patches else set()
for i,row in enumerate(all_imports):
    if row['symbol'] in seen_imports:continue
    seen_imports.add(row['symbol'])
    if row['name'] in bindings or (a.publish_patches and (row['name']=='FlushInstructionCache' or row['symbol'] in linked_import_symbols)):continue
    missing.append(row['name']);ir.append('define hidden fastcc void @%s(ptr %%cpu) { call void @qol_missing_import(ptr %%cpu, i32 %d)\n unreachable\n}'%(row['symbol'],i))
(out/'stops.ll').write_text('\n'.join(ir)+'\n')
build=R/'generated/native-win32/macos';buildmeta=json.loads((build/'result.json').read_text());assert buildmeta['sse_header_sha256']==sha(R/'port/llasm-support/tribes_sse.h');objects=[]
assert buildmeta['bindings_sha256']==sha(R/'port/llasm-runtime/win32-bindings.json')
for name,h in buildmeta['cpu_metadata_sha256'].items():assert sha(R/name)==h,name
for name,h in buildmeta['system_messages_sha256'].items():assert sha(R/name)==h,name
for name,h in buildmeta['ntstatus_sha256'].items():assert sha(R/name)==h,name
for name,h in buildmeta['system_providers_sha256'].items():assert sha(R/name)==h,name
checked_runtime=None
if a.checked_runtime:
    checked_runtime=json.loads((R/'generated/native-win32/checked-macos/result.json').read_text())
    assert checked_runtime['state']=='finished' and not checked_runtime['failures'] and checked_runtime['source_build_sha256']==sha(build/'result.json')
    assert set(checked_runtime['objects'])==set(buildmeta['objects'])
runtime_sources=[]
for name,obj in buildmeta['objects'].items():
    candidates=[R/'port/core'/(name+'.c'),R/'port/llasm-support'/(name+'.c'),R/'port/apple'/(name+'.m'),build/(name+'.ll')]
    actual=[f for f in candidates if f.exists()];assert len(actual)==1 and sha(actual[0])==obj['source_sha256'],name
    path=build/(name+'.o');assert sha(path)==obj['object_sha256'],name
    if name!='bindings' and not (a.probe_ntdll and name=='native_system_providers'):
        if a.checked_runtime:
            row=checked_runtime['objects'][name];assert row['source_sha256']==obj['source_sha256'],name
            path=R/'generated/native-win32/checked-macos'/(name+'.o');assert sha(path)==row['object_sha256'],name
        objects.append(str(path));runtime_sources.append(actual[0])
# Observe the actual service's result without replacing its implementation.
# The standard wrapper itself is independently exercised by the ABI test.
observed=out/'observed-bindings.ll';wrapper=(build/'bindings.ll').read_text();needle=' %result = call i32 @tribes_virtual_protect(i64 %base, i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3)'
assert wrapper.count(needle)==1
wrapper=wrapper.replace(needle,needle+'\n call void @qol_protect_result(i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3, i32 %result)\n call void @qol_protect_checkpoint(ptr %cpu, i32 %arg0, i32 %arg1, i32 %arg2, i32 %result)')
allocation=' %result = call i32 @tribes_virtual_alloc(i64 %base, i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3)'
assert wrapper.count(allocation)==1
wrapper=wrapper.replace(allocation,' call void @qol_alloc_request(ptr %cpu, i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3)\n'+allocation+'\n call void @qol_alloc_result(i32 %arg0, i32 %arg1, i32 %arg3, i32 %result)')
for operation,(helper,context) in enumerate([('tribes_load_library_cpu','ptr %cpu'),('tribes_get_module_handle','i64 %base'),('tribes_free_library_cpu','ptr %cpu')]):
    load_call=' %result = call i32 @'+helper+'('+context+', i32 %arg0)'
    assert wrapper.count(load_call)==1
    wrapper=wrapper.replace(load_call,load_call+'\n call void @qol_module_load_result(ptr %cpu, i32 %arg0, i32 %result, i32 '+str(operation)+')')
adapter=' %result = call i32 @tribes_get_adapters_addresses(i64 %base, i32 %arg0, i32 %arg1, i32 %arg2, i32 %arg3, i32 %arg4)'
assert wrapper.count(adapter)==1
wrapper=wrapper.replace(adapter,' call void @qol_adapter_request(ptr %cpu)\n'+adapter+'\n call void @qol_adapter_result(ptr %cpu, i32 %arg3, i32 %arg4, i32 %result)')
wrapper='declare void @qol_adapter_request(ptr)\ndeclare void @qol_adapter_result(ptr, i32, i32, i32)\n'+wrapper
wrapper='declare void @qol_module_load_result(ptr, i32, i32, i32)\n'+wrapper
for op,(helper,count,aux) in enumerate([('tribes_reg_open_key_ex_w',5,'i32 %arg4'),('tribes_reg_query_value_ex_w',6,'i32 %arg1'),('tribes_reg_close_key',1,'i32 0')]):
    call=f' %result = call i32 @{helper}(i64 %base'+''.join(f', i32 %arg{i}' for i in range(count))+')'
    assert wrapper.count(call)==1
    wrapper=wrapper.replace(call,f'{call}\n call void @qol_registry_result(ptr %cpu, i32 {op}, i32 %arg0, {aux}, i32 %result)')
wrapper='declare void @qol_registry_result(ptr, i32, i32, i32, i32)\n'+wrapper
for op,(helper,count,has_base) in enumerate([('tribes_set_priority_class',2,False),('tribes_get_process_affinity_mask',3,True),('tribes_set_thread_affinity_mask',2,False)]):
    args=(['i64 %base'] if has_base else [])+[f'i32 %arg{i}' for i in range(count)]
    call=f' %result = call i32 @{helper}('+', '.join(args)+')'
    assert wrapper.count(call)==1
    value='i32 0' if op==1 else 'i32 %arg1'
    wrapper=wrapper.replace(call,f'{call}\n call void @qol_scheduling_result(ptr %cpu, i32 {op}, {value}, i32 %result)')
wrapper='declare void @qol_scheduling_result(ptr, i32, i32, i32)\n'+wrapper
for op,helper in enumerate(['tribes_monitor_from_window','tribes_get_monitor_info_a','tribes_get_monitor_info_w']):
    call=f' %result = call i32 @{helper}(i64 %base, i32 %arg0, i32 %arg1)'
    assert wrapper.count(call)==1
    wrapper=wrapper.replace(call,f'{call}\n call void @qol_monitor_result(ptr %cpu, i32 {op}, i32 %arg1, i32 %result)')
wrapper='declare void @qol_monitor_result(ptr, i32, i32, i32)\n'+wrapper


observed.write_text('declare void @qol_protect_checkpoint(ptr, i32, i32, i32, i32)\ndeclare void @qol_protect_result(i32, i32, i32, i32, i32)\ndeclare void @qol_alloc_request(ptr, i32, i32, i32, i32)\ndeclare void @qol_alloc_result(i32, i32, i32, i32)\n'+wrapper)
# Ordinary support objects come from the same runtime used by engine bootstrap.
sources=[R/p for p in ['port/llasm-support/tribes_absolute_memory.c','port/llasm-support/tribes_atomic.c','port/core/ext80.c','port/core/mmx.c','port/core/x87_status.c','port/core/guest_render_job.c','port/core/guest_zlib.c','port/core/guest_zlib_chunk.c','port/llasm-support/tribes_float80.c','port/llasm-support/tribes_mmx.c','port/llasm-support/tribes_scan.c','port/llasm-support/tribes_x87.c','tests/qol_native_bootstrap.c']]
sources += [R/'ref/halopad/port/llasm-support'/('llasm_'+name+'.c') for name in ['divide','pushx','movs','cmps','scas','stos']]
sources += [dispatch_source,out/'stops.ll',work/'native-zlib-routes/bindings.ll',observed]+published_sources
sources += curl_sources+sdl_sources
ntdll_probe=None
if a.probe_ntdll:
    from ntdll_probe_inputs import prepare as prepare_ntdll_probe
    probe_sources,ntdll_probe=prepare_ntdll_probe(out);sources+=probe_sources
if a.checked_image:sources += [R/'port/core/guest_memory_checks.c']
if a.guard_externals:sources += [R/'port/core/guest_external_checks.c']
zbuild=R/'generated/zlib-ng/macos/build';math=sse_inputs('macos');exe=out/'bootstrap'
if a.prepare_only:
    assert a.enter_engine and a.probe_ntdll, 'Mobile preparation requires the full engine source set'
    prepared=dict(state='prepared',sources=[str(p.relative_to(R)) for p in runtime_sources+sources],sources_sha256={str(p.relative_to(R)):sha(p) for p in runtime_sources+sources},directory=str(out.relative_to(R)),images={name:sha(out/name) for name in ['engine.bin','qol.bin']},scope='Verified original preparation only; no compilation or execution.')
    (out/'prepared-inputs.json').write_text(json.dumps(prepared,indent=2)+'\n')
    report_path.write_text(json.dumps(dict(state='prepared',diagnostic_only=True,playable=False,prepared_sha256=sha(out/'prepared-inputs.json')))+'\n')
    print('Prepared original full-engine sources:',out/'prepared-inputs.json');raise SystemExit(0)
support_build_sha256=None;link_sources=list(map(str,sources))
if a.checked_support:
    from checked_support import compile_support
    flags=['-std=c11','-DPTROFS_64BIT','-DTB_CHECKED_IMAGE']+(['-DTB_GUARD_EXTERNALS'] if a.guard_externals else [])+(['-DTB_CHECKED_SUBPAGES'] if a.checked_subpages else [])+(['-DTB_CAPTURE_PATCHES'] if a.capture_patches else [])+(['-DTB_PUBLISH_PATCHES'] if a.publish_patches else [])+['-I',str(out),'-I',str(zbuild),'-I',str(R/'ref/zlib-ng')]+math['compile_flags']
    link_sources,support_build_sha256=compile_support(sources,out/'support',flags+curl_flags+sdl_flags)
checked_math_sha256=None;math_entries=[]
if a.publish_patches:
    from checked_bochs_inputs import checked_bochs_inputs
    math_entries,checked_math_sha256=checked_bochs_inputs()
external_build_sha256=None
if a.guard_externals:
    from guard_checked_link import guard_link
    entries=[]
    for path in objects:
        path=pathlib.Path(path);row=checked_runtime['objects'][path.stem];bc=path.with_suffix('.bc');entries.append(dict(object=str(path),object_sha256=sha(path),bitcode=str(bc),bitcode_sha256=row['bitcode_sha256']))
    if not a.publish_patches:entries.append(dict(object=str(module),object_sha256=sha(module),bitcode=str(module.with_suffix('.bc')),bitcode_sha256=checked_meta['bitcode_sha256']))
    sm=json.loads((out/'support/result.json').read_text())
    for path in link_sources:
        path=pathlib.Path(path);row=sm['objects'][path.stem];entries.append(dict(object=str(path),object_sha256=row['object_sha256'],bitcode=None if row['trusted_barrier_runtime'] else str(path.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
    entries+=math_entries
    objects,external_build_sha256=guard_link(entries,out/'guarded');link_sources=[]
module_objects=[] if a.guard_externals else [str(module)]
cmd=['clang','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),'-target','arm64-apple-macos12','-O2','-std=c11','-Wno-override-module','-DPTROFS_64BIT','-I',str(out),'-I',str(zbuild),'-I',str(R/'ref/zlib-ng')]+(['-DTB_CHECKED_IMAGE'] if a.checked_image else [])+math['compile_flags']+objects+module_objects+link_sources+math['libraries']+([] if a.publish_patches else [str(R/'generated/bochs-math/libmath.dylib')])+[str(zbuild/'libz-ng.a'),'-framework','Foundation','-framework','AppKit','-framework','CoreGraphics','-framework','CoreText','-framework','CoreFoundation','-o',str(exe)]
cmd+=curl_flags+curl_libraries+sdl_flags+sdl_libraries
with (out/'link.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
volume=out/'volume';temp=volume/'TribesPad/Temp';temp.mkdir(parents=True,exist_ok=True)
providers=json.loads((R/'config/runtime/native-providers.json').read_text())['providers']
module_availability=audit_absent_module('dbghelp.dll',[game_root,volume],[p['name'] for p in providers]+['Tribes2.exe','IFC22.dll'])
optional_module_availability=audit_absent_module('discord_game_sdk.dll',[game_root,volume],[p['name'] for p in providers]+['Tribes2.exe','IFC22.dll']) if game_profile else None
legacy_audio_availability=audit_absent_module('OpenAL.dll',[game_root,volume],[p['name'] for p in providers]+['Tribes2.exe','IFC22.dll']) if game_profile else None
if a.capture_patches:
    for name in ['engine-after.bin','qol-after.bin','patch-after.bin']:
        previous=out/name
        if previous.exists():previous.unlink()
if a.capture_late_patch or a.verify_late_patch:
    for prefix in ['late','late-final']:
        for pattern in [prefix+'-cpu.bin',prefix+'-pages.json',prefix+'-page-*.bin']:
            for previous in out.glob(pattern):previous.unlink()
command=[str(exe),str(out/'engine.bin'),str(out/'qol.bin'),str(volume),str(game_root),str(temp)]+([str(out)] if a.capture_patches else [])+(['engine-protection-capture=%08x'%a.capture_protection if a.capture_protection is not None else 'engine-clock-patch-verify' if a.verify_clock_patch else 'engine-allocation-patch-verify' if a.verify_allocation_patch else 'engine-collision-patch-verify' if a.verify_collision_patch else 'engine-post-file-patch-verify' if a.verify_post_file_patch else 'engine-post-file-patch-capture' if a.capture_post_file_patch else 'engine-console-animation-verify' if a.verify_console_animation else 'engine-console-animation-capture' if a.capture_console_animation else 'engine-console-fade-verify' if a.verify_console_fade else 'engine-console-patch-capture' if a.capture_console_patch else 'engine-string-patch-verify' if a.verify_string_patch else 'engine-http-patch-verify' if a.verify_http_patch else 'engine-http-patch-capture' if a.capture_http_patch else 'engine-network-patch-verify' if a.verify_network_patch else 'engine-network-patch-capture' if a.capture_network_patch else 'engine-followup-prefix-verify' if a.verify_followup_prefix else 'engine-followup-patch-capture' if a.capture_followup_patch else 'engine-library-patch-verify' if a.verify_library_patch else 'engine-library-patch-capture' if a.capture_library_patch else 'engine-startup-patch-capture' if a.capture_startup_patch else 'engine-late-verify' if a.verify_late_patch else 'engine-late-capture' if a.capture_late_patch else 'engine'] if a.enter_engine else [])
timed_out=False
try:run=subprocess.run(command,capture_output=True,text=True,timeout=60)
except subprocess.TimeoutExpired as error:
    # subprocess.run has killed and waited for this timed-out child. Retain
    # partial output and a terminal report instead of leaving state=building.
    timed_out=True
    decode=lambda data:data.decode(errors='replace') if isinstance(data,bytes) else data or ''
    run=subprocess.CompletedProcess(command,-9,decode(error.stdout),decode(error.stderr))
(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr)
result=dict(diagnostic_stop=True,kind='execution_timeout',timeout_seconds=60) if timed_out else json.loads(run.stdout) if run.stdout else None
external_stop=re.search(r'^TribesPad: external memory contract stop: (.*)$',run.stderr,re.M)
if external_stop and not result:result=dict(diagnostic_stop=True,kind='external_contract',reason=external_stop[1])
virtual_memory_stop=re.search(r'^TribesPad: virtual memory contract unsupported: (.*)$',run.stderr,re.M)
if virtual_memory_stop and not result:result=dict(diagnostic_stop=True,kind='virtual_memory_contract',reason=virtual_memory_stop[1])
module_stop=re.search(r'^TribesPad: native library provider unavailable for (.*)$',run.stderr,re.M)
if module_stop and not result:result=dict(diagnostic_stop=True,kind='module_provider',name=module_stop[1])
memory_stop=re.search(r'^TribesPad: checked guest memory violation (.*)$',run.stderr,re.M)
if memory_stop and not result:result=dict(diagnostic_stop=True,kind='memory_violation',reason=memory_stop[1])
runtime_stop=re.search(r'^TribesPad: unsupported (.*)$',run.stderr,re.M)
if runtime_stop and not result:result=dict(diagnostic_stop=True,kind='runtime_contract',reason=runtime_stop[1])
if result and not run.stdout:
    returned=re.findall(r'^Returned original DLL stage (\d+) EAX=(\d+)$',run.stderr,re.M)
    if returned and int(returned[-1][0])==len(stages)-1:result['attach_return']=int(returned[-1][1]);result['stages_returned']=len(stages)
    result['engine_entered']='Entering original patched engine at 0x006fde60' in run.stderr
    result['native_patch_publication']=bool(re.search(r'^Published AOT flush 1 address=',run.stderr,re.M))
allocation_events=[dict(caller=int(m[1],16),address=int(m[2],16),size=int(m[3]),type=int(m[4],16),protection=int(m[5],16)) for m in re.finditer(r'VirtualAlloc caller=0x([0-9a-f]+) address=0x([0-9a-f]+) size=(\d+) type=0x([0-9a-f]+) protection=0x([0-9a-f]+)',run.stderr)]
protection_events=[dict(address=int(m[1],16),size=int(m[2]),protection=int(m[3],16),output=int(m[4],16),result=int(m[5]),last_error=int(m[6])) for m in re.finditer(r'VirtualProtect address=0x([0-9a-f]+) size=(\d+) protection=0x([0-9a-f]+) output=0x([0-9a-f]+) result=(\d+) last_error=(\d+)',run.stderr)]
attach_returned=bool(result and result.get('attach_return') and (result.get('stages_returned')==len(stages) or result.get('engine_entered')))
published=bool(result and result.get('native_patch_publication'))
initialized=attach_returned and (not a.capture_patches or (a.publish_patches and published))
if result and result.get('kind')=='import' and result.get('return_pc')==0:
    # A noreturn shutdown import may have no caller return slot. Preserve the
    # actual stop as unverified diagnostic evidence, never as completed exit.
    reached=all_imports[result['index']]
    assert reached['dll'].lower()=='kernel32.dll' and reached['name']=='ExitProcess',('unclassified zero-return import',reached)
    result['import']=reached
    result['original_iat_call_verified']=False
    result['import_path_diagnostic']='ExitProcess reached without a caller return address; call origin and process shutdown remain unverified.'
if result and result.get('kind')=='import' and result.get('return_pc')!=0:
    result['import']=all_imports[result['index']]
    # Validate the emitted return address against the original PE IAT call.
    pc=result['return_pc']
    candidates=[R/'ref/control/Tribes2.exe',work/'IFC22.dll',R/'ref/product/Mss32.dll']
    if patch_inputs:candidates += [R/m['config']['product_path'] for m in patch_inputs['codecs']]
    owners=[]
    for candidate in candidates:
        image=pefile.PE(str(candidate));base=image.OPTIONAL_HEADER.ImageBase
        if base<=pc<base+image.OPTIONAL_HEADER.SizeOfImage:owners.append((candidate,image))
    assert len(owners)==1,('unclassified original return PC',hex(pc))
    original_path,pe=owners[0];md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
    paths=[]
    for width in (6,5):
        decoded=list(md.disasm(pe.get_data(pc-width-pe.OPTIONAL_HEADER.ImageBase,width),pc-width))
        if len(decoded)!=1 or decoded[0].mnemonic!='call' or decoded[0].size!=width:continue
        operand=decoded[0].operands[0];path=dict(call_pc=pc-width,call_bytes=decoded[0].bytes.hex())
        if operand.type==capstone.x86.X86_OP_IMM:
            target=operand.imm;thunk=list(md.disasm(pe.get_data(target-pe.OPTIONAL_HEADER.ImageBase,6),target))
            if len(thunk)!=1 or thunk[0].mnemonic!='jmp' or thunk[0].size!=6:continue
            operand=thunk[0].operands[0];path.update(thunk_pc=target,thunk_bytes=thunk[0].bytes.hex())
        if operand.type!=capstone.x86.X86_OP_MEM or operand.mem.base or operand.mem.index:continue
        matches=[row for row in all_imports if row['slot']==operand.mem.disp and row['symbol']==result['import']['symbol']]
        if len(matches)==1:paths.append((matches[0],dict(path,iat_slot=operand.mem.disp)))
    if not paths:
        from register_import_path import verify as verify_register_import
        path=verify_register_import(pe,original_path.read_bytes(),result['import'],pc,result.get('registers',{}))
        if path:paths.append((result['import'],path))
    assert len(paths)==1,('unverified original import call',hex(pc),paths)
    result['import'],result['original_import_path']=paths[0];result['original_iat_call_verified']=True


report=dict(state='finished',diagnostic_only=True,playable=False,checked_image_only=a.checked_image and not a.checked_runtime,checked_image=a.checked_image,checked_runtime_objects=a.checked_runtime,checked_support_objects=a.checked_support,external_call_gates=a.guard_externals,link_coverage_complete=False,independent_subpages_enabled=a.checked_subpages,initialized=initialized,outcome='attach_succeeded' if initialized else 'attach_failed' if result and not result.get('diagnostic_stop') else 'diagnostic_stop',protection_events=protection_events,exit=run.returncode,result=result,stderr=run.stderr,unbound_imports=missing,object_sha256=sha(module),binary_sha256=sha(exe),preparation_sha256=sha(native/'result.json'),runtime_build_sha256=sha(build/'result.json'),source_sha256={str(f.relative_to(R)):sha(f) for f in sources+[pathlib.Path(__file__).resolve(),R/'config/runtime/environment.json',R/'port/llasm-support/tribes_absolute_memory.h']},scope='Original TLS/process attach executed as ARM64. A completed diagnostic process is not a successful DLL attach: initialized requires a nonzero DllMain return. Initial PE pages are native RW nonexecuting data, without original section permissions; VirtualProtect uses exact native data pages by default, or an explicitly selected synchronized software subpage map with nonexecuting native backing and guarded external calls. Executable patch publication remains unsupported. No Windows SEH/page parity, patch publication, initialized engine/menu, or gameplay acceptance.')
if a.checked_image and not a.publish_patches:report['checked_preparation_sha256']=sha(native/'checked/result.json')
report['ntdll_probe']=ntdll_probe
report['curl_backend']=curl_metadata
report['sdl_backend']=sdl_metadata
report['miles_attach_return']=int(re.search(r'Returned original Miles process attach EAX=(\d+)',run.stderr)[1]) if re.search(r'Returned original Miles process attach EAX=(\d+)',run.stderr) else None
report['register_import_validator_sha256']=sha(R/'scripts/register_import_path.py')
report['curl_events']=[dict(caller=int(m[1],16),operation=int(m[2]),result=int(m[3],16)) for m in re.finditer(r'^Observed curl caller=0x([0-9a-f]+) operation=(\d+) result=0x([0-9a-f]+)$',run.stderr,re.M)]
if result and result.get('kind')=='ntdll_probe':
    assert ntdll_probe
    result['api']=next(p['name'] for p in ntdll_probe['probes'] if p['index']==result['index'])
report['allocation_events']=allocation_events
report['module_availability']=module_availability
report['game_profile']=game_profile
report['optional_module_availability']=optional_module_availability
report['legacy_audio_availability']=legacy_audio_availability
report['module_events']=[dict(operation=m[1],caller=int(m[2],16),name=m[3],result=int(m[4],16),last_error=int(m[5])) for m in re.finditer(r'^Observed (LoadLibraryA|GetModuleHandleA|FreeLibrary) caller=0x([0-9a-f]+) name=(.*?) result=0x([0-9a-f]+) last_error=(\d+)$',run.stderr,re.M)]
report['patch_capture_only']=a.capture_patches and not a.publish_patches;report['native_patch_publication']=published
report['patch_inputs']=patch_inputs
report['apple_legacy_gl_profile']=legacy_gl_profile
report['checked_math_build_sha256']=checked_math_sha256
report['engine_entered']=bool(result and result.get('engine_entered'));report['engine_entry_requested']=a.enter_engine
if report['engine_entered'] and result.get('diagnostic_stop'):report['outcome']='engine_diagnostic_stop'
if a.publish_patches:report['scope']='Checked combined patched engine, community DLL, finite trampoline bank and self-entry route. Exact executable bytes validate atomically before initial publication. Ordinary writes remain blocked; The first source-bound late revision mediates 23 exact writes and activates 27 compiled variants at its validated final flush. A second finite revision activates the input teardown variant after two exact original stores; a third activates25 compiled call routers after25 exact stores, with three separately validated version data updates. A fourth activates the observed104-write followup prefix after its final store, with two intermediate exact-byte flush checks; six new bank records remain unpublished. A fifth revision activates11 exact networking suffix writes after its last store; five additional bank records remain unpublished. A sixth revision admits17 chained HTTP stores including an overlapping displacement and an exact intermediate flush; its new bank record remains unpublished. Later mutations still stop. Unknown patch requests stop. Original pinned PEs remain guest data; the compiler-only candidate is never loaded. DLL initialization requires a nonzero actual attach return. Engine entry is reported separately from DLL initialization; no menu, map or authenticated match claim.'
report['attach_returned']=attach_returned;report['capture_reference_sha256']=capture_reference_sha256
if attach_returned and a.capture_patches and not a.publish_patches:report['outcome']='attach_returned_pending_patch_publication'
report['patch_snapshots']={f.name:dict(path=str(f.relative_to(R)),sha256=sha(f),bytes=f.stat().st_size) for f in out.glob('*-after.bin')} if a.capture_patches else {}
if a.guard_externals:report['external_build_sha256']=external_build_sha256
if a.checked_support:report['checked_support_build_sha256']=support_build_sha256
if a.checked_runtime:report['checked_runtime_build_sha256']=sha(R/'generated/native-win32/checked-macos/result.json')
report['late_patch_checkpoint_requested']=a.capture_late_patch or a.verify_late_patch
report['late_patch_verification_requested']=a.verify_late_patch
report['late_patch_publication']='Published late AOT revision after 23 exact writes' in run.stderr
report['late_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated late AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind') in ['late_patch_checkpoint','late_patch_native_return']:
    for key,prefix,pc in [('late_patch_checkpoint','late',0x10061347)]+([('late_patch_final','late-final',0x1006174d)] if result['kind']=='late_patch_native_return' else []):
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc
        if (prefix=='late-final') or result['kind']=='late_patch_checkpoint':assert len(pages['ranges'])==result['ranges']
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['startup_input_data']=startup_input
report['input_data_update_validated']='Validated original nonexecuting input-provider data update' in run.stderr
report['input_code_publication']='Published input teardown AOT variant after both original stores' in run.stderr
report['input_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4])) for m in re.finditer(r'Validated input AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+)',run.stderr)]
report['startup_patch_capture_requested']=a.capture_startup_patch
report['library_patch_capture_requested']=a.capture_library_patch
report['adapter_events']=[dict(caller=int(m[1],16),output=int(m[2],16),size=int(m[3]),result=int(m[4])) for m in re.finditer(r'^Observed GetAdaptersAddresses caller=0x([0-9a-f]+) output=0x([0-9a-f]+) size=(\d+) result=(\d+)$',run.stderr,re.M)]
report['cpu_metadata_profile']=dict(profile=json.loads((R/'config/runtime/cpu-metadata.json').read_text()),inputs_sha256=buildmeta['cpu_metadata_sha256'])
report['monitor_events']=[dict(operation=int(m[1]),caller=int(m[2],16),result=int(m[3],16),size=int(m[4]),width=int(m[5]),height=int(m[6]),work=[int(m[i]) for i in range(7,11)]) for m in re.finditer(r'^Observed monitor operation=(\d+) caller=0x([0-9a-f]+) result=0x([0-9a-f]+) size=(\d+) width=(\d+) height=(\d+) work=(\d+),(\d+),(\d+),(\d+)$',run.stderr,re.M)]
report['scheduling_events']=[dict(operation=int(m[1]),caller=int(m[2],16),value=int(m[3],16),result=int(m[4]),last_error=int(m[5])) for m in re.finditer(r'^Observed scheduling operation=(\d+) caller=0x([0-9a-f]+) value=0x([0-9a-f]+) result=(\d+) last_error=(\d+)$',run.stderr,re.M)]
report['registry_events']=[dict(operation=int(m[1]),caller=int(m[2],16),key=int(m[3],16),auxiliary=int(m[4],16),handle=int(m[5],16),result=int(m[6])) for m in re.finditer(r'^Observed registry operation=(\d+) caller=0x([0-9a-f]+) key=0x([0-9a-f]+) auxiliary=0x([0-9a-f]+) handle=0x([0-9a-f]+) result=(\d+)$',run.stderr,re.M)]
report['execution_timeout']=timed_out
report['http_patch_capture_requested']=a.capture_http_patch
# Native aborts can bypass the import/instruction diagnostic thread sample.
# Absence of that marker does not establish that no guest workers are alive.
report['concurrent_guest_threads']=True if 'Concurrent guest threads active: CPU/stack diagnostic only;' in run.stderr else None
report['coherent_memory_checkpoint_available']=False if report['concurrent_guest_threads'] else None
if result and result.get('kind')=='instruction' and result.get('engine_entered') and a.publish_patches and not report['concurrent_guest_threads']:
    prefix='instruction';pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==result['index'] and pages['cpu_bytes']==440
    names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
    report['instruction_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
    report['concurrent_guest_threads']=False
    report['coherent_memory_checkpoint_available']=True
if result and result.get('kind')=='import' and result.get('engine_entered') and a.publish_patches and not report['concurrent_guest_threads']:
    prefix='import';pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==result['import']['guest_va'] and pages['cpu_bytes']==440
    names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
    report['import_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
    report['concurrent_guest_threads']=False
    report['coherent_memory_checkpoint_available']=True
report['protection_capture_address']=a.capture_protection
if result and result.get('kind')=='protection_checkpoint':
    assert a.capture_protection==result['address'] and result['engine_entered']
    prefix='protection';pages=json.loads((out/(prefix+'-pages.json')).read_text())
    assert pages['resume_pc']==result['resume_pc'] and pages['cpu_bytes']==440 and len(pages['ranges'])==result['ranges']
    names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
    report['protection_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['post_file_patch_verification_requested']=a.verify_post_file_patch
report['post_file_patch_publication']='Published post-file CALL pc=' in run.stderr
report['post_file_patch_writes']=[dict(pc=int(m[1],16),address=int(m[2],16),size=int(m[3]),value=int(m[4],16)) for m in re.finditer(r'Published post-file CALL pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='post_file_patch_native_return':
    assert a.verify_post_file_patch and report['post_file_patch_publication'] and result['resume_pc']==0x10015d15
    for key,prefix,pc in [('post_file_patch_checkpoint','post-file-patch',0x10015d0a),('post_file_patch_final','post-file-final',0x10015d15)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc and pages['cpu_bytes']==440
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['collision_patch_verification_requested']=a.verify_collision_patch
report['collision_patch_publication']='Published collision CALL pc=' in run.stderr
report['collision_patch_writes']=[dict(pc=int(m[1],16),address=int(m[2],16),size=int(m[3]),value=int(m[4],16)) for m in re.finditer(r'Published collision CALL pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='collision_patch_native_return':
    assert a.verify_collision_patch and report['collision_patch_publication'] and result['resume_pc']==0x1000c3a7
    for key,prefix,pc in [('collision_patch_checkpoint','collision-patch',0x1000c3a1),('collision_patch_final','collision-final',0x1000c3a7)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc and pages['cpu_bytes']==440
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['allocation_patch_verification_requested']=a.verify_allocation_patch
report['allocation_patch_publication']='Published allocation SIZE pc=' in run.stderr
report['allocation_patch_writes']=[dict(pc=int(m[1],16),address=int(m[2],16),size=int(m[3]),value=int(m[4],16)) for m in re.finditer(r'Published allocation SIZE pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='allocation_patch_native_return':
    assert a.verify_allocation_patch and report['allocation_patch_publication'] and result['resume_pc']==0x1000c43d
    for key,prefix,pc in [('allocation_patch_checkpoint','allocation-patch',0x1000c42f),('allocation_patch_final','allocation-final',0x1000c43d)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc and pages['cpu_bytes']==440
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['post_file_patch_capture_requested']=a.capture_post_file_patch
if result and result.get('kind')=='post_file_patch_checkpoint':
    assert a.capture_post_file_patch and result['resume_pc']==0x10015d0a
    prefix='post-file-patch';pages=json.loads((out/(prefix+'-pages.json')).read_text())
    assert pages['resume_pc']==result['resume_pc'] and pages['cpu_bytes']==440 and len(pages['ranges'])==result['ranges']
    names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
    report['post_file_patch_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['clock_patch_verification_requested']=a.verify_clock_patch
report['clock_patch_publication']='Published clock patch after two exact CALL stores' in run.stderr
report['clock_patch_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated clock patch write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='clock_patch_native_return':
    assert a.verify_clock_patch and report['clock_patch_publication'] and result['resume_pc']==0x1006086e
    for key,prefix,pc in [('clock_patch_checkpoint','clock-patch',0x10060806),('clock_patch_final','clock-final',0x1006086e)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc and pages['cpu_bytes']==440
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['console_animation_verification_requested']=a.verify_console_animation
report['console_animation_publication']='Published console animation after two exact CALL stores' in run.stderr
report['console_animation_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated console animation write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='console_animation_native_return':
    assert a.verify_console_animation and report['console_animation_publication'] and result['resume_pc']==0x10002c23
    for key,prefix,pc in [('console_animation_checkpoint','console-animation',0x10002bc0),('console_animation_final','console-animation-final',0x10002c23)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc and pages['cpu_bytes']==440
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['console_animation_capture_requested']=a.capture_console_animation
if result and result.get('kind')=='console_animation_checkpoint':
    assert a.capture_console_animation and result['resume_pc']==0x10002bc0
    prefix='console-animation';pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==0x10002bc0 and pages['cpu_bytes']==440
    names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
    report['console_animation_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['console_fade_verification_requested']=a.verify_console_fade
report['console_fade_publication']='Published console fade CALL pc=0x10002936 address=0x005d2860 bytes=4 value=0xfa2fcec' in run.stderr
if result and result.get('kind')=='console_fade_native_return':
    assert a.verify_console_fade and report['console_fade_publication'] and result['resume_pc']==0x10002938
    for key,prefix,pc in [('console_patch_checkpoint','console-patch',0x1000292d),('console_fade_final','console-fade-final',0x10002938)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc and pages['cpu_bytes']==440
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[r['file'] for r in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['console_patch_capture_requested']=a.capture_console_patch
if result and result.get('kind')=='console_patch_checkpoint':
    assert a.capture_console_patch and result['resume_pc']==0x1000292d
    prefix='console-patch';pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==0x1000292d and pages['cpu_bytes']==440
    names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
    report['console_patch_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}

report['string_patch_verification_requested']=a.verify_string_patch
report['string_patch_publication']='Published string phase after 114 exact writes and 12 flushes' in run.stderr
report['string_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated string AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='string_patch_native_return':
    assert a.verify_string_patch and report['string_patch_publication']
    for key,prefix,pc in [('string_patch_checkpoint','string-patch',0x10061031),('string_patch_final','string-final',0x10061036)]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['http_patch_verification_requested']=a.verify_http_patch
report['http_patch_publication']='Published HTTP phase after 17 exact writes' in run.stderr
report['http_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated HTTP AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind') in ['http_patch_checkpoint','http_patch_native_return']:
    assert a.capture_http_patch or a.verify_http_patch
    for key,prefix,pc in [('http_patch_checkpoint','http-patch',0x10023c80)]+([('http_patch_final','http-final',0x10061031)] if a.verify_http_patch else []):
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['network_patch_capture_requested']=a.capture_network_patch
report['network_patch_verification_requested']=a.verify_network_patch
report['network_patch_publication']='Published network suffix after 11 exact writes' in run.stderr
report['network_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated network AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind') in ['network_patch_checkpoint','network_patch_native_return']:
    assert a.capture_network_patch or a.verify_network_patch
    for key,prefix,pc in [('network_patch_checkpoint','network-patch',0x100203f6)]+([('network_patch_final','network-final',0x1006102c)] if a.verify_network_patch else []):
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['followup_patch_capture_requested']=a.capture_followup_patch
if result and result.get('kind')=='followup_patch_checkpoint':
    assert a.capture_followup_patch and result['resume_pc']==0x10027e5e
    pages=json.loads((out/'followup-patch-pages.json').read_text());assert pages['resume_pc']==result['resume_pc'] and len(pages['ranges'])==result['ranges']
    names=['followup-patch-cpu.bin','followup-patch-pages.json']+[row['file'] for row in pages['ranges']]
    report['followup_patch_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}

report['followup_prefix_verification_requested']=a.verify_followup_prefix
report['followup_prefix_publication']='Published followup prefix after 104 exact writes' in run.stderr
report['followup_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated followup AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind')=='followup_prefix_native_boundary':
    assert a.verify_followup_prefix and report['followup_prefix_publication']
    for key,prefix,pc in [('followup_patch_checkpoint','followup-patch',0x10027e5e),('followup_prefix_final','followup-final',result['resume_pc'])]:
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report['library_patch_publication']='Published library AOT revision after 25 exact writes' in run.stderr
report['library_code_writes']=[dict(index=int(m[1]),pc=int(m[2],16),address=int(m[3],16),size=int(m[4]),value=int(m[5],16)) for m in re.finditer(r'Validated library AOT write (\d+) pc=0x([0-9a-f]+) address=0x([0-9a-f]+) bytes=(\d+) value=0x([0-9a-f]+)',run.stderr)]
if result and result.get('kind') in ['library_patch_checkpoint','library_patch_native_return']:
    for key,prefix,pc in [('library_patch_checkpoint','library-patch',0x10012cf6)]+([('library_patch_final','library-final',0x10061027)] if result['kind']=='library_patch_native_return' else []):
        pages=json.loads((out/(prefix+'-pages.json')).read_text());assert pages['resume_pc']==pc
        names=[prefix+'-cpu.bin',prefix+'-pages.json']+[row['file'] for row in pages['ranges']]
        report[key]={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
if result and result.get('kind')=='startup_patch_checkpoint':
    assert a.capture_startup_patch and result['resume_pc']==0x10060c9a
    pages=json.loads((out/'startup-patch-pages.json').read_text());assert pages['resume_pc']==result['resume_pc'] and len(pages['ranges'])==result['ranges']
    names=['startup-patch-cpu.bin','startup-patch-pages.json']+[row['file'] for row in pages['ranges']]
    report['startup_patch_checkpoint']={name:dict(path=str((out/name).relative_to(R)),sha256=sha(out/name),bytes=(out/name).stat().st_size) for name in names}
report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ['source_sha256','unbound_imports']},indent=2))
assert (run.returncode in [0,71] or (run.returncode==-6 and (external_stop or virtual_memory_stop or module_stop or runtime_stop)) or (timed_out and run.returncode==-9 and result and result.get('kind')=='execution_timeout')) and result,'Unexpected native failure: see diagnostics'
