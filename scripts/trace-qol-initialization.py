#!/usr/bin/env python3
"""Trace pinned IFC22 TLS/process attach to an explicit service/CPU boundary.
This is an original-x86 loader-contract diagnostic, not a native module loader.
No missing API is given a success value. Guest patch writes are captured.
"""
import argparse,ctypes,hashlib,json,mmap,os,pathlib,struct,subprocess,threading,time
import capstone,pefile,unicorn
from unicorn import x86_const as X
from guest_page_contract import GuestPages, PERMISSIONS
from runtime_module_catalog import audit_absent_module
from guest_cache_reference import ReferenceInstructionCache
from guest_teb_contract import configure_sparse_teb,DECLARED_FIELDS
from guest_executable_reference import ExecutableReference
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--diagnostic-executable-capture',action='store_true',help='Original-x86 Unicorn executable-allocation capture only; never native ARM64 execution');ap.add_argument('--budget',type=int,default=1000000);ap.add_argument('--diagnostic-zero-teb',action='store_true',help='Reproduce invalid legacy zero TEB identity as a diagnostic control only');ap.add_argument('--diagnostic-sse2',action='store_true',help='Explore SSE2-capable x86 branch; does not change native capability claims');a=ap.parse_args();out=R/'generated/qol-initialization';out.mkdir(exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','initialized':False})+'\n')
subprocess.run(['python3',str(R/'scripts/gen-oem-environment-table.py'),'--check'],check=True,capture_output=True)
native_path=out/'loader-services.dylib';native_sources=[R/'port/core/exception_filter.c',R/'port/core/guest_console.c',R/'port/core/guest_filesystem.c',R/'port/core/guest_locale_identifiers.c',R/'port/core/guest_locale_info.c',R/'port/core/guest_datetime.c',R/'port/core/guest_locales.c',R/'port/core/guest_collation.c',R/'port/core/guest_once.c',R/'port/core/guest_address_space.c',R/'port/core/guest_sdl_memory.c',R/'port/core/guest_teb.c',R/'port/core/guest_srw.c',R/'port/core/guest_virtual_memory.c',R/'port/core/guest_system.c',R/'port/core/performance_clock.c',R/'port/core/guest_slist.c',R/'port/core/guest_modules.c',R/'port/core/guest_heap.c',R/'port/core/guest_tls.c',R/'port/core/critical_section.c',R/'port/core/process_heap.c',R/'port/core/native_fls.c',R/'port/core/process_startup.c',R/'port/core/native_handles.c',R/'port/core/process_environment.c',R/'port/core/guest_encoding.c']
subprocess.run(['clang','-dynamiclib','-O2','-Wall','-Wextra','-Werror',*[str(p) for p in native_sources],'-o',str(native_path)],check=True,capture_output=True)
native=ctypes.CDLL(str(native_path));native.tribes_set_console_ctrl_handler.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32];native.tribes_set_console_ctrl_handler.restype=ctypes.c_uint32
native.tribes_console_begin.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.POINTER(ctypes.c_void_p)];native.tribes_console_begin.restype=ctypes.c_int
native.tribes_console_next.argtypes=[ctypes.c_void_p];native.tribes_console_next.restype=ctypes.c_uint32
native.tribes_console_ignored.argtypes=[ctypes.c_void_p];native.tribes_console_ignored.restype=ctypes.c_int
native.tribes_console_end.argtypes=[ctypes.c_void_p];native.tribes_console_end.restype=None
native.tribes_set_exception_filter.argtypes=[ctypes.c_uint32];native.tribes_set_exception_filter.restype=ctypes.c_uint32
native.tribes_exception_filter.argtypes=[];native.tribes_exception_filter.restype=ctypes.c_uint32
native.tribes_load_library_ex_w.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32];native.tribes_load_library_ex_w.restype=ctypes.c_uint32;native.tribes_get_last_error.argtypes=[];native.tribes_get_last_error.restype=ctypes.c_uint32
native.tribes_command_line_configure.argtypes=[ctypes.c_uint64,ctypes.c_void_p,ctypes.c_uint32];native.tribes_command_line_configure.restype=ctypes.c_uint32
native.tribes_get_command_line.argtypes=[ctypes.c_uint64];native.tribes_get_command_line.restype=ctypes.c_uint32
native.tribes_command_line_configure_w.argtypes=[ctypes.c_uint64,ctypes.c_void_p,ctypes.c_uint32];native.tribes_command_line_configure_w.restype=ctypes.c_uint32
native.tribes_get_command_line_w.argtypes=[ctypes.c_uint64];native.tribes_get_command_line_w.restype=ctypes.c_uint32
native.tribes_teb_configure.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*6;native.tribes_teb_configure.restype=ctypes.c_uint32
native.x86_read_fs_dword.argtypes=[ctypes.c_uint32];native.x86_read_fs_dword.restype=ctypes.c_uint32
native.x86_write_fs_dword.argtypes=[ctypes.c_uint32,ctypes.c_uint32];native.x86_write_fs_dword.restype=None
teb_native_reads=0;teb_native_writes=0
srw_functions={'InitializeSRWLock':'tribes_srw_initialize','AcquireSRWLockExclusive':'tribes_srw_acquire_exclusive','ReleaseSRWLockExclusive':'tribes_srw_release_exclusive','AcquireSRWLockShared':'tribes_srw_acquire_shared','ReleaseSRWLockShared':'tribes_srw_release_shared','TryAcquireSRWLockExclusive':'tribes_srw_try_exclusive','TryAcquireSRWLockShared':'tribes_srw_try_shared'}
for name,helper in srw_functions.items():
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64,ctypes.c_uint32];fn.restype=ctypes.c_uint32 if name.startswith('Try') else None
native.tribes_srw_live_count.argtypes=[];native.tribes_srw_live_count.restype=ctypes.c_uint32
native.tribes_sleep.argtypes=[ctypes.c_uint32];native.tribes_sleep.restype=None
identifier_functions={'GetUserDefaultLocaleName':('tribes_get_user_default_locale_name',2),'IsValidLocaleName':('tribes_is_valid_locale_name',1),'LocaleNameToLCID':('tribes_locale_name_to_lcid',2),'LCIDToLocaleName':('tribes_lcid_to_locale_name',4)}
for helper,argc in identifier_functions.values():
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*argc;fn.restype=ctypes.c_uint32
native.tribes_get_locale_info_ex.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*4;native.tribes_get_locale_info_ex.restype=ctypes.c_uint32
for helper,argc in [('tribes_get_date_format_ex',7),('tribes_get_time_format_ex',6)]:
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*argc;fn.restype=ctypes.c_uint32
native.tribes_locale_enum_validate.argtypes=[ctypes.c_uint32]*3;native.tribes_locale_enum_validate.restype=ctypes.c_uint32
native.tribes_locale_next.argtypes=[ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32),ctypes.POINTER(ctypes.c_uint16),ctypes.POINTER(ctypes.c_uint32)];native.tribes_locale_next.restype=ctypes.c_uint32
native.tribes_once_begin.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)];native.tribes_once_begin.restype=ctypes.c_uint32
native.tribes_once_complete.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*3;native.tribes_once_complete.restype=ctypes.c_uint32
native.tribes_once_initialize.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_once_initialize.restype=None
native.tribes_sdl_set_memory_functions.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*4;native.tribes_sdl_set_memory_functions.restype=ctypes.c_uint32
native.tribes_sdl_memory_functions.argtypes=[ctypes.c_uint64,ctypes.POINTER(ctypes.c_uint32)];native.tribes_sdl_memory_functions.restype=ctypes.c_int
native.tribes_address_space_begin.argtypes=[ctypes.c_uint64];native.tribes_address_space_begin.restype=ctypes.c_uint32
native.tribes_address_space_declare.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*7;native.tribes_address_space_declare.restype=ctypes.c_uint32
native.tribes_address_space_seal.argtypes=[ctypes.c_uint64];native.tribes_address_space_seal.restype=ctypes.c_uint32
native.tribes_address_space_protect.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*3;native.tribes_address_space_protect.restype=ctypes.c_uint32
native.tribes_virtual_query.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*3;native.tribes_virtual_query.restype=ctypes.c_uint32
native.tribes_get_current_thread.argtypes=[];native.tribes_get_current_thread.restype=ctypes.c_uint32
native.tribes_get_current_process.argtypes=[];native.tribes_get_current_process.restype=ctypes.c_uint32
native.tribes_are_file_apis_ansi.argtypes=[];native.tribes_are_file_apis_ansi.restype=ctypes.c_uint32
native.tribes_get_acp.argtypes=[];native.tribes_get_acp.restype=ctypes.c_uint32
native.tribes_get_oemcp.argtypes=[];native.tribes_get_oemcp.restype=ctypes.c_uint32
native.tribes_is_valid_code_page.argtypes=[ctypes.c_uint32];native.tribes_is_valid_code_page.restype=ctypes.c_uint32
native.tribes_get_cp_info.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32];native.tribes_get_cp_info.restype=ctypes.c_uint32
native.tribes_multibyte_to_wide.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*6;native.tribes_multibyte_to_wide.restype=ctypes.c_uint32
native.tribes_get_string_type_w.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*4;native.tribes_get_string_type_w.restype=ctypes.c_uint32
native.tribes_compare_string_ex.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*9;native.tribes_compare_string_ex.restype=ctypes.c_uint32
native.tribes_lcmap_string_ex.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*9;native.tribes_lcmap_string_ex.restype=ctypes.c_uint32
native.tribes_wide_to_multibyte.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*8;native.tribes_wide_to_multibyte.restype=ctypes.c_uint32
native.tribes_initialize_slist_head.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_initialize_slist_head.restype=None
native.tribes_get_module_handle.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_get_module_handle.restype=ctypes.c_uint32
native.tribes_get_module_handle_w.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_get_module_handle_w.restype=ctypes.c_uint32
native.tribes_system_file_time.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_system_file_time.restype=None
native.tribes_get_temp_path_w.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32];native.tribes_get_temp_path_w.restype=ctypes.c_uint32
for helper in ('tribes_performance_counter','tribes_performance_frequency'):
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64,ctypes.c_uint32];fn.restype=ctypes.c_uint32
native.tribes_get_environment_variable_a.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*3;native.tribes_get_environment_variable_a.restype=ctypes.c_uint32
native.tribes_get_system_info.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_get_system_info.restype=None
native.tribes_load_library.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_load_library.restype=ctypes.c_uint32
topology_functions={'GetCurrentProcessorNumberEx':('tribes_get_current_processor_number_ex',1,4),'GetNumaProcessorNodeEx':('tribes_get_numa_processor_node_ex',2,2),'GetNumaProcessorNode':('tribes_get_numa_processor_node',2,1),'GetNumaNodeProcessorMaskEx':('tribes_get_numa_node_processor_mask_ex',2,12),'GetNumaHighestNodeNumber':('tribes_get_numa_highest_node_number',1,4)}
for helper,argc,size in topology_functions.values():
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*argc;fn.restype=None if helper=='tribes_get_current_processor_number_ex' else ctypes.c_uint32
native.tribes_get_module_filename.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*3;native.tribes_get_module_filename.restype=ctypes.c_uint32
native.tribes_get_module_filename_w.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*3;native.tribes_get_module_filename_w.restype=ctypes.c_uint32
native.tribes_get_environment_strings_w.argtypes=[ctypes.c_uint64];native.tribes_get_environment_strings_w.restype=ctypes.c_uint32
native.tribes_free_environment_strings_w.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_free_environment_strings_w.restype=ctypes.c_uint32
native.tribes_environment_snapshot_size.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_int];native.tribes_environment_snapshot_size.restype=ctypes.c_uint32
native.tribes_global_size.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_global_size.restype=ctypes.c_uint32
environment_snapshots={}
virtual_reservations={}
native.tribes_virtual_alloc.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*4;native.tribes_virtual_alloc.restype=ctypes.c_uint32
def native_virtual_runs():
 runs=[]
 @VirtualMemoryVisitor
 def collect(address,length,protection,context):
  runs.append((address,length,protection));return 1
 assert native.tribes_virtual_memory_visit(host_base,collect,None)
 return runs
native.tribes_virtual_alloc2_from_app.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*7;native.tribes_virtual_alloc2_from_app.restype=ctypes.c_uint32
VirtualMemoryVisitor=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p)
native.tribes_virtual_memory_visit.argtypes=[ctypes.c_uint64,VirtualMemoryVisitor,ctypes.c_void_p];native.tribes_virtual_memory_visit.restype=ctypes.c_int
class NativeExport(ctypes.Structure):
 _fields_=[('name',ctypes.c_char_p),('ordinal',ctypes.c_uint16),('address',ctypes.c_uint32)]
