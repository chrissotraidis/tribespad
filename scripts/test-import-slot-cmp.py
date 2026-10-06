#!/usr/bin/env python3
"""Execute an original relocation-bearing IAT comparison without symbol folding."""
import argparse,hashlib,json,os,pathlib,re,runpy,struct,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1];transform=runpy.run_path(str(R/'scripts/prepare-va-model.py'))['transform']
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/import-slot-cmp'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));slot=0x101296b0;actual=bytes.fromhex('833db096121000');assert pe.get_data(0x53c97,7)==actual
imports=[(d.dll.decode(),i.name.decode()) for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.address==slot];assert imports==[('dbghelp.dll','StackWalk64')]
records=[{'bytes_hex':actual.hex(),'right':0,'original_pc':'0x10053c97'}]
for value in [1,0xffffffff,0x7f,0xffffff80]:records.append({'bytes_hex':(b'\x83\x3d'+struct.pack('<I',slot)+bytes([value&255])).hex(),'right':value})
for value in [0,1,0xffffffff,0x80000000,0x7fffffff,0xfffa0000,0xfffe1000,0x55555555,0xaaaaaaaa]:records.append({'bytes_hex':(b'\x81\x3d'+struct.pack('<II',slot,value)).hex(),'right':value})
for r in records:r.update(kind=0,slot=4)
for pc,addr,index in [(0x10053d92,0x101296ac,3),(0x10053d9f,0x101296a8,2)]:
 raw=b'\xff\x35'+struct.pack('<I',addr);assert pe.get_data(pc-0x10000000,len(raw))==raw;records.append({'bytes_hex':raw.hex(),'right':0,'original_pc':hex(pc),'kind':1,'slot':index})
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
def fixture(directory,rows,full=True):
 directory.mkdir(exist_ok=True);raw=bytearray(original.read_bytes()[:0x400]);header=struct.unpack_from('<I',raw,0x3c)[0];optional=header+24;section=optional+224;code=b''.join(bytes.fromhex(r['bytes_hex'])+b'\xc3'+bytes(31-len(bytes.fromhex(r['bytes_hex']))) for r in rows);assert len(code)<=512
 struct.pack_into('<H',raw,header+6,2);struct.pack_into('<H',raw,header+22,0x103)
 for off,value in [(4,512),(8,4096),(16,0x1000),(56,0x12a000),(60,0x400)]:struct.pack_into('<I',raw,optional+off,value)
 raw[optional+96:optional+224]=bytes(128);struct.pack_into('<II',raw,optional+104,0x129000,40);struct.pack_into('<II',raw,optional+192,0x1296a0,24);raw[section:]=bytes(len(raw)-section)
 struct.pack_into('<8s6I2HI',raw,section,b'.text\0\0\0',len(code),0x1000,512,0x400,0,0,0,0,0x60000020)
 struct.pack_into('<8s6I2HI',raw,section+40,b'.rdata\0\0',4096,0x129000,4096,0x600,0,0,0,0,0xc0000040)
 data=bytearray(4096);struct.pack_into('<5I',data,0,0x129100,0,0,0x129200,0x1296a0);data[0x200:0x20c]=b'dbghelp.dll\0'
 for i,name in enumerate(['SymInitialize','SymCleanup','SymFunctionTableAccess64','SymGetModuleBase64','StackWalk64']):
  hint=0x300+i*64;struct.pack_into('<I',data,0x100+i*4,0x129000+hint);struct.pack_into('<I',data,0x6a0+i*4,0x129000+hint);encoded=name.encode()+b'\0';data[hint+2:hint+2+len(encoded)]=encoded
 raw+=code+bytes(512-len(code))+data;(directory/'fixture.exe').write_bytes(raw)
 p=pefile.PE(data=bytes(raw));assert len(p.DIRECTORY_ENTRY_IMPORT)==1 and p.DIRECTORY_ENTRY_IMPORT[0].imports[4].address==slot and p.DIRECTORY_ENTRY_IMPORT[0].imports[4].name==b'StackWalk64'
 (directory/'global_aliases.sci').write_text(''.join('loc_%X,fixture_%d\n'%(0x10001000+32*i,i) for i,r in enumerate(rows)));(directory/'relocations.csv').write_text(''.join('0x%x,0x%x,\n'%(0x10001000+32*i+2,0x101296a0+4*r.get('slot',4)) for i,r in enumerate(rows)))
 env={k:v for k,v in os.environ.items() if k not in ['HALOPAD_SRW_DIAG','TRIBES_SRW_FULL_FLAGS']}
 if full:env['TRIBES_SRW_FULL_FLAGS']='1'
 run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=directory,env=env,capture_output=True,text=True,timeout=60);(directory/'stdout.log').write_text(run.stdout);(directory/'stderr.log').write_text(run.stderr);return run
