"""Source-bound next CALL redirect and original four-instruction publication."""
import hashlib,json,pathlib,struct
import unicorn
from unicorn import x86_const as X
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1]
FROZEN=R/'generated/integration-review/post-file-patch-checkpoint-20261003'
OUT=R/'generated/integration-review/post-file-patch-candidate'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
REGS=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
def load():
    checkpoint=json.loads((FROZEN/'checkpoint.json').read_text());assert checkpoint['passed']
    for row in checkpoint['files']:assert sha(FROZEN/row['path'])==row['sha256'],row['path']
    table=json.loads((FROZEN/'capture/post-file-patch-pages.json').read_text())
    pages=[(row,(FROZEN/'capture'/row['file']).read_bytes()) for row in table['ranges']]
    cpu=(FROZEN/'capture/post-file-patch-cpu.bin').read_bytes()
    engine=R/'generated/integration-review/string-patched-engine/va/engine.ll'
    dll=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/module.ll'
    assert sha(engine)==json.loads(engine.with_name('result.json').read_text())['engine_ll_sha256']
    assert sha(dll)==json.loads(dll.with_name('result.json').read_text())['module_ll_sha256']
    def read(address,size):
        for row,data in pages:
            at=row['address']
            if at<=address and address+size<=at+len(data):return data[address-at:address-at+size]
        raise AssertionError((hex(address),size))
    return cpu,pages,read,engine,dll
def replay(cpu,pages):
    assert len(cpu)==440
    u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);permissions={}
    for row,data in pages:
        assert len(data)==row['size'];u.mem_map(row['address'],len(data));u.mem_write(row['address'],data)
        for a in range(row['address'],row['address']+len(data),4096):permissions[a]=(row['allocation'],row['protect'])
    values=struct.unpack_from('<9I',cpu);assert values[0]==1 and values[6]==(0x10015a10-0x5bff7d)&0xffffffff
    for reg,value in zip(REGS,values):u.reg_write(reg,value)
    trace=[];writes=[]
    def code(machine,address,size,_):
        assert 0x10015d0a<=address<0x10015d15,hex(address)
        trace.append(dict(pc=address,bytes=bytes(machine.mem_read(address,size)).hex()))
    def write(machine,access,address,size,value,_):
        writes.append(dict(pc=machine.reg_read(X.UC_X86_REG_EIP),address=address,size=size,before=bytes(machine.mem_read(address,size)).hex(),after=value.to_bytes(size,'little').hex()))
    u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write)
    u.emu_start(0x10015d0a,0x10015d15,count=20)
    assert u.reg_read(X.UC_X86_REG_EIP)==0x10015d15 and len(trace)==4 and len(writes)==1
    assert (writes[0]['pc'],writes[0]['address'],writes[0]['size'])==(0x10015d13,0x5bff79,4)
    final=bytearray(cpu);struct.pack_into('<9I',final,0,*[u.reg_read(reg) for reg in REGS])
    memory={row['address']:bytes(u.mem_read(row['address'],len(data))) for row,data in pages}
    return dict(instructions=trace,writes=writes,services=[]),bytes(final),memory,permissions