# A reserved, nonexecuting host mirror. Only validated call input/output ranges
# are copied; this does not make Unicorn image execution native.
host_window=mmap.mmap(-1,0x100000000);host_base=ctypes.addressof(ctypes.c_char.from_buffer(host_window));kernel32=0x60010000
native.tribes_filesystem_configure.argtypes=[ctypes.c_uint64]+[ctypes.c_char_p]*3;native.tribes_filesystem_configure.restype=ctypes.c_int
native.tribes_set_current_directory.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_set_current_directory.restype=ctypes.c_uint32
native.tribes_get_current_directory.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32];native.tribes_get_current_directory.restype=ctypes.c_uint32
filesystem_root=out/'filesystem';filesystem_temp=filesystem_root/'TribesPad/Temp';filesystem_temp.mkdir(parents=True,exist_ok=True)
assert native.tribes_filesystem_configure(host_base,os.fsencode(filesystem_root),os.fsencode(R/'ref/product'),os.fsencode(filesystem_temp))
native.tribes_get_std_handle.argtypes=[ctypes.c_uint32];native.tribes_get_std_handle.restype=ctypes.c_uint32
native.tribes_get_file_type.argtypes=[ctypes.c_uint32];native.tribes_get_file_type.restype=ctypes.c_uint32
launch_handles=(ctypes.c_uint32*3)(*[native.tribes_get_std_handle(n) for n in (0xfffffff6,0xfffffff5,0xfffffff4)])
native.tribes_startup_configure.argtypes=[ctypes.c_uint64,ctypes.c_uint16,ctypes.POINTER(ctypes.c_uint32)];native.tribes_startup_configure.restype=ctypes.c_int
assert native.tribes_startup_configure(host_base,1,launch_handles)
native.tribes_get_startup_info.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_get_startup_info.restype=None
command_line=b'"C:\\TribesPad\\Game\\Tribes2.exe"\0'
assert native.tribes_command_line_configure(host_base,command_line,len(command_line))
wide_command_line=command_line[:-1].decode('ascii').encode('utf-16-le')+b'\0\0'
assert native.tribes_command_line_configure_w(host_base,wide_command_line,len(wide_command_line))
native.tribes_environment_configure.argtypes=[ctypes.c_uint64,ctypes.c_void_p,ctypes.c_uint32];native.tribes_environment_configure.restype=ctypes.c_uint32
native.tribes_temp_directory_configure.argtypes=[ctypes.c_uint64,ctypes.c_char_p];native.tribes_temp_directory_configure.restype=ctypes.c_uint32
temp_environment=b'TMP=C:\\TribesPad\\Temp\0TEMP=C:\\TribesPad\\Temp\0\0'
assert native.tribes_environment_configure(host_base,temp_environment,len(temp_environment))
assert native.tribes_temp_directory_configure(host_base,b'C:\\Windows')
tls_functions={'TlsAlloc':('tribes_tls_alloc',0),'TlsFree':('tribes_tls_free',1),'TlsGetValue':('tribes_tls_get',1),'TlsSetValue':('tribes_tls_set',2)}
fls_functions={'FlsAlloc':('tribes_fls_alloc_index',1),'FlsGetValue':('tribes_fls_get_value',1),'FlsSetValue':('tribes_fls_set_value',2)}
export_names=[*tls_functions,'InitializeCriticalSectionEx',*fls_functions,'FlsFree','LCMapStringEx','GetSystemTimePreciseAsFileTime','GetTempPath2W','VirtualAlloc2FromApp','GetCurrentProcessorNumberEx','GetNumaProcessorNodeEx','GetNumaNodeProcessorMaskEx','GetNumaProcessorNode','AreFileApisANSI','CompareStringEx','EnumSystemLocalesEx','GetDateFormatEx','GetTimeFormatEx','GetLocaleInfoEx',*identifier_functions]
provider_exports=(NativeExport*len(export_names))(*[NativeExport(name.encode(),0,0xfffb0000+16*i) for i,name in enumerate(export_names)])
native.tribes_module_define_native.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_char_p,ctypes.POINTER(NativeExport),ctypes.c_uint32];native.tribes_module_define_native.restype=ctypes.c_int
assert native.tribes_module_define_native(host_base,kernel32,b'C:\\Windows\\System32\\kernel32.dll',provider_exports,len(export_names))
kernelbase=0x60020000
assert native.tribes_module_define_native(host_base,kernelbase,b'C:\\Windows\\System32\\kernelbase.dll',provider_exports,len(export_names))
ntdll=0x60000000
ntdll_exports=(NativeExport*1)(NativeExport(b'NtAllocateVirtualMemoryEx',0,0xfffb1000))
assert native.tribes_module_define_native(host_base,ntdll,b'C:\\Windows\\System32\\ntdll.dll',ntdll_exports,1)
provider_modules={'kernel32':kernel32,'kernelbase':kernelbase,'ntdll':ntdll}
provider_symbols={kernel32:set(export_names),kernelbase:set(export_names),ntdll:{'NtAllocateVirtualMemoryEx'}}
native.tribes_get_proc_address.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32];native.tribes_get_proc_address.restype=ctypes.c_uint32
native.tribes_critical_init_spin.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32];native.tribes_critical_init_spin.restype=ctypes.c_uint32
native.tribes_critical_init_ex.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32];native.tribes_critical_init_ex.restype=ctypes.c_uint32
critical_functions={'EnterCriticalSection':'tribes_critical_enter','LeaveCriticalSection':'tribes_critical_leave','DeleteCriticalSection':'tribes_critical_delete','TryEnterCriticalSection':'tribes_critical_try_enter'}
for helper in critical_functions.values():
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64,ctypes.c_uint32];fn.restype=ctypes.c_uint32
native.tribes_set_last_error.argtypes=[ctypes.c_uint32];native.tribes_set_last_error.restype=ctypes.c_uint32
native.tribes_get_process_heap.argtypes=[ctypes.c_uint64];native.tribes_get_process_heap.restype=ctypes.c_uint32
for helper in ('tribes_heap_alloc','tribes_heap_free'):
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32];fn.restype=ctypes.c_uint32
for helper,argc in tls_functions.values():
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint32]*argc;fn.restype=ctypes.c_uint32
# The x86 tracer cannot yet dispatch guest cleanup from a native callback.
# Free stops before mutation; native ARM64 dispatch is tested independently.
native.tribes_fls_fiber_create.argtypes=[ctypes.c_uint64,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p];native.tribes_fls_fiber_create.restype=ctypes.c_void_p
assert native.tribes_fls_fiber_create(host_base,None,None,None)
for helper,argc in fls_functions.values():
 fn=getattr(native,helper);fn.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*argc;fn.restype=ctypes.c_uint32
absent_diagnostic_exports={'FlsGetValue2'}
provider_names={export.name.decode() for export in [*provider_exports,*ntdll_exports]}
paths={'engine':R/'ref/control/Tribes2.exe','qol':R/'ref/product/IFC22.dll'};expected={'engine':'7ad1d40a6aa74a25d089af6bd6f34fcc20a172fd5932ded82c69fca673e7f8f9','qol':'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'};u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.reg_write(X.UC_X86_REG_MXCSR,0x1f80);images={};imports={};regions=[]
for name,path in paths.items():
 raw=path.read_bytes();assert hashlib.sha256(raw).hexdigest()==expected[name];pe=pefile.PE(data=raw);base=pe.OPTIONAL_HEADER.ImageBase;image=pe.get_memory_mapped_image();size=pe.OPTIONAL_HEADER.SizeOfImage;u.mem_map(base,(size+4095)&~4095);u.mem_write(base,image);images[name]=(pe,base,image)
 for section in pe.sections:
  if section.Characteristics&0x20000000:regions.append((name,base+section.VirtualAddress,base+section.VirtualAddress+max(section.Misc_VirtualSize,section.SizeOfRawData)))
 for dll in pe.DIRECTORY_ENTRY_IMPORT:
  for symbol in dll.imports:
   token=0xfffa0000+16*len(imports);imports[token]={'image':name,'dll':dll.dll.decode(),'name':symbol.name.decode() if symbol.name else '#'+str(symbol.ordinal),'slot':symbol.address};u.mem_write(symbol.address,struct.pack('<I',token))
native.tribes_module_register.argtypes=[ctypes.c_uint64,ctypes.c_uint32,ctypes.c_char_p,ctypes.c_int];native.tribes_module_register.restype=ctypes.c_int
registered_image_paths={'engine':'C:\\TribesPad\\Game\\Tribes2.exe','qol':'C:\\TribesPad\\Game\\IFC22.dll'}
for image_name,path in registered_image_paths.items():assert native.tribes_module_register(host_base,images[image_name][1],path.encode('ascii'),image_name=='engine')
module_availability=audit_absent_module('dbghelp.dll',[R/'ref/product',filesystem_root],[name+'.dll' for name in provider_modules]+list(registered_image_paths.values()))
native.tribes_module_define_absent.argtypes=[ctypes.c_uint64,ctypes.c_char_p];native.tribes_module_define_absent.restype=ctypes.c_int
assert native.tribes_module_define_absent(host_base,b'dbghelp.dll')
assert max(imports)<0xfffb0000
for export in [*provider_exports,*ntdll_exports]:imports[export.address]={'image':'native_provider','dll':'ntdll.dll' if export.address==0xfffb1000 else 'KERNEL32.dll','name':export.name.decode(),'slot':None}
u.mem_map(0xfffa0000,0x40000);u.mem_map(0x100000,0x200000);u.mem_map(0x3c0000,4096);u.mem_map(0 if a.diagnostic_zero_teb else 0x3d0000,4096);u.mem_map(0x3e0000,0x2000);u.mem_map(0x3f0000,0x10000)
# Explicit nonzero sparse TEB addressed through an actual x86 FS descriptor.
# Self must equal the FS base: original allocator uses it as thread identity.
# Only these fields are
# declared; full loader, TEB/SEH fault delivery and other threads are unproven.
process_id=os.getpid();thread_id=threading.get_native_id()&0xffffffff
teb_base=0 if a.diagnostic_zero_teb else 0x3d0000;gdt_base=0x3c0000;fs_selector=0x33
segment_profile=configure_sparse_teb(u,teb_base,gdt_base,0x300000,0x100000,process_id,thread_id,0x3e0000,allow_zero_identity=a.diagnostic_zero_teb)
descriptor=bytes.fromhex(segment_profile['fs_descriptor_hex'])
if not a.diagnostic_zero_teb:assert native.tribes_teb_configure(host_base,teb_base,0x300000,0x100000,process_id,thread_id,0x3e0000)
pe,base,image=images['qol'];t=pe.DIRECTORY_ENTRY_TLS.struct;template=bytes(u.mem_read(t.StartAddressOfRawData,t.EndAddressOfRawData-t.StartAddressOfRawData));u.mem_write(t.AddressOfIndex,struct.pack('<I',0));u.mem_write(0x3e0000,struct.pack('<I',0x3f0000));u.mem_write(0x3f0000,template+bytes(t.SizeOfZeroFill));callbacks=[]
for index in range(64):
 pointer=struct.unpack('<I',u.mem_read(t.AddressOfCallBacks+4*index,4))[0]
 if not pointer:break
 callbacks.append(pointer)
else:raise AssertionError('unterminated TLS callback table')
# Apply PE section permissions after loader writes/import/TLS initialization.
# Guest 4 KiB permissions are enforced by Unicorn, not Darwin's 16 KiB pages.
pages=GuestPages(u)
assert native.tribes_address_space_begin(host_base)
address_space_declarations=[]
for image_name,(image_pe,image_base,image_bytes) in images.items():
 assert image_pe.OPTIONAL_HEADER.SectionAlignment==4096
 image_size=(image_pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095
 assert native.tribes_address_space_declare(host_base,image_base,image_size,image_base,0x80,0x1000,1,0x1000000)
 address_space_declarations.append({'address':image_base,'bytes':image_size,'allocation':image_base,'allocation_protect':0x80,'state':0x1000,'initial_protect':1,'type':0x1000000,'source':'mapped pinned PE image under declared image allocation-protection profile'})
 u.mem_protect(image_base,(image_pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095,unicorn.UC_PROT_NONE)
 pages.declare(image_base,(image_pe.OPTIONAL_HEADER.SizeOfHeaders+4095)&~4095,2,image_name)
 assert native.tribes_address_space_protect(host_base,image_base,(image_pe.OPTIONAL_HEADER.SizeOfHeaders+4095)&~4095,2)
 for section in image_pe.sections:
  span=(max(section.Misc_VirtualSize,section.SizeOfRawData)+4095)&~4095
  if not span:continue
  flags=section.Characteristics;read=bool(flags&0x40000000);writeable=bool(flags&0x80000000);execute=bool(flags&0x20000000)
  protection=(0x40 if writeable else 0x20 if read else 0x10) if execute else 4 if writeable else 2 if read else 1
  pages.declare(image_base+section.VirtualAddress,span,protection,image_name)
  assert native.tribes_address_space_protect(host_base,image_base+section.VirtualAddress,span,protection)
for address,size,protection,reservation in [(0xfffa0000,0x40000,0x20,'synthetic_abi'),(gdt_base,4096,2,'gdt'),(teb_base,4096,4,'sparse_teb'),(0x100000,0x200000,4,'stack'),(0x3e0000,0x2000,4,'tls_array'),(0x3f0000,0x10000,4,'tls_block')]:
 pages.declare(address,size,protection,reservation)
 if address<0x30000000:
  assert native.tribes_address_space_declare(host_base,address,size,address,protection,0x1000,protection,0x20000)
  address_space_declarations.append({'address':address,'bytes':size,'allocation':address,'allocation_protect':protection,'state':0x1000,'initial_protect':protection,'type':0x20000,'source':reservation})
assert native.tribes_address_space_seal(host_base)
def native_reference_query(address):
 # Native-only scratch in the declared stack; original guest stack is untouched.
 assert native.tribes_virtual_query(host_base,address,0x100000,28)==28
 return struct.unpack('<7I',host_window[0x100000:0x10001c])
executable_reference=ExecutableReference(u,pages,native_reference_query)
reference_executions=[]
simd_sites={};simd_pending=[];decoded_simd={}
def finish_simd(completed):
 for site in simd_pending:
  site['completed' if completed else 'unconfirmed']+=1
 simd_pending.clear()
def observe_simd(uc,address,size):
 raw=bytes(uc.mem_read(address,size));key=(address,raw)
 if key not in decoded_simd:
  ins=next(decoder.disasm(raw,address),None)
  assert ins and ins.size==size,'executed instruction decode mismatch'
  vector=any(register in ins.op_str for register in ('xmm','ymm','zmm')) or ins.mnemonic in ('ldmxcsr','stmxcsr','vldmxcsr','vstmxcsr','vzeroupper','vzeroall')
  decoded_simd[key]=(ins.mnemonic,ins.op_str) if vector else None
 if decoded_simd[key] is not None:
  if key not in simd_sites:
   mnemonic,operands=decoded_simd[key]
   simd_sites[key]={'pc':address,'bytes_hex':raw.hex(),'mnemonic':mnemonic,'operands':operands,'entered':0,'completed':0,'unconfirmed':0}
  site=simd_sites[key];site['entered']+=1;simd_pending.append(site)
cache_reference=ReferenceInstructionCache(u,pages)
initial_pages_sha256=hashlib.sha256(json.dumps(sorted(pages.pages.items())).encode()).hexdigest()
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);recent=[];writes=[];write_attempts=[];pending_writes=[];data_writes=[];data_write_attempts=[];instructions=0;stop=None;stages=[];services=[]
locale_frames=[];locale_events=[]
once_frames=[];once_events=[]
once_registers=[X.UC_X86_REG_EAX,X.UC_X86_REG_EBX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI,X.UC_X86_REG_EBP,X.UC_X86_REG_EFLAGS]
heap_allocations={}
initialized_sections=set()
hardware=[];pending_hardware=[]
def finish_hardware(completed):
 for event in pending_hardware:
  event['instruction_completed']=completed
  event['outputs']={name:u.reg_read(register) for name,register in [('eax',X.UC_X86_REG_EAX),('ebx',X.UC_X86_REG_EBX),('ecx',X.UC_X86_REG_ECX),('edx',X.UC_X86_REG_EDX)]}
 pending_hardware.clear()
def finish_writes(completed):
 for event in pending_writes:
  event['instruction_completed']=completed
  try:event['observed_bytes']=bytes(u.mem_read(event['address'],event['size'])).hex()
  except unicorn.UcError:event['observed_bytes']=None
  if completed:(data_writes if event.get('category')=='image_data' else writes).append(event)
 pending_writes.clear()
binding_contracts=json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text())
def locale_schedule(uc,frame):
 index=ctypes.c_uint32(frame['index']);name=(ctypes.c_uint16*85)();flags=ctypes.c_uint32();event=frame['event']
 if not native.tribes_locale_next(event['wanted'],ctypes.byref(index),name,ctypes.byref(flags)):return False
 frame['index']=index.value;count=0
 while name[count]:count+=1
 raw=bytes(name)[:2*(count+1)];uc.mem_write(frame['scratch'],raw)
 frame['pending']={'name':raw[:-2].decode('utf-16-le'),'flags':flags.value}
 uc.mem_write(frame['top']-16,struct.pack('<4I',0xfffc0020,frame['scratch'],flags.value,event['parameter']))
 for register,value in frame['registers'].items():uc.reg_write(register,value)
 uc.reg_write(X.UC_X86_REG_ESP,frame['top']-16);uc.reg_write(X.UC_X86_REG_EIP,event['callback']);return True
