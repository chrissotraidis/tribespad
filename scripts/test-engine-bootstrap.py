#!/usr/bin/env python3
"""Link the entire AOT engine with explicit diagnostic stops, compare first entry."""
import argparse,hashlib,importlib.util,json,os,pathlib,re,stat,struct,subprocess
import pefile,unicorn,capstone
from capstone import x86_const as CX
from unicorn import x86_const as X
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('lift',type=pathlib.Path);ap.add_argument('--instruction-budget',type=int,default=1000000000);ap.add_argument('--simulator-device');a=ap.parse_args();work=a.lift.resolve();va=work/'va';object_dir=va/'simulator' if a.simulator_device else va;out=object_dir/('bootstrap-'+a.simulator_device if a.simulator_device else 'bootstrap');out.mkdir(exist_ok=True)
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));booted=[d for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert len(booted)==1 and booted[0]['udid']==a.simulator_device,'Only the requested simulator may be booted'
meta=json.loads((va/'result.json').read_text());compiled=json.loads((object_dir/'compile.json').read_text());assert meta['prepared'] and meta['engine_ll_sha256']==compiled['input_sha256'];assert compiled['exit']==0 and compiled['input_sha256']==hashlib.sha256((va/'engine.ll').read_bytes()).hexdigest()
assert compiled['object_sha256']==hashlib.sha256((object_dir/'engine.o').read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps({'first_entry_differential_pass':False,'state':'running','game_startup_completed':False})+'\n')
exe=work/'Tribes2.exe';assert hashlib.sha256(exe.read_bytes()).hexdigest()==meta['image_sha256'];image=pefile.PE(str(exe)).get_memory_mapped_image();(out/'image.bin').write_bytes(image);imports=meta['imports'];n=len(imports)
profile=json.loads((R/'config/runtime/environment.json').read_text());environment=b''.join((name+'='+value).encode(profile['encoding'])+b'\0' for name,value in sorted(profile['variables'].items(),key=lambda kv:kv[0].casefold()))+b'\0'
if environment==b'\0':environment=b'\0\0'
command_line=profile['command_line']['value'].encode(profile['command_line']['encoding'])+b'\0'
(out/'bootstrap_imports.h').write_text('#define TB_IMPORT_COUNT %d\n'%n+'static const uint32_t tb_import_slots[] = {'+','.join(str(row['slot']) for row in imports)+'};\nstatic const uint32_t tb_import_values[] = {'+','.join(str(row['guest_va']) for row in imports)+'};\nstatic const unsigned char tb_environment[]={'+','.join(str(b) for b in environment)+'};\nstatic const unsigned char tb_command_line[]={'+','.join(str(b) for b in command_line)+'};\n')
keyboard_profile=json.loads((R/'config/runtime/keyboard-layout.json').read_text());keyboard_calls=[];ascii_calls=[];icon_calls=[];cursor_calls=[];stock_calls=[];drive_string_calls=[];drive_type_calls=[];semaphore_calls=[];metric_calls=[];display_calls=[];display_dcs={};display_dc_next=0xd7000001
subprocess.run(['python3',str(R/'scripts/gen-cursor-theme.py'),'--check'],check=True)
subprocess.run(['python3',str(R/'scripts/gen-keyboard-layout.py'),'--check'],check=True)
keyboard_state_profile=json.loads((R/'config/runtime/keyboard-state.json').read_text());keyboard_initial=keyboard_state_profile['initial_virtual_keys'];assert len(keyboard_initial)==256;processed_keys=bytearray(keyboard_initial);key_state_calls=[]
with (out/'bootstrap_imports.h').open('a') as header:header.write('static const unsigned char tb_initial_keyboard_state[]={'+','.join(str(v) for v in keyboard_initial)+'};\n')
input_profile=json.loads((R/'config/runtime/input-devices.json').read_text());input_devices=input_profile['devices']
records=[];object_headers=[]
for device_index,device in enumerate(input_devices):
 objects=[]
 for obj in device["objects"]:
  guid=bytes.fromhex(obj["guid_hex"]);assert len(guid)==16
  objects.append("{"+",".join(["{"+",".join(str(b) for b in guid)+"}",str(obj["offset"]),str(obj["type"]),str(obj["flags"]),json.dumps(obj["name"]),"0","0"])+"}")
 object_headers.append("static const TribesInputObject tb_objects_%d[]={%s};\n"%(device_index,",".join(objects)))
 instance=bytes.fromhex(device['instance_guid_hex']);product=bytes.fromhex(device['product_guid_hex']);assert len(instance)==len(product)==16
 records.append('{'+','.join(['{'+','.join(str(b) for b in instance)+'}','{'+','.join(str(b) for b in product)+'}',str(device['type']),json.dumps(device['instance_name']),json.dumps(device['product_name']),str(int(device['attached'])),str(device['axes']),str(device['buttons']),str(device['povs']),'tb_objects_'+str(device_index),str(len(device['objects']))])+'}')
with (out/'bootstrap_imports.h').open('a') as header:header.write(''.join(object_headers));header.write('static const uint32_t tb_input_device_count=%d;\nstatic const TribesInputDevice tb_input_devices[]={%s};\n'%(len(records),','.join(records)))
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
bindings=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())
ir=['target triple = "arm64-apple-macos12"','declare void @tribes_missing_import(ptr, i32) noreturn','declare void @tribes_missing_cpu_helper(ptr, i32) noreturn']
for i,row in enumerate(imports):
 if row['name'] in bindings:continue
 ir.append('define hidden fastcc void @%s(ptr %%cpu) { call void @tribes_missing_import(ptr %%cpu, i32 %d)\n unreachable\n}'%(row['symbol'],i))
# CPUID has a fixed native compatibility profile; clock/string gaps still stop.
for i,(name,ret,args) in enumerate([('x86_cpuid','void','ptr %cpu'),('x86_rdtsc','void','ptr %cpu'),('x86_repe_cmpsw_prep','void','ptr %cpu')]):
 if name=='x86_cpuid':continue
 ir.append('define %s @%s(%s) { call void @tribes_missing_cpu_helper(ptr %%cpu, i32 %d)\n unreachable\n}'%(ret,name,args,i))
ir.append(module.generate(bindings))
(out/'stops.ll').write_text('\n'.join(ir)+'\n')
sources=['port/core/guest_memory_checks.c','port/core/guest_console.c','port/core/guest_console_callback.c','port/core/guest_locale_identifiers.c','port/core/guest_locale_info.c','port/core/guest_datetime.c','port/core/guest_locales.c','port/core/guest_locale_callback.c','port/core/guest_collation.c','port/core/guest_once.c','port/core/guest_once_callback.c','port/core/guest_address_space.c','port/core/guest_sdl_memory.c','port/core/guest_sdl_allocator.c','port/core/guest_srw.c','port/core/guest_condition.c','port/core/guest_virtual_memory.c','port/core/guest_system.c','port/core/guest_slist.c','port/core/guest_encoding.c','port/core/native_fls.c','port/core/guest_fls.c','port/core/process_heap.c','port/core/guest_file.c','port/core/guest_find.c','port/core/guest_display.c','port/core/guest_cursors.c','port/core/guest_icons.c','port/core/keyboard_state.c','port/core/keyboard_translation.c','port/core/input_buffer.c','port/core/guest_dinput.c','port/core/native_exports.c','port/core/native_system_providers.c','port/core/guest_version.c','port/core/guest_cpu.c','port/core/guest_processor_features.c','port/core/guest_thread_ids.c','port/core/guest_dispatch.c','port/core/exception_filter.c','port/core/guest_messages.c','port/core/guest_ntstatus.c','port/core/critical_section.c','port/core/guest_heap.c','port/core/native_handles.c','port/core/process_environment.c','port/core/process_startup.c','port/core/guest_modules.c','port/core/gdi_memory.c','port/core/window_classes.c','port/core/guest_callback.c','port/core/hidden_windows.c','port/core/guest_window_catalog.c','port/core/native_winsock.c','port/core/guest_tls.c','port/core/performance_clock.c','port/core/guest_filesystem.c','port/core/ext80.c','port/core/sse_arithmetic.c','port/core/mmx.c','port/core/shift.c','port/core/x87_status.c','port/llasm-support/tribes_float80.c','port/llasm-support/tribes_result.c','port/llasm-support/tribes_mmx.c','port/llasm-support/tribes_sse.c','port/llasm-support/tribes_shift.c','port/llasm-support/tribes_scan.c','port/llasm-support/tribes_x87.c','tests/engine_bootstrap.c']
sources += ['ref/halopad/port/llasm-support/llasm_'+name+'.c' for name in ['divide','pushx','movs','cmps','scas','stos']]
platform_flags=['-arch','arm64']
math_objects=[str(R/'generated/bochs-math/libmath.dylib')];float_library=R/'generated/softfloat/macos/softfloat.a'
if a.simulator_device:
 sdk=subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip();platform_flags=['-target','arm64-apple-ios16-simulator','-isysroot',sdk];math_objects=[str(R/'generated/bochs-math/simulator/math_bridge.o'),str(R/'generated/bochs-math/simulator/libbochs-math.a'),'-lc++'];float_library=R/'generated/softfloat/simulator/softfloat.a'
