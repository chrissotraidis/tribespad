"""Relocate three pinned, repeatedly updated float immediates to checked reads."""
import hashlib,json,pathlib
import pefile
from renderer_batch_runtime_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ROWS=[(0x10010f23,'loc_10010F1E',0x5edc8d,3205622661,'%8'),
      (0x10010f50,'loc_10010F4B',0x5edc94,1058139013,'%12'),
      (0x10010f7d,'loc_10010F78',0x5edc9b,1058139013,'%16')]
def verify_images(engine=R/'ref/control/Tribes2.exe',qol=R/'ref/qol/IFC22.dll'):
 assert sha(engine)=='7ad1d40a6aa74a25d089af6bd6f34fcc20a172fd5932ded82c69fca673e7f8f9'
 assert sha(qol)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 e=pefile.PE(str(engine));q=pefile.PE(str(qol))
 for (pc,_,address,value,_),displacement in zip(ROWS,[0xcc,0xc4,0xc8]):
  assert q.get_data(pc-0x10000000,8)==bytes.fromhex('f30f1105')+address.to_bytes(4,'little')
  assert e.get_data(address-3-0x400000,7)==bytes([0xc7,0x45,displacement])+value.to_bytes(4,'little')
def reader_body(body):
 # Each original MOV retains its position and its own checked read. Do not
 # take a vector snapshot: independent VirtualProtect branches can skip stores.
 for i,(_,_,address,value,operand) in enumerate(ROWS):
  old=f'store i32 {value}, i32* {operand}, align 1'
  assert body.count(old)==1,old
  new=f'''%literal_address{i} = add i64 %5, {address}
%literal_pointer{i} = inttoptr i64 %literal_address{i} to i32*
%literal_bits{i} = load i32, i32* %literal_pointer{i}, align 1
store i32 %literal_bits{i}, i32* {operand}, align 1'''
  body=body.replace(old,new,1)
 return body
def writer_body(body,pc,address):
 old=f'call void @tb_sse_store(%_cpu* %cpu, i32 0, i32 {address}, i32 0) nounwind'
 assert body.count(old)==1
 return body.replace(old,f'call void @qol_mission_literal_write(%_cpu* %cpu, i32 {pc}, i32 {address})',1)
def prepare(dll,engine,renderer,out,verify_test=True):
 verify_images()
 if verify_test:
  proof=json.loads((R/'generated/integration-review/mission-literals/result.json').read_text())
  assert proof['passed']
  for n,h in proof['sources_sha256'].items():assert sha(R/n)==h,n
 records=[]
 for path,name in [(engine,'tribes_before_renderer_batch_loc_5EDC8A'),(renderer,'tribes_renderer_batch_loc_5EDC8A')]:
  text=path.read_text();body=definition(text,name);new=reader_body(body)
  assert text.count(body)==1
  path.write_text(text.replace(body,new,1));records.append(dict(name=name,before_sha256=hashlib.sha256(body.encode()).hexdigest(),after_sha256=hashlib.sha256(new.encode()).hexdigest()))
 for pc,name,address,_,_ in ROWS:
  body=definition(dll,name);new=writer_body(body,pc,address);assert dll.count(body)==1
  dll=dll.replace(body,new,1)
 dll+='\ndeclare void @qol_mission_literal_write(%_cpu*, i32, i32)\n'
 report=dict(readers=records,writers=[dict(pc=pc,name=name,address=address) for pc,name,address,_,_ in ROWS],script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'mission-literal-inputs.json').write_text(json.dumps(report,indent=2)+'\n')
 return dll,report