def service(uc,entry,sp):
 # A narrow diagnostic implementation of the four security-cookie inputs.
 # Samples come from this host process; this is not native guest API execution.
 # Preserve registers/flags except documented scalar returns, and stdcall ESP.
 if entry['dll'].lower()!='kernel32.dll' and not (entry['dll'].lower()=='sdl3.dll' and entry['name']=='SDL_SetMemoryFunctions'):return False
 name=entry['name'];argc=0;result=None;output=None;value=None
 if name=='SDL_SetMemoryFunctions':
  argc=4;arguments=struct.unpack('<4I',uc.mem_read(sp+4,16))
  for pointer in arguments:
   if pointer:pages.require(pointer,1,unicorn.UC_PROT_EXEC)
  result=native.tribes_sdl_set_memory_functions(host_base,*arguments);current=(ctypes.c_uint32*4)();registered=native.tribes_sdl_memory_functions(host_base,current)
  if result:assert registered and list(current)==list(arguments)
  value={'registered':bool(registered),'callbacks':list(current),'scope':'Actual native owned guest allocator table; callback consumers tested separately in ARM64 fixtures, no native host-SDL allocator replacement'}
 elif name=='InitOnceInitialize':
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0];pages.require(output,4,unicorn.UC_PROT_WRITE)
  host_window[output:output+4]=uc.mem_read(output,4);native.tribes_once_initialize(host_base,output);uc.mem_write(output,host_window[output:output+4]);value={'initialized_word':0}
 elif name=='InitOnceExecuteOnce':
  argc=4;once,callback,parameter,context_out=struct.unpack('<4I',uc.mem_read(sp+4,16))
  if not callback:result=0;native.tribes_set_last_error(87)
  else:
   pages.require(once,4,unicorn.UC_PROT_READ|unicorn.UC_PROT_WRITE);pages.require(callback,1,unicorn.UC_PROT_EXEC)
   if context_out:
    pages.require(context_out,4,unicorn.UC_PROT_WRITE)
    if context_out<once+4 and once<context_out+4:raise ValueError('once context output aliases object')
   host_window[once:once+4]=uc.mem_read(once,4);context=ctypes.c_uint32();ownership=native.tribes_once_begin(host_base,once,ctypes.byref(context));uc.mem_write(once,host_window[once:once+4])
   if ownership==1:
    scratch=sp-128;top=scratch-64;pages.require(top-16,scratch+4-(top-16),unicorn.UC_PROT_WRITE)
    uc.mem_write(scratch,bytes(4));uc.mem_write(top-16,struct.pack('<4I',0xfffc0010,once,parameter,scratch))
    target=struct.unpack('<I',uc.mem_read(sp,4))[0]
    event={'once':once,'callback':callback,'parameter':parameter,'context_output':context_out,'return_pc':target,'state':'callback pending','scope':'Original x86 callback with native C once ownership'};once_events.append(event)
    once_frames.append({'registers':{r:uc.reg_read(r) for r in once_registers},'sp':sp,'top':top,'scratch':scratch,'event':event})
    uc.reg_write(X.UC_X86_REG_ESP,top-16);uc.reg_write(X.UC_X86_REG_EIP,callback);return True
   result=1 if ownership==2 else 0
   if result and context_out:uc.mem_write(context_out,struct.pack('<I',context.value))
   value={'once':once,'ownership':ownership,'context':context.value}
 elif name=='GetCurrentProcessId':result=process_id
 elif name=='GetCurrentThreadId':result=thread_id
 elif name=='GetCurrentProcess':result=native.tribes_get_current_process()
 elif name=='GetCurrentThread':result=native.tribes_get_current_thread()
 elif name in srw_functions:
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0];pages.require(output,4,unicorn.UC_PROT_READ|unicorn.UC_PROT_WRITE)
  host_window[output:output+4]=uc.mem_read(output,4);result=getattr(native,srw_functions[name])(host_base,output)
  uc.mem_write(output,host_window[output:output+4]);value={'bytes_hex':host_window[output:output+4].hex(),'last_error':native.tribes_get_last_error(),'native_live_objects':native.tribes_srw_live_count()}
 elif name=='Sleep':
  argc=1;milliseconds=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if milliseconds>=60000:return False
  started=time.monotonic_ns();native.tribes_sleep(milliseconds)
  value={'milliseconds':milliseconds,'elapsed_ns':time.monotonic_ns()-started,'last_error':native.tribes_get_last_error(),'scope':'Actual finite native delay; no message/APC pumping'}
 elif name in ('GetStdHandle','GetFileType'):
  argc=1;argument=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  result=(native.tribes_get_std_handle if name=='GetStdHandle' else native.tribes_get_file_type)(argument);value=argument
 elif name=='GetACP':result=native.tribes_get_acp()
 elif name=='GetOEMCP':result=native.tribes_get_oemcp()
 elif name=='IsValidCodePage':
  argc=1;page=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if page not in (1252,437,65001):return False
  result=native.tribes_is_valid_code_page(page);value=page
 elif name=='GetCPInfo':
  argc=2;page,output=struct.unpack('<2I',uc.mem_read(sp+4,8))
  if page not in (0,1,1252,437,65001):return False
  pages.require(output,20,unicorn.UC_PROT_WRITE)
  result=native.tribes_get_cp_info(host_base,page,output)
  if result:uc.mem_write(output,host_window[output:output+20])
  value={'page':page,'bytes_hex':host_window[output:output+20].hex()}
 elif name=='MultiByteToWideChar':
  argc=6;page,flags,pointer,count,output,capacity=struct.unpack('<6I',uc.mem_read(sp+4,24))
  if page not in (0,1252,65001) or flags not in ((0,8) if page==65001 else (0,1,8,9)) or not (0<count<=1048576 or count==0xffffffff) or capacity>1048576:return False
  if count==0xffffffff:
   raw=bytearray()
   for i in range(1048576):
    pages.require(pointer+i,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(pointer+i,1));raw.extend(byte)
    if byte==b'\0':break
   else:return False
   raw=bytes(raw)
  else:pages.require(pointer,count,unicorn.UC_PROT_READ);raw=bytes(uc.mem_read(pointer,count))
  host_window[pointer:pointer+len(raw)]=raw
  if capacity and pointer!=output and pointer<output+capacity*2 and output<pointer+len(raw):return False
  if capacity and output:pages.require(output,capacity*2,unicorn.UC_PROT_WRITE)
  result=native.tribes_multibyte_to_wide(host_base,page,flags,pointer,count,output,capacity)
  if result and capacity:uc.mem_write(output,host_window[output:output+result*2])
  value={'page':page,'flags':flags,'bytes':len(raw),'count_argument':count,'capacity':capacity,'input_sha256':hashlib.sha256(raw).hexdigest(),'output_hex':host_window[output:output+result*2].hex() if result and capacity else None,'last_error':native.tribes_get_last_error()}
 elif name=='WideCharToMultiByte':
  argc=8;args=list(struct.unpack('<8I',uc.mem_read(sp+4,32)));page,flags,pointer,count,output,capacity,default,used=args
  # Actual current request has no replacement/BOOL pointers. Other profiles
  # stay at the exact boundary until the tracer validates those guest ranges.
  if page not in (0,1252,65001) or flags not in ((0,0x80) if page==65001 else (0,0x400)) or not (0<count<=1048576 or count==0xffffffff) or capacity>1048576 or default or used:return False
  if count==0xffffffff:
   raw=bytearray()
   for index in range(1048576):
    pages.require(pointer+index*2,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(pointer+index*2,2));raw.extend(pair)
    if pair==b'\0\0':break
   else:return False
   raw=bytes(raw);units=len(raw)//2
  else:
   units=count;pages.require(pointer,units*2,unicorn.UC_PROT_READ);raw=bytes(uc.mem_read(pointer,units*2))
  if page!=65001 and any(0xd800<=ch<=0xdfff for ch in struct.unpack('<'+'H'*units,raw)):return False
  written=min(capacity,units*(4 if page==65001 else 1))
  if capacity and pointer!=output and pointer<output+written and output<pointer+units*2:return False
  host_window[pointer:pointer+units*2]=raw
  if written and output:pages.require(output,written,unicorn.UC_PROT_WRITE)
  result=native.tribes_wide_to_multibyte(host_base,*args)
  if page==65001:written=result if result and capacity else 0
  if written and output:uc.mem_write(output,host_window[output:output+written])
  value={'page':page,'flags':flags,'units':units,'count_argument':count,'capacity':capacity,'input_hex':raw.hex(),'output_hex':host_window[output:output+written].hex() if written and output else None,'reference':'Unicode scalar UTF-8 conversion' if page==65001 else 'Pinned Microsoft CP1252 WCTABLE'}
 elif name=='GetStringTypeW':
  argc=4;kind,pointer,count,output=struct.unpack('<4I',uc.mem_read(sp+4,16))
  if kind not in (1,2,4) or not 0<count<=1048576:return False
  if pointer!=output and pointer<output+count*2 and output<pointer+count*2:return False
  pages.require(pointer,count*2,unicorn.UC_PROT_READ);pages.require(output,count*2,unicorn.UC_PROT_WRITE)
  raw=bytes(uc.mem_read(pointer,count*2));host_window[pointer:pointer+count*2]=raw
  result=native.tribes_get_string_type_w(host_base,kind,pointer,count,output)
  if result:uc.mem_write(output,host_window[output:output+count*2])
  value={'kind':kind,'code_units':count,'reference':'Wine 10.0 NLS classification data; not a Windows DLL reference','input_sha256':hashlib.sha256(raw).hexdigest(),'output_hex':host_window[output:output+count*2].hex() if result else None}
 elif name=='EnumSystemLocalesEx':
  argc=4;callback,wanted,parameter,reserved=struct.unpack('<4I',uc.mem_read(sp+4,16));result=native.tribes_locale_enum_validate(callback,wanted,reserved)
  if result:
   pages.require(callback,1,unicorn.UC_PROT_EXEC);scratch=sp-256;top=scratch-64;pages.require(top-16,170+scratch-(top-16),unicorn.UC_PROT_WRITE)
   target=struct.unpack('<I',uc.mem_read(sp,4))[0];event={'callback':callback,'wanted':wanted,'parameter':parameter,'return_pc':target,'state':'enumeration pending','callbacks':[],'scope':'Pinned native Wine locale profile with original x86 guest callbacks'};locale_events.append(event)
   frame={'registers':{r:uc.reg_read(r) for r in once_registers},'sp':sp,'top':top,'scratch':scratch,'index':0,'event':event}
   if locale_schedule(uc,frame):locale_frames.append(frame);return True
   event.update(state='enumeration complete',eax=1);value=event
  else:value={'last_error':native.tribes_get_last_error()}
 elif name in identifier_functions:
  helper,argc=identifier_functions[name];args=list(struct.unpack('<'+str(argc)+'I',uc.mem_read(sp+4,argc*4)));locale_name=None;output=None;capacity=0
  if name in ('IsValidLocaleName','LocaleNameToLCID') and args[0]:
   pointer=args[0];raw=bytearray()
   for i in range(85):
    pages.require(pointer+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(pointer+2*i,2));raw.extend(pair)
    if pair==b'\0\0':break
   else:return False
   host_window[pointer:pointer+len(raw)]=raw;locale_name=raw[:-2].decode('utf-16-le')
  if name=='GetUserDefaultLocaleName':output,capacity=args
  elif name=='LCIDToLocaleName':output,capacity=args[1:3]
  if capacity>1048576:return False
  if capacity:
   pages.require(output,capacity*2,unicorn.UC_PROT_WRITE);host_window[output:output+capacity*2]=uc.mem_read(output,capacity*2)
  result=getattr(native,helper)(host_base,*args)
  if capacity and result:uc.mem_write(output,host_window[output:output+result*2])
  value={'arguments':args,'locale_name':locale_name,'last_error':native.tribes_get_last_error(),'output_hex':host_window[output:output+result*2].hex() if capacity and result else None,'reference':'Pinned Wine 10.0 name/LCID indexes; declared en-US defaults'}
 elif name=='GetLocaleInfoEx':
  argc=4;args=list(struct.unpack('<4I',uc.mem_read(sp+4,16)));locale,kind,output,capacity=args
  if capacity>1048576:return False
  locale_name=None
  if locale:
   raw=bytearray()
   for i in range(85):
    pages.require(locale+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(locale+2*i,2));raw.extend(pair)
    if pair==b'\0\0':break
   else:return False
   host_window[locale:locale+len(raw)]=raw;locale_name=raw[:-2].decode('utf-16-le')
  if capacity:
   pages.require(output,capacity*2,unicorn.UC_PROT_WRITE);host_window[output:output+capacity*2]=uc.mem_read(output,capacity*2)
  result=native.tribes_get_locale_info_ex(host_base,*args)
  if capacity and result:uc.mem_write(output,host_window[output:output+result*2])
  value={'locale':locale_name,'type':kind,'capacity':capacity,'last_error':native.tribes_get_last_error(),'output_hex':host_window[output:output+result*2].hex() if capacity and result else None,'reference':'Pinned immutable Wine 10.0 locale information; no registry overrides'}
 elif name in ('GetDateFormatEx','GetTimeFormatEx'):
  argc=7 if name=='GetDateFormatEx' else 6;args=list(struct.unpack('<'+str(argc)+'I',uc.mem_read(sp+4,argc*4)));locale,flags,date,format,output,capacity=args[:6]
  if capacity>1048576:return False
  inputs=[]
  for pointer,limit in [(locale,85),(format,4096)]:
   if not pointer:continue
   raw=bytearray()
   for i in range(limit):
    pages.require(pointer+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(pointer+2*i,2));raw.extend(pair)
    if pair==b'\0\0':break
   else:return False
   host_window[pointer:pointer+len(raw)]=raw;inputs.append({'pointer':pointer,'utf16le_hex':raw.hex()})
  if date:
   pages.require(date,16,unicorn.UC_PROT_READ);host_window[date:date+16]=uc.mem_read(date,16)
  if capacity:
   pages.require(output,capacity*2,unicorn.UC_PROT_WRITE);host_window[output:output+capacity*2]=uc.mem_read(output,capacity*2)
  helper=native.tribes_get_date_format_ex if argc==7 else native.tribes_get_time_format_ex
  result=helper(host_base,*args)
  if capacity:uc.mem_write(output,host_window[output:output+capacity*2])
  value={'arguments':args,'inputs':inputs,'systemtime_hex':host_window[date:date+16].hex() if date else None,'output_hex':host_window[output:output+capacity*2].hex() if capacity else None,'last_error':native.tribes_get_last_error(),'reference':'Pinned Wine 10.0 Gregorian presentation data and native local clock'}
 elif name=='CompareStringEx':
  argc=9;args=list(struct.unpack('<9I',uc.mem_read(sp+4,36)));locale,flags,pointer1,count1,pointer2,count2,version,reserved,handle=args
  if version or reserved or handle:return False
  locale_name=None
  if locale:
   raw=bytearray()
   for i in range(85):
    pages.require(locale+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(locale+2*i,2));raw.extend(pair)
    if pair==b'\0\0':break
   else:return False
   locale_name=raw[:-2].decode('utf-16-le')
   if locale_name.lower() not in ('','en','en-us','!x-sys-default-locale'):return False
   host_window[locale:locale+len(raw)]=raw
  inputs=[]
  for pointer,count in [(pointer1,count1),(pointer2,count2)]:
   if pointer<0x10000:return False
   if count&0x80000000:
    raw=bytearray()
    for i in range(1048576):
     pages.require(pointer+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(pointer+2*i,2));raw.extend(pair)
     if pair==b'\0\0':break
    else:return False
    raw=bytes(raw)
   else:
    if count>1048576:return False
    if count:pages.require(pointer,2*count,unicorn.UC_PROT_READ)
    raw=bytes(uc.mem_read(pointer,2*count)) if count else b''
   host_window[pointer:pointer+len(raw)]=raw;inputs.append({'pointer':pointer,'count':count,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'utf16le_hex':raw.hex() if len(raw)<=512 else None})
  result=native.tribes_compare_string_ex(host_base,*args);value={'locale':locale_name,'flags':flags,'inputs':inputs,'last_error':native.tribes_get_last_error(),'reference':'Pinned Wine 10.0 default NLS sort profile'}
 elif name=='LCMapStringEx':
  argc=9;args=list(struct.unpack('<9I',uc.mem_read(sp+4,36)));locale,flags,pointer,count,output,capacity,version,reserved,handle=args
  if flags not in (0x100,0x200) or not 0<count<=1048576 or capacity>1048576 or version or reserved or handle:return False
  locale_name=None
  if locale:
   units=[]
   for i in range(85):
    pages.require(locale+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(locale+2*i,2));units.append(pair)
    if pair==b'\0\0':break
   else:return False
   text=b''.join(units);locale_name=text[:-2].decode('utf-16-le')
   if locale_name.lower() not in ('','en-us','en'):return False
   host_window[locale:locale+len(text)]=text
  pages.require(pointer,count*2,unicorn.UC_PROT_READ);raw=bytes(uc.mem_read(pointer,count*2))
  units=struct.unpack('<'+'H'*count,raw)
  if any(0xd800<=a<=0xdbff and 0xdc00<=b<=0xdfff for a,b in zip(units,units[1:])):return False
  if capacity and pointer!=output and pointer<output+count*2 and output<pointer+count*2:return False
  host_window[pointer:pointer+count*2]=raw
  if capacity>=count and output:pages.require(output,count*2,unicorn.UC_PROT_WRITE)
  result=native.tribes_lcmap_string_ex(host_base,*args)
  if result and capacity:uc.mem_write(output,host_window[output:output+result*2])
  value={'locale':locale_name,'flags':flags,'units':units,'count_argument':count,'capacity':capacity,'input_hex':raw.hex(),'output_hex':host_window[output:output+result*2].hex() if result and capacity else None,'reference':'Wine 10.0 default BMP case maps'}
 elif name=='InitializeSListHead':
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if not output or output&7:return False
  pages.require(output,8,unicorn.UC_PROT_WRITE)
  before=bytes(uc.mem_read(output,8));host_window[output:output+8]=before
  native.tribes_initialize_slist_head(host_base,output);uc.mem_write(output,host_window[output:output+8])
  value={'head':output,'before_hex':before.hex(),'header_hex':host_window[output:output+8].hex(),'layout':'x86 Next DWORD, Depth WORD, Sequence WORD; initialized exclusively'}
 elif name=='GetProcessHeap':result=native.tribes_get_process_heap(host_base)
 elif name in ('GetStartupInfoA','GetStartupInfoW'):
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  pages.require(output,68,unicorn.UC_PROT_WRITE)
  native.tribes_get_startup_info(host_base,output)
  data=host_window[output:output+68];uc.mem_write(output,data)
  value={'show':1,'initial_handles':list(launch_handles),'bytes_hex':data.hex(),'string_pointers':'NULL'}
 elif name=='GetEnvironmentStringsW':
  result=native.tribes_get_environment_strings_w(host_base)
  if result:
   logical=native.tribes_environment_snapshot_size(host_base,result,1);span=native.tribes_global_size(host_base,result);assert logical and span and result%4096==0 and span%4096==0
   assert all(page not in pages.pages for page in range(result,result+span,4096))
   uc.mem_map(result,span,unicorn.UC_PROT_READ|unicorn.UC_PROT_WRITE);uc.mem_write(result,host_window[result:result+span]);pages.declare(result,span,2,('environment_snapshot',result))
   environment_snapshots[result]=(logical,span)
   value={'pointer':result,'logical_bytes':logical,'backing_bytes':span,'utf16_hex':host_window[result:result+logical].hex(),'profile':'Owned native CP437-decoded snapshot; Unicorn read-only pages'}
 elif name=='FreeEnvironmentStringsW':
  argc=1;pointer=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if pointer not in environment_snapshots:return False
  logical,span=environment_snapshots[pointer];result=native.tribes_free_environment_strings_w(host_base,pointer)
  if result:
   uc.mem_unmap(pointer,span)
   for page in range(pointer,pointer+span,4096):del pages.pages[page]
   del environment_snapshots[pointer]
  value={'pointer':pointer,'logical_bytes':logical,'released':bool(result)}
 elif name in ('GetCommandLineA','GetCommandLineW'):
  text=command_line if name=='GetCommandLineA' else wide_command_line
  result=(native.tribes_get_command_line if name=='GetCommandLineA' else native.tribes_get_command_line_w)(host_base)
  if result:
   start=result&~4095;end=(result+len(text)+4095)&~4095
   for page in range(start,end,4096):
    if page not in pages.pages:
     uc.mem_map(page,4096,unicorn.UC_PROT_READ|unicorn.UC_PROT_WRITE);pages.declare(page,4096,4,'global_heap')
   pages.require(result,len(text),unicorn.UC_PROT_WRITE)
   uc.mem_write(result,host_window[result:result+len(text)])
  value=command_line[:-1].decode('ascii')
 elif name=='HeapAlloc':
  heap,flags,size=struct.unpack('<3I',uc.mem_read(sp+4,12))
  if flags not in (0,8):return False
  argc=3;result=native.tribes_heap_alloc(host_base,heap,flags,size);value={'heap':heap,'flags':flags,'bytes':size}
  if result:
   span=(max(size,1)+7)&~7;heap_allocations[result]=span
   page_size=os.sysconf('SC_PAGE_SIZE');start=result&~(page_size-1);end=(result+span+page_size-1)&~(page_size-1)
   for page in range(start,end,4096):
    if page not in pages.pages:
     uc.mem_map(page,4096,unicorn.UC_PROT_READ|unicorn.UC_PROT_WRITE);pages.declare(page,4096,4,'process_heap')
   pages.require(result,span,unicorn.UC_PROT_WRITE)
   uc.mem_write(result,host_window[result:result+span])
 elif name=='HeapFree':
  heap,flags,pointer=struct.unpack('<3I',uc.mem_read(sp+4,12))
  if flags or pointer not in heap_allocations:return False
  argc=3;result=native.tribes_heap_free(host_base,heap,flags,pointer);value={'heap':heap,'pointer':pointer}
  if result:del heap_allocations[pointer]
 elif name=='LoadLibraryA':
  argc=1;pointer=struct.unpack('<I',uc.mem_read(sp+4,4))[0];raw=bytearray()
  if not pointer:return False
  for i in range(4096):
   pages.require(pointer+i,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(pointer+i,1));raw.extend(byte)
   if byte==b'\0':break
  else:return False
  if not raw[:-1].isascii():return False
  module_name=raw[:-1].decode('ascii');key=module_name.lower();key=key[:-4] if key.endswith('.dll') else key
  declared_absent=module_name.casefold() in ('dbghelp','dbghelp.dll')
  if key not in provider_modules and not declared_absent:return False
  host_window[pointer:pointer+len(raw)]=raw;result=native.tribes_load_library(host_base,pointer)
  if declared_absent:
   assert result==0 and native.tribes_get_last_error()==126
   value={'name':module_name,'reference_added':False,'absent':True,'last_error':native.tribes_get_last_error(),'availability_auditor_sha256':module_availability['auditor_sha256'],'scope':'Actual native loader failure for audited absent debugging DLL; no exports or image published'}
  else:
   assert result==(provider_modules[key] if key else images['engine'][1])
   value={'name':module_name,'reference_added':True,'provider':'actual registered native module'}
 elif name=='LoadLibraryExW':
  pointer,file,flags=struct.unpack('<3I',uc.mem_read(sp+4,12))
  if file or not pointer or flags not in (0,0x800):return False
  if flags==0:
   raw=bytes(uc.mem_read(pointer,512));end=next((i+2 for i in range(0,512,2) if raw[i:i+2]==b'\0\0'),None)
   if end is None:raise ValueError('unterminated module name')
   module_name=raw[:end-2].decode('utf-16-le').lower()
   module_name=module_name[:-4] if module_name.endswith('.dll') else module_name
   if module_name not in provider_modules:return False
   host_window[pointer:pointer+end]=raw[:end]
  argc=3;result=native.tribes_load_library_ex_w(host_base,pointer,file,flags);value=native.tribes_get_last_error()
  assert (result==0 and value==87) if flags else result==provider_modules[module_name]
 elif name=='SetUnhandledExceptionFilter':
  argc=1;handler=struct.unpack('<I',uc.mem_read(sp+4,4))[0];result=native.tribes_set_exception_filter(handler)
  value={'handler':handler,'previous':result,'registered_filter':native.tribes_exception_filter(),'scope':'Existing actual native atomic registration; one guest process per native runtime; fault-to-guest SEH delivery remains unfinished'}
 elif name=='SetConsoleCtrlHandler':
  argc=2;handler,add=struct.unpack('<2I',uc.mem_read(sp+4,8));result=native.tribes_set_console_ctrl_handler(host_base,handler,add)
  snapshot=ctypes.c_void_p();assert native.tribes_console_begin(host_base,1,ctypes.byref(snapshot));handlers=[]
  while True:
   address=native.tribes_console_next(snapshot)
   if not address:break
   handlers.append(address)
  native.tribes_console_end(snapshot);assert native.tribes_console_begin(host_base,0,ctypes.byref(snapshot));ignored=bool(native.tribes_console_ignored(snapshot));native.tribes_console_end(snapshot)
  value={'handler':handler,'add':bool(add),'registered_handlers_lifo':handlers,'ctrl_c_ignored':ignored,'last_error':native.tribes_get_last_error(),'scope':'Actual native process-owned registration; no event or callback execution in this startup capture'}
 elif name=='SetCurrentDirectoryA':
  argc=1;pointer=struct.unpack('<I',uc.mem_read(sp+4,4))[0];raw=bytearray()
  if pointer:
   for i in range(os.pathconf('/', 'PC_PATH_MAX')):
    pages.require(pointer+i,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(pointer+i,1));raw.extend(byte)
    if byte==b'\0':break
   else:return False
   host_window[pointer:pointer+len(raw)]=raw
  result=native.tribes_set_current_directory(host_base,pointer)
  value={'input_hex':raw.hex(),'path':raw[:-1].decode('ascii') if raw and raw[:-1].isascii() else None,'last_error':native.tribes_get_last_error(),'mounts':{'volume':str(filesystem_root.relative_to(R)),'game':'ref/product','temp':str(filesystem_temp.relative_to(R))}}
 elif name=='GetLastError':result=native.tribes_get_last_error()
 elif name=='SetLastError':
  argc=1;value=struct.unpack('<I',uc.mem_read(sp+4,4))[0];native.tribes_set_last_error(value)
 elif name in critical_functions:
  argc=1;pointer=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if pointer not in initialized_sections:return False
  value=pointer;answer=getattr(native,critical_functions[name])(host_base,pointer)
  if name=='TryEnterCriticalSection':result=answer
  if name=='DeleteCriticalSection':initialized_sections.remove(pointer)
 elif name=='FreeLibrary':
  argc=1;module=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if module not in provider_modules.values():return False
  native.tribes_free_library.argtypes=[ctypes.c_uint64,ctypes.c_uint32];native.tribes_free_library.restype=ctypes.c_uint32
  result=native.tribes_free_library(host_base,module);value={'module':module,'reference_released':bool(result),'provider':'actual registered native module'}
 elif name in topology_functions:
  helper,argc,size=topology_functions[name];args=struct.unpack('<'+'I'*argc,uc.mem_read(sp+4,4*argc));output=args[-1]
  if name=='GetNumaProcessorNodeEx' and args[0]:
   pages.require(args[0],4,unicorn.UC_PROT_READ);host_window[args[0]:args[0]+4]=uc.mem_read(args[0],4)
  if output:pages.require(output,size,unicorn.UC_PROT_WRITE);host_window[output:output+size]=uc.mem_read(output,size)
  result=getattr(native,helper)(host_base,*args)
  if output:uc.mem_write(output,host_window[output:output+size])
  value={'arguments':list(args),'bytes_hex':host_window[output:output+size].hex() if output else None,'last_error':native.tribes_get_last_error(),'profile':'One guest group / UMA node; current CPU uses actual Apple sample'}
 elif name=='GetSystemInfo':
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0];pages.require(output,36,unicorn.UC_PROT_WRITE)
  native.tribes_get_system_info(host_base,output);raw=host_window[output:output+36];uc.mem_write(output,raw)
  words=struct.unpack('<9I',raw)
  value={'bytes_hex':raw.hex(),'architecture':words[0]&0xffff,'page_size':words[1],'minimum_address':words[2],'maximum_address':words[3],'processor_mask':words[4],'processors':words[5],'processor_type':words[6],'allocation_granularity':words[7],'level':words[8]&0xffff,'revision':words[8]>>16,'profile':'Logical x86 guest geometry; larger Apple backing pages and native subpage permissions are separate'}
 elif name=='GetEnvironmentVariableA':
  argc=3;pointer,output,capacity=struct.unpack('<3I',uc.mem_read(sp+4,12));raw=bytearray()
  if not pointer:return False
  for i in range(32767):
   pages.require(pointer+i,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(pointer+i,1));raw.extend(byte)
   if byte==b'\0':break
  else:return False
  if not raw[:-1].isascii():return False
  host_window[pointer:pointer+len(raw)]=raw
  required=native.tribes_get_environment_variable_a(host_base,pointer,0,0)
  if required and capacity>=required:pages.require(output,required,unicorn.UC_PROT_WRITE)
  result=native.tribes_get_environment_variable_a(host_base,pointer,output,capacity)
  if required and capacity>=required:uc.mem_write(output,host_window[output:output+required])
  value={'name':raw[:-1].decode('ascii'),'capacity':capacity,'required':required,'last_error':native.tribes_get_last_error(),'output_hex':host_window[output:output+required].hex() if required and capacity>=required else None,'source':'explicit configured guest environment'}
 elif name in ('GetTempPathW','GetTempPath2W'):
  argc=2;capacity,output=struct.unpack('<2I',uc.mem_read(sp+4,8))
  required=native.tribes_get_temp_path_w(host_base,0,0)
  if capacity>=required:pages.require(output,required*2,unicorn.UC_PROT_WRITE)
  result=native.tribes_get_temp_path_w(host_base,capacity,output)
  if result and result<capacity:uc.mem_write(output,host_window[output:output+(result+1)*2])
  value={'capacity':capacity,'required':required,'path':'C:\\TribesPad\\Temp\\','profile':'configured ordinary non-SYSTEM environment; no host path exposed'}
 elif name=='GetSystemTimePreciseAsFileTime':
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0];pages.require(output,8,unicorn.UC_PROT_WRITE)
  native.tribes_system_file_time(host_base,output);uc.mem_write(output,host_window[output:output+8]);value=struct.unpack('<Q',host_window[output:output+8])[0]
 elif name=='AreFileApisANSI':
  result=native.tribes_are_file_apis_ansi();value={'mode':'ANSI','code_page':native.tribes_get_acp(),'filename_scope':'ASCII guest namespace; OEM switching not published'}
 elif name in ('GetModuleFileNameA','GetModuleFileNameW'):
  argc=3;module,output,capacity=struct.unpack('<3I',uc.mem_read(sp+4,12))
  width=2 if name.endswith('W') else 1
  if capacity:pages.require(output,capacity*width,unicorn.UC_PROT_WRITE)
  helper=native.tribes_get_module_filename_w if width==2 else native.tribes_get_module_filename
  result=helper(host_base,module,output,capacity);written=(result+1 if result<capacity else capacity)*width if result else 0
  if written:uc.mem_write(output,host_window[output:output+written])
  value={'module':module,'capacity_units':capacity,'unit_bytes':width,'last_error':native.tribes_get_last_error(),'path':host_window[output:output+written].decode('utf-16-le' if width==2 else 'cp1252').rstrip('\0') if written else None,'registry':'Declared loaded guest images/providers; modern truncation profile'}
 elif name=='GetModuleHandleA':
  argc=1;pointer=struct.unpack('<I',uc.mem_read(sp+4,4))[0];module_name=None
  if pointer:
   raw=bytearray()
   for i in range(4096):
    pages.require(pointer+i,1,unicorn.UC_PROT_READ);unit=bytes(uc.mem_read(pointer+i,1));raw.extend(unit)
    if unit==b'\0':break
   else:return False
   if not raw[:-1].isascii():return False
   module_name=raw[:-1].decode('ascii');host_window[pointer:pointer+len(raw)]=raw
  result=native.tribes_get_module_handle(host_base,pointer);value={'name':module_name,'reference_added':False,'last_error':native.tribes_get_last_error()}
 elif name=='GetModuleHandleW':
  argc=1;pointer=struct.unpack('<I',uc.mem_read(sp+4,4))[0];module_name=None
  if pointer:
   raw=bytearray()
   for i in range(4096):
    pages.require(pointer+2*i,2,unicorn.UC_PROT_READ);pair=bytes(uc.mem_read(pointer+2*i,2));raw.extend(pair)
    if pair==b'\0\0':break
   else:return False
   module_name=raw[:-2].decode('utf-16-le')
   if not module_name.isascii():return False
   key=module_name.lower();key=key[:-4] if key.endswith('.dll') else key
   if key not in provider_modules:return False
   host_window[pointer:pointer+len(raw)]=raw
  else:key=None # NULL refers to the registered original main image
  result=native.tribes_get_module_handle_w(host_base,pointer);value={'name':module_name,'reference_added':False}
  assert result==(provider_modules[key] if key else images['engine'][1])
 elif name=='GetProcAddress':
  module,pointer=struct.unpack('<2I',uc.mem_read(sp+4,8))
  if module not in provider_modules.values() or pointer<=0xffff:return False
  raw=bytes(uc.mem_read(pointer,256));symbol=raw.split(b'\0',1)[0].decode('ascii')
  if b'\0' not in raw:raise ValueError('unterminated export name')
  if symbol not in provider_symbols[module] and (module==ntdll or symbol not in absent_diagnostic_exports):return False
  host_window[pointer:pointer+len(symbol)+1]=raw[:len(symbol)+1]
  argc=2;result=native.tribes_get_proc_address(host_base,module,pointer);value=symbol
  assert bool(result)==(symbol in provider_symbols[module])
 elif name in fls_functions:
  helper,argc=fls_functions[name];args=struct.unpack('<'+'I'*argc,uc.mem_read(sp+4,4*argc))
  if name=='FlsAlloc' and args[0]:pages.require(args[0],1,unicorn.UC_PROT_EXEC)
  result=getattr(native,helper)(host_base,*args);value=list(args)
 elif name in tls_functions:
  helper,argc=tls_functions[name];args=struct.unpack('<'+'I'*argc,uc.mem_read(sp+4,4*argc)) if argc else ()
  result=getattr(native,helper)(*args);value=list(args)
 elif name in ('InitializeCriticalSectionAndSpinCount','InitializeCriticalSectionEx'):
  output,spin=struct.unpack('<2I',uc.mem_read(sp+4,8))
  if not output or output>0xffffffe8:raise ValueError('invalid critical section range')
  pages.require(output,24,unicorn.UC_PROT_WRITE)
  host_window[output:output+24]=bytes(uc.mem_read(output,24))
  if name=='InitializeCriticalSectionEx':
   flags=struct.unpack('<I',uc.mem_read(sp+12,4))[0]
   if flags:return False
   argc=3;result=native.tribes_critical_init_ex(host_base,output,spin,flags);value={'spin':spin,'flags':flags}
  else:argc=2;result=native.tribes_critical_init_spin(host_base,output,spin);value=spin
  if result:
   initialized_sections.add(output);uc.mem_write(output,host_window[output:output+24])
 elif name=='VirtualAlloc2FromApp':
  argc=7;arguments=struct.unpack('<7I',uc.mem_read(sp+4,28));process,address,size,kind,protection,parameters,count=arguments
  # New reserve or reserve+commit; existing-region commit uses the classic
  # synchronization branch below. Free/NT remain explicit boundaries.
  if kind not in (0x2000,0x3000):return False
  if kind==0x3000 and (protection not in (2,4) or size%os.sysconf('SC_PAGE_SIZE')):return False
  if count>1:return False
  if count:
   pages.require(parameters,16,unicorn.UC_PROT_READ);raw=bytes(uc.mem_read(parameters,16));parameter_kind,payload=struct.unpack('<2Q',raw)
   if parameter_kind!=1 or not 0<payload<=0xffffffff:return False
   pages.require(payload,12,unicorn.UC_PROT_READ);requirements=bytes(uc.mem_read(payload,12))
   host_window[parameters:parameters+16]=raw;host_window[payload:payload+12]=requirements
  result=native.tribes_virtual_alloc2_from_app(host_base,*arguments)
  value={'arguments':list(arguments),'last_error':native.tribes_get_last_error(),'scope':'Actual native FromApp reserve or reserve+commit allocation'}
  if result:
   runs=[]
   @VirtualMemoryVisitor
   def collect(address,length,protection,context):
    runs.append((address,length,protection));return 1
   assert native.tribes_virtual_memory_visit(host_base,collect,None)
   native_protection=protection if kind==0x3000 else 0
   assert (result,size,native_protection) in runs,'native allocation state mismatch'
   assert result%65536==0 and size%4096==0
   assert all(page not in pages.pages for page in range(result,result+size,4096))
   uc.mem_map(result,size,unicorn.UC_PROT_NONE)
   if native_protection:uc.mem_write(result,host_window[result:result+size])
   pages.declare(result,size,native_protection or 1,('virtual_reservation',result))
   virtual_reservations[result]={'bytes':size,'native_runs':[[result,size,native_protection]],'committed':bool(native_protection)}
   # Require must reject the mirrored reservation. Host reserve fault behavior
   # is independently covered by the native allocation ABI fault tests.
   if not native_protection:
    try:pages.require(result,1,unicorn.UC_PROT_READ)
    except ValueError:pass
    else:raise AssertionError('reserved pages unexpectedly readable')
   value['native_allocation_run']=[result,size,native_protection]
 elif name=='VirtualAlloc' and a.diagnostic_executable_capture and struct.unpack('<I',uc.mem_read(sp+16,4))[0] in (0x10,0x20,0x40,0x80):
  argc=4;arguments=struct.unpack('<4I',uc.mem_read(sp+4,16));result=executable_reference.allocate(*arguments)
  regions.append(('executable_reference',result,result+arguments[1]))
  value={'arguments':list(arguments),'reference_only':True,'event':executable_reference.events[-1]}
 elif name=='VirtualFree' and a.diagnostic_executable_capture and executable_reference.containing(struct.unpack('<I',uc.mem_read(sp+4,4))[0]):
  argc=3;arguments=struct.unpack('<3I',uc.mem_read(sp+4,12));result=executable_reference.release(*arguments)
  regions[:]=[region for region in regions if not (region[0]=='executable_reference' and region[1]==arguments[0])]
  value={'arguments':list(arguments),'reference_only':True,'event':executable_reference.events[-1]}
 elif name=='VirtualAlloc' and struct.unpack('<I',uc.mem_read(sp+12,4))[0] in (0x2000,0x3000):
  argc=4;arguments=struct.unpack('<4I',uc.mem_read(sp+4,16));address,size,kind,protection=arguments
  if protection not in (1,2,4):return False
  length=((address+size+4095)&~4095)-(address&~65535)
  if kind==0x3000 and (protection==1 or length%os.sysconf('SC_PAGE_SIZE')):return False
  result=native.tribes_virtual_alloc(host_base,*arguments)
  value={'arguments':list(arguments),'last_error':native.tribes_get_last_error(),'scope':'Actual native classic reserve or reserve+commit'}
  if result:
   length=((address+size+4095)&~4095)-(address&~65535)
   native_protection=protection if kind==0x3000 else 0
   assert (result,length,native_protection) in native_virtual_runs()
   assert all(page not in pages.pages for page in range(result,result+length,4096))
   uc.mem_map(result,length,unicorn.UC_PROT_NONE)
   if native_protection:uc.mem_write(result,host_window[result:result+length])
   pages.declare(result,length,native_protection or 1,('virtual_reservation',result))
   virtual_reservations[result]={'bytes':length,'native_runs':[[result,length,native_protection]],'committed':bool(native_protection)}
   value['native_allocation_run']=[result,length,native_protection]
 elif name=='VirtualAlloc':
  argc=4;arguments=struct.unpack('<4I',uc.mem_read(sp+4,16));address,size,kind,protection=arguments
  # Existing reservations only. New combined reserve/commit, decommit/release and
  # executable allocations remain explicit boundaries until their bridge exists.
  if kind!=0x1000 or not address or not size or protection not in (2,4):return False
  start=address&~4095;end=(address+size+4095)&~4095
  reservation=next((base for base,entry in virtual_reservations.items() if base<=start and end<=base+entry['bytes']),None)
  if reservation is None:return False
  if start%os.sysconf('SC_PAGE_SIZE') or end%os.sysconf('SC_PAGE_SIZE'):return False
  before=native_virtual_runs()
  for base,length,old_protection in before:
   low=max(start,base);high=min(end,base+length)
   if low>=high or old_protection==0:continue
   # Original execution can modify RW bytes between API calls. Recommit must
   # retain those bytes in the actual native backend as well as the x86 mirror.
   for page in range(low,high,4096):
    assert pages.pages[page]==(('virtual_reservation',reservation),old_protection),'native/diagnostic VM protections diverged'
   pages.require(low,high-low,unicorn.UC_PROT_READ)
   raw=bytes(uc.mem_read(low,high-low))
   if old_protection==4:host_window[low:high]=raw
   elif old_protection==2:assert host_window[low:high]==raw,'readonly native/guest bytes diverged'
   else:return False
  result=native.tribes_virtual_alloc(host_base,*arguments)
  value={'arguments':list(arguments),'last_error':native.tribes_get_last_error(),'scope':'Actual native classic commit/recommit within owned reservation'}
  if result:
   assert result==start
   after=native_virtual_runs();changed=[]
   for base,length,new_protection in after:
    low=max(start,base);high=min(end,base+length)
    if low>=high:continue
    assert new_protection in (2,4),'successful commit did not expose readable native pages'
    raw=host_window[low:high];uc.mem_write(low,raw);uc.mem_protect(low,high-low,PERMISSIONS[new_protection])
    for page in range(low,high,4096):pages.pages[page]=(('virtual_reservation',reservation),new_protection)
    assert bytes(uc.mem_read(low,high-low))==raw
    changed.append({'address':low,'bytes':high-low,'native_protection':new_protection,'sha256':hashlib.sha256(raw).hexdigest()})
   assert sum(run['bytes'] for run in changed)==end-start
   virtual_reservations[reservation]['native_runs']=[list(run) for run in after if reservation<=run[0]<reservation+virtual_reservations[reservation]['bytes']]
   virtual_reservations[reservation]['committed']=True
   value['synchronized_runs']=changed
 elif name=='VirtualQuery':
  argc=3;address,output,length=struct.unpack('<3I',uc.mem_read(sp+4,12))
  if length>=28 and output:pages.require(output,28,unicorn.UC_PROT_WRITE)
  result=native.tribes_virtual_query(host_base,address,output,length)
  reference_fields=None;reference_query=False
  if result and a.diagnostic_executable_capture:
   reference_fields,reference_query=executable_reference.query(address)
   if reference_query:host_window[output:output+28]=struct.pack('<7I',*reference_fields)
  value={'address':address,'length':length,'last_error':native.tribes_get_last_error(),'reference_only':reference_query}
  if result:
   fields=list(struct.unpack('<7I',host_window[output:output+28]));value['fields']=dict(zip(('base_address','allocation_base','allocation_protect','region_size','state','protect','type'),fields))
   if fields[4]==0x1000:
    declared=pages.pages.get(address&~4095)
    if (declared is None and fields[5]!=1) or (declared is not None and declared[1]!=fields[5]):raise ValueError('native VirtualQuery protection disagrees with diagnostic guest mapping')
   uc.mem_write(output,host_window[output:output+28])
 elif name=='FlushInstructionCache':
  if not a.diagnostic_executable_capture:return False
  process,address,size=struct.unpack('<3I',uc.mem_read(sp+4,12))
  if process!=0xffffffff:return False
  cache_reference.validate(address,size)
  global stop
  stop={'kind':'cache_flush_pending','process':process,'address':address,'bytes':size,'sp':sp,'return_pc':struct.unpack('<I',uc.mem_read(sp,4))[0]}
  uc.emu_stop();return True
 elif name=='VirtualProtect':
  address,size,protection,output=struct.unpack('<4I',uc.mem_read(sp+4,16))
  if a.diagnostic_executable_capture and executable_reference.containing(address,size):
   value=executable_reference.protect(address,size,protection,output);value['reference_only']=True
  else:
   if address>=0x30000000 or (address+size+4095)&~4095>0x30000000:return False
   value=pages.protect(address,size,protection,output)
   assert native.tribes_address_space_protect(host_base,address&~4095,((address+size+4095)&~4095)-(address&~4095),protection)
  argc=4;result=1
 elif name=='IsProcessorFeaturePresent':
  argc=1;feature=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if feature!=10:return False
  value=feature;result=int(a.diagnostic_sse2)
 elif name in ('GetSystemTimeAsFileTime','QueryPerformanceCounter','QueryPerformanceFrequency'):
  argc=1;output=struct.unpack('<I',uc.mem_read(sp+4,4))[0]
  if not output or output>0xfffffff8:raise ValueError('invalid service output')
  pages.require(output,8,unicorn.UC_PROT_WRITE)
  if name=='GetSystemTimeAsFileTime':native.tribes_system_file_time(host_base,output)
  else:result=(native.tribes_performance_counter if name=='QueryPerformanceCounter' else native.tribes_performance_frequency)(host_base,output)
  value=struct.unpack('<Q',host_window[output:output+8])[0];uc.mem_write(output,host_window[output:output+8])
 else:return False
 if name in binding_contracts:assert argc==binding_contracts[name]['arguments'],(name,argc,binding_contracts[name]['arguments'])
 target=struct.unpack('<I',uc.mem_read(sp,4))[0]
 services.append({'name':name,'return_pc':target,'output':output,'value':value,'eax':result,'source':'Unicorn original-x86 executable allocation reference' if isinstance(value,dict) and value.get('reference_only') else 'enforced Unicorn 4 KiB page contract' if name=='VirtualProtect' else 'native C helper' if name in identifier_functions or name in topology_functions or name in srw_functions or name in ('GetLocaleInfoEx','GetDateFormatEx','GetTimeFormatEx','EnumSystemLocalesEx','CompareStringEx','InitOnceInitialize','InitOnceExecuteOnce','GetModuleHandleA','VirtualQuery','SDL_SetMemoryFunctions','Sleep','VirtualAlloc','VirtualAlloc2FromApp','GetCurrentProcess','GetCurrentThread','GetEnvironmentStringsW','FreeEnvironmentStringsW','AreFileApisANSI','GetModuleFileNameA','GetModuleFileNameW','SetCurrentDirectoryA','SetConsoleCtrlHandler','SetUnhandledExceptionFilter','FreeLibrary','GetSystemInfo','GetEnvironmentVariableA','QueryPerformanceCounter','QueryPerformanceFrequency','GetSystemTimeAsFileTime','GetTempPathW','GetTempPath2W','GetSystemTimePreciseAsFileTime','GetModuleHandleW','InitializeSListHead','WideCharToMultiByte','LCMapStringEx','GetStringTypeW','MultiByteToWideChar','IsValidCodePage','GetACP','GetOEMCP','GetCPInfo','GetCommandLineA','GetCommandLineW','GetStdHandle','GetFileType','GetStartupInfoA','GetStartupInfoW','GetProcessHeap','HeapAlloc','HeapFree','LoadLibraryA','LoadLibraryExW','SetLastError','GetLastError','GetProcAddress','InitializeCriticalSectionAndSpinCount','InitializeCriticalSectionEx') or name in tls_functions or name in fls_functions or name in critical_functions else 'declared CPU diagnostic profile' if name=='IsProcessorFeaturePresent' else 'host sample under diagnostic service contract'})
 if result is not None:uc.reg_write(X.UC_X86_REG_EAX,result)
 uc.reg_write(X.UC_X86_REG_ESP,sp+4+(0 if binding_contracts.get(name,{}).get('calling_convention')=='cdecl' else 4*argc));uc.reg_write(X.UC_X86_REG_EIP,target)
 return True