sse_math=sse_inputs('simulator' if a.simulator_device else 'macos');assert str(float_library)==sse_math['libraries'][0]
apple_object=out/'apple-display.o';apple_frameworks=['-framework','Foundation','-framework','UIKit'] if a.simulator_device else ['-framework','Foundation','-framework','AppKit','-framework','CoreGraphics']
subprocess.run(['clang',*platform_flags,'-fobjc-arc','-Wall','-Wextra','-Werror','-c',str(R/'port/apple/display.m'),'-o',str(apple_object)],check=True)
cmd=['clang',*platform_flags,'-O2','-std=c11','-Wno-override-module','-DPTROFS_64BIT','-DTRIBESPAD_BRUSH_OBSERVER','-DTRIBESPAD_CURSOR_OBSERVER','-DTRIBESPAD_ICON_OBSERVER','-DTRIBESPAD_CLOCK_OBSERVER','-DTRIBESPAD_FIND_OBSERVER','-DTRIBESPAD_DISK_OBSERVER','-DTRIBESPAD_WINDOW_OBSERVER','-DTRIBESPAD_INPUT_OBSERVER','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(out),'-I',str(R/'ref/softfloat/SoftFloat-3e/source/include'),str(object_dir/'engine.o'),str(va/'dispatch.ll'),str(out/'stops.ll'),*[str(R/name) for name in sources],str(float_library),*math_objects,str(apple_object),*apple_frameworks,'-o',str(out/'bootstrap')]
with (out/'link.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=log,check=True)
volume=out/'volume';temp=volume/'TribesPad/Temp';temp.mkdir(parents=True,exist_ok=True);game=R/'ref/product';assert (game/'Tribes2.exe').is_file()
run=subprocess.run([*(['xcrun','simctl','spawn',a.simulator_device] if a.simulator_device else []),str(out/'bootstrap'),str(out/'image.bin'),str(out/'stack.bin'),str(volume),str(game),str(temp)],capture_output=True,text=True,timeout=60);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert run.returncode in [71,72,80],(run.returncode,run.stderr)
clock_samples=list(struct.iter_unpack('<IQ',(out/'stack.bin.clocks').read_bytes()));clock_next=0
find_raw=(out/'stack.bin.find').read_bytes();find_samples=[];find_next=0;find_searches={};find_handle_next=0xe3000001;find_evidence=[];file_open_calls=[];file_size_calls=[];file_positions={};file_seek_calls=[];file_read_calls=[]
position=0
while position<len(find_raw):
 fields=struct.unpack_from('<6I',find_raw,position);position+=24;size=fields[5];assert size in [0,320];data=find_raw[position:position+size];assert len(data)==size;position+=size;find_samples.append((fields,data))
assert position==len(find_raw)
def find_host(guest):
 text=guest.replace('\\','/');assert not text.startswith('//')
 if len(text)>1 and text[1]==':':assert text[0].lower()=='c';text=text[2:]
 parts=[] if text.startswith('/') else ['TribesPad','Game']
 for part in text.split('/'):
  if part in ['', '.']:continue
  if part=='..':
   if parts:parts.pop()
  else:parts.append(part)
 root=volume
 if len(parts)>=2 and parts[0].lower()=='tribespad' and parts[1].lower() in ['game','temp']:root=game if parts[1].lower()=='game' else temp;parts=parts[2:]
 host=root
 for part in parts:
  matches=[p for p in host.iterdir() if p.name.lower()==part.lower()];assert len(matches)==1;host=matches[0]
 return host

def find_names(directory,pattern):
 if pattern=='*.*':pattern='*'
 regex=re.escape(pattern).replace(r'\*','.*');patterns=[regex]
 if pattern.endswith('.*'):patterns.append(re.escape(pattern[:-2]).replace(r'\*','.*'))
 return sorted([name for name in ['.','..']+[p.name for p in find_host(directory).iterdir()] if any(re.fullmatch(p,name,re.IGNORECASE) for p in patterns)],key=lambda name:(name.lower(),name))

def validate_find_data(directory,name,data):
 assert len(data)==320 and data[44:304].split(b'\0',1)[0].decode('ascii')==name
 assert data[44+len(name):]==bytes(320-44-len(name)) # terminator, unused filename tail, no 8.3 alias/padding
 host=find_host(directory+'/'+name);st=host.stat();attributes=(16 if host.is_dir() else 32)|(0 if st.st_mode&0o200 else 1)|(2 if getattr(st,'st_flags',0)&0x8000 else 0)
 assert struct.unpack_from('<I',data)[0]==attributes
 creation,access,write=struct.unpack_from('<3Q',data,4);epoch=116444736000000000
 assert abs(creation-(epoch+int(st.st_birthtime*10000000)))<=4
 assert write==epoch+st.st_mtime_ns//100
 high,low,reserved0,reserved1=struct.unpack_from('<4I',data,28);assert reserved0==reserved1==0 and (high<<32|low)==(0 if host.is_dir() else st.st_size)
 find_evidence.append({'directory':directory,'name':name,'attributes':attributes,'bytes':high<<32|low,'creation_filetime':creation,'access_filetime':access,'write_filetime':write})
disk_samples=list(struct.iter_unpack('<9I',(out/'stack.bin.disks').read_bytes()));disk_next=0
brush_events=list(struct.iter_unpack('<4I',(out/'stack.bin.brushes').read_bytes()));assert brush_events==[(4,0xd6000004,0,0),(2,0xd6000002,0,0x808080)]
cursor_trace=(out/'stack.bin.cursors').read_bytes();assert len(cursor_trace)==32+32*32*5
cursor_fields=struct.unpack('<8I',cursor_trace[:32]);assert cursor_fields==(0,32512,0xd4000001,32,32,0,0,32512)
cursor_profile=json.loads((R/'config/runtime/cursor-theme.json').read_text());cursor_rgba=bytearray();cursor_mask=bytearray()
for glyph in ''.join(cursor_profile['rows']):
 color=255 if glyph=='W' else 0;cursor_rgba.extend([color,color,color,0 if glyph=='.' else 255]);cursor_mask.append(int(glyph=='.'))
assert cursor_trace[32:4128]==cursor_rgba and cursor_trace[4128:]==cursor_mask,'Native cursor pixels differ from explicit theme'
cursor_evidence={'native_pixels_match_software_theme':True,'rgba_sha256':hashlib.sha256(cursor_rgba).hexdigest(),'and_mask_sha256':hashlib.sha256(cursor_mask).hexdigest(),'hotspot':[0,0],'dimensions':[32,32],'actual_Windows_theme_bitmap_compared':False,'Apple_pointer_installed':False}
icon_trace=(out/'stack.bin.icons').read_bytes();assert len(icon_trace)==32+32*32*5
icon_fields=struct.unpack('<8I',icon_trace[:32]);assert icon_fields==(0x400000,107,0xd3000001,32,32,2,8,0)
pe=pefile.PE(str(exe));icon_entry=next(e for e in pe.DIRECTORY_ENTRY_RESOURCE.entries if e.id==3);icon_resource=next(e for e in icon_entry.directory.entries if e.id==2).directory.entries[0].data.struct
icon_data=pe.get_data(icon_resource.OffsetToData,icon_resource.Size);header=struct.unpack('<IiiHHIIiiII',icon_data[:40]);assert header[:6]==(40,32,64,1,8,0)
colors=header[9] or 256;pixel_offset=40+colors*4;expected_rgba=bytearray();expected_mask=bytearray()
for y in range(32):
 for x in range(32):
  index=icon_data[pixel_offset+(31-y)*32+x];blue,green,red,_=icon_data[40+index*4:44+index*4];mask=(icon_data[pixel_offset+1024+(31-y)*4+x//8]>>(7-x%8))&1;expected_rgba.extend([red,green,blue,0 if mask else 255]);expected_mask.append(mask)
assert icon_trace[32:4128]==expected_rgba and icon_trace[4128:]==expected_mask,'Native decoded original icon does not match independent PE resource decoding'
icon_evidence={'native_pixels_match_independent_resource_decode':True,'group_resource':107,'selected_resource':2,'width':32,'height':32,'bits_per_pixel':8,'rgba_sha256':hashlib.sha256(expected_rgba).hexdigest(),'and_mask_sha256':hashlib.sha256(expected_mask).hexdigest(),'original_payload_sha256':hashlib.sha256(icon_data).hexdigest(),'uses_alpha':False}
native=json.loads(run.stdout);hardware=native.get('helper_index');dinput_method=native.get('dinput_method')
device_methods=['QueryInterface', 'AddRef', 'Release', 'GetCapabilities', 'EnumObjects', 'GetProperty', 'SetProperty', 'Acquire', 'Unacquire', 'GetDeviceState', 'GetDeviceData', 'SetDataFormat', 'SetEventNotification', 'SetCooperativeLevel', 'GetObjectInfo', 'GetDeviceInfo', 'RunControlPanel', 'Initialize', 'CreateEffect', 'EnumEffects', 'GetEffectInfo', 'GetForceFeedbackState', 'SendForceFeedbackCommand', 'EnumCreatedEffectObjects', 'Escape', 'Poll', 'SendDeviceData']
if dinput_method is not None:row={'name':('IDirectInputDevice2A::'+device_methods[dinput_method-100]) if dinput_method>=100 else ('IDirectInputA::'+{3:'CreateDevice',4:'EnumDevices',5:'GetDeviceStatus',6:'RunControlPanel',8:'FindDevice'}[dinput_method]),'kind':'unimplemented native input method','method_index':dinput_method}
elif hardware is not None:row={'name':['CPUID','RDTSC','REP CMPSW'][hardware],'kind':'unimplemented hardware helper','helper_index':hardware}
else:row=imports[native['import_index']]
# Original x86 reference: FS base zero supplies only the exception-list head.
# This models the first-entry observable FS:0 access, not a complete Windows TEB.
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x400000,(len(image)+4095)&~4095);u.mem_write(0x400000,image);u.mem_map(0x100000,0x200000);u.mem_map(0,4096);u.mem_write(0,struct.pack('<I',0xffffffff));u.mem_map(0xfffd0000,0x30000)
for imp in imports:u.mem_write(imp['slot'],struct.pack('<I',imp['guest_va']))
u.mem_write(0x2ffefc,struct.pack('<I',0xfffff000));u.reg_write(X.UC_X86_REG_ESP,0x2ffefc);u.reg_write(X.UC_X86_REG_EFLAGS,0x202);u.reg_write(X.UC_X86_REG_FPCW,0x37f);u.reg_write(X.UC_X86_REG_FPSW,0);u.reg_write(X.UC_X86_REG_FPTAG,0xffff);u.reg_write(X.UC_X86_REG_MXCSR,0x1f80)
seen=[];filters=[];critical=set();calls=[];allocations=[];heap_next=0x30000000;heap_blocks={};environment_blocks=set();environment_calls=[];command_address=0;last_error=0;standard={0xfffffff6:0xe1000001,0xfffffff5:0xe1000002,0xfffffff4:0xe1000003};io_calls=[];tls={};tls_calls=[];gdi={};gdi_next=0xd1000001;gdi_calls=[];window_classes=[];window_model={};callback_frames=[];callback_events=[];hardware_seen=[];dinput_seen=[];dinput_objects={};module_references=0;module_calls=[];dinput_calls=[];input_events=[];device_calls=[];object_events=[];format_events=[];property_events=[]
version_profile=json.loads((R/'config/runtime/version.json').read_text());version_calls=[]
cpu_profile=json.loads((R/'config/runtime/cpu.json').read_text());cpuid_calls=[]
recent=[];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True;flag_cache={};flag_producer=None;instruction_count=0
flag_bits={'CF':1,'PF':4,'AF':16,'ZF':64,'SF':128,'DF':1024,'OF':2048}
def fpu_snapshot(uc):
 return tuple(uc.reg_read(reg) for reg in [X.UC_X86_REG_FPCW,X.UC_X86_REG_FPSW,X.UC_X86_REG_FPTAG]+[getattr(X,'UC_X86_REG_FP'+str(i)) for i in range(8)])
def start_window_callback(uc,frame):
 global flag_producer
 uc.context_restore(frame['context']);flag_producer=frame['flag_producer'];message,lp=frame['messages'][frame['stage']];top=frame['sp']-128
 uc.mem_write(top-20,struct.pack('<5I',0xfffff000,frame['handle'],message,0,lp));uc.reg_write(X.UC_X86_REG_ESP,top-20);uc.reg_write(X.UC_X86_REG_EIP,frame['proc'])
 callback_events.append((frame['proc'],frame['handle'],message,0,lp))
def start_input_callback(uc,frame):
 global flag_producer
 uc.context_restore(frame['context']);flag_producer=frame['flag_producer'];device=frame['devices'][frame['stage']];size=frame['size'];value=bytearray(size);struct.pack_into('<I',value,0,size);value[4:20]=bytes.fromhex(device['instance_guid_hex']);value[20:36]=bytes.fromhex(device['product_guid_hex']);struct.pack_into('<I',value,36,device['type'])
 for offset,key in [(40,'instance_name'),(300,'product_name')]:name=device[key].encode('ascii');value[offset:offset+len(name)]=name
 descriptor=frame['sp']-608;uc.mem_write(descriptor,bytes(value));uc.mem_write(descriptor-12,struct.pack('<3I',0xfffff000,descriptor,frame['user']));uc.reg_write(X.UC_X86_REG_ESP,descriptor-12);uc.reg_write(X.UC_X86_REG_EIP,frame['proc']);input_events.append((frame['proc'],frame['user'],descriptor,size,bytes(value)))
def start_object_callback(uc,frame):
 global flag_producer
 uc.context_restore(frame['context']);flag_producer=frame['flag_producer'];obj=frame['devices'][frame['stage']];size=frame['size'];value=bytearray(316);struct.pack_into('<I',value,0,size);value[4:20]=bytes.fromhex(obj['guid_hex']);struct.pack_into('<3I',value,20,obj['offset'],obj['type'],obj['flags']);name=obj['name'].encode('ascii');value[32:32+len(name)]=name
 descriptor=frame['sp']-352;uc.mem_write(descriptor,bytes(value));uc.mem_write(descriptor-12,struct.pack('<3I',0xfffff000,descriptor,frame['user']));uc.reg_write(X.UC_X86_REG_ESP,descriptor-12);uc.reg_write(X.UC_X86_REG_EIP,frame['proc']);object_events.append((frame['proc'],frame['user'],descriptor,size,bytes(value[:size])))
def finish_window_callback(uc):
 global flag_producer
 if callback_frames[-1].get('kind') in ['input','object']:
  frame=callback_frames[-1];assert uc.reg_read(X.UC_X86_REG_ESP)==frame['sp']-(352 if frame['kind']=='object' else 608);result=uc.reg_read(X.UC_X86_REG_EAX);fp=fpu_snapshot(uc);uc.context_restore(frame['context']);flag_producer=frame['flag_producer']
  for reg,value in zip([X.UC_X86_REG_FPCW,X.UC_X86_REG_FPSW,X.UC_X86_REG_FPTAG]+[getattr(X,'UC_X86_REG_FP'+str(i)) for i in range(8)],fp):uc.reg_write(reg,value)
  frame['context']=uc.context_save();frame['stage']+=1
  if result and frame['stage']<len(frame['devices']):(start_object_callback if frame['kind']=='object' else start_input_callback)(uc,frame)
  else:callback_frames.pop();dinput_objects[frame['root_object']]['refs']-=1;uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,frame['sp']+(20 if frame['kind']=='object' else 24));uc.reg_write(X.UC_X86_REG_EIP,frame['ret'])
  return
 frame=callback_frames[-1];result=uc.reg_read(X.UC_X86_REG_EAX);assert uc.reg_read(X.UC_X86_REG_ESP)==frame['sp']-128
 assert fpu_snapshot(uc)==frame['fpu'],'Current window-creation callbacks must preserve the observed FPU state'
 message=frame['messages'][frame['stage']][0]
 assert not (message==0x81 and result==0) and not (message==1 and result==0xffffffff),'Creation rejection needs a matching independently modeled cleanup path'
 frame['stage']+=1
 if frame['stage']<len(frame['messages']):start_window_callback(uc,frame)
 else:
  callback_frames.pop();uc.context_restore(frame['context']);flag_producer=frame['flag_producer'];uc.reg_write(X.UC_X86_REG_EAX,frame['handle']);uc.reg_write(X.UC_X86_REG_ESP,frame['sp']+52);uc.reg_write(X.UC_X86_REG_EIP,frame['ret'])
def stop_import(uc,address,size,user):
 global flag_producer,instruction_count,heap_next,module_references,display_dc_next
 if 0xfffd0000<=address<0xfffe0000:
  if address==0xfffd0000:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,instance,version,output,outer=struct.unpack("<5I",uc.mem_read(sp,20));assert instance and version in [0x300,0x500] and outer==0
   page=os.sysconf("SC_PAGE_SIZE");object=heap_next;heap_next+=page;heap_blocks[object]=page;uc.mem_map(object,page);uc.mem_write(object,struct.pack("<10I",object+4,*[0xfffd1000+16*i for i in range(9)]));uc.mem_write(output,struct.pack("<I",object));dinput_objects[object]={"refs":1,"version":version};dinput_calls.append({"name":"DirectInputCreateA","instance":instance,"version":version,"output":output,"object":object});allocations.append({"address":object,"requested":40,"span":page,"flags":0x40});uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+20);uc.reg_write(X.UC_X86_REG_EIP,ret);return
  if address==0xfffd1030:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,root,guid,outp,outer=struct.unpack('<5I',uc.mem_read(sp,20));assert root in dinput_objects and outer==0;id=bytes(uc.mem_read(guid,16));device=next(d for d in input_devices if bytes.fromhex(d['instance_guid_hex'])==id)
   page=os.sysconf('SC_PAGE_SIZE');object=heap_next;heap_next+=page;heap_blocks[object]=page;uc.mem_map(object,page);uc.mem_write(object,struct.pack('<28I',object+4,*[0xfffd2000+16*i for i in range(27)]));uc.mem_write(outp,struct.pack('<I',object));dinput_objects[root]['refs']+=1;dinput_objects[object]={'refs':1,'root':root,'device':device};device_calls.append({'name':'CreateDevice','root':root,'guid':id.hex(),'output':outp,'object':object});allocations.append({'address':object,'requested':112,'span':page,'flags':0x40});uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+20);uc.reg_write(X.UC_X86_REG_EIP,ret);return
  if address in [0xfffd2000,0xfffd2010,0xfffd2020,0xfffd2030,0xfffd20f0]:
   name={0xfffd2000:'QueryInterface',0xfffd2010:'AddRef',0xfffd2020:'Release',0xfffd2030:'GetCapabilities',0xfffd20f0:'GetDeviceInfo'}[address];count={'QueryInterface':3,'AddRef':1,'Release':1,'GetCapabilities':2,'GetDeviceInfo':2}[name];sp=uc.reg_read(X.UC_X86_REG_ESP);args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)));object=args[1];entry=dinput_objects[object];device=entry['device'];result=0
   if name=='QueryInterface':
    iid=bytes(uc.mem_read(args[2],16));assert iid.hex() in ['82e644592ec9cf11bfc7444553540000','80e644592ec9cf11bfc7444553540000','0000000000000000c000000000000046'];entry['refs']+=1;uc.mem_write(args[3],struct.pack('<I',object))
   elif name=='AddRef':entry['refs']+=1;result=entry['refs']
   elif name=='Release':
    entry['refs']-=1;result=entry['refs']
    if not result:uc.mem_unmap(object,heap_blocks.pop(object));dinput_objects.pop(object);dinput_objects[entry['root']]['refs']-=1
   elif name=='GetCapabilities':
    size=struct.unpack('<I',uc.mem_read(args[2],4))[0];assert size in [24,44];fields=[size,4|int(device['attached']),device['type'],device['axes'],device['buttons'],device['povs'],0,0,0,0,0];uc.mem_write(args[2],struct.pack('<11I',*fields)[:size])
   else:
    size=struct.unpack('<I',uc.mem_read(args[2],4))[0];assert size in [560,580];value=bytearray(size);struct.pack_into('<I',value,0,size);value[4:20]=bytes.fromhex(device['instance_guid_hex']);value[20:36]=bytes.fromhex(device['product_guid_hex']);struct.pack_into('<I',value,36,device['type'])
    for offset,key in [(40,'instance_name'),(300,'product_name')]:text=device[key].encode('ascii');value[offset:offset+len(text)]=text
    uc.mem_write(args[2],bytes(value))
   device_calls.append({'name':name,'args':list(args[1:]),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0]);return
  if address==0xfffd2060:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,object,prop,header=struct.unpack('<4I',uc.mem_read(sp,16));fields=struct.unpack('<5I',uc.mem_read(header,20));
   if prop not in [1,2]:
    assert dinput_method==106;dinput_seen.append(address);uc.emu_stop();return
   assert fields[:4]==(20,16,0,0);entry=dinput_objects[object]
   if prop==1:entry['buffer_size']=min(fields[4],4096);property_events.append((object,1,entry['buffer_size'],max(0,entry['buffer_size']-1)))
   else:assert fields[4] in [0,1];entry['axis_mode']=fields[4];property_events.append((object,2,fields[4],0))
   device_calls.append({'name':'SetProperty','args':[object,prop,header],'header':list(fields),'actual_value':entry['buffer_size'] if prop==1 else entry['axis_mode'],'result':0});uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+16);uc.reg_write(X.UC_X86_REG_EIP,ret);return
  if address==0xfffd20b0:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,object,fmt=struct.unpack('<3I',uc.mem_read(sp,12));fields=struct.unpack('<6I',uc.mem_read(fmt,24));assert fields[:2]==(24,16) and fields[2] in [1,2]
   rows=[struct.unpack('<4I',uc.mem_read(fields[5]+i*16,16)) for i in range(fields[4])];entry=dinput_objects[object];assert len(rows)==len(entry['device']['objects']) and all(bytes(uc.mem_read(row[0],16)).hex()==entry['device']['objects'][i]['guid_hex'] and row[2]==entry['device']['objects'][i]['type'] and not row[3] for i,row in enumerate(rows));mapping=[(i,row[1],1 if row[2]&12 else 4) for i,row in enumerate(rows)];assert all(offset+width<=fields[3] for _,offset,width in mapping) and len(set(offset for _,offset,_ in mapping))==len(mapping);entry['format']={'flags':fields[2],'size':fields[3],'mapping':mapping}
   format_events.append((object,fields[2],fields[3],len(rows),entry['format']['mapping']));device_calls.append({'name':'SetDataFormat','args':[object,fmt],'header':list(fields),'result':0});uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret);return
  if address==0xfffd2050:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,object,prop,header=struct.unpack('<4I',uc.mem_read(sp,16));fields=struct.unpack('<4I',uc.mem_read(header,16));assert prop==4 and fields[0:2]==(24,16) and fields[3]==2
   selected=next((item for item in dinput_objects[object]['device']['objects'] if item['type']==fields[2]),None);result=0x80070002
   if selected:
    cls=selected['type']&255;result=0x80004001
    if cls in [1,2]:result=0;uc.mem_write(header+16,struct.pack('<2I',0x80000000 if cls==1 else 0,0x7fffffff if cls==1 else 65535))
   device_calls.append({'name':'GetProperty','args':[object,prop,header],'header':list(fields),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+16);uc.reg_write(X.UC_X86_REG_EIP,ret);return
  if address==0xfffd2040:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,object,callback,context,flags=struct.unpack('<5I',uc.mem_read(sp,20));entry=dinput_objects[object];assert flags==0;selected=entry['device']['objects'];size=292 if dinput_objects[entry['root']]['version']<0x500 else 316
   if selected:
    entry['refs']+=1;frame={'root_object':object,'kind':'object','context':uc.context_save(),'flag_producer':flag_producer,'sp':sp,'ret':ret,'devices':selected,'size':size,'proc':callback,'user':context,'stage':0};callback_frames.append(frame);start_object_callback(uc,frame)
   else:uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+20);uc.reg_write(X.UC_X86_REG_EIP,ret)
   return
  if address==0xfffd1040:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,object,kind,callback,context,flags=struct.unpack('<6I',uc.mem_read(sp,24));assert object in dinput_objects and flags in [0,1]
   selected=[d for d in input_devices if (not kind or d['type']&255==kind) and (not flags or d['attached'])];size=560 if dinput_objects[object]['version']<0x500 else 580
   if selected:
    dinput_objects[object]['refs']+=1;frame={'root_object':object,'kind':'input','context':uc.context_save(),'flag_producer':flag_producer,'sp':sp,'ret':ret,'devices':selected,'size':size,'proc':callback,'user':context,'stage':0};callback_frames.append(frame);start_input_callback(uc,frame)
   else:uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+24);uc.reg_write(X.UC_X86_REG_EIP,ret)
   return
  assert address==(0xfffd2000+16*(dinput_method-100) if dinput_method>=100 else 0xfffd1000+16*dinput_method);dinput_seen.append(address);uc.emu_stop();return
 if address==0xfffff000 and callback_frames:
  finish_window_callback(uc);return
 if address<0xfffe0000:
  instruction_count+=1
  if address not in flag_cache:
   instruction=next(decoder.disasm(bytes(uc.mem_read(address,size)),address));effects=instruction.eflags
   produces=bool(effects and not instruction.mnemonic.startswith('f') and any(effects&getattr(CX,'X86_EFLAGS_'+kind+'_'+name,0) for kind in ['MODIFY','RESET','SET','UNDEFINED'] for name in flag_bits))
   flag_cache[address]=(instruction.mnemonic,instruction.op_str,effects,produces)
  mnemonic,operands,effects,produces=flag_cache[address]
  if mnemonic=='cpuid':
   leaf=uc.reg_read(X.UC_X86_REG_EAX);subleaf=uc.reg_read(X.UC_X86_REG_ECX)
   if leaf==0:
    vendor=cpu_profile['vendor'].encode('ascii');b,d,c=struct.unpack('<3I',vendor);values=[cpu_profile['maximum_basic_leaf'],b,c,d]
   elif leaf==0x80000000:values=[cpu_profile['maximum_extended_leaf'],0,0,0]
   elif leaf==0x80000001:values=[0,0,0,0]
   else:values=[cpu_profile['signature'],0,0,cpu_profile['leaf_1_edx']]
   cpuid_calls.append({'pc':hex(address),'leaf':hex(leaf),'subleaf':subleaf,'eax_ebx_ecx_edx':values})
   for reg,value in zip([X.UC_X86_REG_EAX,X.UC_X86_REG_EBX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX],values):uc.reg_write(reg,value)
   uc.reg_write(X.UC_X86_REG_EIP,address+size);return
  if hardware is not None and mnemonic=={1:'rdtsc',2:'rep cmpsw'}[hardware]:
   hardware_seen.append(address);uc.emu_stop();return
  # An intermediate startup boundary ends immediately after IMUL.
  # Its CF/OF are defined; SF/ZF/AF/PF are explicitly undefined (Intel SDM).
  if produces:flag_producer=(address,mnemonic,operands,effects)
  recent.append((address,size))
  if len(recent)>32:del recent[0]
 global last_error,command_address,clock_next,disk_next,gdi_next,find_next,find_handle_next
 if address>=0xfffe0000:
  imp=next(row for row in imports if row['guest_va']==address)
  if imp['name']=='SetUnhandledExceptionFilter':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,value=struct.unpack('<II',uc.mem_read(sp,8));uc.reg_write(X.UC_X86_REG_EAX,filters[-1] if filters else 0);filters.append(value);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='LoadCursorA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,module,name=struct.unpack('<3I',uc.mem_read(sp,12));assert module==0 and name==32512
   result=0xd4000001;cursor_calls.append({'module':module,'system_id':name,'handle':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='LoadIconA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,module,name=struct.unpack('<3I',uc.mem_read(sp,12));assert module==0x400000 and name==107
   result=0xd3000001;icon_calls.append({'module':module,'group_resource':name,'handle':result,'selected_icon_resource':2});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetKeyState':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,vk=struct.unpack('<2I',uc.mem_read(sp,8));state=processed_keys[vk] if vk<256 else 0;result=(0xffff8000 if state&128 else 0)|(state&1);key_state_calls.append({'virtual_key':vk,'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='MapVirtualKeyA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,code,kind=struct.unpack('<3I',uc.mem_read(sp,12));assert kind==1 and code<=255;result=keyboard_profile['base_scan_to_vk'][code] if code<128 else 0;result={0xa0:0x10,0xa1:0x10,0xa2:0x11,0xa3:0x11,0xa4:0x12,0xa5:0x12}.get(result,result);keyboard_calls.append({'code':code,'mapping_type':kind,'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='ToAscii':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,vk,scan,keys,output,flags=struct.unpack('<6I',uc.mem_read(sp,24));assert keys and output and flags==0;state=bytes(uc.mem_read(keys,256));shift=bool(state[16]&128 or state[160]&128 or state[161]&128);control=bool(state[17]&128 or state[162]&128 or state[163]&128);alt=bool(state[18]&128 or state[164]&128 or state[165]&128);assert not alt or control
   if vk>255 or alt:character=65535
   else:
    if not control and 65<=vk<=90:shift=bool(shift)^bool(state[20]&1)
    character=keyboard_profile['ascii_character_columns'][vk][int(shift)+2*int(control)]
   result=int(character!=65535)
   if result:uc.mem_write(output,struct.pack('<H',character))
   ascii_calls.append({'virtual_key':vk,'scan':scan,'shift':shift,'control':control,'alt':alt,'result':result,'character':character if result else None});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+24);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name'] in ['GlobalAlloc','GlobalFree','GlobalSize','GetLastError','SetLastError']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);count=bindings[imp['name']]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)));name=imp['name'];result=0
   if name=='GlobalAlloc':
    flags,requested=args[1:];assert flags in [0,0x40];page=os.sysconf('SC_PAGE_SIZE');span=(max(requested,1)+page-1)&~(page-1);result=heap_next;heap_next+=span;uc.mem_map(result,span);heap_blocks[result]=span;allocations.append({'address':result,'requested':requested,'span':span,'flags':flags})
   elif name=='GlobalFree':
    address=args[1]
    if address in heap_blocks:uc.mem_unmap(address,heap_blocks.pop(address))
    elif address:result=address;last_error=6
   elif name=='GlobalSize':result=heap_blocks.get(args[1],0)
   elif name=='GetLastError':result=last_error
   else:last_error=args[1]
   if bindings[name]['returns']:uc.reg_write(X.UC_X86_REG_EAX,result)
   uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name'] in ['TlsAlloc','TlsFree','TlsGetValue','TlsSetValue']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);count=bindings[imp['name']]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)));name=imp['name'];result=0
   if name=='TlsAlloc':result=next(i for i in range(1088) if i not in tls);tls[result]=0
   elif name=='TlsFree':assert args[1] in tls;del tls[args[1]];result=1
   elif name=='TlsSetValue':assert args[1] in tls;tls[args[1]]=args[2];result=1
   else:assert args[1] in tls;result=tls[args[1]];last_error=0
   tls_calls.append({'name':name,'arguments':list(args[1:]),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name'] in ['LoadLibraryA','GetProcAddress','FreeLibrary']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);name=imp['name'];count=bindings[name]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)))
   if name=='LoadLibraryA':
    text=bytearray();pos=args[1]
    while uc.mem_read(pos,1)!=b'\0':text.extend(uc.mem_read(pos,1));pos+=1
    assert text.decode('ascii').casefold()=='dinput.dll';module_references+=1;result=0x60000000
   elif name=='GetProcAddress':
    assert args[1]==0x60000000 and module_references>0;text=bytearray();pos=args[2]
    while uc.mem_read(pos,1)!=b'\0':text.extend(uc.mem_read(pos,1));pos+=1
    assert text.decode('ascii')=='DirectInputCreateA';result=0xfffd0000
   else:assert args[1]==0x60000000 and module_references>0;module_references-=1;result=1
   module_calls.append({'name':name,'args':list(args[1:]),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name']=='GetVersionExA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,outp=struct.unpack('<II',uc.mem_read(sp,8));size=struct.unpack('<I',uc.mem_read(outp,4))[0];assert size in [148,156]
   p=version_profile;value=bytearray(size);struct.pack_into('<5I',value,0,size,p['major'],p['minor'],p['build'],p['platform']);service=p['service_pack'].encode('ascii');value[20:20+len(service)]=service
   if size==156:struct.pack_into('<3HBB',value,148,p['service_pack_major'],p['service_pack_minor'],p['suite_mask'],p['product_type'],0)
   uc.mem_write(outp,bytes(value));version_calls.append({'output':outp,'size':size});uc.reg_write(X.UC_X86_REG_EAX,1);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetTickCount':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret=struct.unpack('<I',uc.mem_read(sp,4))[0];address,value=clock_samples[clock_next];clock_next+=1;assert address==0 and 0<=value<=0xffffffff
   uc.reg_write(X.UC_X86_REG_EAX,value);uc.reg_write(X.UC_X86_REG_ESP,sp+4);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name'] in ['QueryPerformanceFrequency','QueryPerformanceCounter','GetSystemTimeAsFileTime']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,outp=struct.unpack('<II',uc.mem_read(sp,8));assert clock_samples[clock_next][0]==outp
   value=clock_samples[clock_next][1];clock_next+=1
   if imp['name']=='QueryPerformanceFrequency':assert value==1000000000
   uc.mem_write(outp,struct.pack('<Q',value));
   if bindings[imp['name']]['returns']:uc.reg_write(X.UC_X86_REG_EAX,1)
   uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetDiskFreeSpaceA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);args=struct.unpack('<6I',uc.mem_read(sp,24));sample=disk_samples[disk_next];disk_next+=1;assert args[1:]==sample[:5]
   root=args[1]
   if root:
    text=bytearray();pos=root
    while uc.mem_read(pos,1)!=b'\0':text.extend(uc.mem_read(pos,1));pos+=1
    assert text.decode('ascii').casefold()=='c:\\'
   for pointer,value in zip(args[2:],sample[5:]):uc.mem_write(pointer,struct.pack('<I',value))
   uc.reg_write(X.UC_X86_REG_EAX,1);uc.reg_write(X.UC_X86_REG_ESP,sp+24);uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name']=='GetStartupInfoA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,output=struct.unpack('<II',uc.mem_read(sp,8));info=bytearray(68);struct.pack_into('<I',info,0,68);struct.pack_into('<I',info,44,0x101);struct.pack_into('<H',info,48,1)
   struct.pack_into('<3I',info,56,0xe1000001,0xe1000002,0xe1000003);uc.mem_write(output,bytes(info));uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name'] in ['GetModuleHandleA','GetModuleFileNameA']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);name=imp['name'];count=bindings[name]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)))
   if name=='GetModuleHandleA':assert args[1]==0;result=0x400000
   else:
    assert args[1] in [0,0x400000];path=b'C:\\TribesPad\\Game\\Tribes2.exe';outp,capacity=args[2:]
    if not capacity:result=0;last_error=122
    elif capacity<=len(path):uc.mem_write(outp,path[:capacity-1]+b'\0');result=capacity;last_error=122
    else:uc.mem_write(outp,path+b'\0');result=len(path)
   uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name'] in ['CreateCompatibleDC','CreateCompatibleBitmap','SelectObject','DeleteObject']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);name=imp['name'];count=bindings[name]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)))
   if name=='CreateCompatibleDC':
    assert args[1]==0 or args[1] in gdi;result=gdi_next;gdi_next+=2;gdi[result]={'kind':'dc','bitmap':result+1};gdi[result+1]={'kind':'bitmap','width':1,'height':1}
   elif name=='CreateCompatibleBitmap':assert args[1] in gdi and gdi[args[1]]['kind']=='dc';result=gdi_next;gdi_next+=1;gdi[result]={'kind':'bitmap','width':args[2],'height':args[3]}
   else:raise AssertionError('Later GDI selection/deletion needs a complete independent lifetime model')
   gdi_calls.append({'name':name,'arguments':list(args[1:]),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name'] in ['GetDesktopWindow','GetDC','GetDeviceCaps','ReleaseDC']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);name=imp['name'];count=bindings[name]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)));result=0
   if name=='GetDesktopWindow':result=0xc3000001
   elif name=='GetDC':
    assert args[1] in [0,0xc3000001];result=display_dc_next;display_dc_next+=1;display_dcs[result]=args[1]
   elif name=='GetDeviceCaps':
    assert args[1] in display_dcs and args[2] in [8,10,12];result={8:native['apple_display']['width'],10:native['apple_display']['height'],12:32}[args[2]]
   else:
    assert args[2] in display_dcs and display_dcs[args[2]]==args[1];display_dcs.pop(args[2]);result=1
   display_calls.append({'name':name,'arguments':list(args[1:]),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name']=='GetStockObject':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,index=struct.unpack('<2I',uc.mem_read(sp,8));assert index in [4,2];result=0xd6000000+index;stock_calls.append({'index':index,'handle':result,'style':0,'color':0 if index==4 else 0x808080});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='RegisterClassA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,descriptor=struct.unpack('<II',uc.mem_read(sp,8));fields=struct.unpack('<10I',uc.mem_read(descriptor,40));pointer=fields[9];text=bytearray()
   while uc.mem_read(pointer,1)!=b'\0':text.extend(uc.mem_read(pointer,1));pointer+=1
   assert 0x400000<=fields[1]<0x400000+len(image) or fields[1]==next(item['guest_va'] for item in imports if item['name']=='DefWindowProcA') # Preserve original image/import callback identity.
   atom=0xc000+len(window_classes);window_classes.append({'fields':fields,'name':text.decode('ascii'),'atom':atom});uc.reg_write(X.UC_X86_REG_EAX,atom);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='CreateWindowExA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);args=struct.unpack('<13I',uc.mem_read(sp,52));ret,extended,cls,title,style,x,y,width,height,parent,menu,instance,param=args
   assert not any([extended,style,x,y,width,height,parent,menu]);assert len(window_model)==0;registered=window_classes[0];assert registered['fields'][4]==instance;handle=0xc1000001;cs=sp-112;rect=sp-64
   uc.mem_write(cs,struct.pack('<12I',param,instance,menu,parent,height,width,y,x,style,title,cls,extended));uc.mem_write(rect,struct.pack('<4I',x,y,x+width,y+height))
   frame={'context':uc.context_save(),'fpu':fpu_snapshot(uc),'flag_producer':flag_producer,'sp':sp,'ret':ret,'handle':handle,'proc':registered['fields'][1],'stage':0,'messages':[(0x81,cs),(0x83,rect),(1,cs)]};window_model[handle]=frame;callback_frames.append(frame);start_window_callback(uc,frame)
  elif imp['name']=='DefWindowProcA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,handle,message,wp,lp=struct.unpack('<5I',uc.mem_read(sp,20));assert handle in window_model;assert message in [0x81,0x83,1,2,0x82,0x24];result=1 if message==0x81 else 0
   uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+20);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='WSAStartup':
   import resource
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,version,output=struct.unpack('<3I',uc.mem_read(sp,12));assert version==0x101;data=bytearray(400);struct.pack_into('<HH',data,0,0x101,0x101);description=b'TribesPad native BSD sockets\0';status=b'Running\0';data[4:4+len(description)]=description;data[261:261+len(status)]=status;struct.pack_into('<HH',data,390,min(resource.getrlimit(resource.RLIMIT_NOFILE)[0],65535),65507);uc.mem_write(output,bytes(data));uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetLogicalDriveStringsA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,capacity,output=struct.unpack('<3I',uc.mem_read(sp,12));result=5 if capacity<5 else 4
   if capacity>=5:assert output;uc.mem_write(output,b'C:\\\0\0')
   drive_string_calls.append({'capacity':capacity,'output':output,'result':result,'roots':['C:\\']});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetDriveTypeA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,root=struct.unpack('<2I',uc.mem_read(sp,8));name=bytes(uc.mem_read(root,4)) if root else b'C:\\\0';result=3 if name in [b'C:\\\0',b'c:\\\0'] else 1
   drive_type_calls.append({'root':root,'bytes':name.hex(),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetSystemMetrics':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,index=struct.unpack('<2I',uc.mem_read(sp,8));assert index in [0,1,42];result={0:native['apple_display']['width'],1:native['apple_display']['height'],42:0}[index]
   metric_calls.append({'index':index,'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='CreateSemaphoreA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,attributes,initial,maximum,name=struct.unpack('<5I',uc.mem_read(sp,20));assert attributes==name==0 and (initial>=0x80000000 or maximum==0 or maximum>=0x80000000 or initial>maximum);uc.reg_write(X.UC_X86_REG_EAX,0);last_error=87
   semaphore_calls.append({'initial':initial,'maximum':maximum,'result':0,'last_error':87});uc.reg_write(X.UC_X86_REG_ESP,sp+20);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='WaitForSingleObject':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,handle,timeout=struct.unpack('<3I',uc.mem_read(sp,12));assert handle==0;last_error=6;uc.reg_write(X.UC_X86_REG_EAX,0xffffffff);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
   semaphore_calls.append({'operation':'wait','handle':handle,'timeout':timeout,'result':0xffffffff,'last_error':6})
  elif imp['name']=='ReleaseSemaphore':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,handle,count,previous=struct.unpack('<4I',uc.mem_read(sp,16));assert handle==0 and 0<count<0x80000000 and previous==0;last_error=6;uc.reg_write(X.UC_X86_REG_EAX,0);uc.reg_write(X.UC_X86_REG_ESP,sp+16);uc.reg_write(X.UC_X86_REG_EIP,ret)
   semaphore_calls.append({'operation':'release','handle':handle,'count':count,'result':0,'last_error':6})
  elif imp['name']=='CreateFileA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);args=struct.unpack('<8I',uc.mem_read(sp,32));ret,pointer,access,share,security,creation,flags,template=args;assert access==0x80000000 and share==3 and security==template==0 and creation==3 and flags==0x08000080;text=bytearray()
   while uc.mem_read(pointer,1)!=b'\0':text.extend(uc.mem_read(pointer,1));pointer+=1
   guest=text.decode('ascii');host=find_host(guest);assert host.is_file();result=0xe1000000+4+sum(call['name']=='DuplicateHandle' for call in io_calls)+len(file_open_calls)
   file_positions[result]=0;file_open_calls.append({'guest_path':guest,'host_path':str(host),'handle':result,'bytes':host.stat().st_size,'sha256':hashlib.sha256(host.read_bytes()).hexdigest(),'access':access,'share':share,'flags':flags});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+32);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetFileSize':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,handle,high=struct.unpack('<3I',uc.mem_read(sp,12));file=next(call for call in file_open_calls if call['handle']==handle);size=file['bytes'];result=size&0xffffffff
   if high:uc.mem_write(high,struct.pack('<I',size>>32))
   last_error=0;file_size_calls.append({'handle':handle,'high_output':high,'bytes':size});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='SetFilePointer':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,handle,low,high,method=struct.unpack('<5I',uc.mem_read(sp,20));assert method in [0,1,2];file=next(call for call in file_open_calls if call['handle']==handle);distance=struct.unpack('<q',struct.pack('<II',low,struct.unpack('<I',uc.mem_read(high,4))[0]))[0] if high else struct.unpack('<i',struct.pack('<I',low))[0];origin=[0,file_positions[handle],file['bytes']][method];position=origin+distance;assert position>=0 and (high or position<=0xffffffff);file_positions[handle]=position
   if high:uc.mem_write(high,struct.pack('<I',position>>32))
   last_error=0;file_seek_calls.append({'handle':handle,'distance':distance,'method':method,'position':position});uc.reg_write(X.UC_X86_REG_EAX,position&0xffffffff);uc.reg_write(X.UC_X86_REG_ESP,sp+20);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='ReadFile':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,handle,buffer,count,output,overlapped=struct.unpack('<6I',uc.mem_read(sp,24));assert overlapped==0 and output;file=next(call for call in file_open_calls if call['handle']==handle);position=file_positions[handle]
   with open(file['host_path'],'rb') as source:source.seek(position);data=source.read(count)
   if data:uc.mem_write(buffer,data)
   uc.mem_write(output,struct.pack('<I',len(data)));file_positions[handle]+=len(data);file_read_calls.append({'handle':handle,'position':position,'requested':count,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()});uc.reg_write(X.UC_X86_REG_EAX,1);uc.reg_write(X.UC_X86_REG_ESP,sp+24);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name'] in ['FindFirstFileA','FindNextFileA','FindClose']:
   name=imp['name'];sp=uc.reg_read(X.UC_X86_REG_ESP);count=1 if name=='FindClose' else 2;args=struct.unpack('<'+'I'*(count+1),uc.mem_read(sp,4*(count+1)));fields,data=find_samples[find_next];find_next+=1;operation,argument,output,result,error,size=fields
   assert operation=={'FindFirstFileA':1,'FindNextFileA':2,'FindClose':3}[name] and argument==args[1] and output==(args[2] if count==2 else 0)
   if name=='FindFirstFileA':
    pointer=args[1];text=bytearray()
    while uc.mem_read(pointer,1)!=b'\0':text.extend(uc.mem_read(pointer,1));pointer+=1
    pattern=text.decode('ascii').replace('\\','/');directory,separator,leaf=pattern.rpartition('/');directory=directory if separator else '.';leaf=leaf if separator else pattern;names=find_names(directory,leaf)
    if names:assert result==find_handle_next and size==320;find_handle_next+=1;find_searches[result]={'directory':directory,'names':names,'index':1};validate_find_data(directory,names[0],data);assert error==last_error;uc.mem_write(output,data)
    else:assert result==0xffffffff and not data and error==2;last_error=2
   elif name=='FindNextFileA':
    search=find_searches[argument];index=search['index']
    if index<len(search['names']):assert result==1 and size==320;validate_find_data(search['directory'],search['names'][index],data);search['index']+=1;assert error==last_error;uc.mem_write(output,data)
    else:assert result==0 and not data and error==18;last_error=18
   else:assert argument in find_searches and result==1 and not data and error==last_error;del find_searches[argument]
   uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name']=='GetCurrentDirectoryA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,capacity,output=struct.unpack('<3I',uc.mem_read(sp,12));directory=b'C:\\TribesPad\\Game'
   if capacity<=len(directory):result=len(directory)+1
   else:uc.mem_write(output,directory+b'\0');result=len(directory)
   uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+12);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name']=='GetCommandLineA':
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret=struct.unpack('<I',uc.mem_read(sp,4))[0]
   if not command_address:
    # Choose the lowest available page in the configured allocation band.
    page=os.sysconf('SC_PAGE_SIZE');command_address=next(v for v in range(0x30000000,heap_next,page) if all(not a<=v<a+n for a,n in heap_blocks.items())) if any(v not in heap_blocks for v in range(0x30000000,heap_next,page) if all(not a<=v<a+n for a,n in heap_blocks.items())) else heap_next
    if command_address==heap_next:heap_next+=page
    uc.mem_map(command_address,page);uc.mem_write(command_address,command_line);heap_blocks[command_address]=page
   uc.reg_write(X.UC_X86_REG_EAX,command_address);uc.reg_write(X.UC_X86_REG_ESP,sp+4);uc.reg_write(X.UC_X86_REG_EIP,ret)
  elif imp['name'] in ['GetEnvironmentStrings','FreeEnvironmentStringsA']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);count=bindings[imp['name']]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)));name=imp['name'];result=0
   if name=='GetEnvironmentStrings':
    page=os.sysconf('SC_PAGE_SIZE');span=(len(environment)+page-1)&~(page-1);result=heap_next;heap_next+=span;uc.mem_map(result,span);uc.mem_write(result,environment);heap_blocks[result]=span;environment_blocks.add(result)
   else:
    assert args[1] in environment_blocks;environment_blocks.remove(args[1]);uc.mem_unmap(args[1],heap_blocks.pop(args[1]));result=1
   environment_calls.append({'name':name,'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name'] in ['GetStdHandle','SetStdHandle','GetFileType','CloseHandle','GetCurrentProcess','DuplicateHandle']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);count=bindings[imp['name']]['arguments'];args=struct.unpack('<'+str(count+1)+'I',uc.mem_read(sp,4*(count+1)));name=imp['name'];result=0
   if name=='GetCurrentProcess':result=0xffffffff
   elif name=='GetStdHandle':result=standard.get(args[1],0xffffffff)
   elif name=='SetStdHandle':standard[args[1]]=args[2];result=1
   elif name=='DuplicateHandle':
    assert args[1]==0xffffffff and args[3]==0xffffffff and args[7]==2;result=1;duplicate=0xe1000000+4+sum(call['name']=='DuplicateHandle' for call in io_calls);uc.mem_write(args[4],struct.pack('<I',duplicate))
   elif name=='CloseHandle':
    handle=args[1];assert handle in file_positions;del file_positions[handle];result=1
   else:
    handle=args[1];assert handle in [0xe1000001,0xe1000002,0xe1000003]
    if handle==0xe1000001:
     mode=os.fstat(0).st_mode;result=2 if stat.S_ISCHR(mode) else 3 if stat.S_ISFIFO(mode) or stat.S_ISSOCK(mode) else 1
    else:result=3 # Both native subprocess outputs are actual pipes.
   io_calls.append({'name':name,'arguments':list(args[1:]),'result':result});uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,sp+4*(count+1));uc.reg_write(X.UC_X86_REG_EIP,args[0])
  elif imp['name'] in ['InitializeCriticalSection','DeleteCriticalSection','EnterCriticalSection','LeaveCriticalSection','TryEnterCriticalSection']:
   sp=uc.reg_read(X.UC_X86_REG_ESP);ret,value=struct.unpack('<II',uc.mem_read(sp,8));name=imp['name'];calls.append({'name':name,'guest_address':value})
   if name=='InitializeCriticalSection':
    assert value not in critical;critical.add(value);uc.mem_write(value,struct.pack('<6I',0,0xffffffff,0,0,0,0))
   elif name=='DeleteCriticalSection':critical.remove(value)
   else:assert value in critical
   if name=='TryEnterCriticalSection':uc.reg_write(X.UC_X86_REG_EAX,1)
   uc.reg_write(X.UC_X86_REG_ESP,sp+8);uc.reg_write(X.UC_X86_REG_EIP,ret)
  else:seen.append(address);uc.emu_stop()
u.hook_add(unicorn.UC_HOOK_CODE,stop_import);u.emu_start(0x6fde60,0,count=a.instruction_budget)
if dinput_method is not None:
 assert dinput_seen==[(0xfffd2000+16*(dinput_method-100)) if dinput_method>=100 else (0xfffd1000+16*dinput_method)] and not seen
 snapshot=(out/'stack.bin.input').read_bytes();object=struct.unpack('<I',snapshot[:4])[0];assert object in dinput_objects and snapshot[4:]==bytes(u.mem_read(object,112 if dinput_method>=100 else 40))
 if dinput_method==4:
  sp=u.reg_read(X.UC_X86_REG_ESP);ret,obj,kind,callback,context,flags=struct.unpack('<6I',u.mem_read(sp,24));assert obj==object and kind==3 and callback==0x556f90 and flags==1
  row['arguments']={'object':obj,'device_type':kind,'callback':hex(callback),'context':context,'flags':flags}
 if dinput_method==104:
  sp=u.reg_read(X.UC_X86_REG_ESP);ret,obj,callback,context,flags=struct.unpack('<5I',u.mem_read(sp,20));assert obj==object
  row['arguments']={'device_object':obj,'callback':hex(callback),'context':context,'flags':flags}
 if dinput_method==105:
  sp=u.reg_read(X.UC_X86_REG_ESP);ret,obj,property_id,header=struct.unpack('<4I',u.mem_read(sp,16));assert obj==object
  fields=struct.unpack('<4I',u.mem_read(header,16));assert fields==(24,16,0,2) and property_id==4
  row['arguments']={'device_object':obj,'property_id':property_id,'property_header':header,'size':fields[0],'header_size':fields[1],'object_id':fields[2],'selection_mode':fields[3]}
 if dinput_method==106:
  sp=u.reg_read(X.UC_X86_REG_ESP);ret,obj,prop,header=struct.unpack('<4I',u.mem_read(sp,16));fields=struct.unpack('<4I',u.mem_read(header,16));assert obj==object
  row['arguments']={'device_object':obj,'property_id':prop,'header_address':header,'header':list(fields),'payload_hex':bytes(u.mem_read(header+16,fields[0]-16)).hex()}
 if dinput_method==111:
  sp=u.reg_read(X.UC_X86_REG_ESP);ret,obj,format_address=struct.unpack('<3I',u.mem_read(sp,12));fields=struct.unpack('<6I',u.mem_read(format_address,24));assert obj==object and fields[:5]==(24,16,1,1024,256)
  rows=[struct.unpack('<4I',u.mem_read(fields[5]+i*16,16)) for i in range(fields[4])];assert len(set(row[1] for row in rows))==256 and len(set(row[2] for row in rows))==256
  assert all(row[0] and bytes(u.mem_read(row[0],16)).hex()=='208272553cd3cf11bfc7444553540000' and row[1]==i and row[2]==(i<<8|4) and row[3]==0 for i,row in enumerate(rows))
  row['arguments']={'device_object':obj,'format_address':format_address,'size':fields[0],'object_size':fields[1],'flags':fields[2],'data_size':fields[3],'object_count':fields[4],'object_array':fields[5],'complete_unique_keyboard_format_verified':True}
 if dinput_method==3:
  sp=u.reg_read(X.UC_X86_REG_ESP);ret,obj,guid,output,outer=struct.unpack('<5I',u.mem_read(sp,20));assert obj==object and outer==0
  row['arguments']={'root_object':obj,'instance_guid_hex':bytes(u.mem_read(guid,16)).hex(),'output':output,'outer':outer}
if not seen and hardware is None and dinput_method is None:print('Budget progress',instruction_count,'file opens',len(file_open_calls),'reads',len(file_read_calls),'bytes',sum(call['bytes'] for call in file_read_calls),'seeks',len(file_seek_calls),'heap allocations',len(allocations),flush=True)
if hardware is None and dinput_method is None:assert seen==[row['guest_va']],('first import',seen,row,'x86 pc',hex(u.reg_read(X.UC_X86_REG_EIP)),'clock consumed',clock_next)
if hardware is not None:
 assert len(hardware_seen)==1,('hardware stop not reached',hardware,hardware_seen,seen,hex(u.reg_read(X.UC_X86_REG_EIP)),recent);row.update({'original_pc':hex(hardware_seen[0]),'leaf':native['regs'][0],'subleaf':native['regs'][1]})
if hardware is None and dinput_method is None and row['name']=='LoadCursorA':
 sp=u.reg_read(X.UC_X86_REG_ESP);ret,module,name=struct.unpack('<3I',u.mem_read(sp,12));row['arguments']={'module':module,'resource_id':name}
if hardware is None and dinput_method is None and row['name']=='GetKeyState':
 sp=u.reg_read(X.UC_X86_REG_ESP);ret,vk=struct.unpack('<2I',u.mem_read(sp,8));row['arguments']={'virtual_key':vk}
if hardware is None and dinput_method is None and row['name']=='ToAscii':
 sp=u.reg_read(X.UC_X86_REG_ESP);ret,vk,scan,keys,output,flags=struct.unpack('<6I',u.mem_read(sp,24));row['arguments']={'virtual_key':vk,'scan_code':scan,'keyboard_state_address':keys,'keyboard_state_hex':bytes(u.mem_read(keys,256)).hex() if keys else None,'output_word_address':output,'flags':flags}
if hardware is None and dinput_method is None and row['name']=='MapVirtualKeyA':
 sp=u.reg_read(X.UC_X86_REG_ESP);ret,code,mapping=struct.unpack('<3I',u.mem_read(sp,12));row['arguments']={'code':code,'mapping_type':mapping}
if hardware is None and dinput_method is None and row['name']=='LoadLibraryA':
 sp=u.reg_read(X.UC_X86_REG_ESP);name_address=struct.unpack('<I',u.mem_read(sp+4,4))[0];name=bytearray()
 for offset in range(4096):
  byte=bytes(u.mem_read(name_address+offset,1))
  if byte==b'\0':break
  name.extend(byte)
 else:raise AssertionError('Unterminated DLL name')
 row['requested_library']=name.decode('cp1252')
native_window_events=list(struct.iter_unpack('<5I',(out/'stack.bin.windows').read_bytes()));assert callback_events==native_window_events;assert all(frame.get("kind") in ["input","object"] for frame in callback_frames)
raw=(out/"stack.bin.enums").read_bytes();native_input_events=[];position=0
while position<len(raw):
 callback,context,descriptor,size=struct.unpack_from("<4I",raw,position);position+=16;assert size in [560,580];native_input_events.append((callback,context,descriptor,size,raw[position:position+size]));position+=size
assert input_events==native_input_events and position==len(raw)
raw=(out/'stack.bin.objects').read_bytes();native_object_events=[];position=0
while position<len(raw):
 callback,context,descriptor,size=struct.unpack_from('<4I',raw,position);position+=16;assert size in [292,316];native_object_events.append((callback,context,descriptor,size,raw[position:position+size]));position+=size
assert object_events==native_object_events and position==len(raw)
raw=(out/'stack.bin.formats').read_bytes();position=0;native_formats=[]
while position<len(raw):
 obj,flags,data_size,count=struct.unpack_from('<4I',raw,position);position+=16;mapping=list(struct.iter_unpack('<3I',raw[position:position+count*12]));position+=count*12;native_formats.append((obj,flags,data_size,count,mapping))
assert position==len(raw) and format_events==native_formats
native_properties=list(struct.iter_unpack('<4I',(out/'stack.bin.properties').read_bytes()));assert property_events==native_properties
raw=(out/'stack.bin.heap').read_bytes();position=0;heap_snapshots={}
while position<len(raw):
 address,span=struct.unpack_from('<2I',raw,position);position+=8;assert address in heap_blocks and heap_blocks[address]==span;reference=bytes(u.mem_read(address,span)); actual=raw[position:position+span]; assert actual==reference,('heap divergence',hex(address),span,[(hex(address+j),actual[j:j+16].hex(),reference[j:j+16].hex()) for j in range(0,span,16) if actual[j:j+16]!=reference[j:j+16]][:20]);heap_snapshots[address]=span;position+=span
assert heap_snapshots==heap_blocks and position==len(raw)

regs=[X.UC_X86_REG_EAX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_EBX,X.UC_X86_REG_ESP,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI,X.UC_X86_REG_EFLAGS]
oracle=[u.reg_read(reg) for reg in regs];assert native['regs'][:8]==oracle[:8],('register divergence',row['name'],native['regs'],oracle);defined_mask=0x200ed5
if flag_producer and flag_producer[1]=='imul':
 # Capstone 5 labels SF modified here; Intel defines SF/ZF/AF/PF undefined.
 defined_mask&=~(128|64|16|4)
assert native['regs'][8]&defined_mask==oracle[8]&defined_mask,('defined flag divergence',row['name'],native['regs'][8],oracle[8],hex(defined_mask),flag_producer,recent)
assert native['registered_filter']==(filters[-1] if filters else 0)
assert native['seh_head']==struct.unpack('<I',u.mem_read(0,4))[0]
native_stack=(out/'stack.bin').read_bytes();reference_stack=bytes(u.mem_read(0x100000,0x200000))
if native_stack!=reference_stack:
 differences=[i for i,(a,b) in enumerate(zip(native_stack,reference_stack)) if a!=b];print('Stack differences',[(hex(0x100000+(i&~3)),native_stack[i&~3:(i&~3)+4].hex(),reference_stack[i&~3:(i&~3)+4].hex()) for i in differences[:24]],'next',row['name'])
assert native_stack==reference_stack
assert (out/'stack.bin.image').read_bytes()==bytes(u.mem_read(0x400000,len(image))),'loaded image data diverged'
text_cache_address=0x83a020;text_cache_size=0x60c;native_text_cache=(out/'stack.bin.image').read_bytes()[text_cache_address-0x400000:text_cache_address-0x400000+text_cache_size];assert len(native_text_cache)==text_cache_size and native_text_cache==bytes(u.mem_read(text_cache_address,text_cache_size));text_cache_rows=[struct.unpack_from('<3I',native_text_cache,i*12) for i in range(text_cache_size//12)]
detected_cpu_flags=struct.unpack('<I',u.mem_read(0x8477fc,4))[0];assert detected_cpu_flags==7,('original CPU feature selection',detected_cpu_flags)
assert find_next==len(find_samples)
assert native['mxcsr']==u.reg_read(X.UC_X86_REG_MXCSR),(native['mxcsr'],u.reg_read(X.UC_X86_REG_MXCSR))
assert native['xmm_hex']==b''.join(u.reg_read(getattr(X,'UC_X86_REG_XMM'+str(i))).to_bytes(16,'little') for i in range(8)).hex()

report={'file_seek_calls':file_seek_calls,'file_read_calls':file_read_calls,'file_size_calls':file_size_calls,'file_open_calls':file_open_calls,'file_search_entries':find_evidence,'file_search_calls':len(find_samples),'file_searches_active_at_stop':len(find_searches),'file_search_trace_sha256':hashlib.sha256(find_raw).hexdigest(),'semaphore_creation_calls':semaphore_calls,'system_metric_calls':metric_calls,'drive_type_calls':drive_type_calls,'logical_drive_string_calls':drive_string_calls,'apple_display':native['apple_display'],'native_display_calls':display_calls,'apple_display_source_sha256':hashlib.sha256((R/'port/apple/display.m').read_bytes()).hexdigest(),'native_stock_brush_descriptors_match':True,'native_stock_brush_descriptors':brush_events,'registered_window_classes':[dict(item,fields=list(item['fields'])) for item in window_classes],'stock_brush_calls':stock_calls,'cursor_resource':cursor_evidence,'cursor_calls':cursor_calls,'cursor_theme_sha256':hashlib.sha256((R/'config/runtime/cursor-theme.json').read_bytes()).hexdigest(),'simulator_device':a.simulator_device,'execution_target':'arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos','original_icon':icon_evidence,'icon_calls':icon_calls,'key_state_calls':key_state_calls,'keyboard_state_profile_sha256':hashlib.sha256((R/'config/runtime/keyboard-state.json').read_bytes()).hexdigest(),'original_text_cache_matches':True,'text_cache_address':hex(text_cache_address),'text_cache_bytes':text_cache_size,'text_cache_sha256':hashlib.sha256(native_text_cache).hexdigest(),'nonzero_text_cache_rows':sum(any(row) for row in text_cache_rows),'ascii_translation_calls':ascii_calls,'keyboard_mapping_calls':keyboard_calls,'keyboard_profile_sha256':hashlib.sha256((R/'config/runtime/keyboard-layout.json').read_bytes()).hexdigest(),'native_retained_properties_match':True,'retained_input_properties':property_events,'native_retained_format_matches_original_request':True,'retained_formats':format_events,'whole_live_guest_heap_matches_service_model':True,'live_guest_heap_bytes_compared':sum(heap_snapshots.values()),'original_object_callbacks_entered':[list(event[:4]) for event in object_events],'object_descriptor_and_callback_entry_match':True,'native_device_calls':device_calls,'input_profile':input_profile,'input_profile_sha256':hashlib.sha256((R/'config/runtime/input-devices.json').read_bytes()).hexdigest(),'original_input_callbacks_entered':[list(event[:4]) for event in input_events],'input_descriptor_and_callback_entry_match':True,'input_callbacks_still_active_at_stop':sum(frame.get('kind') in ['input','object'] for frame in callback_frames),'native_module_calls':module_calls,'directinput_calls':dinput_calls,'native_input_method_stop':dinput_method,'directinput_guest_object_bytes_match':True if dinput_method is not None else None,'original_detected_cpu_flags':detected_cpu_flags,'cpu_profile_sha256':hashlib.sha256((R/'config/runtime/cpu.json').read_bytes()).hexdigest(),'version_profile_sha256':hashlib.sha256((R/'config/runtime/version.json').read_bytes()).hexdigest(),'guest_version_profile':version_profile,'version_calls':version_calls,'guest_cpu_profile':cpu_profile,'original_cpuid_calls':cpuid_calls,'whole_image_data_matches_service_model':True,'state':'finished','first_entry_differential_pass':True,'engine_entry':'0x6fde60','first_unimplemented_service':row,'stop_kind':'native input method' if dinput_method is not None else 'import' if hardware is None else 'hardware helper','native_bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'bootstrap_driver_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'native_service_sources':{name:hashlib.sha256((R/name).read_bytes()).hexdigest() for name in sources if name.startswith('port/core/')},'native_bootstrap_sha256':hashlib.sha256((out/'bootstrap').read_bytes()).hexdigest(),'defined_flags_and_registers_match':True,'flags_compared_mask':hex(defined_mask),'additional_control_flags_compared':['IF','ID'],'last_integer_flag_producer':flag_producer,'x86_instruction_budget':a.instruction_budget,'x86_instructions_observed':instruction_count,'whole_stack_bytes_match':True,'seh_head_match':True,'registered_filter':native['registered_filter'],'implemented_registration_calls':len(filters),'critical_section_calls':calls,'fixed_global_allocations':allocations,'standard_handle_calls':io_calls,'gdi_calls':gdi_calls,'window_class_registrations':window_classes,'original_window_callbacks':callback_events,'window_callback_differential_pass':True,'environment_calls':environment_calls,'tls_calls':tls_calls,'environment_sha256':hashlib.sha256(environment).hexdigest(),'command_line_sha256':hashlib.sha256(command_line).hexdigest(),'clock_samples_replayed':clock_next,'disk_samples_replayed':disk_next,'filesystem_mounts':{'C_root':str(volume),'game':str(game),'temp':str(temp)},'disk_external_inputs':disk_samples,'diagnostic_exit':run.returncode,'native_engine_sha256':compiled['object_sha256'],'whole_engine_entry_executed':True,'game_startup_completed':False,'menus_or_match_verified':False,'limits':['CPUID supplies a fixed compatibility profile advertising tested x87/MMX; later feature-dependent paths and QoL instructions remain unfinished','Winsock has an explicit 1.1 negotiation profile and native IPv4 UDP services; select, DNS, async notifications, TCP connection/auth and full cancellation remain unfinished','Native DirectInput provider load/export routing and COM root creation are implemented; device acquisition/state, Apple input feed, arbitrary PE DLL loading/initialization, exports and patching remain unfinished','GDI supports memory DCs and monochrome bitmaps; fonts, drawing, window/screen DCs and rendering remain unfinished','A hidden WinSock notification window executes its three original creation callbacks; visible Apple windows, message queues, async socket notifications and thread-exit cleanup remain unfinished','Filesystem namespace currently supports existing ASCII paths only; creation, full ANSI decoding, UNC/device paths, teardown and physical-device mounts remain unfinished','Live clock, disk and file metadata snapshots are shared external inputs; native tests check layouts/timestamps and an independent host inventory validates file names, attributes, sizes and stable timestamps before replay. Actual Windows DLL execution remains unverified','Exception-filter registration, recursive critical sections, fixed allocations and descriptor-backed handle services are implemented; other Windows imports stop. Fault delivery is unfinished','FS:0 supplies an exception-list head only; Windows TEB and SEH delivery remain unfinished','Original x86 is compared with a single-thread service contract model; actual Windows DLL execution and later runtime correctness are unverified']}
assert native['ymm_hi_hex']=='00'*128, 'legacy engine changed initialized upper vector lanes'
report.update(sse_math=sse_math['evidence'],initial_mxcsr_profile=0x1f80,native_vector_state_matches=True,upper_vector_lanes_preserved=True,native_mxcsr=native['mxcsr'],cpu_header_sha256=hashlib.sha256((R/'port/llasm-support/llasm_cpu.h').read_bytes()).hexdigest(),sse_source_sha256=hashlib.sha256((R/'port/llasm-support/tribes_sse.c').read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
