"""Source-bound clock redirect inputs and bounded original-x86 replay."""
import hashlib, json, pathlib, struct
import unicorn
from unicorn import x86_const as X
from console_fade_inputs import definition

R=pathlib.Path(__file__).resolve().parents[1]
FROZEN=R/'generated/integration-review/clock-patch-checkpoint-20261003'
OUT=R/'generated/integration-review/clock-patch-candidate'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
REGS=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
def load():
    checkpoint=json.loads((FROZEN/'checkpoint.json').read_text()); assert checkpoint['passed']
    for r in checkpoint['files']: assert sha(FROZEN/r['path'])==r['sha256'],r['path']
    table=json.loads((FROZEN/'capture/protection-pages.json').read_text())
    pages=[(r,(FROZEN/'capture'/r['file']).read_bytes()) for r in table['ranges']]
    cpu=(FROZEN/'capture/protection-cpu.bin').read_bytes()
    engine=R/'generated/integration-review/string-patched-engine/va/engine.ll'
    dll=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/module.ll'
    assert sha(engine)==json.loads(engine.with_name('result.json').read_text())['engine_ll_sha256']
    assert sha(dll)==json.loads(dll.with_name('result.json').read_text())['module_ll_sha256']
    def read(address,size):
        for row,data in pages:
            start=row['address']
            if start<=address and address+size<=start+len(data):return data[address-start:address-start+size]
        raise AssertionError((hex(address),size))
    return cpu,pages,read,engine,dll

def replay(cpu,pages):
    assert len(cpu)==440
    u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32); protections={}
    for row,data in pages:
        assert len(data)==row['size'];u.mem_map(row['address'],len(data));u.mem_write(row['address'],data)
        for a in range(row['address'],row['address']+len(data),4096):protections[a]=(row['allocation'],row['protect'])
    values=struct.unpack_from('<9I',cpu);assert values[0]==1 and values[7]==(0x1005f0d0-0x58a96a)&0xffffffff
    for reg,value in zip(REGS,values):u.reg_write(reg,value)
    u.mem_map(0xfff00000,4096);u.mem_write(0xfff00390,b'\xcc')
    trace=[];writes=[];services=[]
    def code(machine,address,size,_):
        if address==0xfff00390:
            args=struct.unpack('<5I',machine.mem_read(machine.reg_read(X.UC_X86_REG_ESP),20))
            assert args==(0x10060868,0x50cb13,4,0x40,values[5]-8),args
            old=protections[0x50c000][1]
            assert old==4
            machine.mem_write(args[4],struct.pack('<I',old))
            protections[0x50c000]=(protections[0x50c000][0],0x40)
            services.append(dict(name='VirtualProtect',return_pc=args[0],address=args[1],size=args[2],protection=args[3],output=args[4],old_protection=old,result=1))
            machine.reg_write(X.UC_X86_REG_EAX,1);machine.reg_write(X.UC_X86_REG_ESP,machine.reg_read(X.UC_X86_REG_ESP)+20);machine.reg_write(X.UC_X86_REG_EIP,args[0]);return
        assert 0x10060806<=address<0x1006086e,hex(address)
        trace.append(dict(pc=address,bytes=bytes(machine.mem_read(address,size)).hex()))
    def write(machine,access,address,size,value,_):
        if 0x400000<=address<0xa00000:
            writes.append(dict(pc=machine.reg_read(X.UC_X86_REG_EIP),address=address,size=size,before=bytes(machine.mem_read(address,size)).hex(),after=value.to_bytes(size,'little').hex()))
    u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write)
    u.emu_start(0x10060806,0x1006086e,count=100)
    assert u.reg_read(X.UC_X86_REG_EIP)==0x1006086e and len(services)==1
    assert [(w['pc'],w['address'],w['size']) for w in writes]==[(0x1006080f,0x58a966,4),(0x1006086c,0x50cb13,4)]
    for w,target in zip(writes,[0x1005f0d0,0x1005f0d0]):assert (w['address']+4+int.from_bytes(bytes.fromhex(w['after']),'little'))&0xffffffff==target
    final=bytearray(cpu);struct.pack_into('<9I',final,0,*[u.reg_read(reg) for reg in REGS])
    # The import returns through lookup, without modifying the dispatch mailbox.
    memory={row['address']:bytes(u.mem_read(row['address'],len(data))) for row,data in pages}
    return dict(instructions=trace,writes=writes,services=services),bytes(final),memory,protections