def code(uc,address,size,unused):
 global instructions,stop
 finish_simd(True)
 finish_writes(True)
 finish_hardware(True)
 instructions+=1
 if address==0xfffc0020:
  if not locale_frames:stop={'kind':'locale_callback_error','error':'unowned callback return'};uc.emu_stop();return
  frame=locale_frames[-1];event=frame['event']
  if uc.reg_read(X.UC_X86_REG_ESP)!=frame['top']:stop={'kind':'locale_callback_error','error':'stdcall stack mismatch'};uc.emu_stop();return
  result=uc.reg_read(X.UC_X86_REG_EAX);event['callbacks'].append(dict(frame['pending'],eax=result))
  if result and locale_schedule(uc,frame):return
  locale_frames.pop();event.update(state='enumeration complete',early_stop=not bool(result),eax=1)
  services.append({'name':'EnumSystemLocalesEx','return_pc':event['return_pc'],'output':None,'value':dict(event),'eax':1,'source':'native C locale data with original x86 callbacks'})
  for register,value in frame['registers'].items():uc.reg_write(register,value)
  uc.reg_write(X.UC_X86_REG_EAX,1);uc.reg_write(X.UC_X86_REG_ESP,frame['sp']+20);uc.reg_write(X.UC_X86_REG_EIP,event['return_pc']);return
 if address==0xfffc0010:
  if not once_frames:stop={'kind':'once_callback_error','error':'unowned callback return'};uc.emu_stop();return
  frame=once_frames.pop();event=frame['event']
  if uc.reg_read(X.UC_X86_REG_ESP)!=frame['top']:stop={'kind':'once_callback_error','error':'stdcall callback stack mismatch','event':event};uc.emu_stop();return
  callback_result=uc.reg_read(X.UC_X86_REG_EAX);context=struct.unpack('<I',uc.mem_read(frame['scratch'],4))[0]
  # Native registry owns the word; synchronize only after actual callback return.
  if bytes(uc.mem_read(event['once'],4))!=host_window[event['once']:event['once']+4]:stop={'kind':'once_callback_error','error':'callback modified INIT_ONCE','event':event};uc.emu_stop();return
  if callback_result and context&3:stop={'kind':'once_callback_error','error':'context reserved bits','event':event};uc.emu_stop();return
  result=native.tribes_once_complete(host_base,event['once'],callback_result,context);uc.mem_write(event['once'],host_window[event['once']:event['once']+4])
  if result and event['context_output']:uc.mem_write(event['context_output'],struct.pack('<I',context))
  event.update(state='callback returned',callback_result=callback_result,context=context,eax=result)
  services.append({'name':'InitOnceExecuteOnce','return_pc':event['return_pc'],'output':event['context_output'],'value':dict(event),'eax':result,'source':'native C once ownership with original x86 callback'})
  for register,value in frame['registers'].items():uc.reg_write(register,value)
  uc.reg_write(X.UC_X86_REG_EAX,result);uc.reg_write(X.UC_X86_REG_ESP,frame['sp']+20);uc.reg_write(X.UC_X86_REG_EIP,event['return_pc']);return
 if address==0xfffc0000:stop={'kind':'return','eax':uc.reg_read(X.UC_X86_REG_EAX),'esp':uc.reg_read(X.UC_X86_REG_ESP)};uc.emu_stop();return
 if address in imports:
  sp=uc.reg_read(X.UC_X86_REG_ESP)
  try:
   if service(uc,imports[address],sp):return
  except (ValueError,unicorn.UcError) as error:
   stop={'kind':'service_error','service':imports[address],'error':str(error)};uc.emu_stop();return
  stop={'kind':'import','token':address,**imports[address],'return_and_stack_words':list(struct.unpack('<9I',uc.mem_read(sp,36)))}
  if imports[address]['name']=='RaiseException':
   exception_code,flags,count,arguments=stop['return_and_stack_words'][1:5]
   exception={'code':exception_code,'flags':flags,'count':count,'arguments_pointer':arguments}
   if count<=15:
    if count:pages.require(arguments,count*4,unicorn.UC_PROT_READ)
    values=list(struct.unpack('<%dI'%count,uc.mem_read(arguments,count*4))) if count else []
    exception['arguments']=values
    # Decode only the bounded metadata of MSVC x86 C++ throws, never dispatch
    # an exception or choose a handler from this diagnostic observation.
    if exception_code==0xe06d7363 and count==3 and values[0]==0x19930520:
     throw_info=values[2];pages.require(throw_info,16,unicorn.UC_PROT_READ);fields=struct.unpack('<4I',uc.mem_read(throw_info,16));exception['msvc_throw_info']=dict(zip(('attributes','unwind','forward_compatibility','catchable_types'),fields))
     array=fields[3];pages.require(array,4,unicorn.UC_PROT_READ);number=struct.unpack('<I',uc.mem_read(array,4))[0]
     if number<=32:
      pages.require(array+4,number*4,unicorn.UC_PROT_READ);types=[]
      for pointer in struct.unpack('<%dI'%number,uc.mem_read(array+4,number*4)):
       pages.require(pointer,28,unicorn.UC_PROT_READ);metadata=struct.unpack('<7I',uc.mem_read(pointer,28));descriptor=metadata[1];name=bytearray()
       for index in range(256):
        pages.require(descriptor+8+index,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(descriptor+8+index,1))
        if byte==b'\0':break
        name.extend(byte)
       else:raise ValueError('unterminated MSVC type name')
       types.append({'catchable_type':pointer,'type_descriptor':descriptor,'name':name.decode('ascii'),'metadata':list(metadata)})
      exception['msvc_catchable_types']=types
   stop['exception_request']=exception
  if imports[address]['name']=='GetEnvironmentVariableA':
   pointer,output,capacity=stop['return_and_stack_words'][1:4];raw=bytearray()
   for i in range(256):
    pages.require(pointer+i,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(pointer+i,1))
    if byte==b'\0':break
    raw.extend(byte)
   else:raise ValueError('diagnostic environment name exceeds 255 bytes')
   stop['environment_request']={'name':raw.decode('ascii'),'output':output,'capacity':capacity,'configured_environment_hex':temp_environment.hex()}
  if imports[address]['name'] in ('VirtualAlloc2FromApp','NtAllocateVirtualMemoryEx'):
   stop['virtual_allocation_arguments']=dict(zip(('process','address_or_pointer','size_or_pointer','type','protection','parameters','count'),stop['return_and_stack_words'][1:8]))
   parameters=stop['virtual_allocation_arguments']['parameters'];count=stop['virtual_allocation_arguments']['count']
   if count==1 and parameters:
    pages.require(parameters,16,unicorn.UC_PROT_READ);raw=bytes(uc.mem_read(parameters,16));kind,payload=struct.unpack('<2Q',raw)
    stop['virtual_allocation_extended_parameter']={'type':kind,'payload':payload,'bytes_hex':raw.hex()}
    if kind==1 and 0<payload<=0xffffffff:
     pages.require(payload,12,unicorn.UC_PROT_READ);requirements=bytes(uc.mem_read(payload,12))
     stop['virtual_allocation_extended_parameter']['address_requirements']=dict(zip(('lowest','highest','alignment'),struct.unpack('<3I',requirements)))
     stop['virtual_allocation_extended_parameter']['address_requirements_bytes_hex']=requirements.hex()
   if imports[address]['name']=='NtAllocateVirtualMemoryEx':
    for field in ('address_or_pointer','size_or_pointer'):
     pointer=stop['virtual_allocation_arguments'][field];pages.require(pointer,4,unicorn.UC_PROT_READ);stop['virtual_allocation_arguments'][field+'_contents']=struct.unpack('<I',uc.mem_read(pointer,4))[0]
  if imports[address]['name']=='LoadLibraryA':
   pointer=stop['return_and_stack_words'][1];raw=bytearray()
   for i in range(4096):
    pages.require(pointer+i,1,unicorn.UC_PROT_READ);byte=bytes(uc.mem_read(pointer+i,1))
    if byte==b'\0':break
    raw.extend(byte)
   else:raise ValueError('unterminated module name')
   stop['requested_module']=raw.decode('ascii')
  if imports[address]['name']=='LoadLibraryExW':
   pointer=stop['return_and_stack_words'][1];units=[]
   for index in range(256):
    pair=bytes(uc.mem_read(pointer+index*2,2))
    if pair==b'\0\0':break
    units.append(pair)
   else:raise ValueError('unterminated module name')
   stop['requested_module']=b''.join(units).decode('utf-16-le');stop['flags']=stop['return_and_stack_words'][3]
  if imports[address]['name']=='GetProcAddress':
   pointer=stop['return_and_stack_words'][2]
   stop['requested_export']=bytes(uc.mem_read(pointer,256)).split(b'\0',1)[0].decode('ascii') if pointer>0xffff else '#'+str(pointer)
  if imports[address]['name']=='WideCharToMultiByte':
   page,flags,pointer,count,output,capacity,default,used=stop['return_and_stack_words'][1:9]
   stop['reverse_conversion_request']={'page':page,'flags':flags,'input':pointer,'code_units':count,'output':output,'capacity':capacity,'default_character':default,'used_default_out':used}
   if 0<count<=4096:
    pages.require(pointer,count*2,unicorn.UC_PROT_READ);raw=bytes(uc.mem_read(pointer,count*2));stop['reverse_conversion_request'].update(input_utf16le_hex=raw.hex(),input_sha256=hashlib.sha256(raw).hexdigest())
  if imports[address]['name']=='GetStringTypeW':
   kind,pointer,count,output=stop['return_and_stack_words'][1:5]
   stop['classification_request']={'kind':kind,'input':pointer,'code_units':count,'output':output}
   if 0<count<=4096:
    pages.require(pointer,count*2,unicorn.UC_PROT_READ)
    raw=bytes(uc.mem_read(pointer,count*2));stop['classification_request'].update(utf16le_hex=raw.hex(),utf16le_sha256=hashlib.sha256(raw).hexdigest())
  if imports[address]['name']=='MultiByteToWideChar':
   page,flags,pointer,count,output,capacity=stop['return_and_stack_words'][1:7]
   stop['conversion_request']={'page':page,'flags':flags,'input':pointer,'input_count':count,'output':output,'capacity':capacity}
   if 0<count<=4096:
    pages.require(pointer,count,unicorn.UC_PROT_READ)
    raw=bytes(uc.mem_read(pointer,count));stop['conversion_request'].update(input_hex=raw.hex(),input_sha256=hashlib.sha256(raw).hexdigest(),all_byte_values_once=count==256 and sorted(raw)==list(range(256)))
  if imports[address]['name']=='FlsAlloc':
   callback=stop['return_and_stack_words'][1]
   stop['callback_address']=callback
   if callback:
    pages.require(callback,1,unicorn.UC_PROT_EXEC)
    stop['callback_entry_33_bytes_sha256']=hashlib.sha256(bytes(uc.mem_read(callback,33))).hexdigest()
   stop['required_behavior']='Owned fiber-local values plus actual guest cleanup callback dispatch; not a TLS alias'
  uc.emu_stop();return
 if executable_reference.containing(address):
  reference_executions.append({'pc':address,'bytes_hex':bytes(uc.mem_read(address,size)).hex()})
 recent.append(address)
 if len(recent)>32:del recent[0]
 observe_simd(uc,address,size)
 operation={b'\x0f\xa2':'cpuid',b'\x0f\x31':'rdtsc',b'\x0f\x01\xf9':'rdtscp',b'\x0f\x01\xd0':'xgetbv'}.get(bytes(uc.mem_read(address,size)))
 if operation:
  event={'pc':address,'operation':operation,'inputs':{'eax':uc.reg_read(X.UC_X86_REG_EAX),'ecx':uc.reg_read(X.UC_X86_REG_ECX)},'source':'Unicorn default x86 CPU model; not native compatibility CPU'}
  hardware.append(event);pending_hardware.append(event)
