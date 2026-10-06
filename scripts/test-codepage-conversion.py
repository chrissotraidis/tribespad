#!/usr/bin/env python3
"""Compare native CP1252 conversion with two independent primary data files."""
import ctypes,hashlib,json,pathlib,re,struct,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/native-win32/encoding-data';out.mkdir(parents=True,exist_ok=True);report=out/'result.json';report.write_text(json.dumps({'pass':False,'state':'running'})+'\n')
manifest=json.loads((R/'config/encoding-sources.json').read_text());reference=manifest['windows_best_fit'];raw=(R/reference['local_path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==reference['sha256']
body=raw.decode('ascii').split('MBTABLE 256',1)[1].split('WCTABLE',1)[0];pairs=re.findall(r'^0x([0-9a-f]+)\s+0x([0-9a-f]+)',body,re.M|re.I);mapping={int(a,16):int(b,16) for a,b in pairs};assert sorted(mapping)==list(range(256));expected=struct.pack('<256H',*[mapping[i] for i in range(256)])
standard=(R/manifest['local_path']).read_bytes();assert hashlib.sha256(standard).hexdigest()==manifest['sha256'];defined=re.findall(r'^0x([0-9a-f]+)\s+0x([0-9a-f]+)',standard.decode('ascii'),re.M|re.I);assert len(defined)==251 and all(mapping[int(a,16)]==int(b,16) for a,b in defined)
lib=out/'conversion.dylib';sources=[R/'port/core/guest_encoding.c',R/'port/core/guest_heap.c'];subprocess.run(['clang','-dynamiclib','-O2','-Wall','-Wextra','-Werror',*map(str,sources),'-o',str(lib)],check=True,capture_output=True)
native=ctypes.CDLL(str(lib));call=native.tribes_multibyte_to_wide;call.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*6;call.restype=ctypes.c_uint32
native.tribes_set_last_error.argtypes=[ctypes.c_uint32];native.tribes_get_last_error.restype=ctypes.c_uint32
memory=ctypes.create_string_buffer(2048);base=ctypes.addressof(memory);data=bytes(range(256));ctypes.memmove(base+1,data,256);runs=0
for page in (0,1252):
 for flags in (0,1,8,9):
  native.tribes_set_last_error(123);assert call(base,page,flags,1,256,0,0)==256
  ctypes.memset(base+512,0x79,514);assert call(base,page,flags,1,256,513,256)==256
  assert memory.raw[513:1025]==expected and memory.raw[512]==0x79 and memory.raw[1025]==0x79
  assert memory.raw[1:257]==data and native.tribes_get_last_error()==123;runs+=1
# Independently verify the actual initializer's modified byte table too.
trace=json.loads((R/'generated/qol-initialization/no-sse2.json').read_text());conversions=[r for r in trace['service_calls'] if r['name']=='MultiByteToWideChar'];assert len(conversions)>=2 and all(row['eax']==256 for row in conversions)
assert all(bytes.fromhex(row['value']['output_hex'])==b' \0'+expected[2:] for row in conversions if row['value']['capacity'])
# Expand the primary NLS file independently of the generated compact C arrays.
classification=manifest['classification_reference'];nls=(R/classification['local_path']).read_bytes();assert hashlib.sha256(nls).hexdigest()==classification['sha256']
ctype_offset=struct.unpack_from('<4I',nls)[2];index_offset=ctype_offset+struct.unpack_from('<H',nls,ctype_offset+2)[0]+2
matrix=[]
for code_unit in range(65536):
    page_offset=struct.unpack_from('<H',nls,index_offset+2*(code_unit//256))[0]
    block_offset=struct.unpack_from('<H',nls,index_offset+page_offset+2*((code_unit//16)%16))[0]
    record=nls[index_offset+block_offset+code_unit%16]
    matrix.append(struct.unpack_from('<3H',nls,ctype_offset+4+record*6))
classify=native.tribes_get_string_type_w;classify.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*4;classify.restype=ctypes.c_uint32
arena=ctypes.create_string_buffer(262208);arena_base=ctypes.addressof(arena);wide=struct.pack('<65536H',*range(65536));ctypes.memmove(arena_base+1,wide,len(wide));output=131089
for kind,column in [(1,0),(2,1),(4,2)]:
    ctypes.memset(arena_base+output-1,0x79,len(wide)+2);native.tribes_set_last_error(123)
    assert classify(arena_base,kind,1,65536,output)==1
    assert arena.raw[output:output+len(wide)]==struct.pack('<65536H',*[row[column] for row in matrix])
    assert arena.raw[output-1]==0x79 and arena.raw[output+len(wide)]==0x79 and arena.raw[1:1+len(wide)]==wide
    assert native.tribes_get_last_error()==123
observed=[r for r in trace['service_calls'] if r['name']=='GetStringTypeW'];assert len(observed)==1 and observed[0]['eax']==1
qol_wide=bytes.fromhex(conversions[1]['value']['output_hex']);qol_units=struct.unpack('<256H',qol_wide)
assert bytes.fromhex(observed[0]['value']['output_hex'])==struct.pack('<256H',*[matrix[ch][0] for ch in qol_units])
# Case maps are expanded directly from the pinned file, not generated arrays.
case_base=struct.unpack_from('<4I',nls)[1];case_starts=[case_base+4,case_base+4+2*struct.unpack_from('<H',nls,case_base+2)[0]]
case_maps=[]
for start in case_starts:
    values=[]
    for ch in range(65536):
        page=struct.unpack_from('<H',nls,start+2*(ch//256))[0]
        block=struct.unpack_from('<H',nls,start+2*(page+(ch//16)%16))[0]
        delta=struct.unpack_from('<H',nls,start+2*(block+ch%16))[0]
        values.append((ch+delta)%65536)
    case_maps.append(values)
map_string=native.tribes_lcmap_string_ex;map_string.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*9;map_string.restype=ctypes.c_uint32
case_arena=ctypes.create_string_buffer(524400);case_address=ctypes.addressof(case_arena);case_output=262161
# Insert NUL separators to test every individual code unit without combining
# adjacent high/low surrogate units into unsupported supplementary scalars.
case_input=struct.pack('<131072H',*[value for ch in range(65536) for value in (ch,0)]);ctypes.memmove(case_address+1,case_input,len(case_input))
for flags,table in [(0x200,case_maps[0]),(0x100,case_maps[1])]:
    native.tribes_set_last_error(123);assert map_string(case_address,0,flags,1,131072,0,0,0,0,0)==131072
    ctypes.memset(case_address+case_output-1,0x79,len(case_input)+2)
    assert map_string(case_address,0,flags,1,131072,case_output,131072,0,0,0)==131072
    expected_case=struct.pack('<131072H',*[value for ch in range(65536) for value in (table[ch],0)])
    assert case_arena.raw[case_output:case_output+len(case_input)]==expected_case
    assert case_arena.raw[case_output-1]==0x79 and case_arena.raw[case_output+len(case_input)]==0x79 and case_arena.raw[1:1+len(case_input)]==case_input
    assert native.tribes_get_last_error()==123
case_calls=[r for r in trace['service_calls'] if r['name']=='LCMapStringEx'];assert len(case_calls)==4 and all(row['eax']==256 for row in case_calls)
for row in case_calls:
 request=row['value']
 if not request['capacity']:continue
 qol_case_input=struct.unpack('<256H',bytes.fromhex(request['input_hex']));table=case_maps[1 if request['flags']==0x100 else 0]
 assert bytes.fromhex(request['output_hex'])==struct.pack('<256H',*[table[ch] for ch in qol_case_input])
# Independent WCTABLE expansion checks every supported non-surrogate BMP unit.
reverse_body=raw.decode('ascii').split('WCTABLE 698',1)[1]
reverse_pairs=re.findall(r'^0x([0-9a-f]+)\s+0x([0-9a-f]+)',reverse_body,re.M|re.I)
reverse_mapping={int(a,16):int(b,16) for a,b in reverse_pairs};assert len(reverse_mapping)==698
reverse=native.tribes_wide_to_multibyte;reverse.argtypes=[ctypes.c_uint64]+[ctypes.c_uint32]*8;reverse.restype=ctypes.c_uint32
reverse_units=[ch for ch in range(65536) if not 0xd800<=ch<=0xdfff];reverse_input=struct.pack('<'+'H'*len(reverse_units),*reverse_units)
reverse_arena=ctypes.create_string_buffer(200000);reverse_base=ctypes.addressof(reverse_arena);ctypes.memmove(reverse_base+1,reverse_input,len(reverse_input));reverse_output=130001
for page in (0,1252):
 for flags in (0,0x400):
  for replacement in (None,0x21):
   default=199001 if replacement is not None else 0
   if default:ctypes.memmove(reverse_base+default,bytes([replacement]),1)
   expected_reverse=[];expected_used=0
   for ch in reverse_units:
    byte=reverse_mapping.get(ch,0x3f)
    if (flags==0x400 or byte==0x3f) and mapping[byte]!=ch:byte=0x3f if replacement is None else replacement;expected_used=1
    expected_reverse.append(byte)
   ctypes.memset(reverse_base+reverse_output-1,0x79,len(reverse_units)+2);native.tribes_set_last_error(123)
   assert reverse(reverse_base,page,flags,1,len(reverse_units),0,0,default,198001)==len(reverse_units)
   assert reverse(reverse_base,page,flags,1,len(reverse_units),reverse_output,len(reverse_units),default,198001)==len(reverse_units)
   assert reverse_arena.raw[reverse_output:reverse_output+len(reverse_units)]==bytes(expected_reverse)
   assert reverse_arena.raw[reverse_output-1]==0x79 and reverse_arena.raw[reverse_output+len(reverse_units)]==0x79
   assert struct.unpack_from('<I',reverse_arena.raw,198001)[0]==expected_used and native.tribes_get_last_error()==123
   assert reverse_arena.raw[1:1+len(reverse_input)]==reverse_input
reverse_calls=[r for r in trace['service_calls'] if r['name']=='WideCharToMultiByte']
for row in reverse_calls:
 request=row['value'];units=struct.unpack('<'+'H'*request['units'],bytes.fromhex(request['input_hex']))
 assert row['eax']==request['units'] and bytes.fromhex(request['output_hex'])==bytes(reverse_mapping.get(ch,0x3f) for ch in units)
result={'pass':True,'state':'finished','reverse_unit_profile_pairs':len(reverse_units)*8,'qol_reverse_calls_verified':len(reverse_calls),'data_cases':runs,'defined_standard_entries_cross_checked':len(defined),'windows_table_entries':len(mapping),'actual_qol_table_matches':True,'qol_conversion_calls_verified':len(conversions),'classification_code_unit_flag_pairs':196608,'all_three_classification_levels_pass':True,'actual_qol_classification_matches':True,'case_unit_direction_pairs':131072,'actual_qol_lowercase_matches':True,'classification_reference_sha256':classification['sha256'],'binary_sha256':hashlib.sha256(lib.read_bytes()).hexdigest(),'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[R/'port/core/cp1252_table.inc',R/'port/core/nls_classification.inc',R/'port/core/nls_casing.inc']},'references_sha256':{reference['local_path']:reference['sha256'],manifest['local_path']:manifest['sha256']},'scope':'Actual native C conversion and original-x86 QoL trace outputs checked against pinned Microsoft tables; Wine 10.0 NLS-backed classification of all UTF-16 code units and actual CRT table checked; not Windows DLL execution or ARM64 QoL startup'};report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
