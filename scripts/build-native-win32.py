#!/usr/bin/env python3
"""Compile implemented native Win32 services and original-address wrappers."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--platform',choices=['macos','device','simulator'],default='macos');a=ap.parse_args()
profiles={'macos':('macosx','arm64-apple-macos12'),'device':('iphoneos','arm64-apple-ios16'),'simulator':('iphonesimulator','arm64-apple-ios16-simulator')};sdk,target=profiles[a.platform];sdkpath=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
out=R/'generated/native-win32'/a.platform;out.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);bindings_path=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(module.generate(json.loads(bindings_path.read_text())))
subprocess.run(['python3',str(R/'scripts/gen-cpu-metadata.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-native-system-providers.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-system-messages.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-ntstatus-table.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-oem-environment-table.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-codepage-table.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-classification.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-casing.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-composition.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-collation.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-locales.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-datetime.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-nls-info.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-locale-identifiers.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-cursor-theme.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-keyboard-layout.py'),'--check'],check=True)
math=sse_inputs(a.platform)
sources=[R/'port/core'/(name+'.c') for name in ['guest_adapters','guest_avi','guest_bcrypt','guest_console','guest_console_callback','guest_locale_identifiers','guest_locale_info','guest_datetime','guest_locales','guest_locale_callback','guest_collation','guest_once','guest_once_callback','guest_address_space','guest_sdl_memory','guest_sdl_allocator','guest_teb','guest_thread_ids','guest_threads','guest_srw','guest_condition','guest_virtual_memory','guest_system','guest_system_query','guest_registry','guest_scheduling','guest_slist','guest_encoding','native_fls','guest_fls','process_heap','guest_file','guest_find','guest_display','guest_cursors','guest_icons','keyboard_state','keyboard_translation','input_buffer','guest_dinput','native_exports','native_system_providers','guest_version','guest_cpu','guest_processor_features','critical_section','guest_heap','exception_filter','guest_raise_exception','guest_strings','guest_messages','guest_user_messages','guest_ntstatus','native_handles','guest_profile','process_environment','process_startup','guest_modules','guest_loader','gdi_memory','window_classes','guest_callback','hidden_windows','guest_window_catalog','native_winsock','guest_tls','performance_clock','multimedia_timer','guest_filesystem']]+[R/'port/apple/display.m',R/'port/apple/font.m',R/'port/core/guest_dispatch.c',R/'port/llasm-support/tribes_result.c',R/'port/llasm-support/tribes_sse.c',R/'port/llasm-support/tribes_quadword_conversion.c',R/'port/core/quadword_conversion.c',R/'port/core/sse_arithmetic.c',R/'port/llasm-support/tribes_shift.c',R/'port/core/shift.c',wrapper];objects={}
for src in sources:
 obj=out/(src.stem+'.o');cmd=['clang','-target',target,'-isysroot',sdkpath,'-O2','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror','-c',str(src),'-o',str(obj)]
 if src.name=='sse_arithmetic.c':cmd+=math['compile_flags']
 if src.suffix=='.m':cmd.insert(1,'-fobjc-arc')
 subprocess.run(cmd,check=True,capture_output=True);objects[src.stem]={'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest()}
report={'cpu_metadata_sha256':{name:hashlib.sha256((R/name).read_bytes()).hexdigest() for name in ['config/runtime/cpu-metadata.json','port/core/cpu_metadata_profile.h','scripts/gen-cpu-metadata.py','ref/downloads/libuv-cpu-reference/darwin.c']},'ntstatus_sha256':{name:hashlib.sha256((R/name).read_bytes()).hexdigest() for name in ['port/core/guest_ntstatus.h', 'port/core/ntstatus_table.inc', 'port/core/guest_teb.h', 'config/runtime/ntstatus-sources.json', 'scripts/gen-ntstatus-table.py']},'system_messages_sha256':{name:hashlib.sha256((R/name).read_bytes()).hexdigest() for name in ['port/core/guest_messages.h','port/core/system_messages.inc','config/runtime/system-messages.json','scripts/gen-system-messages.py']},'system_providers_sha256':{name:hashlib.sha256((R/name).read_bytes()).hexdigest() for name in ['config/runtime/native-providers.json','port/core/native_system_providers.inc','port/core/native_system_providers.h','scripts/gen-native-system-providers.py']},'sse_math':math['evidence'],'cpu_header_sha256':hashlib.sha256((R/'port/llasm-support/llasm_cpu.h').read_bytes()).hexdigest(),'sse_header_sha256':hashlib.sha256((R/'port/llasm-support/tribes_sse.h').read_bytes()).hexdigest(),'target':target,'sdk':sdkpath,'objects':objects,'bindings_sha256':hashlib.sha256(bindings_path.read_bytes()).hexdigest(),'run_on_device':False,'whole_game_linked':False};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