def write(uc,access,address,size,value,unused):
 for name,start,end in regions:
  if address<end and address+size>start:
   event={'image':name,'address':address,'size':size,'value':value,'pc':uc.reg_read(X.UC_X86_REG_EIP)}
   try:event['before_bytes']=bytes(uc.mem_read(address,size)).hex()
   except unicorn.UcError:event['before_bytes']=None
   write_attempts.append(event);pending_writes.append(event)
 for name,(pe,image_base,image) in images.items():
  if image_base<=address and address+size<=image_base+pe.OPTIONAL_HEADER.SizeOfImage and not any(n==name and address<end and address+size>start for n,start,end in regions):
   event={'image':name,'category':'image_data','address':address,'size':size,'value':value,'pc':uc.reg_read(X.UC_X86_REG_EIP),'before_bytes':bytes(uc.mem_read(address,size)).hex()}
   data_write_attempts.append(event);pending_writes.append(event)
def invalid(uc,access,address,size,value,unused):
 global stop
 stop={'kind':'invalid_memory','access':access,'address':address,'size':size,'pc':uc.reg_read(X.UC_X86_REG_EIP)};return False
def teb_read(uc,access,address,size,value,unused):
 global stop,teb_native_reads
 declared=DECLARED_FIELDS
 relative=address-teb_base
 if not any(offset<=relative and relative+size<=offset+4 for offset in declared):
  stop={'kind':'undeclared_teb_read','address':address,'size':size,'pc':uc.reg_read(X.UC_X86_REG_EIP)};uc.emu_stop()
 elif not a.diagnostic_zero_teb:
  offset=next(offset for offset in declared if offset<=relative and relative+size<=offset+4)
  actual=native.x86_read_fs_dword(offset);expected=struct.pack('<I',actual)[relative-offset:relative-offset+size]
  assert bytes(uc.mem_read(address,size))==expected,'native/original TEB field divergence'
  teb_native_reads+=1