run=fixture(out/'fixture',records);assert run.returncode==0,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
# Import sections are omitted from SRW data output; the production VA model
# maps original PE memory and rewrites these labels to their guest addresses.
declarations=['StackWalk64','SymGetModuleBase64','SymFunctionTableAccess64']
main=(out/'fixture/fixture.llasm').read_text()
for name in declarations:assert 'proc '+name+' external' in main
source=out/'bodies.llasm';bodies=[]
for m in procedures:
 body=m[2].split(';ret\n')[0];assert ('loc_%X'%(0x101296a0+4*records[int(m[1])]['slot'])) in body and 'StackWalk64' not in body and 'SymGetModuleBase64' not in body and 'SymFunctionTableAccess64' not in body
 body,stats=transform(body.splitlines());assert stats['value_labels']>0;bodies.append('proc slot_cmp_'+m[1]+' public\n'+'\n'.join(body)+'\nendp\n')
source.write_text('include llasm.llinc\n'+''.join(bodies))
negative=[]
for name,raw,full in [('conservative-mode-required',actual.hex(),False),('byte-width','803db096121000',True),('test-unsupported','f705b09612100000000000',True)]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],full);assert run.returncode and 'Error: -53' in run.stderr,(name,run.stdout,run.stderr);negative.append({'name':name,'exit':run.returncode,'bytes_hex':raw})
llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(ir),str(source)],check=True)
(out/'cases.inc').write_text(''.join('extern void slot_cmp_%d(_cpu *);\n'%i for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('slot_cmp_'+str(i) for i in range(len(records)))+'};\nstatic const uint32_t right[]={'+','.join(str(r['right'])+'u' for r in records)+'};\nstatic const unsigned kind[]={'+','.join(str(r['kind']) for r in records)+'};\nstatic const unsigned slot_index[]={'+','.join(str(r['slot']) for r in records)+'};\n')
target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk','iphonesimulator' if a.simulator_device else 'macosx','--show-sdk-path'],text=True).strip();c=R/'tests/import_slot_cmp_test.c';exe=out/'native';subprocess.run(['clang','-target',target,'-isysroot',sdk,'-O2','-Wall','-Wextra','-Werror','-Wno-override-module','-DPTROFS_64BIT','-I',str(out),str(ir),str(c),'-o',str(exe)],check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[pathlib.Path(__file__).resolve(),c,R/'scripts/prepare-va-model.py',R/'port/llasm-support/llasm_cpu.h']+sorted((R/'ref/halopad/port/llasm-support').glob('*.llinc'));report={'pass':True,'state':'finished','cases':len(records)*65536,'blocks':len(records),'records':records,'negative':negative,'import_declarations':declarations,'original_image_sha256':sha(original),'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'relocations_sha256':sha(out/'fixture/relocations.csv'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'simulator_device':a.simulator_device,'output':run.stdout.strip(),'scope':'Original dbghelp PE imports/relocations and CMP/PUSH bytes; emitted ARM with original guest VA model reads mutable guarded IAT slot and independently checks subtraction flags/full CPU and exact stack writes including source/destination overlap. No StackWalk64 implementation, Windows exception delivery, full DLL initialization or gameplay claim.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