def teb_write(uc,access,address,size,value,unused):
 global stop,teb_native_writes
 if size!=4 or address!=teb_base:
  stop={'kind':'undeclared_teb_write','address':address,'size':size,'pc':uc.reg_read(X.UC_X86_REG_EIP)};uc.emu_stop();return
 if not a.diagnostic_zero_teb:native.x86_write_fs_dword(0,value&0xffffffff);teb_native_writes+=1
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,teb_write,begin=teb_base,end=teb_base+4095)
u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);u.hook_add(unicorn.UC_HOOK_MEM_INVALID,invalid);u.hook_add(unicorn.UC_HOOK_MEM_READ,teb_read,begin=teb_base,end=teb_base+4095)
for label,entry in [('tls_callback_'+str(i),pointer) for i,pointer in enumerate(callbacks)]+[('dll_process_attach',base+pe.OPTIONAL_HEADER.AddressOfEntryPoint)]:
 stop=None;before=instructions;sp=0x2fff00;u.mem_write(sp,struct.pack('<4I',0xfffc0000,base,1,0));u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
 resume=entry
 while instructions-before<a.budget:
  cache_reference.running=True
  try:u.emu_start(resume,0,count=a.budget-(instructions-before))
  except unicorn.UcError as error:
   if stop is None:stop={'kind':'cpu_error','error':str(error),'pc':u.reg_read(X.UC_X86_REG_EIP)}
  finally:cache_reference.running=False
  if not stop or stop['kind']!='cache_flush_pending':break
  request=stop
  checked=[getattr(X,'UC_X86_REG_'+name) for name in ('EAX','EBX','ECX','EDX','ESI','EDI','EBP','ESP','EIP','EFLAGS','FPCW','FPSW','FPTAG','MXCSR')]+[getattr(X,'UC_X86_REG_FP'+str(i)) for i in range(8)]+[getattr(X,'UC_X86_REG_YMM'+str(i)) for i in range(8)]
  architecture=[u.reg_read(reg) for reg in checked];last_error=native.tribes_get_last_error()
  try:value=dict(cache_reference.invalidate(request['address'],request['bytes']))
  except (ValueError,unicorn.UcError) as error:
   stop={'kind':'service_error','service':'FlushInstructionCache','error':str(error),'request':request};break
  assert architecture==[u.reg_read(reg) for reg in checked] and last_error==native.tribes_get_last_error(),'cache invalidation mutated architectural state'
  value.update(process=request['process'],checked_registers=checked,architectural_state_preserved=True,last_error=last_error,paused_for_invalidation=True)
  services.append({'name':'FlushInstructionCache','return_pc':request['return_pc'],'output':None,'value':value,'eax':1,'source':'Actual paused Unicorn original-x86 translation-cache invalidation; no native ARM64 publication'})
  u.reg_write(X.UC_X86_REG_EAX,1);u.reg_write(X.UC_X86_REG_ESP,request['sp']+16);u.reg_write(X.UC_X86_REG_EIP,request['return_pc']);resume=request['return_pc'];stop=None
 if stop is None:stop={'kind':'budget','pc':u.reg_read(X.UC_X86_REG_EIP)}
 stages.append({'name':label,'entry':entry,'instructions':instructions-before,'stop':stop})
 if stop['kind']!='return':break
finish_writes(False)
finish_hardware(False)
finish_simd(False)
trace=[]
for address in recent:
 ins=next(decoder.disasm(bytes(u.mem_read(address,15)),address),None)
 if ins:trace.append({'pc':address,'assembly':ins.mnemonic+' '+ins.op_str})
initialized=len(stages)==len(callbacks)+1 and stages[-1]['stop']['kind']=='return' and bool(stages[-1]['stop']['eax'])
factory_probe=None
if initialized:
 symbol=next(symbol for symbol in images['qol'][0].DIRECTORY_ENTRY_EXPORT.symbols if symbol.name and symbol.name.startswith(b'?CreateDevice@CImmDevice@@'))
 factory_pc=images['qol'][1]+symbol.address;factory_bytes=bytes(u.mem_read(factory_pc,3));assert factory_bytes==bytes.fromhex('33c0c3'),'factory body differs from pinned null-return leaf'
 context=u.context_save();saved_stop=stop;saved_error=native.tribes_get_last_error();probe_sp=0x2ffe00;pages.require(probe_sp,12,unicorn.UC_PROT_WRITE);u.mem_write(probe_sp,struct.pack('<3I',0xfffc0000,images['engine'][1],0));u.reg_write(X.UC_X86_REG_ESP,probe_sp);u.reg_write(X.UC_X86_REG_EAX,0x13579bdf);stop=None;probe_before=instructions;service_before=len(services);write_before=len(write_attempts);cache_reference.running=True
 try:u.emu_start(factory_pc,0,count=32)
 finally:cache_reference.running=False
 assert stop and stop['kind']=='return' and stop['eax']==0 and stop['esp']==probe_sp+4 and len(services)==service_before and len(write_attempts)==write_before,'factory contract divergence'
 assert saved_error==native.tribes_get_last_error()
 factory_probe={'export':symbol.name.decode('ascii'),'entry':factory_pc,'body_hex':factory_bytes.hex(),'eax':stop['eax'],'esp':stop['esp'],'stack_argument_bytes_left':8,'calling_convention':'cdecl','instructions':instructions-probe_before,'services':0,'memory_write_attempts':0,'last_error_preserved':True,'scope':'Actual pinned original-x86 null-return leaf after successful diagnostic process attach; this is the disabled haptic factory, not a graphics/device initialization success or native ARM64 DLL execution'}
 u.context_restore(context);stop=saved_stop

finish_simd(False)
simd_coverage={'sites':sorted(simd_sites.values(),key=lambda site:(site['pc'],site['bytes_hex'])),'scope':'Original-x86 instructions entered under the declared startup profile; completion means a subsequent code hook was observed. Unobserved paths are not proven unreachable. No native ARM64 SIMD implementation claim.'}
simd_coverage['completed_by_mnemonic']={name:sum(site['completed'] for site in simd_sites.values() if site['mnemonic']==name) for name in sorted({site['mnemonic'] for site in simd_sites.values()})}
cookie_check=None
if len(services)>=4:
 sample,tid,pid,counter=services[:4]
 assert [entry['name'] for entry in (sample,tid,pid,counter)]==['GetSystemTimeAsFileTime','GetCurrentThreadId','GetCurrentProcessId','QueryPerformanceCounter']
 expected_cookie=((sample['value']&0xffffffff)^(sample['value']>>32)^tid['eax']^pid['eax']^(counter['value']&0xffffffff)^(counter['value']>>32)^(counter['output']+16))&0xffffffff
 if expected_cookie==0xbb40e64e:expected_cookie+=1
 elif not expected_cookie&0xffff0000:expected_cookie|=((expected_cookie|0x4711)<<16)&0xffffffff
 actual_cookie=struct.unpack('<I',u.mem_read(0x101d5d40,4))[0];complement=struct.unpack('<I',u.mem_read(0x101d5d80,4))[0]
 cookie_check={'expected':expected_cookie,'actual':actual_cookie,'complement':complement,'passed':actual_cookie==expected_cookie and complement==expected_cookie^0xffffffff}
 assert cookie_check['passed'],'security cookie input/output mismatch'
report={'state':'finished','initialized':initialized,'execution':'Original x86 in Unicorn under declared sparse loader contract; no ARM64 module execution or real Windows DLL claim','inputs_sha256':expected,'native_bridge':{'binary_sha256':hashlib.sha256(native_path.read_bytes()).hexdigest(),'source_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in native_sources},'scope':'Actual C loader references/ANSI-wide handle/export lookup and wide guest module paths and default ANSI file-mode query, spin critical sections, TLS, owned FLS allocation/get/set, owned CP437-decoded wide environment snapshots/free, immutable ANSI/wide launch metadata and native descriptor handles plus ANSI/OEM code-page metadata, x86 system metadata and sampled single-group/UMA processor topology, ASCII environment lookup, ordinary temporary-path profile, native clocks, x86 SLIST initialization, CP1252 decoding/reverse conversion and Wine-backed UTF-16 classification/default case mapping; FlsFree stops before mutation because diagnostic cleanup dispatch is pending'},'script_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'tls':{'index_address':t.AddressOfIndex,'index':0,'template_hex':template.hex(),'zero_fill':t.SizeOfZeroFill,'callbacks':callbacks,'array':0x3e0000,'block':0x3f0000},'stages':stages,'instructions':instructions,'service_calls':services,'security_cookie_check':cookie_check,'hardware_observations':hardware,'unicorn_version':unicorn.__version__,'cpu_profile':{'sse2':a.diagnostic_sse2,'meaning':'IsProcessorFeaturePresent(10) response; CPU instructions use the separately recorded Unicorn model','scope':'diagnostic branch only; native runtime remains x87/MMX'},'security_cookie':struct.unpack('<I',u.mem_read(0x101d5d40,4))[0],'heap_allocations_live':heap_allocations,'code_writes':writes,'code_write_attempts':write_attempts,'image_data_writes':data_writes,'image_data_write_attempts':data_write_attempts,'page_protection':{'initial_pages_sha256':initial_pages_sha256,'guest_page_size':4096,'host_page_size':os.sysconf('SC_PAGE_SIZE'),'changes':pages.changes,'contract_source_sha256':hashlib.sha256((R/'scripts/guest_page_contract.py').read_bytes()).hexdigest(),'native_enforcement':False},'recent_instructions':trace,'limits':['Two sampled process/thread identity services, native clock cookie inputs and one declared SSE2 query; native C loader references, export lookup, spin critical sections, TLS, FLS allocation/get/set, ANSI/wide launch metadata and native descriptor handles plus ANSI/OEM code-page metadata, x86 system metadata and sampled single-group/UMA processor topology, ASCII environment lookup, ordinary temporary-path profile, native clocks, x86 SLIST initialization, CP1252 decoding/reverse conversion and Wine-backed UTF-16 classification/default case mapping; unknown services and FLS cleanup stop; VirtualProtect uses enforced Unicorn 4 KiB permissions','Sparse FS/TEB/TLS mapping is a diagnostic profile, not a complete Windows loader','Patch writes are traced only until the first boundary','Native QoL startup, 4 KiB permission enforcement on 16 KiB Apple pages, AVX execution, precompiled patch routing and game integration remain unverified']};report.update(locale_callbacks=locale_events,once_callbacks=once_events,teb_profile={'diagnostic_zero_identity':a.diagnostic_zero_teb,'base':teb_base,'self':struct.unpack('<I',u.mem_read(teb_base+0x18,4))[0],'fs_base':u.reg_read(X.UC_X86_REG_FS_BASE),'fs_selector':u.reg_read(X.UC_X86_REG_FS),'gdt_base':gdt_base,'descriptor_hex':descriptor.hex(),'segment_profile':segment_profile,'contract_source_sha256':hashlib.sha256((R/'scripts/guest_teb_contract.py').read_bytes()).hexdigest(),'native_checked_reads':teb_native_reads,'native_seh_head_writes':teb_native_writes,'native_configured':not a.diagnostic_zero_teb,'scope':'Sparse x86 FS identity checked through native C helper; no ARM64 QoL loader/SEH publication'},srw_live_objects=native.tribes_srw_live_count(),virtual_reservations_live={str(k):v for k,v in virtual_reservations.items()},environment_snapshots_live={str(k):{'bytes':v[0],'backing':v[1]} for k,v in environment_snapshots.items()},oem_environment_table_sha256=hashlib.sha256((R/'port/core/cp437_environment_table.inc').read_bytes()).hexdigest(),casing_data_sha256=hashlib.sha256((R/'port/core/nls_casing.inc').read_bytes()).hexdigest(),classification_reference=json.loads((R/'config/encoding-sources.json').read_text())['classification_reference'],classification_data_sha256=hashlib.sha256((R/'port/core/nls_classification.inc').read_bytes()).hexdigest(),encoding_profile={'ansi':1252,'oem':437,'scope':'Metadata, known-page validity, CP1252 decoding with flags 0/1/8/9 and Wine 10.0 CTYPE1/2/3 UTF-16 code-unit classification plus English/invariant default BMP upper/lower mapping; CP1252 reverse conversion flags 0/0x400 with null optionals; other locales/flags and surrogate reverse input stop'},launch_profile={'show':1,'initial_handles':list(launch_handles),'startup_string_pointers':'NULL','ansi_command_line_hex':command_line.hex(),'utf16le_command_line_hex':wide_command_line.hex(),'encoding':'Explicit matching ASCII launch text in ANSI and UTF-16LE; general code-page conversion not yet implemented'},binding_contracts_sha256=hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest());reference_snapshots=[]
for address,record in executable_reference.allocations.items():
 raw=bytes(u.mem_read(address,record['bytes']));path=out/('executable-reference-%08x.bin'%address);path.write_bytes(raw)
 reference_snapshots.append({'address':address,'bytes':len(raw),'path':str(path.relative_to(R)),'sha256':hashlib.sha256(raw).hexdigest()})
report['executable_reference']={'enabled':a.diagnostic_executable_capture,'contract_source_sha256':hashlib.sha256((R/'scripts/guest_executable_reference.py').read_bytes()).hexdigest(),'events':executable_reference.events,'snapshots':reference_snapshots,'executed_instructions':reference_executions,'scope':'Original x86 in real Unicorn executable pages only; not native ARM64 guest execution, AOT publication or a complete Windows reference'};
report['address_space_profile']={'declarations':address_space_declarations,'sealed':True,'image_allocation_protect':0x80,'heap_arenas':'Permanent native provider ownership, active GlobalAlloc ranges and retained process-heap backing inspected through actual provider locks','vm':'Actual owned reservation identity, initial protection and commit state; native tail padding remains owned/reserved','scope':'Logical x86 loader state plus actual C provider ownership; independent native 4 KiB permission enforcement and full-engine integration unverified'};report['native_bridge'].update(temp_directory_profile={'identity':'ordinary non-SYSTEM','environment_hex':temp_environment.hex(),'windows_directory':'C:\\Windows','guest_temp':'C:\\TribesPad\\Temp\\'},provider_scope='Diagnostic kernel32/kernelbase providers share TLS, FLS, InitializeCriticalSectionEx LCMapStringEx native UTC FILETIME, GetTempPath2W, four tested processor/UMA topology exports and the tested VirtualAlloc2FromApp export; VirtualAlloc2FromApp reserve and reserve+commit calls use actual native state and matching Unicorn permissions; classic readable commit/recommit in these owned reservations synchronizes native/original bytes; classic reserve/combined reserve+commit also use actual native state; free and NT calls stop at the diagnostic mapping boundary; FLS cleanup stops in this tracer; ntdll exposes the tested NtAllocateVirtualMemoryEx wrapper; no ARM64 engine provider publication',exports=sorted(provider_names),modules=provider_modules,registered_image_paths=registered_image_paths,declared_absent_diagnostic_exports=sorted(absent_diagnostic_exports));report['fp_profile']={'initial_mxcsr':0x1f80,'final_mxcsr':u.reg_read(X.UC_X86_REG_MXCSR),'scope':'Explicit masked-exception, nearest-even startup profile; not complete Windows loader or native FP equivalence'};report['simd_coverage']=simd_coverage;report['factory_probe']=factory_probe;report['instruction_cache_reference']={'events':cache_reference.events,'contract_source_sha256':hashlib.sha256((R/'scripts/guest_cache_reference.py').read_bytes()).hexdigest(),'scope':'Paused real Unicorn translation-cache invalidation only; native ARM64 patch publication remains unimplemented'};report['module_availability']=module_availability;report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
